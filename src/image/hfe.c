/*
 * hfe.c
 * 
 * HxC Floppy Emulator (HFE) image files.
 * 
 * Written & released by Keir Fraser <keir.xen@gmail.com>
 * 
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

/* NB. Fields are little endian. */
struct disk_header {
    char sig[8];
    uint8_t formatrevision;
    uint8_t nr_tracks, nr_sides;
    uint8_t track_encoding;
    uint16_t bitrate; /* kB/s, approx */
    uint16_t rpm; /* unused, can be zero */
    uint8_t interface_mode;
    uint8_t rsvd; /* set to 1? */
    uint16_t track_list_offset;
    /* from here can write 0xff to end of block... */
    uint8_t write_allowed;
    uint8_t single_step;
    uint8_t t0s0_altencoding, t0s0_encoding;
    uint8_t t0s1_altencoding, t0s1_encoding;
};

/* track_encoding */
enum {
    ENC_ISOIBM_MFM,
    ENC_Amiga_MFM,
    ENC_ISOIBM_FM,
    ENC_Emu_FM,
    ENC_Unknown = 0xff
};

/* interface_mode */
enum {
    IFM_IBMPC_DD,
    IFM_IBMPC_HD,
    IFM_AtariST_DD,
    IFM_AtariST_HD,
    IFM_Amiga_DD,
    IFM_Amiga_HD,
    IFM_CPC_DD,
    IFM_GenericShugart_DD,
    IFM_IBMPC_ED,
    IFM_MSX2_DD,
    IFM_C64_DD,
    IFM_EmuShugart_DD,
    IFM_S950_DD,
    IFM_S950_HD,
    IFM_Disable = 0xfe
};

struct track_header {
    uint16_t offset;
    uint16_t len;
};

/* HFEv3 opcodes. The 4-bit codes have their bit ordering reversed. */
enum {
    OP_nop = 0,     /* 0: no effect */
    OP_index = 8,   /* 1: index mark */
    OP_bitrate = 4, /* 2: +1byte: new bitrate */
    OP_skip = 12,   /* 3: +1byte: skip 0-8 bits in next byte */
    OP_rand = 2     /* 4: flaky byte */
};

#define MAX_BC_SECS 8

static void hfe_seek_track(struct image *im, uint16_t track, bool_t async);

static bool_t hfe_open(struct image *im)
{
    struct disk_header dhdr;
    uint16_t bitrate;

    F_read(&im->fp, &dhdr, sizeof(dhdr), NULL);
    if (!strncmp(dhdr.sig, "HXCHFEV3", sizeof(dhdr.sig))) {
        if (dhdr.formatrevision > 0)
            return FALSE;
        im->hfe.is_v3 = TRUE;
    } else if (!strncmp(dhdr.sig, "HXCPICFE", sizeof(dhdr.sig))) {
        if (dhdr.formatrevision > 1)
            return FALSE;
        im->hfe.is_v3 = FALSE;
    } else {
        return FALSE;
    }

    /* Sanity-check the header fields. */
    bitrate = le16toh(dhdr.bitrate);
    if ((dhdr.nr_tracks == 0)
        || (dhdr.nr_sides < 1) || (dhdr.nr_sides > 2)
        || (bitrate == 0)) {
        return FALSE;
    }

    im->hfe.double_step = !dhdr.single_step;
    im->hfe.tlut_base = le16toh(dhdr.track_list_offset);
    im->nr_cyls = dhdr.nr_tracks;
    if (im->hfe.double_step)
        im->nr_cyls = min_t(unsigned int, im->nr_cyls*2, 255);
    im->nr_sides = dhdr.nr_sides;
    im->write_bc_ticks = sysclk_us(500) / bitrate;
    im->ticks_per_cell = im->write_bc_ticks * 16;
    im->sync = SYNC_none;

    /* Get an initial value for ticks per revolution. */
    hfe_seek_track(im, 0, FALSE);
    im->cur_track = -1;

    return TRUE;
}

static void hfe_seek_track(struct image *im, uint16_t track, bool_t async)
{
    struct track_header thdr;
    uint16_t trk_off;

    if (async) {
        F_lseek_async(&im->fp, im->hfe.tlut_base*512 + (track/2)*4);
        F_async_wait(F_read_async(&im->fp, &thdr, sizeof(thdr), NULL));
    } else {
        F_lseek(&im->fp, im->hfe.tlut_base*512 + (track/2)*4);
        F_read(&im->fp, &thdr, sizeof(thdr), NULL);
    }

    trk_off = le16toh(thdr.offset);
    im->hfe.trk_len = le16toh(thdr.len) / 2;
    im->tracklen_bc = im->hfe.trk_len * 8;
    im->stk_per_rev = stk_sysclk(im->tracklen_bc * im->write_bc_ticks);

    ring_io_init(&im->hfe.ring_io, &im->fp, &im->bufs.read_data,
            (LBA_t)trk_off * 512, ~0, (im->hfe.trk_len*2 + 511) / 512);
    /* Aggressively batch our reads at HD data rate, as that can be faster
     * than some USB drives will serve up a single block.*/
    im->hfe.ring_io.batch_secs =
        (im->write_bc_ticks > sysclk_ns(1500)) ? 4 : 8;
    im->hfe.ring_io.trailing_secs = MAX_BC_SECS;
}

static void hfe_setup_track(
    struct image *im, uint16_t track, uint32_t *start_pos)
{
    struct image_buf *bc = &im->bufs.read_bc;
    uint32_t sys_ticks;
    uint8_t cyl = track >> (im->hfe.double_step ? 2 : 1);
    uint8_t side = track & (im->nr_sides - 1);

    track = cyl*2 + side;
    if (track/2 != im->cur_track/2) {
        ring_io_sync(&im->hfe.ring_io);
        ring_io_shutdown(&im->hfe.ring_io);

        im->cur_track = track;
        hfe_seek_track(im, track, TRUE);
    } else if (track != im->cur_track) {
        im->cur_track = track;
    }

    /* If track does not fit in memory, now is a good time to flush writes to
     * reduce chances of future buffer underrun caused by a very slow write.
     * However if write-drain=realtime, then any delays cut into reads so we
     * just accept the buffer underrun risk. */
    if ((im->hfe.trk_len*2 + 511) / 512 > im->bufs.read_data.len
            && ff_cfg.write_drain != WDRAIN_realtime)
        ring_io_sync(&im->hfe.ring_io);

    sys_ticks = start_pos ? *start_pos : get_write(im, im->wr_cons)->start;
    im->cur_bc = (sys_ticks * 16) / im->ticks_per_cell;
    if (im->cur_bc >= im->tracklen_bc)
        im->cur_bc = 0;
    im->cur_ticks = im->cur_bc * im->ticks_per_cell;
    im->ticks_since_flux = 0;

    sys_ticks = im->cur_ticks / 16;

    bc->prod = bc->cons = 0;

    if (start_pos) {
        /* Read mode. */
        ring_io_seek(&im->hfe.ring_io, im->cur_bc/8 / 256 * 512, FALSE, FALSE);
        /* Consumer may be ahead of producer, but only until the first read
         * completes. */
        bc->cons = im->cur_bc % (256*8);
        *start_pos = sys_ticks;
    } else {
        uint32_t pos = im->cur_bc / 8 / 256 * 512
                     + im->cur_bc / 8 % 256
                     + (im->cur_track & 1) * 256;
        /* Write mode. */
        ring_io_seek(&im->hfe.ring_io, pos, TRUE, FALSE);
    }
}

static bool_t hfe_read_track(struct image *im)
{
    struct image_buf *rd = &im->bufs.read_data;
    struct image_buf *bc = &im->bufs.read_bc;
    uint8_t *buf = rd->p;
    uint8_t *bc_b = bc->p;
    uint32_t bc_len, bc_mask, bc_space, bc_p, bc_c;
    unsigned int nr_sec;

    ring_io_progress(&im->hfe.ring_io);
    if (rd->cons >= rd->prod)
        return FALSE;

    /* Fill the raw-bitcell ring buffer. */
    bc_p = bc->prod / 8;
    bc_c = bc->cons / 8;
    bc_len = bc->len;
    bc_mask = bc_len - 1;
    bc_space = min_t(uint32_t, bc_len, MAX_BC_SECS*256)
        - (int16_t)(bc_p - bc_c);

    nr_sec = min_t(unsigned int, (rd->prod - rd->cons)/512, bc_space/256);
    if (nr_sec == 0)
        return FALSE;

    while (nr_sec--) {
        uint32_t cons = rd->cons + (im->cur_track&1)*256;
        memcpy(&bc_b[bc_p & bc_mask],
               &buf[ring_io_idx(&im->hfe.ring_io, cons)],
               256);
        rd->cons += 512;
        bc_p += 256;
    }

    barrier();
    bc->prod = bc_p * 8;

    return TRUE;
}

static uint16_t hfe_rdata_flux(struct image *im, uint16_t *tbuf, uint16_t nr)
{
    struct image_buf *bc = &im->bufs.read_bc;
    uint8_t *bc_b = bc->p;
    uint32_t bc_c = bc->cons, bc_p = bc->prod, bc_mask = bc->len - 1;
    uint32_t ticks = im->ticks_since_flux;
    uint32_t ticks_per_cell = im->ticks_per_cell;
    uint32_t y = 8, todo = nr;
    uint8_t x;
    bool_t is_v3 = im->hfe.is_v3;

    while ((int32_t)(bc_p - bc_c) >= 3*8) {
        ASSERT(y == 8);
        if (im->cur_bc >= im->tracklen_bc) {
            //ASSERT(im->cur_bc == im->tracklen_bc);
            im->tracklen_ticks = im->cur_ticks;
            im->cur_bc = im->cur_ticks = 0;
            /* Skip tail of current 256-byte block. */
            bc_c = (bc_c + 256*8-1) & ~(256*8-1);
            continue;
        }
        y = bc_c % 8;
        x = bc_b[(bc_c/8) & bc_mask] >> y;
        if (is_v3 && (y == 0) && ((x & 0xf) == 0xf)) {
            /* V3 byte-aligned opcode processing. */
            switch (x >> 4) {
            case OP_nop:
            case OP_index:
            default:
                bc_c += 8;
                im->cur_bc += 8;
                y = 8;
                continue;
            case OP_bitrate:
                x = _rbit32(bc_b[(bc_c/8+1) & bc_mask]) >> 24;
                im->ticks_per_cell = ticks_per_cell = 
                    (sysclk_us(2) * 16 * x) / 72;
                bc_c += 2*8;
                im->cur_bc += 2*8;
                y = 8;
                continue;
            case OP_skip:
                x = (_rbit32(bc_b[(bc_c/8+1) & bc_mask]) >> 24) & 7;
                bc_c += 2*8 + x;
                im->cur_bc += 2*8 + x;
                y = x;
                x = bc_b[(bc_c/8) & bc_mask] >> y;
                break;
            case OP_rand:
                bc_c += 8;
                im->cur_bc += 8;
                x = rand();
                break;
            }
        }
        bc_c += 8 - y;
        im->cur_bc += 8 - y;
        im->cur_ticks += (8 - y) * ticks_per_cell;
        while (y < 8) {
            y++;
            ticks += ticks_per_cell;
            if (x & 1) {
                *tbuf++ = (ticks >> 4) - 1;
                ticks &= 15;
                if (!--todo)
                    goto out;
            }
            x >>= 1;
        }
    }

out:
    bc->cons = bc_c - (8 - y);
    im->cur_bc -= 8 - y;
    im->cur_ticks -= (8 - y) * ticks_per_cell;
    im->ticks_since_flux = ticks;
    return nr - todo;
}

static bool_t hfe_write_track(struct image *im)
{
    bool_t flush;
    struct write *write = get_write(im, im->wr_cons);
    struct image_buf *wr = &im->bufs.write_bc;
    uint8_t *buf = wr->p;
    unsigned int bufmask = wr->len - 1;
    uint8_t *w;
    struct image_buf *rd = &im->bufs.read_data;
    uint32_t i, space, c = wr->cons / 8, p = wr->prod / 8;

    /* If we are processing final data then use the end index, rounded to
     * nearest. */
    barrier();
    flush = (im->wr_cons != im->wr_bc);
    if (flush)
        p = (write->bc_end + 4) / 8;

    for (;;) {

        uint32_t pos = ring_io_pos(&im->hfe.ring_io, rd->cons);
        UINT nr;

        /* All bytes remaining in the raw-bitcell buffer. */
        nr = space = (p - c) & bufmask;
        /* Limit to end of current 256-byte HFE block. */
        nr = min_t(UINT, nr, 256 - (pos & 255));
        /* Limit to end of HFE track. */
        nr = min_t(UINT, nr, im->hfe.trk_len - pos / 512 * 256 - pos % 256);

        /* Bail if no bytes to write. */
        if (nr == 0)
            break;

        /* It should be quite rare to wait on the read, as that'd be like a
         * buffer underrun during normal reading. */
        if (rd->cons + nr > rd->prod) {
            flush = FALSE;
            break;
        }

        /* Encode into the sector buffer for later write-out. */
        w = rd->p + ring_io_idx(&im->hfe.ring_io, rd->cons);
        for (i = 0; i < nr; i++)
            *w++ = _rbit32(buf[c++ & bufmask]) >> 24;

        rd->cons += nr;
        /* Stay aligned to track side. */
        if (rd->cons % 256 == 0)
            rd->cons += 256;
    }

    if (flush)
        ring_io_flush(&im->hfe.ring_io);
    else
        ring_io_progress(&im->hfe.ring_io);

    wr->cons = c * 8;

    return flush;
}

static void hfe_sync(struct image *im)
{
    ring_io_sync(&im->hfe.ring_io);
    ring_io_shutdown(&im->hfe.ring_io);
}

const struct image_handler hfe_image_handler = {
    .open = hfe_open,
    .setup_track = hfe_setup_track,
    .read_track = hfe_read_track,
    .rdata_flux = hfe_rdata_flux,
    .write_track = hfe_write_track,
    .sync = hfe_sync,

    .async = TRUE,
};

/*
 * Local variables:
 * mode: C
 * c-file-style: "Linux"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */

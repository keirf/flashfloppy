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

/* HFEv3 opcodes. Bit order is reversed to match raw HFE bit order. */
enum {
    OP_Nop      = 0x0f, /* Nop */
    OP_Index    = 0x8f, /* Index mark */
    OP_Bitrate  = 0x4f, /* +1 byte: new bitrate */
    OP_SkipBits = 0xcf, /* +1 byte: skip 1-7 bits in following byte */
    OP_Rand     = 0x2f  /* Random byte (or bits, if following OP_skip) */
};

static void hfe_seek_track(struct image *im, uint16_t track);

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
    im->step = im->hfe.double_step ? 2 : 1;
    im->nr_sides = dhdr.nr_sides;
    im->write_bc_ticks = sampleclk_us(500) / bitrate;
    im->ticks_per_cell = im->write_bc_ticks * 16;
    im->sync = SYNC_none;

    ASSERT(8*512 <= im->bufs.read_data.len);
    volume_cache_init(im->bufs.read_data.p + 8*512,
                      im->bufs.read_data.p + im->bufs.read_data.len);
    if (im->bufs.read_data.len < (64*1024))
        volume_cache_metadata_only(&im->fp);

    /* Get an initial value for ticks per revolution. */
    hfe_seek_track(im, 0);

    return TRUE;
}

static void hfe_seek_track(struct image *im, uint16_t track)
{
    struct track_header thdr;

    F_lseek(&im->fp, im->hfe.tlut_base*512 + (track/2)*4);
    F_read(&im->fp, &thdr, sizeof(thdr), NULL);

    im->hfe.trk_off = le16toh(thdr.offset);
    im->hfe.trk_len = le16toh(thdr.len) / 2;
    im->tracklen_bc = im->hfe.trk_len * 8;
    if (im->hfe.is_v3 && im->tracklen_ticks) {
        /* Opcodes in v3 make it difficult to predict the track's length. Keep
         * the previous track's value since this isn't the first seek. */
    } else {
        im->tracklen_ticks = im->tracklen_bc * im->ticks_per_cell;
        im->stk_per_rev = stk_sampleclk(im->tracklen_ticks / 16);
    }

    im->cur_track = track;
}

static void hfe_setup_track(
    struct image *im, uint16_t track, uint32_t *start_pos)
{
    struct image_buf *rd = &im->bufs.read_data;
    struct image_buf *bc = &im->bufs.read_bc;
    uint32_t start_ticks;
    uint8_t cyl = track >> (im->hfe.double_step ? 2 : 1);
    uint8_t side = track & (im->nr_sides - 1);

    track = cyl*2 + side;
    if (track != im->cur_track)
        hfe_seek_track(im, track);

    start_ticks = start_pos ? *start_pos : get_write(im, im->wr_cons)->start;

    im->cur_ticks = start_ticks * 16;
    im->cur_bc = udiv64((uint64_t)im->cur_ticks * im->tracklen_bc,
                        im->tracklen_ticks);
    if ((im->cur_ticks >= im->tracklen_ticks) ||
        (im->cur_bc >= im->tracklen_bc)) {
        im->cur_ticks = 0;
        im->cur_bc = 0;
    }
    im->ticks_since_flux = 0;

    rd->prod = rd->cons = 0;
    bc->prod = bc->cons = 0;

    /* Aggressively batch our reads at HD data rate, as that can be faster 
     * than some USB drives will serve up a single block.*/
    im->hfe.batch_secs = (im->write_bc_ticks > sampleclk_ns(1500)) ? 2 : 8;

    if (start_pos) {
        /* Read mode. */
        im->hfe.trk_pos = (im->cur_bc/8) & ~255;
        image_read_track(im);
        bc->cons = im->cur_bc & 2047;
    } else {
        /* Write mode. */
        im->hfe.trk_pos = im->cur_bc / 8;
        if (im->hfe.is_v3) {
            /* Provide context to the write to avoid corrupting an opcode. */
            if ((im->hfe.trk_pos & 255) == 0 && im->hfe.trk_pos != 0)
                im->hfe.trk_pos--;
            else if ((im->hfe.trk_pos & 255) == 1)
                im->hfe.trk_pos = (im->hfe.trk_pos+1) % im->hfe.trk_len;
        }
        im->hfe.write.start = im->hfe.trk_pos;
        im->hfe.write.wrapped = FALSE;
        im->hfe.write_batch.len = 0;
        im->hfe.write_batch.dirty = FALSE;
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

    if (rd->prod == rd->cons) {
        nr_sec = min_t(unsigned int, im->hfe.batch_secs,
                       (im->hfe.trk_len+255 - im->hfe.trk_pos) / 256);
        F_lseek(&im->fp, im->hfe.trk_off * 512 + im->hfe.trk_pos * 2);
        F_read(&im->fp, buf, nr_sec*512, NULL);
        rd->cons = 0;
        rd->prod = nr_sec;
        im->hfe.trk_pos += nr_sec * 256;
        if (im->hfe.trk_pos >= im->hfe.trk_len)
            im->hfe.trk_pos = 0;
    }

    /* Fill the raw-bitcell ring buffer. */
    bc_p = bc->prod / 8;
    bc_c = bc->cons / 8;
    bc_len = bc->len;
    bc_mask = bc_len - 1;
    bc_space = bc_len - (uint16_t)(bc_p - bc_c);

    nr_sec = min_t(unsigned int, rd->prod - rd->cons, bc_space/256);
    if (nr_sec == 0)
        return FALSE;

    while (nr_sec--) {
        memcpy(&bc_b[bc_p & bc_mask],
               &buf[rd->cons*512 + (im->cur_track&1)*256],
               256);
        rd->cons++;
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
    uint32_t bit_off, todo = nr;
    uint8_t x;
    bool_t is_v3 = im->hfe.is_v3;

    while ((uint32_t)(bc_p - bc_c) >= 3*8) {

        if (im->cur_bc >= im->tracklen_bc) {
            /* Malformed HFE v3 file can trigger this assertion. Requires a
             * multi-byte opcode which extends beyond reported track length. */
            ASSERT(im->cur_bc == im->tracklen_bc);
            im->tracklen_ticks = im->cur_ticks;
            im->stk_per_rev = stk_sampleclk(im->tracklen_ticks / 16);
            im->cur_bc = im->cur_ticks = 0;
            /* Skip tail of current 256-byte block. */
            bc_c = (bc_c + 256*8-1) & ~(256*8-1);
            continue;
        }

        bit_off = bc_c % 8;
        x = bc_b[(bc_c/8) & bc_mask];
        bc_c += 8 - bit_off;
        im->cur_bc += 8 - bit_off;

        if (is_v3 && ((x & 0xf) == 0xf)) {
            /* V3 byte-aligned opcode processing. */
            switch (x) {
            case OP_Nop:
            case OP_Index:
            default:
                continue;
            case OP_Bitrate:
                x = _rbit32(bc_b[(bc_c/8) & bc_mask]) >> 24;
                im->ticks_per_cell = ticks_per_cell = 
                    (sampleclk_us(2) * 16 * x) / 72;
                im->write_bc_ticks = ticks_per_cell / 16;
                bc_c += 8;
                im->cur_bc += 8;
                continue;
            case OP_SkipBits:
                x = (_rbit32(bc_b[(bc_c/8) & bc_mask]) >> 24) & 7;
                bc_c += 8 + x;
                im->cur_bc += 8 + x;
                continue;
            case OP_Rand:
                x = rand();
                break;
            }
        }

        x >>= bit_off;
        im->cur_ticks += (8 - bit_off) * ticks_per_cell;
        while (bit_off < 8) {
            bit_off++;
            ticks += ticks_per_cell;
            if (x & 1) {
                *tbuf++ = (ticks >> 4) - 1;
                ticks &= 15;
                if (!--todo) {
                    bc_c -= 8 - bit_off;
                    im->cur_bc -= 8 - bit_off;
                    im->cur_ticks -= (8 - bit_off) * ticks_per_cell;
                    goto out;
                }
            }
            x >>= 1;
        }

        /* Subdivide a long flux gap to avoid overflowing the 16-bit timer.
         * This mishandles long No Flux Areas slightly, by regularly emitting
         * a flux-reversal pulse every 2^14 sampleclk ticks. */
        if (unlikely((ticks >> (15+4)) != 0)) {
            *tbuf++ = (1u << 14) - 1;
            ticks -= 1u << (14+4);
            if (!--todo)
                goto out;
        }

    }

out:
    bc->cons = bc_c;
    im->ticks_since_flux = ticks;
    return nr - todo;
}

static bool_t hfe_write_track(struct image *im)
{
    const unsigned int batch_secs = 8;
    bool_t flush;
    struct write *write = get_write(im, im->wr_cons);
    struct image_buf *wr = &im->bufs.write_bc;
    uint8_t *buf = wr->p;
    unsigned int bufmask = wr->len - 1;
    uint8_t *w, *wrbuf = im->bufs.write_data.p;
    uint32_t i, space, c = wr->cons / 8, p = wr->prod / 8;
    bool_t is_v3 = im->hfe.is_v3;
    bool_t writeback = FALSE;
    time_t t;

    /* If we are processing final data then use the end index, rounded to
     * nearest. */
    barrier();
    flush = (im->wr_cons != im->wr_bc);
    if (flush)
        p = (write->bc_end + 4) / 8;

    if (im->hfe.write_batch.len == 0) {
        ASSERT(!im->hfe.write_batch.dirty);
        im->hfe.write_batch.off = (im->hfe.trk_pos & ~255) << 1;
        im->hfe.write_batch.len = min_t(
            uint32_t, batch_secs * 512,
            (((im->hfe.trk_len * 2) + 511) & ~511) - im->hfe.write_batch.off);
        F_lseek(&im->fp, im->hfe.trk_off * 512 + im->hfe.write_batch.off);
        F_read(&im->fp, wrbuf, im->hfe.write_batch.len, NULL);
        F_lseek(&im->fp, im->hfe.trk_off * 512 + im->hfe.write_batch.off);
    }

    for (;;) {

        uint32_t batch_off, off = im->hfe.trk_pos;
        UINT nr;

        /* All bytes remaining in the raw-bitcell buffer. */
        nr = space = (p - c) & bufmask;
        /* Limit to end of current 256-byte HFE block. */
        nr = min_t(UINT, nr, 256 - (off & 255));
        /* Limit to end of HFE track. */
        nr = min_t(UINT, nr, im->hfe.trk_len - off);

        /* Bail if no bytes to write. */
        if (nr == 0)
            break;

        /* Bail if required data not in the write buffer. */
        batch_off = (off & ~255) << 1; 
        if ((batch_off < im->hfe.write_batch.off)
            || (batch_off >= (im->hfe.write_batch.off
                              + im->hfe.write_batch.len))) {
            writeback = TRUE;
            break;
        }

        /* Encode into the sector buffer for later write-out. */
        w = wrbuf
            + (im->cur_track & 1) * 256
            + batch_off - im->hfe.write_batch.off
            + (off & 255);

        i = 0;

        if (is_v3 && off == im->hfe.write.start && off != 0) {
            /* Avoid starting write in the middle of an opcode. */
            if (w[-2] == OP_SkipBits) {
                i++;
            } else {
                switch (w[-1]) {
                case OP_SkipBits:
                    i += 2;
                    break;
                case OP_Bitrate:
                    i++;
                    break;
                default:
                    break;
                }
            }
        }

        while (i < nr) {
            if (is_v3 && (w[i] & 0xf) == 0xf) {
                switch (w[i]) {
                case OP_SkipBits:
                    /* Keep the write byte-aligned. This changes the length of
                     * the track by 8+skip bitcells, but overwriting OP_SkipBits
                     * should be rare. */
                    w[i++] = OP_Nop;
                    continue;

                case OP_Bitrate:
                    /* Assume bitrate does not change significantly for the
                     * entire track, and write_bc_ticks already adjusted when
                     * reading. */
                    i += 2;
                    continue;

                case OP_Nop:
                case OP_Index:
                default:
                    /* Preserve opcode. But making sure not to write past end of
                     * buffer. */
                    i++;
                    continue;

                case OP_Rand:
                    /* Replace with data. */
                    break;
                }
            }
            w[i++] = _rbit32(buf[c++ & bufmask]) >> 24;
        }
        im->hfe.write_batch.dirty = TRUE;

        im->hfe.trk_pos += i; /* i may be larger than nr due to opcodes. */
        if (im->hfe.trk_pos >= im->hfe.trk_len) {
            ASSERT(im->hfe.trk_pos == im->hfe.trk_len);
            im->hfe.trk_pos = 0;
            im->hfe.write.wrapped = TRUE;
        }
    }

    if (writeback) {
        /* If writeback requested then ensure we get called again. */
        flush = FALSE;
    } else if (flush) {
        /* If this is the final call, we should do writeback. */
        writeback = TRUE;
    }

    if (writeback && im->hfe.write_batch.dirty) {
        t = time_now();
        printk("Write %u-%u (%u)... ",
               im->hfe.write_batch.off,
               im->hfe.write_batch.off + im->hfe.write_batch.len - 1,
               im->hfe.write_batch.len);
        F_write(&im->fp, wrbuf, im->hfe.write_batch.len, NULL);
        printk("%u us\n", time_diff(t, time_now()) / TIME_MHZ);
        im->hfe.write_batch.len = 0;
        im->hfe.write_batch.dirty = FALSE;
    }

    if (flush && im->hfe.write.wrapped
        && (im->hfe.trk_pos > im->hfe.write.start))
        printk("Wrapped (%u > %u)\n", im->hfe.trk_pos, im->hfe.write.start);

    wr->cons = c * 8;

    return flush;
}

const struct image_handler hfe_image_handler = {
    .open = hfe_open,
    .setup_track = hfe_setup_track,
    .read_track = hfe_read_track,
    .rdata_flux = hfe_rdata_flux,
    .write_track = hfe_write_track,
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

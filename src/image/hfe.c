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

static bool_t hfe_open(struct image *im)
{
    struct disk_header dhdr;
    UINT nr;

    im->fr = f_read(&im->fp, &dhdr, sizeof(dhdr), &nr);
    if (im->fr
        || strncmp(dhdr.sig, "HXCPICFE", sizeof(dhdr.sig))
        || (dhdr.formatrevision != 0))
        return FALSE;

    im->hfe.tlut_base = le16toh(dhdr.track_list_offset);
    im->nr_tracks = dhdr.nr_tracks * 2;

    return TRUE;
}

static bool_t hfe_seek_track(struct image *im, uint8_t track,
                             stk_time_t *ptime_after_index)
{
    uint32_t ticks_after_index = *ptime_after_index;
    struct track_header thdr;
    UINT nr;

    /* TODO: Fake out unformatted tracks. */
    track = min_t(uint8_t, track, im->nr_tracks-1);

    if ((im->fr = f_lseek(&im->fp, im->hfe.tlut_base*512 + (track/2)*4))
        || (im->fr = f_read(&im->fp, &thdr, sizeof(thdr), &nr)))
        return FALSE;

    im->hfe.trk_off = le16toh(thdr.offset);
    im->hfe.trk_len = le16toh(thdr.len) / 2;
    im->tracklen_bc = im->hfe.trk_len * 8;
    im->hfe.ticks_per_cell = ((sysclk_ms(DRIVE_MS_PER_REV) * 16u)
                              / im->tracklen_bc);
    im->ticks_since_flux = 0;
    im->cur_track = track;

    im->cur_bc = ((ticks_after_index*(16*SYSCLK_MHZ/STK_MHZ))
                  / im->hfe.ticks_per_cell);
    im->cur_bc &= ~7;
    if (im->cur_bc >= im->tracklen_bc)
        im->cur_bc = 0;
    im->cur_ticks = im->cur_bc * im->hfe.ticks_per_cell;

    ticks_after_index = im->cur_ticks / (16*SYSCLK_MHZ/STK_MHZ);

    im->hfe.trk_pos = (im->cur_bc/8) & ~255;
    im->prod = im->cons = 0;
    image_prefetch_data(im);
    im->cons = im->cur_bc & 2047;

    *ptime_after_index = ticks_after_index;
    return TRUE;
}

static void hfe_prefetch_data(struct image *im)
{
    UINT nr;
    uint8_t *buf = (uint8_t *)im->buf;

    if ((uint32_t)(im->prod - im->cons) > (sizeof(im->buf)-256)*8)
        return;

    f_lseek(&im->fp,
            im->hfe.trk_off * 512
            + (im->cur_track & 1) * 256
            + ((im->hfe.trk_pos & ~255) << 1)
            + (im->hfe.trk_pos & 255));
    f_read(&im->fp, &buf[(im->prod/8) % sizeof(im->buf)], 256, &nr);
    ASSERT(nr == 256);
    im->prod += nr * 8;
    im->hfe.trk_pos += nr;
    if (im->hfe.trk_pos >= im->hfe.trk_len)
        im->hfe.trk_pos = 0;
}

static uint16_t hfe_load_flux(struct image *im, uint16_t *tbuf, uint16_t nr)
{
    uint32_t ticks = im->ticks_since_flux;
    uint32_t ticks_per_cell = im->hfe.ticks_per_cell;
    uint32_t y = 8, todo = nr;
    uint8_t x, *buf = (uint8_t *)im->buf;

    while (im->cons != im->prod) {
        ASSERT(y == 8);
        if (im->cur_bc >= im->tracklen_bc) {
            ASSERT(im->cur_bc == im->tracklen_bc);
            im->tracklen_ticks = im->cur_ticks;
            im->cur_bc = im->cur_ticks = 0;
            /* Skip tail of current 256-byte block. */
            im->cons = (im->cons + 256*8-1) & ~(256*8-1);
        }
        y = im->cons % 8;
        x = buf[(im->cons/8) % sizeof(im->buf)] >> y;
        im->cons += 8 - y;
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
    im->cons -= 8 - y;
    im->cur_bc -= 8 - y;
    im->cur_ticks -= (8 - y) * ticks_per_cell;
    im->ticks_since_flux = ticks;
    return nr - todo;
}

struct image_handler hfe_image_handler = {
    .open = hfe_open,
    .seek_track = hfe_seek_track,
    .prefetch_data = hfe_prefetch_data,
    .load_flux = hfe_load_flux
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

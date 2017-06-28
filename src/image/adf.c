/*
 * adf.c
 * 
 * Amiga Disk File (ADF) files.
 * 
 * Written & released by Keir Fraser <keir.xen@gmail.com>
 * 
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

#define TRACKS_PER_DISK 160
#define BYTES_PER_TRACK 11*512
#define TRACKLEN_BC 100160 /* multiple of 32 */
#define TICKS_PER_CELL ((sysclk_ms(DRIVE_MS_PER_REV) * 16u) / TRACKLEN_BC)

/* Shift even/odd bits into MFM data-bit positions */
#define even(x) ((x)>>1)
#define odd(x) (x)

/* Generate clock bits for given data bits and insert in MFM buffer. */
static void gen_mfm(struct image *im, unsigned int i, uint32_t y)
{
    uint32_t x = im->adf.mfm[(i-1)&(ARRAY_SIZE(im->adf.mfm)-1)];
    y &= 0x55555555u; /* data bits */
    x = ~((x<<30)|(y>>2)|y) & 0x55555555u; /* clock bits */
    im->adf.mfm[i] = y | (x<<1);
}

static uint32_t amigados_checksum(void *dat, unsigned int bytes)
{
    uint32_t *p = dat, csum = 0;
    unsigned int i;
    for (i = 0; i < bytes/4; i++)
        csum ^= be32toh(p[i]);
    csum ^= csum >> 1;
    csum &= 0x55555555u;
    return csum;
}

static bool_t adf_open(struct image *im)
{
    if (f_size(&im->fp) != BYTES_PER_TRACK*TRACKS_PER_DISK)
        return FALSE;

    im->nr_tracks = TRACKS_PER_DISK;

    return TRUE;
}

static bool_t adf_seek_track(struct image *im, uint8_t track,
                             stk_time_t *ptime_after_index)
{
    uint32_t sector, ticks_after_index = *ptime_after_index;

    /* TODO: Fake out unformatted tracks. */
    track = min_t(uint8_t, track, im->nr_tracks-1);

    im->adf.trk_off = track * BYTES_PER_TRACK;
    im->adf.trk_len = BYTES_PER_TRACK;
    im->adf.mfm_cons = 512;
    im->tracklen_bc = TRACKLEN_BC;
    im->ticks_since_flux = 0;
    im->cur_track = track;

    im->cur_bc = (ticks_after_index*(16*SYSCLK_MHZ/STK_MHZ)) / TICKS_PER_CELL;
    im->cur_bc &= ~511;
    if (im->cur_bc >= im->tracklen_bc)
        im->cur_bc = 0;
    im->cur_ticks = im->cur_bc * TICKS_PER_CELL;

    ticks_after_index = im->cur_ticks / (16*SYSCLK_MHZ/STK_MHZ);

    sector = (im->cur_bc - 1024) / (544*16);
    im->adf.trk_pos = (sector < 11) ? sector * 512 : 0;
    im->prod = im->cons = 0;
    image_prefetch_data(im);

    *ptime_after_index = ticks_after_index;
    return TRUE;
}

static bool_t adf_prefetch_data(struct image *im)
{
    const UINT nr = 512;
    uint8_t *buf = (uint8_t *)im->buf;

    if ((uint32_t)(im->prod - im->cons) > (sizeof(im->buf)-512)*8)
        return FALSE;

    F_lseek(&im->fp, im->adf.trk_off + im->adf.trk_pos);
    F_read(&im->fp, &buf[(im->prod/8) % sizeof(im->buf)], nr, NULL);
    im->prod += nr * 8;
    im->adf.trk_pos += nr;
    if (im->adf.trk_pos >= im->adf.trk_len)
        im->adf.trk_pos = 0;

    return TRUE;
}

static uint16_t adf_load_flux(struct image *im, uint16_t *tbuf, uint16_t nr)
{
    uint32_t ticks = im->ticks_since_flux, ticks_per_cell = TICKS_PER_CELL;
    uint32_t info, csum, i, x, y = 32, todo = nr, sector, sec_offset;

    for (;;) {
        /* Convert pre-generated MFM into flux timings. */
        while (im->adf.mfm_cons != 512) {
            y = im->adf.mfm_cons % 32;
            x = im->adf.mfm[im->adf.mfm_cons/32] << y;
            im->adf.mfm_cons += 32 - y;
            im->cur_bc += 32 - y;
            im->cur_ticks += (32 - y) * ticks_per_cell;
            while (y < 32) {
                y++;
                ticks += ticks_per_cell;
                if ((int32_t)x < 0) {
                    *tbuf++ = (ticks >> 4) - 1;
                    ticks &= 15;
                    if (!--todo)
                        goto out;
                }
                x <<= 1;
            }
        }

        ASSERT(y == 32);
        if (im->cur_bc >= im->tracklen_bc) {
            ASSERT(im->cur_bc == im->tracklen_bc);
            im->tracklen_ticks = im->cur_ticks;
            im->cur_bc = im->cur_ticks = 0;
        }

        /* We need more MFM: ensure we have buffered data to convert. */
        if (im->cons == im->prod)
            goto out;
        im->adf.mfm_cons = 0;

        /* Generate MFM in a small holding buffer. */
        sector = im->cur_bc - 1024;
        sec_offset = sector % (544*16);
        sector = sector / (544*16);
        if (sector >= 11) {
            /* Track gap */
            for (i = 0; i < ARRAY_SIZE(im->adf.mfm); i++)
                gen_mfm(im, i, 0);
            x = im->tracklen_bc - im->cur_bc;
            if (x < 512) {
                im->adf.mfm_cons = 512-x;
                /* Copy first clock bit to the correct place. */
                ASSERT(!(im->adf.mfm_cons & 31));
                im->adf.mfm[im->adf.mfm_cons/32] = im->adf.mfm[0];
                /* Fake a write splice at the index. */
                im->adf.mfm[15] &= ~0xf;
            }
        } else if (sec_offset == 0) {
            /* Sector header */
            /* sector gap */
            gen_mfm(im, 0, 0);
            /* sync */
            im->adf.mfm[1] = 0x44894489;
            /* info word */
            info = ((0xff << 24)
                    | (im->cur_track << 16)
                    | (sector << 8)
                    | (11 - sector));
            gen_mfm(im, 2, even(info));
            gen_mfm(im, 3, odd(info));
            /* label */
            for (i = 0; i < 8; i++)
                gen_mfm(im, 4+i, 0);
            /* header checksum */
            csum = info ^ (info >> 1);
            gen_mfm(im, 12, 0);
            gen_mfm(im, 13, odd(csum));
            /* data checksum */
            csum = amigados_checksum(
                &im->buf[(im->cons/32) % ARRAY_SIZE(im->buf)], 512);
            gen_mfm(im, 14, 0);
            gen_mfm(im, 15, odd(csum));
        } else {
            /* Sector data */
            sec_offset -= 512;
            for (i = 0; i < 16; i++) {
                x = im->buf[((im->cons + (sec_offset & 0xfff)) / 32 + i)
                            % ARRAY_SIZE(im->buf)];
                if (!(sec_offset & 0x1000)) x >>= 1; /* even then odd */
                gen_mfm(im, i, be32toh(x));
            }
            /* Finished with this sector's data? Then mark it consumed. */
            if ((sec_offset + 512) == 8192)
                im->cons += 512*8;
        }
    }

out:
    im->adf.mfm_cons -= 32 - y;
    im->cur_bc -= 32 - y;
    im->cur_ticks -= (32 - y) * ticks_per_cell;
    im->ticks_since_flux = ticks;
    return nr - todo;
}

struct image_handler adf_image_handler = {
    .open = adf_open,
    .seek_track = adf_seek_track,
    .prefetch_data = adf_prefetch_data,
    .load_flux = adf_load_flux
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

/*
 * img.c
 * 
 * IBM sector image (IMG) and Atari ST sector image (ST) files.
 * 
 * Written & released by Keir Fraser <keir.xen@gmail.com>
 * 
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

#define GAP_1    50 /* Post-IAM */
#define GAP_2    22 /* Post-IDAM */
#define GAP_4A   80 /* Post-Index */
#define GAP_SYNC 12

const unsigned int sec_sz = 512;

const static struct img_type {
    uint8_t nr_secs:6;
    uint8_t nr_sides:2;
    uint8_t gap3;
    uint8_t interleave;
} img_type[] = {
    {  9, 1, 84, 1 },
    { 10, 1, 30, 1 },
    { 11, 1,  3, 2 },
    {  8, 2, 84, 1 },
    {  9, 2, 84, 1 },
    { 10, 2, 30, 1 },
    { 11, 2,  3, 2 },
    { 18, 2, 84, 1 },
    { 19, 2, 70, 1 },
    { 21, 2, 18, 1 },
    { 20, 2, 40, 1 },
    { 36, 2, 84, 1 }
};

static bool_t _img_open(struct image *im, bool_t has_iam)
{
    const struct img_type *type;
    unsigned int i, nr_cyls, cyl_sz;
    uint32_t tracklen;

    /* Walk the layout/type hints looking for a match on file size. */
    for (i = 0; i < ARRAY_SIZE(img_type); i++) {
        type = &img_type[i];
        cyl_sz = type->nr_secs * sec_sz * type->nr_sides;
        for (nr_cyls = 77; nr_cyls <= 85; nr_cyls++)
            if ((nr_cyls * cyl_sz) == f_size(&im->fp))
                goto found;
    }

    return FALSE;

found:
    im->nr_cyls = nr_cyls;
    im->nr_sides = type->nr_sides;

    im->img.sec_base = 1;
    im->img.nr_sectors = type->nr_secs;
    for (i = 0; i < im->img.nr_sectors; i++) {
        /* Create logical sector map in rotational order. */
        im->img.sec_map[(i * type->interleave) % im->img.nr_sectors]
            = i + im->img.sec_base;
    }
    im->img.gap3 = type->gap3;
    im->img.has_iam = has_iam;
    im->img.idx_sz = GAP_4A;
    if (im->img.has_iam)
        im->img.idx_sz += GAP_SYNC + 4 + GAP_1;
    im->img.idam_sz = GAP_SYNC + 8 + 2 + GAP_2;
    im->img.dam_sz = GAP_SYNC + 4 + sec_sz + 2 + im->img.gap3;

    /* Work out minimum track length (with no pre-index track gap). */
    tracklen = (im->img.idam_sz + im->img.dam_sz) * im->img.nr_sectors;
    tracklen += im->img.idx_sz;
    tracklen *= 16;

    /* Infer the data rate and hence the standard track length. */
    im->img.data_rate = (tracklen < 55000) ? 250 /* SD */
        : (tracklen < 105000) ? 500 /* DD */
        : (tracklen < 205000) ? 1000 /* HD */
        : 2000; /* ED */
    im->tracklen_bc = im->img.data_rate * 200;

    /* Extend the track length if it's too short, and round it up. */
    if (im->tracklen_bc < tracklen)
        im->tracklen_bc += 5000;
    im->tracklen_bc = (im->tracklen_bc + 31) & ~31;

    im->img.ticks_per_cell = ((sysclk_ms(DRIVE_MS_PER_REV) * 16u)
                              / im->tracklen_bc);
    im->img.gap4 = (im->tracklen_bc - tracklen) / 16;

    im->write_bc_ticks = sysclk_ms(1) / im->img.data_rate;

    return TRUE;
}

static bool_t img_open(struct image *im)
{
    return _img_open(im, TRUE);
}

static bool_t st_open(struct image *im)
{
    return _img_open(im, FALSE);
}

static bool_t img_seek_track(
    struct image *im, uint16_t track, stk_time_t *start_pos)
{
    struct image_buf *rd = &im->bufs.read_data;
    struct image_buf *mfm = &im->bufs.read_mfm;
    uint32_t decode_off, sys_ticks = start_pos ? *start_pos : 0;
    uint8_t cyl = track/2, side = track&1;

    /* TODO: Fake out unformatted tracks. */
    cyl = min_t(uint8_t, cyl, im->nr_cyls-1);
    side = min_t(uint8_t, side, im->nr_sides-1);
    track = cyl*2 + side;

    im->img.write_sector = -1;
    im->img.trk_len = im->img.nr_sectors * sec_sz;
    im->img.trk_off = ((uint32_t)cyl * im->nr_sides + side) * im->img.trk_len;
    im->img.trk_pos = 0;
    im->ticks_since_flux = 0;
    im->cur_track = track;

    im->cur_bc = (sys_ticks * 16) / im->img.ticks_per_cell;
    im->cur_bc &= ~15;
    if (im->cur_bc >= im->tracklen_bc)
        im->cur_bc = 0;
    im->cur_ticks = im->cur_bc * im->img.ticks_per_cell;

    decode_off = im->cur_bc / 16;
    if (decode_off < im->img.idx_sz) {
        im->img.decode_pos = 0;
    } else {
        decode_off -= im->img.idx_sz;
        im->img.decode_pos = decode_off / (im->img.idam_sz + im->img.dam_sz);
        if (im->img.decode_pos < im->img.nr_sectors) {
            im->img.trk_pos = im->img.decode_pos * sec_sz;
            im->img.decode_pos = im->img.decode_pos * 2 + 1;
            decode_off %= im->img.idam_sz + im->img.dam_sz;
            if (decode_off >= im->img.idam_sz) {
                decode_off -= im->img.idam_sz;
                im->img.decode_pos++;
            }
        } else {
            im->img.decode_pos = im->img.nr_sectors * 2 + 1;
            decode_off -= im->img.nr_sectors
                * (im->img.idam_sz + im->img.dam_sz);
        }
    }

    rd->prod = rd->cons = 0;
    mfm->prod = mfm->cons = 0;

    if (start_pos) {
        image_read_track(im);
        mfm->cons = decode_off * 16;
        *start_pos = sys_ticks;
    }

    return FALSE;
}

static bool_t img_read_track(struct image *im)
{
    struct image_buf *rd = &im->bufs.read_data;
    struct image_buf *mfm = &im->bufs.read_mfm;
    uint8_t *buf = rd->p;
    uint16_t *mfmb = mfm->p;
    unsigned int i, mfmlen, mfmp, mfmc;
    uint16_t pr = 0, crc;
    unsigned int buflen = rd->len & ~511;

    if (rd->prod == rd->cons) {
        F_lseek(&im->fp, im->img.trk_off + im->img.trk_pos);
        F_read(&im->fp, &buf[(rd->prod/8) % buflen], sec_sz, NULL);
        rd->prod += sec_sz * 8;
        im->img.trk_pos += sec_sz;
        if (im->img.trk_pos >= im->img.trk_len)
            im->img.trk_pos = 0;
    }

    /* Generate some MFM if there is space in the MFM ring buffer. */
    mfmp = mfm->prod / 16; /* MFM words */
    mfmc = mfm->cons / 16; /* MFM words */
    mfmlen = mfm->len / 2; /* MFM words */

#define emit_raw(r) ({                                  \
    uint16_t _r = (r);                                  \
    mfmb[mfmp++ % mfmlen] = htobe16(_r & ~(pr << 15));  \
    pr = _r; })
#define emit_byte(b) emit_raw(mfmtab[(uint8_t)(b)])

    if (im->img.decode_pos == 0) {
        /* Post-index track gap */
        if ((mfmlen - (mfmp - mfmc)) < (GAP_4A + GAP_SYNC + 4 + GAP_1))
            return FALSE;
        for (i = 0; i < GAP_4A; i++)
            emit_byte(0x4e);
        if (im->img.has_iam) {
            /* IAM */
            for (i = 0; i < GAP_SYNC; i++)
                emit_byte(0x00);
            for (i = 0; i < 3; i++)
                emit_raw(0x5224);
            emit_byte(0xfc);
            for (i = 0; i < GAP_1; i++)
                emit_byte(0x4e);
        }
    } else if (im->img.decode_pos == (im->img.nr_sectors * 2 + 1)) {
        /* Pre-index track gap */
        if ((mfmlen - (mfmp - mfmc)) < im->img.gap4)
            return FALSE;
        for (i = 0; i < im->img.gap4; i++)
            emit_byte(0x4e);
        im->img.decode_pos = -1;
    } else if (im->img.decode_pos & 1) {
        /* IDAM */
        uint8_t cyl = im->cur_track/2, hd = im->cur_track&1;
        uint8_t sec = im->img.sec_map[(im->img.decode_pos-1) >> 1], no = 2;
        uint8_t idam[8] = { 0xa1, 0xa1, 0xa1, 0xfe, cyl, hd, sec, no };
        if ((mfmlen - (mfmp - mfmc)) < (GAP_SYNC + 8 + 2 + GAP_2))
            return FALSE;
        for (i = 0; i < GAP_SYNC; i++)
            emit_byte(0x00);
        for (i = 0; i < 3; i++)
            emit_raw(0x4489);
        for (; i < 8; i++)
            emit_byte(idam[i]);
        crc = crc16_ccitt(idam, sizeof(idam), 0xffff);
        emit_byte(crc >> 8);
        emit_byte(crc);
        for (i = 0; i < GAP_2; i++)
            emit_byte(0x4e);
    } else {
        /* DAM */
        uint8_t *dat = &buf[(rd->cons/8)%buflen];
        uint8_t dam[4] = { 0xa1, 0xa1, 0xa1, 0xfb };
        if ((mfmlen - (mfmp - mfmc)) < (GAP_SYNC + 4 + sec_sz + 2
                                        + im->img.gap3))
            return FALSE;
        for (i = 0; i < GAP_SYNC; i++)
            emit_byte(0x00);
        for (i = 0; i < 3; i++)
            emit_raw(0x4489);
        emit_byte(dam[3]);
        for (i = 0; i < sec_sz; i++)
            emit_byte(dat[i]);
        crc = crc16_ccitt(dam, sizeof(dam), 0xffff);
        crc = crc16_ccitt(dat, sec_sz, crc);
        emit_byte(crc >> 8);
        emit_byte(crc);
        for (i = 0; i < im->img.gap3; i++)
            emit_byte(0x4e);
        rd->cons += sec_sz * 8;
    }

    im->img.decode_pos++;
    mfm->prod = mfmp * 16;

    return TRUE;
}

static uint16_t img_rdata_flux(struct image *im, uint16_t *tbuf, uint16_t nr)
{
    uint32_t ticks = im->ticks_since_flux;
    uint32_t ticks_per_cell = im->img.ticks_per_cell;
    uint32_t x, y = 32, todo = nr;
    struct image_buf *mfm = &im->bufs.read_mfm;
    uint32_t *mfmb = mfm->p;

    /* Convert pre-generated MFM into flux timings. */
    while (mfm->cons != mfm->prod) {
        y = mfm->cons % 32;
        x = be32toh(mfmb[(mfm->cons/32)%(mfm->len/4)]) << y;
        mfm->cons += 32 - y;
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

out:
    if (im->cur_bc >= im->tracklen_bc) {
        im->cur_bc -= im->tracklen_bc;
        ASSERT(im->cur_bc < im->tracklen_bc);
        im->tracklen_ticks = im->cur_ticks - im->cur_bc * ticks_per_cell;
        im->cur_ticks -= im->tracklen_ticks;
    }

    mfm->cons -= 32 - y;
    im->cur_bc -= 32 - y;
    im->cur_ticks -= (32 - y) * ticks_per_cell;
    im->ticks_since_flux = ticks;
    return nr - todo;
}

static void img_write_track(struct image *im, bool_t flush)
{
    const uint8_t header[] = { 0xa1, 0xa1, 0xa1, 0xfb };

    struct image_buf *wr = &im->bufs.write_mfm;
    uint16_t *buf = wr->p;
    unsigned int buflen = wr->len / 2;
    uint8_t *wrbuf = im->bufs.write_data.p;
    uint32_t c = wr->cons / 16, p = wr->prod / 16;
    uint32_t base = im->write_start / (im->write_bc_ticks * 16);
    unsigned int i;
    stk_time_t t;
    uint16_t crc;
    uint8_t x;

    if (im->img.write_sector == -1) {
        /* Convert write offset to sector number (in rotational order). */
        im->img.write_sector =
            (base - im->img.idx_sz - im->img.idam_sz
             + (im->img.idam_sz + im->img.dam_sz) / 2)
            / (im->img.idam_sz + im->img.dam_sz);
        if (im->img.write_sector >= im->img.nr_sectors) {
            printk("IMG Bad Sector Offset: %u -> %u\n",
                   base, im->img.write_sector);
            im->img.write_sector = -2;
        } else {
            /* Convert rotational order to logical order. */
            im->img.write_sector = im->img.sec_map[im->img.write_sector];
            im->img.write_sector -= im->img.sec_base;
        }
    }

    /* Round up the producer index if we are processing final data. */
    if (flush && (wr->prod & 15))
        p++;

    while ((p - c) >= (3 + sec_sz + 2)) {

        /* Scan for sync words and IDAM. Because of the way we sync we expect
         * to see only 2*4489 and thus consume only 3 words for the header. */
        if (be16toh(buf[c++ % buflen]) != 0x4489)
            continue;
        for (i = 0; i < 2; i++)
            if ((x = mfmtobin(buf[c++ % buflen])) != 0xa1)
                break;

        switch (x) {

        case 0xfe: /* IDAM */
            for (i = 0; i < 3; i++)
                wrbuf[i] = 0xa1;
            wrbuf[i++] = x;
            for (; i < 10; i++)
                wrbuf[i] = mfmtobin(buf[c++ % buflen]);
            crc = crc16_ccitt(wrbuf, i, 0xffff);
            if (crc != 0) {
                printk("IMG IDAM Bad CRC %04x, sector %u\n", crc, wrbuf[6]);
                break;
            }
            im->img.write_sector = wrbuf[6] - im->img.sec_base;
            if ((uint8_t)im->img.write_sector >= im->img.nr_sectors) {
                printk("IMG IDAM Bad Sector: %u\n", wrbuf[6]);
                im->img.write_sector = -2;
            }
            break;

        case 0xfb: /* DAM */
            for (i = 0; i < (sec_sz + 2); i++)
                wrbuf[i] = mfmtobin(buf[c++ % buflen]);

            crc = crc16_ccitt(wrbuf, sec_sz + 2,
                              crc16_ccitt(header, 4, 0xffff));
            if (crc != 0) {
                printk("IMG Bad CRC %04x, sector %u[%u]\n",
                       crc, im->img.write_sector,
                       im->img.write_sector + im->img.sec_base);
                break;
            }

            if (im->img.write_sector < 0) {
                printk("IMG DAM for unknown sector (%d)\n",
                       im->img.write_sector);
                break;
            }

            /* All good: write out to mass storage. */
            printk("Write %u[%u]/%u... ", im->img.write_sector,
                   im->img.write_sector + im->img.sec_base,
                   im->img.nr_sectors);
            t = stk_now();
            F_lseek(&im->fp, im->img.trk_off + im->img.write_sector*sec_sz);
            F_write(&im->fp, wrbuf, sec_sz, NULL);
            printk("%u us\n", stk_diff(t, stk_now()) / STK_MHZ);
            break;
        }
    }

    wr->cons = c * 16;

    if (flush)
        im->img.write_sector = -1;
}

const struct image_handler img_image_handler = {
    .open = img_open,
    .seek_track = img_seek_track,
    .read_track = img_read_track,
    .rdata_flux = img_rdata_flux,
    .write_track = img_write_track,
    .syncword = 0x44894489
};

const struct image_handler st_image_handler = {
    .open = st_open,
    .seek_track = img_seek_track,
    .read_track = img_read_track,
    .rdata_flux = img_rdata_flux,
    .write_track = img_write_track,
    .syncword = 0x44894489
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

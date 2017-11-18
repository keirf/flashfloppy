/*
 * dsk.c
 * 
 * Amstrad CPC DSK image files. Also used by Spectrum +3.
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

struct dib { /* disk info */
    char sig[34];
    char creator[14];
    uint8_t nr_tracks, nr_sides;
    uint16_t track_sz;
    uint8_t track_szs[1];
};

struct tib { /* track/cyl info */
    char sig[12];
    uint8_t pad[4];
    uint8_t track, side;
    uint8_t pad2[2];
    uint8_t sec_sz;
    uint8_t nr_secs;
    uint8_t gap3;
    uint8_t filler;
    struct sib { /* sector info */
        uint8_t c, h, r, n, stat1, stat2;
        uint16_t actual_length; /* ext */
    } sib[1];
};


static struct dib *dib_p(struct image *im)
{
    struct image_buf *rd = &im->bufs.read_data;
    return (struct dib *)rd->p;
}

static struct tib *tib_p(struct image *im)
{
    struct image_buf *rd = &im->bufs.read_data;
    return (struct tib *)((char *)rd->p + 256);
}

static bool_t dsk_open(struct image *im)
{
    struct dib *dib = dib_p(im);

    /* HACK! We stash TIB in the read-data area. Assert that it is also
     * available at the same offset in the write-data area too. */
    ASSERT(im->bufs.read_data.p == im->bufs.write_data.p);

    /* Read the Disk Information Block. */
    F_read(&im->fp, dib, 256, NULL);

    /* Check the header signature. */
    if (!strncmp(dib->sig, "MV - CPC", 8)) {
        /* regular DSK */
    } else if (!strncmp(dib->sig, "EXTENDED CPC DSK", 16)) {
        /* extended DSK */
        im->dsk.extended = 1;
    } else {
        return FALSE;
    }

    /* Sanity check the disk parameters. */
    if ((dib->nr_sides == 0) || (dib->nr_sides > 2)
        || (dib->nr_tracks * dib->nr_sides > 200)) {
        return FALSE;
    }

    im->nr_cyls = dib->nr_tracks;
    im->nr_sides = dib->nr_sides;
    printk("DSK: %u cyls, %u sides\n", im->nr_cyls, im->nr_sides);

    return TRUE;
}

static void dsk_seek_track(
    struct image *im, uint16_t track, stk_time_t *start_pos)
{
    struct dib *dib = dib_p(im);
    struct tib *tib = tib_p(im);
    struct image_buf *rd = &im->bufs.read_data;
    struct image_buf *mfm = &im->bufs.read_mfm;
    unsigned int i, nr;
    uint32_t decode_off, sys_ticks = start_pos ? *start_pos : 0;
    uint32_t tracklen;
    uint8_t cyl = track/2, side = track&1;

    side = min_t(uint8_t, side, im->nr_sides-1);
    track = cyl*2 + side;

    if (cyl >= im->nr_cyls) {
    unformatted:
        printk("%u %u: Unformatted\n", cyl, side);
        memset(tib, 0, sizeof(*tib));
        im->tracklen_bc = 100160;
        im->dsk.ticks_per_cell = ((sysclk_ms(DRIVE_MS_PER_REV) * 16u)
                                  / im->tracklen_bc);
        im->ticks_since_flux = 0;
        im->cur_track = track;
        im->cur_bc = (sys_ticks * 16) / im->dsk.ticks_per_cell;
        im->cur_bc &= ~15;
        if (im->cur_bc >= im->tracklen_bc)
            im->cur_bc = 0;
        im->cur_ticks = im->cur_bc * im->dsk.ticks_per_cell;
        decode_off = 0;
        goto out;
    }

    im->dsk.trk_off = 0x100;
    nr = (unsigned int)cyl * im->nr_sides + side;
    if (im->dsk.extended) {
        if (dib->track_szs[nr] == 0)
            goto unformatted;
        for (i = 0; i < nr; i++)
            im->dsk.trk_off += dib->track_szs[i] * 256;
    } else {
        im->dsk.trk_off += nr * le16toh(dib->track_sz);
    }

    /* Read the Track Info Block and Sector Info Blocks. */
    F_lseek(&im->fp, im->dsk.trk_off);
    F_read(&im->fp, tib, 256, NULL);
    im->dsk.trk_off += 256;
    if (strncmp(tib->sig, "Track-Info", 10) || !tib->nr_secs)
        goto unformatted;

    printk("%u %u / %u %u: %u sectors\n", cyl, side, tib->track, tib->side,
           tib->nr_secs);

    /* Clamp number of sectors. */
    if (tib->nr_secs > 29)
        tib->nr_secs = 29;

    /* Compute per-sector actual length. */
    for (i = 0; i < tib->nr_secs; i++) {
        tib->sib[i].actual_length = im->dsk.extended
            ? le16toh(tib->sib[i].actual_length) : 128 << tib->sec_sz;
        /* Clamp sector size */
        if (tib->sib[i].actual_length > 16384) {
            printk("Warn: clamp sector size %u\n", tib->sib[i].actual_length);
            tib->sib[i].actual_length = 16384;
        }
    }

    im->dsk.idx_sz = GAP_4A;
    im->dsk.idx_sz += GAP_SYNC + 4 + GAP_1;
    im->dsk.idam_sz = GAP_SYNC + 8 + 2 + GAP_2;
    im->dsk.dam_sz = GAP_SYNC + 4 + /*sec_sz +*/ 2 + tib->gap3;

    /* Work out minimum track length (with no pre-index track gap). */
    tracklen = (im->dsk.idam_sz + im->dsk.dam_sz) * tib->nr_secs;
    tracklen += im->dsk.idx_sz;
    for (i = 0; i < tib->nr_secs; i++)
        tracklen += tib->sib[i].actual_length;
    tracklen *= 16;

    /* Calculate and round the track length. */
    im->tracklen_bc = 100000;
    if (im->tracklen_bc < tracklen)
        im->tracklen_bc = tracklen + 1000;
    im->tracklen_bc = (im->tracklen_bc + 31) & ~31;

    /* Calculate output data rate (bitcell size). */
    im->dsk.ticks_per_cell = ((sysclk_ms(DRIVE_MS_PER_REV) * 16u)
                              / im->tracklen_bc);

    /* Now calculate the pre-index track gap. */
    im->dsk.gap4 = (im->tracklen_bc - tracklen) / 16;

    im->dsk.write_sector = -1;
    im->ticks_since_flux = 0;
    im->cur_track = track;

    im->cur_bc = (sys_ticks * 16) / im->dsk.ticks_per_cell;
    im->cur_bc &= ~15;
    if (im->cur_bc >= im->tracklen_bc)
        im->cur_bc = 0;
    im->cur_ticks = im->cur_bc * im->dsk.ticks_per_cell;

    im->dsk.trk_pos = 0;
    decode_off = im->cur_bc / 16;
    if (decode_off < im->dsk.idx_sz) {
        im->dsk.decode_pos = 0;
    } else {
        decode_off -= im->dsk.idx_sz;
        for (i = 0; i < tib->nr_secs; i++) {
            uint16_t sec_sz = im->dsk.idam_sz + im->dsk.dam_sz
                + tib->sib[i].actual_length;
            if (decode_off < sec_sz)
                break;
            decode_off -= sec_sz;
        }
        if (i < tib->nr_secs) {
            im->dsk.trk_pos = i;
            im->dsk.decode_pos = i * 2 + 1;
            if (decode_off >= im->dsk.idam_sz) {
                decode_off -= im->dsk.idam_sz;
                im->dsk.decode_pos++;
            }
        } else {
            im->dsk.trk_pos = 0;
            im->dsk.decode_pos = tib->nr_secs * 2 + 1;
        }
    }

out:
    rd->prod = rd->cons = 0;
    mfm->prod = mfm->cons = 0;

    if (start_pos) {
        image_read_track(im);
        mfm->cons = decode_off * 16;
        *start_pos = sys_ticks;
    }
}

static bool_t dsk_read_track(struct image *im)
{
    struct tib *tib = tib_p(im);
    struct image_buf *rd = &im->bufs.read_data;
    struct image_buf *mfm = &im->bufs.read_mfm;
    uint8_t *buf = (uint8_t *)rd->p + 512; /* skip DIB/TIB */
    uint16_t *mfmb = mfm->p;
    unsigned int i, mfmlen, mfmp, mfmc;
    uint16_t pr = 0, crc;
    unsigned int buflen = rd->len & ~511;

    if (tib->nr_secs == 0) {
        /* Unformatted. */
        mfm->prod = mfm->cons + mfm->len*8;
        return TRUE;
    }

    if (rd->prod == rd->cons) {
        uint16_t off = 0;
        for (i = 0; i < im->dsk.trk_pos; i++)
            off += tib->sib[i].actual_length;
        rd->cons = rd->prod = 0;
        F_lseek(&im->fp, im->dsk.trk_off + off);
        F_read(&im->fp, buf, tib->sib[i].actual_length, NULL);
        rd->prod += tib->sib[i].actual_length * 8;
        if (++im->dsk.trk_pos >= tib->nr_secs)
            im->dsk.trk_pos = 0;
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

    if (im->dsk.decode_pos == 0) {
        /* Post-index track gap */
        if ((mfmlen - (mfmp - mfmc)) < (GAP_4A + GAP_SYNC + 4 + GAP_1))
            return FALSE;
        for (i = 0; i < GAP_4A; i++)
            emit_byte(0x4e);
        /* IAM */
        for (i = 0; i < GAP_SYNC; i++)
            emit_byte(0x00);
        for (i = 0; i < 3; i++)
            emit_raw(0x5224);
        emit_byte(0xfc);
        for (i = 0; i < GAP_1; i++)
            emit_byte(0x4e);
    } else if (im->dsk.decode_pos == (tib->nr_secs * 2 + 1)) {
        /* Pre-index track gap */
        if ((mfmlen - (mfmp - mfmc)) < im->dsk.gap4)
            return FALSE;
        for (i = 0; i < im->dsk.gap4; i++)
            emit_byte(0x4e);
        im->dsk.decode_pos = -1;
    } else if (im->dsk.decode_pos & 1) {
        /* IDAM */
        uint8_t sec = (im->dsk.decode_pos-1) >> 1;
        uint8_t idam[8] = { 0xa1, 0xa1, 0xa1, 0xfe };
        if ((mfmlen - (mfmp - mfmc)) < (GAP_SYNC + 8 + 2 + GAP_2))
            return FALSE;
        if ((tib->sib[sec].stat1 & 0x01) && !(tib->sib[sec].stat2 & 0x01))
            idam[3] = 0x00; /* Missing Address Mark (ID) */
        memcpy(&idam[4], &tib->sib[sec].c, 4);
        for (i = 0; i < GAP_SYNC; i++)
            emit_byte(0x00);
        for (i = 0; i < 3; i++)
            emit_raw(0x4489);
        for (; i < 8; i++)
            emit_byte(idam[i]);
        crc = crc16_ccitt(idam, sizeof(idam), 0xffff);
        if ((tib->sib[sec].stat1 & 0x20) && !(tib->sib[sec].stat2 & 0x20))
            crc = ~crc; /* CRC Error in ID */
        emit_byte(crc >> 8);
        emit_byte(crc);
        for (i = 0; i < GAP_2; i++)
            emit_byte(0x4e);
    } else {
        /* DAM */
        uint8_t sec = (im->dsk.decode_pos-2) >> 1;
        uint16_t sec_sz = tib->sib[sec].actual_length;
        uint8_t *dat = &buf[(rd->cons/8)%buflen];
        uint8_t dam[4] = { 0xa1, 0xa1, 0xa1, 0xfb };
        if ((mfmlen - (mfmp - mfmc)) < (GAP_SYNC + 4 + sec_sz + 2
                                        + tib->gap3))
            return FALSE;
        if ((tib->sib[sec].stat1 & 0x01) && (tib->sib[sec].stat2 & 0x01))
            dam[3] = 0x00; /* Missing Address Mark (Data) */
        else if (tib->sib[sec].stat2 & 0x40)
            dam[3] = 0xf8; /* Found DDAM */
        for (i = 0; i < GAP_SYNC; i++)
            emit_byte(0x00);
        for (i = 0; i < 3; i++)
            emit_raw(0x4489);
        emit_byte(dam[3]);
        for (i = 0; i < sec_sz; i++)
            emit_byte(dat[i]);
        crc = crc16_ccitt(dam, sizeof(dam), 0xffff);
        crc = crc16_ccitt(dat, sec_sz, crc);
        if ((tib->sib[sec].stat1 & 0x20) && (tib->sib[sec].stat2 & 0x20))
            crc = ~crc; /* CRC Error in Data */
        emit_byte(crc >> 8);
        emit_byte(crc);
        for (i = 0; i < tib->gap3; i++)
            emit_byte(0x4e);
        rd->cons += sec_sz * 8;
    }

    im->dsk.decode_pos++;
    mfm->prod = mfmp * 16;

    return TRUE;
}

static uint16_t dsk_rdata_flux(struct image *im, uint16_t *tbuf, uint16_t nr)
{
    uint32_t ticks = im->ticks_since_flux;
    uint32_t ticks_per_cell = im->dsk.ticks_per_cell;
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

static void dsk_write_track(struct image *im, bool_t flush)
{
    const uint8_t header[] = { 0xa1, 0xa1, 0xa1, 0xfb };

    struct tib *tib = tib_p(im);
    struct image_buf *wr = &im->bufs.write_mfm;
    uint16_t *buf = (uint16_t *)((uint8_t *)wr->p + 512); /* skip DIB/TIB */
    unsigned int buflen = wr->len / 2;
    uint8_t *wrbuf = im->bufs.write_data.p;
    uint32_t c = wr->cons / 16, p = wr->prod / 16;
    int32_t base = im->write_start / (im->write_bc_ticks * 16);
    unsigned int i;
    stk_time_t t;
    uint16_t crc, sec_sz, off;
    uint8_t x;

    if (tib->nr_secs == 0) {
        /* Unformatted. */
        return;
    }

    if (im->dsk.write_sector == -1) {
        /* Convert write offset to sector number (in rotational order). */
        base -= im->dsk.idx_sz + im->dsk.idam_sz;
        for (i = 0; i < tib->nr_secs; i++) {
            /* Within small range of expected data start? */
            if ((base >= -64) && (base <= 64))
                break;
            base -= im->dsk.idam_sz + im->dsk.dam_sz
                + tib->sib[i].actual_length;
        }
        im->dsk.write_sector = i;
        if (im->dsk.write_sector >= tib->nr_secs) {
            printk("DSK Bad Sector Offset: %u -> %u\n",
                   base, im->dsk.write_sector);
            im->dsk.write_sector = -2;
        }
    }

    /* Round up the producer index if we are processing final data. */
    if (flush && (wr->prod & 15))
        p++;

    for (;;) {

        sec_sz = (im->dsk.write_sector >= 0)
            ? tib->sib[im->dsk.write_sector].actual_length : 0;
        if ((p - c) < (3 + sec_sz + 2))
            break;

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
                printk("DSK IDAM Bad CRC %04x, sector %02x\n", crc, wrbuf[6]);
                break;
            }
            /* Convert logical sector number -> rotational number. */
            for (i = 0; i < tib->nr_secs; i++)
                if (wrbuf[6] == tib->sib[i].r)
                    break;
            im->dsk.write_sector = i;
            if (im->dsk.write_sector >= tib->nr_secs) {
                printk("DSK IDAM Bad Sector: %02x\n", wrbuf[6]);
                im->dsk.write_sector = -2;
            }
            break;

        case 0xfb: /* DAM */
            for (i = 0; i < (sec_sz + 2); i++)
                wrbuf[i] = mfmtobin(buf[c++ % buflen]);

            crc = crc16_ccitt(wrbuf, sec_sz + 2,
                              crc16_ccitt(header, 4, 0xffff));
            if (crc != 0) {
                printk("DSK Bad CRC %04x, sector %d[%02x]\n",
                       crc, im->dsk.write_sector,
                       (im->dsk.write_sector >= 0)
                       ? tib->sib[im->dsk.write_sector].r : 0xff);
                break;
            }

            if (im->dsk.write_sector < 0) {
                printk("DSK DAM for unknown sector (%d)\n",
                       im->dsk.write_sector);
                break;
            }

            /* All good: write out to mass storage. */
            printk("Write %d[%02x]/%u... ", im->dsk.write_sector,
                   (im->dsk.write_sector >= 0)
                   ? tib->sib[im->dsk.write_sector].r : 0xff,
                   tib->nr_secs);
            t = stk_now();
            for (i = off = 0; i < im->dsk.write_sector; i++)
                off += tib->sib[i].actual_length;
            F_lseek(&im->fp, im->dsk.trk_off + off);
            F_write(&im->fp, wrbuf, sec_sz, NULL);
            printk("%u us\n", stk_diff(t, stk_now()) / STK_MHZ);
            break;
        }
    }

    wr->cons = c * 16;

    if (flush)
        im->dsk.write_sector = -1;
}

const struct image_handler dsk_image_handler = {
    .open = dsk_open,
    .seek_track = dsk_seek_track,
    .read_track = dsk_read_track,
    .rdata_flux = dsk_rdata_flux,
    .write_track = dsk_write_track,
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

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

    /* DSK data rate is fixed at 2us bitcell. Where the specified track layout 
     * will not fit in regular 100k-bitcell track we simply extend the track 
     * length and thus the period between index pulses. */
    im->ticks_per_cell = im->write_bc_ticks * 16;

    return TRUE;
}

static void dsk_seek_track(
    struct image *im, uint16_t track, unsigned int cyl, unsigned int side)
{
    struct dib *dib = dib_p(im);
    struct tib *tib = tib_p(im);
    unsigned int i, nr;
    uint32_t tracklen;

    im->cur_track = track;

    if (cyl >= im->nr_cyls) {
    unformatted:
        printk("T%u.%u: Unformatted\n", cyl, side);
        memset(tib, 0, sizeof(*tib));
        im->tracklen_bc = 100160;
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

    printk("T%u.%u -> %u.%u: %u sectors\n", cyl, side, tib->track,
           tib->side, tib->nr_secs);

    /* Clamp number of sectors. */
    if (tib->nr_secs > 29)
        tib->nr_secs = 29;

    /* Compute per-sector actual length. */
    for (i = 0; i < tib->nr_secs; i++) {
        tib->sib[i].actual_length = im->dsk.extended
            ? le16toh(tib->sib[i].actual_length) : 128 << tib->sec_sz;
        /* Clamp sector size */
        if (tib->sib[i].actual_length > 16384) {
            printk("Warn: clamp sector size %u\n",
                   tib->sib[i].actual_length);
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
    im->tracklen_bc = max_t(unsigned int, 100000, tracklen + 20*16);
    im->tracklen_bc = (im->tracklen_bc + 31) & ~31;

    /* Now calculate the pre-index track gap. */
    im->dsk.gap4 = (im->tracklen_bc - tracklen) / 16;

out:
    /* Calculate ticks per revolution */
    im->stk_per_rev = stk_sysclk(im->tracklen_bc * im->write_bc_ticks);
}

static void dsk_setup_track(
    struct image *im, uint16_t track, uint32_t *start_pos)
{
    struct tib *tib = tib_p(im);
    struct image_buf *rd = &im->bufs.read_data;
    struct image_buf *bc = &im->bufs.read_bc;
    unsigned int i;
    uint32_t decode_off = 0, sys_ticks = start_pos ? *start_pos : 0;
    uint8_t cyl = track/2, side = track&1;

    side = min_t(uint8_t, side, im->nr_sides-1);
    track = cyl*2 + side;

    if (track != im->cur_track)
        dsk_seek_track(im, track, cyl, side);

    im->dsk.write_sector = -1;

    im->cur_bc = (sys_ticks * 16) / im->ticks_per_cell;
    im->cur_bc &= ~15;
    if (im->cur_bc >= im->tracklen_bc)
        im->cur_bc = 0;
    im->cur_ticks = im->cur_bc * im->ticks_per_cell;
    im->ticks_since_flux = 0;

    if (tib->nr_secs != 0) {

        /* Calculate start position within the track. */
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

    }

    rd->prod = rd->cons = 0;
    bc->prod = bc->cons = 0;

    if (start_pos) {
        image_read_track(im);
        bc->cons = decode_off * 16;
        *start_pos = sys_ticks;
    }
}

static bool_t dsk_read_track(struct image *im)
{
    struct tib *tib = tib_p(im);
    struct image_buf *rd = &im->bufs.read_data;
    struct image_buf *bc = &im->bufs.read_bc;
    uint8_t *buf = (uint8_t *)rd->p + 512; /* skip DIB/TIB */
    uint16_t *bc_b = bc->p;
    uint32_t bc_len, bc_mask, bc_space, bc_p, bc_c;
    uint16_t pr = 0, crc;
    unsigned int i;

    if (tib->nr_secs == 0) {
        /* Unformatted. */
        bc->prod = bc->cons + bc->len*8;
        return TRUE;
    }

    if (rd->prod == rd->cons) {
        uint16_t off = 0;
        for (i = 0; i < im->dsk.trk_pos; i++)
            off += tib->sib[i].actual_length;
        F_lseek(&im->fp, im->dsk.trk_off + off);
        F_read(&im->fp, buf, tib->sib[i].actual_length, NULL);
        rd->prod++;
        if (++im->dsk.trk_pos >= tib->nr_secs)
            im->dsk.trk_pos = 0;
    }

    /* Generate some MFM if there is space in the raw-bitcell ring buffer. */
    bc_p = bc->prod / 16; /* MFM words */
    bc_c = bc->cons / 16; /* MFM words */
    bc_len = bc->len / 2; /* MFM words */
    bc_mask = bc_len - 1;
    bc_space = bc_len - (uint16_t)(bc_p - bc_c);

#define emit_raw(r) ({                                   \
    uint16_t _r = (r);                                   \
    bc_b[bc_p++ & bc_mask] = htobe16(_r & ~(pr << 15));  \
    pr = _r; })
#define emit_byte(b) emit_raw(mfmtab[(uint8_t)(b)])

    if (im->dsk.decode_pos == 0) {
        /* Post-index track gap */
        if (bc_space < (GAP_4A + GAP_SYNC + 4 + GAP_1))
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
        if (bc_space < im->dsk.gap4)
            return FALSE;
        for (i = 0; i < im->dsk.gap4; i++)
            emit_byte(0x4e);
        im->dsk.decode_pos = -1;
    } else if (im->dsk.decode_pos & 1) {
        /* IDAM */
        uint8_t sec = (im->dsk.decode_pos-1) >> 1;
        uint8_t idam[8] = { 0xa1, 0xa1, 0xa1, 0xfe };
        if (bc_space < (GAP_SYNC + 8 + 2 + GAP_2))
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
        uint8_t dam[4] = { 0xa1, 0xa1, 0xa1, 0xfb };
        if (bc_space < (GAP_SYNC + 4 + sec_sz + 2 + tib->gap3))
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
            emit_byte(buf[i]);
        crc = crc16_ccitt(dam, sizeof(dam), 0xffff);
        crc = crc16_ccitt(buf, sec_sz, crc);
        if ((tib->sib[sec].stat1 & 0x20) && (tib->sib[sec].stat2 & 0x20))
            crc = ~crc; /* CRC Error in Data */
        emit_byte(crc >> 8);
        emit_byte(crc);
        for (i = 0; i < tib->gap3; i++)
            emit_byte(0x4e);
        rd->cons++;
    }

    im->dsk.decode_pos++;
    bc->prod = bc_p * 16;

    return TRUE;
}

static bool_t dsk_write_track(struct image *im)
{
    const uint8_t header[] = { 0xa1, 0xa1, 0xa1, 0xfb };

    bool_t flush;
    struct write *write = get_write(im, im->wr_cons);
    struct tib *tib = tib_p(im);
    struct image_buf *wr = &im->bufs.write_bc;
    uint16_t *buf = wr->p;
    unsigned int bufmask = (wr->len / 2) - 1;
    uint8_t *wrbuf = (uint8_t *)im->bufs.write_data.p + 512; /* skip DIB/TIB */
    uint32_t c = wr->cons / 16, p = wr->prod / 16;
    int32_t base = write->start / im->ticks_per_cell; /* in data bytes */
    unsigned int i;
    time_t t;
    uint16_t crc, sec_sz, off;
    uint8_t x;

    /* If we are processing final data then use the end index, rounded up. */
    barrier();
    flush = (im->wr_cons != im->wr_bc);
    if (flush)
        p = (write->bc_end + 15) / 16;

    if (tib->nr_secs == 0) {
        /* Unformatted. */
        goto out;
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

    for (;;) {

        sec_sz = (im->dsk.write_sector >= 0)
            ? tib->sib[im->dsk.write_sector].actual_length : 128;
        if ((int16_t)(p - c) < (3 + sec_sz + 2))
            break;

        /* Scan for sync words and IDAM. Because of the way we sync we expect
         * to see only 2*4489 and thus consume only 3 words for the header. */
        if (be16toh(buf[c++ & bufmask]) != 0x4489)
            continue;
        for (i = 0; i < 2; i++)
            if ((x = mfmtobin(buf[c++ & bufmask])) != 0xa1)
                break;

        switch (x) {

        case 0xfe: /* IDAM */
            for (i = 0; i < 3; i++)
                wrbuf[i] = 0xa1;
            wrbuf[i++] = x;
            for (; i < 10; i++)
                wrbuf[i] = mfmtobin(buf[c++ & bufmask]);
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
                wrbuf[i] = mfmtobin(buf[c++ & bufmask]);

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
            t = time_now();
            for (i = off = 0; i < im->dsk.write_sector; i++)
                off += tib->sib[i].actual_length;
            F_lseek(&im->fp, im->dsk.trk_off + off);
            F_write(&im->fp, wrbuf, sec_sz, NULL);
            printk("%u us\n", time_diff(t, time_now()) / TIME_MHZ);
            break;
        }
    }

    wr->cons = c * 16;

out:
    return flush;
}

const struct image_handler dsk_image_handler = {
    .open = dsk_open,
    .setup_track = dsk_setup_track,
    .read_track = dsk_read_track,
    .rdata_flux = bc_rdata_flux,
    .write_track = dsk_write_track,
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

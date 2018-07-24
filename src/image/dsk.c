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

#define n_sz(_sib) (128u << min_t(unsigned, (_sib)->n, 8))

static uint16_t data_sz(struct sib *sib)
{
    uint16_t n_sz = n_sz(sib);
    if (sib->actual_length == 0)
        return 0;
    return (sib->actual_length % n_sz) ? sib->actual_length : n_sz;
}

static bool_t is_gaps_sector(struct sib *sib)
{
    uint16_t dsz = data_sz(sib);
    return ((dsz & (dsz-1)) || (dsz & 0x7f));
}

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
        printk("T%u.%u: Empty\n", cyl, side);
        memset(tib, 0, sizeof(*tib));
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
    for (i = 0; i < tib->nr_secs; i++)
        tib->sib[i].actual_length = im->dsk.extended
            ? le16toh(tib->sib[i].actual_length)
            : 128 << min_t(unsigned, tib->sec_sz, 8);

out:
    im->dsk.idx_sz = GAP_4A;
    im->dsk.idx_sz += GAP_SYNC + 4 + GAP_1;
    im->dsk.idam_sz = GAP_SYNC + 8 + 2 + GAP_2;
    im->dsk.dam_sz_pre = GAP_SYNC + 4;
    im->dsk.dam_sz_post = 2 + tib->gap3;

    /* Work out minimum track length (with no pre-index track gap). */
    tracklen = (im->dsk.idam_sz + im->dsk.dam_sz_pre + im->dsk.dam_sz_post)
        * tib->nr_secs;
    tracklen += im->dsk.idx_sz;
    for (i = 0; i < tib->nr_secs; i++) {
        tracklen += data_sz(&tib->sib[i]);
        if (is_gaps_sector(&tib->sib[i]))
            tracklen -= im->dsk.dam_sz_post;
    }
    tracklen *= 16;

    /* Calculate and round the track length. */
    im->tracklen_bc = max_t(unsigned int, 100000, tracklen + 20*16);
    im->tracklen_bc = (im->tracklen_bc + 31) & ~31;

    /* Now calculate the pre-index track gap. */
    im->dsk.gap4 = (im->tracklen_bc - tracklen) / 16;

    /* Calculate ticks per revolution */
    im->stk_per_rev = stk_sysclk(im->tracklen_bc * im->write_bc_ticks);
}

static uint32_t calc_start_pos(struct image *im)
{
    struct tib *tib = tib_p(im);
    uint32_t decode_off;
    unsigned int i;

    /* Calculate start position within the track. */
    im->dsk.crc = 0xffff;
    im->dsk.trk_pos = im->dsk.rd_sec_pos = im->dsk.decode_data_pos = 0;
    decode_off = im->cur_bc / 16;
    if (decode_off < im->dsk.idx_sz) {
        /* Post-index track gap */
        im->dsk.decode_pos = 0;
    } else {
        decode_off -= im->dsk.idx_sz;
        for (i = 0; i < tib->nr_secs; i++) {
            uint16_t sec_sz = im->dsk.idam_sz + im->dsk.dam_sz_pre
                + data_sz(&tib->sib[i]) + im->dsk.dam_sz_post;
            if (is_gaps_sector(&tib->sib[i]))
                sec_sz -= im->dsk.dam_sz_post;
            if (decode_off < sec_sz)
                break;
            decode_off -= sec_sz;
        }
        if (i < tib->nr_secs) {
            /* IDAM */
            im->dsk.trk_pos = i;
            im->dsk.decode_pos = i * 4 + 1;
            if (decode_off >= im->dsk.idam_sz) {
                /* DAM */
                decode_off -= im->dsk.idam_sz;
                im->dsk.decode_pos++;
                if (decode_off >= im->dsk.dam_sz_pre) {
                    /* Data or Post Data */
                    decode_off -= im->dsk.dam_sz_pre;
                    im->dsk.decode_pos++;
                    if (decode_off < data_sz(&tib->sib[i])) {
                        /* Data */
                        im->dsk.rd_sec_pos = decode_off / 1024;
                        im->dsk.decode_data_pos = im->dsk.rd_sec_pos;
                        decode_off %= 1024;
                    } else {
                        /* Post Data */
                        decode_off -= data_sz(&tib->sib[i]);
                        im->dsk.decode_pos++;
                        im->dsk.trk_pos = (i + 1) % tib->nr_secs;
                    }
                }
            }
        } else {
            /* Pre-index track gap */
            im->dsk.decode_pos = tib->nr_secs * 4 + 1;
            im->dsk.decode_data_pos = decode_off / 1024;
            decode_off %= 1024;
        }
    }

    return decode_off;
}

static void dsk_setup_track(
    struct image *im, uint16_t track, uint32_t *start_pos)
{
    struct image_buf *rd = &im->bufs.read_data;
    struct image_buf *bc = &im->bufs.read_bc;
    uint32_t decode_off, sys_ticks = start_pos ? *start_pos : 0;
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

    decode_off = calc_start_pos(im);

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

    if (tib->nr_secs && (rd->prod == rd->cons)) {
        uint16_t off = 0, len;
        for (i = 0; i < im->dsk.trk_pos; i++)
            off += tib->sib[i].actual_length;
        len = data_sz(&tib->sib[i]);
        if (len != tib->sib[i].actual_length) {
            /* Weak sector -- pick different data each revolution. */
            off += len * (im->dsk.rev % (tib->sib[i].actual_length / len));
        }
        off += im->dsk.rd_sec_pos * 1024;
        len -= im->dsk.rd_sec_pos * 1024;
        if (len > 1024) {
            len = 1024;
            im->dsk.rd_sec_pos++;
        } else {
            im->dsk.rd_sec_pos = 0;
            if (++im->dsk.trk_pos >= tib->nr_secs) {
                im->dsk.trk_pos = 0;
                im->dsk.rev++;
            }
        }
        F_lseek(&im->fp, im->dsk.trk_off + off);
        F_read(&im->fp, buf, len, NULL);
        rd->prod++;
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
        if (bc_space < im->dsk.idx_sz)
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
    } else if (im->dsk.decode_pos == (tib->nr_secs * 4 + 1)) {
        /* Pre-index track gap */
        uint16_t sz = im->dsk.gap4 - im->dsk.decode_data_pos * 1024;
        if (bc_space < min_t(unsigned int, sz, 1024))
            return FALSE;
        if (sz > 1024) {
            sz = 1024;
            im->dsk.decode_data_pos++;
            im->dsk.decode_pos--;
        } else {
            im->dsk.decode_data_pos = 0;
            im->dsk.decode_pos = -1;
        }
        for (i = 0; i < sz; i++)
            emit_byte(0x4e);
    } else {
        uint8_t sec = (im->dsk.decode_pos-1) >> 2;
        switch ((im->dsk.decode_pos - 1) & 3) {
        case 0: /* IDAM */ {
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
            break;
        }
        case 1: /* DAM */ {
            uint8_t dam[4] = { 0xa1, 0xa1, 0xa1, 0xfb };
            if (bc_space < im->dsk.dam_sz_pre)
                return FALSE;
            if (tib->sib[sec].stat2 & 0x01)
                dam[3] = 0x00; /* Missing Address Mark (Data) */
            else if (tib->sib[sec].stat2 & 0x40)
                dam[3] = 0xf8; /* Found DDAM */
            for (i = 0; i < GAP_SYNC; i++)
                emit_byte(0x00);
            for (i = 0; i < 3; i++)
                emit_raw(0x4489);
            emit_byte(dam[3]);
            im->dsk.crc = crc16_ccitt(dam, sizeof(dam), 0xffff);
            break;
        }
        case 2: /* Data */ {
            uint16_t sec_sz = data_sz(&tib->sib[sec]);
            sec_sz -= im->dsk.decode_data_pos * 1024;
            if (bc_space < min_t(unsigned int, sec_sz, 1024))
                return FALSE;
            if (sec_sz > 1024) {
                sec_sz = 1024;
                im->dsk.decode_data_pos++;
                im->dsk.decode_pos--;
            } else {
                im->dsk.decode_data_pos = 0;
            }
            for (i = 0; i < sec_sz; i++)
                emit_byte(buf[i]);
            im->dsk.crc = crc16_ccitt(buf, sec_sz, im->dsk.crc);
            rd->cons++;
            break;
        }
        case 3: /* Post Data */ {
            if (is_gaps_sector(&tib->sib[sec]))
                break;
            if (bc_space < im->dsk.dam_sz_post)
                return FALSE;
            crc = im->dsk.crc;
            if ((tib->sib[sec].stat1 & 0x20) && (tib->sib[sec].stat2 & 0x20))
                crc = ~crc; /* CRC Error in Data */
            emit_byte(crc >> 8);
            emit_byte(crc);
            for (i = 0; i < tib->gap3; i++)
                emit_byte(0x4e);
            break;
        }
        }
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

    if (im->dsk.write_sector == -1) {
        /* Convert write offset to sector number (in rotational order). */
        base -= im->dsk.idx_sz + im->dsk.idam_sz;
        for (i = 0; i < tib->nr_secs; i++) {
            /* Within small range of expected data start? */
            if ((base >= -64) && (base <= 64))
                break;
            base -= im->dsk.idam_sz + im->dsk.dam_sz_pre
                + data_sz(&tib->sib[i]) + im->dsk.dam_sz_post;
            if (is_gaps_sector(&tib->sib[i]))
                base += im->dsk.dam_sz_post;
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
            ? data_sz(&tib->sib[im->dsk.write_sector]) : 128;
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

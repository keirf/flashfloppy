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
#define NR_SECS 11
#define BYTES_PER_TRACK (NR_SECS*512)
/* Amiga writes short bitcells (PAL: 14/7093790 us) hence long tracks. 
 * For better loader compatibility it is sensible to emulate this. */
#define TRACKLEN_BC 101376 /* multiple of 32 */
#define TICKS_PER_CELL ((sysclk_stk(im->stk_per_rev) * 16u) / TRACKLEN_BC)
#define POST_IDX_GAP_BC 1024
#define PRE_IDX_GAP_BC (TRACKLEN_BC - NR_SECS*544*16 - POST_IDX_GAP_BC)

/* Shift even/odd bits into MFM data-bit positions */
#define even(x) ((x)>>1)
#define odd(x) (x)

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
    if (f_size(&im->fp) % BYTES_PER_TRACK)
        return FALSE;

    im->nr_cyls = f_size(&im->fp) / (2 * BYTES_PER_TRACK);
    im->nr_sides = 2;

    return TRUE;
}

static void adf_seek_track(
    struct image *im, uint16_t track, stk_time_t *start_pos)
{
    struct image_buf *rd = &im->bufs.read_data;
    struct image_buf *mfm = &im->bufs.read_mfm;
    uint32_t decode_off, sector, sys_ticks = start_pos ? *start_pos : 0;
    uint8_t cyl = track/2, side = track&1;

    /* TODO: Fake out unformatted tracks. */
    cyl = min_t(uint8_t, cyl, im->nr_cyls-1);
    side = min_t(uint8_t, side, im->nr_sides-1);
    track = cyl*2 + side;

    im->adf.trk_off = track * BYTES_PER_TRACK;
    im->adf.trk_len = BYTES_PER_TRACK;
    im->tracklen_bc = TRACKLEN_BC;
    im->ticks_since_flux = 0;
    im->cur_track = track;

    im->cur_bc = (sys_ticks * 16) / TICKS_PER_CELL;
    if (im->cur_bc >= im->tracklen_bc)
        im->cur_bc = 0;
    im->cur_ticks = im->cur_bc * TICKS_PER_CELL;

    decode_off = im->cur_bc;
    if (decode_off < POST_IDX_GAP_BC) {
        im->adf.decode_pos = 0;
        im->adf.trk_pos = 0;
    } else {
        decode_off -= POST_IDX_GAP_BC;
        sector = decode_off / (544*16);
        decode_off %= 544*16;
        im->adf.decode_pos = sector + 1;
        im->adf.trk_pos = sector * 512;
        if (im->adf.trk_pos >= im->adf.trk_len)
            im->adf.trk_pos = 0;
    }

    rd->prod = rd->cons = 0;
    mfm->prod = mfm->cons = 0;

    if (start_pos) {
        image_read_track(im);
        mfm->cons = decode_off;
    }
}

static bool_t adf_read_track(struct image *im)
{
    const UINT sec_sz = 512;
    struct image_buf *rd = &im->bufs.read_data;
    struct image_buf *mfm = &im->bufs.read_mfm;
    uint8_t *buf = rd->p;
    uint32_t pr, *mfmb = mfm->p;
    unsigned int i, mfmlen, mfmp, mfmc;
    unsigned int buflen = rd->len & ~511;

    if (rd->prod == rd->cons) {
        F_lseek(&im->fp, im->adf.trk_off + im->adf.trk_pos);
        F_read(&im->fp, &buf[(rd->prod/8) % buflen], sec_sz, NULL);
        rd->prod += sec_sz * 8;
        im->adf.trk_pos += sec_sz;
        if (im->adf.trk_pos >= im->adf.trk_len)
            im->adf.trk_pos = 0;
    }

    /* Generate some MFM if there is space in the MFM ring buffer. */
    mfmp = mfm->prod / 32; /* MFM longs */
    mfmc = mfm->cons / 32; /* MFM longs */
    mfmlen = mfm->len / 4; /* MFM longs */

    pr = be32toh(mfmb[(mfmp-1) % mfmlen]);
#define emit_raw(r) ({                                  \
    uint32_t _r = (r);                                  \
    mfmb[mfmp++ % mfmlen] = htobe32(_r & ~(pr << 31));  \
    pr = _r; })
#define emit_long(l) ({                                         \
    uint32_t _l = (l);                                          \
    _l &= 0x55555555u; /* data bits */                          \
    _l |= (~((l>>2)|l) & 0x55555555u) << 1; /* clock bits */    \
    emit_raw(_l); })

    if (im->adf.decode_pos == 0) {

        /* Post-index track gap */
        if ((mfmlen - (mfmp - mfmc)) < POST_IDX_GAP_BC/32)
            return FALSE;
        for (i = 0; i < POST_IDX_GAP_BC/32; i++)
            emit_long(0);

    } else if (im->adf.decode_pos == NR_SECS+1) {

        /* Pre-index track gap */
        if ((mfmlen - (mfmp - mfmc)) < PRE_IDX_GAP_BC/32)
            return FALSE;
        for (i = 0; i < PRE_IDX_GAP_BC/32-1; i++)
            emit_long(0);
        emit_raw(0xaaaaaaa0); /* write splice */
        im->adf.decode_pos = -1;

    } else {

        uint32_t sector = im->adf.decode_pos - 1;
        uint32_t info, csum, *dat = (uint32_t *)&buf[(rd->cons/8)%buflen];
        if ((mfmlen - (mfmp - mfmc)) < (544*16)/32)
            return FALSE;

        /* Sector header */

        /* sector gap */
        emit_long(0);
        /* sync */
        emit_raw(0x44894489);
        /* info word */
        info = ((0xff << 24)
                | (im->cur_track << 16)
                | (sector << 8)
                | (NR_SECS - sector));
        emit_long(even(info));
        emit_long(odd(info));
        /* label */
        for (i = 0; i < 8; i++)
            emit_long(0);
        /* header checksum */
        csum = info ^ (info >> 1);
        emit_long(0);
        emit_long(odd(csum));
        /* data checksum */
        csum = amigados_checksum(dat, 512);
        emit_long(0);
        emit_long(odd(csum));

        /* Sector data */

        for (i = 0; i < 512/4; i++)
            emit_long(even(be32toh(dat[i])));
        for (i = 0; i < 512/4; i++)
            emit_long(odd(be32toh(dat[i])));
        rd->cons += sec_sz * 8;

    }

    im->adf.decode_pos++;
    mfm->prod = mfmp * 32;

    return TRUE;
}

static uint16_t adf_rdata_flux(struct image *im, uint16_t *tbuf, uint16_t nr)
{
    return mfm_rdata_flux(im, tbuf, nr, TICKS_PER_CELL);
}

static bool_t adf_write_track(struct image *im)
{
    bool_t flush;
    struct write *write = get_write(im, im->wr_cons);
    struct image_buf *wr = &im->bufs.write_mfm;
    uint32_t *buf = wr->p;
    unsigned int buflen = wr->len / 4;
    uint32_t *w, *wrbuf = im->bufs.write_data.p;
    uint32_t c = wr->cons / 32, p = wr->prod / 32;
    uint32_t info, dsum, csum;
    unsigned int i, sect;
    stk_time_t t;

    /* If we are processing final data then use the end index, rounded up. */
    barrier();
    flush = (im->wr_cons != im->wr_mfm);
    if (flush)
        p = (write->mfm_end + 31) / 32;

    while ((p - c) >= (542/2)) {

        /* Scan for sync word. */
        if (be32toh(buf[c++ % buflen]) != 0x44894489)
            continue;

        /* Info word (format,track,sect,sect_to_gap). */
        info = (buf[c++ % buflen] & 0x55555555) << 1;
        info |= buf[c++ % buflen] & 0x55555555;
        csum = info ^ (info >> 1);
        info = be32toh(info);
        sect = (uint8_t)(info >> 8);

        /* Label area. Scan for header checksum only. */
        for (i = 0; i < 8; i++)
            csum ^= buf[c++ % buflen];
        csum &= 0x55555555;

        /* Header checksum. */
        csum ^= (buf[c++ % buflen] & 0x55555555) << 1;
        csum ^= buf[c++ % buflen] & 0x55555555;
        csum = be32toh(csum);

        /* Check the info word and header checksum.  */
        if (((info>>16) != ((0xff<<8) | im->cur_track))
            || (sect >= NR_SECS) || (csum != 0)) {
            printk("Bad header: info=%08x csum=%08x\n", info, csum);
            continue;
        }

        /* Data checksum. */
        csum = (buf[c++ % buflen] & 0x55555555) << 1;
        csum |= buf[c++ % buflen] & 0x55555555;

        /* Data area. Decode to a write buffer and keep a running checksum. */
        dsum = 0;
        w = wrbuf;
        for (i = dsum = 0; i < 128; i++) {
            uint32_t o = buf[(c + 128) % buflen] & 0x55555555;
            uint32_t e = buf[c++ % buflen] & 0x55555555;
            dsum ^= o ^ e;
            *w++ = (e << 1) | o;
        }
        c += 128;

        /* Validate the data checksum. */
        csum = be32toh(csum ^ dsum);
        if (csum != 0) {
            printk("Bad data: csum=%08x\n", csum);
            continue;
        }

        /* All good: write out to mass storage. */
        t = stk_now();
        printk("Write %u/%u... ", (uint8_t)(info>>16), sect);
        F_lseek(&im->fp, im->adf.trk_off + sect*512);
        F_write(&im->fp, wrbuf, 512, NULL);
        printk("%u us\n", stk_diff(t, stk_now()) / STK_MHZ);
    }

    wr->cons = c * 32;

    return flush;
}

const struct image_handler adf_image_handler = {
    .open = adf_open,
    .seek_track = adf_seek_track,
    .read_track = adf_read_track,
    .rdata_flux = adf_rdata_flux,
    .write_track = adf_write_track,
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

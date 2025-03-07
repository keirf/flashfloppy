/*
 * apple2.c
 * 
 * Apple 2 raw sector files: PO, DO, DSK.
 * 
 * Written & released by Keir Fraser <keir.xen@gmail.com>
 * 
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

/* Amiga writes short bitcells (PAL: 14/7093790 us) hence long tracks. 
 * For better loader compatibility it is sensible to emulate this. */
#define DD_TRACKLEN_BC 101376 /* multiple of 32 */
#define POST_IDX_GAP_BC 1024

#define MAX_WR_BATCH 11

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

static bool_t apple2_open(struct image *im)
{
    if ((f_size(&im->fp) % (2*11*512)) || (f_size(&im->fp) == 0))
        return FALSE;

    im->nr_sides = 2;
    im->apple2.nr_secs = 11;
    im->tracklen_bc = DD_TRACKLEN_BC;
    im->ticks_per_cell = ((sampleclk_stk(im->stk_per_rev) * 16u)
                          / im->tracklen_bc);
    im->write_bc_ticks = im->ticks_per_cell / 16u;

    im->nr_cyls = f_size(&im->fp) / (2 * 11 * 512);

    if (im->nr_cyls > 90) {
        /* HD image: twice as many sectors per track, same data rate. */
        im->nr_cyls /= 2;
        im->stk_per_rev *= 2;
        im->apple2.nr_secs *= 2;
        im->tracklen_bc *= 2;
    }

    im->apple2.pre_idx_gap_bc = (im->tracklen_bc
                              - im->apple2.nr_secs * 544 * 16
                              - POST_IDX_GAP_BC);

    volume_cache_init(im->bufs.write_data.p + MAX_WR_BATCH * 512,
                      im->bufs.write_data.p + im->bufs.write_data.len);

    return TRUE;
}

#define apple2_po_open apple2_open
#define apple2_do_open apple2_open
#define apple2_dsk_open apple2_open

static void apple2_setup_track(
    struct image *im, uint16_t track, uint32_t *start_pos)
{
    struct image_buf *rd = &im->bufs.read_data;
    struct image_buf *bc = &im->bufs.read_bc;
    uint32_t decode_off, sector, start_ticks = start_pos ? *start_pos : 0;

    if ((im->cur_track ^ track) & ~1) {
        /* New cylinder: Refresh the sector maps (ordered by sector #). */
        unsigned int sect;
        for (sect = 0; sect < im->apple2.nr_secs; sect++)
            im->apple2.sec_map[0][sect] = im->apple2.sec_map[1][sect] = sect;
    }

    im->apple2.trk_off = track * im->apple2.nr_secs * 512;
    im->cur_track = track;

    im->cur_bc = (start_ticks * 16) / im->ticks_per_cell;
    if (im->cur_bc >= im->tracklen_bc)
        im->cur_bc = 0;
    im->cur_ticks = im->cur_bc * im->ticks_per_cell;
    im->ticks_since_flux = 0;

    decode_off = im->cur_bc;
    if (decode_off < POST_IDX_GAP_BC) {
        im->apple2.decode_pos = 0;
        im->apple2.sec_idx = 0;
    } else {
        decode_off -= POST_IDX_GAP_BC;
        sector = decode_off / (544*16);
        decode_off %= 544*16;
        im->apple2.decode_pos = sector + 1;
        im->apple2.sec_idx = sector;
        if (im->apple2.sec_idx >= im->apple2.nr_secs)
            im->apple2.sec_idx = 0;
    }

    rd->prod = rd->cons = 0;
    bc->prod = bc->cons = 0;

    if (start_pos) {
        image_read_track(im);
        bc->cons = decode_off;
    } else {
        im->apple2.sec_idx = 0;
        im->apple2.written_secs = 0;
    }
}

static bool_t apple2_read_track(struct image *im)
{
    const UINT sec_sz = 512;
    struct image_buf *rd = &im->bufs.read_data;
    struct image_buf *bc = &im->bufs.read_bc;
    uint32_t *buf = rd->p;
    uint32_t pr, *bc_b = bc->p;
    uint32_t bc_len, bc_mask, bc_space, bc_p, bc_c;
    unsigned int hd = im->cur_track & 1;
    unsigned int i;

    if (rd->prod == rd->cons) {
        unsigned int sector = im->apple2.sec_map[hd][im->apple2.sec_idx];
        F_lseek(&im->fp, im->apple2.trk_off + sector * sec_sz);
        F_read(&im->fp, buf, sec_sz, NULL);
        rd->prod++;
        im->apple2.sec_idx++;
        if (im->apple2.sec_idx >= im->apple2.nr_secs)
            im->apple2.sec_idx = 0;
    }

    /* Generate some MFM if there is space in the raw-bitcell ring buffer. */
    bc_p = bc->prod / 32; /* MFM longs */
    bc_c = bc->cons / 32; /* MFM longs */
    bc_len = bc->len / 4; /* MFM longs */
    bc_mask = bc_len - 1;
    bc_space = bc_len - (uint16_t)(bc_p - bc_c);

    pr = be32toh(bc_b[(bc_p-1) & bc_mask]);
#define emit_raw(r) ({                                   \
    uint32_t _r = (r);                                   \
    bc_b[bc_p++ & bc_mask] = htobe32(_r & ~(pr << 31));  \
    pr = _r; })
#define emit_long(l) ({                                         \
    uint32_t _l = (l);                                          \
    _l &= 0x55555555u; /* data bits */                          \
    _l |= (~((l>>2)|l) & 0x55555555u) << 1; /* clock bits */    \
    emit_raw(_l); })

    if (im->apple2.decode_pos == 0) {

        /* Post-index track gap */
        if (bc_space < POST_IDX_GAP_BC/32)
            return FALSE;
        for (i = 0; i < POST_IDX_GAP_BC/32; i++)
            emit_long(0);

    } else if (im->apple2.decode_pos == im->apple2.nr_secs+1) {

        /* Pre-index track gap */
        if (bc_space < im->apple2.pre_idx_gap_bc/32)
            return FALSE;
        for (i = 0; i < im->apple2.pre_idx_gap_bc/32-1; i++)
            emit_long(0);
        emit_raw(0xaaaaaaa0); /* write splice */
        im->apple2.decode_pos = -1;

    } else {

        uint32_t info, csum, sec_idx = im->apple2.decode_pos - 1;
        uint32_t sector = im->apple2.sec_map[hd][sec_idx];

        if (bc_space < (544*16)/32)
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
                | (im->apple2.nr_secs - sec_idx));
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
        csum = amigados_checksum(buf, 512);
        emit_long(0);
        emit_long(odd(csum));

        /* Sector data */

        for (i = 0; i < 512/4; i++)
            emit_long(even(be32toh(buf[i])));
        for (i = 0; i < 512/4; i++)
            emit_long(odd(be32toh(buf[i])));
        rd->cons++;

    }

    im->apple2.decode_pos++;
    bc->prod = bc_p * 32;

    return TRUE;
}

static void write_batch(struct image *im, unsigned int sect, unsigned int nr)
{
    uint32_t *wrbuf = im->bufs.write_data.p;
    time_t t;

    if (nr == 0)
        return;

    t = time_now();
    printk("Write %u/%u-%u... ", im->cur_track, sect, sect+nr-1);
    F_lseek(&im->fp, im->apple2.trk_off + sect*512);
    F_write(&im->fp, wrbuf, 512*nr, NULL);
    printk("%u us\n", time_diff(t, time_now()) / TIME_MHZ);
}

static bool_t apple2_write_track(struct image *im)
{
    bool_t flush;
    struct write *write = get_write(im, im->wr_cons);
    struct image_buf *wr = &im->bufs.write_bc;
    uint32_t *buf = wr->p;
    unsigned int bufmask = (wr->len / 4) - 1;
    uint32_t *w, *wrbuf = im->bufs.write_data.p;
    uint32_t c = wr->cons / 32, p = wr->prod / 32;
    uint32_t info, dsum, csum;
    unsigned int i, sect, batch_sect, batch, max_batch;
    unsigned int hd = im->cur_track & 1;

    /* If we are processing final data then use the end index, rounded up. */
    barrier();
    flush = (im->wr_cons != im->wr_bc);
    if (flush)
        p = (write->bc_end + 31) / 32;

    batch = batch_sect = 0;
    max_batch = min_t(unsigned int,
                      im->bufs.write_data.len / 512,
                      MAX_WR_BATCH);
    w = wrbuf;

    while ((int16_t)(p - c) >= (542/2)) {

        /* Scan for sync word. */
        if (be32toh(buf[c++ & bufmask]) != 0x44894489)
            continue;

        /* Info word (format,track,sect,sect_to_gap). */
        info = (buf[c++ & bufmask] & 0x55555555) << 1;
        info |= buf[c++ & bufmask] & 0x55555555;
        csum = info ^ (info >> 1);
        info = be32toh(info);
        sect = (uint8_t)(info >> 8);

        /* Label area. Scan for header checksum only. */
        for (i = 0; i < 8; i++)
            csum ^= buf[c++ & bufmask];
        csum &= 0x55555555;

        /* Header checksum. */
        csum ^= (buf[c++ & bufmask] & 0x55555555) << 1;
        csum ^= buf[c++ & bufmask] & 0x55555555;
        csum = be32toh(csum);

        /* Check the info word and header checksum.  */
        if (((info>>16) != ((0xff<<8) | im->cur_track))
            || (sect >= im->apple2.nr_secs) || (csum != 0)) {
            printk("Bad header: info=%08x csum=%08x\n", info, csum);
            continue;
        }

        if (batch && ((sect != batch_sect + batch) || (batch >= max_batch))) {
            ASSERT(batch <= max_batch);
            write_batch(im, batch_sect, batch);
            batch = 0;
            w = wrbuf;
        }

        /* Data checksum. */
        csum = (buf[c++ & bufmask] & 0x55555555) << 1;
        csum |= buf[c++ & bufmask] & 0x55555555;

        /* Data area. Decode to a write buffer and keep a running checksum. */
        dsum = 0;
        for (i = dsum = 0; i < 128; i++) {
            uint32_t o = buf[(c + 128) & bufmask] & 0x55555555;
            uint32_t e = buf[c++ & bufmask] & 0x55555555;
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

        /* All good: add to the write-out batch. */
        if (!(im->apple2.written_secs & (1u<<sect))) {
            im->apple2.written_secs |= 1u<<sect;
            im->apple2.sec_map[hd][im->apple2.sec_idx++] = sect;
        }
        if (batch++ == 0)
            batch_sect = sect;
    }

    write_batch(im, batch_sect, batch);

    if (flush && (im->apple2.sec_idx != im->apple2.nr_secs)) {
        /* End of write: If not all sectors were correctly written,
         * force the default in-order sector map. */
        for (sect = 0; sect < im->apple2.nr_secs; sect++)
            im->apple2.sec_map[hd][sect] = sect;
    }

    wr->cons = c * 32;

    return flush;
}

const struct image_handler apple2_po_image_handler = {
    .open = apple2_po_open,
    .setup_track = apple2_setup_track,
    .read_track = apple2_read_track,
    .rdata_flux = bc_rdata_flux,
    .write_track = apple2_write_track,
};

const struct image_handler apple2_do_image_handler = {
    .open = apple2_do_open,
    .setup_track = apple2_setup_track,
    .read_track = apple2_read_track,
    .rdata_flux = bc_rdata_flux,
    .write_track = apple2_write_track,
};

const struct image_handler apple2_dsk_image_handler = {
    .open = apple2_dsk_open,
    .setup_track = apple2_setup_track,
    .read_track = apple2_read_track,
    .rdata_flux = bc_rdata_flux,
    .write_track = apple2_write_track,
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

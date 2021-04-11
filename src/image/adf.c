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

/* Amiga writes short bitcells (PAL: 14/7093790 us) hence long tracks. 
 * For better loader compatibility it is sensible to emulate this. */
#define DD_TRACKLEN_BC 101376 /* multiple of 32 */
#define POST_IDX_GAP_BC 1024

/* Shift even/odd bits into MFM data-bit positions */
#define even(x) ((x)>>1)
#define odd(x) (x)

static void progress_write(struct image *im)
{
    struct image_buf *wb = &im->adf.write_buffer;
    uint16_t idx, off, cnt;
    uint32_t c = wb->cons;

    ASSERT(im->adf.write_offsets != NULL);

    thread_yield();
    if (!F_async_isdone(im->adf.write_op))
        return;
    if (im->adf.write_cnt) {
        wb->cons += im->adf.write_cnt;
        im->adf.write_cnt = 0;
    }
    if (wb->prod == wb->cons)
        return;

    idx = c++ % wb->len;
    off = im->adf.write_offsets[idx];
    for (cnt = 1; c < wb->prod && idx + cnt < wb->len; cnt++)
        if (im->adf.write_offsets[idx+cnt] != off + cnt)
            break;
    F_lseek_async(&im->fp, off*512);
    im->adf.write_op = F_write_async(&im->fp, wb->p + idx*512, cnt*512, NULL);
    im->adf.write_cnt = cnt;
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
    if ((f_size(&im->fp) % (2*11*512)) || (f_size(&im->fp) == 0))
        return FALSE;

    im->nr_sides = 2;
    im->adf.nr_secs = 11;
    im->tracklen_bc = DD_TRACKLEN_BC;
    im->ticks_per_cell = (sysclk_stk(im->stk_per_rev) * 16u) / im->tracklen_bc;

    im->nr_cyls = f_size(&im->fp) / (2 * 11 * 512);

    if (im->nr_cyls > 90) {
        /* HD image: twice as many sectors per track, same data rate. */
        im->nr_cyls /= 2;
        im->stk_per_rev *= 2;
        im->adf.nr_secs *= 2;
        im->tracklen_bc *= 2;
    }

    im->adf.pre_idx_gap_bc = (im->tracklen_bc
                              - im->adf.nr_secs * 544 * 16
                              - POST_IDX_GAP_BC);

    return TRUE;
}

static void adf_setup_track(
    struct image *im, uint16_t track, uint32_t *start_pos)
{
    const UINT sec_sz = 512;
    struct image_buf *rd = &im->bufs.read_data;
    struct image_buf *bc = &im->bufs.read_bc;
    uint32_t decode_off, sector, sys_ticks = start_pos ? *start_pos : 0;

    if ((im->cur_track ^ track) & ~1) {
        /* New cylinder: Refresh the sector maps (ordered by sector #). */
        unsigned int sect;
        for (sect = 0; sect < im->adf.nr_secs; sect++)
            im->adf.sec_map[0][sect] = im->adf.sec_map[1][sect] = sect;
        if (im->adf.ring_io_inited)
            ring_io_shutdown(&im->adf.ring_io);
        im->adf.ring_io_inited = FALSE;
    }

    im->cur_track = track;

    im->cur_bc = (sys_ticks * 16) / im->ticks_per_cell;
    if (im->cur_bc >= im->tracklen_bc)
        im->cur_bc = 0;
    im->cur_ticks = im->cur_bc * im->ticks_per_cell;
    im->ticks_since_flux = 0;

    decode_off = im->cur_bc;
    if (decode_off < POST_IDX_GAP_BC) {
        im->adf.decode_pos = 0;
        im->adf.sec_idx = 0;
    } else {
        decode_off -= POST_IDX_GAP_BC;
        sector = decode_off / (544*16);
        decode_off %= 544*16;
        im->adf.decode_pos = sector + 1;
        im->adf.sec_idx = sector;
        if (im->adf.sec_idx >= im->adf.nr_secs)
            im->adf.sec_idx = 0;
    }

    bc->prod = bc->cons = 0;

    if (start_pos) {
        if (im->adf.ring_io_inited)
            ring_io_seek(&im->adf.ring_io,
                    im->adf.sec_map[im->cur_track&1][im->adf.sec_idx] * sec_sz,
                    FALSE, im->cur_track&1);
        im->adf.trash_bc = decode_off;
    } else {
        if (im->adf.write_offsets == NULL) {
            ring_io_shutdown(&im->adf.ring_io);
            im->adf.ring_io_inited = FALSE;

            im->adf.write_offsets = rd->p;
            im->adf.write_buffer.prod = 0;
            im->adf.write_buffer.cons = 0;
            im->adf.write_buffer.p = rd->p + 512;
            im->adf.write_buffer.len = rd->len / 512 - 1;
            ASSERT(im->adf.write_buffer.len * sizeof(*im->adf.write_offsets)
                    <= 512);
            im->adf.write_op = F_async_get_completed_op();
        }

        im->adf.sec_idx = 0;
        im->adf.written_secs = 0;
    }
}

static bool_t adf_read_track(struct image *im)
{
    const UINT sec_sz = 512;
    struct image_buf *rd = &im->bufs.read_data;
    struct image_buf *bc = &im->bufs.read_bc;
    uint32_t pr, *bc_b = bc->p;
    uint32_t bc_len, bc_mask, bc_space, bc_p, bc_c;
    unsigned int hd = im->cur_track & 1;
    unsigned int i;

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

    if (im->adf.write_offsets != NULL) {
        if (im->adf.write_buffer.prod != im->adf.write_buffer.cons) {
            progress_write(im);
            return FALSE;
        }
        im->adf.write_offsets = NULL;
        im->adf.write_buffer.p = NULL;
    }
    if (!im->adf.ring_io_inited) {
        ring_io_init(&im->adf.ring_io, &im->fp, &im->bufs.read_data,
                (im->cur_track & ~1) * im->adf.nr_secs * 512,
                ((im->cur_track & ~1) + 1) * im->adf.nr_secs * 512,
                im->adf.nr_secs);
        im->adf.ring_io.batch_secs = 2;
        im->adf.ring_io_inited = TRUE;

        ring_io_seek(&im->adf.ring_io,
                im->adf.sec_map[im->cur_track&1][im->adf.sec_idx] * sec_sz,
                FALSE, im->cur_track&1);
    }
    ring_io_progress(&im->adf.ring_io);

    if (im->adf.decode_pos == 0) {

        /* Post-index track gap */
        if (bc_space < POST_IDX_GAP_BC/32)
            return FALSE;
        for (i = 0; i < POST_IDX_GAP_BC/32; i++)
            emit_long(0);

    } else if (im->adf.decode_pos == im->adf.nr_secs+1) {

        /* Pre-index track gap */
        if (bc_space < im->adf.pre_idx_gap_bc/32)
            return FALSE;
        for (i = 0; i < im->adf.pre_idx_gap_bc/32-1; i++)
            emit_long(0);
        emit_raw(0xaaaaaaa0); /* write splice */
        im->adf.decode_pos = -1;

    } else {

        uint32_t info, csum, sec_idx = im->adf.decode_pos - 1;
        uint32_t sector = im->adf.sec_map[hd][sec_idx];
        uint32_t *buf = rd->p + ring_io_idx(&im->adf.ring_io, rd->cons);

        if (bc_space < (544*16)/32)
            return FALSE;

        if (rd->prod < rd->cons + sec_sz)
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
                | (im->adf.nr_secs - sec_idx));
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
        im->adf.sec_idx++;
        if (im->adf.sec_idx >= im->adf.nr_secs)
            im->adf.sec_idx = 0;
        ring_io_seek(&im->adf.ring_io,
                im->adf.sec_map[hd][im->adf.sec_idx] * sec_sz,
                FALSE, im->cur_track&1);
    }

    if (im->adf.trash_bc) {
        int16_t to_consume = min_t(uint16_t, bc_p - bc_c, im->adf.trash_bc);
        im->adf.trash_bc -= to_consume;
        bc->cons += to_consume * 16;
    }
    im->adf.decode_pos++;
    bc->prod = bc_p * 32;

    return TRUE;
}

static bool_t adf_write_track(struct image *im)
{
    const UINT sec_sz = 512;
    bool_t flush;
    struct write *write = get_write(im, im->wr_cons);
    struct image_buf *wr = &im->bufs.write_bc;
    uint32_t *buf = wr->p;
    unsigned int bufmask = (wr->len / 4) - 1;
    uint32_t *w;
    struct image_buf *wb = &im->adf.write_buffer;
    uint32_t c = wr->cons / 32, p = wr->prod / 32;
    uint32_t info, dsum, csum;
    unsigned int i, sect;
    unsigned int hd = im->cur_track & 1;

    /* If we are processing final data then use the end index, rounded up. */
    barrier();
    flush = (im->wr_cons != im->wr_bc);
    if (flush)
        p = (write->bc_end + 31) / 32;

    while ((int16_t)(p - c) >= 13) {
        uint32_t c_sav = c;

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
            || (sect >= im->adf.nr_secs) || (csum != 0)) {
            printk("Bad header: info=%08x csum=%08x\n", info, csum);
            continue;
        }

        if ((int16_t)(p - c_sav) < (542/2)) {
            c = c_sav;
            break;
        }

        if (wb->prod - wb->cons >= wb->len) {
            c = c_sav;
            break;
        }

        ring_io_seek(&im->adf.ring_io, sect * sec_sz, FALSE, im->cur_track&1);

        /* Data checksum. */
        csum = (buf[c++ & bufmask] & 0x55555555) << 1;
        csum |= buf[c++ & bufmask] & 0x55555555;

        /* Data area. Decode to a write buffer and key a running checksum.*/
        im->adf.write_offsets[wb->prod % wb->len]
            = im->cur_track * im->adf.nr_secs + sect;
        w = wb->p + (wb->prod % wb->len) * 512;
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

        printk("Write %u/%u...\n", im->cur_track, sect);
        wb->prod += 1;

        /* All good: add to the write-out batch. */
        if (!(im->adf.written_secs & (1u<<sect))) {
            im->adf.written_secs |= 1u<<sect;
            im->adf.sec_map[hd][im->adf.sec_idx++] = sect;
        }
    }

    progress_write(im);

    if (flush && (im->adf.sec_idx != im->adf.nr_secs)) {
        /* End of write: If not all sectors were correctly written,
         * force the default in-order sector map. */
        for (sect = 0; sect < im->adf.nr_secs; sect++)
            im->adf.sec_map[hd][sect] = sect;
    }

    wr->cons = c * 32;

    return flush && (int16_t)(p - c) < (542/2);
}

static void adf_sync(struct image *im)
{
    if (im->adf.write_offsets == NULL) {
        ring_io_shutdown(&im->adf.ring_io);
    } else {
        while (im->adf.write_buffer.prod != im->adf.write_buffer.cons) {
            progress_write(im);
            F_async_wait(im->adf.write_op);
        }
    }
}

const struct image_handler adf_image_handler = {
    .open = adf_open,
    .setup_track = adf_setup_track,
    .read_track = adf_read_track,
    .rdata_flux = bc_rdata_flux,
    .write_track = adf_write_track,
    .sync = adf_sync,

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

/*
 * ring_io.c
 * 
 * Stream file reads and writes for a looped section of a file.
 * 
 * Written & released by Keir Fraser <keir.xen@gmail.com> and Eric Anderson
 * <ejona86@gmail.com>
 * 
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

#define BIT_GET(bf, i) ((bf)[(i)/32] & (1<<((i)&31)))
#define BIT_SET(bf, i) {(bf)[(i)/32] |= (1<<((i)&31));}
#define BIT_CLR(bf, i) {(bf)[(i)/32] &= ~(1<<((i)&31));}
#define BIT_ANY(bf) (bf[0] | bf[1] | bf[2] | bf[3])

#define RING_INIT ~0

static void enqueue_io(struct ring_io *rio);

void ring_io_init(struct ring_io *rio, FIL *fp, struct image_buf *read_data,
        FSIZE_t off, FSIZE_t shadow_off, uint16_t sec_len)
{
    ASSERT(off % 512 == 0);
    ASSERT(shadow_off == ~0 || shadow_off % 512 == 0);
    memset(rio, 0, sizeof(*rio));
    rio->fp = fp;
    rio->read_data = read_data;
    rio->f_off = off;
    rio->f_shadow_off = shadow_off;
    rio->f_len = sec_len * 512;
    rio->ring_len = sec_len * 512;
    if (shadow_off == ~0) {
        if (rio->ring_len > read_data->len)
            rio->ring_len = read_data->len & ~511;
    } else {
        ASSERT(off < shadow_off ? off + sec_len*512 <= shadow_off
                                : shadow_off + sec_len*512 <= off);
        if (rio->ring_len*2 > read_data->len)
            rio->ring_len = (read_data->len / 2) & ~511;
    }
    rio->ring_off = RING_INIT;
    rio->batch_secs = 1;
    rio->trailing_secs = 0;
    ASSERT(rio->ring_len <= RING_IO_MAX_RING_LEN);

    for (int i = 0; i < rio->ring_len / 512 << (shadow_off == ~0 ? 0 : 1); i++)
        BIT_SET(rio->unread_bitfield, i);
}

static void progress_io(struct ring_io *rio)
{
    thread_yield();
    while (rio->fop_cb != NULL && F_async_isdone(rio->fop)) {
        void (*fop_cb)(struct ring_io*) = rio->fop_cb;
        rio->fop_cb = NULL;
        fop_cb(rio);
    }
}

static void register_fop_whendone(
        struct ring_io *rio, FOP fop, void (*cb)(struct ring_io *))
{
    ASSERT(rio->fop_cb == NULL);
    rio->fop = fop;
    rio->fop_cb = cb;
    thread_yield(); /* Give fop a chance to start. */
}

static void sync_complete(struct ring_io *rio)
{
    if (!BIT_ANY(rio->dirty_bitfield))
        rio->sync_needed = FALSE;
    enqueue_io(rio);
}

static void write_complete(struct ring_io *rio)
{
    enqueue_io(rio);
}

static void write_start(struct ring_io *rio)
{
    struct image_buf *rd = rio->read_data;
    FOP fop;
    uint32_t max_io_cnt, start_bit, cons;
    bool_t has_shadow = rio->f_shadow_off != ~0;
    ASSERT(rio->sync_needed);
    ASSERT(BIT_ANY(rio->dirty_bitfield));
    /* Since seeks can rewind the reader, check for writes past the producer. */
    cons = rio->wd_cons;
    while (cons + 511 < rd->prod) {
        uint32_t i = (cons % rio->ring_len) / 512;
        if (BIT_GET(rio->dirty_bitfield, i))
            break;
        if (has_shadow && BIT_GET(rio->dirty_bitfield, i + rio->ring_len/512))
            break;
        cons += 512;
    }
    if (0) printk("dirty: %08x %08x %08x %08x\n",
            rio->dirty_bitfield[3],
            rio->dirty_bitfield[2],
            rio->dirty_bitfield[1],
            rio->dirty_bitfield[0]);

    /* Find contiguous write. */
    /* Do not take into account wd_prod, because there may be a partial sector
     * to flush. */
    max_io_cnt = min_t(uint32_t,
            (rio->ring_len - cons % rio->ring_len) / 512,
            (rio->f_len - ring_io_pos(rio, cons)) / 512);
    max_io_cnt = min_t(uint8_t, rio->batch_secs, max_io_cnt);
    ASSERT(max_io_cnt);

    /* Check primary ring. */

    start_bit = (cons % rio->ring_len) / 512;
    for (rio->io_cnt = 0; rio->io_cnt < max_io_cnt; rio->io_cnt++) {
        if (!BIT_GET(rio->dirty_bitfield, start_bit + rio->io_cnt))
            break;
        /* Clear eagerly to re-write a partial flush if necessary. */
        BIT_CLR(rio->dirty_bitfield, start_bit + rio->io_cnt);
    }

    if (rio->io_cnt) {
        F_lseek_async(rio->fp, rio->f_off + ring_io_pos(rio, cons));
        fop = F_write_async(rio->fp, rd->p + cons % rio->ring_len,
                rio->io_cnt * 512, NULL);
        register_fop_whendone(rio, fop, write_complete);
        return;
    }
    ASSERT(has_shadow);

    /* There must be a shadow ring write necessary. */

    start_bit += rio->ring_len/512;
    for (rio->io_cnt = 0; rio->io_cnt < max_io_cnt; rio->io_cnt++) {
        if (!BIT_GET(rio->dirty_bitfield, start_bit + rio->io_cnt))
            break;
        /* Clear eagerly to re-write a partial flush if necessary. */
        BIT_CLR(rio->dirty_bitfield, start_bit + rio->io_cnt);
    }
    ASSERT(rio->io_cnt);
    F_lseek_async(rio->fp, rio->f_shadow_off + ring_io_pos(rio, cons));
    fop = F_write_async(rio->fp,
            rd->p + rio->ring_len + cons % rio->ring_len,
            rio->io_cnt * 512, NULL);
    register_fop_whendone(rio, fop, write_complete);
}

static void read_complete(struct ring_io *rio)
{
    for (int i = 0; i < rio->io_cnt; i++)
        BIT_CLR(rio->unread_bitfield, rio->io_idx + i);
    enqueue_io(rio);
}

static void read_start(struct ring_io *rio)
{
    struct image_buf *rd = rio->read_data;
    FOP fop;
    uint32_t max_io_cnt;
    if (0) printk("unread: %08x %08x %08x %08x\n",
            rio->unread_bitfield[3],
            rio->unread_bitfield[2],
            rio->unread_bitfield[1],
            rio->unread_bitfield[0]);

    /* Find contiguous read. */
    max_io_cnt = min_t(uint32_t,
            rio->ring_len - rd->prod % rio->ring_len,
            rio->f_len - ring_io_pos(rio, rd->prod)) / 512;
    max_io_cnt = min_t(uint8_t, max_io_cnt,
            (rio->rd_valid + rio->ring_len - rd->prod) / 512);
    max_io_cnt = min_t(uint8_t, rio->batch_secs, max_io_cnt);
    ASSERT(max_io_cnt);

    /* Check primary ring. */
    rio->io_idx = (rd->prod % rio->ring_len) / 512;
    for (rio->io_cnt = 0; rio->io_cnt < max_io_cnt; rio->io_cnt++) {
        if (!BIT_GET(rio->unread_bitfield, rio->io_idx + rio->io_cnt))
            break;
    }
    if (rio->io_cnt) {
        F_lseek_async(rio->fp, rio->f_off + ring_io_pos(rio, rd->prod));
        fop = F_read_async(rio->fp, rd->p + rd->prod % rio->ring_len,
                rio->io_cnt * 512, NULL);
        register_fop_whendone(rio, fop, read_complete);
        return;
    }
    ASSERT(rio->f_shadow_off != ~0);

    /* There must be a shadow ring read necessary. */
    rio->io_idx += rio->ring_len/512;
    for (rio->io_cnt = 0; rio->io_cnt < max_io_cnt; rio->io_cnt++) {
        if (!BIT_GET(rio->unread_bitfield, rio->io_idx + rio->io_cnt))
            break;
    }
    ASSERT(rio->io_cnt);
    F_lseek_async(rio->fp, rio->f_shadow_off + ring_io_pos(rio, rd->prod));
    fop = F_read_async(rio->fp,
            rd->p + rio->ring_len + rd->prod % rio->ring_len,
            rio->io_cnt * 512, NULL);
    register_fop_whendone(rio, fop, read_complete);
}

static void enqueue_io(struct ring_io *rio)
{
    struct image_buf *rd = rio->read_data;
    uint32_t cons;
    bool_t has_shadow = rio->f_shadow_off != ~0;

    if (rio->fop_cb != NULL)
        return;

    if (rio->sync_needed) {
        /* Advance write cursor past unmodified blocks. */
        while (rio->wd_cons + 511 < rio->wd_prod) {
            uint32_t i = (rio->wd_cons % rio->ring_len) / 512;
            if (BIT_GET(rio->dirty_bitfield, i))
                break;
            if (has_shadow
                    && BIT_GET(rio->dirty_bitfield, i + rio->ring_len/512))
                break;
            rio->wd_cons += 512;
        }

        cons = rd->cons & ~511;
        cons = min_t(uint32_t, rio->wd_cons, cons);
    } else {
        cons = rd->cons & ~511;
        ASSERT(cons >= rio->rd_valid);
        cons -= min_t(uint32_t, rio->trailing_secs*512, cons);
        cons = max_t(uint32_t, cons, rio->rd_valid);
    }

    if (rd->prod + rio->batch_secs * 512 < cons)
        /* Jump forward. */
        rd->prod = cons;

    if (rio->ring_len == rio->f_len)
        /* Fully buffered, so no need to BIT_SET unread_bitfield. */
        rio->rd_valid = cons;
    else {
        /* Invalidate read data to open up space for new reads. Do it in
         * batches to optimize I/O throughput. */
        if (rio->rd_valid + rio->batch_secs * 512 <= cons
                && rio->rd_valid + rio->ring_len <= rd->prod) {
            for (int i = 0; i < rio->batch_secs; i++) {
                uint32_t p = (rio->rd_valid % rio->ring_len) / 512;
                BIT_SET(rio->unread_bitfield, p);
                if (has_shadow)
                    BIT_SET(rio->unread_bitfield, p + rio->ring_len/512);
                rio->rd_valid += 512;
            }
        }
    }

    /* Advance read cursor past already read blocks. */
    while (rio->rd_valid + rio->ring_len > rd->prod) {
        uint32_t i = (rd->prod % rio->ring_len) / 512;
        if (BIT_GET(rio->unread_bitfield, i))
            break;
        if (has_shadow && BIT_GET(rio->unread_bitfield, i + rio->ring_len/512))
            break;
        rd->prod += 512;
    }

    if (!rio->disable_reading && rio->rd_valid + rio->ring_len > rd->prod) {
        read_start(rio);
        return;
    }

    if (rio->sync_needed) {
        if (BIT_ANY(rio->dirty_bitfield))
            write_start(rio);
        else
            register_fop_whendone(rio, F_sync_async(rio->fp), sync_complete);
        return;
    }
}

void ring_io_sync(struct ring_io *rio)
{
    struct image_buf *rd = rio->read_data;
    uint8_t batch_secs = rio->batch_secs;
    ASSERT(!rio->writing || rio->wd_prod == rd->cons); /* Missing a flush? */
    /* Write out as quickly as possible. Avoid lingering reads, as the caller
     * will likely call ring_io_init() just after this.  */
    rio->disable_reading = TRUE;
    rio->batch_secs = 255;
    while (rio->sync_needed) {
        ASSERT(rio->fop_cb != NULL);
        progress_io(rio);
    }
    rio->batch_secs = batch_secs;
    rio->disable_reading = FALSE;
}

void ring_io_shutdown(struct ring_io *rio)
{
    if (rio->fop_cb == NULL)
        return;
    F_async_wait(rio->fop);
}

void ring_io_seek(
        struct ring_io *rio, uint32_t pos, bool_t writing, bool_t shadow)
{
    struct image_buf *rd = rio->read_data;
    bool_t has_shadow = rio->f_shadow_off != ~0;
    ASSERT(!rio->writing || rio->wd_prod == rd->cons); /* Missing a flush? */
    ASSERT(!shadow || rio->f_shadow_off != ~0);

    rio->writing = writing;
    rio->shadow_active = shadow;
    if (rio->ring_off == RING_INIT) {
        rd->prod = rio->rd_valid = 0;
        rd->cons = pos % 512;
        rio->ring_off = pos & ~511;
    } else {
        uint32_t valid_pos = ring_io_pos(rio, rio->rd_valid);
        if (valid_pos > pos)
            pos += rio->f_len;
        rd->cons = rio->rd_valid + pos - valid_pos;
        rd->prod = rd->cons & ~511;
    }
    /* Advance read cursor past already read blocks. */
    while (rio->rd_valid + rio->ring_len > rd->prod) {
        uint32_t i = (rd->prod % rio->ring_len) / 512;
        if (BIT_GET(rio->unread_bitfield, i))
            break;
        if (has_shadow && BIT_GET(rio->unread_bitfield, i + rio->ring_len/512))
            break;
        rd->prod += 512;
    }
    if (writing) {
        rio->wd_prod = rd->cons;
        if (!rio->sync_needed)
            rio->wd_cons = rio->wd_prod & ~511;
        else
            rio->wd_cons = min_t(uint32_t, rio->wd_cons, rio->wd_prod & ~511);
    }

    enqueue_io(rio);
}

static void flush(struct ring_io *rio, bool_t partial)
{
    struct image_buf *rd = rio->read_data;
    ASSERT(rio->writing);
    ASSERT(rd->cons - rio->wd_prod < 512);
    if (partial && rio->wd_prod < rd->cons) {
        BIT_SET(rio->dirty_bitfield, ring_io_idx(rio, rio->wd_prod) / 512);
        BIT_SET(rio->dirty_bitfield, ring_io_idx(rio, rd->cons - 1) / 512);
        rio->wd_prod = rd->cons;
    }
    enqueue_io(rio);
}

void ring_io_progress(struct ring_io *rio)
{
    struct image_buf *rd = rio->read_data;
    if (rio->writing && rio->wd_prod < rd->cons) {
        uint32_t saved_wd_prod = rio->wd_prod;
        bool_t doflush = FALSE;
        while (rio->wd_prod + 512 <= rd->cons) {
            BIT_SET(rio->dirty_bitfield, ring_io_idx(rio, rio->wd_prod) / 512);
            rio->wd_prod += 512;
            doflush = TRUE;
        }

        if (!rio->sync_needed) {
            rio->sync_needed = TRUE;
            rio->wd_cons = saved_wd_prod & ~511;
        }

        if (doflush)
            flush(rio, FALSE);
    }
    progress_io(rio);
    enqueue_io(rio);
    if (!rio->writing && !rio->sync_needed
            && rd->cons >= rio->ring_len
            && rio->rd_valid >= rio->ring_len) {
        rd->prod -= rio->ring_len;
        rd->cons -= rio->ring_len;
        rio->rd_valid -= rio->ring_len;
        rio->ring_off += rio->ring_len;
        if (rio->ring_off >= rio->f_len)
            rio->ring_off -= rio->f_len;
    }
}

void ring_io_flush(struct ring_io *rio)
{
    if (rio->writing) {
        ring_io_progress(rio);
        flush(rio, TRUE);
    }
}

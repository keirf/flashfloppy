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
        FSIZE_t off, uint16_t sec_len, uint8_t batch_secs,
        uint8_t trailing_secs)
{
    memset(rio, 0, sizeof(*rio));
    rio->fp = fp;
    rio->read_data = read_data;
    rio->f_off = off;
    rio->f_len = sec_len * 512;
    rio->ring_len = sec_len * 512;
    // FIXME: this doesn't allow image driver to 'steal' space from read_data
    if (rio->ring_len > read_data->len)
        rio->ring_len = read_data->len & ~511;
    rio->ring_off = RING_INIT;
    rio->batch_secs = batch_secs;
    rio->trailing_len = trailing_secs * 512;
    ASSERT(rio->ring_len <= RING_IO_MAX_RING_LEN);
    ASSERT(rio->ring_len >= (rio->batch_secs * 2 + trailing_secs) * 512);
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
    rio->wd_cons += (rio->io_cnt-1) * 512;
    /* A partial sector flush followed by more writing could re-dirty the last
     * sector. A partial sector may not even be complete yet. */
    if (!BIT_GET(rio->dirty_bitfield, ring_io_idx(rio, rio->wd_cons)/512)
            && rio->wd_cons + 512 <= rio->wd_prod)
        rio->wd_cons += 512;
    ASSERT(rio->wd_cons <= rio->wd_prod);
    enqueue_io(rio);
}

static void write_start(struct ring_io *rio)
{
    struct image_buf *rd = rio->read_data;
    FOP fop;
    uint32_t max_io_cnt, start_bit;
    ASSERT(rio->sync_needed);
    ASSERT(BIT_ANY(rio->dirty_bitfield));
    /* Skip unmodified. */
    while (rio->wd_cons + 511 < rio->wd_prod
            && !BIT_GET(rio->dirty_bitfield, ring_io_idx(rio, rio->wd_cons)/512))
        rio->wd_cons += 512;

    /* Find contiguous write. */
    /* Do not take into account wd_prod, because there may be a partial sector
     * to flush. */
    max_io_cnt = min_t(uint32_t,
            (rio->ring_len - ring_io_idx(rio, rio->wd_cons)) / 512,
            (rio->f_len - ring_io_pos(rio, rio->wd_cons)) / 512);
    max_io_cnt = min_t(uint8_t, rio->batch_secs, max_io_cnt);
    ASSERT(max_io_cnt);
    start_bit = ring_io_idx(rio, rio->wd_cons) / 512;
    for (rio->io_cnt = 0; rio->io_cnt < max_io_cnt; rio->io_cnt++) {
        if (!BIT_GET(rio->dirty_bitfield, start_bit + rio->io_cnt))
            break;
        /* Clear eagerly to re-write a partial flush if necessary. */
        BIT_CLR(rio->dirty_bitfield, start_bit + rio->io_cnt);
    }

    F_lseek_async(rio->fp, rio->f_off + ring_io_pos(rio, rio->wd_cons));

    fop = F_write_async(rio->fp, rd->p + ring_io_idx(rio, rio->wd_cons),
            rio->io_cnt * 512, NULL);
    register_fop_whendone(rio, fop, write_complete);
}

static void read_complete(struct ring_io *rio)
{
    struct image_buf *rd = rio->read_data;
    rd->prod += rio->io_cnt * 512;
    if (rio->ring_len == rio->f_len)
        rio->read_bytes += rio->io_cnt * 512;
    enqueue_io(rio);
}

static void read_start(struct ring_io *rio)
{
    struct image_buf *rd = rio->read_data;
    FOP fop;
    uint32_t max_cnt = min_t(uint32_t,
            rio->ring_len - ring_io_idx(rio, rd->prod),
            rio->f_len - ring_io_pos(rio, rd->prod));
    if (rio->ring_len == rio->f_len)
        max_cnt = min_t(uint32_t, max_cnt, rio->f_len - rio->read_bytes);
    else {
        uint32_t cons = rio->sync_needed ? rio->wd_cons : rd->cons;
        /* Checking the ring length should not actually matter, because
         * enqueue_io() wouldn't call read_start() unless there was a full
         * batch to read. But that check is an optimization whereas this is for
         * correctness. */
        max_cnt = min_t(uint32_t, max_cnt, rio->ring_len + cons - rd->prod);
    }
    rio->io_cnt = min_t(uint8_t, rio->batch_secs, max_cnt / 512);
    if (rd->prod + rio->io_cnt * 512 - rio->rd_valid > rio->ring_len)
        rio->rd_valid = rd->prod + rio->io_cnt * 512 - rio->ring_len;

    F_lseek_async(rio->fp, rio->f_off + ring_io_pos(rio, rd->prod));

    fop = F_read_async(rio->fp, rd->p + ring_io_idx(rio, rd->prod),
            rio->io_cnt * 512, NULL);
    register_fop_whendone(rio, fop, read_complete);
}

static void enqueue_io(struct ring_io *rio)
{
    struct image_buf *rd = rio->read_data;
    uint32_t cons = rd->cons & ~511;
    cons -= min_t(uint32_t, rio->trailing_len, cons);
    if (rio->sync_needed)
        cons = min_t(uint32_t, rio->wd_cons, cons);
    if (rio->fop_cb != NULL)
        return;

    if (rio->disable_reading)
        ; /* Skip reading checks. */
    else if (rio->ring_len == rio->f_len) {
        if (rio->read_bytes != rio->f_len) {
            read_start(rio);
            return;
        }
        rd->prod = cons + rio->ring_len;
    } else {
        if (rd->prod < cons)
            rd->prod = cons;

        /* Only read if we can do a full batch, to optimize I/O throughput. */
        if (rd->prod + rio->batch_secs * 512 - cons <= rio->ring_len) {
            read_start(rio);
            return;
        }
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
    ASSERT(!rio->writing || rio->wd_prod == rd->cons); /* Missing a flush? */
    /* Write out as quickly as possible. Avoid lingering reads, as the caller
     * will likely call ring_io_init() just after this.  */
    rio->disable_reading = TRUE;
    while (rio->sync_needed) {
        ASSERT(rio->fop_cb != NULL);
        progress_io(rio);
    }
    rio->disable_reading = FALSE;
}

void ring_io_seek(struct ring_io *rio, uint32_t pos, bool_t writing)
{
    struct image_buf *rd = rio->read_data;
    ASSERT(!rio->writing || rio->wd_prod == rd->cons); /* Missing a flush? */

    rio->writing = writing;
    if (rio->ring_off == RING_INIT) {
        rd->prod = rio->rd_valid = 0;
        rd->cons = pos % 512;
        rio->ring_off = pos & ~511;
    } else {
        uint32_t valid_pos = ring_io_pos(rio, rio->rd_valid);
        if (valid_pos > pos)
            pos += rio->f_len;
        rd->cons = rio->rd_valid + pos - valid_pos;
    }
    if (writing) {
        rio->wd_prod = rd->cons;
        if (!rio->sync_needed)
            rio->wd_cons = rio->wd_prod & ~511;
    }

    enqueue_io(rio);
}

static void flush(struct ring_io *rio, bool_t partial)
{
    struct image_buf *rd = rio->read_data;
    ASSERT(rio->writing);
    ASSERT(rd->cons - rio->wd_prod < 512);
    if (partial && rio->wd_prod < rd->cons) {
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

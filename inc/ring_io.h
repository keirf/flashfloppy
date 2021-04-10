/*
 * ring_io.h
 * 
 * Stream file reads and writes for a looped section of a file.
 * 
 * Written & released by Keir Fraser <keir.xen@gmail.com> and Eric Anderson
 * <ejona86@gmail.com>
 * 
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

/* Since the ring is not guaranteed to be a power of 2, the read_data.prod and
 * .cons indexes need to be adjusted occassionally to avoid overflows; they may
 * be changed by any ring_io_* function except for ring_io_idx and ring_io_pos.
 */

/* There is no guarantee or requirement that rd->cons will be less than or
 * equal to rd->prod, although the consumer must not access the ring past
 * rd->prod.
 *
 * Instead of 'rd->cons == rd->prod' use 'rd->cons >= rd->prod'.
 * Instead of 'rd->cons != rd->prod' use 'rd->cons < rd->prod'.
 */

#define RING_IO_MAX_RING_LEN (64 * 1024)

struct ring_io {
    /* Options. Safe to change at any time. */
    uint8_t batch_secs, trailing_secs;

    /* Internals. */
    FIL *fp;
    struct image_buf *read_data;
    FOP fop;
    void (*fop_cb)(struct ring_io*);
    uint32_t unread_bitfield[(RING_IO_MAX_RING_LEN/512+31)/32];
    uint32_t dirty_bitfield[(RING_IO_MAX_RING_LEN/512+31)/32];
    FSIZE_t f_off;
    FSIZE_t f_shadow_off;
    uint32_t f_len;
    uint32_t ring_len, ring_off;
    uint16_t io_idx;
    uint8_t io_cnt;
    uint32_t wd_cons; /* Internal cursor of oldest write. Sector aligned. */
    uint32_t wd_prod; /* Internal cursor that follows rd->cons. */
    uint32_t rd_valid; /* Cursor of oldest valid read data. */
    bool_t sync_needed:1;
    bool_t writing:1; /* The caller is writing, per ring_io_seek. */
    bool_t shadow_active:1; /* The caller is using shadow ring, per ring_io_seek. */
    bool_t disable_reading:1; /* Inhibit read ops in the I/O scheduler. */
};

/* shadow_off != ~0 maintains a second parallel ring of the same size that
 * tracks the primary ring. */
void ring_io_init(struct ring_io *rio, FIL *fp, struct image_buf *read_data,
        FSIZE_t off, FSIZE_t shadow_off, uint16_t sec_len);
void ring_io_sync(struct ring_io *rio);
/* Stop all I/O activity and wait for outstanding I/O to complete. */
void ring_io_shutdown(struct ring_io *rio);
/* Seek ring to 'pos' in file; read_data.cons and .prod will be adjusted. If
 * 'writing', read data will be made available via read_data as normal, but
 * read_data.cons doubles as a write producer cursor.
 *
 * If shadow, then use secondary ring instead of primary one. */
void ring_io_seek(
        struct ring_io *rio, uint32_t pos, bool_t writing, bool_t shadow);
void ring_io_progress(struct ring_io *rio);
void ring_io_flush(struct ring_io *rio);

/* Find position in ring buffer. Sectors are guaranteed to be contiguous
 * (non-wrapping); it is safe to compute this idx only once per sector. */
static inline uint32_t ring_io_idx(struct ring_io *rio, uint32_t idx)
{
    return idx % rio->ring_len + (rio->shadow_active ? rio->ring_len : 0);
}

static inline uint32_t ring_io_idxend(struct ring_io *rio)
{
    return rio->ring_len + (rio->shadow_active ? rio->ring_len : 0);
}

/* Find position in file (relative to ring_io_init offset). */
static inline uint32_t ring_io_pos(struct ring_io *rio, uint32_t idx)
{
    return (rio->ring_off + idx) % rio->f_len;
}

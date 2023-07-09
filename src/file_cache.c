/*
 * file_cache.c
 * 
 * Caching I/O for a single file.
 * 
 * Written & released by Keir Fraser <keir.xen@gmail.com> and Eric Anderson
 * <ejona86@gmail.com>
 * 
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

#define SECSZ 512


/*
Writes require the cache to have a memory buffer for I/O, since it is hard to
guarantee the image's buffer will remain around.

fs requires contiguous buffers for multi-sector reads/writes.

ADF wants to write 512 byte chunks without any reads. HFE wants to write 256
byte chunks and requires reads.

If a partial-sector write occurs, if the sector isn't already buffered, trigger
a read for the sector and reject the write.

How to handle read-ahead? Esp for HFE. Esp when track doesn't fit in memory.
- If I/O in progress, do nothing
- Write LRU if it is dirty
- Check following sectors to see if they are in the cache. updating LRU. If one
  isn't in the cache, read it (evicting LRU).
- If there is a dirty sector (by scanning LRU), write it
*/

struct file_cache {
    struct cache *cache;
    FIL *fp;
    FOP fop;
    void (*fop_cb)(struct file_cache *);
    FSIZE_t cur_sector;
    FSIZE_t io_sector; /* Sector of 'fop' */
    FSIZE_t readahead_start;
    FSIZE_t readahead_end;
    uint32_t dirty_key; /* Optimization to find dirty entries */
    struct entry *dirty_val;
    uint16_t dirty_entries; /* Number of dirty entries */
    uint16_t entry_cnt;
    uint16_t readahead_prio;
    uint16_t readahead_sectors;
    uint8_t io_max;
    uint8_t subkey_bits;
    uint8_t io_cnt;
    bool_t sync_needed:1;
    bool_t sync_requested:1;
    bool_t writing:1;
    bool_t readahead:1;
};

struct entry {
    /* indexed by subkey */
    uint8_t unread_bitfield;
    uint8_t dirty_bitfield; /* Every dirty sector has unread=0. */
    uint32_t _align[0];
    uint8_t data[0][512];
};

static void enqueue_io(struct file_cache *fcache);

struct file_cache *file_cache_init(FIL *fp, uint8_t batch_secs,
        void *start, void *end)
{
    unsigned int entry_cnt;
    struct file_cache *fcache = (void *)(((uint32_t)start + 3) & ~3);
    start = (uint8_t *)fcache + sizeof(*fcache);
    ASSERT(start < end);

    memset(fcache, 0, sizeof(*fcache));
    fcache->fp = fp;
    ASSERT(batch_secs <= sizeof(((struct entry *)NULL)->unread_bitfield)*8);
    ASSERT((batch_secs & (batch_secs-1)) == 0); /* Power of 2 */

    for (uint8_t i = batch_secs; i > 1; i /= 2)
        fcache->subkey_bits++;
    fcache->cur_sector = UINT_MAX;
    fcache->io_sector = UINT_MAX;
    fcache->io_max = 0xff;
    fcache->cache = cache_init(
            start, end, sizeof(struct entry) + SECSZ*batch_secs, &entry_cnt);
    /* We need at least one cache entry for the API to function. */
    ASSERT(fcache->cache != NULL);
    fcache->entry_cnt = entry_cnt;
    return fcache;
}

static void init_entry(const struct file_cache *fcache, struct entry *entry)
{
    entry->unread_bitfield = (1 << (1 << fcache->subkey_bits))-1;
    entry->dirty_bitfield = 0;
}

static uint32_t calc_key(const struct file_cache *fcache, FSIZE_t ofs)
{
    return (ofs/SECSZ)>>fcache->subkey_bits;
}

static uint32_t calc_subkey(const struct file_cache *fcache, FSIZE_t ofs)
{
    return ofs/SECSZ & ((1<<fcache->subkey_bits)-1);
}

static void progress_io(struct file_cache *fcache)
{
    thread_yield();
    if (fcache->fop_cb != NULL && F_async_isdone(fcache->fop)) {
        void (*fop_cb)(struct file_cache*) = fcache->fop_cb;
        fcache->fop_cb = NULL;
        fop_cb(fcache);
    }
}

void file_cache_sync_wait(struct file_cache *fcache)
{
    bool_t readahead = fcache->readahead;
    ASSERT(!fcache->writing); /* Missing a flush? */
    fcache->readahead = FALSE;
    ASSERT(!fcache->sync_needed || fcache->sync_requested);
    while (fcache->sync_needed) {
        ASSERT(fcache->fop_cb != NULL);
        F_async_wait(fcache->fop);
        file_cache_progress(fcache);
    }
    fcache->readahead = readahead;
}

void file_cache_shutdown(struct file_cache *fcache)
{
    if (fcache->fop_cb == NULL)
        return;
    F_async_wait(fcache->fop);
}

const void *file_cache_peek_read(struct file_cache *fcache, FSIZE_t ofs)
{
    const struct entry *val;
    uint32_t subkey = calc_subkey(fcache, ofs);
    ASSERT(ofs % SECSZ == 0);
    ASSERT(!fcache->writing); /* Missing a flush? */
    fcache->cur_sector = ofs;
    file_cache_progress(fcache);
    val = cache_lookup(fcache->cache, calc_key(fcache, ofs));
    if (!val || (val->unread_bitfield & 1<<subkey))
        return NULL;
    return val->data[subkey];
}

static void mark_dirty(
        struct file_cache *fcache, struct entry *val, FSIZE_t ofs)
{
    if (!fcache->dirty_val) {
        fcache->dirty_key = calc_key(fcache, ofs);
        fcache->dirty_val = val;
    }
    if (!val->dirty_bitfield) {
        fcache->dirty_entries++;
        fcache->sync_needed = TRUE;
    }
    val->dirty_bitfield |= 1<<calc_subkey(fcache, ofs);
}

static void flush(struct file_cache *fcache)
{
    FSIZE_t ofs = fcache->cur_sector;
    struct entry *val;
    uint32_t subkey;
    ASSERT(fcache->writing);
    val = cache_lookup_mutable(fcache->cache, calc_key(fcache, ofs));
    subkey = calc_subkey(fcache, ofs);
    ASSERT(val && !(val->unread_bitfield & 1<<subkey));
    mark_dirty(fcache, val, ofs);
    fcache->writing = FALSE;
}

void *file_cache_peek_write(struct file_cache *fcache, FSIZE_t ofs)
{
    struct entry *val;
    uint32_t subkey;
    ASSERT(ofs % SECSZ == 0);
    if (fcache->writing && fcache->cur_sector != ofs)
        flush(fcache);
    fcache->writing = TRUE;
    fcache->cur_sector = ofs;
    file_cache_progress(fcache);
    val = cache_lookup_mutable(fcache->cache, calc_key(fcache, ofs));
    subkey = calc_subkey(fcache, ofs);
    if (!val || (val->unread_bitfield & 1<<subkey))
        return NULL;
    return val->data[subkey];
}

void file_cache_progress(struct file_cache *fcache)
{
    progress_io(fcache);
    enqueue_io(fcache);
}

void file_cache_sync(struct file_cache *fcache)
{
    if (fcache->writing)
        flush(fcache);
    if (fcache->sync_needed)
        fcache->sync_requested = TRUE;
    file_cache_progress(fcache);
}

bool_t file_cache_try_read(struct file_cache *fcache, void *buf, FSIZE_t ofs,
        UINT btr)
{
    const uint8_t *data;
    ASSERT(ofs / SECSZ == (ofs+btr-1) / SECSZ);
    data = file_cache_peek_read(fcache, ofs & ~(SECSZ-1));
    if (!data)
        return FALSE;
    ofs %= SECSZ;
    memcpy(buf, (uint8_t*)data + ofs, btr);
    return TRUE;
}

bool_t file_cache_try_write(struct file_cache *fcache, const void *buf,
        FSIZE_t ofs, UINT btw)
{
    bool_t created;
    const struct entry *val_lru;
    struct entry *val;
    uint32_t subkey, lru_id;
    ASSERT(ofs / SECSZ == (ofs + btw - 1) / SECSZ);
    if (btw != SECSZ) {
        /* slow read+write path */
        uint8_t *data = file_cache_peek_write(fcache, ofs & ~(SECSZ-1));
        if (!data)
            return FALSE;
        ofs %= SECSZ;
        memcpy((uint8_t*)data + ofs, buf, btw);
        file_cache_sync(fcache);
        return TRUE;
    }

    /* fast write-only path */
    if (fcache->writing && fcache->cur_sector != ofs)
        file_cache_sync(fcache);
    progress_io(fcache);
    val_lru = cache_lru_mutable(fcache->cache, &lru_id);
    if (val_lru && (lru_id == calc_key(fcache, fcache->io_sector)
                || val_lru->dirty_bitfield)) {
        enqueue_io(fcache);
        return FALSE;
    }
    fcache->cur_sector = ofs;
    val = cache_update_mutable(fcache->cache, calc_key(fcache, ofs), &created);
    if (created)
        init_entry(fcache, val);
    subkey = calc_subkey(fcache, ofs);
    if (fcache->io_sector <= ofs
            && ofs < fcache->io_sector + fcache->io_cnt*512
            && (val->unread_bitfield & 1<<subkey)) {
        /* A read op is in progress for this sector. */
        return FALSE;
    }
    val->unread_bitfield &= ~(1 << subkey);
    mark_dirty(fcache, val, ofs);
    ofs %= SECSZ;
    memcpy(val->data[subkey] + ofs, buf, btw);
    enqueue_io(fcache);
    return TRUE;
}

void file_cache_read(struct file_cache *fcache, void *buf, FSIZE_t ofs,
        UINT btr)
{
    while (!file_cache_try_read(fcache, buf, ofs, btr))
        F_async_wait(fcache->fop);
}

void file_cache_write(struct file_cache *fcache, const void *buf,
        FSIZE_t ofs, UINT btw)
{
    while (!file_cache_try_write(fcache, buf, ofs, btw))
        F_async_wait(fcache->fop);
}

void file_cache_io_limit(struct file_cache *fcache, uint8_t io_max)
{
    if (!io_max)
        io_max = 0xff;
    fcache->io_max = io_max;
}

void file_cache_readahead(
        struct file_cache *fcache, FSIZE_t ofs, UINT btr, UINT prio)
{
    int sectors, entries;
    if (!btr) {
        fcache->readahead = FALSE;
        return;
    }
    fcache->readahead = TRUE;
    fcache->readahead_start = ofs & ~(SECSZ-1);
    fcache->readahead_end = (ofs+btr+SECSZ-1) & ~(SECSZ-1);

    sectors = (fcache->readahead_end - fcache->readahead_start)/SECSZ;
    entries = calc_key(fcache, ofs+btr-1) - calc_key(fcache, ofs) + 1;
    if (entries > fcache->entry_cnt) {
        /* Region does not fit in memory. Need to guarantee that readahead will
         * not load enough entries to drop cur_sector. */
        ASSERT(fcache->entry_cnt >= 3);
        /* Maximum number of sectors that can be read when the region ends are
         * in the readahead window. */
        sectors -= (entries - fcache->entry_cnt) << fcache->subkey_bits;
        /* Maximum number of sectors that can be read if cur_sector is at the
         * last sector of an entry and the region ends are in the readahead
         * window. */
        sectors -= (1 << fcache->subkey_bits) - 1;
        ASSERT(sectors >= 3);
    }
    sectors = min_t(int, sectors, 50); /* Avoid large linear scan. */

    prio = (prio+SECSZ-1)/SECSZ;
    prio = min_t(uint32_t, prio, sectors-1);
    fcache->readahead_prio = prio;
    fcache->readahead_sectors = sectors-1 - prio;
}

/* 
 * I/O Scheduler
 */

static void register_fop_whendone(
        struct file_cache *fcache, FOP fop, void (*cb)(struct file_cache *))
{
    ASSERT(fcache->fop_cb == NULL);
    fcache->fop = fop;
    fcache->fop_cb = cb;
    thread_yield(); /* Give fop a chance to start. */
}

static void sync_complete(struct file_cache *fcache)
{
    if (!fcache->dirty_entries) {
        fcache->sync_needed = FALSE;
        fcache->sync_requested = FALSE;
    }
}

static void write_complete(struct file_cache *fcache)
{
    fcache->io_sector = UINT_MAX;
    fcache->io_cnt = 0;
}

/* Write processing should avoid changing the cache LRU. */
static void write_start(
        struct file_cache *fcache, uint32_t key, struct entry *val)
{
    uint32_t subkey = 0;
    FSIZE_t sector;
    FOP fop;
    ASSERT(fcache->dirty_entries);
    ASSERT(val->dirty_bitfield);

    for (uint8_t d = val->dirty_bitfield; !(d & 1); d >>= 1, subkey++)
        ;
    sector = ((key << fcache->subkey_bits) + subkey) * SECSZ;
    for (fcache->io_cnt = 0;
            fcache->io_cnt < fcache->io_max;
            fcache->io_cnt++) {
        if (!(val->dirty_bitfield & (1 << (subkey + fcache->io_cnt))))
            break;
        /* Clear eagerly to track new writes during the I/O. */
        val->dirty_bitfield &= ~(1 << (subkey + fcache->io_cnt));
    }
    if (!val->dirty_bitfield) {
        fcache->dirty_entries--;
        /* Update dirty_key */
        if (fcache->dirty_entries) {
            uint32_t lru_id;
            struct entry *lru_val = val;
            /* Try to find a dirty entry by iterating through entries newer
             * than the current write. */
            while (lru_val && !lru_val->dirty_bitfield)
                lru_val = cache_lru_next_mutable(fcache->cache, lru_val, &lru_id);
            /* If that doesn't find an entry, iterate through all entries. */
            if (!lru_val)
                lru_val = cache_lru_search_mutable(fcache->cache, &lru_id);
            while (lru_val && !lru_val->dirty_bitfield)
                lru_val = cache_lru_next_mutable(fcache->cache, lru_val, &lru_id);
            ASSERT(lru_val);
            fcache->dirty_key = lru_id;
            fcache->dirty_val = lru_val;
        } else {
            fcache->dirty_key = 0;
            fcache->dirty_val = NULL;
        }
    }

    F_lseek_async(fcache->fp, sector);
    fop = F_write_async(fcache->fp, val->data[subkey],
            fcache->io_cnt*SECSZ, NULL);
    register_fop_whendone(fcache, fop, write_complete);
}

static void read_complete(struct file_cache *fcache)
{
    uint32_t key;
    uint32_t subkey;
    struct entry *val;
    key = calc_key(fcache, fcache->io_sector);
    subkey = calc_subkey(fcache, fcache->io_sector);
    val = cache_lookup_mutable(fcache->cache, key);
    for (int i = 0; i < fcache->io_cnt; i++)
        val->unread_bitfield &= ~(1 << (subkey + i));
    fcache->io_sector = UINT_MAX;
    fcache->io_cnt = 0;
}

static bool_t read_start(struct file_cache *fcache, FSIZE_t sector)
{
    uint32_t lru_id;
    struct entry *val;
    bool_t created;
    uint32_t key, subkey;
    FOP fop;

    /* Write LRU if it is dirty, to avoid it being lost. */
    val = cache_lru_mutable(fcache->cache, &lru_id);
    if (val && val->dirty_bitfield) {
        write_start(fcache, lru_id, val);
        return TRUE;
    }

    key = calc_key(fcache, sector);
    subkey = calc_subkey(fcache, sector);
    val = cache_update_mutable(fcache->cache, key, &created);
    if (created)
        init_entry(fcache, val);
    if (!(val->unread_bitfield & (1<<subkey)))
        return FALSE;

    fcache->io_sector = sector;
    for (fcache->io_cnt = 1;
            fcache->io_cnt < fcache->io_max;
            fcache->io_cnt++) {
        if (!(val->unread_bitfield & (1 << (subkey + fcache->io_cnt))))
            break;
    }
    F_lseek_async(fcache->fp, sector);
    fop = F_read_async(fcache->fp, val->data[subkey],
            fcache->io_cnt*SECSZ, NULL);
    register_fop_whendone(fcache, fop, read_complete);
    return TRUE;
}

static bool_t enqueue_readahead(
        struct file_cache *fcache, FSIZE_t *pread_sector, uint16_t limit)
{
    FSIZE_t read_sector = *pread_sector;
    if (!fcache->readahead
            || fcache->readahead_start > read_sector
            || read_sector >= fcache->readahead_end)
        return FALSE;

    for (int i = 0; i < limit; i++) {
        read_sector += SECSZ;
        if (read_sector >= fcache->readahead_end)
            read_sector = fcache->readahead_start;
        if (read_start(fcache, read_sector))
            return TRUE;
    }
    *pread_sector = read_sector;
    return FALSE;
}

static void enqueue_io(struct file_cache *fcache)
{
    FSIZE_t read_sector = fcache->cur_sector;

    if (fcache->fop_cb != NULL)
        return;

    /* Read high-priority sectors. */
    if (read_sector != UINT_MAX) {
        if (read_start(fcache, read_sector))
            return;
        if (enqueue_readahead(fcache, &read_sector,
                fcache->readahead_prio))
            return;
    }
    /* Write. */
    if (fcache->dirty_entries) {
        write_start(fcache, fcache->dirty_key, fcache->dirty_val);
        return;
    }
    if (fcache->sync_requested) {
        fcache->sync_requested = FALSE;
        register_fop_whendone(fcache, F_sync_async(fcache->fp), sync_complete);
        return;
    }
    /* Read low-priority sectors. */
    if (read_sector != UINT_MAX) {
        if (enqueue_readahead(fcache, &read_sector, fcache->readahead_sectors))
            return;
    }
}

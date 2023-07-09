/*
 * file_cache.h
 * 
 * Caching I/O for a single file.
 * 
 * Written & released by Keir Fraser <keir.xen@gmail.com> and Eric Anderson
 * <ejona86@gmail.com>
 * 
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

/* The cache is a write-back cache and uses an I/O scheduler to schedule reads
 * and writes, with preference given to reads. Cache tracking is per-sector of
 * 512 bytes.
 *
 * The cache tries to batch reads and writes into batches of maximum size
 * 'batch_secs'. The file is split into aligned groups of batch_secs, and
 * batching cannot cross the boundary of groups. A read batch starts from the
 * sector requested and ends at the first already-read sector or end of the
 * group. Writes are not delayed to form a batch, but batches form if there are
 * delays due to reads or slow writes.
 *
 * In addition to batch reads, readahead can be enabled via
 * file_cache_readahead(). Reads and writes within the provided region will
 * cause the scheduler to read additional sectors, wrapping around when getting
 * to the end of the region.
 */

struct file_cache;

struct file_cache *file_cache_init(FIL *fp, uint8_t batch_secs,
        void *start, void *end);
/* Waits until written data is flushed and synced to storage. */
void file_cache_sync_wait(struct file_cache *fcache);
/* Stops scheduling I/O and waits for outstanding I/O to complete. To ensure
 * data is not lost, use file_cache_sync_wait() first. */
void file_cache_shutdown(struct file_cache *fcache);
/* Run the I/O scheduler without requesting new I/O. Necessary for periods
 * without I/O operations for writing and readahead. */
void file_cache_progress(struct file_cache *fcache);
/* Limit I/O operation size potentially below that of batch_sec. 0 disables the
 * limit. */
void file_cache_io_limit(struct file_cache *fcache, uint8_t io_max);
/* Enable readahead for I/O ops within the specified region. @prio bytes are
 * higher priority than writes. */
void file_cache_readahead(
        struct file_cache *fcache, FSIZE_t ofs, UINT btr, UINT prio);

/* Read within a sector. Will block until data is read. */
void file_cache_read(struct file_cache *fcache, void *buf, FSIZE_t ofs,
        UINT btr);
/* Read within a sector. If FALSE, try again later. */
bool_t file_cache_try_read(struct file_cache *fcache, void *buf, FSIZE_t ofs,
        UINT btr);
/* Read 512 bytes at sector-aligned @ofs. Returns NULL if read is not yet
 * available. */
const void *file_cache_peek_read(struct file_cache *fcache, FSIZE_t ofs);

/* Write within a sector. May block waiting on cache space, or if a partial
 * sector is being written and the sector data is not already cached. */
void file_cache_write(struct file_cache *fcache, const void *buf,
        FSIZE_t ofs, UINT btw);
/* Write within a sector. If FALSE, try again later. */
bool_t file_cache_try_write(struct file_cache *fcache, const void *buf,
        FSIZE_t ofs, UINT btw);
/* Read 512 bytes at sector-aligned @ofs . Returns NULL if
 * the write is not yet possible. If the return is non-NULL, data written to
 * the buffer is observed by the next write or file_cache_sync(). Reads are
 * not permitted until the written data is observed. */
void *file_cache_peek_write(struct file_cache *fcache, FSIZE_t ofs);

/* Flush filesystem cached data for file. Does not wait for it to complete. */
void file_cache_sync(struct file_cache *fcache);

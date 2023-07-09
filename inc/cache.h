/*
 * cache.h
 * 
 * In-memory data cache.
 * 
 * Written & released by Keir Fraser <keir.xen@gmail.com>
 * 
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

/* Use memory range (@start,@end) to cache data items of size @item_sz.
 * For non-NULL return, @entry_cnt will be set to number of entries available.
 */
struct cache *cache_init(void *start, void *end, unsigned int item_sz,
        unsigned int *entry_cnt);

/* Look up item @id in the cache. Return a pointer to cached data, or NULL. */
const void *cache_lookup(struct cache *c, uint32_t id);

/* Look up item @id in the cache. Return a pointer to cached data, or NULL. */
void *cache_lookup_mutable(struct cache *c, uint32_t id);

/* Returns the item id and cached data of the entry that might be evicted by
 * the next cache_update. Returns NULL if no entry might be evicted. */
void *cache_lru_mutable(struct cache *c, uint32_t *id);

/* Returns the item id and cache data of the next entry that might be evicted
 * after @ent. Returns NULL if @ent is the most recent entry. */
void *cache_lru_next_mutable(struct cache *c, const void* ent, uint32_t *id);

/* Returns the item id and cached data of the LRU entry, even if it is not
 * nearing eviction. Returns NULL if no entries are in the cache. */
void *cache_lru_search_mutable(struct cache *c, uint32_t *id);

/* Update item @id with data @dat. Inserts the item if not present.*/
void cache_update(struct cache *c, uint32_t id, const void *dat);

/* Update @N items (@id..@id+@N-1) with data @dat. Calls cache_update(). */
void cache_update_N(struct cache *c, uint32_t id,
                    const void *dat, unsigned int N);

/* Update item @id using returned pointer to item. Creates an uninitialized
 * item if not present, and sets @created to true. */
void *cache_update_mutable(struct cache *c, uint32_t id, bool_t *created);

/*
 * Local variables:
 * mode: C
 * c-file-style: "Linux"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */

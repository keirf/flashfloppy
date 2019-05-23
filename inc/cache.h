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

#if !defined(BOOTLOADER)

/* Use memory range (@start,@end) to cache data items of size @item_sz. */
struct cache *cache_init(void *start, void *end, unsigned int item_sz);

/* Look up item @id in the cache. Return a pointer to cached data, or NULL. */
const void *cache_lookup(struct cache *c, uint32_t id);

/* Update item @id with data @dat. Inserts the item if not present.*/
void cache_update(struct cache *c, uint32_t id, const void *dat);

/* Update @N items (@id..@id+@N-1) with data @dat. Calls cache_update(). */
void cache_update_N(struct cache *c, uint32_t id,
                    const void *dat, unsigned int N);

#else

#define cache_init(a,b,c) NULL
#define cache_lookup(a,b) NULL
#define cache_update(a,b,c) ((void)0)
#define cache_update_N(a,b,c,d) ((void)0)

#endif

/*
 * Local variables:
 * mode: C
 * c-file-style: "Linux"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */

/*
 * cache.c
 * 
 * In-memory data cache.
 * 
 * Written & released by Keir Fraser <keir.xen@gmail.com>
 * 
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

struct cache_ent {
    uint32_t id;
    struct list_head lru;
    struct list_head hash;
    uint8_t dat[0];
};

struct cache {
    uint32_t item_sz;
    struct list_head lru;
    struct list_head hash[32];
    struct cache_ent ents[0];
};

static struct cache *cache;
#define CACHE_HASH(_id) ((_id)&31)

struct cache *cache_init(void *start, void *end, unsigned int item_sz,
        unsigned int *entry_cnt)
{
    uint8_t *s, *e;
    int i, nitm;
    struct cache *c;
    struct cache_ent *cent;

    /* Cache boundaries are four-byte aligned. */
    s = (uint8_t *)(((uint32_t)start + 3) & ~3);
    e = (uint8_t *)((uint32_t)end & ~3);

    nitm = ((e - s) - (int)sizeof(*c)) / (int)(sizeof(*cent) + item_sz);
    if (nitm < 3) {
        printk("No cache: too small (%d)\n", e - s);
        return NULL;
    }
    *entry_cnt = nitm;

    /* Initialise the empty cache structure. */
    cache = c = (struct cache *)s;
    c->item_sz = item_sz;
    list_init(&c->lru);
    for (i = 0; i < ARRAY_SIZE(c->hash); i++)
        list_init(&c->hash[i]);

    /* Insert all the cache entries into the LRU list. They are not present 
     * in any hash chain as none of the cache entries are yet in use. */
    cent = c->ents;
    for (i = 0; i < nitm; i++) {
        list_insert_tail(&c->lru, &cent->lru);
        list_init(&cent->hash);
        cent = (struct cache_ent *)((uint32_t)cent + sizeof(*cent) + item_sz);
    }

    printk("Cache %u items\n", nitm);

    return c;
}

const void *cache_lookup(struct cache *c, uint32_t id)
{
    return cache_lookup_mutable(c, id);
}

void *cache_lookup_mutable(struct cache *c, uint32_t id)
{
    struct list_head *hash, *ent;
    struct cache_ent *cent;

    /* Look up the item in the appropriate hash chain. */
    hash = &c->hash[CACHE_HASH(id)];
    for (ent = hash->next; ent != hash; ent = ent->next) {
        cent = container_of(ent, struct cache_ent, hash);
        if (cent->id == id)
            goto found;
    }
    return NULL;

found:
    /* Item is cached. Move it to head of LRU and return the data. */
    list_remove(&cent->lru);
    list_insert_head(&c->lru, &cent->lru);
    return cent->dat;
}

void *cache_lru_mutable(struct cache *c, uint32_t *id)
{
    struct cache_ent *cent = container_of(c->lru.prev, struct cache_ent, lru);
    if (list_is_empty(&cent->hash))
        return NULL;
    *id = cent->id;
    return cent->dat;
}

void *cache_lru_search_mutable(struct cache *c, uint32_t *id)
{
    for (struct list_head *ent = c->lru.prev; ent != &c->lru; ent = ent->prev) {
        struct cache_ent *cent = container_of(ent, struct cache_ent, lru);
        if (list_is_empty(&cent->hash))
            continue;
        *id = cent->id;
        return cent->dat;
    }
    return NULL;
}

void *cache_lru_next_mutable(struct cache *c, const void* ent, uint32_t *id)
{
    const struct cache_ent *cent
        = container_of(ent, const struct cache_ent, dat);
    struct list_head *ent_prev = cent->lru.prev;
    struct cache_ent *cent_prev;
    if (ent_prev == &c->lru)
        return NULL;
    cent_prev = container_of(ent_prev, struct cache_ent, lru);
    *id = cent_prev->id;
    return cent_prev->dat;
}

void cache_update(struct cache *c, uint32_t id, const void *dat)
{
    bool_t created;
    void *p = cache_update_mutable(c, id, &created);
    memcpy(p, dat, c->item_sz);
}

void *cache_update_mutable(struct cache *c, uint32_t id, bool_t *created)
{
    struct cache_ent *cent;
    void *p;

    /* Already in the cache? Just update the existing data. */
    if ((p = cache_lookup_mutable(c, id)) != NULL) {
        *created = FALSE;
        return p;
    }

    /* Steal the oldest cache entry from the LRU. */
    cent = container_of(c->lru.prev, struct cache_ent, lru);
    p = cent->dat;

    /* Remove the selected cache entry from the cache. */
    list_remove(&cent->lru);
    if (!list_is_empty(&cent->hash))
        list_remove(&cent->hash);

    /* Reinsert the cache entry in the correct hash chain, and head of LRU. */
    cent->id = id;
    list_insert_head(&c->lru, &cent->lru);
    list_insert_head(&c->hash[CACHE_HASH(id)], &cent->hash);

    *created = TRUE;
    return p;
}

void cache_update_N(struct cache *c, uint32_t id,
                    const void *dat, unsigned int N)
{
    const uint8_t *p = dat;
    while (N--) {
        cache_update(c, id, p);
        id++;
        p += c->item_sz;
    }
}

/*
 * Local variables:
 * mode: C
 * c-file-style: "Linux"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */

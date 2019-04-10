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
};

struct cache {
    uint8_t *dat;
    unsigned int item_sz;
    struct list_head lru;
    struct list_head hash[32];
    struct cache_ent block[0];
};

static struct cache *cache;
#define CACHE_HASH(_id) ((_id)&31)

static void *cache_dat(struct cache *c, struct cache_ent *cent)
{
    return c->dat + (cent - c->block) * c->item_sz;
}

struct cache *cache_init(void *start, void *end, unsigned int item_sz)
{
    uint8_t *s, *e;
    int i, space, nitm, req;
    struct cache *c;

    s = (uint8_t *)(((uint32_t)start + 3) & ~3);
    e = (uint8_t *)((uint32_t)end & ~3);

    space = e - s;
    nitm = space / item_sz;
    space -= nitm * item_sz;

    if (nitm < 8) {
        printk("No cache: too small (%d)\n", e - s);
        return NULL;
    }

    req = sizeof(struct cache) + nitm * sizeof(struct cache_ent);
    while (space < req) {
        nitm--;
        space += item_sz;
        req -= sizeof(struct cache_ent);
    }

    cache = c = (struct cache *)s;
    c->dat = s + req;
    c->item_sz = item_sz;
    list_init(&c->lru);
    for (i = 0; i < ARRAY_SIZE(c->hash); i++)
        list_init(&c->hash[i]);
    for (i = 0; i < nitm; i++) {
        list_insert_tail(&c->lru, &c->block[i].lru);
        list_init(&c->block[i].hash);
    }

    printk("Cache %u items\n", nitm);

    return c;
}

const void *cache_lookup(struct cache *c, uint32_t id)
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
    return cache_dat(c, cent);
}

void cache_update(struct cache *c, uint32_t id, const void *dat)
{
    struct list_head *hash, *ent;
    struct cache_ent *cent;
    void *p;

    /* Already in the cache? Just update the existing data. */
    if ((p = (void *)cache_lookup(c, id)) != NULL)
        goto found;

    /* Find the hash chain. */
    hash = &c->hash[CACHE_HASH(id)];

    /* Steal the oldest cache entry from the LRU. */
    ent = c->lru.prev;
    cent = container_of(ent, struct cache_ent, lru);
    p = cache_dat(c, cent);

    /* Remove the selected cache entry from the cache. */
    list_remove(&cent->lru);
    if (!list_is_empty(&cent->hash))
        list_remove(&cent->hash);

    /* Reinsert the cache entry in the correct hash chain, and head of LRU. */
    cent->id = id;
    list_insert_head(&c->lru, &cent->lru);
    list_insert_head(hash, &cent->hash);

found:
    /* Finally, store away the actual item data. */
    memcpy(p, dat, c->item_sz);
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

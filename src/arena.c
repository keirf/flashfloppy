/*
 * arena.c
 * 
 * Arena-based memory allocation. Only one arena, for now.
 * 
 * Written & released by Keir Fraser <keir.xen@gmail.com>
 * 
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

#define ram_kb 64

#define ram_bytes (ram_kb*1024)

#define heap_bot (_ebss)
#define heap_top ((char *)0x20000000 + ram_bytes)

static char *heap_p;

void *arena_alloc(uint32_t sz)
{
    void *p = heap_p;
    heap_p += (sz + 3) & ~3;
    ASSERT(heap_p <= heap_top);
    return p;
}

uint32_t arena_total(void)
{
    return heap_top - heap_bot;
}

uint32_t arena_avail(void)
{
    return heap_top - heap_p;
}

void arena_init(void)
{
    heap_p = heap_bot;
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

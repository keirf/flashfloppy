/*
 * util.c
 * 
 * General-purpose utility functions.
 * 
 * Written & released by Keir Fraser <keir.xen@gmail.com>
 * 
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

void filename_extension(const char *filename, char *extension, size_t size)
{
    const char *p;
    unsigned int i;

    extension[0] = '\0';
    if ((p = strrchr(filename, '.')) == NULL)
        return;

    for (i = 0; i < (size-1); i++)
        if ((extension[i] = tolower(p[i+1])) == '\0')
            break;
    extension[i] = '\0';
}

void *memset(void *s, int c, size_t n)
{
    char *p = s;

    /* Large aligned memset? */
    size_t n32 = n & ~31;
    if (n32 && !((uint32_t)p & 3)) {
        memset_fast(p, c, n32);
        p += n32;
        n &= 31;
    }

    /* Remainder/unaligned memset. */
    while (n--)
        *p++ = c;
    return s;
}

void *memcpy(void *dest, const void *src, size_t n)
{
    char *p = dest;
    const char *q = src;

    /* Large aligned copy? */
    size_t n32 = n & ~31;
    if (n32 && !(((uint32_t)p | (uint32_t)q) & 3)) {
        memcpy_fast(p, q, n32);
        p += n32;
        q += n32;
        n &= 31;
    }

    /* Remainder/unaligned copy. */
    while (n--)
        *p++ = *q++;
    return dest;
}

asm (
".global memcpy_fast, memset_fast\n"
"memcpy_fast:\n"
"    push  {r4-r10}\n"
"1:  ldmia r1!,{r3-r10}\n"
"    stmia r0!,{r3-r10}\n"
"    subs  r2,r2,#32\n"
"    bne   1b\n"
"    pop   {r4-r10}\n"
"    bx    lr\n"
"memset_fast:\n"
"    push  {r4-r10}\n"
"    uxtb  r5, r1\n"
"    mov.w r4, #0x01010101\n"
"    muls  r4, r5\n"
"    mov   r3, r4\n"
"    mov   r5, r4\n"
"    mov   r6, r4\n"
"    mov   r7, r4\n"
"    mov   r8, r4\n"
"    mov   r9, r4\n"
"    mov   r10, r4\n"
"1:  stmia r0!,{r3-r10}\n"
"    subs  r2,r2,#32\n"
"    bne   1b\n"
"    pop   {r4-r10}\n"
"    bx    lr\n"
    );

void *memmove(void *dest, const void *src, size_t n)
{
    char *p;
    const char *q;
    if (dest < src)
        return memcpy(dest, src, n);
    p = dest; p += n;
    q = src; q += n;
    while (n--)
        *--p = *--q;
    return dest;
}

int memcmp(const void *s1, const void *s2, size_t n)
{
    const char *_s1 = s1;
    const char *_s2 = s2;
    while (n--) {
        int diff = *_s1++ - *_s2++;
        if (diff)
            return diff;
    }
    return 0;
}

size_t strlen(const char *s)
{
    size_t len = 0;
    while (*s++)
        len++;
    return len;
}

size_t strnlen(const char *s, size_t maxlen)
{
    size_t len = 0;
    while (maxlen-- && *s++)
        len++;
    return len;
}

int strcmp_ci(const char *s1, const char *s2)
{
    for (;;) {
        int diff = tolower(*s1) - tolower(*s2);
        if (diff || !*s1)
            return diff;
        s1++; s2++;
    }
    return 0;
}

int strcmp(const char *s1, const char *s2)
{
    return strncmp(s1, s2, INT_MAX);
}

int strncmp(const char *s1, const char *s2, size_t n)
{
    while (n--) {
        int diff = *s1 - *s2;
        if (diff || !*s1)
            return diff;
        s1++; s2++;
    }
    return 0;
}

char *strchr(const char *s, int c)
{
    int d;
    while ((d = *s)) {
        if (d == c) return (char *)s;
        s++;
    }
    return NULL;
}

char *strrchr(const char *s, int c)
{
    char *p = NULL;
    int d;
    while ((d = *s)) {
        if (d == c) p = (char *)s;
        s++;
    }
    return p;
}

char *strcpy(char *dest, const char *src)
{
    char *p = dest;
    const char *q = src;
    while ((*p++ = *q++) != '\0')
        continue;
    return dest;
}

int tolower(int c)
{
    if ((c >= 'A') && (c <= 'Z'))
        c += 'a' - 'A';
    return c;
}

int toupper(int c)
{
    if ((c >= 'a') && (c <= 'z'))
        c += 'A' - 'a';
    return c;
}

int isspace(int c)
{
    return (c == ' ') || (c == '\t') || (c == '\n') || (c == '\r')
        || (c == '\f') || (c == '\v');
}

long int strtol(const char *nptr, char **endptr, int base)
{
    long int val = 0;
    const char *p = nptr;
    bool_t is_neg = FALSE;
    int c;

    /* Optional whitespace prefix. */
    while (isspace(*p))
        p++;
    c = tolower(*p);

    /* Optional sign prefix: +, -. */
    if ((c == '+') || (c == '-')) {
        is_neg = (c == '-');
        c = tolower(*++p);
    }

    /* Optional base prefix: 0, 0x. */
    if (c == '0') {
        if (base == 0)
            base = 8;
        c = tolower(*++p);
        if (c == 'x') {
            if (base == 0)
                base = 16;
            if (base != 16)
                goto out;
            c = tolower(*++p);
        }
    }

    if (base == 0)
        base = 10;

    /* Digits. */
    for (;;) {
        /* Convert c to a digit [0123456789abcdefghijklmnopqrstuvwxyz]. */
        if ((c >= '0') && (c <= '9'))
            c -= '0';
        else if ((c >= 'a') && (c <= 'z'))
            c -= 'a' - 10;
        else
            break;
        if (c >= base)
            break;
        val = (val * base) + c;
        c = tolower(*++p);
    }

out:
    if (endptr)
        *endptr = (char *)p;
    return is_neg ? -val : val;
}

static void __qsort_p(void **a, int l, int r,
                    int (*compar)(const void *, const void *))
{
    int i, j, m;
    void *pivot;

    while (l < r) {

        i = l-1;
        j = r;
        m = (l+r)/2;

        pivot = a[m];
        a[m] = a[r];
        a[r] = pivot;

        while (i < j) {
            while (compar(a[++i], pivot) < 0)
                continue;
            while ((compar(a[--j], pivot) >= 0) && (i < j))
                continue;
            if (i < j) {
                void *t = a[i];
                a[i] = a[j];
                a[j] = t;
            }
        }

        a[r] = a[i];
        a[i] = pivot;

        if ((i-l) < (r-i)) {
            __qsort_p(a, l, i-1, compar);
            l = i+1;
        } else {
            __qsort_p(a, i+1, r, compar);
            r = i-1;
        }

    }
}

void qsort_p(void *base, unsigned int nr,
             int (*compar)(const void *, const void *))
{
    __qsort_p((void **)base, 0, nr-1, compar);
}

uint32_t rand(void)
{
    static uint32_t x = 0x87a2263c;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return x;
}

unsigned int popcount(uint32_t x)
{
    x = x - ((x >> 1) & 0x55555555);
    x = (x & 0x33333333) + ((x >> 2) & 0x33333333);
    return (((x + (x >> 4)) & 0x0f0f0f0f) * 0x01010101) >> 24;
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

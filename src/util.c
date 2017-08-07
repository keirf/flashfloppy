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
    while (n--)
        *p++ = c;
    return s;
}

void *memcpy(void *dest, const void *src, size_t n)
{
    char *p = dest;
    const char *q = src;
    while (n--)
        *p++ = *q++;
    return dest;
}

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

size_t strnlen(const char *s, size_t maxlen)
{
    size_t len = 0;
    while (maxlen-- && *s++)
        len++;
    return len;
}

int strcmp(const char *s1, const char *s2)
{
    return strncmp(s1, s2, ~0);
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

int tolower(int c)
{
    if ((c >= 'A') && (c <= 'Z'))
        c += 'a' - 'A';
    return c;
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

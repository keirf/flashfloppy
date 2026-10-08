/*
 * fatimg.c
 *
 * The emulated USB drive. Made from a directory of the host, it is a
 * FAT32 volume synthesized from the files: only the file system structures
 * are built in memory, and file contents are read from the host files when
 * the firmware asks for their sectors. Made from an image file or a disk, it
 * is read from there as it is. Whatever the firmware writes goes to a
 * temporary file, so that it reads back what it wrote, and never to the host
 * files, the image or the disk. It lasts until the drive is removed, and so
 * through a power cycle, as on a real drive.
 *
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>
#include <wctype.h>
#include <sys/stat.h>

#ifdef __CYGWIN__
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <sys/cygwin.h>
#endif

#include "host.h"

#ifndef O_BINARY
#define O_BINARY 0
#endif

#define SEC 512
#define SEC_PER_CLUS 8
#define CLUS (SEC * SEC_PER_CLUS)
#define RSVD_SECS 32
#define NR_FATS 2
#define MIN_CLUSTERS 66000 /* fewer than 65525 would make it FAT16 */
#define FREE_CLUSTERS 4096 /* room for the firmware to create files */
#define FAT_EOC 0x0fffffffu
#define MAX_NODES 50000
#define MAX_DEPTH 12
#define MAX_LFN 255
#define MAX_FF_CFG 65536

#define ATTR_READONLY 0x01
#define ATTR_HIDDEN 0x02
#define ATTR_SYSTEM 0x04
#define ATTR_VOLUME 0x08
#define ATTR_DIR 0x10
#define ATTR_ARCHIVE 0x20
#define ATTR_LFN 0x0f

/* Byte 12 of an entry: the base or the extension of the short name is
 * shown in lower case, so that a name needs no long name for that. */
#define NT_LOWER_BASE 0x08
#define NT_LOWER_EXT 0x10

struct node {
    struct node *next, *children, *parent;
    char *path;
    uint16_t *lfn;
    unsigned int lfn_len;
    uint8_t sfn[11], attr, nt_case;
    bool is_dir, need_lfn;
    uint32_t size, clus, nr_clus;
    time_t mtime;
    uint8_t *data; /* a directory's contents */
};

struct extent {
    uint32_t clus, nr_clus;
    struct node *node;
};

/* Where a sector that the firmware wrote is in the store of such sectors. */
struct overlay {
    uint32_t sec_plus_1; /* 0: free slot */
    uint32_t rec;
};
#define NO_REC (~0u)

struct image {
    uint8_t rsvd[RSVD_SECS * SEC];
    uint32_t *fat;
    uint32_t fat_secs, nr_clus, data_start, total_secs;
    struct node *root;
    struct extent *ext;
    unsigned int nr_ext, nr_nodes;
    struct node *fd_node;
    int fd;
    int dev_fd;        /* the image file or the disk, or -1 */
    char *ff_cfg_text; /* of an image or a disk, read on insertion */
    struct overlay *ovl;
    unsigned int ovl_size, ovl_used, nr_recs;
    uint32_t layout; /* hash of everything that places the files */
    unsigned long nr_reads, nr_writes;
    struct usb_info info;
};

/* The firmware thread reads @cur while the user interface thread replaces
 * it. A removed image is therefore freed one removal late, by which time no
 * sector read can still be in flight. */
static struct image * volatile cur;
static struct image *grave;
static struct usb_info last_info;

/* Sector access, which the script may do besides the firmware. */
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

/* The store of written sectors: a file of records, each a sector number and
 * the sector's contents. The file has no name, and a power cycle, which
 * re-executes the program, passes it on as an open file. Its records are
 * valid only for a volume with the layout that they were written to. */
#define REC_SIZE (4 + SEC)
static int store_fd = -1;
static uint32_t store_layout;

static void put16(uint8_t *p, unsigned int x)
{
    p[0] = x;
    p[1] = x >> 8;
}

static void put32(uint8_t *p, uint32_t x)
{
    put16(p, x);
    put16(p + 2, x >> 16);
}

static unsigned int get16(const uint8_t *p)
{
    return p[0] | (p[1] << 8);
}

static uint32_t get32(const uint8_t *p)
{
    return get16(p) | ((uint32_t)get16(p + 2) << 16);
}

static void *xalloc(size_t size)
{
    void *p = calloc(1, size ? size : 1);
    if (p == NULL)
        cpu_quit(3, "ffemu: out of memory");
    return p;
}

static uint32_t hash(uint32_t h, const void *data, size_t size)
{
    const uint8_t *p = data;

    while (size--)
        h = (h ^ *p++) * 16777619u;
    return h;
}

/*
 * Names.
 *
 * A short name is made as the system that would have copied the files to a
 * real drive makes it: Windows when built for Cygwin, in the OEM code page
 * of the system, and Linux otherwise, whose vfat uses code page 437 unless
 * told otherwise when mounting. The two differ in the numeric tails, in
 * which names need a long name, and in the characters they keep.
 */

/* Returns the number of UTF-16 units, or -1 if @s is not a usable name. */
static int name_to_utf16(const char *s, uint16_t *out)
{
    const unsigned char *p = (const unsigned char *)s;
    int n = 0;

    while (*p != '\0') {
        uint32_t c = *p++;
        unsigned int more = (c < 0x80) ? 0 : ((c & 0xe0) == 0xc0) ? 1
            : ((c & 0xf0) == 0xe0) ? 2 : ((c & 0xf8) == 0xf0) ? 3 : 4;
        if (more == 4)
            return -1;
        if (more)
            c &= 0x3f >> more;
        while (more--) {
            if ((*p & 0xc0) != 0x80)
                return -1;
            c = (c << 6) | (*p++ & 0x3f);
        }
        if ((c < 0x20) || (c == 0x7f) || (c > 0x10ffff)
            || ((c < 0x80) && (strchr("\"*/:<>?\\|", c) != NULL)))
            return -1;
        if (n >= MAX_LFN - 1)
            return -1;
        if (c >= 0x10000) {
            c -= 0x10000;
            out[n++] = 0xd800 | (c >> 10);
            out[n++] = 0xdc00 | (c & 0x3ff);
        } else {
            out[n++] = c;
        }
    }

#ifdef __CYGWIN__
    /* Windows cannot copy such names. */
    if ((n != 0) && (out[n-1] == '.'))
        return -1;
#else
    /* vfat drops trailing dots. */
    while ((n != 0) && (out[n-1] == '.'))
        n--;
#endif
    /* Neither copies names with trailing spaces. */
    if ((n == 0) || (out[n-1] == ' '))
        return -1;

    return n;
}

static unsigned int ascii_upper(unsigned int c)
{
    return ((c >= 'a') && (c <= 'z')) ? c - 'a' + 'A' : c;
}

/* Characters that a short name has as '_'. */
static bool sfn_replaced(unsigned int c)
{
    return (c < 0x80) && (c != 0) && (strchr("+,;=[]", c) != NULL);
}

/* Set of the short names in use in one directory. */
struct sfn_set {
    uint8_t (*name)[11];
    bool *used;
    unsigned int size; /* power of two */
};

static bool sfn_set_add(struct sfn_set *set, const uint8_t *sfn)
{
    unsigned int i, h = hash(2166136261u, sfn, 11);

    for (i = h & (set->size - 1); set->used[i]; i = (i + 1) & (set->size - 1))
        if (!memcmp(set->name[i], sfn, 11))
            return false;
    set->used[i] = true;
    memcpy(set->name[i], sfn, 11);
    return true;
}

/* Takes short name @n->sfn for @n, unless another entry has it. */
static bool sfn_try(struct node *n, struct sfn_set *set)
{
    if (n->sfn[0] == 0xe5) /* that would mark the entry deleted */
        n->sfn[0] = 0x05;
    return sfn_set_add(set, n->sfn);
}

/* The index of the dot that starts the extension of @n's long name, or the
 * length of the name if it has none: the last dot, unless nothing but dots
 * and spaces comes before it. */
static unsigned int find_dot(const struct node *n)
{
    unsigned int i, dot;

    for (dot = n->lfn_len; dot != 0; dot--)
        if (n->lfn[dot-1] == '.')
            break;
    if (dot-- == 0)
        return n->lfn_len;
    for (i = 0; i < dot; i++)
        if ((n->lfn[i] != '.') && (n->lfn[i] != ' '))
            return dot;
    return n->lfn_len;
}

/* The hash of the long name that Windows puts into a short name, as
 * RtlGenerate8dot3Name() computes it. */
static unsigned int nt_checksum(const struct node *n)
{
    uint16_t sum = 0;
    uint32_t t;
    uint64_t q;
    unsigned int i;

    for (i = 0; i < n->lfn_len; i++)
        sum = sum * 0x25 + n->lfn[i];
    t = sum * 314159269u;
    if (t & 0x80000000u)
        t = -t;
    q = (uint64_t)((int64_t)(int32_t)t * 1152921497) >> 60;
    t -= (uint32_t)q * 1000000007u;
    sum = t;
    return ((sum & 0xf000) >> 12) | ((sum & 0x0f00) >> 4)
        | ((sum & 0x00f0) << 4) | ((sum & 0x000f) << 12);
}

#ifdef __CYGWIN__

/* Character @u upcased, in the OEM code page that Windows writes short
 * names in; returns its length in bytes, or 0 if the code page lacks it. */
static unsigned int sfn_char(uint16_t u, uint8_t *out)
{
    WCHAR w = u;
    BOOL lossy = FALSE;
    int n;

    if (u < 0x80) {
        out[0] = ascii_upper(u);
        return 1;
    }
    if (LCMapStringW(LOCALE_INVARIANT, LCMAP_UPPERCASE, &w, 1, &w, 1) != 1)
        w = u;
    n = WideCharToMultiByte(CP_OEMCP, WC_NO_BEST_FIT_CHARS, &w, 1,
                            (char *)out, 2, NULL, &lossy);
    return ((n > 0) && !lossy) ? n : 0;
}

/* As the FAT driver of Windows: a name that is a valid short name in upper
 * case is one, with the case of its base and extension in byte 12 if each
 * is in one case, and with a long name besides if not or if it has other
 * than ASCII letters. Else the short name gets a numeric tail, after a hash
 * of the long name from the fifth on, or from the first if the name gives
 * fewer than three characters. */
static bool make_sfn(struct node *n, struct sfn_set *set)
{
    const uint16_t *s = n->lfn;
    unsigned int i, dot = find_dot(n), nr_base = 0, nr_ext = 0, nr_short = 0;
    unsigned int starts = 1, sum; /* bit k: a character starts at byte k */
    uint8_t base[8], ext[3], c[2];
    bool exact = true, lfn = false, base_full = false, ext_full = false;
    bool lower[2] = { false, false }, upper[2] = { false, false }, hashed;

    for (i = 0; i < n->lfn_len; i++) {
        bool in_ext = (i > dot);
        unsigned int nc;
        if (i == dot)
            continue;
        if ((s[i] == ' ') || (s[i] == '.')) {
            exact = false;
            continue;
        }
        if (sfn_replaced(s[i])) {
            c[0] = '_';
            nc = 1;
            exact = false;
        } else if ((nc = sfn_char(s[i], c)) == 0) {
            exact = false;
            continue;
        }
        if (s[i] >= 0x80)
            lfn = true;
        else if ((s[i] >= 'a') && (s[i] <= 'z'))
            lower[in_ext] = true;
        else if ((s[i] >= 'A') && (s[i] <= 'Z'))
            upper[in_ext] = true;
        if (in_ext) {
            if (ext_full || (nr_ext + nc > sizeof(ext))) {
                exact = false;
                ext_full = true;
                continue;
            }
            memcpy(&ext[nr_ext], c, nc);
            nr_ext += nc;
        } else {
            if (base_full || (nr_base + nc > sizeof(base))) {
                exact = false;
                base_full = true;
                continue;
            }
            memcpy(&base[nr_base], c, nc);
            nr_base += nc;
            starts |= 1u << nr_base;
            if (nr_base <= 6)
                nr_short = nr_base;
        }
    }

    memset(n->sfn, ' ', 11);
    memcpy(&n->sfn[8], ext, nr_ext);

    if (exact && (nr_base != 0)) {
        lfn = lfn || (lower[0] && upper[0]) || (lower[1] && upper[1]);
        memcpy(n->sfn, base, nr_base);
        if (sfn_try(n, set)) {
            n->need_lfn = lfn;
            if (!lfn)
                n->nt_case = (lower[0] ? NT_LOWER_BASE : 0)
                    | (lower[1] ? NT_LOWER_EXT : 0);
            return true;
        }
    }

    n->need_lfn = true;
    sum = nt_checksum(n);
    for (i = 1, hashed = (nr_short < 3); ; i++) {
        char tail[16];
        unsigned int nr_tail, keep;
        if (!hashed && (i > 4)) {
            hashed = true;
            i = 1;
        }
        nr_tail = hashed ? snprintf(tail, sizeof(tail), "%04X~%u", sum, i)
            : snprintf(tail, sizeof(tail), "~%u", i);
        if (nr_tail > 8)
            return false;
        keep = (nr_short < 8 - nr_tail) ? nr_short : 8 - nr_tail;
        while (!(starts & (1u << keep)))
            keep--;
        memset(n->sfn, ' ', 8);
        memcpy(n->sfn, base, keep);
        memcpy(&n->sfn[keep], tail, nr_tail);
        if (sfn_try(n, set))
            return true;
    }
}

#else /* !__CYGWIN__ */

/* Code page 437 from 0x80 on, as Unicode. */
static const uint16_t cp437[128] = {
    0x00c7, 0x00fc, 0x00e9, 0x00e2, 0x00e4, 0x00e0, 0x00e5, 0x00e7,
    0x00ea, 0x00eb, 0x00e8, 0x00ef, 0x00ee, 0x00ec, 0x00c4, 0x00c5,
    0x00c9, 0x00e6, 0x00c6, 0x00f4, 0x00f6, 0x00f2, 0x00fb, 0x00f9,
    0x00ff, 0x00d6, 0x00dc, 0x00a2, 0x00a3, 0x00a5, 0x20a7, 0x0192,
    0x00e1, 0x00ed, 0x00f3, 0x00fa, 0x00f1, 0x00d1, 0x00aa, 0x00ba,
    0x00bf, 0x2310, 0x00ac, 0x00bd, 0x00bc, 0x00a1, 0x00ab, 0x00bb,
    0x2591, 0x2592, 0x2593, 0x2502, 0x2524, 0x2561, 0x2562, 0x2556,
    0x2555, 0x2563, 0x2551, 0x2557, 0x255d, 0x255c, 0x255b, 0x2510,
    0x2514, 0x2534, 0x252c, 0x251c, 0x2500, 0x253c, 0x255e, 0x255f,
    0x255a, 0x2554, 0x2569, 0x2566, 0x2560, 0x2550, 0x256c, 0x2567,
    0x2568, 0x2564, 0x2565, 0x2559, 0x2558, 0x2552, 0x2553, 0x256b,
    0x256a, 0x2518, 0x250c, 0x2588, 0x2584, 0x258c, 0x2590, 0x2580,
    0x03b1, 0x00df, 0x0393, 0x03c0, 0x03a3, 0x03c3, 0x00b5, 0x03c4,
    0x03a6, 0x0398, 0x03a9, 0x03b4, 0x221e, 0x03c6, 0x03b5, 0x2229,
    0x2261, 0x00b1, 0x2265, 0x2264, 0x2320, 0x2321, 0x00f7, 0x2248,
    0x00b0, 0x2219, 0x00b7, 0x221a, 0x207f, 0x00b2, 0x25a0, 0x00a0,
};

/* The lower case letters of code page 437 that have upper case ones, as
 * the kernel's nls_cp437 upcases them. */
static const uint8_t cp437_upper[][2] = {
    { 0x81, 0x9a }, { 0x82, 0x90 }, { 0x84, 0x8e }, { 0x86, 0x8f },
    { 0x87, 0x80 }, { 0x91, 0x92 }, { 0x94, 0x99 }, { 0xa4, 0xa5 },
};

/* Character @u in code page 437, upcased; false if the code page lacks
 * it. */
static bool sfn_char(uint16_t u, uint8_t *out)
{
    unsigned int i;

    if (u < 0x80) {
        *out = ascii_upper(u);
        return true;
    }
    for (i = 0; (i < 128) && (cp437[i] != u); i++)
        continue;
    if (i == 128)
        return false;
    *out = 0x80 + i;
    for (i = 0; i < sizeof(cp437_upper) / sizeof(cp437_upper[0]); i++)
        if (cp437_upper[i][0] == *out)
            *out = cp437_upper[i][1];
    return true;
}

/* As vfat_create_shortname() of Linux, mounted with shortname=mixed, the
 * default: a name that is a valid short name in upper case is one, with a
 * long name besides unless it is all in upper case and ASCII. Else the
 * short name gets a numeric tail, ~1 to ~9, then a random number, which is
 * a hash of the long name here, so that the volume is reproducible. A name
 * whose short name is taken, or that gives none, cannot be created. */
static bool make_sfn(struct node *n, struct sfn_set *set)
{
    const uint16_t *s = n->lfn;
    unsigned int i, dot = find_dot(n), nr_base = 0, nr_ext = 0, keep, sum;
    uint8_t base[8], ext[3], c;
    bool exact = true, upper = true;

    for (i = 0; i < n->lfn_len; i++) {
        bool in_ext = (i > dot);
        if (i == dot)
            continue;
        if (!in_ext && (nr_base == sizeof(base))) {
            exact = false;
            continue;
        }
        if (in_ext && (nr_ext == sizeof(ext))) {
            exact = false;
            break;
        }
        if ((s[i] == ' ') || (s[i] == '.')) {
            exact = false;
            continue;
        }
        if (sfn_replaced(s[i]) || !sfn_char(s[i], &c)) {
            exact = false;
            c = '_';
        } else if ((s[i] >= 0x7f) || ((s[i] >= 'a') && (s[i] <= 'z'))) {
            upper = false;
        }
        if (in_ext)
            ext[nr_ext++] = c;
        else
            base[nr_base++] = c;
    }

    if (nr_base == 0)
        return false;
    memset(n->sfn, ' ', 11);
    memcpy(n->sfn, base, nr_base);
    memcpy(&n->sfn[8], ext, nr_ext);

    if (exact) {
        n->need_lfn = !upper;
        return sfn_try(n, set);
    }

    n->need_lfn = true;
    keep = (nr_base < 6) ? nr_base : 6;
    for (i = 1; i <= 9; i++) {
        memset(&n->sfn[keep], ' ', 8 - keep);
        n->sfn[keep] = '~';
        n->sfn[keep + 1] = '0' + i;
        if (sfn_try(n, set))
            return true;
    }

    if (keep > 2)
        keep = 2;
    for (sum = nt_checksum(n); ; sum -= 11) {
        char hex[8];
        snprintf(hex, sizeof(hex), "%04X", sum & 0xffff);
        memset(n->sfn, ' ', 8);
        memcpy(n->sfn, base, keep);
        memcpy(&n->sfn[keep], hex, 4);
        n->sfn[keep + 4] = '~';
        n->sfn[keep + 5] = '1';
        if (sfn_try(n, set))
            return true;
    }
}

#endif /* !__CYGWIN__ */

/* FAT compares names as Windows does, with the letters upcased. */
static unsigned int fold(unsigned int u)
{
    if (u < 0x80)
        return ascii_upper(u);
    if ((u >= 0xd800) && (u < 0xe000))
        return u;
    return towupper(u);
}

/* Set of the long names in one directory, compared as FAT compares them. */
struct name_set {
    struct node **node;
    unsigned int size; /* power of two */
};

static bool name_set_add(struct name_set *set, struct node *n)
{
    unsigned int i, j, h = 2166136261u;

    for (i = 0; i < n->lfn_len; i++)
        h = (h ^ fold(n->lfn[i])) * 16777619u;
    for (i = h & (set->size - 1); set->node[i] != NULL;
         i = (i + 1) & (set->size - 1)) {
        const struct node *m = set->node[i];
        if (m->lfn_len != n->lfn_len)
            continue;
        for (j = 0; j < n->lfn_len; j++)
            if (fold(m->lfn[j]) != fold(n->lfn[j]))
                break;
        if (j == n->lfn_len)
            return false;
    }
    set->node[i] = n;
    return true;
}

/*
 * Scanning the host directory.
 */

/* What copying the host file to a FAT drive would give it besides the
 * directory and archive attributes: read-only, and from Windows also hidden
 * and system. */
static unsigned int host_attr(const char *path, const struct stat *st)
{
#ifdef __CYGWIN__
    ssize_t size = cygwin_conv_path(CCP_POSIX_TO_WIN_W, path, NULL, 0);
    DWORD a = INVALID_FILE_ATTRIBUTES;
    wchar_t *w;

    if (size <= 0)
        return 0;
    w = xalloc(size);
    if (cygwin_conv_path(CCP_POSIX_TO_WIN_W, path, w, size) == 0)
        a = GetFileAttributesW(w);
    free(w);
    if (a == INVALID_FILE_ATTRIBUTES)
        return 0;
    return a & (ATTR_READONLY | ATTR_HIDDEN | ATTR_SYSTEM);
#else
    /* vfat makes a file that no one may write read-only, but no directory. */
    if (S_ISREG(st->st_mode)
        && !(st->st_mode & (S_IWUSR | S_IWGRP | S_IWOTH)))
        return ATTR_READONLY;
    return 0;
#endif
}

static int node_cmp(const void *a, const void *b)
{
    const struct node * const *x = a, * const *y = b;
    return strcmp((*x)->path, (*y)->path);
}

static void free_node(struct node *n)
{
    free(n->path);
    free(n->lfn);
    free(n->data);
    free(n);
}

static void scan_dir(struct image *im, struct node *dir, unsigned int depth)
{
    DIR *d = opendir(dir->path);
    struct dirent *de;
    struct node **list = NULL, **tail, *n;
    unsigned int i, nr = 0, max = 0;
    struct sfn_set set;
    struct name_set names;
    uint16_t name[MAX_LFN];

    if (d == NULL) {
        im->info.nr_skipped++;
        return;
    }

    while ((de = readdir(d)) != NULL) {
        struct stat st;
        size_t sz;
        char *path;
        int len;

        if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, ".."))
            continue;

        sz = strlen(dir->path) + strlen(de->d_name) + 2;
        path = xalloc(sz);
        snprintf(path, sz, "%s/%s", dir->path, de->d_name);

        len = name_to_utf16(de->d_name, name);
        if ((len < 0) || (stat(path, &st) != 0)
            || !(S_ISREG(st.st_mode) || S_ISDIR(st.st_mode))
            || (S_ISREG(st.st_mode) && (st.st_size > 0xffffffffll))
            || (S_ISDIR(st.st_mode) && (depth >= MAX_DEPTH))
            || (im->nr_nodes >= MAX_NODES)) {
            im->info.nr_skipped++;
            free(path);
            continue;
        }

        n = xalloc(sizeof(*n));
        n->parent = dir;
        n->path = path;
        n->lfn_len = len;
        n->lfn = xalloc(len * sizeof(uint16_t));
        memcpy(n->lfn, name, len * sizeof(uint16_t));
        n->is_dir = S_ISDIR(st.st_mode);
        n->size = n->is_dir ? 0 : st.st_size;
        n->mtime = st.st_mtime;
        n->attr = host_attr(path, &st);
        im->nr_nodes++;

        if (nr == max) {
            max = max ? 2*max : 64;
            list = realloc(list, max * sizeof(*list));
            if (list == NULL)
                cpu_quit(3, "ffemu: out of memory");
        }
        list[nr++] = n;
    }

    closedir(d);

    /* readdir() promises no order: make the volume reproducible. */
    if (nr != 0)
        qsort(list, nr, sizeof(*list), node_cmp);

    for (set.size = 16; set.size < 2*nr; set.size *= 2)
        continue;
    set.name = xalloc(set.size * sizeof(*set.name));
    set.used = xalloc(set.size * sizeof(*set.used));
    names.size = set.size;
    names.node = xalloc(names.size * sizeof(*names.node));

    /* A name that FAT cannot tell from an earlier one is left out, as is a
     * name that the system would not create. */
    tail = &dir->children;
    for (i = 0; i < nr; i++) {
        n = list[i];
        if (!name_set_add(&names, n)) {
            if (im->info.nr_case_dups++ == 0)
                snprintf(im->info.case_dup, sizeof(im->info.case_dup), "%s",
                         n->path + strlen(im->root->path) + 1);
        } else if (!make_sfn(n, &set)) {
            im->info.nr_skipped++;
        } else {
            *tail = n;
            tail = &n->next;
            if (n->is_dir)
                im->info.nr_dirs++;
            else
                im->info.nr_files++;
            continue;
        }
        im->nr_nodes--;
        free_node(n);
    }

    for (n = dir->children; n != NULL; n = n->next)
        if (n->is_dir)
            scan_dir(im, n, depth + 1);

    free(names.node);
    free(set.name);
    free(set.used);
    free(list);
}

/*
 * Building the volume.
 */

static unsigned int nr_dirents(const struct node *n)
{
    return 1 + (n->need_lfn ? (n->lfn_len + 12) / 13 : 0);
}

static void fat_time(time_t t, unsigned int *date, unsigned int *time)
{
    struct tm tm;

    localtime_r(&t, &tm);
    if (tm.tm_year < 80) {
        *date = (1 << 5) | 1;
        *time = 0;
        return;
    }
    *date = ((tm.tm_year - 80) << 9) | ((tm.tm_mon + 1) << 5) | tm.tm_mday;
    *time = (tm.tm_hour << 11) | (tm.tm_min << 5) | (tm.tm_sec >> 1);
}

static uint8_t *put_dirent(uint8_t *p, const uint8_t *sfn, unsigned int attr,
                           unsigned int nt_case, uint32_t clus, uint32_t size,
                           time_t mtime)
{
    unsigned int date, time;

    fat_time(mtime, &date, &time);
    memcpy(p, sfn, 11);
    p[11] = attr;
    p[12] = nt_case;
    put16(p + 14, time);
    put16(p + 16, date);
    put16(p + 18, date);
    put16(p + 20, clus >> 16);
    put16(p + 22, time);
    put16(p + 24, date);
    put16(p + 26, clus);
    put32(p + 28, size);
    return p + 32;
}

static uint8_t *put_lfn(uint8_t *p, const struct node *n)
{
    static const uint8_t offs[13] = {
        1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30 };
    unsigned int i, j, nr = (n->lfn_len + 12) / 13;
    uint8_t sum = 0;

    for (i = 0; i < 11; i++)
        sum = ((sum & 1) << 7) + (sum >> 1) + n->sfn[i];

    for (i = nr; i != 0; i--) {
        p[0] = i | ((i == nr) ? 0x40 : 0);
        p[11] = ATTR_LFN;
        p[13] = sum;
        for (j = 0; j < 13; j++) {
            unsigned int k = (i-1)*13 + j;
            put16(p + offs[j], (k < n->lfn_len) ? n->lfn[k]
                  : (k == n->lfn_len) ? 0 : 0xffff);
        }
        p += 32;
    }

    return p;
}

/* Gives @n the next @nr_clus clusters. */
static void place(struct node *n, uint32_t nr_clus, uint32_t *next)
{
    n->nr_clus = nr_clus;
    n->clus = nr_clus ? *next : 0;
    *next += nr_clus;
}

/* Directories first, so that the firmware's directory scans stay within a
 * small part of the volume. */
static void place_dirs(struct node *dir, uint32_t *next)
{
    struct node *n;
    unsigned int nr = (dir->parent == NULL) ? 1 : 2;

    for (n = dir->children; n != NULL; n = n->next)
        nr += nr_dirents(n);
    place(dir, (nr * 32 + CLUS - 1) / CLUS, next);

    for (n = dir->children; n != NULL; n = n->next)
        if (n->is_dir)
            place_dirs(n, next);
}

static void place_files(struct node *dir, uint32_t *next)
{
    struct node *n;

    for (n = dir->children; n != NULL; n = n->next) {
        if (n->is_dir)
            place_files(n, next);
        else
            place(n, ((uint64_t)n->size + CLUS - 1) / CLUS, next);
    }
}

static void add_extent(struct image *im, struct node *n)
{
    uint32_t i;

    if (n->nr_clus == 0)
        return;
    im->ext[im->nr_ext].clus = n->clus;
    im->ext[im->nr_ext].nr_clus = n->nr_clus;
    im->ext[im->nr_ext].node = n;
    im->nr_ext++;
    for (i = 0; i < n->nr_clus; i++)
        im->fat[n->clus + i] = (i == n->nr_clus - 1) ? FAT_EOC
            : n->clus + i + 1;
}

static void build_dirs(struct image *im, struct node *dir)
{
    static const uint8_t dot[11] = ".          ", dotdot[11] = "..         ";
    static const uint8_t label[11] = "FFEMU      ";
    struct node *n;
    uint8_t *p;

    p = dir->data = xalloc(dir->nr_clus * CLUS);
    add_extent(im, dir);

    if (dir->parent == NULL) {
        p = put_dirent(p, label, ATTR_VOLUME, 0, 0, 0, time(NULL));
    } else {
        /* The root directory is cluster 0 when referred to by "..". */
        uint32_t up = dir->parent->parent ? dir->parent->clus : 0;
        p = put_dirent(p, dot, ATTR_DIR, 0, dir->clus, 0, dir->mtime);
        p = put_dirent(p, dotdot, ATTR_DIR, 0, up, 0, dir->mtime);
    }

    for (n = dir->children; n != NULL; n = n->next) {
        if (n->need_lfn)
            p = put_lfn(p, n);
        p = put_dirent(p, n->sfn,
                       n->attr | (n->is_dir ? ATTR_DIR : ATTR_ARCHIVE),
                       n->nt_case, n->clus, n->size, n->mtime);
    }

    for (n = dir->children; n != NULL; n = n->next)
        if (n->is_dir)
            build_dirs(im, n);
}

static void build_files(struct image *im, struct node *dir)
{
    struct node *n;

    for (n = dir->children; n != NULL; n = n->next) {
        if (n->is_dir)
            build_files(im, n);
        else
            add_extent(im, n);
    }
}

static void build_boot_sectors(struct image *im, uint32_t nr_used)
{
    uint8_t *b = im->rsvd, *f = im->rsvd + SEC;

    b[0] = 0xeb;
    b[1] = 0x58;
    b[2] = 0x90;
    memcpy(b + 3, "FFEMU   ", 8);
    put16(b + 11, SEC);
    b[13] = SEC_PER_CLUS;
    put16(b + 14, RSVD_SECS);
    b[16] = NR_FATS;
    b[21] = 0xf8; /* media: fixed disk */
    put16(b + 24, 63);
    put16(b + 26, 255);
    put32(b + 32, im->total_secs);
    put32(b + 36, im->fat_secs);
    put32(b + 44, 2); /* root directory cluster */
    put16(b + 48, 1); /* FSInfo sector */
    put16(b + 50, 6); /* backup boot sector */
    b[64] = 0x80;
    b[66] = 0x29;
    put32(b + 67, (uint32_t)time(NULL));
    memcpy(b + 71, "FFEMU      ", 11);
    memcpy(b + 82, "FAT32   ", 8);
    put16(b + 510, 0xaa55);

    put32(f + 0, 0x41615252);
    put32(f + 484, 0x61417272);
    put32(f + 488, im->nr_clus - nr_used);
    put32(f + 492, 2 + nr_used);
    put32(f + 508, 0xaa550000);

    memcpy(im->rsvd + 6*SEC, im->rsvd, 2*SEC);
}

static void free_nodes(struct node *n)
{
    while (n != NULL) {
        struct node *next = n->next;
        free_nodes(n->children);
        free_node(n);
        n = next;
    }
}

static void free_image(struct image *im)
{
    if (im == NULL)
        return;
    if (im->fd_node != NULL)
        close(im->fd);
    if (im->dev_fd >= 0)
        close(im->dev_fd);
    free(im->ff_cfg_text);
    free(im->ovl);
    free(im->ext);
    free(im->fat);
    free_nodes(im->root);
    free(im);
}

static uint32_t hash_layout(uint32_t h, const struct node *dir)
{
    const struct node *n;

    for (n = dir->children; n != NULL; n = n->next) {
        int64_t mtime = n->mtime;
        h = hash(h, n->sfn, sizeof(n->sfn));
        h = hash(h, n->lfn, n->lfn_len * sizeof(uint16_t));
        h = hash(h, &n->size, sizeof(n->size));
        h = hash(h, &n->clus, sizeof(n->clus));
        h = hash(h, &mtime, sizeof(mtime));
        if (n->is_dir)
            h = hash_layout(h, n);
    }
    return hash(h, &dir->nr_clus, sizeof(dir->nr_clus));
}

/* The entry of host directory @dir that is named @name in any letter case,
 * a directory if @want_dir, else a file; its name into @found, which must
 * have room for @name. */
static bool find_entry(const char *dir, const char *name, bool want_dir,
                       char *found, size_t size)
{
    DIR *d = opendir(dir);
    struct dirent *de;
    struct stat st;
    char path[4096];
    bool ok = false;

    if (d == NULL)
        return false;
    while (!ok && ((de = readdir(d)) != NULL)) {
        if (strcasecmp(de->d_name, name))
            continue;
        snprintf(path, sizeof(path), "%s/%s", dir, de->d_name);
        ok = (stat(path, &st) == 0)
            && (want_dir ? S_ISDIR(st.st_mode) : S_ISREG(st.st_mode));
        if (ok && (strlen(name) < size))
            memcpy(found, de->d_name, strlen(name) + 1);
    }
    closedir(d);
    return ok;
}

/* The FF.CFG that the firmware reads, into @buf as a path on the drive, or
 * "": the one in folder FF if the drive has that folder, else the one in
 * the root. Names match in any letter case. */
static void find_ff_cfg(const char *dir, char *buf, size_t size)
{
    char path[4096], ff[8], cfg[8];

    buf[0] = '\0';
    if (find_entry(dir, "FF", true, ff, sizeof(ff))) {
        snprintf(path, sizeof(path), "%s/%s", dir, ff);
        if (find_entry(path, "FF.CFG", false, cfg, sizeof(cfg)))
            snprintf(buf, size, "%s/%s", ff, cfg);
    } else if (find_entry(dir, "FF.CFG", false, cfg, sizeof(cfg))) {
        snprintf(buf, size, "%s", cfg);
    }
}

static struct image *build_image(const char *dir, const struct stat *st)
{
    struct image *im = xalloc(sizeof(*im));
    uint32_t next = 2;

    im->dev_fd = -1;
    im->root = xalloc(sizeof(*im->root));
    im->root->path = strdup(dir);
    im->root->is_dir = true;
    im->root->mtime = st->st_mtime;
    scan_dir(im, im->root, 0);

    place_dirs(im->root, &next);
    place_files(im->root, &next);

    im->nr_clus = next - 2 + FREE_CLUSTERS;
    if (im->nr_clus < MIN_CLUSTERS)
        im->nr_clus = MIN_CLUSTERS;
    im->fat_secs = ((im->nr_clus + 2) * 4 + SEC - 1) / SEC;
    im->data_start = RSVD_SECS + NR_FATS * im->fat_secs;
    im->total_secs = im->data_start + im->nr_clus * SEC_PER_CLUS;

    im->fat = xalloc((im->nr_clus + 2) * sizeof(uint32_t));
    im->fat[0] = 0x0ffffff8;
    im->fat[1] = FAT_EOC;
    im->ext = xalloc((im->nr_nodes + 1) * sizeof(*im->ext));
    build_dirs(im, im->root);
    build_files(im, im->root);

    build_boot_sectors(im, next - 2);
    im->layout = hash_layout(hash(2166136261u, &im->nr_clus,
                                  sizeof(im->nr_clus)), im->root);

    find_ff_cfg(dir, im->info.ff_cfg, sizeof(im->info.ff_cfg));
    im->info.kind = USB_dir;
    im->info.inserted = true;
    im->info.image_bytes = (uint64_t)im->total_secs * SEC;
    return im;
}

/*
 * An image file or a disk.
 */

static bool dev_read(struct image *im, void *buf, uint32_t sec,
                     unsigned int count)
{
    off_t offs = (off_t)sec * SEC;
    size_t done = 0, size = (size_t)count * SEC;

    if (lseek(im->dev_fd, offs, SEEK_SET) != offs)
        return false;
    while (done < size) {
        ssize_t n = read(im->dev_fd, (uint8_t *)buf + done, size - done);
        if ((n < 0) && (errno == EINTR))
            continue;
        if (n <= 0)
            return false;
        done += n;
    }
    return true;
}

/* The volume that the firmware mounts, as FatFS finds it: in sector 0, or
 * else in the first of the partitions in the MBR that holds one. */
struct vol {
    struct image *im;
    uint32_t fat, root, nr_root, data, nr_clus, root_clus;
    unsigned int spc, type; /* type: 12, 16 or 32 */
    uint32_t cached;        /* the FAT sector in @buf */
    uint8_t buf[SEC];
};

/* As check_fs() of FatFS. */
static bool is_fat_vbr(const uint8_t *b)
{
    if ((b[0] != 0xeb) && (b[0] != 0xe9) && (b[0] != 0xe8))
        return false;
    if ((get16(b + 510) == 0xaa55) && !memcmp(b + 82, "FAT32   ", 8))
        return true;
    return (get16(b + 11) == SEC) && (b[13] != 0) && !(b[13] & (b[13] - 1))
        && ((b[16] == 1) || (b[16] == 2)) && (get16(b + 17) != 0)
        && (get16(b + 22) != 0);
}

static bool vol_open(struct vol *v, struct image *im)
{
    uint8_t b[SEC];
    uint32_t base = 0, part[4], nr_sys, total, fat_size;
    unsigned int i;

    v->im = im;
    v->cached = ~0u;
    if (!dev_read(im, b, 0, 1))
        return false;
    if (!is_fat_vbr(b)) {
        if (get16(b + 510) != 0xaa55)
            return false;
        for (i = 0; i < 4; i++)
            part[i] = get32(b + 446 + 16*i + 8);
        for (i = 0; i < 4; i++)
            if (part[i] && dev_read(im, b, part[i], 1) && is_fat_vbr(b))
                break;
        if (i == 4)
            return false;
        base = part[i];
    }

    if ((get16(b + 11) != SEC) || (b[13] == 0) || (b[16] == 0))
        return false;
    v->spc = b[13];
    total = get16(b + 19) ? get16(b + 19) : get32(b + 32);
    fat_size = get16(b + 22) ? get16(b + 22) : get32(b + 36);
    v->nr_root = (get16(b + 17) * 32 + SEC - 1) / SEC;
    nr_sys = get16(b + 14) + b[16] * fat_size + v->nr_root;
    if (total <= nr_sys)
        return false;
    v->fat = base + get16(b + 14);
    v->root = v->fat + b[16] * fat_size;
    v->data = v->root + v->nr_root;
    v->nr_clus = (total - nr_sys) / v->spc;
    v->type = (v->nr_clus <= 0xff5) ? 12 : (v->nr_clus <= 0xfff5) ? 16 : 32;
    v->root_clus = (v->type == 32) ? get32(b + 44) : 0;
    return true;
}

/* The cluster after @clus in its chain, or 0 at the end or on an error. */
static uint32_t vol_next(struct vol *v, uint32_t clus)
{
    uint32_t offs = (v->type == 12) ? clus + clus/2 : clus * (v->type/8);
    uint32_t x = 0;
    unsigned int i;

    for (i = 0; i < ((v->type == 32) ? 4 : 2); i++) {
        uint32_t sec = v->fat + (offs + i) / SEC;
        if (sec != v->cached) {
            if (!dev_read(v->im, v->buf, sec, 1))
                return 0;
            v->cached = sec;
        }
        x |= (uint32_t)v->buf[(offs + i) % SEC] << (8*i);
    }
    if (v->type == 12)
        x = (clus & 1) ? x >> 4 : x & 0xfff;
    else if (v->type == 32)
        x &= 0x0fffffff;
    return ((x >= 2) && (x < v->nr_clus + 2)) ? x : 0;
}

/* Reads sector @k of the directory or file at cluster @clus, 0 for the root
 * directory, moving @clus along its chain; false past the end. */
static bool vol_sector(struct vol *v, uint32_t *clus, uint32_t k, uint8_t *b)
{
    if (*clus == 0) {
        if (v->root_clus == 0)
            return (k < v->nr_root) && dev_read(v->im, b, v->root + k, 1);
        *clus = v->root_clus;
    }
    if ((k != 0) && (k % v->spc == 0))
        *clus = vol_next(v, *clus);
    return (*clus != 0)
        && dev_read(v->im, b, v->data + (*clus - 2) * v->spc + k % v->spc, 1);
}

/* The entry with short name @name in the directory at cluster @dir, 0 for
 * the root, into @e. */
static bool vol_find(struct vol *v, uint32_t dir, const char *name,
                     uint8_t *e)
{
    uint8_t b[SEC];
    uint32_t k, i;

    for (k = 0; (k < 65536) && vol_sector(v, &dir, k, b); k++) {
        for (i = 0; i < SEC; i += 32) {
            if (b[i] == 0)
                return false;
            if ((b[i] == 0xe5) || (b[i + 11] & ATTR_VOLUME))
                continue;
            if (!memcmp(b + i, name, 11)) {
                memcpy(e, b + i, 32);
                return true;
            }
        }
    }
    return false;
}

static uint32_t entry_clus(const struct vol *v, const uint8_t *e)
{
    return ((v->type == 32) ? (uint32_t)get16(e + 20) << 16 : 0)
        | get16(e + 26);
}

/* The name in entry @e as FatFS shows it without a long name. */
static char *entry_name(const uint8_t *e, char *buf)
{
    unsigned int i, n = 0;
    bool dot = false;

    for (i = 0; i < 11; i++) {
        unsigned int c = e[i];
        if (c == ' ')
            continue;
        if ((i >= 8) && !dot) {
            buf[n++] = '.';
            dot = true;
        }
        if ((c >= 'A') && (c <= 'Z')
            && (e[12] & ((i < 8) ? NT_LOWER_BASE : NT_LOWER_EXT)))
            c += 'a' - 'A';
        buf[n++] = c;
    }
    buf[n] = '\0';
    return buf;
}

/* Finds FF.CFG on an image as the firmware does, and reads it. */
static void image_ff_cfg(struct image *im)
{
    struct vol v;
    uint8_t e[32], b[SEC];
    char ff[16] = "", cfg[16] = "";
    uint32_t clus, size, k;
    char *text;

    if (!vol_open(&v, im))
        return;
    if (vol_find(&v, 0, "FF         ", e) && (e[11] & ATTR_DIR)) {
        entry_name(e, ff);
        if (((clus = entry_clus(&v, e)) == 0)
            || !vol_find(&v, clus, "FF      CFG", e))
            return;
    } else if (!vol_find(&v, 0, "FF      CFG", e)) {
        return;
    }
    if (e[11] & ATTR_DIR)
        return;
    entry_name(e, cfg);
    snprintf(im->info.ff_cfg, sizeof(im->info.ff_cfg), "%s%s%s", ff,
             ff[0] ? "/" : "", cfg);

    size = get32(e + 28);
    if (size > MAX_FF_CFG)
        size = MAX_FF_CFG;
    text = im->ff_cfg_text = xalloc(size + 1);
    clus = entry_clus(&v, e);
    for (k = 0; (k * SEC < size) && (clus != 0); k++) {
        if (!vol_sector(&v, &clus, k, b))
            break;
        memcpy(text + k * SEC, b, (size - k * SEC < SEC) ? size - k * SEC
               : SEC);
    }
}

static struct image *open_image(const char *path, const struct stat *st)
{
    struct image *im;
    off_t size;
    int fd = open(path, O_RDONLY | O_BINARY);

    if (fd < 0) {
        snprintf(last_info.error, sizeof(last_info.error), "%s: %s", path,
                 strerror(errno));
        return NULL;
    }
    size = lseek(fd, 0, SEEK_END);
    if (size < SEC) {
        snprintf(last_info.error, sizeof(last_info.error), "%s is %s", path,
                 (size < 0) ? "of unknown size" : "empty");
        close(fd);
        return NULL;
    }

    im = xalloc(sizeof(*im));
    im->dev_fd = fd;
    im->total_secs = (size / SEC > 0xffffffff) ? 0xffffffff : size / SEC;
    im->info.kind = S_ISREG(st->st_mode) ? USB_image : USB_disk;
    im->info.inserted = true;
    im->info.image_bytes = (uint64_t)im->total_secs * SEC;

    /* The firmware's writes survive a power cycle as long as the image is
     * the same, as far as can be told. */
    im->layout = hash(2166136261u, path, strlen(path));
    im->layout = hash(im->layout, &im->total_secs, sizeof(im->total_secs));
    if (S_ISREG(st->st_mode)) {
        int64_t mtime = st->st_mtime;
        im->layout = hash(im->layout, &mtime, sizeof(mtime));
    }

    image_ff_cfg(im);
    return im;
}

/*
 * Sector access, from the firmware thread.
 */

/* Returns the sector's slot in the overlay, or NULL if it has none. */
static struct overlay *ovl_find(struct image *im, uint32_t sec, bool create)
{
    unsigned int i;

    if (im->ovl_used * 2 >= im->ovl_size) {
        struct overlay *old = im->ovl;
        unsigned int old_size = im->ovl_size;
        if (!create && (old == NULL))
            return NULL;
        im->ovl_size = old_size ? 2*old_size : 256;
        im->ovl = xalloc(im->ovl_size * sizeof(*im->ovl));
        im->ovl_used = 0;
        for (i = 0; i < old_size; i++)
            if (old[i].sec_plus_1 != 0)
                *ovl_find(im, old[i].sec_plus_1 - 1, true) = old[i];
        free(old);
    }

    for (i = (sec * 2654435761u) & (im->ovl_size - 1);
         im->ovl[i].sec_plus_1 != 0;
         i = (i + 1) & (im->ovl_size - 1))
        if (im->ovl[i].sec_plus_1 == sec + 1)
            return &im->ovl[i];

    if (!create)
        return NULL;
    im->ovl[i].sec_plus_1 = sec + 1;
    im->ovl[i].rec = NO_REC;
    im->ovl_used++;
    return &im->ovl[i];
}

/* Seek and transfer rather than pread() and pwrite(), which the Cygwin
 * runtime refuses on a file inherited across the re-execution. Only one
 * thread at a time uses the store. */
static bool store_read(void *buf, size_t size, off_t offs)
{
    return (lseek(store_fd, offs, SEEK_SET) == offs)
        && (read(store_fd, buf, size) == (ssize_t)size);
}

static bool store_write(const void *buf, size_t size, off_t offs)
{
    return (lseek(store_fd, offs, SEEK_SET) == offs)
        && (write(store_fd, buf, size) == (ssize_t)size);
}

static void store_create(void)
{
    const char *tmp = getenv("TMPDIR");
    char path[512];

    snprintf(path, sizeof(path), "%s/ffemu-XXXXXX", tmp ? tmp : "/tmp");
    store_fd = mkstemp(path);
    if (store_fd < 0)
        cpu_quit(3, "ffemu: cannot create a temporary file");
    unlink(path);
}

/* Takes over the records in the store. */
static void store_load(struct image *im)
{
    struct stat st;
    unsigned int i, nr = 0;
    uint8_t hdr[4];

    if (fstat(store_fd, &st) == 0)
        nr = st.st_size / REC_SIZE;

    for (i = 0; i < nr; i++) {
        uint32_t sec;
        if (!store_read(hdr, 4, (off_t)i * REC_SIZE))
            break;
        sec = get32(hdr);
        if (sec >= im->total_secs)
            break;
        ovl_find(im, sec, true)->rec = i;
    }
    im->nr_recs = i;
}

static void read_file(struct image *im, struct node *n, uint64_t offs,
                      uint8_t *buf)
{
    ssize_t done = 0;

    if (im->fd_node != n) {
        if (im->fd_node != NULL)
            close(im->fd);
        im->fd = open(n->path, O_RDONLY | O_BINARY);
        im->fd_node = (im->fd >= 0) ? n : NULL;
    }

    if (im->fd_node != NULL)
        done = pread(im->fd, buf, SEC, offs);
    if (done < 0)
        done = 0;
    /* The host file may have shrunk, or gone, since the volume was built. */
    memset(buf + done, 0, SEC - done);
}

static void read_sector(struct image *im, uint32_t sec, uint8_t *buf)
{
    struct overlay *o = ovl_find(im, sec, false);

    if (o != NULL) {
        if (!store_read(buf, SEC, (off_t)o->rec * REC_SIZE + 4))
            memset(buf, 0, SEC);
    } else if (sec < RSVD_SECS) {
        memcpy(buf, im->rsvd + sec * SEC, SEC);
    } else if (sec < im->data_start) {
        uint32_t i, first = ((sec - RSVD_SECS) % im->fat_secs) * (SEC/4);
        for (i = 0; i < SEC/4; i++)
            put32(buf + 4*i, (first + i < im->nr_clus + 2)
                  ? im->fat[first + i] : 0);
    } else {
        uint32_t offs = sec - im->data_start;
        uint32_t clus = 2 + offs / SEC_PER_CLUS;
        unsigned int lo = 0, hi = im->nr_ext;
        struct extent *e = NULL;
        while (lo < hi) {
            unsigned int mid = (lo + hi) / 2;
            if (clus < im->ext[mid].clus) {
                hi = mid;
            } else if (clus >= im->ext[mid].clus + im->ext[mid].nr_clus) {
                lo = mid + 1;
            } else {
                e = &im->ext[mid];
                break;
            }
        }
        if (e == NULL) {
            memset(buf, 0, SEC);
        } else {
            uint64_t pos = (uint64_t)(clus - e->clus) * CLUS
                + (offs % SEC_PER_CLUS) * SEC;
            if (e->node->is_dir)
                memcpy(buf, e->node->data + pos, SEC);
            else
                read_file(im, e->node, pos, buf);
        }
    }
}

/* Sectors of the drive, with the firmware's writes over them; a run of
 * them that it has not written is read from an image in one go. */
static bool get_sectors(struct image *im, uint8_t *p, uint32_t sec,
                        unsigned int count)
{
    bool ok = true;

    pthread_mutex_lock(&lock);
    while (ok && (count != 0)) {
        unsigned int run = 1;
        if ((im->dev_fd < 0) || (ovl_find(im, sec, false) != NULL)) {
            read_sector(im, sec, p);
        } else {
            while ((run < count) && (ovl_find(im, sec + run, false) == NULL))
                run++;
            ok = dev_read(im, p, sec, run);
        }
        p += run * SEC;
        sec += run;
        count -= run;
    }
    pthread_mutex_unlock(&lock);
    return ok;
}

int emu_usb_inserted(void)
{
    return cur != NULL;
}

int emu_usb_read(void *buf, uint32_t sector, unsigned int count)
{
    struct image *im = cur;

    if ((im == NULL) || (sector >= im->total_secs)
        || (count > im->total_secs - sector))
        return -1;

    im->nr_reads++;
    return get_sectors(im, buf, sector, count) ? 0 : -1;
}

int emu_usb_write(const void *buf, uint32_t sector, unsigned int count)
{
    struct image *im = cur;
    const uint8_t *p = buf;
    int rc = 0;

    if ((im == NULL) || (sector >= im->total_secs)
        || (count > im->total_secs - sector))
        return -1;

    pthread_mutex_lock(&lock);
    for (; (rc == 0) && (count != 0); count--, p += SEC) {
        struct overlay *o = ovl_find(im, sector, true);
        uint8_t rec[REC_SIZE];
        if (o->rec == NO_REC)
            o->rec = im->nr_recs++;
        put32(rec, sector++);
        memcpy(rec + 4, p, SEC);
        if (!store_write(rec, REC_SIZE, (off_t)o->rec * REC_SIZE))
            rc = -1;
    }
    pthread_mutex_unlock(&lock);
    im->nr_writes++;
    return rc;
}

/*
 * Insertion and removal, from the user interface thread.
 */

bool usb_insert(const char *path, bool keep_writes)
{
    struct image *im;
    struct stat st;

    usb_remove();
    last_info.error[0] = '\0';
    if (stat(path, &st) != 0) {
        snprintf(last_info.error, sizeof(last_info.error), "%s: %s", path,
                 strerror(errno));
        return false;
    }
    im = S_ISDIR(st.st_mode) ? build_image(path, &st)
        : open_image(path, &st);
    if (im == NULL)
        return false;

    if (store_fd < 0)
        store_create();
    if (keep_writes && (store_layout == im->layout)) {
        store_load(im);
        im->info.nr_kept = im->nr_recs;
    } else {
        im->info.writes_lost = keep_writes && (fstat(store_fd, &st) == 0)
            && (st.st_size != 0);
        if (ftruncate(store_fd, 0) != 0)
            cpu_quit(3, "ffemu: cannot empty the temporary file");
    }
    store_layout = im->layout;

    cur = im;
    return true;
}

void usb_get_store(int *fd, uint32_t *layout)
{
    *fd = store_fd;
    *layout = store_layout;
}

void usb_set_store(int fd, uint32_t layout)
{
    struct stat st;

    if ((fd >= 0) && (fstat(fd, &st) == 0) && S_ISREG(st.st_mode)) {
        store_fd = fd;
        store_layout = layout;
    }
}

void usb_remove(void)
{
    struct image *im = cur;

    if (im == NULL)
        return;
    cur = NULL;
    free_image(grave);
    grave = im;
    memset(&last_info, 0, sizeof(last_info));
}

void usb_get_info(struct usb_info *info)
{
    struct image *im = cur;

    if (im == NULL) {
        *info = last_info;
        info->inserted = false;
        return;
    }
    *info = im->info;
    info->nr_reads = im->nr_reads;
    info->nr_writes = im->nr_writes;
}

const char *usb_ff_cfg_text(void)
{
    struct image *im = cur;

    return (im != NULL) ? im->ff_cfg_text : NULL;
}

bool usb_save(const char *path, char *err, size_t size)
{
    struct image *im = cur;
    uint8_t *buf;
    uint32_t sec;
    bool ok = true;
    int fd;

    if (im == NULL) {
        snprintf(err, size, "no USB drive");
        return false;
    }
    fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_BINARY, 0644);
    if (fd < 0) {
        snprintf(err, size, "%s: %s", path, strerror(errno));
        return false;
    }
    buf = xalloc(128 * SEC);
    for (sec = 0; ok && (sec < im->total_secs); sec += 128) {
        unsigned int n = (im->total_secs - sec < 128)
            ? im->total_secs - sec : 128;
        if (!get_sectors(im, buf, sec, n)) {
            snprintf(err, size, "cannot read sector %u", sec);
            ok = false;
        } else if (write(fd, buf, n * SEC) != (ssize_t)(n * SEC)) {
            snprintf(err, size, "%s: %s", path, strerror(errno));
            ok = false;
        }
    }
    free(buf);
    if ((close(fd) != 0) && ok) {
        snprintf(err, size, "%s: %s", path, strerror(errno));
        ok = false;
    }
    return ok;
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

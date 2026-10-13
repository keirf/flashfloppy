/*
 * harness.c
 *
 * The firmware's environment for the unit tests: a USB drive in RAM holding
 * a FAT16 volume, the console kept for the tests, and an image mounted and
 * driven as the floppy code does it.
 *
 * Written & released by Mike Shevchenko <mike.shevchenko.kz@gmail.com>
 *
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

#include "harness.h"

const struct ff_cfg dfl_ff_cfg = {
    .version = FFCFG_VERSION,
    .size = sizeof(struct ff_cfg),
#define x(n,o,v) .o = v,
#include "ff_cfg_defaults.h"
#undef x
};

struct ff_cfg ff_cfg;
uint8_t board_id = BRDREV_Gotek_standard;

/*
 * Console: kept for the runner, which prints it with a test's failures.
 */

static char log_buf[32*1024];
static unsigned int log_len;

const char *ft_fw_log(void)
{
    return log_buf;
}

void ft_fw_log_clear(void)
{
    log_len = 0;
    log_buf[0] = '\0';
}

int vprintk(const char *format, va_list ap)
{
    char line[256], *p;
    int n = vsnprintf(line, sizeof(line), format, ap);

    for (p = line; *p != '\0'; p++) {
        if (log_len + 6 >= sizeof(log_buf))
            break;
        if ((log_len == 0) || (log_buf[log_len-1] == '\n')) {
            memcpy(&log_buf[log_len], "    ", 4);
            log_len += 4;
        }
        log_buf[log_len++] = *p;
    }
    log_buf[log_len] = '\0';
    return n;
}

int printk(const char *format, ...)
{
    va_list ap;
    int n;

    va_start(ap, format);
    n = vprintk(format, ap);
    va_end(ap);
    return n;
}

bool_t log_has(const char *s)
{
    unsigned int i, n = strlen(s);

    for (i = 0; i + n <= log_len; i++)
        if (!memcmp(&log_buf[i], s, n))
            return TRUE;
    return FALSE;
}

/*
 * The drive: 16 MB of RAM holding a FAT16 volume with no partition table,
 * as a USB drive may have it. Files are added to the root folder, each in
 * contiguous clusters.
 */

#define SECSZ 512
#define DRIVE_SECS 32768
#define CLUS_SECS 4
#define FAT_SECS 32
#define ROOT_ENTS 512
#define FAT_START 1
#define ROOT_START (FAT_START + 2*FAT_SECS)
#define DATA_START (ROOT_START + ROOT_ENTS*32/SECSZ)
#define NR_CLUS ((DRIVE_SECS - DATA_START) / CLUS_SECS)

static uint8_t drive[DRIVE_SECS * SECSZ] aligned(4);
static unsigned int next_clus, nr_files;
static FATFS fatfs;
static bool_t mounted;

static void put16(uint8_t *p, unsigned int v)
{
    p[0] = v;
    p[1] = v >> 8;
}

static void put32(uint8_t *p, uint32_t v)
{
    put16(p, v);
    put16(p + 2, v >> 16);
}

static void put_fat(unsigned int clus, unsigned int v)
{
    put16(&drive[FAT_START*SECSZ + clus*2], v);
    put16(&drive[(FAT_START+FAT_SECS)*SECSZ + clus*2], v);
}

void drive_reset(void)
{
    uint8_t *b = drive;

    if (mounted)
        f_mount(NULL, "", 0);
    mounted = FALSE;
    volume_cache_destroy();

    memset(drive, 0, DATA_START * SECSZ);
    memcpy(b, "\xeb\x3c\x90" "FFTEST  ", 11);
    put16(b + 11, SECSZ);
    b[13] = CLUS_SECS;
    put16(b + 14, FAT_START);
    b[16] = 2;
    put16(b + 17, ROOT_ENTS);
    put16(b + 19, DRIVE_SECS);
    b[21] = 0xf8;
    put16(b + 22, FAT_SECS);
    put16(b + 24, 32);
    put16(b + 26, 2);
    b[36] = 0x80;
    b[38] = 0x29;
    put32(b + 39, 0x12345678);
    memcpy(b + 43, "NO NAME    FAT16   ", 19);
    put16(b + 510, 0xaa55);
    put_fat(0, 0xfff8);
    put_fat(1, 0xffff);
    next_clus = 2;
    nr_files = 0;

    ff_cfg = dfl_ff_cfg;
}

#define CLUS_SZ (CLUS_SECS*SECSZ)

/* Adds a file in clusters from next_clus on: in order, or scattered over
 * every other cluster of twice the span from the last to the first, so
 * that no cluster is followed by its neighbor. */
static unsigned int add_file(const char *name, const void *data,
                             uint32_t len, bool_t scatter)
{
    uint8_t *ent = &drive[ROOT_START*SECSZ + nr_files*32];
    const uint8_t *p = data;
    unsigned int i, j, first = next_clus;
    unsigned int nr = (len + CLUS_SZ - 1) / CLUS_SZ;
    unsigned int span = scatter ? 2*nr : nr;
    unsigned int clus[2];

    if ((nr_files == ROOT_ENTS) || (next_clus + span > NR_CLUS + 2))
        ft_abort(__FILE__, __LINE__, "drive full adding %s", name);

    /* The 8.3 name, in capitals and padded with blanks. */
    memset(ent, ' ', 11);
    for (i = j = 0; (name[i] != '\0') && (name[i] != '.') && (j < 8); i++)
        ent[j++] = toupper(name[i]);
    while ((name[i] != '\0') && (name[i] != '.'))
        i++;
    for (j = 8; (name[i] != '\0') && (name[i+1] != '\0') && (j < 11); i++)
        ent[j++] = toupper(name[i+1]);
    ent[11] = AM_ARC;
    put16(ent + 26, len ? (scatter ? first + 2*(nr-1) : first) : 0);
    put32(ent + 28, len);

    for (i = 0; i < nr; i++) {
        for (j = 0; j < 2; j++)
            clus[j] = scatter ? first + 2*(nr-1-(i+j)) : first + i + j;
        put_fat(clus[0], (i + 1 < nr) ? clus[1] : 0xffff);
        memcpy(&drive[(DATA_START + (clus[0]-2)*CLUS_SECS) * SECSZ],
               p + i*CLUS_SZ, min_t(uint32_t, CLUS_SZ, len - i*CLUS_SZ));
    }
    next_clus += span;
    nr_files++;
    return nr;
}

void drive_add(const char *name, const void *data, uint32_t len)
{
    add_file(name, data, len, FALSE);
}

unsigned int drive_add_fragmented(const char *name, const void *data,
                                  uint32_t len)
{
    return add_file(name, data, len, TRUE);
}

static void drive_mount(void)
{
    FRESULT fr;

    if (mounted)
        return;
    fr = f_mount(&fatfs, "", 1);
    if (fr != FR_OK)
        ft_abort(__FILE__, __LINE__, "f_mount() gave %d", fr);
    mounted = TRUE;
}

uint32_t drive_get(const char *name, void *buf, uint32_t max)
{
    FIL f;
    UINT nr = 0;
    FRESULT fr;

    drive_mount();
    if ((fr = f_open(&f, name, FA_READ)) != FR_OK)
        ft_abort(__FILE__, __LINE__, "f_open(%s) gave %d", name, fr);
    if ((fr = f_read(&f, buf, max, &nr)) != FR_OK)
        ft_abort(__FILE__, __LINE__, "f_read(%s) gave %d", name, fr);
    f_close(&f);
    return nr;
}

static DSTATUS drive_initialize(BYTE pdrv)
{
    return 0;
}

static DSTATUS drive_status(BYTE pdrv)
{
    return 0;
}

static DRESULT drive_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count)
{
    if (pdrv || (sector + count > DRIVE_SECS))
        return RES_PARERR;
    memcpy(buff, &drive[sector * SECSZ], count * SECSZ);
    return RES_OK;
}

static DRESULT drive_write(
    BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count)
{
    if (pdrv || (sector + count > DRIVE_SECS))
        return RES_PARERR;
    memcpy(&drive[sector * SECSZ], buff, count * SECSZ);
    return RES_OK;
}

static DRESULT drive_ioctl(BYTE pdrv, BYTE ctrl, void *buff)
{
    return (ctrl == CTRL_SYNC) ? RES_OK : RES_PARERR;
}

static bool_t drive_connected(void)
{
    return TRUE;
}

static bool_t drive_readonly(void)
{
    return FALSE;
}

struct volume_ops usb_ops = {
    .initialize = drive_initialize,
    .status = drive_status,
    .read = drive_read,
    .write = drive_write,
    .ioctl = drive_ioctl,
    .connected = drive_connected,
    .readonly = drive_readonly
};

/* No SD card on the board the tests model. */
struct volume_ops sd_ops;

bool_t usbh_msc_inserted(void)
{
    return TRUE;
}

/* From src/main.c, which the tests do not link. */
void fatfs_from_slot(FIL *file, const struct slot *slot, BYTE mode)
{
    memset(file, 0, sizeof(*file));
    file->obj.fs = &fatfs;
    file->obj.id = fatfs.id;
    file->obj.attr = slot->attributes;
    file->obj.sclust = slot->firstCluster;
    file->obj.objsize = slot->size;
    file->flag = mode;
    file->dir_sect = slot->dir_sect;
    file->dir_ptr = slot->dir_ptr;
}

/* No IMG.CFG on the test drive. */
bool_t get_img_cfg(struct slot *slot)
{
    return FALSE;
}

int get_next_opt(struct opts *opts)
{
    return OPT_eof;
}

/*
 * The rest of the firmware that the image code reaches: no display, no
 * direct access and no slot list. The board is in floppy_env.c.
 */

unsigned int sysclk_mhz = 144;
const char fw_ver[] = "test";
uint8_t display_type;

void lcd_write(int col, int row, int min, const char *str)
{
}

void led_7seg_write_string(const char *p)
{
}

int led_7seg_nr_digits(void)
{
    return 0;
}

bool_t lba_within_fat_volume(uint32_t lba)
{
    return FALSE;
}

uint16_t get_slot_nr(void)
{
    return 0;
}

bool_t set_slot_nr(uint16_t slot_nr)
{
    return FALSE;
}

void set_slot_name(const char *name)
{
}

/*
 * The image, with buffers laid out as the floppy code lays them out on an
 * STM32F105: the read ring is the second half of the write ring.
 */

static struct image image;
struct image *im = &image;
static struct slot slot;
static uint8_t ring[32*1024] aligned(32);
static uint8_t staging[32*1024] aligned(32);

/* A fast-seek table of the size the floppy code gives it. */
static DWORD cltbl[(511 + 1) * 2];

static int mount_fn(void *arg)
{
    image_open(im, &slot, arg);
    return 0;
}

static FRESULT mount(const char *name, bool_t fast_seek)
{
    const char *dot = strrchr(name, '.');
    unsigned int i;
    FIL f;
    FRESULT fr;

    drive_mount();
    if ((fr = f_open(&f, name, FA_READ)) != FR_OK)
        ft_abort(__FILE__, __LINE__, "f_open(%s) gave %d", name, fr);
    memset(&slot, 0, sizeof(slot));
    slot.attributes = f.obj.attr;
    slot.firstCluster = f.obj.sclust;
    slot.size = f.obj.objsize;
    slot.dir_sect = f.dir_sect;
    slot.dir_ptr = f.dir_ptr;
    f_close(&f);
    snprintf(slot.name, sizeof(slot.name), "%s", name);
    if (dot != NULL) {
        slot.name[dot - name] = '\0';
        for (i = 0; (dot[i+1] != '\0') && (i + 1 < sizeof(slot.type)); i++)
            slot.type[i] = tolower(dot[i+1]);
    }

    memset(im, 0, sizeof(*im));
    im->bufs.write_bc.p = ring;
    im->bufs.write_bc.len = sizeof(ring);
    im->bufs.read_bc.p = ring + sizeof(ring)/2;
    im->bufs.read_bc.len = sizeof(ring)/2;
    im->bufs.write_data.p = staging;
    im->bufs.write_data.len = sizeof(staging);
    im->bufs.read_data = im->bufs.write_data;

    if (!fast_seek)
        return F_call_cancellable(mount_fn, NULL);

    cltbl[0] = ARRAY_SIZE(cltbl);
    fatfs_from_slot(&im->fp, &slot, FA_READ);
    im->fp.cltbl = cltbl;
    if ((fr = f_lseek(&im->fp, CREATE_LINKMAP)) != FR_OK)
        ft_abort(__FILE__, __LINE__, "fast-seek table of %s: %d", name, fr);
    return F_call_cancellable(mount_fn, cltbl);
}

FRESULT image_mount(const char *name)
{
    return mount(name, FALSE);
}

FRESULT image_mount_fast_seek(const char *name)
{
    return mount(name, TRUE);
}

struct slot *image_slot(void)
{
    return &slot;
}

unsigned int bit_get(const uint8_t *p, uint32_t i)
{
    return (p[i/8] >> (7 - (i & 7))) & 1;
}

static void bit_put(uint8_t *p, uint32_t i, unsigned int bit)
{
    uint8_t mask = 0x80 >> (i & 7);
    p[i/8] = bit ? (p[i/8] | mask) : (p[i/8] & ~mask);
}

void bits_rotate(uint8_t *dst, const uint8_t *src, uint32_t nr_bits,
                 uint32_t start)
{
    uint32_t i;
    for (i = 0; i < nr_bits; i++)
        bit_put(dst, i, bit_get(src, (start + i) % nr_bits));
}

struct stream_arg {
    unsigned int track;
    uint32_t start; /* the bit cell to start at, then the one started at */
    uint8_t *out;
    uint32_t nr_bits; /* 0: one revolution, then the number streamed */
    uint32_t max_bits;
};

static int stream_fn(void *arg)
{
    struct stream_arg *a = arg;
    struct image_buf *bc = &im->bufs.read_bc;
    const uint8_t *bc_b = bc->p;
    uint32_t start_ticks, c, n = 0;
    unsigned int idle = 0;

    /* The floppy code asks for a position in ticks past the index. */
    start_ticks = ((uint64_t)a->start * im->ticks_per_cell + 15) / 16;
    image_setup_track(im, a->track, &start_ticks);
    a->start = im->cur_bc;
    if (a->nr_bits == 0)
        a->nr_bits = im->tracklen_bc;
    if (a->nr_bits > a->max_bits)
        ft_abort(__FILE__, __LINE__, "track %u: %u bit cells, more than %u",
                 a->track, a->nr_bits, a->max_bits);
    for (;;) {
        for (c = bc->cons; (c != bc->prod) && (n < a->nr_bits); c++, n++)
            bit_put(a->out, n, bit_get(bc_b, c & (bc->len*8 - 1)));
        bc->cons = c;
        if (n == a->nr_bits)
            break;
        if (image_read_track(im))
            idle = 0;
        else if (++idle == 4)
            ft_abort(__FILE__, __LINE__, "track %u stopped after %u bits",
                     a->track, n);
    }
    return 0;
}

static void stream(struct stream_arg *a)
{
    FRESULT fr = F_call_cancellable(stream_fn, a);
    if (fr != FR_OK)
        ft_abort(__FILE__, __LINE__, "reading track %u gave %d",
                 a->track, fr);
}

unsigned int image_stream(unsigned int track, void *out, unsigned int max)
{
    struct stream_arg a = { track, 0, out, 0, max * 8 };
    stream(&a);
    return a.nr_bits / 8;
}

uint32_t image_stream_from(unsigned int track, uint32_t start, void *out,
                           uint32_t nr_bits)
{
    struct stream_arg a = { track, start, out, nr_bits, nr_bits };
    stream(&a);
    return a.start;
}

struct flux_arg {
    unsigned int track;
    uint16_t *out;
    unsigned int nr;
};

static int flux_fn(void *arg)
{
    struct flux_arg *a = arg;
    uint32_t start = 0;
    unsigned int n, got = 0, idle = 0;

    image_setup_track(im, a->track, &start);
    while (got < a->nr) {
        n = image_rdata_flux(im, &a->out[got], a->nr - got);
        got += n;
        if (got == a->nr)
            break;
        if (image_read_track(im) || n)
            idle = 0;
        else if (++idle == 4)
            ft_abort(__FILE__, __LINE__, "track %u stopped after %u flux",
                     a->track, got);
    }
    return 0;
}

void image_flux(unsigned int track, uint16_t *out, unsigned int nr)
{
    struct flux_arg a = { track, out, nr };
    FRESULT fr = F_call_cancellable(flux_fn, &a);
    if (fr != FR_OK)
        ft_abort(__FILE__, __LINE__, "flux of track %u gave %d", track, fr);
}

unsigned int flux_of_cells(uint16_t *out, unsigned int max,
                           const uint8_t *cells, uint32_t nr_cells,
                           bool_t lsb_first, uint32_t ticks_per_cell)
{
    uint64_t last = 0, now;
    unsigned int nr = 0;
    uint32_t i, bit;

    for (i = 0; (i < nr_cells) && (nr < max); i++) {
        bit = lsb_first ? (cells[i/8] >> (i & 7)) & 1 : bit_get(cells, i);
        if (!bit)
            continue;
        now = ((uint64_t)(i + 1) * ticks_per_cell) / 16;
        out[nr++] = now - last - 1;
        last = now;
    }
    return nr;
}

struct write_arg {
    unsigned int track;
    uint32_t start;
    const uint8_t *bits;
    uint32_t nr_bits;
};

static int write_fn(void *arg)
{
    struct write_arg *a = arg;
    struct image_buf *wr = &im->bufs.write_bc;
    struct write *write = get_write(im, 0);
    unsigned int i, idle = 0;

    if ((a->nr_bits + 7) / 8 > wr->len)
        ft_abort(__FILE__, __LINE__, "write of %u bits too long", a->nr_bits);
    for (i = 0; i < (a->nr_bits + 7) / 8; i++)
        ((uint8_t *)wr->p)[i] = a->bits[i];
    wr->cons = 0;
    wr->prod = a->nr_bits;
    write->start = a->start * im->write_bc_ticks;
    write->bc_end = a->nr_bits;
    write->track = a->track;
    im->wr_cons = 0;
    im->wr_bc = im->wr_prod = 1;

    image_setup_track(im, a->track, NULL);
    while (!image_write_track(im))
        if (++idle == 10000)
            ft_abort(__FILE__, __LINE__, "write to track %u never ended",
                     a->track);
    return 0;
}

void image_write(unsigned int track, uint32_t start, const void *bits,
                 uint32_t nr_bits)
{
    struct write_arg a = { track, start, bits, nr_bits };
    FRESULT fr = F_call_cancellable(write_fn, &a);
    if (fr != FR_OK)
        ft_abort(__FILE__, __LINE__, "writing track %u gave %d", track, fr);
}

static int sync_fn(void *arg)
{
    F_sync(&im->fp);
    return 0;
}

void image_sync(void)
{
    FRESULT fr = F_call_cancellable(sync_fn, NULL);
    if (fr != FR_OK)
        ft_abort(__FILE__, __LINE__, "syncing the image gave %d", fr);
}

uint32_t crc32(const void *buf, uint32_t len)
{
    const uint8_t *p = buf;
    uint32_t crc = ~0u;
    unsigned int i;

    while (len--) {
        crc ^= *p++;
        for (i = 0; i < 8; i++)
            crc = (crc >> 1) ^ (0xedb88320 & -(crc & 1));
    }
    return ~crc;
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

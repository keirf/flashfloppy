/*
 * volume.c
 * 
 * Volume abstraction for low-level storage drivers.
 * 
 * Written & released by Keir Fraser <keir.xen@gmail.com>
 * 
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

extern struct volume_ops sd_ops;
extern struct volume_ops usb_ops;

static struct volume_ops *vol_ops = &usb_ops;

static struct cache *cache;
static bool_t interrupt;
static bool_t inprogress;
static void *metadata_addr;
#define SECSZ 512

#if !defined(BOOTLOADER)
void volume_cache_init(void *start, void *end)
{
    unsigned int entry_cnt;
    volume_cache_destroy();
    cache = cache_init(start, end, SECSZ, &entry_cnt);
    interrupt = FALSE;
    inprogress = FALSE;
}

void volume_cache_destroy(void)
{
    cache = NULL;
    metadata_addr = NULL;
}

void volume_cache_metadata_only(FIL *fp)
{
    /* All metadata is accessed via the per-filesystem "sector window". */
    metadata_addr = fp->obj.fs->win;
}
#endif

DSTATUS disk_initialize(BYTE pdrv)
{
    /* Default to USB if inserted. */
    vol_ops = &usb_ops;
    if (!(usb_ops.initialize(pdrv) & STA_NOINIT))
        goto out;

    /* Try SD if the build and the board support it, and no USB drive is 
     * inserted. */
    if ((board_id == BRDREV_Gotek_sd_card)
        && !usbh_msc_inserted()
        && !(sd_ops.initialize(pdrv) & STA_NOINIT)) {
        vol_ops = &sd_ops;
    }

out:
    return disk_status(pdrv);
}

static inline void start_op(void)
{
    ASSERT(!inprogress);
    inprogress = TRUE;
}

static inline void end_op(void)
{
    ASSERT(inprogress);
    inprogress = FALSE;
    if (interrupt) {
        thread_yield();
        interrupt = FALSE;
    }
}

DSTATUS disk_status(BYTE pdrv)
{
    DSTATUS status;
    start_op();
    status = vol_ops->status(pdrv);
    end_op();
    return status;
}

DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count)
{
    DRESULT res;
    const void *p;
    struct cache *c;

    if (((c = cache) == NULL)
        || (metadata_addr && (buff != metadata_addr))) {
        start_op();
        res = vol_ops->read(pdrv, buff, sector, count);
        end_op();
        return res;
    }

    while (count) {
        if ((p = cache_lookup(c, sector)) == NULL)
            goto read_tail;
        memcpy(buff, p, SECSZ);
        sector++;
        count--;
        buff += SECSZ;
    }
    return RES_OK;

read_tail:
    start_op();
    res = vol_ops->read(pdrv, buff, sector, count);
    if (res == RES_OK)
        cache_update_N(c, sector, buff, count);
    end_op();
    return res;
}

DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count)
{
    DRESULT res;
    struct cache *c;
    start_op();
    res = vol_ops->write(pdrv, buff, sector, count);
    if ((res == RES_OK) && ((c = cache) != NULL)
        && (!metadata_addr || (buff == metadata_addr)))
        cache_update_N(c, sector, buff, count);
    end_op();
    return res;
}

DRESULT disk_ioctl(BYTE pdrv, BYTE ctrl, void *buff)
{
    DRESULT res;
    start_op();
    res = vol_ops->ioctl(pdrv, ctrl, buff);
    end_op();
    return res;
}

bool_t volume_connected(void)
{
    /* Force switch to USB drive if inserted. */
    if ((vol_ops == &sd_ops) && usbh_msc_inserted())
        return FALSE;
    return vol_ops->connected();
}

bool_t volume_readonly(void)
{
    bool_t ret;
    start_op();
    ret = vol_ops->readonly();
    end_op();
    return ret;
}

bool_t volume_interrupt(void)
{
    if (inprogress)
        interrupt = TRUE;
    return inprogress;
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

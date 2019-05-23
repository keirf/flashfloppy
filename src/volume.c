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
static void *metadata_addr;
#define SECSZ 512

#if !defined(BOOTLOADER)
void volume_cache_init(void *start, void *end)
{
    volume_cache_destroy();
    cache = cache_init(start, end, SECSZ);
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

DSTATUS disk_status(BYTE pdrv)
{
    return vol_ops->status(pdrv);
}

DRESULT disk_read(BYTE pdrv, BYTE *buff, DWORD sector, UINT count)
{
    DRESULT res;
    const void *p;
    struct cache *c;

    if (((c = cache) == NULL)
        || (metadata_addr && (buff != metadata_addr)))
        return vol_ops->read(pdrv, buff, sector, count);

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
    res = vol_ops->read(pdrv, buff, sector, count);
    if (res == RES_OK)
        cache_update_N(c, sector, buff, count);
    return res;
}

DRESULT disk_write(BYTE pdrv, const BYTE *buff, DWORD sector, UINT count)
{
    DRESULT res = vol_ops->write(pdrv, buff, sector, count);
    struct cache *c;
    if ((res == RES_OK) && ((c = cache) != NULL)
        && (!metadata_addr || (buff == metadata_addr)))
        cache_update_N(c, sector, buff, count);
    return res;
}

DRESULT disk_ioctl(BYTE pdrv, BYTE ctrl, void *buff)
{
    return vol_ops->ioctl(pdrv, ctrl, buff);
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
    return vol_ops->readonly();
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

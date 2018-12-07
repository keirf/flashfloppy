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

#if !defined(BOOTLOADER) && !defined(RELOADER)
#define USE_SD 1
extern struct volume_ops sd_ops;
#endif

extern struct volume_ops usb_ops;

static struct volume_ops *vol_ops = &usb_ops;

DSTATUS disk_initialize(BYTE pdrv)
{
    /* Default to USB if inserted. */
    vol_ops = &usb_ops;
    if (!(usb_ops.initialize(pdrv) & STA_NOINIT))
        goto out;

#ifdef USE_SD
    /* Try SD if the build and the board support it, and no USB drive is 
     * inserted. */
    if ((board_id == BRDREV_Gotek_sd_card)
        && !usbh_msc_inserted()
        && !(sd_ops.initialize(pdrv) & STA_NOINIT)) {
        vol_ops = &sd_ops;
    }
#endif

out:
    return disk_status(pdrv);
}

DSTATUS disk_status(BYTE pdrv)
{
    return vol_ops->status(pdrv);
}

DRESULT disk_read(BYTE pdrv, BYTE *buff, DWORD sector, UINT count)
{
    return vol_ops->read(pdrv, buff, sector, count);
}

DRESULT disk_write(BYTE pdrv, const BYTE *buff, DWORD sector, UINT count)
{
    return vol_ops->write(pdrv, buff, sector, count);
}

DRESULT disk_ioctl(BYTE pdrv, BYTE ctrl, void *buff)
{
    return vol_ops->ioctl(pdrv, ctrl, buff);
}

bool_t volume_connected(void)
{
#ifdef USE_SD
    /* Force switch to USB drive if inserted. */
    if ((vol_ops == &sd_ops) && usbh_msc_inserted())
        return FALSE;
#endif
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

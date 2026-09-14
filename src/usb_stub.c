/*
 * usb_stub.c
 *
 * Stub USB-host API for targets with no USB stack (RP2350: mass storage
 * is on an SD card instead). The USB volume never initialises, so
 * volume.c always selects the SD backend.
 *
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

void usbh_msc_init(void)
{
}

void usbh_msc_buffer_set(uint8_t *buf)
{
}

void usbh_msc_process(void)
{
}

bool_t usbh_msc_inserted(void)
{
    return FALSE;
}

static DSTATUS usb_disk_initialize(BYTE pdrv)
{
    return STA_NOINIT;
}

static DSTATUS usb_disk_status(BYTE pdrv)
{
    return STA_NOINIT;
}

static DRESULT usb_disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count)
{
    return RES_NOTRDY;
}

static DRESULT usb_disk_write(
    BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count)
{
    return RES_NOTRDY;
}

static DRESULT usb_disk_ioctl(BYTE pdrv, BYTE cmd, void *buff)
{
    return RES_NOTRDY;
}

static bool_t usb_connected(void)
{
    return FALSE;
}

static bool_t usb_readonly(void)
{
    return FALSE;
}

struct volume_ops usb_ops = {
    .initialize = usb_disk_initialize,
    .status = usb_disk_status,
    .read = usb_disk_read,
    .write = usb_disk_write,
    .ioctl = usb_disk_ioctl,
    .connected = usb_connected,
    .readonly = usb_readonly
};

/*
 * Local variables:
 * mode: C
 * c-file-style: "Linux"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */

/*
 * usb_disk.c
 * 
 * FatFS wrapper around USB MSC. Currently stubbed.
 * 
 * Written & released by Keir Fraser <keir.xen@gmail.com>
 * 
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

#include "../fatfs/diskio.h"

DSTATUS disk_initialize(BYTE pdrv)
{
    return RES_PARERR;
}

DSTATUS disk_status (BYTE pdrv)
{
    return RES_PARERR;
}

DRESULT disk_read(BYTE pdrv, BYTE *buff, DWORD sector, UINT count)
{
    return RES_PARERR;
}

DRESULT disk_write(BYTE pdrv, const BYTE *buff, DWORD sector, UINT count)
{
    return RES_PARERR;
}

DRESULT disk_ioctl(BYTE pdrv, BYTE ctrl, void *buff)
{
    return RES_PARERR;
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

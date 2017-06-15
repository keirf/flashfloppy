
#include "usb_conf.h"
#include "usbh_msc_core.h"
#include "../../fatfs/diskio.h"

static volatile DSTATUS Stat = STA_NOINIT;	/* Disk status */

extern USB_OTG_CORE_HANDLE          USB_OTG_Core;
extern USBH_HOST                     USB_Host;

DSTATUS disk_initialize(BYTE pdrv)
{
    if (HCD_IsDeviceConnected(&USB_OTG_Core))
        Stat &= ~STA_NOINIT;

    return Stat;
}

DSTATUS disk_status(BYTE pdrv)
{
    if (pdrv) return STA_NOINIT;		/* Supports only single drive */
    return Stat;
}

DRESULT disk_read(BYTE pdrv, BYTE *buff, DWORD sector, UINT count)
{
    BYTE status;

    if (pdrv || !count) return RES_PARERR;
    if (Stat & STA_NOINIT) return RES_NOTRDY;
    if (!HCD_IsDeviceConnected(&USB_OTG_Core))
        return RES_ERROR;

    do {
        status = USBH_MSC_Read10(&USB_OTG_Core, buff, sector, 512 * count);
        USBH_MSC_HandleBOTXfer(&USB_OTG_Core, &USB_Host);
        if (!HCD_IsDeviceConnected(&USB_OTG_Core))
            return RES_ERROR;
    } while (status == USBH_MSC_BUSY);

    return (status == USBH_MSC_OK) ? RES_OK : RES_ERROR;
}

DRESULT disk_write(BYTE pdrv, const BYTE *buff, DWORD sector, UINT count)
{
    BYTE status;

    if (pdrv || !count) return RES_PARERR;
    if (Stat & STA_NOINIT) return RES_NOTRDY;
    if (Stat & STA_PROTECT) return RES_WRPRT;
    if (!HCD_IsDeviceConnected(&USB_OTG_Core))
        return RES_ERROR;

    do {
        status = USBH_MSC_Write10(&USB_OTG_Core, (BYTE*)buff,
                                  sector, 512 * count);
        USBH_MSC_HandleBOTXfer(&USB_OTG_Core, &USB_Host);
        if (!HCD_IsDeviceConnected(&USB_OTG_Core))
            return RES_ERROR;
    } while (status == USBH_MSC_BUSY);

    return (status == USBH_MSC_OK) ? RES_OK : RES_ERROR;
}

DRESULT disk_ioctl(BYTE pdrv, BYTE ctrl, void *buff)
{
    DRESULT res = RES_OK;

    if (pdrv) return RES_PARERR;
    if (Stat & STA_NOINIT) return RES_NOTRDY;

    switch (ctrl) {
    case CTRL_SYNC :		/* Make sure that no pending write process */
        break;

    case GET_SECTOR_COUNT :	/* Get number of sectors on the disk (DWORD) */
        *(DWORD*)buff = (DWORD) USBH_MSC_Param.MSCapacity;
        break;

    case GET_SECTOR_SIZE :	/* Get R/W sector size (WORD) */
        *(WORD*)buff = 512;
        break;

    case GET_BLOCK_SIZE :	/* Get erase block size in unit of sector (DWORD) */
        *(DWORD*)buff = 512;
        break;

    default:
        res = RES_PARERR;
    }

    return res;
}

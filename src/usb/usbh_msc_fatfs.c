/*
 * usbh_msc_fatfs.c
 * 
 * Glue between USB Host MSC driver and FatFS.
 * 
 * Written & released by Keir Fraser <keir.xen@gmail.com>
 * 
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

#include "usbh_msc_core.h"

static DSTATUS dstatus = STA_NOINIT;
static bool_t msc_device_connected;

extern USB_OTG_CORE_HANDLE USB_OTG_Core;
USBH_HOST USB_Host;

static void USBH_USR_Init(void)
{
    printk("> %s\n", __FUNCTION__);
}

static void USBH_USR_DeInit(void)
{
    printk("> %s\n", __FUNCTION__);
    msc_device_connected = FALSE;
}

static void USBH_USR_DeviceAttached(void)
{
    printk("> %s\n", __FUNCTION__);
}

static void USBH_USR_ResetDevice(void)
{
    printk("> %s\n", __FUNCTION__);
}

static void USBH_USR_DeviceDisconnected (void)
{
    printk("> %s\n", __FUNCTION__);
    msc_device_connected = FALSE;
}

static void USBH_USR_OverCurrentDetected (void)
{
    printk("> %s\n", __FUNCTION__);
}

static void USBH_USR_DeviceSpeedDetected(uint8_t DeviceSpeed)
{
    printk("> %s\n", __FUNCTION__);
    printk("> Device speed: %s\n",
           (DeviceSpeed == HPRT0_PRTSPD_HIGH_SPEED) ? "High" :
           (DeviceSpeed == HPRT0_PRTSPD_FULL_SPEED) ? "Full" :
           (DeviceSpeed == HPRT0_PRTSPD_LOW_SPEED) ? "Low" : "???");
}

static void USBH_USR_DeviceDescAvailable(void *DeviceDesc)
{
    USBH_DevDesc_TypeDef *hs = DeviceDesc;
    printk("> %s\n", __FUNCTION__);
    printk(" VID : %04X\n", hs->idVendor);
    printk(" PID : %04X\n", hs->idProduct);
}

static void USBH_USR_DeviceAddressAssigned(void)
{
    printk("> %s\n", __FUNCTION__);
}

static void USBH_USR_ConfigurationDescAvailable(
    USBH_CfgDesc_TypeDef * cfgDesc,
    USBH_InterfaceDesc_TypeDef *itfDesc,
    USBH_EpDesc_TypeDef *epDesc)
{
    USBH_InterfaceDesc_TypeDef *id = itfDesc;

    printk("> %s\n", __FUNCTION__);
    printk("> Class connected: %02x (%s)\n",
           id->bInterfaceClass,
           (id->bInterfaceClass == 0x08) ? "MSC" :
           (id->bInterfaceClass == 0x03) ? "HID" : "???");
}

static void USBH_USR_ManufacturerString(void *ManufacturerString)
{
    printk(" Manufacturer : %s\n", (char *)ManufacturerString);
}

static void USBH_USR_ProductString(void *ProductString)
{
    printk(" Product : %s\n", (char *)ProductString);
}

static void USBH_USR_SerialNumString(void *SerialNumString)
{
    printk(" Serial Number : %s\n", (char *)SerialNumString);
}

static void USBH_USR_EnumerationDone(void)
{
    printk("> %s\n", __FUNCTION__);
}

static USBH_USR_Status USBH_USR_UserInput(void)
{
    printk("> %s\n", __FUNCTION__);
    return USBH_USR_RESP_OK;
}

static int USBH_USR_UserApplication(void)
{
    msc_device_connected = TRUE;
    /* 1 forces reset, 0 okay */
    return 0;
}

static void USBH_USR_DeviceNotSupported(void)
{
    printk("> %s\n", __FUNCTION__);
}

static void USBH_USR_UnrecoveredError (void)
{
    printk("> %s\n", __FUNCTION__);
    msc_device_connected = FALSE;
}

static USBH_Usr_cb_TypeDef USR_cb = {
    USBH_USR_Init,
    USBH_USR_DeInit,
    USBH_USR_DeviceAttached,
    USBH_USR_ResetDevice,
    USBH_USR_DeviceDisconnected,
    USBH_USR_OverCurrentDetected,
    USBH_USR_DeviceSpeedDetected,
    USBH_USR_DeviceDescAvailable,
    USBH_USR_DeviceAddressAssigned,
    USBH_USR_ConfigurationDescAvailable,
    USBH_USR_ManufacturerString,
    USBH_USR_ProductString,
    USBH_USR_SerialNumString,
    USBH_USR_EnumerationDone,
    USBH_USR_UserInput,
    USBH_USR_UserApplication,
    USBH_USR_DeviceNotSupported,
    USBH_USR_UnrecoveredError
};

void usbh_msc_init(void)
{
    USBH_Init(&USB_OTG_Core, 
              USB_OTG_FS_CORE_ID,
              &USB_Host,
              &USBH_MSC_cb, 
              &USR_cb);
}

void usbh_msc_buffer_set(uint8_t *buf)
{
    Cfg_Rx_Buffer = buf;
}

void usbh_msc_process(void)
{
    USBH_Process(&USB_OTG_Core, &USB_Host);
}

bool_t usbh_msc_inserted(void)
{
    return HCD_IsDeviceConnected(&USB_OTG_Core)
        || (USB_Host.gState != HOST_IDLE);
}

static bool_t usbh_msc_connected(void)
{
    return msc_device_connected && HCD_IsDeviceConnected(&USB_OTG_Core);
}

static bool_t usbh_msc_readonly(void)
{
    return usbh_msc_connected() && USBH_MSC_Param.MSWriteProtect;
}

/*
 * FatFS low-level driver callbacks.
 */

static DSTATUS usb_disk_initialize(BYTE pdrv)
{
    if (pdrv)
        return RES_PARERR;

    dstatus = (!usbh_msc_connected() ? STA_NOINIT
               : USBH_MSC_Param.MSWriteProtect ? STA_PROTECT
               : 0);

    return dstatus;
}

static DSTATUS usb_disk_status(BYTE pdrv)
{
    return pdrv ? STA_NOINIT : dstatus;
}

static DRESULT handle_usb_status(BYTE status)
{
    if (status != USBH_MSC_OK) {
        /* Kick the USBH state machine to reinitialise. */
        USB_Host.usr_cb->UnrecoveredError();
        USB_Host.gState = HOST_ERROR_STATE;
        /* Disallow further disk operations. */
        dstatus |= STA_NOINIT;
        return RES_ERROR;
    }

    return RES_OK;
}

static DRESULT usb_disk_read(BYTE pdrv, BYTE *buff, DWORD sector, UINT count)
{
    BYTE status;

    if (pdrv || !count)
        return RES_PARERR;
    if (dstatus & STA_NOINIT)
        return RES_NOTRDY;

    do {
        if (!HCD_IsDeviceConnected(&USB_OTG_Core))
            return handle_usb_status(USBH_MSC_FAIL);
        status = USBH_MSC_Read10(&USB_OTG_Core, buff, sector, 512 * count);
        USBH_MSC_HandleBOTXfer(&USB_OTG_Core, &USB_Host);
    } while (status == USBH_MSC_BUSY);

    return handle_usb_status(status);
}

static DRESULT usb_disk_write(
    BYTE pdrv, const BYTE *buff, DWORD sector, UINT count)
{
    BYTE status;

    if (pdrv || !count)
        return RES_PARERR;
    if (dstatus & STA_NOINIT)
        return RES_NOTRDY;
    if (dstatus & STA_PROTECT)
        return RES_WRPRT;

    do {
        if (!HCD_IsDeviceConnected(&USB_OTG_Core))
            return handle_usb_status(USBH_MSC_FAIL);
        status = USBH_MSC_Write10(
            &USB_OTG_Core, (BYTE *)buff, sector, 512 * count);
        USBH_MSC_HandleBOTXfer(&USB_OTG_Core, &USB_Host);
    } while (status == USBH_MSC_BUSY);

    return handle_usb_status(status);
}

static DRESULT usb_disk_ioctl(BYTE pdrv, BYTE ctrl, void *buff)
{
    DRESULT res = RES_ERROR;

    if (pdrv)
        return RES_PARERR;
    if (dstatus & STA_NOINIT)
        return RES_NOTRDY;

    switch (ctrl) {
    case CTRL_SYNC:
        res = RES_OK;
        break;
    default:
        res = RES_PARERR;
        break;
    }

    return res;
}

struct volume_ops usb_ops = {
    .initialize = usb_disk_initialize,
    .status = usb_disk_status,
    .read = usb_disk_read,
    .write = usb_disk_write,
    .ioctl = usb_disk_ioctl,
    .connected = usbh_msc_connected,
    .readonly = usbh_msc_readonly
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

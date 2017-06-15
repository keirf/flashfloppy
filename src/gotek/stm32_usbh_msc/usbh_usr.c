/**
******************************************************************************
* @file    usbh_usr.c
* @author  MCD Application Team
* @version V1.2.0
* @date    09-November-2015
* @brief   This file includes the usb host library user callbacks
******************************************************************************
* @attention
*
* <h2><center>&copy; COPYRIGHT 2015 STMicroelectronics</center></h2>
*
* Licensed under MCD-ST Liberty SW License Agreement V2, (the "License");
* You may not use this file except in compliance with the License.
* You may obtain a copy of the License at:
*
*        http://www.st.com/software_license_agreement_liberty_v2
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*
******************************************************************************
*/

#include "usbh_msc_core.h"

extern USB_OTG_CORE_HANDLE USB_OTG_Core;
USBH_HOST USB_Host;

/* State Machine for the USBH_USR_ApplicationState */
#define USH_USR_FS_INIT       0
#define USH_USR_FS_READLIST   1
#define USH_USR_FS_WRITEFILE  2
static uint8_t USBH_USR_ApplicationState = USH_USR_FS_INIT;

static FATFS fatfs;
static FIL file;
static char lfn[_MAX_LFN+1];

static uint8_t Explore_Disk(char *path , uint8_t recu_level)
{
    FRESULT res;
    FILINFO fno;
    DIR dir;
    char *fn;

    fno.lfname = lfn;
    fno.lfsize = sizeof(lfn);

    res = f_opendir(&dir, path);
    if (res == FR_OK) {
        while (HCD_IsDeviceConnected(&USB_OTG_Core)) {
            res = f_readdir(&dir, &fno);
            if (res != FR_OK || fno.fname[0] == 0)
                break;
            if (fno.fname[0] == '.')
                continue;

            fn = *fno.lfname ? fno.lfname : fno.fname;

            if(recu_level == 2)
                printk("   |");
            printk("   |__ %s\n", fn);

            if (((fno.fattrib & AM_MASK) == AM_DIR)&&(recu_level == 1))
                Explore_Disk(fn, 2);
        }
    }
    return res;
}

static void USBH_USR_Init(void)
{
    printk("> %s\n", __FUNCTION__);
}

static void USBH_USR_DeInit(void)
{
    printk("> %s\n", __FUNCTION__);
    USBH_USR_ApplicationState = USH_USR_FS_INIT;
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
    printk(" VID : %04Xh\n" , (uint32_t)(*hs).idVendor);
    printk(" PID : %04Xh\n" , (uint32_t)(*hs).idProduct);
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
    printk("Manufacturer : %s\n", (char *)ManufacturerString);
}

static void USBH_USR_ProductString(void *ProductString)
{
    printk("Product : %s\n", (char *)ProductString);
}

static void USBH_USR_SerialNumString(void *SerialNumString)
{
    printk( "Serial Number : %s\n", (char *)SerialNumString);
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
    switch (USBH_USR_ApplicationState)
    {
    case USH_USR_FS_INIT:
        /* Initialises the File System*/
        if ( f_mount(&fatfs, "", 0) != FR_OK )
        {
            /* efs initialisation fails*/
            printk("> Cannot initialize File System.\n");
            return(-1);
        }
        printk("> File System initialized.\n");
        printk("> Disk capacity : %u Bytes\n",
               USBH_MSC_Param.MSCapacity * USBH_MSC_Param.MSPageLength);

        if (USBH_MSC_Param.MSWriteProtect == DISK_WRITE_PROTECTED)
            printk("> The disk is write protected\n");

        USBH_USR_ApplicationState = USH_USR_FS_READLIST;
        break;

    case USH_USR_FS_READLIST:
        printk("> Exploring Flash...\n");
        Explore_Disk("0:/", 1);
        USBH_USR_ApplicationState = USH_USR_FS_WRITEFILE;
        break;

    case USH_USR_FS_WRITEFILE: {
        FRESULT res;
        const static uint8_t writeTextBuff[] =
            "STM32 Connectivity line Host Demo application using FAT_FS   ";
        unsigned int bytesWritten, bytesToWrite;
        /* Writes a text file, STM32.TXT in the disk*/
        printk("> Writing File to disk flash ...\n");
        if(USBH_MSC_Param.MSWriteProtect == DISK_WRITE_PROTECTED)
        {
            printk ( "> Disk flash is write protected \n");
            return 1; /* force reset */
        }

        /* Register work area for logical drives */
        f_mount(&fatfs, "", 0);

        if (f_open(&file, "0:STM32.TXT",FA_CREATE_ALWAYS | FA_WRITE) == FR_OK)
        {
            /* Write buffer to file */
            bytesToWrite = sizeof(writeTextBuff);
            res= f_write (&file, writeTextBuff, bytesToWrite, (void *)&bytesWritten);

            if((bytesWritten == 0) || (res != FR_OK)) /*EOF or Error*/
            {
                printk("> STM32.TXT CANNOT be writen.\n");
            }
            else
            {
                printk("> 'STM32.TXT' file created\n");
            }

            /*close file and filesystem*/
            f_close(&file);
            f_mount(NULL, "", 0);
        }
        else
        {
            printk ("> STM32.TXT could not be created\n");
        }

        return 1; /* force reset */
    }
    }
    return(0);
}

static void USBH_USR_DeviceNotSupported(void)
{
    printk("> %s\n", __FUNCTION__);
}

static void USBH_USR_UnrecoveredError (void)
{
    printk("> %s\n", __FUNCTION__);
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

void usbh_msc_process(void)
{
    USBH_Process(&USB_OTG_Core, &USB_Host);
}

/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/

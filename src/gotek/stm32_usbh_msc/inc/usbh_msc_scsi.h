/**
  ******************************************************************************
  * @file    usbh_msc_scsi.h
  * @author  MCD Application Team
  * @version V2.2.0
  * @date    09-November-2015
  * @brief   Header file for usbh_msc_scsi.c
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

#ifndef __USBH_MSC_SCSI_H__
#define __USBH_MSC_SCSI_H__

#include "usbh_stdreq.h"

typedef enum {
    USBH_MSC_OK = 0,
    USBH_MSC_FAIL = 1,
    USBH_MSC_PHASE_ERROR = 2,
    USBH_MSC_BUSY = 3
} USBH_MSC_Status_TypeDef;

typedef enum {
    CMD_UNINITIALIZED_STATE =0,
    CMD_SEND_STATE,
    CMD_WAIT_STATUS
} CMD_STATES_TypeDef;

typedef struct __MassStorageParameter
{
    uint32_t MSCapacity;
    uint32_t MSSenseKey;
    uint16_t MSPageLength;
    uint8_t MSBulkOutEp;
    uint8_t MSBulkInEp;
    uint8_t MSWriteProtect;
} MassStorageParameter_TypeDef;

extern MassStorageParameter_TypeDef USBH_MSC_Param;

uint8_t USBH_MSC_TestUnitReady(USB_OTG_CORE_HANDLE *pdev);
uint8_t USBH_MSC_ReadCapacity10(USB_OTG_CORE_HANDLE *pdev);
uint8_t USBH_MSC_ModeSense6(USB_OTG_CORE_HANDLE *pdev);
uint8_t USBH_MSC_RequestSense(USB_OTG_CORE_HANDLE *pdev);
uint8_t USBH_MSC_Write10(USB_OTG_CORE_HANDLE *pdev,
                         uint8_t *,
                         uint32_t ,
                         uint32_t );
uint8_t USBH_MSC_Read10(USB_OTG_CORE_HANDLE *pdev,
                        uint8_t *,
                        uint32_t ,
                        uint32_t );
void USBH_MSC_StateMachine(USB_OTG_CORE_HANDLE *pdev);

#endif  //__USBH_MSC_SCSI_H__

/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/

/**
  ******************************************************************************
  * @file    usbh_msc_core.h
  * @author  MCD Application Team
  * @version V2.2.0
  * @date    09-November-2015
  * @brief   This file contains all the prototypes for the usbh_msc_core.c
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

#ifndef __USBH_MSC_CORE_H
#define __USBH_MSC_CORE_H

#include "usbh_core.h"
#include "usbh_stdreq.h"
#include "usb_bsp.h"
#include "usbh_ioreq.h"
#include "usbh_hcs.h"
#include "usbh_msc_core.h"
#include "usbh_msc_scsi.h"
#include "usbh_msc_bot.h"

/* Structure for MSC process */
typedef struct _MSC_Process
{
    uint8_t              hc_num_in;
    uint8_t              hc_num_out;
    uint8_t              MSBulkOutEp;
    uint8_t              MSBulkInEp;
    uint16_t             MSBulkInEpSize;
    uint16_t             MSBulkOutEpSize;
    uint8_t              buff[USBH_MSC_MPS_SIZE];
    uint8_t              maxLun;
} MSC_Machine_TypeDef;

#define USB_REQ_BOT_RESET                0xFF
#define USB_REQ_GET_MAX_LUN              0xFE

extern USBH_Class_cb_TypeDef  USBH_MSC_cb;
extern MSC_Machine_TypeDef    MSC_Machine;
extern uint8_t MSCErrorCount;

#endif  /* __USBH_MSC_CORE_H */

/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/

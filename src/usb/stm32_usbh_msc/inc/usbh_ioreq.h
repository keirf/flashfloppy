/**
  ******************************************************************************
  * @file    usbh_ioreq.h
  * @author  MCD Application Team
  * @version V2.2.0
  * @date    09-November-2015
  * @brief   Header file for usbh_ioreq.c
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

#ifndef __USBH_IOREQ_H
#define __USBH_IOREQ_H

#include "usb_conf.h"
#include "usbh_core.h"
#include "usbh_def.h"

#define USBH_SETUP_PKT_SIZE   8
#define USBH_EP0_EP_NUM       0
#define USBH_MAX_PACKET_SIZE  0x40

USBH_Status USBH_CtlSendSetup ( USB_OTG_CORE_HANDLE *pdev,
                                uint8_t *buff,
                                uint8_t hc_num);

USBH_Status USBH_CtlSendData ( USB_OTG_CORE_HANDLE *pdev,
                                uint8_t *buff,
                                uint16_t length,
                                uint8_t hc_num);

USBH_Status USBH_CtlReceiveData( USB_OTG_CORE_HANDLE *pdev,
                                uint8_t *buff,
                                uint16_t length,
                                uint8_t hc_num);

USBH_Status USBH_BulkReceiveData( USB_OTG_CORE_HANDLE *pdev,
                                uint8_t *buff,
                                uint16_t length,
                                uint8_t hc_num);

USBH_Status USBH_BulkSendData ( USB_OTG_CORE_HANDLE *pdev,
                                uint8_t *buff,
                                uint16_t length,
                                uint8_t hc_num);

USBH_Status USBH_InterruptReceiveData( USB_OTG_CORE_HANDLE *pdev,
                                       uint8_t             *buff,
                                       uint8_t             length,
                                       uint8_t             hc_num);

USBH_Status USBH_InterruptSendData( USB_OTG_CORE_HANDLE *pdev,
                                    uint8_t *buff,
                                    uint8_t length,
                                    uint8_t hc_num);

USBH_Status USBH_CtlReq (USB_OTG_CORE_HANDLE *pdev,
                         USBH_HOST *phost,
                         uint8_t             *buff,
                         uint16_t            length);

USBH_Status USBH_IsocReceiveData( USB_OTG_CORE_HANDLE *pdev,
                                uint8_t *buff,
                                uint32_t length,
                                uint8_t hc_num);


USBH_Status USBH_IsocSendData( USB_OTG_CORE_HANDLE *pdev,
                                uint8_t *buff,
                                uint32_t length,
                                uint8_t hc_num);
#endif /* __USBH_IOREQ_H */

/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/

/**
  ******************************************************************************
  * @file    USBH_conf.h
  * @author  MCD Application Team
  * @version V1.2.0
  * @date    09-November-2015
  * @brief   General low level driver configuration
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

#ifndef __USBH_CONF__H__
#define __USBH_CONF__H__

#include "usb_conf.h"

#define USBH_MAX_NUM_ENDPOINTS                3  /* 1 bulk IN + 1 bulk Out */
                                                 /* + 1 additional interrupt IN* needed for some keys*/
#define USBH_MAX_NUM_INTERFACES               2
#define USBH_MSC_MAX_LUNS                     5  /* Up to 5 LUNs to be supported */
#ifdef USE_USB_OTG_FS
#define USBH_MSC_MPS_SIZE                 0x40
#else
#define USBH_MSC_MPS_SIZE                 0x200
#endif
#define USBH_MAX_DATA_BUFFER              0x400

#endif //__USBH_CONF__H__

/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/

/**
******************************************************************************
* @file    usb_conf.h
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

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __USB_CONF__H__
#define __USB_CONF__H__

#include "core_cm3.h"

#define USE_USB_OTG_FS 1
#define USE_HOST_MODE
#define USB_OTG_FS_CORE

#define RX_FIFO_FS_SIZE   128 /* 512 bytes */
#define TXH_NP_FS_FIFOSIZ  96 /* 384 bytes */
#define TXH_P_FS_FIFOSIZ   96 /* 384 bytes */

#endif //__USB_CONF__H__

/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/


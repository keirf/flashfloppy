/**
  ******************************************************************************
  * @file    usb_bsp.c
  * @author  MCD Application Team
  * @version V1.2.0
  * @date    09-November-2015
  * @brief   This file implements the board support package for the USB host library
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

#include "usb_bsp.h"
#include "usb_hcd_int.h"
#include "usbh_core.h"

#define USB_IRQ 67
void IRQ_67(void) __attribute__((alias("IRQ_usb")));

USB_OTG_CORE_HANDLE USB_OTG_Core;

void USB_OTG_BSP_Init(USB_OTG_CORE_HANDLE *pdev)
{
/*    RCC_OTGFSCLKConfig(RCC_OTGFSCLKSource_PLLVCO_Div3);*/
    rcc->ahbenr |= RCC_AHBENR_OTGFSEN;
}

void USB_OTG_BSP_EnableInterrupt(USB_OTG_CORE_HANDLE *pdev)
{
    IRQx_set_prio(USB_IRQ, 14); /* low-ish */
    IRQx_enable(USB_IRQ);
}

void USB_OTG_BSP_DriveVBUS(USB_OTG_CORE_HANDLE *pdev, uint8_t state)
{
}

void USB_OTG_BSP_ConfigVBUS(USB_OTG_CORE_HANDLE *pdev)
{
}

void USB_OTG_BSP_uDelay (const uint32_t usec)
{
    delay_us(usec);
}

void USB_OTG_BSP_mDelay (const uint32_t msec)
{
    delay_ms(msec);
}

static void IRQ_usb(void)
{
    USBH_OTG_ISR_Handler(&USB_OTG_Core);
}

/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/

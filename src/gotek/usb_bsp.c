/*
 * usb_bsp.c
 * 
 * FlashFloppy board callbacks for low-level STM32 USB OTG setup & handling.
 * 
 * Written & released by Keir Fraser <keir.xen@gmail.com>
 * 
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

#include "usb_bsp.h"
#include "usb_hcd_int.h"
#include "usb_core.h"

#define USB_IRQ 67
void IRQ_67(void) __attribute__((alias("IRQ_usb")));

USB_OTG_CORE_HANDLE USB_OTG_Core;

void USB_OTG_BSP_Init(USB_OTG_CORE_HANDLE *pdev)
{
    /* OTGFSPRE already clear in rcc->cfgr, OTG clock = PLL/3 */
    rcc->ahbenr |= RCC_AHBENR_OTGFSEN; /* OTG clock enable */
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

void USB_OTG_BSP_uDelay(const uint32_t usec)
{
    delay_us(usec);
}

void USB_OTG_BSP_mDelay(const uint32_t msec)
{
    delay_ms(msec);
}

static void IRQ_usb(void)
{
    USBH_OTG_ISR_Handler(&USB_OTG_Core);
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

/*
 * main.c
 * 
 * Bootstrap the STM32F105RB and get things moving.
 * 
 * Written & released by Keir Fraser <keir.xen@gmail.com>
 * 
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

#include "stm32f10x.h"
#include "intrinsics.h"
#include "util.h"

void ms_delay(int ms)
{
   while (ms-- > 0) {
      volatile int x=5971;
      while (x-- > 0)
         __asm("nop");
   }
}

int main(void)
{
    uint32_t x = 1u<<16;
    int i;

    clock_init();
    console_init();

    gpio_configure_pin(gpioa, 0, GPO_opendrain);

    i = usart1->dr; /* clear UART_SR_RXNE */    
    for (i = 0; !(usart1->sr & USART_SR_RXNE); i++) {
        printk("Hello world! printf test: '%5d' '%05d' %08x\n",
               -i, -i, rcc->cfgr);
        gpioa->bsrr = x ^= (1u<<16)|(1u<<0);
        ms_delay(100);
    }

    /* System reset */
    scb->aircr = SCB_AIRCR_VECTKEY | SCB_AIRCR_SYSRESETREQ;
    for (;;) ;

    return 0;
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

/*
 * main.c
 * 
 * Bootstrap the STM32F105RB and get things moving.
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

    rcc->apb2enr |= (1u<<14)/* usart1 */ | (1u<<2)/* gpioa */ | (1u<<0)/* afioen */;

    gpioa->crl = 0x44444446u;
    gpioa->crh = 0x444444a4u;

    usart1->cr1 = (1u<<13);
    usart1->cr2 = 0;
    usart1->cr3 = 0;
    usart1->gtpr = 0;
    usart1->brr = (1u<<4) | 1u; /* 460800 baud @ 8MHz */
    usart1->cr1 = (1u<<13) | (1u<<3) | (1u<<2);

    for (i = 0; i < 5; i++) {
        printk("Hello world! printf test: '%5d' '%05d' '%#014hhx' '%p' '%%'\n",
               -i, -i, 0x65383^i, gpioa);
        gpioa->bsrr = x ^= (1u<<16)|(1u<<0);
        ms_delay(100);
    }

    /* System reset */
    scb->aircr = SCB_AIRCR_VECTKEY | SCB_AIRCR_SYSRESETREQ;
    for (;;) ;

    return 0;
}

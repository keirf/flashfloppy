/*
 * console.c
 * 
 * printf-style interface to USART1.
 * 
 * Written & released by Keir Fraser <keir.xen@gmail.com>
 * 
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

#define BAUD 3000000 /* 3Mbaud */

int vprintk(const char *format, va_list ap)
{
    static char str[128];
    char *p;
    int n;

    IRQ_global_disable();

    n = vsnprintf(str, sizeof(str), format, ap);

    for (p = str; *p; p++) {
        while (!(usart1->sr & USART_SR_TXE))
            cpu_relax();
        usart1->dr = *p;
    }

    IRQ_global_enable();

    return n;
}

int printk(const char *format, ...)
{
    va_list ap;
    int n;

    va_start(ap, format);
    n = vprintk(format, ap);
    va_end(ap);

    return n;
}

void console_init(void)
{
    /* Enable the peripheral clock */
    rcc->apb2enr |= RCC_APB2ENR_USART1EN;

    /* Enable TX pin (PA9) for USART output, RX pin (PA10) as input. */
    gpio_configure_pin(gpioa, 9, AFO_pushpull);
    gpio_configure_pin(gpioa, 10, GPI_floating);

    /* BAUD, 8n1. */
    usart1->brr = SYSCLK / BAUD;
    usart1->cr1 = (USART_CR1_UE | USART_CR1_TE | USART_CR1_RE);
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

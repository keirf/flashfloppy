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

#define USART1_IRQ 37

static void emit_char(uint8_t c)
{
    while (!(usart1->sr & USART_SR_TXE))
        cpu_relax();
    usart1->dr = c;
}

int vprintk(const char *format, va_list ap)
{
    static char str[128];
    char *p, c;
    int n;

    IRQ_global_disable();

    n = vsnprintf(str, sizeof(str), format, ap);

    p = str;
    while ((c = *p++) != '\0') {
        switch (c) {
        case '\r': /* CR: ignore as we generate our own CR/LF */
            break;
        case '\n': /* LF: convert to CR/LF (usual terminal behaviour) */
            emit_char('\r');
            /* fall through */
        default:
            emit_char(c);
            break;
        }
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

void console_sync(void)
{
    IRQ_global_disable();
    /* Leave IRQs globally disabled. */
}

void console_init(void)
{
    /* Turn on the clocks. */
    rcc->apb2enr |= RCC_APB2ENR_USART1EN;

    /* Enable TX pin (PA9) for USART output, RX pin (PA10) as input. */
    gpio_configure_pin(gpioa, 9, AFO_pushpull(_10MHz));
    gpio_configure_pin(gpioa, 10, GPI_pull_up);

    /* BAUD, 8n1. */
    usart1->brr = SYSCLK / BAUD;
    usart1->cr1 = (USART_CR1_UE | USART_CR1_TE | USART_CR1_RE);
    usart1->cr3 = 0;
}

/* Debug helper: if we get stuck somewhere, calling this beforehand will cause 
 * any serial input to cause a crash dump of the stuck context. */
void console_crash_on_input(void)
{
    (void)usart1->dr; /* clear UART_SR_RXNE */
    usart1->cr1 |= USART_CR1_RXNEIE;
    IRQx_set_prio(USART1_IRQ, RESET_IRQ_PRI);
    IRQx_enable(USART1_IRQ);
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

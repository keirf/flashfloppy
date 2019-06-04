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

/* Normally flush to serial is asynchronously executed in a low-pri IRQ. */
void IRQ_44(void) __attribute__((alias("SOFTIRQ_console")));
#define CONSOLE_SOFTIRQ 44

/* We stage serial output in a ring buffer. */
static char ring[2048];
#define MASK(x) ((x)&(sizeof(ring)-1))
static unsigned int cons, prod;

/* The console can be set into synchronous mode in which case IRQ is disabled 
 * and the transmit-empty flag is polled manually for each byte. */
static bool_t sync_console;

static void flush_ring_to_serial(void)
{
    unsigned int c = cons, p = prod;
    barrier();

    while (c != p) {
        while (!(usart1->sr & USART_SR_TXE))
            cpu_relax();
        usart1->dr = ring[MASK(c++)];
    }

    barrier();
    cons = c;
}

static void SOFTIRQ_console(void)
{
    flush_ring_to_serial();
}

static void kick_tx(void)
{
    if (sync_console) {
        flush_ring_to_serial();
    } else if (cons != prod) {
        IRQx_set_pending(CONSOLE_SOFTIRQ);
    }
}

int vprintk(const char *format, va_list ap)
{
    static char str[128];
    char *p, c;
    int n;

    IRQ_global_disable();

    n = vsnprintf(str, sizeof(str), format, ap);

    p = str;
    while (((c = *p++) != '\0') && ((prod-cons) != (sizeof(ring) - 1))) {
        switch (c) {
        case '\r': /* CR: ignore as we generate our own CR/LF */
            break;
        case '\n': /* LF: convert to CR/LF (usual terminal behaviour) */
            ring[MASK(prod++)] = '\r';
            /* fall through */
        default:
            ring[MASK(prod++)] = c;
            break;
        }
    }

    kick_tx();

    if (!sync_console)
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
    if (sync_console)
        return;

    IRQ_global_disable();
    sync_console = TRUE;
    kick_tx();

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
    usart1->cr1 = USART_CR1_UE | USART_CR1_TE | USART_CR1_RE;
    usart1->cr3 = 0;

    IRQx_set_prio(CONSOLE_SOFTIRQ, CONSOLE_IRQ_PRI);
    IRQx_enable(CONSOLE_SOFTIRQ);
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

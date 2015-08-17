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

#define DMA1_CH4_IRQ 14
void IRQ_14(void) __attribute__((alias("IRQ_dma1_ch4_tc")));

/* We stage serial output in a ring buffer. DMA occurs from the ring buffer;
 * the consumer index being updated each time a DMA sequence completes. */
static char ring[2048];
#define MASK(x) ((x)&(sizeof(ring)-1))
static unsigned int cons, prod, dma_sz;

/* The console can be set into synchronous mode in which case DMA is disabled 
 * and the transmit-empty flag is polled manually for each byte. */
static bool_t sync_console = 1;

static void kick_tx(void)
{
    if (sync_console) {

        while (cons != prod) {
            while (!(usart1->sr & USART_SR_TXE))
                cpu_relax();
            usart1->dr = ring[MASK(cons++)];
        }

    } else if (!dma_sz && (cons != prod)) {

        dma_sz = min(MASK(prod-cons), sizeof(ring)-MASK(cons));
        dma1->ch4.cmar = (uint32_t)(unsigned long)&ring[MASK(cons)];
        dma1->ch4.cndtr = dma_sz;
        dma1->ch4.ccr = (DMA_CCR_MSIZE_8BIT |
                         /* The manual doesn't allow byte accesses to usart. */
                         DMA_CCR_PSIZE_16BIT |
                         DMA_CCR_MINC |
                         DMA_CCR_DIR_M2P |
                         DMA_CCR_TCIE |
                         DMA_CCR_EN);

    }
}

static void IRQ_dma1_ch4_tc(void)
{
    /* Clear the DMA controller. */
    dma1->ch4.ccr = 0;
    dma1->ifcr = DMA_IFCR_CTCIF4;

    /* Update ring state. */
    cons += dma_sz;
    dma_sz = 0;

    /* Kick off more transmit activity. */
    kick_tx();
}

int vprintk(const char *format, va_list ap)
{
    static char str[128];
    char *p;
    int n;

    IRQ_global_disable();

    n = vsnprintf(str, sizeof(str), format, ap);

    for (p = str; *p && ((prod-cons) != sizeof(ring)); p++)
        ring[MASK(prod++)] = *p;

    kick_tx();

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

    /* Wait for DMA completion and then kick off synchronous mode. */
    while (dma1->ch4.cndtr)
        cpu_relax();
    IRQ_dma1_ch4_tc();

    /* Leave IRQs globally disabled. */
}

void console_init(void)
{
    /* Turn on the clocks. */
    rcc->apb2enr |= RCC_APB2ENR_USART1EN;

    /* Enable TX pin (PA9) for USART output, RX pin (PA10) as input. */
    gpio_configure_pin(gpioa, 9, AFO_pushpull(_10MHz));
    gpio_configure_pin(gpioa, 10, GPI_floating);

    /* BAUD, 8n1. */
    usart1->brr = SYSCLK / BAUD;
    usart1->cr1 = (USART_CR1_UE | USART_CR1_TE | USART_CR1_RE);
    usart1->cr3 = USART_CR3_DMAT;

    /* Initialise DMA1 channel 4 and its completion interrupt. */
    dma1->ch4.cpar = (uint32_t)(unsigned long)&usart1->dr;
    dma1->ifcr = DMA_IFCR_CTCIF4;
    IRQx_set_prio(DMA1_CH4_IRQ, 13);
    IRQx_enable(DMA1_CH4_IRQ);
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

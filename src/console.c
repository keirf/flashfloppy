/*
 * console.c
 * 
 * printf-style interface to USART.
 * 
 * Written & released by Keir Fraser <keir.xen@gmail.com>
 * 
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

#define BAUD 3000000 /* 3Mbaud */

#if MCU == MCU_rp2350

/* UART0 on GPIO 0 (TX) / GPIO 1 (RX), clocked from clk_peri. */
#define USART_IRQ UART0_IRQ
#define usart_tx_pin 0
#define usart_rx_pin 1
#define PCLK (APB1_MHZ * 1000000)

#else

#define USART1_IRQ 37
#define USART3_IRQ 39

#if 1
#define usart usart1
#define USART_IRQ USART1_IRQ
#define usart_gpio gpioa
#define usart_tx_pin 9
#define usart_rx_pin 10
#define PCLK (APB2_MHZ * 1000000)
#else
#define usart usart3
#define USART_IRQ USART3_IRQ
#define usart_gpio gpioc
#define usart_tx_pin 10
#define usart_rx_pin 11
#define PCLK (APB1_MHZ * 1000000)
#endif

#endif

/* On RP2350 the console is mirrored to a USB CDC-ACM device on the Pico's
 * own USB socket, as well as going out of UART0. */
#if MCU == MCU_rp2350
#define USB_CONSOLE 1
#else
#define USB_CONSOLE 0
#endif

/* Normally flush to serial is asynchronously executed in a low-pri IRQ. */
#define CONSOLE_SOFTIRQ SOFTIRQ_1
DEFINE_IRQ(CONSOLE_SOFTIRQ, "SOFTIRQ_console");

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
#if MCU == MCU_rp2350
        while (uart0->fr & UART_FR_TXFF)
            cpu_relax();
        uart0->dr = ring[MASK(c++)];
#else
        while (!(usart->sr & USART_SR_TXE))
            cpu_relax();
        usart->dr = ring[MASK(c++)];
#endif
    }

    barrier();
    cons = c;
}

static void SOFTIRQ_console(void)
{
    flush_ring_to_serial();
#if USB_CONSOLE
    usb_cdc_kick();
#endif
}

/* Stage one character for output. */
static void ring_push(char c)
{
    ring[MASK(prod++)] = c;
#if USB_CONSOLE
    usb_cdc_putc(c);
#endif
}

static void kick_tx(void)
{
    if (sync_console) {
        flush_ring_to_serial();
#if USB_CONSOLE
        usb_cdc_flush_sync();
#endif
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
            ring_push('\r');
            /* fall through */
        default:
            ring_push(c);
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
#if MCU == MCU_rp2350

    /* TX/RX pins to the UART. */
    gpio_configure_pin(gpioa, usart_rx_pin, GPI_pull_up);
    io_bank0->gpio[usart_tx_pin].ctrl = GPIO_FUNC_UART;
    io_bank0->gpio[usart_rx_pin].ctrl = GPIO_FUNC_UART;
    gpio_set_pad(usart_tx_pin, PAD_IE | PAD_DRIVE_4MA);

    /* BAUD, 8n1, FIFOs enabled. */
    uart0->ibrd = PCLK / (16 * BAUD);
    uart0->fbrd = ((PCLK % (16 * BAUD)) * 64 + (16 * BAUD) / 2) / (16 * BAUD);
    uart0->lcr_h = UART_LCR_H_WLEN8 | UART_LCR_H_FEN;
    uart0->cr = UART_CR_UARTEN | UART_CR_TXE | UART_CR_RXE;

#else

    /* Turn on the clocks. */
#if USART_IRQ == USART1_IRQ
    rcc->apb2enr |= RCC_APB2ENR_USART1EN;
#else
    rcc->apb1enr |= RCC_APB1ENR_USART3EN;
#endif

    /* Enable TX pin for USART output, RX pin as input. */
#if MCU == MCU_stm32f105
    gpio_configure_pin(usart_gpio, usart_tx_pin, AFO_pushpull(_10MHz));
    gpio_configure_pin(usart_gpio, usart_rx_pin, GPI_pull_up);
#elif MCU == MCU_at32f435
    gpio_set_af(usart_gpio, usart_tx_pin, 7);
    gpio_set_af(usart_gpio, usart_rx_pin, 7);
    gpio_configure_pin(usart_gpio, usart_tx_pin, AFO_pushpull(_10MHz));
    gpio_configure_pin(usart_gpio, usart_rx_pin, AFI(PUPD_up));
#endif

    /* BAUD, 8n1. */
    usart->brr = PCLK / BAUD;
    usart->cr1 = USART_CR1_UE | USART_CR1_TE | USART_CR1_RE;
    usart->cr3 = 0;

#endif

    IRQx_set_prio(CONSOLE_SOFTIRQ, CONSOLE_IRQ_PRI);
    IRQx_enable(CONSOLE_SOFTIRQ);
}

/* Debug helper: if we get stuck somewhere, calling this beforehand will cause
 * any serial input to cause a crash dump of the stuck context. */
void console_crash_on_input(void)
{
#if MCU == MCU_rp2350

    (void)uart0->dr;
    uart0->imsc = UART_INT_RX; /* deliberately unhandled -> crash dump */

#else

    if (mcu_package == MCU_QFN32) {
        /* Unavailable: PA10 is reassigned from SER_RX to K4 (rotary select
         * on the KC30 header). */
        return;
    }

    (void)usart->dr; /* clear UART_SR_RXNE */
    usart->cr1 |= USART_CR1_RXNEIE;

#endif

    IRQx_set_prio(USART_IRQ, RESET_IRQ_PRI);
    IRQx_enable(USART_IRQ);
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

/*
 * gotek/floppy.c
 * 
 * Gotek-specific floppy-interface setup.
 * 
 * Written & released by Keir Fraser <keir.xen@gmail.com>
 * 
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

#define O_FALSE 1
#define O_TRUE  0

/* Offsets within the input_pins bitmap. */
#define inp_dir     0
#define inp_step    2
#define inp_sel0    1
#define inp_wgate   7
#define inp_side    4

/* Outputs. */
#define gpio_out gpiob
#define pin_dskchg  7
#define pin_index   8
#define pin_trk0    6
#define pin_wrprot  5
#define pin_rdy     3

#define gpio_data gpioa

#define pin_wdata   8
#define tim_wdata   (tim1)
#define dma_wdata   (dma1->ch2)
#define dma_wdata_ch 2
#define dma_wdata_irq 12
void IRQ_12(void) __attribute__((alias("IRQ_wdata_dma")));

#define pin_rdata   7
#define tim_rdata   (tim3)
#define dma_rdata   (dma1->ch3)
#define dma_rdata_ch 3
#define dma_rdata_irq 13
void IRQ_13(void) __attribute__((alias("IRQ_rdata_dma")));

/* SELA line changes. */
#define IRQ_SELA 6
void IRQ_6(void) __attribute__((alias("IRQ_SELA_changed"))); /* EXTI0 */

/* Other EXTI IRQs relevant for us. */
void IRQ_7(void) __attribute__((alias("IRQ_input_changed"))); /* EXTI1 */
void IRQ_10(void) __attribute__((alias("IRQ_input_changed"))); /* EXTI4 */
void IRQ_23(void) __attribute__((alias("IRQ_input_changed"))); /* EXTI9_5 */
static const uint8_t exti_irqs[] = { 7, 10, 23 };

/* Input pins:
 * DIR = PB0, STEP=PA1, SELA=PA0, WGATE=PB9, SIDE=PB4
 */
static uint8_t input_update(void)
{
    uint16_t pr, in_a, in_b;

    pr = exti->pr & ~1; /* ignore SELA, handled in IRQ_SELA_changed */
    exti->pr = pr;

    in_a = gpioa->idr;
    in_b = gpiob->idr;
    input_pins = ((in_a << 1) & 0x06) | ((in_b >> 2) & 0x80) | (in_b & 0x11);

    return ((pr << 1) & 0x06) | ((pr >> 2) & 0x80) | (pr & 0x10);
}

static void board_floppy_init(void)
{
    gpio_configure_pin(gpiob, 0, GPI_bus);
    gpio_configure_pin(gpioa, 1, GPI_bus);
    gpio_configure_pin(gpioa, 0, GPI_bus);
    gpio_configure_pin(gpiob, 9, GPI_bus);
    gpio_configure_pin(gpiob, 4, GPI_bus);

    /* PB[15:2] -> EXT[15:2], PA[1:0] -> EXT[1:0] */
    afio->exticr2 = afio->exticr3 = afio->exticr4 = 0x1111;
    afio->exticr1 = 0x1100;

    exti->imr = exti->rtsr = exti->ftsr = m(9) | m(4) | m(1) | m(0);

    IRQx_set_prio(IRQ_SELA, FLOPPY_IRQ_SEL_PRI);
    IRQx_set_pending(IRQ_SELA);
    IRQx_enable(IRQ_SELA);
}

static void IRQ_SELA_changed(void)
{
    /* Clear SELA-changed flag. */
    exti->pr = 1;

    if (!(gpioa->idr & 1)) {
        /* SELA is asserted (this drive is selected). 
         * Immediately re-enable all our asserted outputs. */
        gpio_out->brr = gpio_out_active;
        /* Set pin_rdata as timer output (AFO_bus). */
        if (dma_rd && (dma_rd->state == DMA_active))
            gpio_data->crl = (gpio_data->crl & ~(0xfu<<(pin_rdata<<2)))
                | (AFO_bus<<(pin_rdata<<2));
        /* Let main code know it can drive the bus until further notice. */
        drive.sel = 1;
    } else {
        /* SELA is deasserted (this drive is not selected).
         * Relinquish the bus by disabling all our asserted outputs. */
        gpio_out->bsrr = gpio_out_active;
        /* Set pin_rdata to GPO_pushpull(_2MHz). */
        if (dma_rd && (dma_rd->state == DMA_active))
            gpio_data->crl = (gpio_data->crl & ~(0xfu<<(pin_rdata<<2)))
                | (2<<(pin_rdata<<2));
        /* Tell main code to leave the bus alone. */
        drive.sel = 0;
    }
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

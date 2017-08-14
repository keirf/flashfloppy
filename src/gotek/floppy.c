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

/* Input pins: DIR=PB0, STEP=PA1, SELA=PA0, WGATE=PB9, SIDE=PB4 */
#define pin_dir     0 /* PB0 */
#define pin_step    1 /* PA1 */
#define pin_sel0    0 /* PA0 */
#define pin_wgate   9 /* PB9 */
#define pin_side    4 /* PB4 */

/* Output pins. */
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

/* EXTI IRQs. */
void IRQ_6(void) __attribute__((alias("IRQ_SELA_changed"))); /* EXTI0 */
void IRQ_7(void) __attribute__((alias("IRQ_STEP_changed"))); /* EXTI1 */
void IRQ_10(void) __attribute__((alias("IRQ_SIDE_changed"))); /* EXTI4 */
void IRQ_23(void) __attribute__((alias("IRQ_WGATE_changed"))); /* EXTI9_5 */
static const struct exti_irq exti_irqs[] = {
    {  6, FLOPPY_IRQ_SEL_PRI, 0 }, 
    {  7, FLOPPY_IRQ_STEP_PRI, m(pin_step) },
    { 10, FLOPPY_IRQ_SIDE_PRI, 0 }, 
    { 23, FLOPPY_IRQ_WGATE_PRI, 0 } 
};

static void board_floppy_init(void)
{
    gpio_configure_pin(gpiob, pin_dir,   GPI_bus);
    gpio_configure_pin(gpioa, pin_step,  GPI_bus);
    gpio_configure_pin(gpioa, pin_sel0,  GPI_bus);
    gpio_configure_pin(gpiob, pin_wgate, GPI_bus);
    gpio_configure_pin(gpiob, pin_side,  GPI_bus);

    /* PB[15:2] -> EXT[15:2], PA[1:0] -> EXT[1:0] */
    afio->exticr2 = afio->exticr3 = afio->exticr4 = 0x1111;
    afio->exticr1 = 0x1100;

    exti->imr = exti->rtsr = exti->ftsr =
        m(pin_wgate) | m(pin_side) | m(pin_step) | m(pin_sel0);
}

static void IRQ_SELA_changed(void)
{
    /* Clear SELA-changed flag. */
    exti->pr = m(pin_sel0);

    if (!(gpioa->idr & m(pin_sel0))) {
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

static void IRQ_STEP_changed(void)
{
    struct drive *drv = &drive;
    uint8_t idr_a, idr_b;

    /* Clear STEP-changed flag. */
    exti->pr = m(pin_step);

    /* Latch inputs. */
    idr_a = gpioa->idr;
    idr_b = gpiob->idr;

    /* Bail if drive not selected. */
    if (idr_a & m(pin_sel0))
        return;

    /* DSKCHG asserts on any falling edge of STEP. We deassert on any edge. */
    if ((gpio_out_active & m(pin_dskchg)) && (dma_rd != NULL))
        floppy_change_outputs(m(pin_dskchg), O_FALSE);

    if (!(idr_a & m(pin_step))   /* Not rising edge on STEP? */
        || (drv->step.state & STEP_active)) /* Already mid-step? */
        return;

    /* Latch the step direction and check bounds (0 <= cyl <= 255). */
    drv->step.inward = !(idr_b & m(pin_dir));
    if (drv->cyl == (drv->step.inward ? 255 : 0))
        return;

    /* Valid step request for this drive: start the step operation. */
    drv->step.start = stk_now();
    drv->step.state = STEP_started;
    if (gpio_out_active & m(pin_trk0))
        floppy_change_outputs(m(pin_trk0), O_FALSE);
    if (dma_rd != NULL)
        rdata_stop();
    IRQx_set_pending(STEP_IRQ);
}

static void IRQ_SIDE_changed(void)
{
    struct drive *drv = &drive;

    /* Clear SIDE-changed flag. */
    exti->pr = m(pin_side);

    drv->head = !(gpiob->idr & m(pin_side));
    if (dma_rd != NULL)
        rdata_stop();
}

static void IRQ_WGATE_changed(void)
{
    /* Clear WGATE-changed flag. */
    exti->pr = m(pin_wgate);

    /* If WRPROT line is asserted then we ignore WGATE. */
    if (gpio_out_active & m(pin_wrprot))
        return;

    if ((gpiob->idr & m(pin_wgate))      /* WGATE off? */
        || (gpioa->idr & m(pin_sel0))) { /* Not selected? */
        wdata_stop();
    } else {
        rdata_stop();
        wdata_start();
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

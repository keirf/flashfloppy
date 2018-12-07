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

/* Input pins: DIR=PB0, STEP=PA1, SELA=PA0, SELB=PA3, WGATE=PB9, SIDE=PB4, 
 *             MOTOR=PA15 */
#define pin_dir     0 /* PB0 */
#define pin_step    1 /* PA1 */
#define pin_sel0    0 /* PA0 */
#define pin_sel1    3 /* PA3 */
#define pin_wgate   9 /* PB9 */
#define pin_side    4 /* PB4 */
#define pin_motor  15 /* PA15 */

/* Output pins. */
#define gpio_out gpiob
#define pin_02      7
#define pin_08      8
#define pin_26      6
#define pin_28      5
#define pin_34      3

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
/*void IRQ_6(void) __attribute__((alias("IRQ_SELA_changed")));*/ /* EXTI0 */
void IRQ_7(void) __attribute__((alias("IRQ_STEP_changed"))); /* EXTI1 */
void IRQ_10(void) __attribute__((alias("IRQ_SIDE_changed"))); /* EXTI4 */
void IRQ_23(void) __attribute__((alias("IRQ_WGATE_changed"))); /* EXTI9_5 */
static const struct exti_irq exti_irqs[] = {
    {  6, FLOPPY_IRQ_SEL_PRI, 0 }, 
    {  7, FLOPPY_IRQ_STEP_PRI, m(pin_step) },
    { 10, FLOPPY_IRQ_SIDE_PRI, 0 }, 
    { 23, FLOPPY_IRQ_WGATE_PRI, 0 } 
};

bool_t floppy_ribbon_is_reversed(void)
{
    time_t t_start = time_now();

    /* If ribbon is reversed then most/all inputs are grounded. 
     * Check SEL plus three inputs which are supposed only to pulse. */
    while (!(gpioa->idr & (m(pin_sel0) | m(pin_step) | m(pin_wdata)))
           && !(gpiob->idr & m(pin_wgate))) {
        /* If all four inputs are LOW for a full second, conclude that 
         * the ribbon is reversed. */
        if (time_since(t_start) > time_ms(1000))
            return TRUE;
    }

    return FALSE;
}

static void board_floppy_init(void)
{
    gpio_configure_pin(gpiob, pin_dir,   GPI_bus);
    gpio_configure_pin(gpioa, pin_step,  GPI_bus);
    gpio_configure_pin(gpioa, pin_sel0,  GPI_bus);
    gpio_configure_pin(gpiob, pin_wgate, GPI_bus);
    gpio_configure_pin(gpiob, pin_side,  GPI_bus);
    if (gotek_enhanced()) {
        gpio_configure_pin(gpioa, pin_sel1,  GPI_bus);
        gpio_configure_pin(gpioa, pin_motor, GPI_bus);
    }

    /* PB[15:2] -> EXT[15:2], PA[1:0] -> EXT[1:0] */
    afio->exticr2 = afio->exticr3 = afio->exticr4 = 0x1111;
    afio->exticr1 = 0x1100;

    exti->imr = exti->rtsr = exti->ftsr =
        m(pin_wgate) | m(pin_side) | m(pin_step) | m(pin_sel0);
}

/* Fast speculative entry point for SELA-changed IRQ. We assume SELA has 
 * changed to the opposite of what we observed on the previous interrupt. This
 * is always the case unless we missed an edge (fast transitions). 
 * Note that the entirety of the SELA handler is in SRAM (.data) -- not only 
 * is this faster to execute, but allows us to co-locate gpio_out_active for 
 * even faster access in the time-critical speculative entry point. */
void IRQ_SELA_changed(void);
asm (
"    .data\n"
"    .align 4\n"
"    .thumb_func\n"
"    .type IRQ_SELA_changed,%function\n"
"IRQ_SELA_changed:\n"
"    ldr  r0, [pc, #4]\n" /* r0 = gpio_out_active */
"    ldr  r1, [pc, #8]\n" /* r1 = &gpio_out->b[s]rr */
"    str  r0, [r1, #0]\n" /* gpio_out->b[s]rr = gpio_out_active */
"    b.n  _IRQ_SELA_changed\n" /* branch to the main ISR entry point */
"gpio_out_active:   .word 0\n"
"gpio_out_setreset: .word 0x40010c10\n" /* gpio_out->b[s]rr */
"    .global IRQ_6\n"
"    .thumb_set IRQ_6,IRQ_SELA_changed\n"
"    .previous\n"
);

/* Subset of output pins which are active (O_TRUE). */
extern uint32_t gpio_out_active;

/* GPIO register to either assert or deassert active output pins. */
extern uint32_t gpio_out_setreset;

static void Amiga_HD_ID(uint32_t _gpio_out_active, uint32_t _gpio_out_setreset)
    __attribute__((used)) __attribute__((section(".data@")));
static void _IRQ_SELA_changed(uint32_t _gpio_out_active)
    __attribute__((used)) __attribute__((section(".data@")));

/* Intermediate SELA-changed handler for generating the Amiga HD RDY signal. */
static void Amiga_HD_ID(uint32_t _gpio_out_active, uint32_t _gpio_out_setreset)
{
    /* If deasserting the bus, toggle pin 34 for next time we take the bus. */
    if (!(_gpio_out_setreset & 4))
        gpio_out_active ^= m(pin_34);

    /* Continue to the main SELA-changed IRQ entry point. */
    _IRQ_SELA_changed(_gpio_out_active);
}

/* Main entry point for SELA-changed IRQ. This fixes up GPIO pins if we 
 * mis-speculated, also handles the timer-driver RDATA pin, and sets up the 
 * speculative entry point for the next interrupt. */
static void _IRQ_SELA_changed(uint32_t _gpio_out_active)
{
    /* Clear SELA-changed flag. */
    exti->pr = m(pin_sel0);

    if (!(gpioa->idr & m(pin_sel0))) {
        /* SELA is asserted (this drive is selected). 
         * Immediately re-enable all our asserted outputs. */
        gpio_out->brr = _gpio_out_active;
        /* Set pin_rdata as timer output (AFO_bus). */
        if (dma_rd && (dma_rd->state == DMA_active))
            gpio_data->crl = (gpio_data->crl & ~(0xfu<<(pin_rdata<<2)))
                | ((AFO_bus&0xfu)<<(pin_rdata<<2));
        /* Let main code know it can drive the bus until further notice. */
        drive.sel = 1;
    } else {
        /* SELA is deasserted (this drive is not selected).
         * Relinquish the bus by disabling all our asserted outputs. */
        gpio_out->bsrr = _gpio_out_active;
        /* Set pin_rdata as quiescent (GPO_bus). */
        if (dma_rd && (dma_rd->state == DMA_active))
            gpio_data->crl = (gpio_data->crl & ~(0xfu<<(pin_rdata<<2)))
                | ((GPO_bus&0xfu)<<(pin_rdata<<2));
        /* Tell main code to leave the bus alone. */
        drive.sel = 0;
    }

    /* Set up the speculative fast path for the next interrupt. */
    if (drive.sel)
        gpio_out_setreset &= ~4; /* gpio_out->bsrr */
    else
        gpio_out_setreset |= 4; /* gpio_out->brr */
}

/* Update the SELA handler. Used for switching in the Amiga HD-ID "magic". 
 * Must be called with interrupts disabled. */
static void update_SELA_irq(bool_t amiga_hd_id)
{
    uint32_t handler = amiga_hd_id ? (uint32_t)Amiga_HD_ID
        : (uint32_t)_IRQ_SELA_changed;
    uint32_t entry = (uint32_t)IRQ_SELA_changed;
    uint16_t opcode;

    /* Strip the Thumb LSB from the function addresses. */
    handler &= ~1;
    entry &= ~1;

    /* Create a new tail-call instruction for the entry stub. */
    opcode = handler - (entry + 6 + 4);
    opcode = 0xe000 | (opcode >> 1);

    /* If the tail-call instruction has changed, modify the entry stub. */
    if (unlikely(((uint16_t *)entry)[3] != opcode)) {
        ((uint16_t *)entry)[3] = opcode;
        cpu_sync(); /* synchronise self-modifying code */
    }
}

static bool_t drive_is_writing(void)
{
    if (!dma_wr)
        return FALSE;
    switch (dma_wr->state) {
    case DMA_starting:
    case DMA_active:
        return TRUE;
    }
    return FALSE;
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
    if ((drv->outp & m(outp_dskchg)) && (dma_rd != NULL))
        drive_change_output(drv, outp_dskchg, FALSE);

    if (!(idr_a & m(pin_step))   /* Not rising edge on STEP? */
        || (drv->step.state & STEP_active) /* Already mid-step? */
        || drive_is_writing())   /* Write in progress? */
        return;

    /* Latch the step direction and check bounds (0 <= cyl <= 255). */
    drv->step.inward = !(idr_b & m(pin_dir));
    if (drv->cyl == (drv->step.inward ? 255 : 0))
        return;

    /* Valid step request for this drive: start the step operation. */
    drv->step.start = time_now();
    drv->step.state = STEP_started;
    if (drv->outp & m(outp_trk0))
        drive_change_output(drv, outp_trk0, FALSE);
    if (dma_rd != NULL) {
        rdata_stop();
        if (!ff_cfg.index_suppression) {
            /* Opportunistically insert an INDEX pulse ahead of seek op. */
            drive_change_output(drv, outp_index, TRUE);
            index.fake_fired = TRUE;
        }
    }
    IRQx_set_pending(FLOPPY_SOFTIRQ);
}

static void IRQ_SIDE_changed(void)
{
    stk_time_t t = stk_now();
    unsigned int filter = stk_us(ff_cfg.side_select_glitch_filter);
    struct drive *drv = &drive;
    uint8_t hd;

    do {
        /* Clear SIDE-changed flag. */
        exti->pr = m(pin_side);

        /* Has SIDE actually changed? */
        hd = !(gpiob->idr & m(pin_side));
        if (hd == drv->head)
            return;

        /* If configured to do so, wait a few microseconds to ensure this isn't
         * a glitch (eg. signal is mistaken for the archaic Fault-Reset line by
         * old CP/M loaders, and pulsed LOW when starting a read). */
    } while (stk_diff(t, stk_now()) < filter);

    drv->head = hd;
    if ((dma_rd != NULL) && (drv->nr_sides == 2))
        rdata_stop();
}

static void IRQ_WGATE_changed(void)
{
    struct drive *drv = &drive;

    /* Clear WGATE-changed flag. */
    exti->pr = m(pin_wgate);

    /* If WRPROT line is asserted then we ignore WGATE. */
    if (drv->outp & m(outp_wrprot))
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

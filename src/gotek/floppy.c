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
 *             MOTOR=PA15/PB15 */
#if defined(APPLE2)
#define pin_pha0    6 /* PA6 */
#define pin_pha1   15 /* PA15 */
#define pin_pha2    0 /* PB0 */
#define pin_pha3    1 /* PA1 */
#else
#define pin_dir     0 /* PB0 */
#define pin_step    1 /* PA1 */
#endif
#define pin_sel0    0 /* PA0 */
#define pin_sel1    3 /* PA3 */
static uint8_t pin_wgate = 9; /* PB9 */
#define pin_side    4 /* PB4 */
static uint8_t pin_motor = 15; /* PA15 or PB15 */
#define pin_chgrst 14 /* PA14 if CHGRST_pa14 */

/* Output pins. PBx = 0-15, PAx = 16-31. */
static uint8_t pin_02 = 7; /* PB7 */
#define pin_08      8      /* PB8 */
static uint8_t pin_26 = 6; /* PB6 */
#define pin_28      5      /* PB5 */
#define pin_34      3      /* PB3 */

#define gpio_data gpioa

#define pin_wdata   8
#define tim_wdata   (tim1)
#define dma_wdata   (dma1->ch[2-1])
#define dma_wdata_ch 2
#define dma_wdata_irq DMA1_CH2_IRQ
DEFINE_IRQ(dma_wdata_irq, "IRQ_wdata_dma");

#define pin_rdata   7
#define tim_rdata   (tim3)
#define dma_rdata   (dma1->ch[3-1])
#define dma_rdata_ch 3
#define dma_rdata_irq DMA1_CH3_IRQ
DEFINE_IRQ(dma_rdata_irq, "IRQ_rdata_dma");

/* Head step handling. */
#if defined(APPLE2)
static struct timer step_timer;
static void POLL_step(void *unused);
#else
void IRQ_28(void) __attribute__((alias("IRQ_STEP_changed"))); /* TMR2 */
#endif

/* EXTI IRQs. */
void IRQ_6(void) __attribute__((alias("IRQ_SELA_changed"))); /* EXTI0 */
void IRQ_7(void) __attribute__((alias("IRQ_WGATE_rotary"))); /* EXTI1 */
void IRQ_10(void) __attribute__((alias("IRQ_SIDE_changed"))); /* EXTI4 */
void IRQ_23(void) __attribute__((alias("IRQ_WGATE_rotary"))); /* EXTI9_5 */
void IRQ_40(void) __attribute__((alias("IRQ_MOTOR_CHGRST_rotary"))); /* EXTI15_10 */
#if WDATA_TOGGLE
void IRQ_27(void) __attribute__((alias("IRQ_wdata_capture"))); /* TMR1_CC */
#endif
#define MOTOR_CHGRST_IRQ 40
static const struct exti_irq exti_irqs[] = {
#if WDATA_TOGGLE
    /* WDATA */ { 27, FLOPPY_IRQ_WGATE_PRI, 0 },
#endif
    /* SELA */ {  6, FLOPPY_IRQ_SEL_PRI, 0 }, 
#if !defined(APPLE2)
    /* STEP */ { 28, FLOPPY_IRQ_STEP_PRI, m(2) /* dummy */ },
#endif
    /* WGATE */ {  7, FLOPPY_IRQ_WGATE_PRI, 0 },
    /* SIDE */ { 10, TIMER_IRQ_PRI, 0 }, 
    /* WGATE */ { 23, FLOPPY_IRQ_WGATE_PRI, 0 },
    /* MTR/CHGRST */ { 40, TIMER_IRQ_PRI, 0 }
};

/* Subset of output pins which are active (O_TRUE). */
extern uint32_t gpio_out_active;

/* Abuse gpio_out_active:PA11 to indicate that read DMA is active. This is 
 * safe because PA11 is configured for USB, so GPIO level has no effect. 
 * This saves some memory loads in the critical SELA IRQ handler. */
#define GPIO_OUT_DMA_RD_ACTIVE (16+11)

/* GPIO register to either assert or deassert active output pins. */
extern uint32_t gpiob_setreset;

/* This bitband address is used to atomically update GPIO_OUT_DMA_RD_ACTIVE */
static volatile uint32_t *p_dma_rd_active;
#define dma_rd_set_active(x) (*p_dma_rd_active = (x))

bool_t floppy_ribbon_is_reversed(void)
{
#if !defined(APPLE2)
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
#endif

    return FALSE;
}

static uint32_t *get_bitband(void *ram_addr, unsigned int bit)
{
    uint32_t byte = (uint32_t)ram_addr - 0x20000000u;
    return (uint32_t *)(0x22000000u + (byte * 32) + (bit * 4));
}

static void board_floppy_init(void)
{
    p_dma_rd_active = get_bitband(&gpio_out_active, GPIO_OUT_DMA_RD_ACTIVE);

#if MCU == STM32F105

#define change_pin_mode(gpio, pin, mode)                \
    gpio->crl = (gpio->crl & ~(0xfu<<((pin)<<2)))       \
        | (((mode)&0xfu)<<((pin)<<2))

#if !defined(APPLE2)
    gpio_configure_pin(gpioa, pin_step, GPI_bus);
#endif
    gpio_configure_pin(gpio_data, pin_wdata, GPI_bus);
    gpio_configure_pin(gpio_data, pin_rdata, GPO_bus);

#elif MCU == AT32F435

#define change_pin_mode(gpio, pin, mode)                \
    gpio->moder = (gpio->moder & ~(0x3u<<((pin)<<1)))   \
        | (((mode)&0x3u)<<((pin)<<1));
#define afio syscfg

#if !defined(APPLE2)
    gpio_set_af(gpioa, pin_step, 1);
    gpio_configure_pin(gpioa, pin_step, AFI(PUPD_none));
#endif

    gpio_set_af(gpio_data, pin_wdata, 1);
    gpio_configure_pin(gpio_data, pin_wdata, AFI(PUPD_none));

    gpio_set_af(gpio_data, pin_rdata, 2);
    gpio_configure_pin(gpio_data, pin_rdata, GPO_bus);

    dmamux1->cctrl[dma_wdata_ch-1] = DMAMUX_CCTRL_REQSEL(DMAMUX_REQ_TIM1_CH1);
    dmamux1->cctrl[dma_rdata_ch-1] = DMAMUX_CCTRL_REQSEL(DMAMUX_REQ_TIM3_OVF);

#endif

    /* PA1 (STEP) triggers IRQ via TIM2 Channel 2, since EXTI is used for 
     * WGATE on PB1. */
    tim2->ccmr1 = TIM_CCMR1_CC2S(TIM_CCS_INPUT_TI1);
    tim2->ccer = TIM_CCER_CC2E;
    tim2->dier = TIM_DIER_CC2IE;
    tim2->cr1 = TIM_CR1_CEN;

    if (mcu_package == MCU_QFN32) {
        pin_02 = 16 + 14; /* PA14 */
        pin_26 = 16 + 13; /* PA13 */
        pin_wgate = 1; /* PB1 */
    }

#if defined(APPLE2)
    gpio_configure_pin(gpioa, pin_pha0,  GPI_bus);
    gpio_configure_pin(gpioa, pin_pha1,  GPI_bus);
    gpio_configure_pin(gpiob, pin_pha2,  GPI_bus);
    gpio_configure_pin(gpioa, pin_pha3,  GPI_bus);
    timer_init(&step_timer, POLL_step, NULL);
    timer_set(&step_timer, time_now());
#else
    gpio_configure_pin(gpiob, pin_dir,   GPI_bus);
#endif
    gpio_configure_pin(gpioa, pin_sel0,  GPI_bus);
    gpio_configure_pin(gpiob, pin_wgate, GPI_bus);
    gpio_configure_pin(gpiob, pin_side,  GPI_bus);

    /* PA[15:13], PB[12], PC[11:10], PB[9:1], PA[0] */
    afio->exticr[4-1] = 0x0001;
    afio->exticr[3-1] = 0x2211;
    afio->exticr[2-1] = 0x1111;
    afio->exticr[1-1] = 0x1110;

    if (gotek_enhanced()) {
        gpio_configure_pin(gpioa, pin_sel1,  GPI_bus);
        gpio_configure_pin(gpioa, pin_motor, GPI_bus);
    } else if (has_kc30_header == 2) {
        pin_motor = 12; /* PB12 */
    } else {
        /* This gives us "motor always on" if the pin is not connected. 
         * It is safe enough to pull down even if connected direct to 5v, 
         * will only sink ~0.15mA via the weak internal pulldown. */
        gpio_configure_pin(gpiob, pin_motor, GPI_pull_down);
        exti_route_pb(15); /* Motor = PB15 */
    }

    exti->rtsr = 0xffff;
    exti->ftsr = 0xffff;
    exti->imr = m(pin_wgate) | m(pin_side) | m(pin_sel0);

    gpiob_setreset = (uint32_t)&gpiob->bsrr;
}

/* Fast speculative entry point for SELA-changed IRQ. We assume SELA has 
 * changed to the opposite of what we observed on the previous interrupt. This
 * is always the case unless we missed an edge (fast transitions). 
 * Note that the entirety of the SELA handler is in SRAM (.data) -- not only 
 * is this faster to execute, but allows us to co-locate gpio_out_active for 
 * even faster access in the time-critical speculative entry point. */
__attribute__((naked)) __attribute__((section(".ramfuncs"))) aligned(4)
void IRQ_SELA_changed(void) {
    asm (
        ".global gpio_out_active, gpiob_setreset\n"
        "    ldr  r0, [pc, #8]\n"  /* r0 = gpio_out_active */
        "    ldr  r1, [pc, #12]\n" /* r1 = &gpiob->b[s]rr */
        "    uxth r2, r0\n"        /* r2 = (uint16_t)gpio_out_active */
        "    str  r2, [r1, #0]\n"  /* gpiob->b[s]rr = gpio_out_active */
        "    b.n  _IRQ_SELA_changed\n" /* branch to the main ISR entry point */
        "    nop\n"
        "gpio_out_active: .word 0\n"
        "gpiob_setreset:  .word 0\n" /* gpiob->b[s]rr */
        );
}

static void Amiga_HD_ID(uint32_t _gpio_out_active, uint32_t _gpiob_setreset)
    __attribute__((used)) __attribute__((section(".ramfuncs")));
static void _IRQ_SELA_changed(uint32_t _gpio_out_active)
    __attribute__((used)) __attribute__((section(".ramfuncs")))
    __attribute__((optimize("Ofast")));

/* Intermediate SELA-changed handler for generating the Amiga HD RDY signal. */
static void Amiga_HD_ID(uint32_t _gpio_out_active, uint32_t _gpiob_setreset)
{
    /* If deasserting the bus, toggle pin 34 for next time we take the bus. */
    if ((uint8_t)_gpiob_setreset == (uint8_t)(uint32_t)&gpiob->bsrr)
        gpio_out_active ^= m(pin_34);

    /* Continue to the main SELA-changed IRQ entry point. */
    _IRQ_SELA_changed(_gpio_out_active);
}

/* Main entry point for SELA-changed IRQ. This fixes up GPIO pins if we 
 * mis-speculated, also handles the timer-driver RDATA pin, and sets up the 
 * speculative entry point for the next interrupt. */
static void _IRQ_SELA_changed(uint32_t _gpio_out_active)
{
    /* Latch SELA. */
    exti->pr = m(pin_sel0);
    drive.sel = !(gpioa->idr & m(pin_sel0));

    if (drive.sel) {
        /* SELA is asserted (this drive is selected). 
         * Immediately re-enable all our asserted outputs. */
        gpiob->brr = _gpio_out_active & 0xffff;
        gpioa->brr = _gpio_out_active >> 16;
        /* Set pin_rdata as timer output (AFO_bus). */
        if (_gpio_out_active & m(GPIO_OUT_DMA_RD_ACTIVE))
            change_pin_mode(gpio_data, pin_rdata, AFO_bus);
        /* Speculate that, on next interrupt, SELA is deasserted. */
        *(uint8_t *)&gpiob_setreset = (uint8_t)(uint32_t)&gpiob->bsrr;
    } else {
        /* SELA is deasserted (this drive is not selected).
         * Relinquish the bus by disabling all our asserted outputs. */
        gpiob->bsrr = _gpio_out_active & 0xffff;
        gpioa->bsrr = _gpio_out_active >> 16;
        /* Set pin_rdata as quiescent (GPO_bus). */
        change_pin_mode(gpio_data, pin_rdata, GPO_bus);
        /* Speculate that, on next interrupt, SELA is asserted. */
        *(uint8_t *)&gpiob_setreset = (uint8_t)(uint32_t)&gpiob->brr;
    }
}

/* Update the SELA handler. Used for switching in the Amiga HD-ID "magic". 
 * Must be called with interrupts disabled. */
static void update_SELA_irq(bool_t amiga_hd_id)
{
#define OFF 4
    uint32_t handler = amiga_hd_id ? (uint32_t)Amiga_HD_ID
        : (uint32_t)_IRQ_SELA_changed;
    uint32_t entry = (uint32_t)IRQ_SELA_changed;
    uint16_t opcode;

    /* Strip the Thumb LSB from the function addresses. */
    handler &= ~1;
    entry &= ~1;

    /* Create a new tail-call instruction for the entry stub. */
    opcode = handler - (entry + OFF*2 + 4);
    opcode = 0xe000 | (opcode >> 1);

    /* If the tail-call instruction has changed, modify the entry stub. */
    if (unlikely(((uint16_t *)entry)[OFF] != opcode)) {
        ((uint16_t *)entry)[OFF] = opcode;
        cpu_sync(); /* synchronise self-modifying code */
    }
#undef OFF
}

#if defined(APPLE2)

static void POLL_step(void *unused)
{
    static unsigned int _pha;

    struct drive *drv = &drive;
    uint16_t idr_a, idr_b;
    unsigned int pha;

    /* Latch inputs. */
    idr_a = gpioa->idr;
    idr_b = gpiob->idr;

    /* Bail if drive not selected. */
    if (idr_a & m(pin_sel0)) {
        _pha = 0;
        goto out;
    }

    /* Debounce the phase signals. */
    pha = _pha;
    _pha = ((idr_a >> pin_pha0) & 1)
        | ((idr_a >> (pin_pha1-1)) & 2)
        | ((idr_b << (2-pin_pha2)) & 4)
        | ((idr_a << (3-pin_pha3)) & 8);
    pha &= _pha;

    /* Do nothing while we're mid-step. */
    if (drv->step.state & STEP_active)
        goto out;

    /* Rotate the phase bitmap so that the current phase is at bit 0. Note 
     * that the current phase is directly related to the current cylinder. */
    pha = ((pha | (pha << 4)) >> (drv->cyl & 3)) & 0xf;

    /* Conditions to action a head step:
     *  (1) Only one phase is asserted;
     *  (2) That phase is adjacent to the current phase;
     *  (3) We haven't hit a cylinder hard limit. */
    switch (pha) {
    case m(1): /* Phase +1 only */
        if (drv->cyl == ff_cfg.max_cyl)
            goto out;
        drv->step.inward = TRUE;
        break;
    case m(3): /* Phase -1 only */
        if (drv->cyl == 0)
            goto out;
        drv->step.inward = FALSE;
        break;
    default:
        goto out;
    }

    /* Action a head step. */
    if (dma_rd != NULL)
        rdata_stop();
    if (dma_wr != NULL)
        wdata_stop();
    drv->step.start = time_now();
    drv->step.state = STEP_started;
    IRQx_set_pending(FLOPPY_SOFTIRQ);

out:
    timer_set(&step_timer, step_timer.deadline + time_ms(1));
}

#else

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

    /* Latch inputs. */
    idr_a = gpioa->idr;
    idr_b = gpiob->idr;

    /* Clear STEP-changed flag. */
    (void)tim2->ccr2;

    /* Bail if drive not selected. */
    if (idr_a & m(pin_sel0))
        return;

    /* Deassert DSKCHG if a disk is inserted. */
    if ((drv->outp & m(outp_dskchg)) && drv->inserted
        && (ff_cfg.chgrst == CHGRST_step))
        drive_change_output(drv, outp_dskchg, FALSE);

    /* Do we accept this STEP command? */
    if ((drv->step.state & STEP_active) /* Already mid-step? */
        || drive_is_writing())   /* Write in progress? */
        return;

    /* Latch the step direction and check bounds (0 <= cyl <= 255). */
    drv->step.inward = !(idr_b & m(pin_dir));
    if (drv->cyl == (drv->step.inward ? ff_cfg.max_cyl : 0))
        return;

    /* Valid step request for this drive: start the step operation. */
    drv->step.start = time_now();
    drv->step.state = STEP_started;
    if (drv->outp & m(outp_trk0))
        drive_change_output(drv, outp_trk0, FALSE);
    if (dma_rd != NULL) {
        rdata_stop();
        if (!ff_cfg.index_suppression
                && ff_cfg.track_change != TRKCHG_realtime) {
            /* Opportunistically insert an INDEX pulse ahead of seek op. */
            drive_change_output(drv, outp_index, TRUE);
            index.fake_fired = TRUE;
        }
    }
    IRQx_set_pending(FLOPPY_SOFTIRQ);
}

#endif

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
    if ((dma_rd != NULL) && (drv->image->nr_sides == 2))
        rdata_stop();
}

static void IRQ_WGATE(void)
{
    struct drive *drv = &drive;

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

static void IRQ_WGATE_rotary(void)
{
    uint32_t rot_mask = board_rotary_exti_mask, pr = exti->pr;

    /* Latch and clear PR[9:5] and PR[1]. */
    exti->pr = pr & 0x03e2;

    if (pr & m(pin_wgate))
        IRQ_WGATE();

    if (pr & rot_mask)
        IRQ_rotary();
}

static void IRQ_MOTOR(struct drive *drv)
{
    GPIO gpio = gotek_enhanced() ? gpioa : gpiob;
    bool_t mtr_asserted = !(gpio->idr & m(pin_motor));

    if (drv->amiga_pin34 && (ff_cfg.motor_delay != MOTOR_ignore)) {
        IRQ_global_disable();
        drive_change_pin(drv, pin_34, !mtr_asserted);
    }

    timer_cancel(&drv->motor.timer);
    drv->motor.on = FALSE;

    if (!drv->inserted) {
        /* No disk inserted -- MOTOR OFF */
        drive_change_output(drv, outp_rdy, FALSE);
    } else if (ff_cfg.motor_delay == MOTOR_ignore) {
        /* Motor signal ignored -- MOTOR ON */
        drv->motor.on = TRUE;
        drive_change_output(drv, outp_rdy, TRUE);
    } else if (!mtr_asserted) {
        /* Motor signal off -- MOTOR OFF */
        drive_change_output(drv, outp_rdy, FALSE);
    } else {
        /* Motor signal on -- MOTOR SPINNING UP */
        timer_set(&drv->motor.timer,
                  time_now() + time_ms(ff_cfg.motor_delay * 10));
    }
}

static void IRQ_CHGRST(struct drive *drv)
{
    if ((ff_cfg.chgrst == CHGRST_pa14)
        && (gpio_read_pin(gpioa, pin_chgrst) == O_TRUE)
        && drv->inserted) {
        drive_change_output(drv, outp_dskchg, FALSE);
    }
}

#if WDATA_TOGGLE
static void IRQ_wdata_capture(void)
{
    /* Clear WDATA-captured flag. */
    (void)tim_wdata->ccr1;

    /* Toggle polarity to capture the next edge. */
    tim_wdata->ccer ^= TIM_CCER_CC1P;
}
#endif

static void IRQ_MOTOR_CHGRST_rotary(void)
{
    struct drive *drv = &drive;
    bool_t changed = drv->motor.changed;
    uint32_t rot_mask = board_rotary_exti_mask, pr = exti->pr;

    drv->motor.changed = FALSE;

    /* Latch and clear PR[15:10] */
    exti->pr = pr & 0xfc00;

    if (((pr & m(pin_motor)) && (ff_cfg.motor_delay != MOTOR_ignore))
        || changed)
        IRQ_MOTOR(drv);

    if ((pr & m(pin_chgrst))
        || changed)
        IRQ_CHGRST(drv);

    if (pr & rot_mask)
        IRQ_rotary();
}

static void motor_chgrst_update_status(struct drive *drv)
{
    drv->motor.changed = TRUE;
    barrier();
    IRQx_set_pending(MOTOR_CHGRST_IRQ);
}

uint32_t motor_chgrst_exti_mask;
void motor_chgrst_setup_exti(void)
{
    uint32_t m = 0;

    if (ff_cfg.motor_delay != MOTOR_ignore) {
        _exti_route(gotek_enhanced()?0/*PA*/:1/*PB*/, pin_motor);
        m |= m(pin_motor);
    }

    if (ff_cfg.chgrst == CHGRST_pa14) {
        exti_route_pa(pin_chgrst);
        m |= m(pin_chgrst);
    }

    motor_chgrst_exti_mask = m;
    exti->imr |= m;

    motor_chgrst_update_status(&drive);
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

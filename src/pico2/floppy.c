/*
 * pico2/floppy.c
 *
 * RP2350 (Pico 2)-specific floppy-interface setup.
 *
 * The flux engine runs on PIO0 at clk_sys=144MHz, two PIO cycles per
 * SAMPLECLK (72MHz) tick:
 *  - SM0 generates RDATA: DMA feeds 16-bit flux intervals from
 *    dma_rd->buf into the TX FIFO; the SM emits a fixed-width low pulse
 *    then counts out the remainder of the interval.
 *  - SM1 captures WDATA: a free-running 16-bit "timestamp" counter is
 *    sampled at each falling edge and pushed to the RX FIFO, which DMA
 *    drains into dma_wr->buf. This mimics the STM32 input-capture DMA.
 *
 * Bus outputs are driven push-pull at 3.3V while selected, as the Gotek
 * drives them: asserted lines low, everything else actively high.
 * Deselect releases the whole bus with a single SIO OE_CLR write; pins
 * whose bus line is unmapped in the current interface mode stay inputs.
 *
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

#define O_FALSE 1
#define O_TRUE  0

/*
 * Pin assignment (physical GPIO numbers). GPIOs 2-14 carry the thirteen
 * signal lines of the 34-way floppy bus *in bus-pin order*, so a ribbon
 * header wires straight across with no crossovers:
 *
 *   GPIO   2   3   4   5   6   7   8   9  10  11  12  13  14
 *   Bus    2   8  12  16  18  20  22  24  26  28  30  32  34
 *
 * (Bus pins 16..34 are simply GPIO*2+6; the odd bus pins are ground.)
 */

/* Input pins. */
#define pin_sel0    4 /* Bus 12 */
#define pin_motor   5 /* Bus 16 */
#define pin_dir     6 /* Bus 18 */
#define pin_step    7 /* Bus 20 */
#define pin_wgate   9 /* Bus 24 */
#define pin_side   13 /* Bus 32 */

/* Output pins. */
#define pin_02     2  /* Bus 2: DSKCHG/HDEN */
#define pin_08     3  /* Bus 8: INDEX */
#define pin_26     10 /* Bus 26: TRK0 */
#define pin_28     11 /* Bus 28: WRPROT */
#define pin_34     14 /* Bus 34: RDY */

/* All OE-managed bus outputs (excludes the PIO-driven RDATA pin). */
#define FLOPPY_OUT_MASK (m(pin_02) | m(pin_08) | m(pin_26) \
                         | m(pin_28) | m(pin_34))

#define gpio_data gpioa /* ignored on RP2350 */

#define pin_wdata   8  /* Bus 22: PIO0 SM1 input */
#define pin_rdata   12 /* Bus 30: PIO0 SM0 output */

/* DMA channels and their interrupt lines. */
#define dma_rdata   (dma->ch[0])
#define dma_wdata   (dma->ch[1])
#define dma_rdata_irq DMA_IRQ_0
#define dma_wdata_irq DMA_IRQ_1
DEFINE_IRQ(dma_rdata_irq, "IRQ_rdata_dma");
DEFINE_IRQ(dma_wdata_irq, "IRQ_wdata_dma");

/* All floppy-bus GPIO events arrive on the single IO_BANK0 interrupt;
 * lower-priority MOTOR/rotary work is punted to a software IRQ so that
 * timer operations stay serialised at TIMER_IRQ_PRI. */
#define FLOPPY_GPIO_IRQ IO_IRQ_BANK0
#define MOTOR_CHGRST_IRQ SOFTIRQ_2
DEFINE_IRQ(FLOPPY_GPIO_IRQ, "IRQ_io_bank0");
DEFINE_IRQ(MOTOR_CHGRST_IRQ, "IRQ_MOTOR_CHGRST_rotary");

static const struct exti_irq exti_irqs[] = {
    { FLOPPY_GPIO_IRQ, FLOPPY_IRQ_SEL_PRI, 0 },
    { MOTOR_CHGRST_IRQ, TIMER_IRQ_PRI, 0 }
};

/* Subset of output pins which are active (O_TRUE). */
static uint32_t gpio_out_active;

/* Output pins we actually drive: set from the interface mode. Unmapped
 * pins stay inputs -- bus pin 2 is DENSEL on a PC cable, driven by the
 * controller, and driving against it would contend. */
static uint32_t gpio_out_driven;

/* Reconfigure the output pins for a new driven set. Caller must have IRQs
 * disabled: this races the SELA handler otherwise. */
static void board_floppy_set_driven(uint32_t mask)
{
    static const uint8_t pins[] = { pin_02, pin_08, pin_26, pin_28, pin_34 };
    unsigned int i;

    gpio_out_driven = mask;
    for (i = 0; i < ARRAY_SIZE(pins); i++) {
        if (m(pins[i]) & mask)
            gpio_configure_pin(gpioa, pins[i], GPO_bus);
        else
            gpio_configure_pin(gpioa, pins[i], GPI_pull_up);
    }
    /* Leave the bus consistent with the current select state. */
    if (drive.sel) {
        sio->gpio_out_clr = gpio_out_active & mask;
        sio->gpio_out_set = ~gpio_out_active & mask;
        sio->gpio_oe_set = mask;
    } else {
        sio->gpio_oe_clr = mask;
    }
}

/* Is the RDATA read stream active? Read by the SELA handler. */
static volatile uint8_t rdata_active;
#define dma_rd_set_active(x) (rdata_active = (x))

/* Toggle pin 34 on each deselect? (Amiga HD-ID "magic".) */
static bool_t sela_amiga_hd_id;

/* Precomputed IO_BANK0 CTRL values for the RDATA pin. */
#define rdata_ctrl_pio GPIO_FUNC_PIO0
#define rdata_ctrl_sio GPIO_FUNC_SIO

/* Latched flag: MOTOR pin changed (processed at TIMER_IRQ_PRI). */
static volatile uint8_t motor_pin_event;

uint32_t motor_chgrst_exti_mask;

/*
 * PIO0 program memory layout:
 *
 * RDATA generator (SM0), 2 cycles per SAMPLECLK tick, wrap 0..4:
 *   0: out x, 16           ; next interval (stalls if FIFO dry)
 *   1: set pins, 0 [31]    ; assert RDATA (drive low)...
 *   2: nop [23]            ; ...56-cycle (389ns) pulse
 *   3: set pins, 1         ; deassert (drive high: push-pull)
 *   4: jmp x-- 4 [1]       ; burn 2 cycles per remaining tick
 * Period = 60 + 2*X cycles = (30 + X) ticks: producer entries are biased
 * by FLUX_LEAD relative to the STM32 (ARR+1) encoding.
 *
 * WDATA capture (SM1), free-running down-counter in X:
 *   8: mov x, ~null        ; init
 *   9: jmp x-- 10          ; \ 2-cycle count loop
 *  10: jmp pin 9           ; / while WDATA high
 *  11: mov isr, ~x         ; falling edge: timestamp (up-count)
 *  12: push noblock
 *  13: jmp pin 9           ; \ 2-cycle count loop
 *  14: jmp x-- 13          ; / while WDATA low
 *  15: jmp 13              ; (X wrap fall-through)
 */
#define RDATA_PROG_ORIGIN 0
#define WDATA_PROG_ORIGIN 8
static const uint16_t pio_flux_prog[] = {
    /*  0 */ 0x6030, /* out x, 16 */
    /*  1 */ 0xff00, /* set pins, 0 [31] */
    /*  2 */ 0xb742, /* nop [23] */
    /*  3 */ 0xe001, /* set pins, 1 */
    /*  4 */ 0x0144, /* jmp x-- 4 [1] */
    /*  5 */ 0x0000, /* (unused) */
    /*  6 */ 0x0000, /* (unused) */
    /*  7 */ 0x0000, /* (unused) */
    /*  8 */ 0xa02b, /* mov x, ~null */
    /*  9 */ 0x004a, /* jmp x-- 10 */
    /* 10 */ 0x00c9, /* jmp pin 9 */
    /* 11 */ 0xa0c9, /* mov isr, ~x */
    /* 12 */ 0x8000, /* push noblock */
    /* 13 */ 0x00c9, /* jmp pin 9 */
    /* 14 */ 0x004d, /* jmp x-- 13 */
    /* 15 */ 0x000d, /* jmp 13 */
};

/* Fixed per-interval overhead of the RDATA PIO program, in SAMPLECLK
 * ticks. Producer entries represent (value + FLUX_LEAD) ticks; the STM32
 * timer encoding is (value + 1). flux_adjust() converts. */
#define FLUX_LEAD 30
#define DMA_RD_TICKS(x) ((uint32_t)(x) + FLUX_LEAD)

static void flux_adjust(uint16_t *buf, unsigned int nr)
{
    /* Rebias flux intervals from the generic (x+1)-ticks encoding to
     * this PIO program's (x+FLUX_LEAD)-ticks encoding. */
    while (nr--) {
        uint16_t x = *buf;
        *buf++ = (x > (FLUX_LEAD-1)) ? x - (FLUX_LEAD-1) : 0;
    }
}

/* DMA engine positions, as sample counts into the rings (analogous to
 * the STM32 "SIZE - CNDTR" calculation). */
#define dma_rdata_pos()                                                 \
    ((uint16_t)((dma_rdata.read_addr - (uint32_t)dma_rd->buf) / 2)      \
     & (ARRAY_SIZE(dma_rd->buf) - 1))
#define dma_wdata_pos()                                                 \
    ((uint16_t)((dma_wdata.write_addr - (uint32_t)dma_wr->buf) / 2)     \
     & (ARRAY_SIZE(dma_wr->buf) - 1))

/* Ticks remaining in the currently-playing sample: not cheaply observable
 * on PIO (the SM scratch registers are write-only from the CPU). The only
 * consumer tolerates a one-sample error in index alignment. */
#define rdata_ticks_remaining() 0

#define flux_dma_ack_rdata() (dma->ints0 = m(0))
#define flux_dma_ack_wdata() (dma->ints1 = m(1))

static bool_t rdata_dma_en, wdata_dma_en;

static void pio_sm_reset(unsigned int sm, unsigned int pc, bool_t drain)
{
    RP_CLR(&pio0->ctrl) = PIO_CTRL_SM_ENABLE(sm);
    if (drain) {
        /* Toggling FJOIN clears both FIFOs. */
        RP_XOR(&pio0->sm[sm].shiftctrl) = PIO_SHIFTCTRL_FJOIN_RX;
        RP_XOR(&pio0->sm[sm].shiftctrl) = PIO_SHIFTCTRL_FJOIN_RX;
    }
    RP_SET(&pio0->ctrl) =
        PIO_CTRL_SM_RESTART(sm) | PIO_CTRL_CLKDIV_RESTART(sm);
    pio0->sm[sm].instr = pc; /* jmp pc */
}

/*
 * Halt a flux DMA channel and wait for it to go idle.
 *
 * A stream must never be restarted with its channel still live, because
 * pio_sm_reset() clears the state machine's FIFO: emptying a FIFO underneath
 * a DREQ-paced channel leaves the channel's outstanding-request counter out
 * of step with the FIFO, and it then sits EN=1/BUSY=1 with a full transfer
 * count and never moves another byte. Aborting here also freezes the
 * channel's position, so the dma_*_pos() reading that the caller snapshots
 * next is exact rather than up to a FIFO-depth ahead.
 *
 * Called from IRQ context: the spin is a few cycles (one 16-bit transfer at
 * most), and bounded regardless.
 */
static void flux_dma_abort(unsigned int ch)
{
    unsigned int i;
    dma->chan_abort = m(ch);
    for (i = 0; i < 1000; i++) {
        if (!(dma->ch[ch].ctrl_trig & DMA_CTRL_BUSY))
            break;
        cpu_relax();
    }
}

static void rdata_hw_start(void)
{
    pio_sm_reset(0, RDATA_PROG_ORIGIN, TRUE);
    /* Known pin state before enabling: idle high, driver on. */
    pio0->sm[0].instr = 0xe001; /* set pins, 1 */
    pio0->sm[0].instr = 0xe081; /* set pindirs, 1 */
    /* Re-arm the channel. It resumes at its current read_addr, which is the
     * position dma_rd->cons was snapped to when the stream stopped. */
    dma_rdata.trans_count = DMA_TRANS_MODE_TRIGGER_SELF
        | (ARRAY_SIZE(dma_rd->buf) / 2);
    rdata_dma_en = TRUE;
    dma_rdata.ctrl_trig = dma_rdata.al1_ctrl | DMA_CTRL_EN;
    RP_SET(&pio0->ctrl) = PIO_CTRL_SM_ENABLE(0);
}

static void rdata_hw_stop(void)
{
    RP_CLR(&pio0->ctrl) = PIO_CTRL_SM_ENABLE(0);
    if (rdata_dma_en) {
        rdata_dma_en = FALSE;
        flux_dma_abort(0);
        flux_dma_ack_rdata();
    }
}

static void wdata_hw_start(void)
{
    pio_sm_reset(1, WDATA_PROG_ORIGIN, TRUE);
    /* Re-arm the channel; it continues writing where it left off, which is
     * the ring position recorded as the previous write's dma_end. */
    dma_wdata.trans_count = DMA_TRANS_MODE_TRIGGER_SELF
        | (ARRAY_SIZE(dma_wr->buf) / 2);
    wdata_dma_en = TRUE;
    dma_wdata.ctrl_trig = dma_wdata.al1_ctrl | DMA_CTRL_EN;
    RP_SET(&pio0->ctrl) = PIO_CTRL_SM_ENABLE(1);
}

static void wdata_hw_stop(void)
{
    unsigned int i;
    RP_CLR(&pio0->ctrl) = PIO_CTRL_SM_ENABLE(1);
    /* Let DMA drain the RX FIFO of any tail-end samples (fast, bounded). */
    for (i = 0; i < 1000; i++) {
        if (!((pio0->flevel >> 12) & 0xf)) /* SM1 RX level */
            break;
        cpu_relax();
    }
    if (wdata_dma_en) {
        wdata_dma_en = FALSE;
        flux_dma_abort(1);
        flux_dma_ack_wdata();
    }
}

static void flux_dma_disable(void)
{
    dma->chan_abort = m(0) | m(1);
    while ((dma_rdata.ctrl_trig | dma_wdata.ctrl_trig) & DMA_CTRL_BUSY)
        cpu_relax();
    dma_rdata.al1_ctrl = 0;
    dma_wdata.al1_ctrl = 0;
    rdata_dma_en = wdata_dma_en = FALSE;
    RP_CLR(&pio0->ctrl) = PIO_CTRL_SM_ENABLE(0) | PIO_CTRL_SM_ENABLE(1);
}

/* Initialise PIO and DMA for RDATA/WDATA (called at image mount). */
static void timer_dma_hw_init(void)
{
    unsigned int i;

    /* Ring buffers must be aligned for the DMA address wrap. */
    ASSERT(!((uint32_t)dma_rd->buf & (sizeof(dma_rd->buf)-1)));
    ASSERT(!((uint32_t)dma_wr->buf & (sizeof(dma_wr->buf)-1)));

    /* Enable DMA interrupts. */
    flux_dma_ack_rdata();
    flux_dma_ack_wdata();
    IRQx_set_prio(dma_rdata_irq, RDATA_IRQ_PRI);
    IRQx_set_prio(dma_wdata_irq, WDATA_IRQ_PRI);
    IRQx_enable(dma_rdata_irq);
    IRQx_enable(dma_wdata_irq);
    dma->inte0 = m(0); /* rdata channel -> DMA_IRQ_0 */
    dma->inte1 = m(1); /* wdata channel -> DMA_IRQ_1 */

    /* Load the PIO programs. */
    pio0->gpiobase = 0;
    for (i = 0; i < ARRAY_SIZE(pio_flux_prog); i++)
        pio0->instr_mem[i] = pio_flux_prog[i];

    /* SM0: RDATA generator. */
    pio0->sm[0].clkdiv = 1u << 16;
    pio0->sm[0].execctrl = PIO_EXECCTRL_WRAP_BOTTOM(RDATA_PROG_ORIGIN)
        | PIO_EXECCTRL_WRAP_TOP(RDATA_PROG_ORIGIN+4);
    pio0->sm[0].shiftctrl = PIO_SHIFTCTRL_AUTOPULL
        | PIO_SHIFTCTRL_OUT_SHIFTDIR_R
        | PIO_SHIFTCTRL_PULL_THRESH(16)
        | PIO_SHIFTCTRL_FJOIN_TX;
    pio0->sm[0].pinctrl = PIO_PINCTRL_SET_BASE(pin_rdata)
        | PIO_PINCTRL_SET_COUNT(1);
    /* Idle high with the output driver on: push-pull. */
    pio_sm_reset(0, RDATA_PROG_ORIGIN, TRUE);
    pio0->sm[0].instr = 0xe001; /* set pins, 1 */
    pio0->sm[0].instr = 0xe081; /* set pindirs, 1 */

    /* SM1: WDATA capture. */
    pio0->sm[1].clkdiv = 1u << 16;
    pio0->sm[1].execctrl = PIO_EXECCTRL_WRAP_BOTTOM(0)
        | PIO_EXECCTRL_WRAP_TOP(31)
        | PIO_EXECCTRL_JMP_PIN(pin_wdata);
    pio0->sm[1].shiftctrl = PIO_SHIFTCTRL_FJOIN_RX;
    pio0->sm[1].pinctrl = PIO_PINCTRL_IN_BASE(pin_wdata);
    pio_sm_reset(1, WDATA_PROG_ORIGIN, TRUE);

    /* DMA channel 0: dma_rd->buf ring -> SM0 TX FIFO. Self-retriggering
     * with an IRQ per half-ring, like the STM32 half/full-transfer IRQs.
     * Left disabled until rdata_hw_start(): the ring is still empty. */
    dma_rdata.read_addr = (uint32_t)dma_rd->buf;
    dma_rdata.write_addr = (uint32_t)&pio0->txf[0];
    dma_rdata.trans_count = DMA_TRANS_MODE_TRIGGER_SELF
        | (ARRAY_SIZE(dma_rd->buf) / 2);
    dma_rdata.al1_ctrl = DMA_CTRL_SIZE_HALF | DMA_CTRL_INCR_READ
        | DMA_CTRL_RING_SIZE(11) /* 2048-byte read ring */
        | DMA_CTRL_CHAIN_TO(0) /* no chain */
        | DMA_CTRL_TREQ_SEL(DREQ_PIO0_TX0);

    /* DMA channel 1: SM1 RX FIFO -> dma_wr->buf ring. */
    dma_wdata.read_addr = (uint32_t)&pio0->rxf[1];
    dma_wdata.write_addr = (uint32_t)dma_wr->buf;
    dma_wdata.trans_count = DMA_TRANS_MODE_TRIGGER_SELF
        | (ARRAY_SIZE(dma_wr->buf) / 2);
    dma_wdata.al1_ctrl = DMA_CTRL_SIZE_HALF | DMA_CTRL_INCR_WRITE
        | DMA_CTRL_RING_SIZE(11) | DMA_CTRL_RING_SEL_WR
        | DMA_CTRL_CHAIN_TO(1) /* no chain */
        | DMA_CTRL_TREQ_SEL(DREQ_PIO0_RX1);

    rdata_dma_en = wdata_dma_en = FALSE;
}

bool_t floppy_ribbon_is_reversed(void)
{
    time_t t_start = time_now();

    /* If the ribbon is reversed then most/all inputs are grounded.
     * Check four inputs which are supposed only to pulse. */
    while (!(sio->gpio_in
             & (m(pin_sel0) | m(pin_step) | m(pin_wdata) | m(pin_wgate)))) {
        /* If all four inputs are LOW for a full second, conclude that
         * the ribbon is reversed. */
        if (time_since(t_start) > time_ms(1000))
            return TRUE;
    }

    return FALSE;
}

static void board_floppy_init(void)
{
    /* Inputs, in bus-pin order. */
    gpio_configure_pin(gpioa, pin_sel0,  GPI_bus);
    gpio_configure_pin(gpiob, pin_motor, GPI_pull_down);
    gpio_configure_pin(gpiob, pin_dir,   GPI_bus);
    gpio_configure_pin(gpioa, pin_step,  GPI_bus);
    gpio_configure_pin(gpio_data, pin_wdata, GPI_bus);
    gpio_configure_pin(gpiob, pin_wgate, GPI_bus);
    gpio_configure_pin(gpiob, pin_side,  GPI_bus);

    /* RDATA: SIO function, output latch low, high-impedance. */
    gpio_configure_pin(gpio_data, pin_rdata, GPO_rdata);

    /* GPIO edge interrupts. STEP is sampled on its trailing (rising)
     * edge; the other inputs act on both edges. */
    gpio_irq_enable(pin_sel0,  GPIO_IRQ_EDGE_LOW | GPIO_IRQ_EDGE_HIGH);
    gpio_irq_enable(pin_step,  GPIO_IRQ_EDGE_HIGH);
    gpio_irq_enable(pin_wgate, GPIO_IRQ_EDGE_LOW | GPIO_IRQ_EDGE_HIGH);
    gpio_irq_enable(pin_side,  GPIO_IRQ_EDGE_LOW | GPIO_IRQ_EDGE_HIGH);
}

/* Update the SELA handler. Used for switching in the Amiga HD-ID "magic".
 * Must be called with interrupts disabled. */
static void update_SELA_irq(bool_t amiga_hd_id)
{
    sela_amiga_hd_id = amiga_hd_id;
}

/* Apply the select/deselect state of the bus. Called on SELA edges and
 * once at start of day to prime the state. */
static void IRQ_SELA_changed(void)
{
    struct drive *drv = &drive;
    bool_t sel = !(sio->gpio_in & m(pin_sel0));

    if (sel == drv->sel)
        return;
    drv->sel = sel;

    /* Light the on-board LED while we are the selected drive. */
    board_set_activity_led(sel);

    if (sel) {
        /* SELA is asserted (this drive is selected). Drive the bus:
         * asserted lines low, the rest actively high. Levels first, then
         * output-enable. RDATA idles high via SIO until the PIO takes it. */
        sio->gpio_out_clr = gpio_out_active & gpio_out_driven;
        sio->gpio_out_set = ~gpio_out_active & gpio_out_driven;
        sio->gpio_oe_set = gpio_out_driven | m(pin_rdata);
        /* Hand RDATA to the PIO if the read stream is active. */
        if (rdata_active)
            io_bank0->gpio[pin_rdata].ctrl = rdata_ctrl_pio;
    } else {
        /* SELA is deasserted (this drive is not selected).
         * Relinquish the bus by disabling all our outputs. */
        sio->gpio_oe_clr = gpio_out_driven | m(pin_rdata);
        io_bank0->gpio[pin_rdata].ctrl = rdata_ctrl_sio;
        /* Amiga HD-ID: toggle pin 34 for next time we take the bus. */
        if (sela_amiga_hd_id)
            gpio_out_active ^= m(pin_34);
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

/*
 * Shortest gap between STEP pulses we will believe, in microseconds.
 *
 * An unbuffered ribbon rings hard enough to put thousands of spurious edges
 * on this line, in bursts tens of microseconds apart. Most are rejected
 * anyway because a step is already in progress, but one that lands after
 * the previous step has settled is indistinguishable from a real one, and
 * costs a cylinder: the host then reads a track it did not ask for and the
 * sector headers no longer match, which presents as an unreadable disk
 * rather than as a seek fault.
 *
 * The floor on real hardware is the NEC765's 1ms minimum step rate, and
 * Amiga/Atari hosts step at 3ms, so half a millisecond discards ringing
 * with a wide margin and cannot swallow a genuine step. Measured from the
 * last edge of any kind, not the last accepted one, so a burst holds itself
 * off until the line goes quiet.
 */
#define STEP_GLITCH_FILTER_US 500

static time_t step_prev_time;

static void IRQ_STEP_changed(void)
{
    struct drive *drv = &drive;
    uint32_t in = sio->gpio_in;
    time_t now = time_now();
    bool_t glitch = time_diff(step_prev_time, now)
        < (int32_t)time_us(STEP_GLITCH_FILTER_US);

    step_prev_time = now;
    if (glitch)
        return;

    /* Bail if drive not selected. */
    if (in & m(pin_sel0))
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
    drv->step.inward = !(in & m(pin_dir));
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

static void IRQ_SIDE_changed(void)
{
    stk_time_t t = stk_now();
    unsigned int filter = stk_us(ff_cfg.side_select_glitch_filter);
    struct drive *drv = &drive;
    uint8_t hd;

    do {
        /* Has SIDE actually changed? */
        hd = !(sio->gpio_in & m(pin_side));
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
    uint32_t in = sio->gpio_in;

    /* If WRPROT line is asserted then we ignore WGATE. */
    if (drv->outp & m(outp_wrprot))
        return;

    if ((in & m(pin_wgate))          /* WGATE off? */
        || (in & m(pin_sel0))) {     /* Not selected? */
        wdata_stop();
    } else {
        rdata_stop();
        wdata_start();
    }
}

/* Test a floppy-bus pin's latched edges in the INTS words read by
 * IRQ_io_bank0(): four status bits per GPIO, GPIOs 0-7 in ints0 and
 * GPIOs 8-15 in ints1. Pin numbers are compile-time constants. */
#define pin_ints(pin) \
    (((pin) < 8 ? ints0 : ints1) & (0xfu << (((pin)&7)*4)))

/* Single dispatcher for all floppy-bus GPIO edges. Runs at
 * FLOPPY_IRQ_SEL_PRI; only handlers which perform no timer operations may
 * be called from here. MOTOR (and rotary) events are latched and punted
 * to MOTOR_CHGRST_IRQ at TIMER_IRQ_PRI. */
static void IRQ_io_bank0(void)
{
    uint32_t ints0 = io_bank0->proc0_ints[0]; /* GPIOs 0-7 */
    uint32_t ints1 = io_bank0->proc0_ints[1]; /* GPIOs 8-15 */
    uint32_t ints3 = io_bank0->proc0_ints[3]; /* GPIOs 24-31 */

    /* Acknowledge the latched edges we are about to process. */
    if (ints0)
        io_bank0->intr[0] = ints0;
    if (ints1)
        io_bank0->intr[1] = ints1;
    if (ints3)
        io_bank0->intr[3] = ints3;

    /* SELA first: it is the most latency-critical. Level-based, so it is
     * also how the primed (software-pended) first interrupt initialises
     * the select state. */
    IRQ_SELA_changed();

    if (pin_ints(pin_wgate))
        IRQ_WGATE();

    if (pin_ints(pin_step))
        IRQ_STEP_changed();

    if (pin_ints(pin_side))
        IRQ_SIDE_changed();

    if (pin_ints(pin_motor)) {
        motor_pin_event = TRUE;
        IRQx_set_pending(MOTOR_CHGRST_IRQ);
    }

    /* Rotary encoder (GPIOs 26-27): punt to low-priority handler. */
    if (ints3)
        IRQx_set_pending(MOTOR_CHGRST_IRQ);
}

static void IRQ_MOTOR(struct drive *drv)
{
    bool_t mtr_asserted = !(sio->gpio_in & m(pin_motor));

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

static void IRQ_MOTOR_CHGRST_rotary(void)
{
    struct drive *drv = &drive;
    bool_t changed = drv->motor.changed;
    bool_t motor_event = motor_pin_event;

    drv->motor.changed = FALSE;
    motor_pin_event = FALSE;

    if ((motor_event && (ff_cfg.motor_delay != MOTOR_ignore)) || changed)
        IRQ_MOTOR(drv);

    /* Note: the CHGRST_pa14 input is not supported on this board. */

    if (board_rotary_exti_mask)
        IRQ_rotary();
}

static void motor_chgrst_update_status(struct drive *drv)
{
    drv->motor.changed = TRUE;
    barrier();
    IRQx_set_pending(MOTOR_CHGRST_IRQ);
}

void motor_chgrst_setup_exti(void)
{
    uint32_t m = 0;

    if (ff_cfg.motor_delay != MOTOR_ignore) {
        gpio_irq_enable(pin_motor, GPIO_IRQ_EDGE_LOW | GPIO_IRQ_EDGE_HIGH);
        m |= m(pin_motor);
    }

    motor_chgrst_exti_mask = m;

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

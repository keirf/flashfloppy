/*
 * floppy.c
 * 
 * Floppy interface control.
 * 
 * Written & released by Keir Fraser <keir.xen@gmail.com>
 * 
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

#define GPI_bus GPI_floating
#define GPO_bus GPO_pushpull(_2MHz,O_FALSE)
#define AFO_bus (AFO_pushpull(_2MHz) | (O_FALSE<<4))

#define m(bitnr) (1u<<(bitnr))

/* A soft IRQ for handling lower priority work items. */
static void drive_step_timer(void *_drv);
void IRQ_43(void) __attribute__((alias("IRQ_soft")));
#define FLOPPY_SOFTIRQ 43

/* A DMA buffer for running a timer associated with a floppy-data I/O pin. */
struct dma_ring {
    /* Current state of DMA (RDATA): 
     *  DMA_inactive: No activity, buffer is empty. 
     *  DMA_starting: Buffer is filling, DMA+timer not yet active.
     *  DMA_active: DMA is active, timer is operational. 
     *  DMA_stopping: DMA+timer halted, buffer waiting to be cleared. 
     * Current state of DMA (WDATA): 
     *  DMA_inactive: No activity, flux ring and bitcell buffer are empty. 
     *  DMA_starting: Flux ring and bitcell buffer are filling.
     *  DMA_active: Writeback processing is active (to mass storage).
     *  DMA_stopping: Timer halted, buffers waiting to be cleared. */
#define DMA_inactive 0 /* -> {starting, active} */
#define DMA_starting 1 /* -> {active, stopping} */
#define DMA_active   2 /* -> {stopping} */
#define DMA_stopping 3 /* -> {inactive} */
    volatile uint8_t state;
    /* IRQ handler sets this if the read buffer runs dry. */
    volatile uint8_t kick_dma_irq;
    /* Indexes into the buf[] ring buffer. */
    uint16_t cons;
    union {
        uint16_t prod; /* dma_rd: our producer index for flux samples */
        uint16_t prev_sample; /* dma_wr: previous CCRx sample value */
    };
    /* DMA ring buffer of timer values (ARR or CCRx). */
    uint16_t buf[1024];
};

/* DMA buffers are permanently allocated while a disk image is loaded, allowing 
 * independent and concurrent management of the RDATA/WDATA pins. */
static struct dma_ring *dma_rd; /* RDATA DMA buffer */
static struct dma_ring *dma_wr; /* WDATA DMA buffer */

/* Statically-allocated floppy drive state. Tracks head movements and 
 * side changes at all times, even when the drive is empty. */
static struct drive {
    uint8_t cyl, head, nr_sides;
    bool_t writing;
    bool_t sel;
    bool_t index_suppressed; /* disable IDX while writing to USB stick */
    uint8_t outp;
    struct {
#define STEP_started  1 /* started by hi-pri IRQ */
#define STEP_latched  2 /* latched by lo-pri IRQ */
#define STEP_active   (STEP_started | STEP_latched)
#define STEP_settling 4 /* handled by step.timer */
        uint8_t state;
        bool_t inward;
        time_t start;
        struct timer timer;
    } step;
    uint32_t restart_pos;
    struct image *image;
} drive;

static struct image *image;
static time_t sync_time, sync_pos;

static struct {
    struct timer timer, timer_deassert;
    time_t prev_time;
    bool_t fake_fired;
} index;
static void index_assert(void *);   /* index.timer */
static void index_deassert(void *); /* index.timer_deassert */

static uint32_t max_read_us;

static void rdata_stop(void);
static void wdata_start(void);
static void wdata_stop(void);

static always_inline void drive_change_output(
    struct drive *drv, uint8_t outp, bool_t assert);

struct exti_irq {
    uint8_t irq, pri;
    uint16_t pr_mask; /* != 0: irq- and exti-pending flags are cleared */
};

#include "gotek/floppy.c"

static uint8_t pin02, pin02_inverted;
static uint8_t pin34, pin34_inverted;
static uint8_t fintf_mode;
const static struct fintf {
    uint8_t pin02:4;
    uint8_t pin34:4;
} fintfs[] = {
    [FINTF_SHUGART] = {
        .pin02 = outp_dskchg,
        .pin34 = outp_rdy },
    [FINTF_IBMPC] = {
        .pin02 = outp_unused,
        .pin34 = outp_dskchg },
    [FINTF_IBMPC_HDOUT] = {
        .pin02 = outp_hden,
        .pin34 = outp_dskchg },
    [FINTF_AKAI_S950] = {
        .pin02 = outp_hden,
        .pin34 = outp_rdy },
    [FINTF_AMIGA] = {
        .pin02 = outp_dskchg,
        .pin34 = outp_unused }
};

static always_inline void drive_change_pin(
    struct drive *drv, uint8_t pin, bool_t assert)
{
    uint16_t pin_mask = m(pin);

    /* Logically assert or deassert the pin. */
    if (assert)
        gpio_out_active |= pin_mask;
    else
        gpio_out_active &= ~pin_mask;

    /* Update the physical output pin, if the drive is selected. */
    if (drv->sel)
        gpio_write_pins(gpio_out, pin_mask, assert ? O_TRUE : O_FALSE);

    /* Caller expects us to re-enable interrupts. */
    IRQ_global_enable();
}

static void _drive_change_output(
    struct drive *drv, uint8_t outp, bool_t assert)
{
    IRQ_global_enable();

    if (pin02 == outp) {
        IRQ_global_disable();
        drive_change_pin(drv, pin_02, assert ^ pin02_inverted);
    }

    if (pin34 == outp) {
        IRQ_global_disable();
        drive_change_pin(drv, pin_34, assert ^ pin34_inverted);
    }
}

static void drive_change_output(
    struct drive *drv, uint8_t outp, bool_t assert)
{
    uint8_t outp_mask = m(outp);
    uint8_t pin;

    IRQ_global_disable();

    /* Logically assert or deassert the output line. */
    if (assert)
        drv->outp |= outp_mask;
    else
        drv->outp &= ~outp_mask;

    switch (outp) {
    case outp_index:  pin = pin_08; break;
    case outp_trk0:   pin = pin_26; break;
    case outp_wrprot: pin = pin_28; break;
    default:
        _drive_change_output(drv, outp, assert);
        return;
    }
    drive_change_pin(drv, pin, assert);
}

static void update_amiga_id(bool_t amiga_hd_id)
{
    /* Only for the Amiga interface, with hacked RDY (pin 34) signal. */
    if (fintf_mode != FINTF_AMIGA)
        return;

    IRQ_global_disable();

    /* If mounting an HD image then we signal to the host by toggling pin 34 
     * every time the drive is selected. */
    update_SELA_irq(amiga_hd_id);

    /* DD-ID: !!HACK!! We permanently assert pin 34, even when no disk is
     * inserted. Properly we should only do this when MTR is asserted. */
    /* HD ID: !!HACK!! Without knowledge of MTR signal we cannot synchronise
     * the HD-ID sequence 101010... with the host poll loop. It turns out that
     * starting with pin 34 asserted when the HD image is mounted seems to
     * generally work! */
    gpio_out_active |= m(pin_34);
    if (drive.sel)
        gpio_write_pins(gpio_out, m(pin_34), O_TRUE);

    IRQ_global_enable();
}

void floppy_cancel(void)
{
    struct drive *drv = &drive;

    /* Initialised? Bail if not. */
    if (!dma_rd)
        return;

    /* Immediately change outputs that we control entirely from the main loop. 
     * Asserting WRPROT prevents any further calls to wdata_start(). */
    drive_change_output(drv, outp_rdy, FALSE);
    drive_change_output(drv, outp_wrprot, TRUE);
    drive_change_output(drv, outp_hden, FALSE);
    update_amiga_id(FALSE);

    /* Stop DMA + timer work. */
    IRQx_disable(dma_rdata_irq);
    IRQx_disable(dma_wdata_irq);
    rdata_stop();
    wdata_stop();
    dma_rdata.ccr = 0;
    dma_wdata.ccr = 0;

    /* Clear soft state. */
    timer_cancel(&index.timer);
    barrier(); /* cancel index.timer /then/ clear soft state */
    drv->index_suppressed = FALSE;
    drv->image = NULL;
    max_read_us = 0;
    image = NULL;
    dma_rd = dma_wr = NULL;
    index.fake_fired = FALSE;
    barrier(); /* clear soft state /then/ cancel index.timer_deassert */
    timer_cancel(&index.timer_deassert);

    /* Set outputs for empty drive. */
    barrier();
    drive_change_output(drv, outp_index, FALSE);
    drive_change_output(drv, outp_dskchg, TRUE);
}

static struct dma_ring *dma_ring_alloc(void)
{
    struct dma_ring *dma = arena_alloc(sizeof(*dma));
    memset(dma, 0, offsetof(struct dma_ring, buf));
    return dma;
}

void floppy_set_fintf_mode(void)
{
    static const char * const fintf_name[] = {
        [FINTF_SHUGART]     = "Shugart",
        [FINTF_IBMPC]       = "IBM PC",
        [FINTF_IBMPC_HDOUT] = "IBM PC + HD_OUT",
        [FINTF_AKAI_S950]   = "Akai S950",
        [FINTF_AMIGA]       = "Amiga"
    };
    static const char *const outp_name[] = {
        [outp_dskchg] = "chg",
        [outp_rdy] = "rdy",
        [outp_hden] = "dens",
        [outp_unused] = "high"
    };
    struct drive *drv = &drive;
    uint32_t old_active;
    uint8_t mode = ff_cfg.interface;

    if (mode == FINTF_JC) {
        /* Jumper JC selects default floppy interface configuration:
         *   - No Jumper: Shugart
         *   - Jumpered:  IBM PC */
        mode = gpio_read_pin(gpiob, 1) ? FINTF_SHUGART : FINTF_IBMPC;
    }

    ASSERT(mode < ARRAY_SIZE(fintfs));

    IRQ_global_disable();

    fintf_mode = mode;
    pin02 = ff_cfg.pin02 ? ff_cfg.pin02 - 1 : fintfs[mode].pin02;
    pin34 = ff_cfg.pin34 ? ff_cfg.pin34 - 1 : fintfs[mode].pin34;
    pin02_inverted = !!(pin02 & PIN_invert);
    pin34_inverted = !!(pin34 & PIN_invert);
    pin02 &= ~PIN_invert;
    pin34 &= ~PIN_invert;

    old_active = gpio_out_active;
    gpio_out_active &= ~(m(pin_02) | m(pin_34));
    if (((drv->outp >> pin02) ^ pin02_inverted) & 1)
        gpio_out_active |= m(pin_02);
    if (((drv->outp >> pin34) ^ pin34_inverted) & 1)
        gpio_out_active |= m(pin_34);

    /* Default handler for IRQ_SELA_changed */
    update_SELA_irq(FALSE);

    if (drv->sel) {
        gpio_write_pins(gpio_out, old_active & ~gpio_out_active, O_FALSE);
        gpio_write_pins(gpio_out, ~old_active & gpio_out_active, O_TRUE);
    }

    IRQ_global_enable();

    /* Default to Amiga-DD identity until HD image is mounted. */
    update_amiga_id(FALSE);

    printk("Interface: %s (pin2=%s%s, pin34=%s%s)\n",
           fintf_name[mode],
           pin02_inverted ? "not-" : "", outp_name[pin02] ?: "?",
           pin34_inverted ? "not-" : "", outp_name[pin34] ?: "?");
}

void floppy_init(void)
{
    struct drive *drv = &drive;
    const struct exti_irq *e;
    unsigned int i;

    floppy_set_fintf_mode();

    board_floppy_init();

    timer_init(&drv->step.timer, drive_step_timer, drv);

    gpio_configure_pin(gpio_out, pin_02, GPO_bus);
    gpio_configure_pin(gpio_out, pin_08, GPO_bus);
    gpio_configure_pin(gpio_out, pin_26, GPO_bus);
    gpio_configure_pin(gpio_out, pin_28, GPO_bus);
    gpio_configure_pin(gpio_out, pin_34, GPO_bus);

    gpio_configure_pin(gpio_data, pin_wdata, GPI_bus);
    gpio_configure_pin(gpio_data, pin_rdata, GPO_bus);

    drive_change_output(drv, outp_dskchg, TRUE);
    drive_change_output(drv, outp_wrprot, TRUE);
    drive_change_output(drv, outp_trk0,   TRUE);

    /* Configure physical interface interrupts. */
    for (i = 0, e = exti_irqs; i < ARRAY_SIZE(exti_irqs); i++, e++) {
        IRQx_set_prio(e->irq, e->pri);
        if (e->pr_mask != 0) {
            /* Do not trigger an initial interrupt on this line. Clear EXTI_PR
             * before IRQ-pending, otherwise IRQ-pending is immediately
             * reasserted. */
            exti->pr = e->pr_mask;
            IRQx_clear_pending(e->irq);
        } else {
            /* Common case: we deliberately trigger the first interrupt to 
             * prime the ISR's state. */
            IRQx_set_pending(e->irq);
        }
    }

    /* Enable physical interface interrupts. */
    for (i = 0, e = exti_irqs; i < ARRAY_SIZE(exti_irqs); i++, e++) {
        IRQx_enable(e->irq);
    }

    IRQx_set_prio(FLOPPY_SOFTIRQ, FLOPPY_SOFTIRQ_PRI);
    IRQx_enable(FLOPPY_SOFTIRQ);

    timer_init(&index.timer, index_assert, NULL);
    timer_init(&index.timer_deassert, index_deassert, NULL);
}

void floppy_insert(unsigned int unit, struct slot *slot)
{
    struct drive *drv = &drive;

    arena_init();

    dma_rd = dma_ring_alloc();
    dma_wr = dma_ring_alloc();

    image = arena_alloc(sizeof(*image));
    memset(image, 0, sizeof(*image));

    /* Large buffer to absorb long write latencies at mass-storage layer. */
    image->bufs.write_bc.len = 16*1024; /* 16kB, power of two. */
    image->bufs.write_bc.p = arena_alloc(image->bufs.write_bc.len);

    /* ~0 avoids sync match within fewer than 32 bits of scan start. */
    image->write_bc_window = ~0;

    /* Smaller buffer for absorbing read latencies at mass-storage layer. */
    image->bufs.read_bc.len = 8*1024; /* 8kB, power of two. */
    image->bufs.read_bc.p = arena_alloc(image->bufs.read_bc.len);

    /* Any remaining space is used for staging I/O to mass storage, shared
     * between read and write paths (Change of use of this memory space is
     * fully serialised). */
    image->bufs.write_data.len = arena_avail();
    image->bufs.write_data.p = arena_alloc(image->bufs.write_data.len);
    image->bufs.read_data = image->bufs.write_data;

    /* Minimum allowable buffer space (assumed by hfe image handler). */
    ASSERT(image->bufs.read_data.len >= 20*1024);

    /* Mount the image file. */
    image_open(image, slot);
    drv->image = image;
    if (!image->handler->write_track || volume_readonly())
        slot->attributes |= AM_RDO;
    if (slot->attributes & AM_RDO) {
        printk("Image is R/O\n");
    } else {
        image_extend(image);
    }

    /* After image is extended at mount time, we permit no further changes 
     * to the file metadata. Clear the dirent info to ensure this. */
    image->fp.dir_ptr = NULL;
    image->fp.dir_sect = 0;

    dma_rd->state = DMA_stopping;

    if (image->write_bc_ticks < sysclk_ns(1500))
        drive_change_output(drv, outp_hden, TRUE);

    drv->index_suppressed = FALSE;
    index.prev_time = time_now();

    /* Enable DMA interrupts. */
    dma1->ifcr = DMA_IFCR_CGIF(dma_rdata_ch) | DMA_IFCR_CGIF(dma_wdata_ch);
    IRQx_set_prio(dma_rdata_irq, RDATA_IRQ_PRI);
    IRQx_set_prio(dma_wdata_irq, WDATA_IRQ_PRI);
    IRQx_enable(dma_rdata_irq);
    IRQx_enable(dma_wdata_irq);

    /* RDATA Timer setup:
     * The counter is incremented at full SYSCLK rate. 
     *  
     * Ch.2 (RDATA) is in PWM mode 1. It outputs O_TRUE for 400ns and then 
     * O_FALSE until the counter reloads. By changing the ARR via DMA we alter
     * the time between (fixed-width) O_TRUE pulses, mimicking floppy drive 
     * timings. */
    tim_rdata->psc = 0;
    tim_rdata->ccmr1 = (TIM_CCMR1_CC2S(TIM_CCS_OUTPUT) |
                        TIM_CCMR1_OC2M(TIM_OCM_PWM1));
    tim_rdata->ccer = TIM_CCER_CC2E | ((O_TRUE==0) ? TIM_CCER_CC2P : 0);
    tim_rdata->ccr2 = sysclk_ns(400);
    tim_rdata->dier = TIM_DIER_UDE;
    tim_rdata->cr2 = 0;

    /* DMA setup: From a circular buffer into the RDATA Timer's ARR. */
    dma_rdata.cpar = (uint32_t)(unsigned long)&tim_rdata->arr;
    dma_rdata.cmar = (uint32_t)(unsigned long)dma_rd->buf;
    dma_rdata.cndtr = ARRAY_SIZE(dma_rd->buf);
    dma_rdata.ccr = (DMA_CCR_PL_HIGH |
                     DMA_CCR_MSIZE_16BIT |
                     DMA_CCR_PSIZE_16BIT |
                     DMA_CCR_MINC |
                     DMA_CCR_CIRC |
                     DMA_CCR_DIR_M2P |
                     DMA_CCR_HTIE |
                     DMA_CCR_TCIE |
                     DMA_CCR_EN);

    /* WDATA Timer setup: 
     * The counter runs from 0x0000-0xFFFF inclusive at full SYSCLK rate.
     *  
     * Ch.1 (WDATA) is in Input Capture mode, sampling on every clock and with
     * no input prescaling or filtering. Samples are captured on the falling 
     * edge of the input (CCxP=1). DMA is used to copy the sample into a ring
     * buffer for batch processing in the DMA-completion ISR. */
    tim_wdata->psc = 0;
    tim_wdata->arr = 0xffff;
    tim_wdata->ccmr1 = TIM_CCMR1_CC1S(TIM_CCS_INPUT_TI1);
    tim_wdata->dier = TIM_DIER_CC1DE;
    tim_wdata->cr2 = 0;

    /* DMA setup: From the WDATA Timer's CCRx into a circular buffer. */
    dma_wdata.cpar = (uint32_t)(unsigned long)&tim_wdata->ccr1;
    dma_wdata.cmar = (uint32_t)(unsigned long)dma_wr->buf;
    dma_wdata.cndtr = ARRAY_SIZE(dma_wr->buf);
    dma_wdata.ccr = (DMA_CCR_PL_HIGH |
                     DMA_CCR_MSIZE_16BIT |
                     DMA_CCR_PSIZE_16BIT |
                     DMA_CCR_MINC |
                     DMA_CCR_CIRC |
                     DMA_CCR_DIR_P2M |
                     DMA_CCR_HTIE |
                     DMA_CCR_TCIE |
                     DMA_CCR_EN);

    /* Drive is ready. Set output signals appropriately. */
    drive_change_output(drv, outp_rdy, TRUE);
    update_amiga_id(image->stk_per_rev > stk_ms(300));
    if (!(slot->attributes & AM_RDO))
        drive_change_output(drv, outp_wrprot, FALSE);
}

static unsigned int drive_calc_track(struct drive *drv)
{
    drv->nr_sides = (drv->cyl >= DA_FIRST_CYL) ? 1 : drv->image->nr_sides;
    return drv->cyl*2 + (drv->head & (drv->nr_sides - 1));
}

/* Find current rotational position for read-stream restart. */
static void drive_set_restart_pos(struct drive *drv)
{
    uint32_t pos = max_t(int32_t, 0, time_diff(index.prev_time, time_now()));
    pos %= drv->image->stk_per_rev;
    drv->restart_pos = pos;
    drv->index_suppressed = TRUE;
}

/* Called from IRQ context to stop the write stream. */
static void wdata_stop(void)
{
    struct write *write;
    struct drive *drv = &drive;
    uint8_t prev_state = dma_wr->state;

    /* Already inactive? Nothing to do. */
    if ((prev_state == DMA_inactive) || (prev_state == DMA_stopping))
        return;

    /* Ok we're now stopping DMA activity. */
    dma_wr->state = DMA_stopping;

    /* Turn off timer. */
    tim_wdata->ccer = 0;
    tim_wdata->cr1 = 0;

    /* Drain out the DMA buffer. */
    IRQx_set_pending(dma_wdata_irq);

    /* Restart read exactly where write ended. 
     * No more IDX pulses until write-out is complete. */
    drive_set_restart_pos(drv);

    /* Remember where this write's DMA stream ended. */
    write = get_write(image, image->wr_prod);
    write->dma_end = ARRAY_SIZE(dma_wr->buf) - dma_wdata.cndtr;
    image->wr_prod++;

    if (!ff_cfg.index_suppression) {
        /* Opportunistically insert an INDEX pulse ahead of writeback. */
        drive_change_output(drv, outp_index, TRUE);
        index.fake_fired = TRUE;
        IRQx_set_pending(FLOPPY_SOFTIRQ);
        /* Position read head so it quickly triggers an INDEX pulse. */
        drv->restart_pos = drv->image->stk_per_rev - stk_ms(20);
    }
}

static void wdata_start(void)
{
    struct write *write;
    uint32_t start_pos;

    switch (dma_wr->state) {
    case DMA_starting:
    case DMA_active:
        /* Already active: ignore WGATE glitch. */
        printk("*** WGATE glitch\n");
        return;
    case DMA_stopping:
        if ((image->wr_prod - image->wr_cons) >= ARRAY_SIZE(image->write)) {
            /* The write pipeline is full. Complain to the log. */
            printk("*** Missed write\n");
            return;
        }
        break;
    case DMA_inactive:
        /* The write path is quiescent and ready to process this new write. */
        break;
    }

    dma_wr->state = DMA_starting;

    /* Start timer. */
    tim_wdata->ccer = TIM_CCER_CC1E | TIM_CCER_CC1P;
    tim_wdata->egr = TIM_EGR_UG;
    tim_wdata->sr = 0; /* dummy write, gives h/w time to process EGR.UG=1 */
    tim_wdata->cr1 = TIM_CR1_CEN;

    /* Find rotational start position of the write, in SYSCLK ticks. */
    start_pos = max_t(int32_t, 0, time_diff(index.prev_time, time_now()));
    start_pos %= drive.image->stk_per_rev;
    start_pos *= SYSCLK_MHZ / STK_MHZ;
    write = get_write(image, image->wr_prod);
    write->start = start_pos;
    write->track = drive_calc_track(&drive);

    /* Allow IDX pulses while handling a write. */
    drive.index_suppressed = FALSE;

    /* Exit head-settling state. Ungates INDEX signal. */
    cmpxchg(&drive.step.state, STEP_settling, 0);
}

/* Called from IRQ context to stop the read stream. */
static void rdata_stop(void)
{
    uint8_t prev_state = dma_rd->state;

    /* Already inactive? Nothing to do. */
    if (prev_state == DMA_inactive)
        return;

    /* Ok we're now stopping DMA activity. */
    dma_rd->state = DMA_stopping;

    /* If DMA was not yet active, don't need to touch peripherals. */
    if (prev_state != DMA_active)
        return;

    /* Turn off the output pin */
    gpio_configure_pin(gpio_data, pin_rdata, GPO_bus);

    /* Turn off timer. */
    tim_rdata->cr1 = 0;

    /* track-change = instant: Restart read stream where we left off. */
    if ((ff_cfg.track_change == TRKCHG_instant)
        && !drive.index_suppressed
        && ff_cfg.index_suppression)
        drive_set_restart_pos(&drive);
}

/* Called from user context to start the read stream. */
static void rdata_start(void)
{
    IRQ_global_disable();

    /* Did we race rdata_stop()? Then bail. */
    if (dma_rd->state == DMA_stopping)
        goto out;

    dma_rd->state = DMA_active;

    /* Start timer. */
    tim_rdata->egr = TIM_EGR_UG;
    tim_rdata->sr = 0; /* dummy write, gives h/w time to process EGR.UG=1 */
    tim_rdata->cr1 = TIM_CR1_CEN;

    /* Enable output. */
    if (drive.sel)
        gpio_configure_pin(gpio_data, pin_rdata, AFO_bus);

    /* Exit head-settling state. Ungates INDEX signal. */
    cmpxchg(&drive.step.state, STEP_settling, 0);

out:
    IRQ_global_enable();
}

static void floppy_sync_flux(void)
{
    const uint16_t buf_mask = ARRAY_SIZE(dma_rd->buf) - 1;
    struct drive *drv = &drive;
    uint16_t nr_to_wrap, nr_to_cons, nr;
    int32_t ticks;

    nr_to_wrap = ARRAY_SIZE(dma_rd->buf) - dma_rd->prod;
    nr_to_cons = (dma_rd->cons - dma_rd->prod - 1) & buf_mask;
    nr = min(nr_to_wrap, nr_to_cons);
    if (nr) {
        dma_rd->prod += image_rdata_flux(
            drv->image, &dma_rd->buf[dma_rd->prod], nr);
        dma_rd->prod &= buf_mask;
    }

    nr = (dma_rd->prod - dma_rd->cons) & buf_mask;
    if (nr < buf_mask)
        return;

    if (!drv->index_suppressed) {
        ticks = time_diff(time_now(), sync_time) - time_us(1);
        if (ticks > time_ms(15)) {
            /* Too long to wait. Immediately re-sync index timing. */
            drv->index_suppressed = TRUE;
            printk("Trk %u: skip %ums\n",
                   drv->image->cur_track, (ticks+time_us(500))/time_ms(1));
        } else if (ticks > time_ms(5)) {
            /* A while to wait. Go do other work. */
            return;
        } else {
            if (ticks > 0)
                delay_ticks(ticks);
            /* If we're out of sync then forcibly re-sync index timing. */
            ticks = time_diff(time_now(), sync_time);
            if (ticks < -100) {
                drv->index_suppressed = TRUE;
                printk("Trk %u: late %uus\n",
                       drv->image->cur_track, -ticks/time_us(1));
            }
        }
    } else if (drv->step.state) {
        /* IDX is suppressed: Wait for heads to settle.
         * When IDX is not suppressed, settle time is already accounted for in
         * dma_rd_handle()'s call to image_setup_track(). */
        time_t step_settle = drv->step.start + time_ms(ff_cfg.head_settle_ms);
        int32_t delta = time_diff(time_now(), step_settle) - time_us(1);
        if (delta > time_ms(5))
            return; /* go do other work for a while */
        if (delta > 0)
            delay_ticks(delta);
    }

    if (drv->index_suppressed) {
        /* Re-enable index timing, snapped to the new read stream. */
        timer_cancel(&index.timer);
        IRQ_global_disable();
        index.prev_time = time_now() - sync_pos;
        drv->index_suppressed = FALSE;
    }

    rdata_start();
}

static void floppy_read_data(struct drive *drv)
{
    uint32_t read_us;
    time_t timestamp;

    /* Read some track data if there is buffer space. */
    timestamp = time_now();
    if (image_read_track(drv->image) && dma_rd->kick_dma_irq) {
        /* We buffered some more data and the DMA handler requested a kick. */
        dma_rd->kick_dma_irq = FALSE;
        IRQx_set_pending(dma_rdata_irq);
    }

    /* Log maximum time taken to read track data, in microseconds. */
    read_us = time_diff(timestamp, time_now()) / TIME_MHZ;
    if (read_us > max_read_us) {
        max_read_us = max_t(uint32_t, max_read_us, read_us);
        printk("New max: read_us=%u\n", max_read_us);
    }
}

static bool_t dma_rd_handle(struct drive *drv)
{
    switch (dma_rd->state) {

    case DMA_inactive: {
        time_t index_time, read_start_pos;
        unsigned int track;
        /* Allow 10ms from current rotational position to load new track */
        int32_t delay = time_ms(10);
        /* Allow extra time if heads are settling. */
        if (drv->step.state & STEP_settling) {
            time_t step_settle = drv->step.start
                + time_ms(ff_cfg.head_settle_ms);
            int32_t delta = time_diff(time_now(), step_settle);
            delay = max_t(int32_t, delta, delay);
        }
        /* No data fetch while stepping. */
        barrier(); /* check STEP_settling /then/ check STEP_active */
        if (drv->step.state & STEP_active)
            break;
        /* Work out where in new track to start reading data from. */
        index_time = index.prev_time;
        read_start_pos = drv->index_suppressed
            ? drive.restart_pos /* start read exactly where write ended */
            : time_since(index_time) + delay;
        read_start_pos %= drv->image->stk_per_rev;
        /* Seek to the new track. */
        track = drive_calc_track(drv);
        read_start_pos *= SYSCLK_MHZ/STK_MHZ;
        if ((track >= (DA_FIRST_CYL*2)) && (drv->outp & m(outp_wrprot))
            && !volume_readonly()) {
            /* Remove write-protect when driven into D-A mode. */
            drive_change_output(drv, outp_wrprot, FALSE);
        }
        if (image_setup_track(drv->image, track, &read_start_pos))
            return TRUE;
        read_start_pos /= SYSCLK_MHZ/STK_MHZ;
        sync_pos = read_start_pos;
        if (!drv->index_suppressed) {
            /* Set the deadline to match existing index timing. */
            sync_time = index_time + read_start_pos;
            if (time_diff(time_now(), sync_time) < 0)
                sync_time += drv->image->stk_per_rev;
        }
        /* Change state /then/ check for race against step or side change. */
        dma_rd->state = DMA_starting;
        barrier();
        if ((drv->step.state & STEP_active)
            || (track != drive_calc_track(drv))
            || (dma_wr->state != DMA_inactive))
            dma_rd->state = DMA_stopping;
        break;
    }

    case DMA_starting:
        floppy_sync_flux();
        /* fall through */

    case DMA_active:
        floppy_read_data(drv);
        break;

    case DMA_stopping:
        dma_rd->state = DMA_inactive;
        /* Reinitialise the circular buffer to empty. */
        dma_rd->cons = dma_rd->prod =
            ARRAY_SIZE(dma_rd->buf) - dma_rdata.cndtr;
        /* Free-running index timer. */
        timer_cancel(&index.timer);
        timer_set(&index.timer, index.prev_time + drv->image->stk_per_rev);
        break;
    }

    return FALSE;
}

static bool_t dma_wr_handle(struct drive *drv)
{
    struct image *im = drv->image;
    struct write *write = get_write(im, im->wr_cons);
    bool_t completed;

    ASSERT((dma_wr->state == DMA_starting) || (dma_wr->state == DMA_stopping));

    /* Start a write. */
    if (!drv->writing) {

        /* Bail out of read mode. */
        if (dma_rd->state != DMA_inactive) {
            ASSERT(dma_rd->state == DMA_stopping);
            if (dma_rd_handle(drv))
                return TRUE;
            ASSERT(dma_rd->state == DMA_inactive);
        }
    
        /* Set up the track for writing. */
        if (image_setup_track(im, write->track, NULL))
            return TRUE;

        drv->writing = TRUE;

    }

    /* Continue a write. */
    completed = image_write_track(im);

    /* Is this write now completely processed? */
    if (completed) {

        /* Clear the staging buffer. */
        im->bufs.write_data.cons = 0;
        im->bufs.write_data.prod = 0;

        /* Align the bitcell consumer index for start of next write. */
        im->bufs.write_bc.cons = (write->bc_end + 31) & ~31;

        /* Sync back to mass storage. */
        F_sync(&im->fp);

        /* Consume the write from the pipeline buffer. If the buffer is 
         * empty then return to read operation. */
        IRQ_global_disable();
        if ((++im->wr_cons == im->wr_prod) && (dma_wr->state != DMA_starting))
            dma_wr->state = DMA_inactive;
        IRQ_global_enable();

        /* This particular write is completed. */
        drv->writing = FALSE;
    }

    return FALSE;
}

void floppy_set_cyl(uint8_t unit, uint8_t cyl)
{
    if (unit == 0) {
        struct drive *drv = &drive;
        drv->cyl = cyl;
        if (cyl == 0)
            drive_change_output(drv, outp_trk0, TRUE);
    }
}

void floppy_get_track(uint8_t *p_cyl, uint8_t *p_side, uint8_t *p_sel,
                      uint8_t *p_writing)
{
    *p_cyl = drive.cyl;
    *p_side = drive.head & (drive.nr_sides - 1);
    *p_sel = drive.sel;
    *p_writing = (dma_wr && dma_wr->state != DMA_inactive);
}

bool_t floppy_handle(void)
{
    struct drive *drv = &drive;

    return ((dma_wr->state == DMA_inactive)
            ? dma_rd_handle : dma_wr_handle)(drv);
}

static void index_assert(void *dat)
{
    struct drive *drv = &drive;
    index.prev_time = index.timer.deadline;
    if (!drv->index_suppressed
        && !(drv->step.state && ff_cfg.index_suppression)) {
        drive_change_output(drv, outp_index, TRUE);
        timer_set(&index.timer_deassert, index.prev_time + time_ms(2));
    }
    if (dma_rd->state != DMA_active) /* timer set from input flux stream */
        timer_set(&index.timer, index.prev_time + drv->image->stk_per_rev);
}

static void index_deassert(void *dat)
{
    struct drive *drv = &drive;
    drive_change_output(drv, outp_index, FALSE);
}

static void drive_step_timer(void *_drv)
{
    struct drive *drv = _drv;

    switch (drv->step.state) {
    case STEP_started:
        /* nothing to do, IRQ_soft() needs to reset our deadline */
        break;
    case STEP_latched:
        speaker_pulse();
        if ((drv->cyl >= 84) && !drv->step.inward)
            drv->cyl = 84; /* Fast step back from D-A cyls */
        drv->cyl += drv->step.inward ? 1 : -1;
        timer_set(&drv->step.timer,
                  drv->step.start + time_ms(ff_cfg.head_settle_ms));
        if (drv->cyl == 0)
            drive_change_output(drv, outp_trk0, TRUE);
        /* New state last, as that lets hi-pri IRQ start another step. */
        barrier();
        drv->step.state = STEP_settling;
        break;
    case STEP_settling:
        /* Can race transition to STEP_started. */
        cmpxchg(&drv->step.state, STEP_settling, 0);
        break;
    }
}

static void IRQ_soft(void)
{
    struct drive *drv = &drive;

    if (drv->step.state == STEP_started) {
        timer_cancel(&drv->step.timer);
        drv->step.state = STEP_latched;
        timer_set(&drv->step.timer, drv->step.start + time_ms(1));
    }

    if (index.fake_fired) {
        index.fake_fired = FALSE;
        timer_set(&index.timer_deassert, time_now() + time_us(500));
    }
}

static void IRQ_rdata_dma(void)
{
    const uint16_t buf_mask = ARRAY_SIZE(dma_rd->buf) - 1;
    uint32_t prev_ticks_since_index, ticks, i;
    uint16_t nr_to_wrap, nr_to_cons, nr, dmacons, done;
    time_t now;
    struct drive *drv = &drive;

    /* Clear DMA peripheral interrupts. */
    dma1->ifcr = DMA_IFCR_CGIF(dma_rdata_ch);

    /* If we happen to be called in the wrong state, just bail. */
    if (dma_rd->state != DMA_active)
        return;

    /* Find out where the DMA engine's consumer index has got to. */
    dmacons = ARRAY_SIZE(dma_rd->buf) - dma_rdata.cndtr;

    /* Check for DMA catching up with the producer index (underrun). */
    if (((dmacons < dma_rd->cons)
         ? (dma_rd->prod >= dma_rd->cons) || (dma_rd->prod < dmacons)
         : (dma_rd->prod >= dma_rd->cons) && (dma_rd->prod < dmacons))
        && (dmacons != dma_rd->cons))
        printk("RDATA underrun! %x-%x-%x\n",
               dma_rd->cons, dma_rd->prod, dmacons);

    dma_rd->cons = dmacons;

    /* Find largest contiguous stretch of ring buffer we can fill. */
    nr_to_wrap = ARRAY_SIZE(dma_rd->buf) - dma_rd->prod;
    nr_to_cons = (dmacons - dma_rd->prod - 1) & buf_mask;
    nr = min(nr_to_wrap, nr_to_cons);
    if (nr == 0) /* Buffer already full? Then bail. */
        return;

    /* Now attempt to fill the contiguous stretch with flux data calculated 
     * from buffered image data. */
    prev_ticks_since_index = image_ticks_since_index(drv->image);
    dma_rd->prod += done = image_rdata_flux(
        drv->image, &dma_rd->buf[dma_rd->prod], nr);
    dma_rd->prod &= buf_mask;
    if (done != nr) {
        /* Read buffer ran dry: kick us when more data is available. */
        dma_rd->kick_dma_irq = TRUE;
    } else if (nr != nr_to_cons) {
        /* We didn't fill the ring: re-enter this ISR to do more work. */
        IRQx_set_pending(dma_rdata_irq);
    }

    /* Check if we have crossed the index mark. If not, we're done. */
    if (image_ticks_since_index(drv->image) >= prev_ticks_since_index)
        return;

    /* We crossed the index mark: Synchronise index pulse to the bitstream. */
    for (;;) {
        /* Snapshot current position in flux stream, including progress through
         * current timer sample. */
        now = time_now();
        /* Ticks left in current sample. */
        ticks = tim_rdata->arr - tim_rdata->cnt;
        /* Index of next sample. */
        dmacons = ARRAY_SIZE(dma_rd->buf) - dma_rdata.cndtr;
        /* If another sample was loaded meanwhile, try again for a consistent
         * snapshot. */
        if (dmacons == dma_rd->cons)
            break;
        dma_rd->cons = dmacons;
    }
    /* Sum all flux timings in the DMA buffer. */
    for (i = dmacons; i != dma_rd->prod; i = (i+1) & buf_mask)
        ticks += dma_rd->buf[i] + 1;
    /* Subtract current flux offset beyond the index. */
    ticks -= image_ticks_since_index(drv->image);
    /* Calculate deadline for index timer. */
    ticks /= SYSCLK_MHZ/TIME_MHZ;
    timer_set(&index.timer, now + ticks);
}

static void IRQ_wdata_dma(void)
{
    const uint16_t buf_mask = ARRAY_SIZE(dma_rd->buf) - 1;
    uint16_t cons, prod, prev, curr, next;
    uint16_t cell = image->write_bc_ticks, window;
    uint32_t bc_dat = 0, bc_prod;
    uint32_t *bc_buf = image->bufs.write_bc.p;
    unsigned int sync = image->sync;
    unsigned int bc_bufmask = (image->bufs.write_bc.len / 4) - 1;
    struct write *write = NULL;

    window = cell + (cell >> 1);

    /* Clear DMA peripheral interrupts. */
    dma1->ifcr = DMA_IFCR_CGIF(dma_wdata_ch);

    /* If we happen to be called in the wrong state, just bail. */
    if (dma_wr->state == DMA_inactive)
        return;

    /* Find out where the DMA engine's producer index has got to. */
    prod = ARRAY_SIZE(dma_wr->buf) - dma_wdata.cndtr;

    /* Check if we are processing the tail end of a write. */
    barrier(); /* interrogate peripheral /then/ check for write-end. */
    if (image->wr_bc != image->wr_prod) {
        write = get_write(image, image->wr_bc);
        prod = write->dma_end;
    }

    /* Process the flux timings into the raw bitcell buffer. */
    prev = dma_wr->prev_sample;
    bc_prod = image->bufs.write_bc.prod;
    bc_dat = image->write_bc_window;
    for (cons = dma_wr->cons; cons != prod; cons = (cons+1) & buf_mask) {
        next = dma_wr->buf[cons];
        curr = next - prev;
        prev = next;
        while (curr > window) {
            curr -= cell;
            bc_dat <<= 1;
            bc_prod++;
            if (!(bc_prod&31))
                bc_buf[((bc_prod-1) / 32) & bc_bufmask] = htobe32(bc_dat);
        }
        bc_dat = (bc_dat << 1) | 1;
        bc_prod++;
        switch (sync) {
        case SYNC_fm:
            /* FM clock sync clock byte is 0xc7. Check for:
             * 1010 1010 1010 1010 1x1x 0x0x 0x1x 1x1x */
            if ((bc_dat & 0xffffd555) == 0x55555015)
                bc_prod = (bc_prod - 31) | 31;
            break;
        case SYNC_mfm:
            if (bc_dat == 0x44894489)
                bc_prod &= ~31;
            break;
        }
        if (!(bc_prod&31))
            bc_buf[((bc_prod-1) / 32) & bc_bufmask] = htobe32(bc_dat);
    }

    if (bc_prod & 31)
        bc_buf[(bc_prod / 32) & bc_bufmask] = htobe32(bc_dat << (-bc_prod&31));

    /* Processing the tail end of a write? */
    if (write != NULL) {
        /* Remember where this write's bitcell data ends. */
        write->bc_end = bc_prod;
        image->wr_bc++;
        /* Initialise decoder state for the start of the next write. */
        bc_prod = (bc_prod + 31) & ~31;
        bc_dat = ~0;
        prev = 0;
    }

    /* Save our progress for next time. */
    image->write_bc_window = bc_dat;
    image->bufs.write_bc.prod = bc_prod;
    dma_wr->cons = cons;
    dma_wr->prev_sample = prev;
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

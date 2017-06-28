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
#define AFO_bus AFO_pushpull(_2MHz)

/* NB. All input pins must be 5v tolerant. */
/* Bitmap of current states of input pins. */
static uint8_t input_pins;

/* Mask of output pins within @gpio_out. */
static uint16_t gpio_out_mask;

#define m(pin) (1u<<(pin))

#if BUILD_TOUCH
#include "touch/floppy.c"
#elif BUILD_GOTEK
#include "gotek/floppy.c"
#endif

/* Bind all EXTI IRQs */
void IRQ_6(void) __attribute__((alias("IRQ_input_changed"))); /* EXTI0 */
void IRQ_7(void) __attribute__((alias("IRQ_input_changed"))); /* EXTI1 */
void IRQ_8(void) __attribute__((alias("IRQ_input_changed"))); /* EXTI2 */
void IRQ_9(void) __attribute__((alias("IRQ_input_changed"))); /* EXTI3 */
void IRQ_10(void) __attribute__((alias("IRQ_input_changed"))); /* EXTI4 */
void IRQ_23(void) __attribute__((alias("IRQ_input_changed"))); /* EXTI9_5 */
void IRQ_40(void) __attribute__((alias("IRQ_input_changed"))); /* EXTI15_10 */
static const uint8_t exti_irqs[] = { 6, 7, 8, 9, 10, 23, 40 };

/* A DMA buffer for running a timer associated with a floppy-data I/O pin. */
struct dma_ring {
    /* Current state of DMA: */
    /*  DMA_inactive: No activity, buffer is empty. */
#define DMA_inactive 0 /* -> {starting, active} */
    /*  DMA_starting: Buffer is filling, DMA+timer not yet active. */
#define DMA_starting 1 /* -> {active, stopping} */
    /*  DMA_active: DMA is active, timer is operational. */
#define DMA_active   2 /* -> {stopping} */
    /*  DMA_stopping: DMA+timer halted, buffer waiting to be cleared. */
#define DMA_stopping 3 /* -> {inactive} */
    volatile uint8_t state;
    /* IRQ handler sets this if the prefetch queue runs dry. */
    volatile uint8_t kick_dma_irq;
    /* Indexes into the buf[] ring buffer. */
    uint16_t prod, cons;
    /* {inactive, starting} -> {active} must happen within this cancellation. 
     * This allows it to be cancelled from the EXTI ISR if inputs change. */
    struct cancellation startup_cancellation;
    /* DMA ring buffer of timer values (ARR or CCRx). */
    uint16_t buf[1024];
};

/* DMA buffers are permanently allocated while a disk image is loaded, allowing 
 * independent and concurrent management of the RDATA/WDATA pins. */
static struct dma_ring *dma_rd; /* RDATA DMA buffer */
static struct dma_ring *dma_wr; /* WDATA DMA buffer */

static struct drive drive[NR_DRIVES];
static struct image *image;
static stk_time_t sync_time;

static struct {
    struct timer timer;
    bool_t active;
    stk_time_t prev_time;
} index;
static void index_pulse(void *);

static uint32_t max_load_ticks, max_prefetch_us;

static void rdata_stop(void);

#if 0
/* List changes at floppy inputs and sequentially activate outputs. */
static void floppy_check(void)
{
    uint16_t i=0, pin, prev_pin=0, inp, prev_inp=0;
    uint8_t changed;
    volatile struct gpio *gpio, *prev_gpio = NULL;

    for (;;) {
        gpio = gpio_out;
        switch (i++) {
        case 0: pin = pin_dskchg; break;
        case 1: pin = pin_index; break;
        case 2: pin = pin_trk0; break;
        case 3: pin = pin_wrprot; break;
        case 4: pin = pin_rdata; gpio = gpio_data; break;
        case 5: pin = pin_rdy; break;
        default: pin = 0; i = 0; break;
        }
        if (prev_pin) gpio_write_pin(prev_gpio, prev_pin, O_FALSE);
        if (pin) gpio_write_pin(gpio, pin, O_TRUE);
        prev_pin = pin;
        prev_gpio = gpio;
        delay_ms(50);
        changed = input_update();
        inp = input_pins;
        /* Stash wdata in inp[1] (Touch) or inp[3] (Gotek). */
        inp |= (gpio_data->idr & m(pin_wdata)) >> 5;
        if ((inp ^ prev_inp) || changed)
            printk("IN: %02x->%02x (%02x)\n", prev_inp, inp, changed);
        prev_inp = inp;
    }
}
#else
#define floppy_check() ((void)0)
#endif

void floppy_cancel(void)
{
    unsigned int i;

    /* Initialised? Bail if not. */
    if (!dma_rd)
        return;

    /* Stop interrupt work. */
    for (i = 0; i < ARRAY_SIZE(exti_irqs); i++)
        IRQx_disable(exti_irqs[i]);
    timer_cancel(&index.timer);

    /* Stop DMA/timer work. */
    rdata_stop();

    /* Quiesce outputs. */
    gpio_write_pins(gpio_out, gpio_out_mask, O_FALSE);

    /* Clear soft state. */
    memset(drive, 0, sizeof(drive));
    memset(&index, 0, sizeof(index));
    max_load_ticks = max_prefetch_us = 0;
    image = NULL;
    dma_rd = dma_wr = NULL;
}

static struct dma_ring *dma_ring_alloc(void)
{
    struct dma_ring *dma = arena_alloc(sizeof(*dma));
    memset(dma, 0, offsetof(struct dma_ring, buf));
    return dma;
}

void floppy_init(const char *disk0_name)
{
    unsigned int i;

    arena_init();

    dma_rd = dma_ring_alloc();
    dma_wr = dma_ring_alloc();

    image = arena_alloc(sizeof(*image));
    memset(image, 0, sizeof(*image));

    board_floppy_init();

    gpio_out_mask = ((1u << pin_dskchg)
                     | (1u << pin_index)
                     | (1u << pin_trk0)
                     | (1u << pin_wrprot)
                     | (1u << pin_rdy));

    for (i = 0; i < NR_DRIVES; i++)
        drive[i].cyl = 1; /* XXX */
    drive[0].filename = disk0_name;

    gpio_configure_pin(gpio_out, pin_dskchg, GPO_bus);
    gpio_configure_pin(gpio_out, pin_index,  GPO_bus);
    gpio_configure_pin(gpio_out, pin_trk0,   GPO_bus);
    gpio_configure_pin(gpio_out, pin_wrprot, GPO_bus);
    gpio_configure_pin(gpio_out, pin_rdy,    GPO_bus);

    gpio_configure_pin(gpio_data, pin_wdata, GPI_bus);
    gpio_configure_pin(gpio_data, pin_rdata, GPO_bus);

    floppy_check();

    index.prev_time = stk_now();
    timer_init(&index.timer, index_pulse, NULL);
    timer_set(&index.timer, stk_add(index.prev_time, stk_ms(200)));

    /* Enable interrupts. */
    for (i = 0; i < ARRAY_SIZE(exti_irqs); i++) {
        IRQx_set_prio(exti_irqs[i], FLOPPY_IRQ_HI_PRI);
        IRQx_set_pending(exti_irqs[i]);
        IRQx_enable(exti_irqs[i]);
    }
    dma1->ifcr = DMA_IFCR_CGIF(dma_rdata_ch);
    IRQx_set_prio(dma_rdata_irq, RDATA_IRQ_PRI);
    IRQx_enable(dma_rdata_irq);

    /* Timer setup:
     * The counter is incremented at full SYSCLK rate. 
     *  
     * Ch.2 (RDDATA) is in PWM mode 1. It outputs O_TRUE for 400ns and then 
     * O_FALSE until the counter reloads. By changing the ARR via DMA we alter
     * the time between (fixed-width) O_TRUE pulses, mimicking floppy drive 
     * timings. */
    tim_rdata->psc = 0;
    tim_rdata->ccer = TIM_CCER_CC2E | ((O_TRUE==0) ? TIM_CCER_CC2P : 0);
    tim_rdata->ccmr1 = (TIM_CCMR1_CC2S(TIM_CCS_OUTPUT) |
                        TIM_CCMR1_OC2M(TIM_OCM_PWM1));
    tim_rdata->ccr2 = sysclk_ns(400);
    tim_rdata->dier = TIM_DIER_UDE;
    tim_rdata->cr2 = 0;

    /* DMA setup: From a circular buffer into read-data timer's ARR. */
    dma_rdata.cpar = (uint32_t)(unsigned long)&tim_rdata->arr;
    dma_rdata.cmar = (uint32_t)(unsigned long)dma_rd->buf;
    dma_rdata.cndtr = ARRAY_SIZE(dma_rd->buf);
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

    /* Turn off timer and DMA. */
    tim_rdata->cr1 = 0;
    dma_rdata.ccr = 0;
    dma_rdata.cndtr = ARRAY_SIZE(dma_rd->buf);
}

/* Called from Thread context to start the read stream. */
static int rdata_start(void)
{
    dma_rd->state = DMA_active;
    barrier(); /* ensure IRQ sees the flag before we act on it */

    /* Start DMA from circular buffer. */
    dma_rdata.ccr = (DMA_CCR_PL_HIGH |
                     DMA_CCR_MSIZE_16BIT |
                     DMA_CCR_PSIZE_16BIT |
                     DMA_CCR_MINC |
                     DMA_CCR_CIRC |
                     DMA_CCR_DIR_M2P |
                     DMA_CCR_HTIE |
                     DMA_CCR_TCIE |
                     DMA_CCR_EN);

    /* Start timer. */
    tim_rdata->egr = TIM_EGR_UG;
    tim_rdata->sr = 0; /* dummy write, gives h/w time to process EGR.UG=1 */
    tim_rdata->cr1 = TIM_CR1_CEN;

    /* Enable output. */
    gpio_configure_pin(gpio_data, pin_rdata, AFO_bus);

    return 0;
}

static void image_stop_track(struct image *im)
{
    im->cur_track = TRACKNR_INVALID;
    if (!index.active)
        timer_set(&index.timer, stk_add(index.prev_time, stk_ms(200)));
}

static void floppy_sync_flux(void)
{
    struct drive *drv = &drive[0];
    int32_t ticks;
    uint32_t nr;

    nr = ARRAY_SIZE(dma_rd->buf) - dma_rd->prod - 1;
    if (nr)
        dma_rd->prod += image_load_flux(
            drv->image, &dma_rd->buf[dma_rd->prod], nr);

    if (dma_rd->prod < ARRAY_SIZE(dma_rd->buf)/2)
        return;

    ticks = stk_delta(stk_now(), sync_time) - stk_us(1);
    if (ticks > stk_ms(5)) /* ages to wait; go do other work */
        return;

    if (ticks > 0)
        delay_ticks(ticks);
    ticks = stk_delta(stk_now(), sync_time); /* XXX */
    call_cancellable_fn(&dma_rd->startup_cancellation, rdata_start);
    printk("Trk %u: sync_ticks=%d\n", drv->image->cur_track, ticks);
}

int floppy_handle(void)
{
    uint32_t i, load_ticks, prefetch_us, now = stk_now(), prev_dmaprod;
    stk_time_t timestamp[3];
    struct drive *drv;

    for (i = 0; i < NR_DRIVES; i++) {
        drv = &drive[i];
        if (drv->step.active) {
            drv->step.settling = FALSE;
            if (stk_diff(drv->step.start, now) < stk_ms(2))
                continue;
            speaker_pulse(10);
            drv->cyl += drv->step.inward ? 1 : -1;
            barrier(); /* update cyl /then/ clear active */
            drv->step.active = FALSE;
            drv->step.settling = TRUE;
            if ((i == 0) && (drv->cyl == 0))
                gpio_write_pin(gpio_out, pin_trk0, O_TRUE);
        } else if (drv->step.settling) {
            if (stk_diff(drv->step.start, now) < stk_ms(DRIVE_SETTLE_MS))
                continue;
            drv->step.settling = FALSE;
        }
    }

    drv = &drive[0];

    if (!drv->image) {
        if (!image_open(image, drv->filename))
            return -1;
        drv->image = image;
        image_stop_track(drv->image);
    }

    if (drv->image->cur_track == TRACKNR_INVALID) {
        stk_time_t index_time = index.prev_time;
        stk_time_t time_after_index = stk_timesince(index_time);
        /* Allow 10ms from current rotational position to load new track */
        int32_t delay = stk_ms(10);
        /* No data fetch while stepping. */
        if (drv->step.active)
            goto out;
        /* Allow extra time if heads are settling. */
        if (drv->step.settling) {
            stk_time_t step_settle = stk_add(drv->step.start,
                                             stk_ms(DRIVE_SETTLE_MS));
            int32_t delta = stk_delta(stk_now(), step_settle);
            delay = max_t(int32_t, delta, delay);
        }
        /* Add delay to synchronisation point; handle index wrap. */
        time_after_index += delay;
        if (time_after_index > stk_ms(DRIVE_MS_PER_REV))
            time_after_index -= stk_ms(DRIVE_MS_PER_REV);
        /* Seek to the new track. */
        image_seek_track(drv->image, drv->cyl*2 + drv->head,
                         &time_after_index);
        /* Check if the sync-up position wrapped at the index mark... */
        sync_time = stk_timesince(index_time);
        /* ...and synchronise to next index pulse if so. */
        if (sync_time > (time_after_index + stk_ms(DRIVE_MS_PER_REV)/2))
            time_after_index += stk_ms(DRIVE_MS_PER_REV);
        /* Set the deadline. */
        sync_time = stk_add(index_time, time_after_index);
    }

    timestamp[0] = stk_now();

    prev_dmaprod = dma_rd->prod;

    switch (dma_rd->state) {
    case DMA_inactive:
        dma_rd->state = DMA_starting;
        image_stop_track(drv->image);
        return 0;
    case DMA_starting:
        floppy_sync_flux();
        break;
    case DMA_stopping:
        dma_rd->state = DMA_inactive;
        /* Reinitialise the circular buffer to empty. */
        dma_rd->cons = dma_rd->prod = 0;
        break;
    case DMA_active:
        /* nothing */
        break;
    }

    timestamp[1] = stk_now();
    if (image_prefetch_data(drv->image) && dma_rd->kick_dma_irq) {
        dma_rd->kick_dma_irq = FALSE;
        IRQx_set_pending(dma_rdata_irq);
    }
    timestamp[2] = stk_now();

    /* 9MHz ticks per generated flux transition */
    load_ticks = stk_diff(timestamp[0],timestamp[1]);
    i = dma_rd->prod - prev_dmaprod;
    load_ticks = (i > 100 && dma_rd->prod) ? load_ticks / i : 0;
    /* Microseconds to prefetch data */
    prefetch_us = stk_diff(timestamp[1],timestamp[2])/STK_MHZ;
    /* If we have a new maximum, print it. */
    if ((load_ticks > max_load_ticks) || (prefetch_us > max_prefetch_us)) {
        max_load_ticks = max_t(uint32_t, max_load_ticks, load_ticks);
        max_prefetch_us = max_t(uint32_t, max_prefetch_us, prefetch_us);
        printk("New max: load_ticks=%u prefetch_us=%u\n",
               max_load_ticks, max_prefetch_us);
    }

out:
    return 0;
}

static void index_pulse(void *dat)
{
    index.active ^= 1;
    if (index.active) {
        index.prev_time = index.timer.deadline;
        gpio_write_pin(gpio_out, pin_index, O_TRUE);
        timer_set(&index.timer, stk_add(index.prev_time, stk_ms(2)));
    } else {
        gpio_write_pin(gpio_out, pin_index, O_FALSE);
        if (dma_rd->state != DMA_active) /* timer set from input flux stream */
            timer_set(&index.timer, stk_add(index.prev_time, stk_ms(200)));
    }
}

static void IRQ_input_changed(void)
{
    uint8_t inp, changed;
    uint16_t i;
    struct drive *drv;

    changed = input_update();
    inp = input_pins;

    drive[0].sel = !(inp & m(inp_sel0));
#if NR_DRIVES > 1
    drive[1].sel = !(inp & m(inp_sel1));
#endif

    if (changed & inp & m(inp_step)) {
        bool_t step_inward = !(inp & m(inp_dir));
        for (i = 0; i < NR_DRIVES; i++) {
            drv = &drive[i];
            if (!drv->sel || drv->step.active
                || (drv->cyl == (step_inward ? 84 : 0)))
                continue;
            drv->step.inward = step_inward;
            drv->step.start = stk_now();
            drv->step.active = TRUE;
            if (i == 0) {
                gpio_write_pin(gpio_out, pin_trk0, O_FALSE);
                rdata_stop();
                cancel_call(&dma_rd->startup_cancellation);
            }
        }
    }

    if (changed & m(inp_side)) {
        for (i = 0; i < NR_DRIVES; i++) {
            drv = &drive[i];
            drv->head = !(inp & m(inp_side));
            if (i == 0) {
                rdata_stop();
                cancel_call(&dma_rd->startup_cancellation);
            }
        }
    }
}

static void IRQ_rdata_dma(void)
{
    const uint16_t buf_mask = ARRAY_SIZE(dma_rd->buf) - 1;
    uint32_t prev_ticks_since_index, ticks, i;
    uint16_t nr_to_wrap, nr_to_cons, nr, dmacons, done;
    stk_time_t now;
    struct drive *drv = &drive[0];

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
        printk("Buffer underrun! %x-%x-%x\n",
               dma_rd->cons, dma_rd->prod, dmacons);

    dma_rd->cons = dmacons;

    /* Find largest contiguous stretch of ring buffer we can fill. */
    nr_to_wrap = ARRAY_SIZE(dma_rd->buf) - dma_rd->prod;
    nr_to_cons = (dmacons - dma_rd->prod - 1) & buf_mask;
    nr = min(nr_to_wrap, nr_to_cons);
    if (nr == 0) /* Buffer already full? Then bail. */
        return;

    /* Now attempt to fill the contiguous stretch with flux data calculated 
     * from prefetched image data. */
    prev_ticks_since_index = image_ticks_since_index(drv->image);
    dma_rd->prod += done = image_load_flux(
        drv->image, &dma_rd->buf[dma_rd->prod], nr);
    dma_rd->prod &= buf_mask;
    if (done != nr) {
        /* Prefetch buffer ran dry: kick us when more data is available. */
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
        now = stk_now();
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
    ticks /= SYSCLK_MHZ/STK_MHZ;
    timer_set(&index.timer, stk_add(now, ticks));
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

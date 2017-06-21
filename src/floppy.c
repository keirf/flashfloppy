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

static uint8_t initialised;

/* NB. All input pins must be 5v tolerant. */
/* Bitmap of current states of input pins. */
static uint8_t input_pins;

/* Mask of output pins within @gpio_out. */
static uint16_t gpio_out_mask;

#if BUILD_TOUCH

#define O_FALSE 0
#define O_TRUE  1

/* Offsets within the input_pins bitmap. */
#define inp_dir     0
#define inp_step    3
#define inp_sel0    4
#define inp_sel1    5
#define inp_wgate   6
#define inp_side    7

/* Outputs are buffered, thus do *not* need to be 5v tolerant. */
#define gpio_out gpiob
#define pin_dskchg  3
static uint8_t pin_index; /* PB2 (MM150); PB4 (LC150) */
#define pin_trk0    5
#define pin_wrprot 11
#define pin_rdy    12

#define gpio_timer gpiob
#define pin_wdata   6 /* must be 5v tolerant */
#define pin_rdata   7
#define dma_rdata   (dma1->ch7)
#define tim_rdata   (tim4)

#define NR_DRIVES 2

#elif BUILD_GOTEK

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

#define gpio_timer gpioa
#define pin_wdata   8
#define pin_rdata   7
#define dma_rdata   (dma1->ch3)
#define tim_rdata   (tim3)

#define NR_DRIVES 1

#endif /* BUILD_GOTEK */

#define m(pin) (1u<<(pin))

/* Bind all EXTI IRQs */
void IRQ_6(void) __attribute__((alias("IRQ_input_changed"))); /* EXTI0 */
void IRQ_7(void) __attribute__((alias("IRQ_input_changed"))); /* EXTI1 */
void IRQ_8(void) __attribute__((alias("IRQ_input_changed"))); /* EXTI2 */
void IRQ_9(void) __attribute__((alias("IRQ_input_changed"))); /* EXTI3 */
void IRQ_10(void) __attribute__((alias("IRQ_input_changed"))); /* EXTI4 */
void IRQ_23(void) __attribute__((alias("IRQ_input_changed"))); /* EXTI9_5 */
void IRQ_40(void) __attribute__((alias("IRQ_input_changed"))); /* EXTI15_10 */
static const uint8_t exti_irqs[] = { 6, 7, 8, 9, 10, 23, 40 };

static struct drive drive[NR_DRIVES];
static struct image image;
static uint16_t dmabuf[2048], dmaprod, dmacons_prev;
static stk_time_t sync_time;
/* data_state updated only in IRQ and cancellable contexts. */
static enum {
    DATA_stopped = 0,
    DATA_seeking,
    DATA_active
} data_state;

static struct {
    struct timer timer;
    bool_t active;
    stk_time_t prev_time, next_time;
} index;
static void index_pulse(void *);

static uint32_t max_load_ticks, max_prefetch_us;

static void rddat_stop(void);

static struct cancellation floppy_cancellation;

#if BUILD_TOUCH

/* Updates the board-agnostic input_pins bitmask with current states of 
 * input pins, and returns mask of pins which have changed state. */
static uint8_t (*input_update)(void);

/* Default input pins:
 * DIR = PA8, STEP=PA11, SELA=PA12, SELB=PA13, WGATE=PA14, SIDE=PA15
 */
static uint8_t input_update_default(void)
{
    uint16_t pr;

    pr = exti->pr;
    exti->pr = pr;

    input_pins = (gpioa->idr >> 8) & 0xf9;

    return (pr >> 8) & 0xf8;
}

static void input_init_default(void)
{
    gpio_configure_pin(gpioa, 8+inp_sel0,  GPI_bus);
    gpio_configure_pin(gpioa, 8+inp_sel1,  GPI_bus);
    gpio_configure_pin(gpioa, 8+inp_dir,   GPI_bus);
    gpio_configure_pin(gpioa, 8+inp_step,  GPI_bus);
    gpio_configure_pin(gpioa, 8+inp_wgate, GPI_bus);
    gpio_configure_pin(gpioa, 8+inp_side,  GPI_bus);

    /* PA[15:0] -> EXT[15:0] */
    afio->exticr1 = afio->exticr2 = afio->exticr3 = afio->exticr4 = 0x0000;

    exti->imr = exti->rtsr = exti->ftsr =
        m(8+inp_step) | m(8+inp_sel0) | m(8+inp_sel1)
        | m(8+inp_wgate) | m(8+inp_side);

    input_update = input_update_default;
}

/* TB160 input pins as default except:
 * SELB = PB8, WGATE = PB9.
 */
static uint8_t input_update_tb160(void)
{
    uint16_t pr;

    pr = exti->pr;
    exti->pr = pr;

    input_pins = ((gpioa->idr >> 8) & 0x99) | ((gpiob->idr >> 3) & 0x60);

    return ((pr >> 8) & 0x98) | ((pr >> 3) & 0x60);
}

static void input_init_tb160(void)
{
    gpio_configure_pin(gpioa, 8+inp_sel0,  GPI_bus);
    gpio_configure_pin(gpiob, 3+inp_sel1,  GPI_bus);
    gpio_configure_pin(gpioa, 8+inp_dir,   GPI_bus);
    gpio_configure_pin(gpioa, 8+inp_step,  GPI_bus);
    gpio_configure_pin(gpiob, 3+inp_wgate, GPI_bus);
    gpio_configure_pin(gpioa, 8+inp_side,  GPI_bus);

    /* PA[15:10,7:0] -> EXT[15:10,7:0], PB[9:8] -> EXT[9:8]. */
    afio->exticr1 = afio->exticr2 = afio->exticr4 = 0x0000;
    afio->exticr3 = 0x0011;

    exti->imr = exti->rtsr = exti->ftsr =
        m(8+inp_step) | m(8+inp_sel0) | m(3+inp_sel1)
        | m(3+inp_wgate) | m(8+inp_side);

    input_update = input_update_tb160;
}

#else /* BUILD_GOTEK */

/* Input pins:
 * DIR = PB0, STEP=PA1, SELA=PA0, WGATE=PB9, SIDE=PB4
 */
static uint8_t input_update(void)
{
    uint16_t pr, in_a, in_b;

    pr = exti->pr;
    exti->pr = pr;

    in_a = gpioa->idr;
    in_b = gpiob->idr;
    input_pins = ((in_a << 1) & 0x06) | ((in_b >> 2) & 0x80) | (in_b & 0x11);

    return ((pr << 1) & 0x06) | ((pr >> 2) & 0x80) | (pr & 0x10);
}

static void input_init_gotek(void)
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
}

#endif /* BUILD_GOTEK */

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
        case 4: pin = pin_rdata; gpio = gpio_timer; break;
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
        inp |= (gpio_timer->idr & m(pin_wdata)) >> 5;
        if ((inp ^ prev_inp) || changed)
            printk("IN: %02x->%02x (%02x)\n", prev_inp, inp, changed);
        prev_inp = inp;
    }
}
#else
#define floppy_check() ((void)0)
#endif

void floppy_deinit(void)
{
    unsigned int i;

    ASSERT(!cancellation_is_active(&floppy_cancellation));

    /* Initialised? Bail if not. */
    if (!initialised)
        return;

    /* Stop interrupt work. */
    for (i = 0; i < ARRAY_SIZE(exti_irqs); i++)
        IRQx_disable(exti_irqs[i]);
    timer_cancel(&index.timer);

    /* Stop DMA/timer work. */
    rddat_stop();

    /* Quiesce outputs. */
    gpio_write_pins(gpio_out, gpio_out_mask, O_FALSE);

    /* Clear soft state. */
    memset(&image, 0, sizeof(image));
    memset(drive, 0, sizeof(drive));
    memset(&index, 0, sizeof(index));
    max_load_ticks = max_prefetch_us = 0;
    initialised = FALSE;
    ASSERT(data_state == DATA_stopped);
    ASSERT((dmacons_prev == 0) && (dmaprod == 0));
}

void floppy_init(const char *disk0_name)
{
    unsigned int i;

#if BUILD_TOUCH
    switch (board_id) {
    case BRDREV_LC150:
        pin_index = 4;
        input_init_default();
        break;
    case BRDREV_MM150:
        pin_index = 2;
        input_init_default();
        break;
    case BRDREV_TB160:
        pin_index = 1;
        input_init_tb160();
        break;
    }
#else
    input_init_gotek();
#endif

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

    gpio_configure_pin(gpio_timer, pin_wdata, GPI_bus);
    gpio_configure_pin(gpio_timer, pin_rdata, GPO_bus);

    floppy_check();

    index.prev_time = stk_now();
    index.next_time = ~0u;
    timer_init(&index.timer, index_pulse, NULL);
    timer_set(&index.timer, stk_diff(index.prev_time, stk_ms(200)));

    /* Enable interrupts. */
    for (i = 0; i < ARRAY_SIZE(exti_irqs); i++) {
        IRQx_set_prio(exti_irqs[i], FLOPPY_IRQ_HI_PRI);
        IRQx_set_pending(exti_irqs[i]);
        IRQx_enable(exti_irqs[i]);
    }

    /* Timer setup:
     * The counter is incremented at full SYSCLK rate. 
     *  
     * Ch.2 (RDDATA) is in PWM mode 1. It outputs O_TRUE for 400ns and then 
     * O_FALSE until the counter reloads. By changing the ARR via DMA we alter
     * the time between (fixed-width) O_TRUE pulses, mimicking floppy drive 
     * timings. */
    tim_rdata->psc = 0;
    tim_rdata->ccer = TIM_CCER_CC2E;
    tim_rdata->ccmr1 = (TIM_CCMR1_CC2S(TIM_CCS_OUTPUT) |
                        TIM_CCMR1_OC2M(TIM_OCM_PWM1));
    tim_rdata->ccr2 = sysclk_ns(400);
    tim_rdata->dier = TIM_DIER_UDE;
    tim_rdata->cr2 = 0;

    /* DMA setup: From a circular buffer into read-data timer's ARR. */
    dma_rdata.cpar = (uint32_t)(unsigned long)&tim_rdata->arr;
    dma_rdata.cmar = (uint32_t)(unsigned long)dmabuf;
    dma_rdata.cndtr = ARRAY_SIZE(dmabuf);

    initialised = TRUE;
}

/* Called from IRQ context to stop the read stream. */
static void rddat_stop(void)
{
    int prev_state = data_state;

    data_state = DATA_stopped;

    /* Reinitialise the circular buffer to empty. */
    dmacons_prev = dmaprod = 0;

    if (prev_state != DATA_active)
        return;

    /* Turn off the output pin */
    gpio_configure_pin(gpio_timer, pin_rdata, GPO_bus);

    /* Turn off timer and DMA. */
    tim_rdata->cr1 = 0;
    dma_rdata.ccr = 0;
    dma_rdata.cndtr = ARRAY_SIZE(dmabuf);
}

/* Called from cancellable context to start the read stream. */
static void rddat_start(void)
{
    data_state = DATA_active;
    barrier(); /* ensure IRQ sees the flag before we act on it */

    /* Start DMA from circular buffer. */
    dma_rdata.ccr = (DMA_CCR_PL_HIGH |
                     DMA_CCR_MSIZE_16BIT |
                     DMA_CCR_PSIZE_16BIT |
                     DMA_CCR_MINC |
                     DMA_CCR_CIRC |
                     DMA_CCR_DIR_M2P |
                     DMA_CCR_EN);

    /* Start timer. */
    tim_rdata->egr = TIM_EGR_UG;
    tim_rdata->sr = 0; /* dummy write, gives h/w time to process EGR.UG=1 */
    tim_rdata->cr1 = TIM_CR1_CEN;

    /* Enable output. */
    gpio_configure_pin(gpio_timer, pin_rdata, AFO_bus);
}

static void image_stop_track(struct image *im)
{
    im->cur_track = TRACKNR_INVALID;
    if (!index.active)
        timer_set(&index.timer, stk_diff(index.prev_time, stk_ms(200)));
}

static void floppy_sync_flux(void)
{
    struct drive *drv = &drive[0];
    int32_t ticks;
    uint32_t nr;

    nr = ARRAY_SIZE(dmabuf) - dmaprod - 1;
    if (nr)
        dmaprod += image_load_flux(drv->image, &dmabuf[dmaprod], nr);

    if (dmaprod < ARRAY_SIZE(dmabuf)/2)
        return;

    ticks = stk_delta(stk_now(), sync_time) - stk_us(1);
    if (ticks > stk_ms(5)) /* ages to wait; go do other work */
        return;

    if (ticks > 0)
        delay_ticks(ticks);
    ticks = stk_delta(stk_now(), sync_time); /* XXX */
    rddat_start();
    printk("Trk %u: sync_ticks=%d\n", drv->image->cur_track, ticks);
}

static int floppy_load_flux(void)
{
    uint32_t ticks, i;
    uint16_t nr_to_wrap, nr_to_cons, nr, dmacons;
    stk_time_t now;
    struct drive *drv = &drive[0];

    if (data_state == DATA_stopped) {
        data_state = DATA_seeking;
        /* caller seeks */
        return -1;
    }

    if (data_state == DATA_seeking) {
        floppy_sync_flux();
        if (data_state != DATA_active)
            return 0;
    }

    dmacons = ARRAY_SIZE(dmabuf) - dma_rdata.cndtr;

    /* Check for DMA catching up with the producer index (underrun). */
    if (((dmacons < dmacons_prev)
         ? (dmaprod >= dmacons_prev) || (dmaprod < dmacons)
         : (dmaprod >= dmacons_prev) && (dmaprod < dmacons))
        && (dmacons != dmacons_prev))
        printk("Buffer underrun! %x-%x-%x\n", dmacons_prev, dmaprod, dmacons);

    ticks = image_ticks_since_index(drv->image);

    nr_to_wrap = ARRAY_SIZE(dmabuf) - dmaprod;
    nr_to_cons = (dmacons - dmaprod - 1) & (ARRAY_SIZE(dmabuf) - 1);
    nr = min(nr_to_wrap, nr_to_cons);
    if (nr) {
        dmaprod += image_load_flux(drv->image, &dmabuf[dmaprod], nr);
        dmaprod &= ARRAY_SIZE(dmabuf) - 1;
    }

    dmacons_prev = dmacons;

    /* Check if we have crossed the index mark. */
    if (image_ticks_since_index(drv->image) < ticks) {
        /* Synchronise index pulse to the bitstream. */
        for (;;) {
            /* Snapshot current position in flux stream, including progress
             * through current timer sample. */
            now = stk_now();
            /* Ticks left in current sample. */
            ticks = tim_rdata->arr - tim_rdata->cnt;
            /* Index of next sample. */
            dmacons = ARRAY_SIZE(dmabuf) - dma_rdata.cndtr;
            /* If another sample was loaded meanwhile, try again for a 
             * consistent snapshot. */
            if (dmacons == dmacons_prev)
                break;
            dmacons_prev = dmacons;
        }
        /* Sum all flux timings in the DMA buffer. */
        for (i = dmacons; i != dmaprod; i = (i+1) & (ARRAY_SIZE(dmabuf)-1))
            ticks += dmabuf[i] + 1;
        /* Subtract current flux offset beyond the index. */
        ticks -= image_ticks_since_index(drv->image);
        /* Calculate deadline for index timer. */
        ticks /= SYSCLK_MHZ/STK_MHZ;
        index.next_time = stk_diff(now, ticks);
    }

    return 0;
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
        if (!image_open(&image, drv->filename))
            return -1;
        drv->image = &image;
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
            stk_time_t step_settle = stk_diff(drv->step.start,
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
        sync_time = stk_diff(index_time, time_after_index);
    }

    timestamp[0] = stk_now();

    prev_dmaprod = dmaprod;

    if (call_cancellable_fn(&floppy_cancellation, floppy_load_flux) == -1) {
        image_stop_track(drv->image);
        return 0;
    }

    if (index.next_time != ~0u) {
        timer_set(&index.timer, index.next_time);
        index.next_time = ~0u;
    }

    timestamp[1] = stk_now();

    image_prefetch_data(drv->image);

    timestamp[2] = stk_now();

    /* 9MHz ticks per generated flux transition */
    load_ticks = stk_diff(timestamp[0],timestamp[1]);
    i = dmaprod - prev_dmaprod;
    load_ticks = (i > 100 && dmaprod) ? load_ticks / i : 0;
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
        timer_set(&index.timer, stk_diff(index.prev_time, stk_ms(2)));
    } else {
        gpio_write_pin(gpio_out, pin_index, O_FALSE);
        if (data_state != DATA_active) /* timer set from input flux stream */
            timer_set(&index.timer, stk_diff(index.prev_time, stk_ms(200)));
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
#if BUILD_TOUCH
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
                rddat_stop();
                cancel_call(&floppy_cancellation);
            }
        }
    }

    if (changed & m(inp_side)) {
        for (i = 0; i < NR_DRIVES; i++) {
            drv = &drive[i];
            drv->head = !(inp & m(inp_side));
            if (i == 0) {
                rddat_stop();
                cancel_call(&floppy_cancellation);
            }
        }
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

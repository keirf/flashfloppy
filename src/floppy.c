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

#define O_FALSE 0
#define O_TRUE  1

#define GPI_bus GPI_floating
#define GPO_bus GPO_pushpull(_2MHz,O_FALSE)
#define AFO_bus AFO_pushpull(_2MHz)

/* All gpio_in pins must be 5v tolerant. */
#define gpio_in gpioa
#define pin_dir     8
#define pin_step   11
#define pin_sel0   12
#define pin_sel1   13
#define pin_wgate  14
#define pin_side   15

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

#define m(pin) (1u<<(pin))

/* EXTI[15:10]: IRQ 40 */
void IRQ_40(void) __attribute__((alias("IRQ_input_changed")));
#define EXTI_IRQ 40
#define EXTI_IRQ_PRI 2 /* very high */

static struct drive drive[2];
static struct image image;
static uint16_t dmabuf[2048], dmaprod, dmacons_prev;

static struct timer index_timer;
static void index_pulse(void *);

#define image_open adf_open
#define image_seek_track adf_seek_track
#define image_prefetch_data adf_prefetch_data
#define image_load_mfm adf_load_mfm

#if 0
/* List changes at floppy inputs and sequentially activate outputs. */
static void floppy_check(void)
{
    uint16_t i=0, mask, pin, prev_pin=0, idr, prev_idr;

    gpio_configure_pin(gpio_timer, pin_rdata, GPO_bus);

    mask = m(pin_dir) | m(pin_step) | m(pin_sel0)
        | m(pin_sel1) | m(pin_wgate) | m(pin_side);
    prev_idr = 0;
    for (;;) {
        switch (i++) {
        case 0: pin = pin_dskchg; break;
        case 1: pin = pin_index; break;
        case 2: pin = pin_trk0; break;
        case 3: pin = pin_wrprot; break;
        case 4: pin = pin_rdata; break;
        case 5: pin = pin_rdy; break;
        default: pin = 0; i = 0; break;
        }
        if (prev_pin) gpio_write_pin(gpio_out, prev_pin, 0);
        if (pin) gpio_write_pin(gpio_out, pin, 1);
        prev_pin = pin;
        delay_ms(50);
        idr = gpio_in->idr & mask;
        idr |= gpio_timer->idr & m(pin_wdata);
        if (idr ^ prev_idr)
            printk("IN: %04x->%04x\n", prev_idr, idr);
        prev_idr = idr;
    }
}
#else
#define floppy_check() ((void)0)
#endif

void floppy_init(const char *disk0_name, const char *disk1_name)
{
    uint16_t i;

    pin_index = (board_id == BRDREV_MM150) ? 2 : 4;

    drive[0].filename = disk0_name;
    drive[1].filename = disk1_name;
    drive[0].cyl = drive[1].cyl = 1; /* XXX */

    gpio_configure_pin(gpio_in, pin_sel0,  GPI_bus);
    gpio_configure_pin(gpio_in, pin_sel1,  GPI_bus);
    gpio_configure_pin(gpio_in, pin_dir,   GPI_bus);
    gpio_configure_pin(gpio_in, pin_step,  GPI_bus);
    gpio_configure_pin(gpio_in, pin_wgate, GPI_bus);
    gpio_configure_pin(gpio_in, pin_side,  GPI_bus);

    gpio_configure_pin(gpio_out, pin_dskchg, GPO_bus);
    gpio_configure_pin(gpio_out, pin_index,  GPO_bus);
    gpio_configure_pin(gpio_out, pin_trk0,   GPO_bus);
    gpio_configure_pin(gpio_out, pin_wrprot, GPO_bus);
    gpio_configure_pin(gpio_out, pin_rdy,    GPO_bus);

    rcc->apb1enr |= RCC_APB1ENR_TIM4EN;
    gpio_configure_pin(gpio_timer, pin_wdata, GPI_bus);
    gpio_configure_pin(gpio_timer, pin_rdata, AFO_bus);

    floppy_check();

    index_timer.deadline = stk_deadline(stk_ms(200));
    index_timer.cb_fn = index_pulse;
    timer_set(&index_timer);

    /* PA[15:0] -> EXT[15:0] */
    afio->exticr1 = afio->exticr2 = afio->exticr3 = afio->exticr4 = 0x0000;

    exti->imr = exti->rtsr = exti->ftsr =
        m(pin_step) | m(pin_sel0) | m(pin_sel1) | m(pin_wgate) | m(pin_side);

    /* Enable interrupts. */
    IRQx_set_prio(EXTI_IRQ, EXTI_IRQ_PRI);
    IRQx_set_pending(EXTI_IRQ);
    IRQx_enable(EXTI_IRQ);

    /* Timer setup. 
     * The counter is incremented at full SYSCLK rate. 
     *  
     * Ch.2 (RDDATA) is in PWM mode 1. It outputs O_TRUE for 400ns and then 
     * O_FALSE until the counter reloads. By changing the ARR via DMA we alter
     * the time between (fixed-width) O_TRUE pulses, mimicking floppy drive 
     * timings. */
    tim4->psc = 0;
    tim4->ccer = TIM_CCER_CC2E;
    tim4->ccmr1 = (TIM_CCMR1_CC2S(TIM_CCS_OUTPUT) |
                   TIM_CCMR1_OC2M(TIM_OCM_PWM1));
    tim4->ccr2 = sysclk_ns(400);
    tim4->dier = TIM_DIER_UDE;
    tim4->cr2 = 0;
    tim4->cr1 = TIM_CR1_CEN;

    /* XXX dummy data */
    for (i = 0; i < ARRAY_SIZE(dmabuf); i++)
        dmabuf[i] = SYSCLK_MHZ * ((i&1) ? 2 : 4);

    /* DMA from a circular buffer into Timer 4's ARR. */
    dma1->ch7.cpar = (uint32_t)(unsigned long)&tim4->arr;
    dma1->ch7.cmar = (uint32_t)(unsigned long)dmabuf;
    dma1->ch7.cndtr = ARRAY_SIZE(dmabuf);
    dma1->ch7.ccr = (DMA_CCR_PL_HIGH |
                     DMA_CCR_MSIZE_16BIT |
                     DMA_CCR_PSIZE_16BIT |
                     DMA_CCR_MINC |
                     DMA_CCR_CIRC |
                     DMA_CCR_DIR_M2P |
                     DMA_CCR_EN);
}

static uint32_t max_load_us, max_prefetch_us;

int floppy_handle(void)
{
    FRESULT fr;
    uint32_t load_us, prefetch_us, now = stk_now();
    uint16_t i, nr_to_wrap, nr_to_cons, nr, dmacons;
    stk_time_t timestamp[3];

    for (i = 0; i < ARRAY_SIZE(drive); i++) {

        if (drive[i].step.active) {
            drive[i].step.settling = FALSE;
            if (stk_diff(drive[i].step.start, now) < stk_ms(2))
                continue;
            speaker_pulse(5);
            drive[i].cyl += drive[i].step.inward ? 1 : -1;
            drive[i].step.active = FALSE;
            drive[i].step.settling = TRUE;
            if ((i == 0) && (drive[i].cyl == 0))
                gpio_write_pin(gpio_out, pin_trk0, O_TRUE);
        } else if (drive[i].step.settling) {
            if (stk_diff(drive[i].step.start, now) < stk_ms(16))
                continue;
            drive[i].step.settling = FALSE;
        }
    }

    if (drive[0].step.active || drive[0].step.settling)
        return 0;

    if (!drive[0].image) {
        struct image *im = &image;
        fr = f_open(&im->fp, drive[0].filename, FA_READ);
        if (fr || !image_open(im))
            return -1;
        drive[0].image = im;
        im->cur_track = -1;
    }

    drive[0].head = !gpio_read_pin(gpio_in, pin_side); /* XXX */
    if (drive[0].cyl*2 + drive[0].head != drive[0].image->cur_track) {
        image_seek_track(drive[0].image, drive[0].cyl*2 + drive[0].head);
        dmacons_prev = dmaprod = ARRAY_SIZE(dmabuf) - dma1->ch7.cndtr; /* XXX */
    }

    timestamp[0] = stk_now();

    dmacons = ARRAY_SIZE(dmabuf) - dma1->ch7.cndtr;

    if ((dmacons < dmacons_prev)
        ? (dmaprod > dmacons_prev) || (dmaprod < dmacons)
        : (dmaprod > dmacons_prev) && (dmaprod < dmacons))
        printk("Overflow! %x-%x-%x\n", dmacons_prev, dmaprod, dmacons);

    nr_to_wrap = ARRAY_SIZE(dmabuf) - dmaprod;
    nr_to_cons = (dmacons - dmaprod) & (ARRAY_SIZE(dmabuf) - 1);
    nr = min(nr_to_wrap, nr_to_cons);
    if (nr) {
        dmaprod += image_load_mfm(drive[0].image, &dmabuf[dmaprod], nr);
        dmaprod &= ARRAY_SIZE(dmabuf) - 1;
    }

    dmacons_prev = dmacons;

    timestamp[1] = stk_now();

    image_prefetch_data(drive[0].image);

    timestamp[2] = stk_now();

    load_us = stk_diff(timestamp[0],timestamp[1])/STK_MHZ;
    prefetch_us = stk_diff(timestamp[1],timestamp[2])/STK_MHZ;
    if ((load_us > max_load_us) || (prefetch_us > max_prefetch_us)) {
        max_load_us = max_t(uint32_t, max_load_us, load_us);
        max_prefetch_us = max_t(uint32_t, max_prefetch_us, prefetch_us);
        printk("New max: %u %u\n", max_load_us, max_prefetch_us);
    }

    return 0;
}

static void index_pulse(void *dat)
{
    drive[0].index.active ^= 1;
    if (drive[0].index.active) {
        gpio_write_pin(gpio_out, pin_index, O_TRUE);
        index_timer.deadline = stk_diff(index_timer.deadline, stk_ms(2));
    } else {
        gpio_write_pin(gpio_out, pin_index, O_FALSE);
        index_timer.deadline = stk_diff(index_timer.deadline, stk_ms(198));
    }
    timer_set(&index_timer);
}

static void IRQ_input_changed(void)
{
    uint16_t changed, idr, i;

    changed = exti->pr;
    exti->pr = changed;

    idr = gpio_in->idr;

    drive[0].sel = !!(idr & m(pin_sel0));
    drive[1].sel = !!(idr & m(pin_sel1));

    if (changed & idr & m(pin_step)) {
        bool_t step_inward = !(idr & m(pin_dir));
        for (i = 0; i < ARRAY_SIZE(drive); i++) {
            if (!drive[i].sel || drive[i].step.active
                || (drive[i].cyl == (step_inward ? 84 : 0)))
                continue;
            drive[i].step.inward = step_inward;
            drive[i].step.start = stk_now();
            drive[i].step.active = TRUE;
            if (i == 0)
                gpio_write_pin(gpio_out, pin_trk0, O_FALSE);
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

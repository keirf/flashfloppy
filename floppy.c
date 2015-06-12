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

#if 0
#define GPI_bus GPI_floating
#define GPO_bus GPO_opendrain(_2MHz,HIGH)
#define AFO_bus AFO_opendrain(_2MHz)
#else /* XXX test only */
#define GPI_bus GPI_pull_up
#define GPO_bus GPO_pushpull(_2MHz,HIGH)
#define AFO_bus AFO_pushpull(_2MHz)
#endif

#define gpio_in gpiob
#define pin_dir     2
#define pin_motor   3
#define pin_step    4
#define pin_sel0    5
#define pin_sel1    8
#define pin_wgate   9
#define pin_side   10

#define gpio_out gpioa
#define pin_dskchg 11
#define pin_index  12
#define pin_trk0   13
#define pin_wrprot 14
#define pin_rdy    15

#define gpio_timer gpiob
#define pin_wdata   6
#define pin_rdata   7

#define m(pin) (1u<<(pin))

/* EXTI4: IRQ 10; EXTI[9:5]: IRQ 23; EXTI[15:10]: IRQ 40 */
void IRQ_10(void) __attribute__((alias("IRQ_input_changed")));
void IRQ_23(void) __attribute__((alias("IRQ_input_changed")));
void IRQ_40(void) __attribute__((alias("IRQ_input_changed")));

/* DMA1 channel 7: IRQ 17. */
void IRQ_17(void) __attribute__((alias("IRQ_feed_rdata")));

static const struct {
    uint8_t irq, pri;
} irqs[] = { {10,2}, {23,2}, {40,2}, {17,3} };

static struct {
    const char *filename;
    uint8_t cyl;
    bool_t sel;
    bool_t step_inward;
    uint32_t step_start;
    bool_t read_active;
} disk[2];

static uint16_t dmabuf[1024];
static uint8_t sectors[2][512];
static FIL file;

void floppy_init(const char *disk0_name, const char *disk1_name)
{
    uint16_t i;

    disk[0].filename = disk0_name;
    disk[1].filename = disk1_name;

    gpio_configure_pin(gpio_in, pin_motor, GPI_bus);
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

    /* PB[15:0] -> EXT[15:0] */
    afio->exticr1 = afio->exticr2 = afio->exticr3 = afio->exticr4 = 0x1111;

    exti->imr = exti->rtsr = exti->ftsr =
        m(pin_step) | m(pin_sel0) | m(pin_sel1) | m(pin_wgate) | m(pin_side);

    /* Enable interrupts. */
    for (i = 0; i < ARRAY_SIZE(irqs); i++) {
        IRQx_set_prio(irqs[i].irq, irqs[i].pri);
        IRQx_set_pending(irqs[i].irq);
        IRQx_enable(irqs[i].irq);
    }

    /* Timer setup. 
     * The counter is incremented at full SYSCLK rate. 
     *  
     * Ch.2 (RDDATA) is in PWM mode 2. It outputs LOW for 400ns and then 
     * HIGH until the counter reloads. By changing the ARR via DMA we alter
     * the time between (fixed-width) LOW pulses, mimicking floppy drive 
     * timings. */
    tim4->psc = 0;
    tim4->ccer = TIM_CCER_CC2E;
    tim4->ccmr1 = (TIM_CCMR1_CC2S(TIM_CCS_OUTPUT) |
                   TIM_CCMR1_OC2M(TIM_OCM_PWM2));
    tim4->ccr2 = sysclk_ns(400);
    tim4->dier = TIM_DIER_UDE;
    tim4->cr2 = 0;
    tim4->cr1 = TIM_CR1_CEN;

    /* XXX dummy data */
    for (i = 0; i < ARRAY_SIZE(dmabuf); i++)
        dmabuf[i] = SYSCLK_MHZ * ((i&1) ? 2 : 4);

    /* DMA from a circular buffer into Timer 4's ARR. Take interrupts as the
     * buffer empties so that we keep DMA endlessly supplied. */
    dma1->ch7.cpar = (uint32_t)(unsigned long)&tim4->arr;
    dma1->ch7.cmar = (uint32_t)(unsigned long)dmabuf;
    dma1->ch7.cndtr = ARRAY_SIZE(dmabuf);
    dma1->ch7.ccr = (DMA_CCR_PL_HIGH |
                     DMA_CCR_MSIZE_16BIT |
                     DMA_CCR_PSIZE_16BIT |
                     DMA_CCR_MINC |
                     DMA_CCR_CIRC |
                     DMA_CCR_DIR_M2P |
                     DMA_CCR_HTIE |
                     DMA_CCR_TCIE |
                     DMA_CCR_EN);
}

int floppy_handle(void)
{
    FRESULT fr;
    UINT nr;
    uint32_t time = stk->val;
    uint16_t i;

    for (i = 0; i < ARRAY_SIZE(disk); i++) {
        if (!disk[i].step_start
            || (((disk[i].step_start - time) & STK_MASK) < stk_ms(2)))
            continue;
        disk[i].cyl += disk[i].step_inward ? 1 : -1;
        disk[i].step_start = 0;
    }

    if (disk[0].step_start)
        return 0;

    if (!disk[0].read_active) {
        fr = f_open(&file, disk[0].filename, FA_READ)
            ?: f_read(&file, sectors, sizeof(sectors), &nr);
        if (fr != FR_OK || nr != sizeof(sectors))
            return -1;
    }

    return 0;
}

static void IRQ_feed_rdata(void)
{
    dma1->ifcr = DMA_IFCR_CGIF7;
}

static void IRQ_input_changed(void)
{
    uint16_t changed, idr, i;

    changed = exti->pr;
    exti->pr = changed;

    idr = gpiob->idr;

    disk[0].sel = !!(idr & m(pin_sel0));
    disk[1].sel = !!(idr & m(pin_sel1));

    if ((changed|idr) & m(pin_step)) {
        bool_t step_inward = !!(idr & m(pin_dir));
        for (i = 0; i < ARRAY_SIZE(disk); i++) {
            if (!disk[i].sel || disk[i].step_start
                || (disk[i].cyl == (step_inward ? 84 : 0)))
                continue;
            disk[i].step_inward = step_inward;
            disk[i].step_start = stk->val;
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

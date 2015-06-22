/*
 * led.c
 * 
 * Drive 4-digit display via TM1651 driver IC.
 * I2C-style serial protocol: DIO=PB10, CLK=PB11
 * 
 * TM1651 specified f_max is 500kHz with 50% duty cycle, so clock should change 
 * change value no more often than 1us. We clock with half-cycle 20us so we
 * are very conservative.
 * 
 * Written & released by Keir Fraser <keir.xen@gmail.com>
 * 
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

/* Brightness range is 0-7: 
 * 0 is very dim
 * 1-2 are easy on the eyes 
 * 3-7 are varying degrees of retina burn */
#define BRIGHTNESS 1

/* Serial bus, Timer 2 partially remapped, DMA1 channel 2. */
#define DAT_PIN 10 /* PB10, TIM2 ch 3 (partial remap) */
#define CLK_PIN 11 /* PB11, TIM2 ch 4 (partial remap) */
#define DAT_CCR ccr3
#define CLK_CCR ccr4

/* Command completion is DMA1 channel 2: IRQ 12. */
#define LED_IRQ 12
void IRQ_12(void) __attribute__((alias("IRQ_led")));

static const uint8_t digits[] = {
    0x3f, 0x06, 0x5b, 0x4f, 0x66, 0x6d, 0x7d, 0x07, /* 0-7 */
    0x7f, 0x6f, 0x77, 0x7c, 0x39, 0x5e, 0x79, 0x71, /* 8-f */
};

static uint8_t dmabuf[48], *dmap, flags, pending[3];
#define FLG_PENDING  (1u<<0)
#define FLG_UPDATING (1u<<1)

static void write(uint8_t x)
{
    unsigned int i;
    /* We toggle DAT line so only interested in bit-value changes. */
    uint16_t y = x ^ ((uint16_t)x << 1);

    /* 8 data bits, LSB first, then blithely drive data LOW during ACK. */
    for (i = 0; i < 9; i++) {
        *dmap++ = (y&1) ? 1 : 4;
        y >>= 1;
    }
}

static void start(void)
{
    /* Toggle during CLK HIGH half-period. START is DAT HIGH-to-LOW.
     * This is fine because STOP leaves DAT HIGH. */
    *dmap++ = 3;
}
/* STOP and START are the same, both toggle the DAT line.  
 * Only difference is the pre-condition: STOP is DAT LOW-to-HIGH. 
 * This is fine because START leaves DAT LOW, as does ACK bit of any 
 * transmitted command/data byte. */
#define stop() start()

static void dma_prep(void)
{
    dmap = dmabuf;
}

static void dma_issue(void)
{
    /* Stop toggling the DAT line. (It will be left HIGH). */
    *dmap++ = 4;

    ASSERT((dmap-dmabuf) <= sizeof(dmabuf));

    flags = FLG_UPDATING;
    barrier();

    /* Start the clock output, 50% duty cycle. */
    tim2->CLK_CCR = 2;

    /* Start DMA. */
    dma1->ch2.cndtr = dmap - dmabuf;
    dma1->ch2.ccr = (DMA_CCR_MSIZE_8BIT |
                     DMA_CCR_PSIZE_16BIT |
                     DMA_CCR_MINC |
                     DMA_CCR_DIR_M2P |
                     DMA_CCR_TCIE |
                     DMA_CCR_EN);

    /* Request DMA next time the counter reloads.
     * 
     * NOTE: We don't leave UDE set while idle as the timer would signal a DMA
     * request regardless, and continue to assert in the absence of an ACK from
     * the DMA controller (ref STMicro RM0008, 13.3.1 "DMA Transactions").
     * 
     * The 1st DMA transaction would then occur immediately that the DMA
     * channel is enabled, unsychronised with any contemporary update event.
     * The unsynchronised DMA (to CCR[preload]) could occur just /after/ the 
     * timer has loaded CCR[active], but /before/ it sends the next
     * update-event DMA request. The 2nd DMA will then overwrite the 1st before
     * it is loaded into the active register; the 1st DMA is effectively lost.
     * 
     * By enabling UDE last, we ensure that the 1st DMA is synchronised with an
     * update event, just the same as all successive DMAs. */
    tim2->dier = TIM_DIER_UDE;
}

static void leds_update(void)
{
    dma_prep();
    start();
    write(0xc0);    /* set addr 0 */
    write(pending[0]); /* dat0 */
    write(pending[1]); /* dat1 */
    write(pending[2]); /* dat2 */
    write(0x00);    /* dat3 */
    stop();
    dma_issue();
}

static void leds_write(uint8_t d[3])
{
    bool_t defer = !!(flags & FLG_UPDATING);

    if (defer)
        IRQx_disable(LED_IRQ);

    memcpy(pending, d, 3);
    flags |= FLG_PENDING;

    if (defer)
        IRQx_enable(LED_IRQ);

    if (!defer || (flags == FLG_PENDING))
        leds_update();
}

void leds_write_hex(unsigned int x)
{
    uint8_t d[3] = { digits[(x>>8)&15], digits[(x>>4)&15], digits[x&15] };
    leds_write(d);
}

void leds_init(void)
{
    /* Prepare the bus: timer 2 outputting on PB{10,11}. */
    gpio_configure_pin(gpiob, DAT_PIN, AFO_pushpull(_2MHz));
    gpio_configure_pin(gpiob, CLK_PIN, AFO_pushpull(_2MHz));
    afio->mapr |= AFIO_MAPR_TIM2_REMAP_PARTIAL_2;

    /* Turn on the clocks. */
    rcc->apb1enr |= RCC_APB1ENR_TIM2EN;

    /* Clear IRQ line and then enable it. Peripherals pulse their interrupt
     * line when any ISR flag transitions to set. If we do not ensure the ISR
     * flag is initially cleared, we will never receive an interrupt. */
    dma1->ifcr = DMA_IFCR_CTCIF2;
    IRQx_set_prio(LED_IRQ, 15); /* lowest */
    IRQx_clear_pending(LED_IRQ);
    IRQx_enable(LED_IRQ);

    /* Timer setup. 
     * The counter is incremented every 10us, and counts 0 to 3 before 
     * reloading (i.e. reloads every 40us). 
     * 
     * Ch.4 (CLK) is in PWM mode 2. With CCR set to 2 it outputs LOW-then-HIGH,
     * 50% duty cycle, 40us period. When CCR is 0 the output is locked HIGH. 
     * 
     * Ch.3 is on output toggle mode. Each reload period DMA writes 1, 3, or 
     * 4 into the CCR, with the following effects: 
     *  1: Toggle DAT while CLK is LOW (normal data clock) 
     *  3: Toggle DAT while CLK is HIGH (START or STOP transmission) 
     *  4: No toggle in this clock period (since counter never reaches 4). 
     * Because it is in toggle mode, we must start with a known output value.
     * Hence we force the DAT output HIGH before programming into toggle mode.
     * 
     * Note that both CCRs have preload enabled, so writes have no effect 
     * until the next clock period. For example, starting and stopping the 
     * CLK output will never result in truncated clock cycles at the output. */
    tim2->arr = 3;               /* Count 0 to 3, then reload */
    tim2->psc = SYSCLK_MHZ * 10; /* 10us per tick */
    tim2->ccer = TIM_CCER_CC3E | TIM_CCER_CC4E;
    tim2->ccmr2 = (TIM_CCMR2_CC3S(TIM_CCS_OUTPUT) |
                   TIM_CCMR2_OC3M(TIM_OCM_FORCE_HIGH));
    /* Initialise the CCRs immediately, before we set preload flags. */
    tim2->CLK_CCR = 0; /* locked HIGH; set to 2 to enable 50% duty cycle */
    tim2->DAT_CCR = 4; /* locked HIGH; updated by dma */
    tim2->ccmr2 = (TIM_CCMR2_CC4S(TIM_CCS_OUTPUT) |
                   TIM_CCMR2_OC4M(TIM_OCM_PWM2) |
                   TIM_CCMR2_OC4PE |
                   TIM_CCMR2_CC3S(TIM_CCS_OUTPUT) |
                   TIM_CCMR2_OC3M(TIM_OCM_TOGGLE) |
                   TIM_CCMR2_OC3PE);
    tim2->cr2 = tim2->dier = 0;
    tim2->cr1 = TIM_CR1_CEN;

    /* DMA setup: issues writes from a pre-filled buffer to the DAT CCR. */
    dma1->ch2.cpar = (uint32_t)(unsigned long)&tim2->DAT_CCR;
    dma1->ch2.cmar = (uint32_t)(unsigned long)dmabuf;

    dma_prep();

    /* Data command: write registers, auto-increment address. */
    start();
    write(0x40);
    stop();

    /* Display control: brightness. */
    start();
    write(0x88 + BRIGHTNESS);
    stop();

    dma_issue();
}

static void IRQ_led(void)
{
    /* Latch and zap the progress flags. */
    uint8_t _flags = flags;
    flags = 0;

    /* Stop the CLK output and clear the DMA controller. */
    tim2->CLK_CCR = 0; /* CLK output locked HIGH */
    tim2->dier = 0; /* disable dma requests */
    dma1->ch2.ccr = 0;
    dma1->ifcr = DMA_IFCR_CTCIF2;

    /* An update must have been in progress. */
    ASSERT(_flags & FLG_UPDATING);

    /* Issue the next update if one is pending. */
    if (_flags & FLG_PENDING)
        leds_update();
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

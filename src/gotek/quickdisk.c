/*
 * gotek/quickdisk.c
 * 
 * Gotek-specific QD-interface setup.
 * 
 * Written & released by Keir Fraser <keir.xen@gmail.com>
 * 
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

/* Used by floppy_generic.c to set up correct RDATA polarity. */
#define O_TRUE 1

/* Input pins: All are level signals. */
#define pin_reset   1 /* PA1, /RS: LOW = Reset asserted */
#define pin_motor   0 /* PA0, /MO: LOW = Motor on */
static uint8_t pin_wgate = 9; /* PB9,  WG: HIGH = Write active */

/* Output pins: All are level signals. PBx = 0-15, PAx = 16-31. */
static uint8_t pin_02 = 7; /* PB7 */
#define pin_08      8      /* PB8 */
static uint8_t pin_26 = 6; /* PB6 */
#define pin_28      5      /* PB5 */
#define pin_34      3      /* PB3 */
#define pin_media   pin_02 /* /MS: LOW = Media present */
#define pin_wrprot  pin_28 /* /WP: LOW = Media present and writeable */
#define pin_ready   pin_34 /* /RY: LOW = Read/write window active */

/* RDATA and /WDATA */
#define gpio_data gpioa

#define pin_wdata   8      /* /WD: Negative pulse signal */
#define tim_wdata   (tim1)
#define dma_wdata   (dma1->ch[2-1])
#define dma_wdata_ch 2
#define dma_wdata_irq 12
void IRQ_12(void) __attribute__((alias("IRQ_wdata_dma")));

#define pin_rdata   7      /*  RD: Positive pulse signal */
#define tim_rdata   (tim3)
#define dma_rdata   (dma1->ch[3-1])
#define dma_rdata_ch 3
#define dma_rdata_irq 13
void IRQ_13(void) __attribute__((alias("IRQ_rdata_dma")));

/* EXTI IRQs. */
#define motor_irq  6
#define wgate_irq 23
void IRQ_6(void) __attribute__((alias("IRQ_MOTOR_changed"))); /* EXTI0 */
void IRQ_7(void) __attribute__((alias("IRQ_WGATE_changed"))); /* EXTI1 */
void IRQ_23(void) __attribute__((alias("IRQ_WGATE_changed"))); /* EXTI9_5 */
void IRQ_28(void) __attribute__((alias("IRQ_RESET_changed"))); /* TMR2 */
void IRQ_40(void) __attribute__((alias("_IRQ_rotary"))); /* EXTI15_10 */
static const struct exti_irq exti_irqs[] = {
    /* MOTOR */ { 6, TIMER_IRQ_PRI, 0 }, 
    /* WGATE */ { 7, FLOPPY_IRQ_WGATE_PRI, 0 },
    /* WGATE */ { 23, FLOPPY_IRQ_WGATE_PRI, 0 },
    /* RESET */ { 28, TIMER_IRQ_PRI, 0 },
    /* Rotary */ { 40, TIMER_IRQ_PRI, 0 }
};

bool_t floppy_ribbon_is_reversed(void)
{
    return FALSE;
}

static void board_floppy_init(void)
{
    /* PA1 (RESET) triggers IRQ via TIM2 Channel 2, since EXTI is used for 
     * WGATE on PB1. */
    tim2->ccmr1 = TIM_CCMR1_CC2S(TIM_CCS_INPUT_TI1);
    tim2->ccer = TIM_CCER_CC2E;
    tim2->dier = TIM_DIER_CC2IE;
    tim2->cr1 = TIM_CR1_CEN;

    if (is_32pin_mcu) {
        pin_02 = 16 + 14; /* PA14 */
        pin_26 = 16 + 13; /* PA13 */
        pin_wgate = 1; /* PB1 */
    }

    /* PA[15:14], PB[13:12], PC[11:10], PB[9:1], PA[0] */
    afio->exticr[4-1] = 0x0011;
    afio->exticr[3-1] = 0x2211;
    afio->exticr[2-1] = 0x1111;
    afio->exticr[1-1] = 0x1110;
    
    exti->rtsr = 0xffff;
    exti->ftsr = 0xffff;
    exti->imr = m(pin_wgate) | m(pin_motor);
}

static void IRQ_WGATE_changed(void)
{
    /* Clear WGATE-changed flag. */
    exti->pr = m(pin_wgate);

    /* If WRPROT line is asserted then we ignore WGATE. */
    if (read_pin(wrprot))
        return;

    if (!(gpiob->idr & m(pin_wgate)) || read_pin(ready)) {
        /* !WG || !/RY */
        drive.index_suppressed = FALSE;
        wdata_stop();
        if (drive.index_suppressed && (window.state <= WIN_rdata_off)) {
            window.paused = TRUE;
            window.pause_pos = drive.restart_pos;
        }
    } else {
        /* WG && /RY */
        rdata_stop();
        wdata_start();
    }
}

static void _IRQ_MOTOR_RESET_changed(unsigned int gpioa_idr)
{
    const unsigned int mask = m(pin_reset) | m(pin_motor);
    unsigned int off;

    /* Motor is off if either /RESET low or /MOTOR high. */
    off = gpioa_idr & mask;
    off ^= m(pin_reset);

    /* /RESET is forced by media removal. */
    if (read_pin(media))
        off |= m(pin_reset);

    /* Some signal changed, so we lose the spun-up state immediately. */
    motor.on = FALSE;

    if (!off) {

        /* 2 seconds to spin up the motor. */
        timer_set(&motor.timer, time_now() + time_ms(2000));

    } else {

        /* Motor is spinning down, or off: Cancel the spin-up timer. */
        timer_cancel(&motor.timer);

        if (/* RESET immediately clears READY */
            (off & m(pin_reset))
            /* !MOTOR immediately clears READY iff Jumper JC is strapped */
            || ((off & m(pin_motor)) && board_jc_strapped())) {
            write_pin(ready, HIGH);
        }

    }
}

static void IRQ_MOTOR_changed(void)
{
    /* Clear MOTOR-changed flag. */
    exti->pr = m(pin_motor);

    _IRQ_MOTOR_RESET_changed(gpioa->idr);
}

static void IRQ_RESET_changed(void)
{
    unsigned int gpioa_idr, gpioa_idr2 = gpioa->idr;

    do {

        /* Clear RESET-changed flag. */
        (void)tim2->ccr2;

        /* Execute MOTOR/RESET logic based on snapshotted pin state. */
        gpioa_idr = gpioa_idr2;
        _IRQ_MOTOR_RESET_changed(gpioa_idr);

        /* Update the timer channel's edge detector to detect the next edge 
         * depending on snapshotted RESET pin state. */
        if (gpioa_idr & m(pin_reset)) {
            tim2->ccer |= TIM_CCER_CC2P; /* Falling edge */
        } else {
            tim2->ccer &= ~TIM_CCER_CC2P; /* Rising edge */
        }

        /* Now check if we raced a RESET edge. Loop if so. */
        gpioa_idr2 = gpioa->idr;
    } while ((gpioa_idr ^ gpioa_idr2) & m(pin_reset));
}

static void _IRQ_rotary(void)
{
    exti->pr = 0xfc00; /* Clear PR[15:10] */
    IRQ_rotary();
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

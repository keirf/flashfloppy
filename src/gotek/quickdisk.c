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
#define pin_wgate   9 /* PB9,  WG: HIGH = Write active */

/* Output pins: All are level signals. */
#define gpio_out gpiob
#define pin_02      7
#define pin_08      8
#define pin_26      6
#define pin_28      5
#define pin_34      3
#define pin_media   pin_02 /* /MS: LOW = Media present */
#define pin_wrprot  pin_28 /* /WP: LOW = Media present and writeable */
#define pin_ready   pin_34 /* /RY: LOW = Read/write window active */

/* RDATA and /WDATA */
#define gpio_data gpioa

#define pin_wdata   8      /* /WD: Negative pulse signal */
#define tim_wdata   (tim1)
#define dma_wdata   (dma1->ch2)
#define dma_wdata_ch 2
#define dma_wdata_irq 12
void IRQ_12(void) __attribute__((alias("IRQ_wdata_dma")));

#define pin_rdata   7      /*  RD: Positive pulse signal */
#define tim_rdata   (tim3)
#define dma_rdata   (dma1->ch3)
#define dma_rdata_ch 3
#define dma_rdata_irq 13
void IRQ_13(void) __attribute__((alias("IRQ_rdata_dma")));

/* EXTI IRQs. */
#define motor_irq  6
#define reset_irq  7
#define wgate_irq 23
void IRQ_6(void) __attribute__((alias("IRQ_MOTOR_changed"))); /* EXTI0 */
void IRQ_7(void) __attribute__((alias("IRQ_MOTOR_changed"))); /* EXTI1 */
void IRQ_23(void) __attribute__((alias("IRQ_WGATE_changed"))); /* EXTI9_5 */
static const struct exti_irq exti_irqs[] = {
    { motor_irq, TIMER_IRQ_PRI, 0 }, 
    { reset_irq, TIMER_IRQ_PRI, 0 },
    { wgate_irq, FLOPPY_IRQ_WGATE_PRI, 0 }
};

bool_t floppy_ribbon_is_reversed(void)
{
    return FALSE;
}

static void board_floppy_init(void)
{
    uint32_t pins;

    /* PA[15:14] -> EXT[15:14], PB[13:2] -> EXT[13:2], PA[1:0] -> EXT[1:0] */
    afio->exticr4 = 0x0011;
    afio->exticr2 = 0x1111;
    afio->exticr3 = 0x1111;
    afio->exticr1 = 0x1100;

    pins = m(pin_wgate) | m(pin_reset) | m(pin_motor);
    exti->rtsr = pins;
    exti->ftsr = pins;
    exti->imr = pins;
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
        wdata_stop();
    } else {
        /* WG && /RY */
        rdata_stop();
        wdata_start();
    }
}

static void IRQ_MOTOR_changed(void)
{
    const uint16_t mask = m(pin_reset) | m(pin_motor);
    uint8_t off;

    /* Clear MOTOR- and RESET-changed flags. */
    exti->pr = mask;

    /* Motor is off if either /RESET low or /MOTOR high. */
    off = gpioa->idr & mask;
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
            || ((off & m(pin_motor)) && !gpio_read_pin(gpiob, 1))) {
            write_pin(ready, HIGH);
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

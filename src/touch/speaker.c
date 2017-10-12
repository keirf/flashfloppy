/*
 * speaker.c
 *
 * PC speaker/buzzer control.
 * 
 * We drive the speaker with pulses, the width of which determine volume.
 * Single pulses generate a click. As the pulse frequency rises above ~50Hz,
 * a tone is generated. Max frequency is limited by MAX_KHZ.
 * 
 * Written & released by Keir Fraser <keir.xen@gmail.com>
 * 
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

/* MM150: Timer 3, channel 1, PB4 
 * LC150: Timer 3, channel 4, PB1 */
#define gpio_spk gpiob
#define tim tim3

#define MAX_KHZ 5   /* Limits highest frequency */
#define TICK_MHZ 8  /* Controls volume range */
#define ARR (TICK_MHZ*1000/MAX_KHZ-1)

static void _speaker_pulse(unsigned int volume);

void speaker_init(void)
{
    uint8_t pin_spk = (board_id == BRDREV_LC150) ? 1 : 4;

    /* PWM2 mode achieves a LOW-HIGH-LOW pulse in one-pulse mode, which is 
     * what we require to drive an NPN BJT with grounded emitter. */
    tim->psc = SYSCLK_MHZ/TICK_MHZ - 1;
    tim->arr = ARR;
    tim->ccmr1 = (TIM_CCMR1_CC1S(TIM_CCS_OUTPUT) |
                  TIM_CCMR1_OC1M(TIM_OCM_PWM2)); /* PWM2: low then high */
    tim->ccmr2 = (TIM_CCMR2_CC4S(TIM_CCS_OUTPUT) |
                  TIM_CCMR2_OC4M(TIM_OCM_PWM2)); /* PWM2: low then high */
    tim->ccer = TIM_CCER_CC1E|TIM_CCER_CC4E;
    tim->cr2 = tim->dier = 0;
    _speaker_pulse(0); /* ensures output LOW */

    /* Set up the output pin. */
    afio->mapr |= AFIO_MAPR_TIM3_REMAP_PARTIAL;
    gpio_configure_pin(gpio_spk, pin_spk, AFO_pushpull(_2MHz));
}

static void _speaker_pulse(unsigned int volume)
{
    volatile uint32_t *pwm_ccr =
        (board_id == BRDREV_LC150) ? &tim->ccr4 : &tim->ccr1;

    /* Don't overlap pulses; limit the maximum frequency. */
    if (tim->cr1 & TIM_CR1_CEN)
        return;

    /* Quadratic scaling of pulse width seems to give linear-ish volume. */
    *pwm_ccr = ARR + 1 - volume*volume;
    tim->cr1 = TIM_CR1_OPM | TIM_CR1_CEN;
}

void speaker_pulse(void)
{
    _speaker_pulse(ff_cfg.step_volume);
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

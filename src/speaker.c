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

/* PWM pin */
#define gpio_spk gpiob
#define PIN_SPK 1

/* Timer channel for above pin: Timer 2, channel 3. */
#define tim tim3
#define PWM_CCR ccr4

#define MAX_KHZ 5   /* Limits highest frequency */
#define TICK_MHZ 8  /* Controls volume range */
#define ARR (TICK_MHZ*1000/MAX_KHZ-1)

void speaker_init(void)
{
    /* PWM2 mode achieves a LOW-HIGH-LOW pulse in one-pulse mode, which is 
     * what we require to drive an NPN BJT with grounded emitter. */
    rcc->apb1enr |= RCC_APB1ENR_TIM3EN;
    tim->psc = SYSCLK_MHZ/TICK_MHZ - 1;
    tim->arr = ARR;
    tim->ccer = TIM_CCER_CC4E;
    tim->ccmr2 = (TIM_CCMR2_CC4S(TIM_CCS_OUTPUT) |
                  TIM_CCMR2_OC4M(TIM_OCM_PWM2)); /* PWM2: low then high */
    tim->cr2 = tim->dier = 0;
    speaker_pulse(0); /* ensures output LOW */

    /* Set up the output pin. */
    gpio_configure_pin(gpio_spk, PIN_SPK, AFO_pushpull(_2MHz));
}

/* Volume: 0 (silence) - 20 (loudest) */
void speaker_pulse(uint8_t volume)
{
    /* Don't overlap pulses; limit the maximum frequency. */
    if (tim->cr1 & TIM_CR1_CEN)
        return;

    /* Quadratic scaling of pulse width seems to give linear-ish volume. */
    tim->PWM_CCR = ARR + 1 - volume*volume;
    tim->cr1 = TIM_CR1_OPM | TIM_CR1_CEN;
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

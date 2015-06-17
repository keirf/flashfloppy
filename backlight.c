/*
 * backlight.c
 *
 * PWM-switch the TFT LED backlight.
 * 
 * Written & released by Keir Fraser <keir.xen@gmail.com>
 * 
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

/* Must be a 5v-tolerant pin with a timer channel attached. */
#define gpio_led gpioa
#define PIN_LED 8

/* Timer channel for above pin: Timer 1, channel 1. */
#define tim tim1
#define PWM_CCR ccr1

void backlight_init(void)
{
    /* Set up timer, switch backlight off. 
     * We switch a PNP transistor so PWM output is active low. */
    rcc->apb2enr |= RCC_APB2ENR_TIM1EN;
    tim->arr = 999; /* count 0-999 inclusive */
    tim->psc = SYSCLK_MHZ - 1; /* tick at 1MHz */
    tim->ccer = TIM_CCER_CC1E;
    tim->ccmr1 = (TIM_CCMR1_CC1S(TIM_CCS_OUTPUT) |
                  TIM_CCMR1_OC1M(TIM_OCM_PWM2)); /* PWM2: low then high */
    tim->bdtr = TIM_BDTR_MOE;
    tim->PWM_CCR = tim->cr2 = tim->dier = 0;
    tim->cr1 = TIM_CR1_CEN;
    gpio_configure_pin(gpio_led, PIN_LED, AFO_opendrain(_2MHz));
}

/* Set brightness level: 0-10. */
void backlight_set(uint8_t level)
{
    /* Logarithmic scale. */
    tim->PWM_CCR = (level < 2) ? level : 1u << level;
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

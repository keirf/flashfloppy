/*
 * speaker.c
 *
 * PC speaker/buzzer control.
 * 
 * Written & released by Keir Fraser <keir.xen@gmail.com>
 * 
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

/* JPB: PA2 */
#define gpio_spk gpioa
#define pin_spk 2

static struct timer pulse_timer;
static void pulse_timer_fn(void *unused)
{
    gpio_write_pin(gpio_spk, pin_spk, FALSE);
}

void speaker_init(void)
{
    gpio_configure_pin(gpio_spk, pin_spk, GPO_pushpull(_2MHz, FALSE));
    timer_init(&pulse_timer, pulse_timer_fn, NULL);
}

void speaker_pulse(void)
{
    unsigned int volume = ff_cfg.step_volume;
    if (!volume)
        return;
    gpio_write_pin(gpio_spk, pin_spk, TRUE);
    timer_set(&pulse_timer, time_now() + volume*volume*3);
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

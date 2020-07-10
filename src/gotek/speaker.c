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

static struct {
    volatile enum { STATE_idle, STATE_active, STATE_masked } state;
    time_t start;
    struct timer timer;
} pulse;

static void pulse_timer_fn(void *unused)
{
    switch (pulse.state) {
    case STATE_idle:
        break;
    case STATE_active:
        gpio_write_pin(gpio_spk, pin_spk, FALSE);
        pulse.state = STATE_masked;
        /* Mask to typical minimum floppy step cycle (3ms) less 10% */
        timer_set(&pulse.timer, pulse.start + time_us(2700));
        break;
    case STATE_masked:
        pulse.state = STATE_idle;
        break;
    }
}

void speaker_init(void)
{
    pulse.state = STATE_idle;
    gpio_configure_pin(gpio_spk, pin_spk, GPO_pushpull(_2MHz, FALSE));
    timer_init(&pulse.timer, pulse_timer_fn, NULL);
}

void speaker_pulse(void)
{
    unsigned int volume = ff_cfg.step_volume;
    time_t now;

    if (!volume || (pulse.state != STATE_idle))
        return;

    gpio_write_pin(gpio_spk, pin_spk, TRUE);

    now = time_now();
    pulse.state = STATE_active;
    pulse.start = now;
    timer_set(&pulse.timer, now + volume*volume*3);
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

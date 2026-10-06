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

/* Continuous low-duty-cycle buzz used for the QuickDisk spindle motor. */
static struct {
    bool_t on, level, masked;
    struct timer timer;
} motor_hum;

#define MOTOR_HUM_HZ 100

static void motor_hum_timer_fn(void *unused)
{
    unsigned int volume = ff_cfg.step_volume;
    unsigned int period = time_us(1000000 / MOTOR_HUM_HZ);
    unsigned int on_ticks = volume * volume * (TIME_MHZ / 3);
    time_t now = time_now();

    if (!motor_hum.on || motor_hum.masked || !volume) {
        gpio_write_pin(gpio_spk, pin_spk, FALSE);
        motor_hum.level = FALSE;
        return;
    }

    on_ticks = min(on_ticks, period / 2);
    motor_hum.level = !motor_hum.level;
    gpio_write_pin(gpio_spk, pin_spk, motor_hum.level);
    timer_set(&motor_hum.timer, now + (motor_hum.level
              ? max_t(unsigned int, on_ticks, 1) : period - on_ticks));
}

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
    motor_hum.on = motor_hum.level = motor_hum.masked = FALSE;
    gpio_configure_pin(gpio_spk, pin_spk, GPO_pushpull(_2MHz, FALSE));
    timer_init(&pulse.timer, pulse_timer_fn, NULL);
    timer_init(&motor_hum.timer, motor_hum_timer_fn, NULL);
}

void speaker_pulse(void)
{
    unsigned int volume = ff_cfg.step_volume;
    time_t now;

    if (!volume || motor_hum.on || (pulse.state != STATE_idle))
        return;

    gpio_write_pin(gpio_spk, pin_spk, TRUE);

    now = time_now();
    pulse.state = STATE_active;
    pulse.start = now;
    timer_set(&pulse.timer, now + volume*volume*(TIME_MHZ/3));
}

void speaker_motor(bool_t on)
{
    uint32_t oldpri = IRQ_save(TIMER_IRQ_PRI);

    motor_hum.on = on;
    timer_cancel(&motor_hum.timer);
    gpio_write_pin(gpio_spk, pin_spk, FALSE);
    motor_hum.level = FALSE;
    if (on && !motor_hum.masked && ff_cfg.step_volume)
        timer_set(&motor_hum.timer, time_now());

    IRQ_restore(oldpri);
}

static void speaker_hz(unsigned int hz, unsigned int ms)
{
    unsigned int vol = (ff_cfg.notify_volume & NOTIFY_volume_mask) + 1;
    unsigned int period = STK_MHZ * 1000000 / hz;
    unsigned int period_on = period * vol * vol / (2*400);
    unsigned int nr = hz * ms / 1000;
    while (nr--) {
        gpio_write_pin(gpio_spk, pin_spk, TRUE);
        delay_ticks(period_on);
        gpio_write_pin(gpio_spk, pin_spk, FALSE);
        delay_ticks(period - period_on);
    }
}

static void speaker_lock(void)
{
    uint32_t oldpri;
    oldpri = IRQ_save(TIMER_IRQ_PRI);
    timer_cancel(&pulse.timer);
    timer_cancel(&motor_hum.timer);
    pulse.state = STATE_masked;
    motor_hum.masked = TRUE;
    motor_hum.level = FALSE;
    gpio_write_pin(gpio_spk, pin_spk, FALSE);
    IRQ_restore(oldpri);
}

static void speaker_unlock(void)
{
    pulse.state = STATE_idle;
    motor_hum.masked = FALSE;
    if (motor_hum.on && ff_cfg.step_volume)
        timer_set(&motor_hum.timer, time_now());
}

static void speaker_notify_slot(unsigned int nr)
{
    while (nr >= 5) {
        speaker_hz(1500, 100);
        nr -= 5;
        if (nr != 0)
            delay_ms(120);
    }

    while (nr != 0) {
        speaker_hz(1500, 40);
        nr -= 1;
        if (nr != 0)
            delay_ms(120);
    }
}

void speaker_notify_insert(unsigned int slotnr)
{
    if ((ff_cfg.notify_volume & NOTIFY_volume_mask) == 0)
        return;

    speaker_lock();

    speaker_hz(880, 40); /* a5 */
    delay_ms(20);
    speaker_hz(784, 40); /* g5 */
    delay_ms(20);
    speaker_hz(1046, 60); /* c6 */

    if (ff_cfg.notify_volume & NOTIFY_slotnr) {
        delay_ms(300);
        speaker_notify_slot(slotnr);
    }

    speaker_unlock();
}

void speaker_notify_eject(void)
{
    if ((ff_cfg.notify_volume & NOTIFY_volume_mask) == 0)
        return;

    speaker_lock();

    speaker_hz(932, 40); /* a#5 */
    delay_ms(20);
    speaker_hz(831, 40); /* g#5 */
    delay_ms(20);
    speaker_hz(659, 60); /* e5 */

    speaker_unlock();
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

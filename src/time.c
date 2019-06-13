/*  
 * time.c
 * 
 * System-time abstraction over STM32 STK timer.
 * 
 * Written & released by Keir Fraser <keir.xen@gmail.com>
 * 
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

static volatile time_t time_stamp;
static struct timer time_stamp_timer;

void delay_from(time_t t, unsigned int ticks)
{
    int diff = time_diff(time_now(), t + ticks);
    if (diff > 0)
        delay_ticks(diff);
}

static void time_stamp_update(void *unused)
{
    time_t now = time_now();
    time_stamp = ~now;
    timer_set(&time_stamp_timer, now + time_ms(500));
}

time_t time_now(void)
{
    time_t s, t;
    s = time_stamp;
    t = stk_now() | (s & (0xff << 24));
    if (t > s)
        t -= 1u << 24;
    return ~t;
}

void time_init(void)
{
    timers_init();
    time_stamp = stk_now();
    timer_init(&time_stamp_timer, time_stamp_update, NULL);
    timer_set(&time_stamp_timer, time_now() + time_ms(500));
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

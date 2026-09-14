/*
 * timer.c
 * 
 * Deadline-based timer callbacks.
 * 
 * Written & released by Keir Fraser <keir.xen@gmail.com>
 * 
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

#if MCU == MCU_stm32f105
/* TIM4: IRQ 30. */
void IRQ_30(void) __attribute__((alias("IRQ_timer")));
#define TIMER_IRQ 30
#define tim tim4
#define tim_bits 16
#define TIM_CR1_MCUBITS 0
#elif MCU == MCU_at32f435
void IRQ_50(void) __attribute__((alias("IRQ_timer")));
#define TIMER_IRQ 50
#define tim tim5 /* 32-bit timer */
#define tim_bits 32
#define TIM_CR1_MCUBITS TIM_CR1_PMEN
#elif MCU == MCU_rp2350
/* TIMER0 alarm 0: IRQ 0. */
void IRQ_0(void) __attribute__((alias("IRQ_timer")));
#define TIMER_IRQ TIMER0_IRQ_0
#endif

#if MCU == MCU_rp2350

/* TIMER0 counts the 1MHz tick (== TIME_MHZ); alarms match on the low
 * 32 bits of the count. */
#define SLACK_TICKS 2

static void reprogram_timer(int32_t delta)
{
    if (delta < (SLACK_TICKS + 1))
        delta = SLACK_TICKS + 1;
    timer0->alarm[0] = timer0->timerawl + delta - SLACK_TICKS;
}

#else

/* IRQ only on counter overflow, one-time enable. */
#define TIM_CR1 (TIM_CR1_URS | TIM_CR1_OPM | TIM_CR1_MCUBITS)

/* Empirically-determined offset applied to timer deadlines to counteract the
 * latency incurred by reprogram_timer() and IRQ_timer(). */
#define SLACK_TICKS 12

static void reprogram_timer(int32_t delta)
{
    tim->cr1 = TIM_CR1;
    if ((tim_bits == 32) || (delta < 0x10000)) {
        /* Fine-grained deadline (sub-microsecond accurate) */
        tim->psc = SYSCLK_MHZ/TIME_MHZ-1;
        tim->arr = (delta <= SLACK_TICKS) ? 1 : delta-SLACK_TICKS;
    } else {
        /* Coarse-grained deadline, fires in time to set a shorter,
         * fine-grained deadline. */
        tim->psc = sysclk_us(100)-1;
        tim->arr = min_t(uint32_t, 0xffffu,
                         delta/time_us(100)-10); /* 1ms early */
    }
    tim->egr = TIM_EGR_UG; /* update CNT, PSC, ARR */
    tim->sr = 0; /* dummy write, gives hardware time to process EGR.UG=1 */
    tim->cr1 = TIM_CR1 | TIM_CR1_CEN;
}

#endif

#define TIMER_INACTIVE ((struct timer *)1ul)

static struct timer *head;

void timer_init(struct timer *timer, void (*cb_fn)(void *), void *cb_dat)
{
    timer->cb_fn = cb_fn;
    timer->cb_dat = cb_dat;
    timer->next = TIMER_INACTIVE;
}

static bool_t timer_is_active(struct timer *timer)
{
    return timer->next != TIMER_INACTIVE;
}

static void _timer_cancel(struct timer *timer)
{
    struct timer *t, **pprev;

    if (!timer_is_active(timer))
        return;

    for (pprev = &head; (t = *pprev) != timer; pprev = &t->next)
        continue;

    *pprev = t->next;
    t->next = TIMER_INACTIVE;
}

void timer_set(struct timer *timer, time_t deadline)
{
    struct timer *t, **pprev;
    time_t now;
    int32_t delta;
    uint32_t oldpri;

    oldpri = IRQ_save(TIMER_IRQ_PRI);

    _timer_cancel(timer);

    timer->deadline = deadline;

    now = time_now();
    delta = time_diff(now, deadline);
    for (pprev = &head; (t = *pprev) != NULL; pprev = &t->next)
        if (delta <= time_diff(now, t->deadline))
            break;
    timer->next = *pprev;
    *pprev = timer;

    if (head == timer)
        reprogram_timer(delta);

    IRQ_restore(oldpri);
}

void timer_cancel(struct timer *timer)
{
    uint32_t oldpri;
    oldpri = IRQ_save(TIMER_IRQ_PRI);
    _timer_cancel(timer);
    IRQ_restore(oldpri);
}

void timers_init(void)
{
#if MCU == MCU_rp2350
    timer0->pause = 0;
    timer0->inte = 1; /* alarm 0 */
#else
    tim->cr2 = 0;
    tim->dier = TIM_DIER_UIE;
#endif
    IRQx_set_prio(TIMER_IRQ, TIMER_IRQ_PRI);
    IRQx_enable(TIMER_IRQ);
}

static void IRQ_timer(void)
{
    struct timer *t;
    int32_t delta;

#if MCU == MCU_rp2350
    timer0->intr = 1; /* clear alarm 0 */
#else
    tim->sr = 0;
#endif

    while ((t = head) != NULL) {
        if ((delta = time_diff(time_now(), t->deadline)) > SLACK_TICKS) {
            reprogram_timer(delta);
            break;
        }
        head = t->next;
        t->next = TIMER_INACTIVE;
        (*t->cb_fn)(t->cb_dat);
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

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

/* TIM1_UP: IRQ 25. */
void IRQ_25(void) __attribute__((alias("IRQ_timer")));
#define TIMER_IRQ 25
#define TIMER_IRQ_PRI 3

#define tim tim1

static struct timer *head;

static int32_t get_delta(stk_time_t now, stk_time_t deadline)
{
    int32_t delta = stk_diff(now, deadline);
    if (delta & (STK_MASK^(STK_MASK>>1)))
        delta = -((delta ^ STK_MASK) + 1);
    return delta;
}

static void reprogram_timer(int32_t delta)
{
    tim->cr1 = 0;
    tim->cnt = 0;
    tim->arr = (delta <= 1) ? 1 : delta-1;
    tim->sr = 0;
    tim->cr1 = TIM_CR1_OPM | TIM_CR1_CEN;
}

void timer_set(struct timer *timer)
{
    struct timer *t, **pprev;
    stk_time_t now;
    int32_t delta;

    IRQx_disable(TIMER_IRQ);

    now = stk_now();
    delta = get_delta(now, timer->deadline);
    for (pprev = &head; (t = *pprev) != NULL; pprev = &t->next)
        if (delta <= get_delta(now, t->deadline))
            break;
    timer->next = *pprev;
    *pprev = timer;

    if (head == timer)
        reprogram_timer(delta);

    IRQx_enable(TIMER_IRQ);
}

void timers_init(void)
{
    rcc->apb2enr |= RCC_APB2ENR_TIM1EN;
    tim->psc = SYSCLK_MHZ/STK_MHZ-1;
    tim->cr2 = 0;
    tim->dier = TIM_DIER_UIE;
    IRQx_set_prio(TIMER_IRQ, TIMER_IRQ_PRI);
    IRQx_enable(TIMER_IRQ);
}

static void IRQ_timer(void)
{
    struct timer *t;
    int32_t delta;

    while ((t = head) != NULL) {
        if ((delta = get_delta(stk_now(), t->deadline)) > 0) {
            reprogram_timer(delta);
            break;
        }
        head = t->next;
        t->next = NULL;
        (*t->cb_fn)(t->cb_dat);
    }
}

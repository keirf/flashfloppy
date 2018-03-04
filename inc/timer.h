/*
 * timer.h
 * 
 * Deadline-based timer callbacks.
 * 
 * Written & released by Keir Fraser <keir.xen@gmail.com>
 * 
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

struct timer {
    time_t deadline;
    void (*cb_fn)(void *);
    void *cb_dat;
    struct timer *next;
};

/* Safe to call from any priority level same or lower than TIMER_IRQ_PRI. */
void timer_init(struct timer *timer, void (*cb_fn)(void *), void *cb_dat);
void timer_set(struct timer *timer, time_t deadline);
void timer_cancel(struct timer *timer);

void timers_init(void);

/*
 * Local variables:
 * mode: C
 * c-file-style: "Linux"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */

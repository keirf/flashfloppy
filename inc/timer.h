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
    stk_time_t deadline;
    void (*cb_fn)(void *);
    void *cb_dat;
    struct timer *next;
};

void timer_set(struct timer *timer);

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

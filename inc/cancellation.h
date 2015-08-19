/*
 * cancellation.h
 * 
 * Asynchronously-cancellable function calls.
 * 
 * Written & released by Keir Fraser <keir.xen@gmail.com>
 * 
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

struct cancellation {
    uint32_t *sp;
    bool_t cancellable;
    int (*fn)(struct cancellation *);
    int (*cancel)(struct cancellation *);
};

/* Execute c->fn(c) in a wrapped cancellable environment. */
int call_cancellable_fn(struct cancellation *c);

/* Within c->fn(c): mark execution as currently cancellable, or not. */
void enable_cancel(struct cancellation *c);
void disable_cancel(struct cancellation *c);

/* From e.g., IRQ content: stop running c->fn(c) and execute c->cancel(c). */
void cancel_call(struct cancellation *c);

/*
 * Local variables:
 * mode: C
 * c-file-style: "Linux"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */

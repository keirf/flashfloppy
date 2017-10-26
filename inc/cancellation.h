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
};

#define cancellation_is_active(c) ((c)->sp != NULL)

/* Execute fn() in a wrapped cancellable environment. */
int call_cancellable_fn(struct cancellation *c, int (*fn)(void *), void *arg);

/* From IRQ content: stop running fn() and immediately return -1. */
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

/*
 * thread.h
 * 
 * Cooperative multitasking.
 * 
 * Written & released by Keir Fraser <keir.xen@gmail.com> and Eric Anderson
 * <ejona86@gmail.com>
 * 
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

struct thread {
    /* Internal bookkeeping */
    bool_t exited;
};

/* Initialize a thread and queue it for execution. 'thread' must remain
 * allocated for the lifetime of the thread. */
void thread_start(struct thread *thread, uint32_t *stack, void (*func)(void*), void* arg);

/* Yield execution to allow other threads to run. */
void thread_yield(void);

/* Returns true if provided thread has exited. A thread cannot be joined
 * multiple times, unless it is started anew. */
bool_t thread_tryjoin(struct thread *thread);

/* Continuously yields until provided thread has exited. A thread cannot be
 * joined multiple times, unless it is started anew. */
void thread_join(struct thread *thread);

/* Reinitializes threading subsystem to its initial state, throwing away all
 * threads but the current. */
void thread_reset(void);

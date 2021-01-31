/*
 * thread.c
 * 
 * Cooperative multitasking.
 * 
 * Written & released by Keir Fraser <keir.xen@gmail.com> and Eric Anderson
 * <ejona86@gmail.com>
 * 
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

/* Holds stack pointer. */
static uint32_t *waiting_thread;

__attribute__((naked))
static void _thread_yield(uint32_t *new_stack, uint32_t **save_stack_pointer) {
    asm (
        "    stmdb sp!,{r4-r11,lr}\n"
        "    str   sp,[r1]\n"
        "    b     resume\n"
        );
}

void thread_yield(void) {
    if (!waiting_thread)
        return;
    _thread_yield(waiting_thread, &waiting_thread);
}

__attribute__((naked))
static void resume(uint32_t *stack) {
    asm (
        "    mov   sp,r0\n"
        "    isb\n"
        "    ldmfd sp!,{r4-r11,lr}\n"
        "    bx    lr\n"
        );
}

__attribute__((used))
static void thread_main(struct thread *thread, void (*func)(void*), void* arg) {
    uint32_t *other_thread;
    func(arg);
    thread->exited = TRUE;

    other_thread = waiting_thread;
    waiting_thread = 0;
    resume(other_thread);
    ASSERT(0); /* unreachable */
}

__attribute__((naked))
static void _main(void) {
    asm (
        "    mov r0,r4\n"
        "    mov r1,r5\n"
        "    mov r2,r6\n"
        "    b   thread_main\n"
        );
}

void thread_start(struct thread *thread, uint32_t *stack, void (*func)(void*), void* arg) {
    memset(thread, 0, sizeof(*thread));
    ASSERT(!waiting_thread);
    {
        /* r3 isn't special; it is just "not r4-r11,lr" */
        register uint32_t *stack_asm asm ("r3") = stack;
        register struct thread *thread_asm asm ("r4") = thread;
        register void (*func_asm)(void*) asm ("r5") = func;
        register void *arg_asm asm ("r6") = arg;
        register void *_ret asm ("lr") = _main;
        asm volatile (
            "    stmdb %0!,{r4-r11,lr}\n" /* Fake thread_yield storage */
            : "+r" (stack_asm)
            : "r" (thread_asm), "r" (func_asm), "r" (_ret), "r" (arg_asm)
            : "memory");
        stack = stack_asm;
    }
    waiting_thread = stack;
}

bool_t thread_tryjoin(struct thread *thread) {
    bool_t exited = thread->exited;
    thread->exited = FALSE;
    return exited;
}

void thread_join(struct thread *thread) {
    while (!thread->exited)
        thread_yield();
}

void thread_reset() {
    waiting_thread = NULL;
}

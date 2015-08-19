/*
 * cancellation.c
 * 
 * Asynchronously-cancellable function calls.
 * 
 * Written & released by Keir Fraser <keir.xen@gmail.com>
 * 
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

asm (
    ".global call_cancellable_fn\n"
    ".thumb_func \n"
    "call_cancellable_fn:\n"
    "    stmdb.w sp!, {r0, r4, r5, r6, r7, r8, r9, r10, r11, lr}\n"
    "    str     sp, [r0]\n" /* c->sp = PSP */
    "    blx     r1\n"       /* (*fn)() */
    "    ldr     r2, [sp]\n"
    "    mov     r1, #0\n"
    "    str     r1, [r2]\n" /* c->sp = NULL */
    "do_cancel:\n"
    "    add     sp, #4\n"
    "    ldmia.w sp!, {r4, r5, r6, r7, r8, r9, r10, r11, pc}\n"
    );

void do_cancel(void);

void cancel_call(struct cancellation *c)
{
    struct exception_frame *frame;
    uint32_t *new_frame;

    if (c->sp == NULL)
        return;

    /* Modify return frame: Jump to exit of call_cancellable_fn() with
     * return code -1 and clean xPSR. */
    frame = (struct exception_frame *)read_special(psp);
    frame->r0 = -1;
    frame->pc = (uint32_t)do_cancel;
    frame->psr &= 1u<<24; /* Preserve Thumb mode; clear everything else */

    /* Find new frame address, set STKALIGN if misaligned. */
    new_frame = c->sp - 8;
    if ((uint32_t)new_frame & 4) {
        new_frame--;
        frame->psr |= 1u<<9;
    }

    /* Copy the stack frame and update Process SP. */
    memmove(new_frame, frame, 32);
    write_special(psp, new_frame);

    /* Do this work at most once per invocation of call_cancellable_fn. */
    c->sp = NULL;
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

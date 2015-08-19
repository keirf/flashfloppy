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
    "    str     sp, [r0]\n"
    "    ldr     r1, [r0, #8]\n"    
    "1:  blx     r1\n"
    "    add     sp,#4\n"
    "    ldmia.w sp!, {r4, r5, r6, r7, r8, r9, r10, r11, pc}\n"
    "\n"
    ".global exec_cancellation\n"
    ".thumb_func \n"
    "exec_cancellation:\n"
    "    ldr     r0, [sp]\n"
    "    ldr     r1, [r0, #12]\n"
    "    b       1b\n"
    );

void exec_cancellation(void);

void enable_cancel(struct cancellation *c)
{
    c->cancellable = TRUE;
}

void disable_cancel(struct cancellation *c)
{
    c->cancellable = FALSE;
}

void cancel_call(struct cancellation *c)
{
    struct exception_frame *frame;
    uint32_t *new_frame;

    if (!c->cancellable)
        return;
    c->cancellable = FALSE;

    /* Update frame to point at cancellation PC, and clear STKALIGN. */
    frame = (struct exception_frame *)read_special(psp);
    frame->pc = (uint32_t)exec_cancellation;
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

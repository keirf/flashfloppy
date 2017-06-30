    
  .syntax unified

  .section .vector_table

.global vector_table
vector_table:
    /* Top of stack */
    .word _thread_stacktop

    /* Exceptions (1-15) */
#define E(x) .word x; .weak x; .thumb_set x, EXC_unused;
    E(EXC_reset)
    E(EXC_nmi)
    E(EXC_hard_fault)
    E(EXC_memory_management_fault)
    E(EXC_bus_fault)
    E(EXC_usage_fault)
    E(EXC_7)
    E(EXC_8)
    E(EXC_9)
    E(EXC_10)
    E(EXC_sv_call)
    E(EXC_12)
    E(EXC_13)
    E(EXC_pend_sv)
    E(EXC_systick)

    /* Interrupts/IRQs (0-67) */
#define I(n) E(IRQ_##n)
    I( 0) I( 1) I( 2) I( 3) I( 4) I( 5) I( 6) I( 7) I( 8) I( 9)
    I(10) I(11) I(12) I(13) I(14) I(15) I(16) I(17) I(18) I(19)
    I(20) I(21) I(22) I(23) I(24) I(25) I(26) I(27) I(28) I(29)
    I(30) I(31) I(32) I(33) I(34) I(35) I(36) I(37) I(38) I(39)
    I(40) I(41) I(42) I(43) I(44) I(45) I(46) I(47) I(48) I(49)
    I(50) I(51) I(52) I(53) I(54) I(55) I(56) I(57) I(58) I(59)
    I(60) I(61) I(62) I(63) I(64) I(65) I(66) I(67)

    .text
    .thumb_func
.global EXC_unused
EXC_unused:
    push {r4, r5, r6, r7, r8, r9, r10, r11, lr}
    mov  r0, sp
    b    EXC_unexpected

/*
 * intrinsics.h
 * 
 * Compiler intrinsics for ARMv7-M core.
 * 
 * Written & released by Keir Fraser <keir.xen@gmail.com>
 * 
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

struct exception_frame {
    uint32_t r0, r1, r2, r3, r12, lr, pc, psr;
};

#define illegal() asm volatile (".short 0xde00");

#define barrier() asm volatile ("" ::: "memory")
#define cpu_relax() asm volatile ("nop" ::: "memory")

#define sv_call(imm) asm volatile ( "svc %0" : : "i" (imm) )

#define read_special(reg) ({                        \
    uint32_t __x;                                   \
    asm volatile ("mrs %0,"#reg : "=r" (__x) ::);   \
    __x;                                            \
})

#define write_special(reg,val) ({                   \
    uint32_t __x = (uint32_t)(val);                 \
    asm volatile ("msr "#reg",%0" :: "r" (__x) :);  \
})

/* CONTROL[1] == 0 => running on Master Stack (Exception Handler mode). */
#define in_exception() (!(read_special(control) & 2))

#define IRQ_global_disable() asm volatile ("cpsid i" ::: "memory")
#define IRQ_global_enable() asm volatile ("cpsie i" ::: "memory")

/* Save/restore IRQ priority levels. */
#define IRQ_save(newpri) ({                         \
        uint8_t __newpri = (newpri)<<4;             \
        uint8_t __oldpri = read_special(basepri);   \
        if (!__oldpri || (__oldpri > __newpri))     \
            write_special(basepri, __newpri);       \
        __oldpri; })
#define IRQ_restore(oldpri) write_special(basepri, (oldpri))

static inline uint16_t _rev16(uint16_t x)
{
    uint16_t result;
    asm volatile ("rev16 %0,%1" : "=r" (result) : "r" (x));
    return result;
}

static inline uint32_t _rev32(uint32_t x)
{
    uint32_t result;
    asm volatile ("rev %0,%1" : "=r" (result) : "r" (x));
    return result;
}

static inline uint32_t _rbit32(uint32_t x)
{
    uint32_t result;
    asm volatile ("rbit %0,%1" : "=r" (result) : "r" (x));
    return result;
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

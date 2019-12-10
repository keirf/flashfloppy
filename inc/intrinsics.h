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

#define _STR(x) #x
#define STR(x) _STR(x)

/* Force a compilation error if condition is true */
#define BUILD_BUG_ON(cond) ({ _Static_assert(!(cond), "!(" #cond ")"); })

#define aligned(x) __attribute__((aligned(x)))
#define packed __attribute((packed))
#define always_inline __inline__ __attribute__((always_inline))
#define noinline __attribute__((noinline))

#define likely(x)     __builtin_expect(!!(x),1)
#define unlikely(x)   __builtin_expect(!!(x),0)

#define illegal() asm volatile (".short 0xde00");

#define barrier() asm volatile ("" ::: "memory")
#define cpu_sync() asm volatile("dsb; isb" ::: "memory")
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
#define CONTROL_SPSEL 2
#define in_exception() (!(read_special(control) & CONTROL_SPSEL))

#define global_disable_exceptions() \
    asm volatile ("cpsid f; cpsid i" ::: "memory")
#define global_enable_exceptions() \
    asm volatile ("cpsie f; cpsie i" ::: "memory")

/* NB. IRQ disable via CPSID/MSR is self-synchronising. No barrier needed. */
#define IRQ_global_disable() asm volatile ("cpsid i" ::: "memory")
#define IRQ_global_enable() asm volatile ("cpsie i" ::: "memory")

/* Save/restore IRQ priority levels. 
 * NB. IRQ disable via MSR is self-synchronising. I have confirmed this on 
 * Cortex-M3: any pending IRQs are handled before they are disabled by 
 * a BASEPRI update. Hence no barrier is needed here. */
#define IRQ_save(newpri) ({                         \
        uint8_t __newpri = (newpri)<<4;             \
        uint8_t __oldpri = read_special(basepri);   \
        if (!__oldpri || (__oldpri > __newpri))     \
            write_special(basepri, __newpri);       \
        __oldpri; })
/* NB. Same as CPSIE, any pending IRQ enabled by this BASEPRI update may 
 * execute a couple of instructions after the MSR instruction. This has been
 * confirmed on Cortex-M3. */
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

extern void __bad_cmpxchg(volatile void *ptr, int size);

static always_inline unsigned long __cmpxchg(
    volatile void *ptr, unsigned long old, unsigned long new, int size)
{
    unsigned long oldval, res;

    switch (size) {
    case 1:
        do {
            asm volatile("    ldrexb %1,[%2]      \n"
                         "    movs   %0,#0        \n"
                         "    cmp    %1,%3        \n"
                         "    it     eq           \n"
                         "    strexbeq %0,%4,[%2] \n"
                         : "=&r" (res), "=&r" (oldval)
                         : "r" (ptr), "Ir" (old), "r" (new)
                         : "memory", "cc");
        } while (res);
        break;
    case 2:
        do {
            asm volatile("    ldrexh %1,[%2]      \n"
                         "    movs   %0,#0        \n"
                         "    cmp    %1,%3        \n"
                         "    it     eq           \n"
                         "    strexheq %0,%4,[%2] \n"
                         : "=&r" (res), "=&r" (oldval)
                         : "r" (ptr), "Ir" (old), "r" (new)
                         : "memory", "cc");
        } while (res);
        break;
    case 4:
        do {
            asm volatile("    ldrex  %1,[%2]      \n"
                         "    movs   %0,#0        \n"
                         "    cmp    %1,%3        \n"
                         "    it     eq           \n"
                         "    strexeq %0,%4,[%2]  \n"
                         : "=&r" (res), "=&r" (oldval)
                         : "r" (ptr), "Ir" (old), "r" (new)
                         : "memory", "cc");
        } while (res);
        break;
    default:
        __bad_cmpxchg(ptr, size);
        oldval = 0;
    }

    return oldval;
}

#define cmpxchg(ptr,o,n)                                \
    ((__typeof__(*(ptr)))__cmpxchg((ptr),               \
                                   (unsigned long)(o),  \
                                   (unsigned long)(n),  \
                                   sizeof(*(ptr))))

/*
 * Local variables:
 * mode: C
 * c-file-style: "Linux"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */

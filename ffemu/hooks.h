/*
 * hooks.h
 *
 * Host-build counterpart of inc/intrinsics.h, plus overrides of the few
 * hardware-access macros that ffemu must see happen: interrupt control and
 * the SysTick counter.
 *
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

#include "emu.h"

#define BUILD_BUG_ON(cond) ({ _Static_assert(!(cond), "!(" #cond ")"); })

#define aligned(x) __attribute__((aligned(x)))
#define packed __attribute((packed))
#define always_inline __inline__ __attribute__((always_inline))
#define noinline __attribute__((noinline))

#define likely(x)     __builtin_expect(!!(x),1)
#define unlikely(x)   __builtin_expect(!!(x),0)

#define illegal() emu_illegal(__FILE__, __LINE__)

#define barrier() asm volatile ("" ::: "memory")
#define cpu_sync() barrier()
#define cpu_relax() emu_relax()

#define in_exception() emu_in_exception()

#define IRQ_global_disable() emu_irq_global(0)
#define IRQ_global_enable() emu_irq_global(1)

#define IRQ_save(newpri) emu_irq_save(newpri)
#define IRQ_restore(oldpri) emu_irq_restore(oldpri)

#define __DEFINE_IRQ(nr, name) \
void IRQ_##nr (void) __attribute__((alias(name)))
#define _DEFINE_IRQ(nr, name) __DEFINE_IRQ(nr, name)
#define DEFINE_IRQ(nr, name) _DEFINE_IRQ(nr, name)

void cortex_init(void);

static inline uint16_t _rev16(uint16_t x)
{
    return __builtin_bswap16(x);
}

static inline uint32_t _rev32(uint32_t x)
{
    return __builtin_bswap32(x);
}

static inline uint32_t _rbit32(uint32_t x)
{
    uint32_t y = 0;
    unsigned int i;
    for (i = 0; i < 32; i++, x >>= 1)
        y = (y << 1) | (x & 1);
    return y;
}

#define cmpxchg(ptr,o,n) __sync_val_compare_and_swap(ptr, o, n)

/* Interrupt controller: route to the emulated one. */
#undef IRQx_enable
#undef IRQx_disable
#undef IRQx_is_enabled
#undef IRQx_set_pending
#undef IRQx_clear_pending
#undef IRQx_is_pending
#undef IRQx_set_prio
#undef IRQx_get_prio
#define IRQx_enable(x) emu_irqx_enable(x)
#define IRQx_disable(x) emu_irqx_disable(x)
#define IRQx_is_enabled(x) emu_irqx_is_enabled(x)
#define IRQx_set_pending(x) emu_irqx_set_pending(x)
#define IRQx_clear_pending(x) emu_irqx_clear_pending(x)
#define IRQx_is_pending(x) emu_irqx_is_pending(x)
#define IRQx_set_prio(x,y) emu_irqx_set_prio(x,y)
#define IRQx_get_prio(x) emu_irqx_get_prio(x)

/* SysTick: a free-running counter, which plain memory cannot be. Reading it
 * is also where busy-wait loops let the peripheral models run. */
uint32_t emu_stk_now(void);
#undef stk_now
#define stk_now() emu_stk_now()

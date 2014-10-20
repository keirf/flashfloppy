/*
 * intrinsics.h
 * 
 * Compiler intrinsics for ARMv7-M core.
 * 
 * Written & released by Keir Fraser <keir.xen@gmail.com>
 * 
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit unlicense.org.
 */

#ifndef __INTRINSICS_H__
#define __INTRINSICS_H__

#define cpu_relax() asm volatile ("nop" ::: "memory")
#define irq_disable() asm volatile ("cpsid i" ::: "memory")
#define irq_enable() asm volatile ("cpsie i" ::: "memory")

#endif /* __INTRINSICS_H__ */

/*
 * Local variables:
 * mode: C
 * c-file-style: "Linux"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */

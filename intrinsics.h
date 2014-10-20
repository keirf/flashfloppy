/*
 * intrinsics.h
 * 
 * Compiler intrinsics for ARMv7-M core.
 */

#ifndef __INTRINSICS_H__
#define __INTRINSICS_H__

#define cpu_relax() asm volatile ("nop" ::: "memory")
#define irq_disable() asm volatile ("cpsid i" ::: "memory")
#define irq_enable() asm volatile ("cpsie i" ::: "memory")

#endif /* __INTRINSICS_H__ */

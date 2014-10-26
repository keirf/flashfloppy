/*
 * util.h
 * 
 * Utility definitions.
 * 
 * Written & released by Keir Fraser <keir.xen@gmail.com>
 * 
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

#ifndef __UTIL_H__
#define __UTIL_H__

#include <stdarg.h>
#include <stddef.h>

#include "intrinsics.h"

#define ASSERT(p) if (!(p)) illegal();

typedef char bool_t;
#define TRUE 1
#define FALSE 0

void *memset(void *s, int c, size_t n);
void *memcpy(void *dest, const void *src, size_t n);

int vsnprintf(char *str, size_t size, const char *format, va_list ap)
    __attribute__ ((format (printf, 3, 0)));

int vprintk(const char *format, va_list ap)
    __attribute__ ((format (printf, 1, 0)));

int printk(const char *format, ...)
    __attribute__ ((format (printf, 1, 2)));

void console_init(void);

void leds_init(void);
void leds_write_hex(unsigned int x);

/* Text/data/BSS address ranges. */
extern char _stext[], _etext[];
extern char _sdat[], _edat[], _ldat[];
extern char _sbss[], _ebss[];

#endif /* __UTIL_H__ */

/*
 * Local variables:
 * mode: C
 * c-file-style: "Linux"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */

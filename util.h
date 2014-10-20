/*
 * util.h
 * 
 * Utility definitions.
 */

#ifndef __UTIL_H__
#define __UTIL_H__

#include <stdarg.h>
#include <stddef.h>

int vsnprintf(char *str, size_t size, const char *format, va_list ap)
    __attribute__ ((format (printf, 3, 0)));

int vprintk(const char *format, va_list ap)
    __attribute__ ((format (printf, 1, 0)));

int printk(const char *format, ...)
    __attribute__ ((format (printf, 1, 2)));

#endif /* __UTIL_H__ */

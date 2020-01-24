/*
 * stdarg.h
 * 
 * variable arguments
 * 
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

#ifndef STDARG_H
#define STDARG_H

typedef __builtin_va_list	va_list;
#define va_arg(v, l)		__builtin_va_arg(v, l)
#define va_start(v, l)		__builtin_va_start(v, l)
#define va_end(v)		__builtin_va_end(v)

#endif

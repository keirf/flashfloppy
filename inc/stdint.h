/*
 * stdint.h
 * 
 * standard integer types
 * 
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

#ifndef STDINT_H
#define STDINT_H

typedef signed char	int8_t;
typedef unsigned char	uint8_t;

typedef signed short	int16_t;
typedef unsigned short	uint16_t;

typedef signed int	int32_t;
typedef unsigned int	uint32_t;

typedef signed long long	int64_t;
typedef unsigned long long	uint64_t;

typedef signed long	intptr_t;
typedef unsigned long	uintptr_t;

typedef int64_t		intmax_t;
typedef uint64_t	uintmax_t;

#endif /* STDINT_H */

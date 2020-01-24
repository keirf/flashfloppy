/*
 * limits.h
 * 
 * integer limits
 * 
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

#ifndef LIMITS_H
#define LIMITS_H

#define INT8_MIN   ((int8_t)0x80)
#define INT8_MAX   ((int8_t)0x7F)
#define UINT8_MAX  ((uint8_t)0xFF)

#define INT16_MIN  ((int16_t)0x8000)
#define INT16_MAX  ((int16_t)0x7FFF)
#define UINT16_MAX ((uint16_t)0xFFFF)

#define INT32_MIN  ((int32_t)0x80000000)
#define INT32_MAX  ((int32_t)0x7FFFFFFF)
#define UINT32_MAX ((uint32_t)0xFFFFFFFF)

#define INT64_MIN  ((int64_t)0x8000000000000000)
#define INT64_MAX  ((int64_t)0x7FFFFFFFFFFFFFFF)
#define UINT64_MAX ((uint64_t)0xFFFFFFFFFFFFFFFF)

#define INT_MIN INT32_MIN
#define INT_MAX INT32_MAX

#endif

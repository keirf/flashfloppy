/*
 * decls.h
 *
 * Host-build counterpart of inc/decls.h: pulled into every source file of the
 * firmware half of ffemu. Keep the list of headers in step with the original.
 *
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

#include <stdint.h>
#include <stdarg.h>
#include <stddef.h>
#include <limits.h>

/* The firmware carries its own subset of the C library, with differences.
 * Keep it apart from the host C library that the host half is linked to. */
#define memset ff_memset
#define memcpy ff_memcpy
#define memmove ff_memmove
#define memcmp ff_memcmp
#define strlen ff_strlen
#define strnlen ff_strnlen
#define strcmp ff_strcmp
#define strncmp ff_strncmp
#define strcpy ff_strcpy
#define strchr ff_strchr
#define strrchr ff_strrchr
#define tolower ff_tolower
#define toupper ff_toupper
#define isspace ff_isspace
#define strtol ff_strtol
#define rand ff_rand
#define vsnprintf ff_vsnprintf
#define snprintf ff_snprintf

#include "build_enums.h"
#include "types.h"
#include "mcu/common_regs.h"
#include "mcu/common.h"
#include "mcu/stm32f105_regs.h"
#include "regs.h"
#include "mcu/at32f415_regs.h"
#include "mcu/stm32f105.h"
#include "hooks.h"

#include "time.h"
#include "../src/fatfs/ff.h"
#include "util.h"
#include "list.h"
#include "cache.h"
#include "da.h"
#include "hxc.h"
#include "cancellation.h"
#include "spi.h"
#include "timer.h"
#include "fs.h"
#include "floppy.h"
#include "volume.h"
#include "config.h"

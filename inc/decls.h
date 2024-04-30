/*
 * decls.h
 * 
 * Pull in all other header files in an orderly fashion. Source files include
 * only this header, and only once.
 * 
 * Written & released by Keir Fraser <keir.xen@gmail.com>
 * 
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

#define CONFIG_SWD 1

#include <stdint.h>
#include <stdarg.h>
#include <stddef.h>
#include <limits.h>

#include "types.h"
#include "mcu/common_regs.h"
#include "mcu/common.h"
#if MCU == STM32F105
#include "mcu/stm32f105_regs.h"
#include "mcu/at32f415_regs.h"
#include "mcu/stm32f105.h"
#elif MCU == AT32F435
#include "mcu/at32f435_regs.h"
#include "mcu/at32f435.h"
#endif
#include "intrinsics.h"

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

/*
 * Local variables:
 * mode: C
 * c-file-style: "Linux"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */

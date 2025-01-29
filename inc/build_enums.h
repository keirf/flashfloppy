/*
 * build_enums.h
 * 
 * CPP build-configuration enumeration values. Note that the concrete
 * values are unimportant.
 * 
 * Written & released by Keir Fraser <keir.xen@gmail.com>
 * 
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

/* MCU */
#define MCU_stm32f105  1
#define MCU_at32f435   4

/* TARGET */
#define TARGET_bootloader 1
#define TARGET_bl_update  2
#define TARGET_io_test    3
#define TARGET_shugart    4
#define TARGET_apple2     5
#define TARGET_quickdisk  6

/* LEVEL */
#define LEVEL_prod     1
#define LEVEL_debug    2
#define LEVEL_logfile  3

/*
 * Local variables:
 * mode: C
 * c-file-style: "Linux"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */

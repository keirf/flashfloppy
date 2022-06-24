
#if defined(BOOTLOADER)
#define FLASH_BASE 0x08000000

#elif MCU == STM32F105
#define FLASH_BASE 0x08008000

#elif MCU == AT32F435
#define FLASH_BASE 0x0800c000
#endif

/* Flash overflow is checked by scripts/check_hex.py
 * Just define a big enough value here. */
#define FLASH_LEN  256K

#define RAM_BASE   0x20000000
#define RAM_LEN    16K

#include "../scripts/stm32.ld.S"

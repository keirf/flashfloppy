
#if MCU == STM32F105

#define FLASH_BASE 0x08008000
#define FLASH_LEN  94K

#elif MCU == AT32F435

#define FLASH_BASE 0x0800c000
#define FLASH_LEN  206K

#endif

#define RAM_BASE   0x20000000
#define RAM_LEN    16K

#include "../scripts/stm32.ld.S"

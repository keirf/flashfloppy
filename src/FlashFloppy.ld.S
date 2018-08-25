#define FLASH_BASE 0x08008000
#define FLASH_LEN  FLASH_MEM_SIZE-32K
/* #define FLASH_LEN  FLASH_MEM_SIZE-32K-2K */

#define RAM_BASE   0x20000000
#define RAM_LEN    64K

#include "../scripts/stm32f10x.ld.S"

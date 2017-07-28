#define FLASH_BASE 0x08008000
#define FLASH_LEN  96K

#define RAM_BASE   0x20000000
#if BUILD_GOTEK
#define RAM_LEN    64K
#elif BUILD_TOUCH
#define RAM_LEN    20K
#endif

#include "../scripts/stm32f10x.ld.S"

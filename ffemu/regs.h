/*
 * regs.h
 *
 * Relocates the MCU register blocks to ordinary host memory. The firmware
 * reads and writes them as before; ffemu models the peripherals behind them.
 *
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

extern struct stk emu_stk;
extern struct scb emu_scb;
extern struct nvic emu_nvic;
extern struct dbg emu_dbg;
extern struct flash emu_flash;
extern struct pwr emu_pwr;
extern struct bkp emu_bkp;
extern struct rcc emu_rcc;
extern struct gpio emu_gpio[7];
extern struct afio emu_afio;
extern struct exti emu_exti;
extern struct dma emu_dma[2];
extern struct tim emu_tim[7];
extern struct spi emu_spi[3];
extern struct i2c emu_i2c[2];
extern struct usart emu_usart[3];
extern struct usb_otg emu_usb_otg;

#define EMU_BASE(x) ((uintptr_t)&(x))

#undef STK_BASE
#undef SCB_BASE
#undef NVIC_BASE
#undef DBG_BASE
#undef FLASH_BASE
#undef PWR_BASE
#undef BKP_BASE
#undef RCC_BASE
#undef GPIOA_BASE
#undef GPIOB_BASE
#undef GPIOC_BASE
#undef GPIOD_BASE
#undef GPIOE_BASE
#undef GPIOF_BASE
#undef GPIOG_BASE
#undef AFIO_BASE
#undef EXTI_BASE
#undef DMA1_BASE
#undef DMA2_BASE
#undef TIM1_BASE
#undef TIM2_BASE
#undef TIM3_BASE
#undef TIM4_BASE
#undef TIM5_BASE
#undef TIM6_BASE
#undef TIM7_BASE
#undef SPI1_BASE
#undef SPI2_BASE
#undef SPI3_BASE
#undef I2C1_BASE
#undef I2C2_BASE
#undef USART1_BASE
#undef USART2_BASE
#undef USART3_BASE
#undef USB_OTG_BASE

#define STK_BASE     EMU_BASE(emu_stk)
#define SCB_BASE     EMU_BASE(emu_scb)
#define NVIC_BASE    EMU_BASE(emu_nvic)
#define DBG_BASE     EMU_BASE(emu_dbg)
#define FLASH_BASE   EMU_BASE(emu_flash)
#define PWR_BASE     EMU_BASE(emu_pwr)
#define BKP_BASE     EMU_BASE(emu_bkp)
#define RCC_BASE     EMU_BASE(emu_rcc)
#define GPIOA_BASE   EMU_BASE(emu_gpio[0])
#define GPIOB_BASE   EMU_BASE(emu_gpio[1])
#define GPIOC_BASE   EMU_BASE(emu_gpio[2])
#define GPIOD_BASE   EMU_BASE(emu_gpio[3])
#define GPIOE_BASE   EMU_BASE(emu_gpio[4])
#define GPIOF_BASE   EMU_BASE(emu_gpio[5])
#define GPIOG_BASE   EMU_BASE(emu_gpio[6])
#define AFIO_BASE    EMU_BASE(emu_afio)
#define EXTI_BASE    EMU_BASE(emu_exti)
#define DMA1_BASE    EMU_BASE(emu_dma[0])
#define DMA2_BASE    EMU_BASE(emu_dma[1])
#define TIM1_BASE    EMU_BASE(emu_tim[0])
#define TIM2_BASE    EMU_BASE(emu_tim[1])
#define TIM3_BASE    EMU_BASE(emu_tim[2])
#define TIM4_BASE    EMU_BASE(emu_tim[3])
#define TIM5_BASE    EMU_BASE(emu_tim[4])
#define TIM6_BASE    EMU_BASE(emu_tim[5])
#define TIM7_BASE    EMU_BASE(emu_tim[6])
#define SPI1_BASE    EMU_BASE(emu_spi[0])
#define SPI2_BASE    EMU_BASE(emu_spi[1])
#define SPI3_BASE    EMU_BASE(emu_spi[2])
#define I2C1_BASE    EMU_BASE(emu_i2c[0])
#define I2C2_BASE    EMU_BASE(emu_i2c[1])
#define USART1_BASE  EMU_BASE(emu_usart[0])
#define USART2_BASE  EMU_BASE(emu_usart[1])
#define USART3_BASE  EMU_BASE(emu_usart[2])
#define USB_OTG_BASE EMU_BASE(emu_usb_otg)

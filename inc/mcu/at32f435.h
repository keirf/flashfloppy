/*
 * at32f435.h
 * 
 * Core and peripheral registers.
 * 
 * Written & released by Keir Fraser <keir.xen@gmail.com>
 * 
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

/* C pointer types */
#define FLASH_BANK volatile struct flash_bank * const
#define SYSCFG volatile struct syscfg * const
#define DMAMUX volatile struct dmamux * const

/* C-accessible registers. */
static STK stk = (struct stk *)STK_BASE;
static SCB scb = (struct scb *)SCB_BASE;
static NVIC nvic = (struct nvic *)NVIC_BASE;
static DBG dbg = (struct dbg *)DBG_BASE;
static FLASH flash = (struct flash *)FLASH_BASE;
static PWR pwr = (struct pwr *)PWR_BASE;
static BKP bkp = (struct bkp *)BKP_BASE;
static RCC rcc = (struct rcc *)RCC_BASE;
static GPIO gpioa = (struct gpio *)GPIOA_BASE;
static GPIO gpiob = (struct gpio *)GPIOB_BASE;
static GPIO gpioc = (struct gpio *)GPIOC_BASE;
static GPIO gpiod = (struct gpio *)GPIOD_BASE;
static GPIO gpioe = (struct gpio *)GPIOE_BASE;
static GPIO gpiof = (struct gpio *)GPIOF_BASE;
static GPIO gpiog = (struct gpio *)GPIOG_BASE;
static GPIO gpioh = (struct gpio *)GPIOH_BASE;
static SYSCFG syscfg = (struct syscfg *)SYSCFG_BASE;
static EXTI exti = (struct exti *)EXTI_BASE;
static DMA dma1 = (struct dma *)DMA1_BASE;
static DMA dma2 = (struct dma *)DMA2_BASE;
static DMAMUX dmamux1 = (struct dmamux *)DMAMUX1_BASE;
static DMAMUX dmamux2 = (struct dmamux *)DMAMUX2_BASE;
static TIM tim1 = (struct tim *)TIM1_BASE;
static TIM tim2 = (struct tim *)TIM2_BASE;
static TIM tim3 = (struct tim *)TIM3_BASE;
static TIM tim4 = (struct tim *)TIM4_BASE;
static TIM tim5 = (struct tim *)TIM5_BASE;
static TIM tim6 = (struct tim *)TIM6_BASE;
static TIM tim7 = (struct tim *)TIM7_BASE;
static SPI spi1 = (struct spi *)SPI1_BASE;
static SPI spi2 = (struct spi *)SPI2_BASE;
static SPI spi3 = (struct spi *)SPI3_BASE;
static I2C i2c1 = (struct i2c *)I2C1_BASE;
static I2C i2c2 = (struct i2c *)I2C2_BASE;
static USART usart1 = (struct usart *)USART1_BASE;
static USART usart2 = (struct usart *)USART2_BASE;
static USART usart3 = (struct usart *)USART3_BASE;
static USB_OTG usb_otg = (struct usb_otg *)USB_OTG_BASE;

/* Clocks */
#define SYSCLK_MHZ 288
#define AHB_MHZ    288
#define APB1_MHZ   144
#define APB2_MHZ   144

/* GPIO */
void gpio_set_af(GPIO gpio, unsigned int pin, unsigned int af);

#define SOFTIRQ_0 85
#define SOFTIRQ_1 86

extern volatile uint32_t _reset_flag;
#define RESET_FLAG_BOOTLOADER 0xdeadbeefu

/*
 * Local variables:
 * mode: C
 * c-file-style: "Linux"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */

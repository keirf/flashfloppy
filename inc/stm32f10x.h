/*
 * stm32f10x.h
 * 
 * Core and peripheral registers.
 * 
 * Written & released by Keir Fraser <keir.xen@gmail.com>
 * 
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

void exception_init(void);
void system_reset(void);

/* Clocks */
#define SYSCLK_MHZ 72
#define SYSCLK     (SYSCLK_MHZ * 1000000)
void clock_init(void);

/* SysTick Timer */
#define STK_MHZ    (SYSCLK_MHZ / 8)
void delay_ticks(unsigned int ticks);
void delay_ns(unsigned int ns);
void delay_us(unsigned int us);
void delay_ms(unsigned int ms);
#define stk_us(x) ((x) * STK_MHZ)
#define stk_ms(x) stk_us((x) * 1000)

/* NVIC */
#define IRQx_enable(x) do {                     \
    barrier();                                  \
    nvic->iser[(x)>>5] = 1u<<((x)&31);          \
} while (0)
#define IRQx_disable(x) do {                    \
    nvic->icer[(x)>>5] = 1u<<((x)&31);          \
    barrier();                                  \
} while (0)
#define IRQx_is_enabled(x) ((nvic->iser[(x)>>5]>>((x)&31))&1)
#define IRQx_set_pending(x) (nvic->ispr[(x)>>5] = 1u<<((x)&31))
#define IRQx_clear_pending(x) (nvic->icpr[(x)>>5] = 1u<<((x)&31))
#define IRQx_is_pending(x) ((nvic->ispr[(x)>>5]>>((x)&31))&1)
#define IRQx_set_prio(x,y) (nvic->ipr[x] = (y) << 4)
#define IRQx_get_prio(x) (nvic->ipr[x] >> 4)

/* GPIO */
void gpio_configure_pin(
    volatile struct gpio * const gpio,
    unsigned int pin, unsigned int mode);
#define gpio_write_pin(gpio, pin, level) \
    ((gpio)->bsrr = ((level) ? 0x1u : 0x10000u) << (pin))
#define gpio_read_pin(gpio, pin) (((gpio)->idr >> (pin)) & 1)

/* C-accessible registers. */
static volatile struct stk * const stk = (struct stk *)STK_BASE;
static volatile struct scb * const scb = (struct scb *)SCB_BASE;
static volatile struct nvic * const nvic = (struct nvic *)NVIC_BASE;
static volatile struct flash * const flash = (struct flash *)FLASH_BASE;
static volatile struct pwr * const pwr = (struct pwr *)PWR_BASE;
static volatile struct rcc * const rcc = (struct rcc *)RCC_BASE;
static volatile struct gpio * const gpioa = (struct gpio *)GPIOA_BASE;
static volatile struct gpio * const gpiob = (struct gpio *)GPIOB_BASE;
static volatile struct gpio * const gpioc = (struct gpio *)GPIOC_BASE;
static volatile struct gpio * const gpiod = (struct gpio *)GPIOD_BASE;
static volatile struct gpio * const gpioe = (struct gpio *)GPIOE_BASE;
static volatile struct gpio * const gpiof = (struct gpio *)GPIOF_BASE;
static volatile struct gpio * const gpiog = (struct gpio *)GPIOG_BASE;
static volatile struct afio * const afio = (struct afio *)AFIO_BASE;
static volatile struct dma * const dma1 = (struct dma *)DMA1_BASE;
static volatile struct dma * const dma2 = (struct dma *)DMA2_BASE;
static volatile struct tim * const tim1 = (struct tim *)TIM1_BASE;
static volatile struct tim * const tim2 = (struct tim *)TIM2_BASE;
static volatile struct tim * const tim3 = (struct tim *)TIM3_BASE;
static volatile struct tim * const tim4 = (struct tim *)TIM4_BASE;
static volatile struct tim * const tim5 = (struct tim *)TIM5_BASE;
static volatile struct tim * const tim6 = (struct tim *)TIM6_BASE;
static volatile struct tim * const tim7 = (struct tim *)TIM7_BASE;
static volatile struct spi * const spi1 = (struct spi *)SPI1_BASE;
static volatile struct spi * const spi2 = (struct spi *)SPI2_BASE;
static volatile struct spi * const spi3 = (struct spi *)SPI3_BASE;
static volatile struct usart * const usart1 = (struct usart *)USART1_BASE;
static volatile struct usart * const usart2 = (struct usart *)USART2_BASE;
static volatile struct usart * const usart3 = (struct usart *)USART3_BASE;
static volatile struct usb_otg *const usb_otg = (struct usb_otg *)USB_OTG_BASE;

/* NVIC table */
extern uint32_t vector_table[];

/*
 * Local variables:
 * mode: C
 * c-file-style: "Linux"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */

/*
 * common.h
 * 
 * Core and peripheral registers.
 * 
 * Written & released by Keir Fraser <keir.xen@gmail.com>
 * 
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

/* C pointer types */
#define STK volatile struct stk * const
#define SCB volatile struct scb * const
#define NVIC volatile struct nvic * const
#define DBG volatile struct dbg * const
#define FLASH volatile struct flash * const
#define PWR volatile struct pwr * const
#define BKP volatile struct bkp * const
#define RCC volatile struct rcc * const
#define GPIO volatile struct gpio * const
#define EXTI volatile struct exti * const
#define DMA volatile struct dma * const
#define TIM volatile struct tim * const
#define SPI volatile struct spi * const
#define I2C volatile struct i2c * const
#define USART volatile struct usart * const
#define USB_OTG volatile struct usb_otg * const

/* NVIC table */
extern uint32_t vector_table[];

/* System */
void stm32_init(void);
void system_reset(void) __attribute__((noreturn));
extern bool_t is_artery_mcu;

/* Clocks */
#define SYSCLK     (SYSCLK_MHZ * 1000000)
#define sysclk_ns(x) (((x) * SYSCLK_MHZ) / 1000)
#define sysclk_us(x) ((x) * SYSCLK_MHZ)
#define sysclk_ms(x) ((x) * SYSCLK_MHZ * 1000)
#define sysclk_stk(x) ((x) * (SYSCLK_MHZ / STK_MHZ))

/* SysTick Timer */
#define STK_MHZ    (SYSCLK_MHZ / 8)
void delay_ticks(unsigned int ticks);
void delay_ns(unsigned int ns);
void delay_us(unsigned int us);
void delay_ms(unsigned int ms);

typedef uint32_t stk_time_t;
#define stk_now() (stk->val)
#define stk_diff(x,y) (((x)-(y)) & STK_MASK) /* d = y - x */
#define stk_add(x,d)  (((x)-(d)) & STK_MASK) /* y = x + d */
#define stk_sub(x,d)  (((x)+(d)) & STK_MASK) /* y = x - d */
#define stk_timesince(x) stk_diff(x,stk_now())

#define stk_us(x) ((x) * STK_MHZ)
#define stk_ms(x) stk_us((x) * 1000)
#define stk_sysclk(x) ((x) / (SYSCLK_MHZ / STK_MHZ))

/* NVIC */
#define IRQx_enable(x) do {                     \
    barrier();                                  \
    nvic->iser[(x)>>5] = 1u<<((x)&31);          \
} while (0)
#define IRQx_disable(x) do {                    \
    nvic->icer[(x)>>5] = 1u<<((x)&31);          \
    cpu_sync();                                 \
} while (0)
#define IRQx_is_enabled(x) ((nvic->iser[(x)>>5]>>((x)&31))&1)
#define IRQx_set_pending(x) (nvic->ispr[(x)>>5] = 1u<<((x)&31))
#define IRQx_clear_pending(x) (nvic->icpr[(x)>>5] = 1u<<((x)&31))
#define IRQx_is_pending(x) ((nvic->ispr[(x)>>5]>>((x)&31))&1)
#define IRQx_set_prio(x,y) (nvic->ipr[x] = (y) << 4)
#define IRQx_get_prio(x) (nvic->ipr[x] >> 4)

/* GPIO */
struct gpio;
void gpio_configure_pin(GPIO gpio, unsigned int pin, unsigned int mode);
#define gpio_write_pin(gpio, pin, level) \
    ((gpio)->bsrr = ((level) ? 0x1u : 0x10000u) << (pin))
#define gpio_write_pins(gpio, mask, level) \
    ((gpio)->bsrr = (uint32_t)(mask) << ((level) ? 0 : 16))
#define gpio_read_pin(gpio, pin) (((gpio)->idr >> (pin)) & 1)

/* EXTI */
void _exti_route(unsigned int px, unsigned int pin);
#define exti_route_pa(pin) _exti_route(0, pin)
#define exti_route_pb(pin) _exti_route(1, pin)
#define exti_route_pc(pin) _exti_route(2, pin)

/* FPEC */
void fpec_init(void);
void fpec_page_erase(uint32_t flash_address);
void fpec_write(const void *data, unsigned int size, uint32_t flash_address);

#define FLASH_PAGE_SIZE 2048
extern unsigned int flash_page_size;
extern unsigned int ram_kb;

extern uint8_t mcu_package;
enum { MCU_LQFP64=0, MCU_LQFP48, MCU_QFN32 };

/*
 * Local variables:
 * mode: C
 * c-file-style: "Linux"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */

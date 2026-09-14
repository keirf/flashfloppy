/*
 * rp2350.h
 *
 * RP2350 (Raspberry Pi Pico 2) MCU environment.
 *
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

/* C pointer types */
#define RESETS volatile struct resets * const
#define PSM volatile struct psm * const
#define XOSC volatile struct xosc * const
#define PLL volatile struct pll * const
#define CLOCKS volatile struct clocks * const
#define TICKS volatile struct ticks * const
#define WATCHDOG volatile struct watchdog * const
#define SIO_ volatile struct sio * const
#define IO_BANK0 volatile struct io_bank0 * const
#define PADS_BANK0 volatile struct pads_bank0 * const
#define PIO volatile struct pio * const
#define RP_TIMER volatile struct rp_timer * const
#define RP_UART volatile struct rp_uart * const
#define RP_SPI volatile struct rp_spi * const
#define RP_I2C volatile struct rp_i2c * const
#define QMI volatile struct qmi * const
#define RP_USB volatile struct rp_usb * const
#define USB_DPRAM volatile struct usb_dpram * const

/* C-accessible registers. */
static STK stk = (struct stk *)STK_BASE;
static SCB scb = (struct scb *)SCB_BASE;
static NVIC nvic = (struct nvic *)NVIC_BASE;
static RESETS resets = (struct resets *)RESETS_BASE;
static PSM psm = (struct psm *)PSM_BASE;
static XOSC xosc = (struct xosc *)XOSC_BASE;
static PLL pll_sys = (struct pll *)PLL_SYS_BASE;
static PLL pll_usb = (struct pll *)PLL_USB_BASE;
static CLOCKS clocks = (struct clocks *)CLOCKS_BASE;
static TICKS ticks = (struct ticks *)TICKS_BASE;
static WATCHDOG watchdog = (struct watchdog *)WATCHDOG_BASE;
static SIO_ sio = (struct sio *)SIO_BASE;
static IO_BANK0 io_bank0 = (struct io_bank0 *)IO_BANK0_BASE;
static PADS_BANK0 pads_bank0 = (struct pads_bank0 *)PADS_BANK0_BASE;
static PIO pio0 = (struct pio *)PIO0_BASE;
static RP_TIMER timer0 = (struct rp_timer *)TIMER0_BASE;
static RP_UART uart0 = (struct rp_uart *)UART0_BASE;
static RP_SPI rp_spi0 = (struct rp_spi *)SPI0_BASE;
static RP_I2C rp_i2c0 = (struct rp_i2c *)I2C0_BASE;
static QMI qmi = (struct qmi *)QMI_BASE;
static RP_USB usb_hw = (struct rp_usb *)USBCTRL_REGS_BASE;
static USB_DPRAM usb_dpram = (struct usb_dpram *)USBCTRL_DPRAM_BASE;
static volatile struct dma * const dma = (struct dma *)DMA_BASE;

/* Clocks */
#define SYSCLK_MHZ 144
#define APB1_MHZ SYSCLK_MHZ /* clk_peri == clk_sys */

/* Software IRQs, borrowed from the RP2350 spare interrupt lines. */
#define SOFTIRQ_0 SPARE_IRQ_0
#define SOFTIRQ_1 SPARE_IRQ_1
#define SOFTIRQ_2 SPARE_IRQ_2

/* GPIO API. RP2350 has a single GPIO bank; the gpioa/gpiob/gpioc handles
 * accepted by these functions are ignored, and pin numbers are physical
 * GPIO numbers 0-29. Pins configured as open-drain (emulated: drive low
 * vs high-impedance) are tracked in gpio_od_mask. */
struct gpio;
static GPIO gpioa = (struct gpio *)1;
static GPIO gpiob = (struct gpio *)2;
static GPIO gpioc = (struct gpio *)3;

extern uint32_t gpio_od_mask;

/* Mode encoding for gpio_configure_pin():
 *  [4:0] funcsel, [5] PU, [6] PD, [7] OE, [8] OUT, [9] OD-emulation,
 *  [10] high pad drive */
#define _GPM_FUNC(x) ((x)&0x1f)
#define _GPM_PU  (1u<<5)
#define _GPM_PD  (1u<<6)
#define _GPM_OE  (1u<<7)
#define _GPM_OUT (1u<<8)
#define _GPM_OD  (1u<<9)
#define _GPM_HIDRIVE (1u<<10)

#define GPI_floating  _GPM_FUNC(GPIO_FUNC_SIO)
#define GPI_pull_up   (_GPM_FUNC(GPIO_FUNC_SIO) | _GPM_PU)
#define GPI_pull_down (_GPM_FUNC(GPIO_FUNC_SIO) | _GPM_PD)
/* Output speed is ignored on RP2350: define the usual tokens to 0. */
#define _2MHz  0
#define _10MHz 0
#define _50MHz 0
#define GPO_pushpull(speed,level)                                       \
    (_GPM_FUNC(GPIO_FUNC_SIO) | _GPM_OE | ((level) ? _GPM_OUT : 0))
#define GPO_opendrain(speed,level)                                      \
    (_GPM_FUNC(GPIO_FUNC_SIO) | _GPM_OD | ((level) ? 0 : _GPM_OE))
/* "Alternate function" output: FlashFloppy uses this solely for the
 * PIO-driven RDATA pin. */
#define _AFO_pushpull(speed,level) _GPM_FUNC(GPIO_FUNC_PIO0)
#define AFO_pushpull(speed) _GPM_FUNC(GPIO_FUNC_PIO0)
#define AFI(pupd) GPI_floating

void gpio_set_pad(unsigned int pin, uint32_t pad);

void gpio_write_pin_(unsigned int pin, unsigned int level);
void gpio_write_pins_(uint32_t mask, unsigned int level);
#define gpio_write_pin(gpio, pin, level) gpio_write_pin_(pin, level)
#define gpio_write_pins(gpio, mask, level) gpio_write_pins_(mask, level)
#define gpio_read_pin(gpio, pin) ((sio->gpio_in >> (pin)) & 1)

/* GPIO interrupt helpers (analogue of STM32 EXTI). All handled through
 * the single IO_IRQ_BANK0 vector; see board dispatch code. */
void gpio_irq_enable(unsigned int pin, uint32_t events);
void gpio_irq_disable_mask(uint32_t pin_mask);
void gpio_irq_ack(unsigned int pin);
uint32_t gpio_irq_status(unsigned int reg); /* proc0_ints[reg] */

/* On-board LED, used as the drive-activity indicator (pico2/board.c). */
void board_set_activity_led(bool_t on);

/* Bootrom flash operations (see fpec_rp2350.c) */
void *rp2350_rom_func(uint16_t code);

/* USB CDC-ACM device: debug console, and the Arduino-style "open the port
 * at 1200 baud to enter BOOTSEL" gesture. See pico2/usb_cdc.c. */
void usb_cdc_init(void);
void usb_cdc_putc(char c);
void usb_cdc_kick(void);
void usb_cdc_flush_sync(void);

/*
 * Local variables:
 * mode: C
 * c-file-style: "Linux"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */

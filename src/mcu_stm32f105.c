/*
 * mcu_stm32f105.c
 * 
 * Core and peripheral registers.
 * 
 * Written & released by Keir Fraser <keir.xen@gmail.com>
 * 
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

bool_t is_artery_mcu;
unsigned int flash_page_size = FLASH_PAGE_SIZE;
unsigned int ram_kb = 64;

static void identify_mcu(void)
{
    /* DBGMCU_IDCODE (E0042000): 
     *  STM32F105RB:  10016418 (device id: 418) 
     *  AT32F415CBT7: 700301c5 (device id: 1c5)
     *  AT32F415RCT7: 70030240 (device id: 240) 
     * However the AT32 IDCODE values are undocumented so we cannot rely 
     * on them (for example, what will be the ID for chips with differing 
     * amounts of Flash, or numbers of pins?) */

    /* We detect an Artery MCU by presence of Cortex-M4 CPUID. 
     * Cortex-M4: 41xfc24x ; Cortex-M3: 41xfc23x */
    is_artery_mcu = ((scb->cpuid >> 4) & 0xf) == 4;

    if (is_artery_mcu) {
        unsigned int flash_kb = *(uint16_t *)0x1ffff7e0;
        ram_kb = 32;
        if (flash_kb == 128)
            flash_page_size = 1024;
    }
}

static void clock_init(void)
{
    /* Flash controller: reads require 2 wait states at 72MHz. */
    flash->acr = FLASH_ACR_PRFTBE | FLASH_ACR_LATENCY(2);

    /* Start up the external oscillator. */
    rcc->cr |= RCC_CR_HSEON;
    while (!(rcc->cr & RCC_CR_HSERDY))
        cpu_relax();

    /* PLLs, scalers, muxes. */
    rcc->cfgr = (RCC_CFGR_PLLMUL(9) |        /* PLL = 9*8MHz = 72MHz */
                 RCC_CFGR_PLLSRC_PREDIV1 |
                 RCC_CFGR_ADCPRE_DIV8 |
                 RCC_CFGR_PPRE1_DIV2);

    /* Enable and stabilise the PLL. */
    rcc->cr |= RCC_CR_PLLON;
    while (!(rcc->cr & RCC_CR_PLLRDY))
        cpu_relax();

    /* Switch to the externally-driven PLL for system clock. */
    rcc->cfgr |= RCC_CFGR_SW_PLL;
    while ((rcc->cfgr & RCC_CFGR_SWS_MASK) != RCC_CFGR_SWS_PLL)
        cpu_relax();

    /* Internal oscillator no longer needed. */
    rcc->cr &= ~RCC_CR_HSION;
}

static void gpio_init(GPIO gpio)
{
    /* Floating Input. Reference Manual states that JTAG pins are in PU/PD
     * mode at reset, so ensure all PU/PD are disabled. */
    gpio->crl = gpio->crh = 0x44444444u;
}

static void peripheral_init(void)
{
    /* Enable basic GPIO and AFIO clocks, all timers, and DMA. */
    rcc->apb1enr = (RCC_APB1ENR_TIM2EN |
                    RCC_APB1ENR_TIM3EN |
                    RCC_APB1ENR_TIM4EN);
    rcc->apb2enr = (RCC_APB2ENR_IOPAEN |
                    RCC_APB2ENR_IOPBEN |
                    RCC_APB2ENR_IOPCEN |
                    RCC_APB2ENR_AFIOEN |
                    RCC_APB2ENR_TIM1EN);
    rcc->ahbenr = RCC_AHBENR_DMA1EN;

    /* Turn off serial-wire JTAG and reclaim the GPIOs. */
    afio->mapr = AFIO_MAPR_SWJ_CFG_DISABLED;

    /* All pins in a stable state. */
    gpio_init(gpioa);
    gpio_init(gpiob);
    gpio_init(gpioc);
}

void stm32_init(void)
{
    cortex_init();
    identify_mcu();
    clock_init();
    peripheral_init();
    cpu_sync();
}

void gpio_configure_pin(GPIO gpio, unsigned int pin, unsigned int mode)
{
    gpio_write_pin(gpio, pin, mode >> 4);
    mode &= 0xfu;
    if (pin >= 8) {
        pin -= 8;
        gpio->crh = (gpio->crh & ~(0xfu<<(pin<<2))) | (mode<<(pin<<2));
    } else {
        gpio->crl = (gpio->crl & ~(0xfu<<(pin<<2))) | (mode<<(pin<<2));
    }
}

void _exti_route(unsigned int px, unsigned int pin)
{
    unsigned int n = pin >> 2;
    unsigned int s = (pin & 3) << 2;
    uint32_t exticr = afio->exticr[n];
    ASSERT(!in_exception()); /* no races please */
    exticr &= ~(0xf << s);
    exticr |= px << s;
    afio->exticr[n] = exticr;
}

/*
 * Local variables:
 * mode: C
 * c-file-style: "Linux"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */

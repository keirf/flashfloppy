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

unsigned int sysclk_mhz = 72;
unsigned int apb1_mhz = 36;

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
        sysclk_mhz = 144;
        apb1_mhz = 72;
    }
}

static void clock_init(void)
{
    /* Flash controller: reads require 2 wait states at 72MHz. */
    flash->acr = FLASH_ACR_PRFTBE | FLASH_ACR_LATENCY(sysclk_mhz/32);

    /* Start up the external oscillator. */
    rcc->cr |= RCC_CR_HSEON;
    while (!(rcc->cr & RCC_CR_HSERDY))
        cpu_relax();

    /* PLLs, scalers, muxes. */
    if (is_artery_mcu) {
        uint32_t rcc_pll = *RCC_PLL;
        rcc_pll &= ~(RCC_PLL_PLLCFGEN | RCC_PLL_FREF_MASK);
        rcc_pll |= RCC_PLL_FREF_8M;
        *RCC_PLL = rcc_pll;
        rcc->cfgr = (RCC_CFGR_PLLMUL_18 |        /* PLL = 18*8MHz = 144MHz */
                     RCC_CFGR_USBPSC_3 |         /* USB = SYSCLK/3 = 48MHz */
                     RCC_CFGR_PLLSRC_PREDIV1 |
                     RCC_CFGR_ADCPRE_DIV8 |
                     RCC_CFGR_APB2PSC_2 |        /* APB2 = SYSCLK/2 = 72MHz */
                     RCC_CFGR_APB1PSC_2);        /* APB1 = SYSCLK/2 = 72MHz */
    } else {
        rcc->cfgr = (RCC_CFGR_PLLMUL(9) |        /* PLL = 9*8MHz = 72MHz */
                     RCC_CFGR_PLLSRC_PREDIV1 |
                     RCC_CFGR_ADCPRE_DIV8 |
                     RCC_CFGR_APB1PSC_2);        /* APB1 = SYSCLK/2 = 36MHz */
    }

    /* Enable and stabilise the PLL. */
    rcc->cr |= RCC_CR_PLLON;
    while (!(rcc->cr & RCC_CR_PLLRDY))
        cpu_relax();

    if (is_artery_mcu)
        *RCC_MISC2 |= RCC_MISC2_AUTOSTEP_EN;

    /* Switch to the externally-driven PLL for system clock. */
    rcc->cfgr |= RCC_CFGR_SW_PLL;
    while ((rcc->cfgr & RCC_CFGR_SWS_MASK) != RCC_CFGR_SWS_PLL)
        cpu_relax();

    if (is_artery_mcu)
        *RCC_MISC2 &= ~RCC_MISC2_AUTOSTEP_EN;

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

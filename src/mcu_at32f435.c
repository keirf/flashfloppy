/*
 * mcu_at342f435.c
 * 
 * Core and peripheral registers.
 * 
 * Written & released by Keir Fraser <keir.xen@gmail.com>
 * 
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

bool_t is_artery_mcu = TRUE;
unsigned int flash_page_size = FLASH_PAGE_SIZE;
unsigned int ram_kb = 384;

static void clock_init(void)
{
    /* Enable PWR interface so we can set the LDO boost. */
    rcc->apb1enr |= RCC_APB1ENR_PWREN;

    /* Bootloader leaves MISC1 set up for USB clocked from HICK. 
     * Clear MISC1 register to its reset value. */
    rcc->misc1 = 0;

    /* 288MHz requires LDO voltage boost. */
    pwr->ldoov = PWR_LDOOV_1V3;

    flash->divr = FLASH_DIVR_DIV_3;

    /* Start up the external oscillator. */
    rcc->cr |= RCC_CR_HSEON;
    while (!(rcc->cr & RCC_CR_HSERDY))
        cpu_relax();

    /* Enable auto-step. */
    rcc->misc2 |= RCC_MISC2_AUTOSTEP;

    /* Configure PLL for 8MHz input, 288MHz output. */
    rcc->pllcfgr = (RCC_PLLCFGR_PLLSRC_HSE | /* PLLSrc = HSE = 8MHz */
                    RCC_PLLCFGR_PLL_MS(1) |  /* PLL In = HSE/1 = 8MHz */
                    RCC_PLLCFGR_PLL_NS(72) | /* PLLVCO = 8MHz*72 = 576MHz */
                    RCC_PLLCFGR_PLL_FR(PLL_FR_2)); /* PLL Out = 576MHz/2 */

    /* Bus divisors. */
    rcc->cfgr = (RCC_CFGR_PPRE2(4) | /* APB2 = 288MHz/2 = 144MHz  */
                 RCC_CFGR_PPRE1(4) | /* APB1 = 288MHz/2 = 144MHz */
                 RCC_CFGR_HPRE(0));  /* AHB  = 288MHz/1 = 288MHz */

    /* Enable and stabilise the PLL. */
    rcc->cr |= RCC_CR_PLLON;
    while (!(rcc->cr & RCC_CR_PLLRDY))
        cpu_relax();

    /* Switch to the externally-driven PLL for system clock. */
    rcc->cfgr |= RCC_CFGR_SW(2);
    while ((rcc->cfgr & RCC_CFGR_SWS(3)) != RCC_CFGR_SWS(2))
        cpu_relax();

    /* Internal oscillator no longer needed. */
    rcc->cr &= ~RCC_CR_HSION;

    /* Disable auto-step. */
    rcc->misc2 &= ~RCC_MISC2_AUTOSTEP;
}

static void peripheral_init(void)
{
    /* Enable basic GPIO clocks, DTCM RAM, DMA, and EXTICR. */
    rcc->ahb1enr |= (RCC_AHB1ENR_DMA1EN |
                     RCC_AHB1ENR_GPIOHEN |
                     RCC_AHB1ENR_GPIOCEN |
                     RCC_AHB1ENR_GPIOBEN | 
                     RCC_AHB1ENR_GPIOAEN);
    rcc->apb1enr |= (RCC_APB1ENR_TIM2EN |
                     RCC_APB1ENR_TIM3EN |
                     RCC_APB1ENR_TIM4EN |
                     RCC_APB1ENR_TIM5EN);
    rcc->apb2enr |= (RCC_APB2ENR_SYSCFGEN |
                     RCC_APB2ENR_TIM1EN);

    /* Flexible DMA request mappings. */
    dmamux1->sel = DMAMUX_SEL_TBL_SEL;
    dmamux2->sel = DMAMUX_SEL_TBL_SEL;

    /* Release JTAG pins. */
    gpio_configure_pin(gpioa, 15, GPI_floating);
    gpio_configure_pin(gpiob,  3, GPI_floating);
    gpio_configure_pin(gpiob,  4, GPI_floating);
}

void stm32_init(void)
{
    cortex_init();
    clock_init();
    peripheral_init();
    cpu_sync();
}

void gpio_configure_pin(GPIO gpio, unsigned int pin, unsigned int mode)
{
    gpio_write_pin(gpio, pin, mode >> 7);
    gpio->moder = (gpio->moder & ~(3<<(pin<<1))) | ((mode&3)<<(pin<<1));
    mode >>= 2;
    gpio->otyper = (gpio->otyper & ~(1<<pin)) | ((mode&1)<<pin);
    mode >>= 1;
    gpio->odrvr = (gpio->odrvr & ~(3<<(pin<<1))) | ((mode&3)<<(pin<<1));
    mode >>= 2;
    gpio->pupdr = (gpio->pupdr & ~(3<<(pin<<1))) | ((mode&3)<<(pin<<1));
}

void gpio_set_af(GPIO gpio, unsigned int pin, unsigned int af)
{
    if (pin < 8) {
        gpio->afrl = (gpio->afrl & ~(15<<(pin<<2))) | (af<<(pin<<2));
    } else {
        pin -= 8;
        gpio->afrh = (gpio->afrh & ~(15<<(pin<<2))) | (af<<(pin<<2));
    }
}

void _exti_route(unsigned int px, unsigned int pin)
{
    unsigned int n = pin >> 2;
    unsigned int s = (pin & 3) << 2;
    uint32_t exticr = syscfg->exticr[n];
    ASSERT(!in_exception()); /* no races please */
    exticr &= ~(0xf << s);
    exticr |= px << s;
    syscfg->exticr[n] = exticr;
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

/*
 * mcu_rp2350.c
 *
 * RP2350 (Raspberry Pi Pico 2) system initialisation: clocks, resets,
 * GPIO and GPIO-interrupt management.
 *
 * clk_sys runs at 144MHz so that a 2-cycles-per-tick PIO program yields
 * exactly the 72MHz SAMPLECLK used by the floppy flux engine.
 *
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

unsigned int flash_page_size = FLASH_PAGE_SIZE;
unsigned int ram_kb = 512;

bool_t is_artery_mcu;
uint8_t mcu_package;

/* Pins configured for emulated open-drain (assert = drive low, deassert =
 * high impedance). Consulted by gpio_write_pin[s]_(). */
uint32_t gpio_od_mask;

static void clock_init(void)
{
    /* Start the crystal oscillator (12MHz on Pico 2). */
    xosc->ctrl = XOSC_CTRL_FREQ_1_15MHZ;
    xosc->startup = 0xc4; /* ~1ms in units of 256 xtal cycles */
    RP_SET(&xosc->ctrl) = XOSC_CTRL_ENABLE;
    while (!(xosc->status & XOSC_STATUS_STABLE))
        cpu_relax();

    /* Glitch-free switch of clk_sys to its safe source (clk_ref/rosc)
     * before reconfiguring the PLL. */
    RP_CLR(&clocks->sys.ctrl) = 1; /* SRC = clk_ref */
    while (clocks->sys.selected != 1)
        cpu_relax();

    /* clk_ref = xosc. */
    clocks->ref.ctrl = CLK_REF_CTRL_SRC_XOSC;
    while (clocks->ref.selected != (1u << CLK_REF_CTRL_SRC_XOSC))
        cpu_relax();
    clocks->ref.div = CLK_DIV_INT(1);

    /* PLL_SYS: 12MHz / 1 * 120 = 1440MHz VCO; / 5 / 2 = 144MHz. */
    RP_SET(&resets->reset) = RST_PLL_SYS;
    RP_CLR(&resets->reset) = RST_PLL_SYS;
    while (!(resets->reset_done & RST_PLL_SYS))
        cpu_relax();
    pll_sys->cs = PLL_CS_REFDIV(1);
    pll_sys->fbdiv_int = 120;
    RP_CLR(&pll_sys->pwr) = PLL_PWR_PD | PLL_PWR_VCOPD;
    while (!(pll_sys->cs & PLL_CS_LOCK))
        cpu_relax();
    pll_sys->prim = PLL_PRIM_POSTDIV1(5) | PLL_PRIM_POSTDIV2(2);
    RP_CLR(&pll_sys->pwr) = PLL_PWR_POSTDIVPD;

    /* clk_sys = pll_sys = 144MHz. */
    clocks->sys.div = CLK_DIV_INT(1);
    clocks->sys.ctrl = CLK_SYS_CTRL_AUXSRC_PLL_SYS; /* aux = pll_sys */
    RP_SET(&clocks->sys.ctrl) = 1; /* SRC = aux */
    while (clocks->sys.selected != 2)
        cpu_relax();

    /* clk_peri = clk_sys (feeds UART/SPI). */
    clocks->peri.div = CLK_DIV_INT(1);
    clocks->peri.ctrl = CLK_PERI_CTRL_AUXSRC_SYS | CLK_PERI_CTRL_ENABLE;

    /* PLL_USB: 12MHz / 1 * 100 = 1200MHz VCO; / 5 / 5 = 48MHz, the exact
     * rate the USB full-speed SIE requires. */
    RP_SET(&resets->reset) = RST_PLL_USB;
    RP_CLR(&resets->reset) = RST_PLL_USB;
    while (!(resets->reset_done & RST_PLL_USB))
        cpu_relax();
    pll_usb->cs = PLL_CS_REFDIV(1);
    pll_usb->fbdiv_int = 100;
    RP_CLR(&pll_usb->pwr) = PLL_PWR_PD | PLL_PWR_VCOPD;
    while (!(pll_usb->cs & PLL_CS_LOCK))
        cpu_relax();
    pll_usb->prim = PLL_PRIM_POSTDIV1(5) | PLL_PRIM_POSTDIV2(5);
    RP_CLR(&pll_usb->pwr) = PLL_PWR_POSTDIVPD;

    /* clk_usb = pll_usb = 48MHz. */
    clocks->usb.div = CLK_DIV_INT(1);
    clocks->usb.ctrl = CLK_USB_CTRL_AUXSRC_PLL_USB | CLK_USB_CTRL_ENABLE;

    /* Tick generators: 1MHz ticks from the 12MHz clk_ref for the
     * microsecond timer (TIMER0), watchdog, and this core's SysTick. */
    ticks->timer0.cycles = 12;
    ticks->timer0.ctrl = TICK_CTRL_ENABLE;
    ticks->watchdog.cycles = 12;
    ticks->watchdog.ctrl = TICK_CTRL_ENABLE;
    ticks->proc0.cycles = 12;
    ticks->proc0.ctrl = TICK_CTRL_ENABLE;
}

static void peripheral_init(void)
{
    uint32_t rst = (RST_BUSCTRL | RST_DMA | RST_IO_BANK0 | RST_PADS_BANK0
                    | RST_PIO0 | RST_SPI0 | RST_I2C0 | RST_UART0
                    | RST_TIMER0 | RST_SYSCFG | RST_SYSINFO | RST_PWM);

    /* Cycle the peripherals we use through reset, then wait for ready. */
    RP_SET(&resets->reset) = rst;
    RP_CLR(&resets->reset) = rst;
    while ((resets->reset_done & rst) != rst)
        cpu_relax();
}

void stm32_init(void)
{
    cortex_init();
    clock_init();
    peripheral_init();
    cpu_sync();
}

void gpio_set_pad(unsigned int pin, uint32_t pad)
{
    /* Never set ISO; always clear it (pads are isolated out of reset). */
    pads_bank0->gpio[pin] = pad & ~PAD_ISO;
}

void gpio_configure_pin(GPIO gpio, unsigned int pin, unsigned int mode)
{
    /* Bus outputs ask for the strongest pad drive: they must sink the
     * host's termination current, and a pad that cannot pull the line below
     * the controller's input threshold reads as a dead line however correct
     * the firmware driving it. */
    uint32_t pad = PAD_IE | PAD_SCHMITT
        | ((mode & _GPM_HIDRIVE) ? PAD_DRIVE_12MA : PAD_DRIVE_4MA);
    uint32_t odm = m(pin);

    if (mode & _GPM_PU)
        pad |= PAD_PUE;
    if (mode & _GPM_PD)
        pad |= PAD_PDE;

    if (mode & _GPM_OD)
        gpio_od_mask |= odm;
    else
        gpio_od_mask &= ~odm;

    /* Output level and enable via SIO (atomic set/clear). */
    if (mode & _GPM_OUT)
        sio->gpio_out_set = odm;
    else
        sio->gpio_out_clr = odm;
    if (mode & _GPM_OE)
        sio->gpio_oe_set = odm;
    else
        sio->gpio_oe_clr = odm;

    gpio_set_pad(pin, pad);
    io_bank0->gpio[pin].ctrl = _GPM_FUNC(mode);
}

void gpio_write_pin_(unsigned int pin, unsigned int level)
{
    uint32_t mask = m(pin);
    if (gpio_od_mask & mask) {
        /* Open-drain emulation: high = float, low = drive low. */
        if (level)
            sio->gpio_oe_clr = mask;
        else
            sio->gpio_oe_set = mask;
    } else {
        if (level)
            sio->gpio_out_set = mask;
        else
            sio->gpio_out_clr = mask;
    }
}

void gpio_write_pins_(uint32_t mask, unsigned int level)
{
    uint32_t od = mask & gpio_od_mask;
    uint32_t pp = mask & ~gpio_od_mask;
    if (level) {
        if (od) sio->gpio_oe_clr = od;
        if (pp) sio->gpio_out_set = pp;
    } else {
        if (od) sio->gpio_oe_set = od;
        if (pp) sio->gpio_out_clr = pp;
    }
}

/* GPIO interrupts: enable @events (GPIO_IRQ_* bits) for @pin, routed to
 * this core's IO_IRQ_BANK0 line. */
void gpio_irq_enable(unsigned int pin, uint32_t events)
{
    volatile uint32_t *inte = &io_bank0->proc0_inte[pin >> 3];
    unsigned int shift = (pin & 7) * 4;
    /* Clear any latched edge events before enabling. */
    io_bank0->intr[pin >> 3] = (GPIO_IRQ_EDGE_LOW|GPIO_IRQ_EDGE_HIGH) << shift;
    RP_SET(inte) = events << shift;
}

/* Disable all GPIO interrupt events for the pins in @pin_mask. */
void gpio_irq_disable_mask(uint32_t pin_mask)
{
    unsigned int pin;
    for (pin = 0; pin_mask != 0; pin++, pin_mask >>= 1) {
        if (pin_mask & 1) {
            volatile uint32_t *inte = &io_bank0->proc0_inte[pin >> 3];
            RP_CLR(inte) = 0xfu << ((pin & 7) * 4);
        }
    }
}

/* Acknowledge (clear) latched edge events for @pin. */
void gpio_irq_ack(unsigned int pin)
{
    io_bank0->intr[pin >> 3] =
        (GPIO_IRQ_EDGE_LOW | GPIO_IRQ_EDGE_HIGH) << ((pin & 7) * 4);
}

uint32_t gpio_irq_status(unsigned int reg)
{
    return io_bank0->proc0_ints[reg];
}

/* Bootrom function lookup (Arm secure function pointers). */
void *rp2350_rom_func(uint16_t code)
{
    typedef void *(*rom_table_lookup_fn)(uint32_t code, uint32_t mask);
    rom_table_lookup_fn lookup;
    uint32_t p = BOOTROM_TABLE_LOOKUP_OFFSET;
    /* Launder the constant ROM address past the compiler's bounds checks. */
    asm volatile ("" : "+r" (p));
    lookup = (rom_table_lookup_fn)(uint32_t)*(const uint16_t *)p;
    return lookup(code, RT_FLAG_FUNC_ARM_SEC);
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

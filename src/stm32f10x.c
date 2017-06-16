/*
 * stm32f10x.c
 * 
 * Core and peripheral registers.
 * 
 * Written & released by Keir Fraser <keir.xen@gmail.com>
 * 
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

struct extra_exception_frame {
    uint32_t r4, r5, r6, r7, r8, r9, r10, r11, lr;
};

void EXC_unexpected(struct extra_exception_frame *extra)
{
    struct exception_frame *frame;
    uint8_t exc = (uint8_t)read_special(psr);
    uint32_t msp, psp;

    if (extra->lr & 4) {
        frame = (struct exception_frame *)read_special(psp);
        psp = (uint32_t)(frame + 1);
        msp = (uint32_t)(extra + 1);
    } else {
        frame = (struct exception_frame *)(extra + 1);
        psp = read_special(psp);
        msp = (uint32_t)(frame + 1);
    }

    printk("Unexpected %s #%u at PC=%08x (%s):\n",
           (exc < 16) ? "Exception" : "IRQ",
           (exc < 16) ? exc : exc - 16,
           frame->pc, (extra->lr & 8) ? "Thread" : "Handler");
    printk(" r0:  %08x   r1:  %08x   r2:  %08x   r3:  %08x\n",
           frame->r0, frame->r1, frame->r2, frame->r3);
    printk(" r4:  %08x   r5:  %08x   r6:  %08x   r7:  %08x\n",
           extra->r4, extra->r5, extra->r6, extra->r7);
    printk(" r8:  %08x   r9:  %08x   r10: %08x   r11: %08x\n",
           extra->r8, extra->r9, extra->r10, extra->r11);
    printk(" r12: %08x   sp:  %08x   lr:  %08x   pc:  %08x\n",
           frame->r12, (extra->lr & 4) ? psp : msp, frame->lr, frame->pc);
    printk(" msp: %08x   psp: %08x   psr: %08x\n",
           msp, psp, frame->psr);

    system_reset();
}

static void exception_init(void)
{
    uint32_t sp;

    /* Switch to Process SP, and set up Main SP for IRQ/Exception context. */
    sp = read_special(msp);
    write_special(psp, sp);
    write_special(control, 2); /* CONTROL.SPSEL=1 */
    write_special(msp, _irq_stacktop);

    /* Initialise interrupts and exceptions. */
    scb->vtor = (uint32_t)(unsigned long)vector_table;
    scb->ccr |= SCB_CCR_STKALIGN | SCB_CCR_DIV_0_TRP;
    /* GCC inlines memcpy() using full-word load/store regardless of buffer
     * alignment. Hence it is unsafe to trap on unaligned accesses. */
    /*scb->ccr |= SCB_CCR_UNALIGN_TRP;*/
    scb->shcsr |= (SCB_SHCSR_USGFAULTENA |
                   SCB_SHCSR_BUSFAULTENA |
                   SCB_SHCSR_MEMFAULTENA);

    /* SVCall/PendSV exceptions have lowest priority. */
    scb->shpr2 = 0xff<<24;
    scb->shpr3 = 0xff<<16;
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

    /* Enable SysTick counter at 72/8=9MHz. */
    stk->load = STK_MASK;
    stk->ctrl = STK_CTRL_ENABLE;
}

static void gpio_init(GPIO gpio)
{
    /* Floating Input. Reference Manual states that JTAG pins are in PU/PD
     * mode at reset, so ensure all PU/PD are disabled. */
    gpio->crl = gpio->crh = 0x44444444u;
}

static void peripheral_init(void)
{
    /* Enable basic GPIO and AFIO clocks, and DMA. */
    rcc->apb1enr = 0;
    rcc->apb2enr = (RCC_APB2ENR_IOPAEN |
                    RCC_APB2ENR_IOPBEN |
                    RCC_APB2ENR_IOPCEN |
                    RCC_APB2ENR_AFIOEN);
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
    exception_init();
    clock_init();
    peripheral_init();
}

void delay_ticks(unsigned int ticks)
{
    unsigned int diff, cur, prev = stk->val;

    for (;;) {
        cur = stk->val;
        diff = (prev - cur) & STK_MASK;
        if (ticks <= diff)
            break;
        ticks -= diff;
        prev = cur;
    }
}

void delay_ns(unsigned int ns)
{
    delay_ticks((ns * STK_MHZ) / 1000u);
}

void delay_us(unsigned int us)
{
    delay_ticks(us * STK_MHZ);
}

void delay_ms(unsigned int ms)
{
    delay_ticks(ms * 1000u * STK_MHZ);
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

void system_reset(void)
{
    console_sync();
    printk("Resetting...\n");
    scb->aircr = SCB_AIRCR_VECTKEY | SCB_AIRCR_SYSRESETREQ;
    for (;;) ;
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

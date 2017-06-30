/*
 * gotek/board.c
 * 
 * Gotek board-specific setup and management.
 * 
 * Written & released by Keir Fraser <keir.xen@gmail.com>
 * 
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

void IRQ_12(void) __attribute__((alias("IRQ_dma1_ch2")));

void (*_IRQ_dma1_ch2)(void) = EXC_unused;
static void IRQ_dma1_ch2(void)
{
    (*_IRQ_dma1_ch2)();
}

static void gpio_pull_up_pins(GPIO gpio, uint16_t mask)
{
    unsigned int i;
    for (i = 0; i < 16; i++) {
        if (mask & 1)
            gpio_configure_pin(gpio, i, GPI_pull_up);
        mask >>= 1;
    }
}

void board_init(void)
{
    board_id = BRDREV_Gotek;

    /* Pull up all currently unused and possibly-floating pins. */
    /* Skip PA0-1,8 (floppy inputs), PA9-10 (serial console). */
    gpio_pull_up_pins(gpioa, ~0x0703);
    /* Skip PB0,4,9 (floppy inputs). */
    gpio_pull_up_pins(gpiob, ~0x0211);
    /* Don't skip any PCx pins. */
    gpio_pull_up_pins(gpioc, ~0x0000);
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

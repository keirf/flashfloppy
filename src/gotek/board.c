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

/* Pull up currently unused and possibly-floating pins. */
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
    uint16_t pa_skip, pb_skip;
    uint8_t id;

    /* PA0-1,8 (floppy inputs), PA2 (speaker), PA9-10 (serial console). */
    pa_skip = 0x0707;

    /* PB0,4,9 (floppy inputs). */
    pb_skip = 0x0211;

    /* Pull down PA11 (USB_DM) and PA12 (USB_DP). */
    pa_skip |= 0x1800;
    gpio_configure_pin(gpioa, 11, GPI_pull_down);
    gpio_configure_pin(gpioa, 12, GPI_pull_down);

    /* Pull up all PCx pins. */
    gpio_pull_up_pins(gpioc, ~0x0000);

    /* Wait for ID to stabilise at PC[15:12]. */
    delay_us(5);
    id = (gpioc->idr >> 12) & 0xf;

    switch (board_id = id) {
    case BRDREV_Gotek_standard:
        break;
    case BRDREV_Gotek_enhanced:
    case BRDREV_Gotek_sd_card:
        /* PA3,15 (floppy inputs), PA4 (USBENA). */
        pa_skip |= 0x8018;
        /* PA4: /USBENA */
        gpio_configure_pin(gpioa, 4, GPO_pushpull(_2MHz, LOW));
        break;
    default:
        ASSERT(0);
    }

    gpio_pull_up_pins(gpioa, ~pa_skip);
    gpio_pull_up_pins(gpiob, ~pb_skip);
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

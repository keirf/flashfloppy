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

static bool_t is_48pin_mcu;

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

unsigned int board_get_buttons(void)
{
    /* 64-pin MCU buttons:
     *  PC6 = SELECT, PC7 = LEFT, PC8 = RIGHT
     * 48-pin MCU buttons:
     *  PA5 = SELECT, PA4 = LEFT, PA3 = RIGHT */
    unsigned int x = ~(is_48pin_mcu
                       ? (gpioa->idr >> 3)
                       : (_rbit32(gpioc->idr) >> 23)) & 7;
    /* SLR -> SRL */
    return (x&4) | ((x&1)<<1) | ((x&2)>>1);
}

unsigned int board_get_rotary(void)
{
    /* 64-pin MCU rotary: PC10, PC11
     * 48-pin MCU rotary: PA13, PA14 */
    unsigned int x;
    if (is_48pin_mcu) {
        x = (ff_cfg.chgrst != CHGRST_pa14) ? gpioa->idr >> 13 : 0;
    } else {
        x = gpioc->idr >> 10;
    }
    return x&3;
}

unsigned int board_get_rotary_mask(void)
{
    if (is_48pin_mcu)
        return (ff_cfg.chgrst != CHGRST_pa14) ? m(14) | m(13) : 0;
    return m(11) | m(10); /* PC10,11 */
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

    if (is_artery_mcu) {

        board_id = BRDREV_Gotek_standard;
        /* 48-pin package has PC12 permanently LOW. */
        is_48pin_mcu = !(id & 1);

    } else {

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

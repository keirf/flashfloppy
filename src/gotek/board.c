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
static bool_t has_kc30_header;

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
    /* SFRC922D (64-pin MCU; these pins don't exist on 48-pin MCU):
     *  PC6 = SELECT, PC7 = LEFT, PC8 = RIGHT
     * SFRC922AT3 (48p), SFRKC30:
     *  PA5 = SELECT, PA4 = LEFT, PA3 = RIGHT
     * SFRKC30 (dedicated rotary header):
     *  PF6 = SELECT */
    unsigned int x = (board_id == BRDREV_Gotek_standard)
        ? gpioa->idr >> 3 : -1;
    if (!is_48pin_mcu)
        x &= _rbit32(gpioc->idr) >> 23;
    x = ~x & 7;
    if (has_kc30_header) {
        unsigned int kc30 = ~(gpiof->idr >> (6-2)) & 4;
        x |= kc30;
    }
    /* SLR -> SRL */
    return (x&4) | ((x&1)<<1) | ((x&2)>>1);
}

unsigned int board_get_rotary(void)
{
    /* SFRC922D (64-pin MCU; these pins don't exist on 48-pin MCU):
     *  PC10, PC11
     * SFRC922AT3 (48p; no rotary header, so use SWD header):
     *  PA13, PA14
     * SFRKC30 (dedicated rotary header):
     *  PC10, PC11 *and* PA6, PA15 */
    unsigned int x;
    if (is_48pin_mcu) {
        x = (ff_cfg.chgrst != CHGRST_pa14) ? gpioa->idr >> 13 : 3;
    } else {
        x = gpioc->idr >> 10;
    }
    if (has_kc30_header) {
        unsigned int kc30 = gpioa->idr;
        kc30 = ((kc30>>6)&1) | ((kc30>>(15-1))&2);
        x &= kc30;
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

        /* If PF7 is floating then we may be running on a board with the
         * optional rotary-encoder header (SFRKC30). On earlier boards PF6=VSS
         * and PF7=VDD, hence we take care here. */
        rcc->apb2enr |= RCC_APB2ENR_IOPFEN;
        gpio_configure_pin(gpiof, 7, GPI_pull_down);
        delay_us(10);
        has_kc30_header = (gpio_read_pin(gpiof, 7) == LOW);
        gpio_configure_pin(gpiof, 7, GPI_floating);
        if (has_kc30_header)
            gpio_configure_pin(gpiof, 6, GPI_pull_up);

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

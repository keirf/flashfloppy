/*
 * gotek/board.c
 * 
 * Gotek board-specific setup and management.
 * 
 * SFRC922, SFRC922C, SFRC922D et al
 *  Original LQFP64 designs, using STM or AT chips.
 *  Buttons: PC6 = Select, PC7 = Left, PC7 = Right
 *  Rotary:  PC10, PC11
 * 
 * SFRC922AT3
 *  LQFP48 design, missing rotary header.
 *  Alternative rotary location at PA13, PA14
 *  Buttons: PA5 = Select, PA4 = Left, PA3 = Right
 * 
 * SFRKC30AT4, SFRKC30.AT4, SFRKC30.AT4.7
 *  LQFP64 designs with original rotary header and "KC30" rotary header.
 *  Buttons: PA5 = Select, PA4 = Left, PA3 = Right
 *  Rotary:  PC10, PC11
 *  KC30: PF6 = Select, PA6/PA15 = Rotary
 * 
 * SFRKC30AT3
 *  LQFP48 design similar to SFRC922AT3 but with the "KC30" rotary header.
 *  Buttons: PA5 = Select, PA4 = Left, PA3 = Right
 *  KC30: PF6 = Select, PA6/PA15 = Rotary
 * 
 * SFRKC30.AT2
 *  QFN32 design with various pin changes and features missing:
 *  Missing:
 *   * Original rotary header
 *   * JC jumper position
 *  Relocated to new MCU pins:
 *   * Display header is moved to PB[7:6] using I2C1 instead of I2C2
 *   * KC30 header SELECT/button pin
 *   * Floppy output pins 2 and 26
 *   * Floppy WGATE input pin
 *  Buttons: PA5 = Select, PA4 = Left, PA3 = Right
 *  KC30: PA10 = Select, PA6/PA15 = Rotary
 * 
 * Future QFN32:
 *  Agreed that JC will be implemented at PA9.
 * 
 * Written & released by Keir Fraser <keir.xen@gmail.com>
 * 
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

bool_t is_32pin_mcu;
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
    /* All recent Gotek revisions, regardless of MCU model or package: 
     *  PA5 = Select, PA4 = Left, PA3 = Right. 
     * Note: "Enhanced Gotek" design uses these pins so must skip them here. */
    unsigned int x = (board_id == BRDREV_Gotek_standard)
        ? gpioa->idr >> 3 : -1;
    /* Earlier Gotek revisions (all of which are LQFP64): 
     *  PC6 = Select, PC7 = Left, PC8 = Right. */
    if (!is_48pin_mcu && !is_32pin_mcu)
        x &= _rbit32(gpioc->idr) >> 23;
    x = ~x & 7;
    if (has_kc30_header) {
        /* KC30 Select pin, Artery models only: 
         *  PF6 = Select; except QFN32: PA10 = Select. */
        unsigned int kc30 = (is_32pin_mcu
                             ? gpioa->idr >> (10-2)  /* PA10 */
                             : gpiof->idr >> (6-2)); /* PF6 */
        x |= ~kc30 & 4;
    }
    /* SLR -> SRL */
    return (x&4) | ((x&1)<<1) | ((x&2)>>1);
}

unsigned int board_get_rotary(void)
{
    unsigned int x;
    if (is_32pin_mcu) {
        /* No original rotary header. No alternative location. */
        x = 3;
    } else if (is_48pin_mcu) {
        /* No original rotary header. Alternative location at PA13, PA14. */
        x = (ff_cfg.chgrst != CHGRST_pa14) ? gpioa->idr >> 13 : 3;
    } else {
        /* Original rotary header at PC10, PC11. */
        x = gpioc->idr >> 10;
    }
    if (has_kc30_header) {
        /* KC30 rotary pins PA6, PA15. */
        unsigned int kc30 = gpioa->idr;
        kc30 = ((kc30>>6)&1) | ((kc30>>(15-1))&2);
        x &= kc30;
    }
    return x&3;
}

uint32_t board_rotary_exti_mask;
void board_setup_rotary_exti(void)
{
    uint32_t m = 0;
    if (is_48pin_mcu) {
        /* Alternative location at PA13, PA14. */
        if (ff_cfg.chgrst != CHGRST_pa14) {
            exti_route_pa(13);
            exti_route_pa(14);
            m |= m(13) | m(14);
        }
    } else if (!is_32pin_mcu) {
        /* Original rotary header at PC10, PC11. */
        exti_route_pc(10);
        exti_route_pc(11);
        m |= m(10) | m(11);
    }
    if (has_kc30_header) {
        /* KC30 rotary pins PA6, PA15. */
        if (ff_cfg.motor_delay == MOTOR_ignore) {
            exti_route_pa(6);
            exti_route_pa(15);
            m |= m(6) | m(15);
        }
    }
    board_rotary_exti_mask = m;
    exti->rtsr |= m;
    exti->ftsr |= m;
    exti->imr |= m;
}

bool_t board_jc_strapped(void)
{
    if (is_32pin_mcu) {
#if !defined(NDEBUG)
        return FALSE; /* PA9 is used for serial tx */
#else
        return !gpio_read_pin(gpioa, 9);
#endif
    }
    return !gpio_read_pin(gpiob, 1);
}

void board_init(void)
{
    uint16_t pa_skip, pb_skip, pc_skip;
    uint8_t id;

    /* PA0-1,8 (floppy inputs), PA2 (speaker). */
    pa_skip = 0x0107;

#if !defined(NDEBUG)
    /* PA9-10 (serial console). */
    pa_skip |= 0x0600;
#endif

    /* PB0,4,9 (floppy inputs). */
    pb_skip = 0x0211;

    /* Pull down PA11 (USB_DM) and PA12 (USB_DP). */
    pa_skip |= 0x1800;
    gpio_configure_pin(gpioa, 11, GPI_pull_down);
    gpio_configure_pin(gpioa, 12, GPI_pull_down);

    /* Pull up all PCx pins. */
    pc_skip = 0x0000;
    gpio_pull_up_pins(gpioc, ~pc_skip);

    /* Wait for ID to stabilise at PC[15:12]. */
    delay_us(100);
    id = (gpioc->idr >> 12) & 0xf;

    if (is_artery_mcu) {
        switch (dbg->mcu_idcode & 0xfff) {
        case 0x1c6: /* AT32F415KBU7-4 */
        case 0x242: /* AT32F415KCU7-4 */
            is_32pin_mcu = TRUE;
            id = 0xf;
            break;
        }
    }

    if (is_artery_mcu && (id & 2)) {

        /* This is a factory Gotek board design, or direct clone, with an
         * Artery MCU. We now check which factory design: variants exist for
         * 48- and 64-pin Artery MCUs, and with various headers for buttons and
         * rotary encoders. Though we have discriminated on PC13 alone, the 
         * only expected ID values here are 1110 (48-pin MCU) and 1111 (64-pin 
         * MCU). */
        board_id = BRDREV_Gotek_standard;

        if (is_32pin_mcu) {

            has_kc30_header = TRUE;
            pa_skip &= ~(1<<10); /* PA10 is not used as serial rx */
            pb_skip |= 1<<1; /* PB1 is a floppy input (WGATE) */

        } else {

            /* 48-pin package has PC12 permanently LOW. */
            is_48pin_mcu = !(id & 1);

            /* If PF7 is floating then we may be running on a board with the
             * optional rotary-encoder header (SFRKC30). On earlier boards
             * PF6=VSS and PF7=VDD, hence we take care here. */
#if MCU == STM32F105 /* AT32F435 needs new PCB */
            rcc->apb2enr |= RCC_APB2ENR_IOPFEN;
            gpio_configure_pin(gpiof, 7, GPI_pull_down);
            delay_us(100);
            has_kc30_header = (gpio_read_pin(gpiof, 7) == LOW);
            gpio_configure_pin(gpiof, 7, GPI_floating);
#endif

        }

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

/*
 * gotek/board.c
 * 
 * Gotek board-specific setup and management.
 * 
 * SFRC922, SFRC922C, SFRC922D et al
 *  Original LQFP64 designs, using STM or AT chips.
 *  Buttons: PC6 = Select, PC7 = Right, PC8 = Left
 *  Rotary:  PC10, PC11
 * 
 * SFRC922AT3
 *  LQFP48 design, missing rotary header.
 *  Alternative rotary location at PA13, PA14
 *  Buttons: PA5 = Select, PA4 = Right, PA3 = Left
 * 
 * SFRKC30AT4, SFRKC30.AT4, SFRKC30.AT4.7 (KC30 Rev 1)
 *  LQFP64 designs with original rotary header and "KC30" rotary header.
 *  Buttons: PA5 = Select, PA4 = Right, PA3 = Left
 *  Rotary:  PC10, PC11
 *  KC30: PF6/PH2 = Select, PA6/PA15 = Rotary
 * 
 * SFRKC30AT3 (KC30 Rev 1)
 *  LQFP48 design similar to SFRC922AT3 but with the "KC30" rotary header.
 *  Buttons: PA5 = Select, PA4 = Right, PA3 = Left
 *  KC30: PF6/PH2 = Select, PA6/PA15 = Rotary
 * 
 * SFRKC30.AT2 (KC30 Rev 1)
 *  QFN32 design with various pin changes and features missing. There are
 *  two versions; the newer version reintroduces jumper position JC.
 *  Missing:
 *   * Original rotary header
 *   * JC jumper position (old version)
 *  Relocated to new MCU pins:
 *   * Display header is moved to PB[7:6] using I2C1 instead of I2C2
 *   * KC30 header SELECT/button pin
 *   * Floppy output pins 2 and 26
 *   * Floppy WGATE input pin
 *   * JC jumper at PA9 (new version)
 *  Buttons: PA5 = Select, PA4 = Right, PA3 = Left
 *  KC30: PA10 = Select, PA6/PA15 = Rotary
 * 
 * SFRKC30.AT4.35 (KC30 Rev 2)
 *  As SFRKC30.AT4 except PC15 is tied HIGH for identification.
 *  MOTOR (pin 16) is optionally jumpered to PB12 with 1k pullup to 5v.
 * 
 * Written & released by Keir Fraser <keir.xen@gmail.com>
 * 
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

uint8_t mcu_package;
uint8_t has_kc30_header;

#if MCU == MCU_stm32f105
#define kc30_sel_gpio gpiof
#define kc30_sel_pin  6
#elif MCU == MCU_at32f435
#define kc30_sel_gpio gpioh
#define kc30_sel_pin  2
#endif

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
     *  PA5 = Select, PA4 = Right, PA3 = Left. 
     * Note: "Enhanced Gotek" design uses these pins so must skip them here. */
    unsigned int x = (board_id == BRDREV_Gotek_standard)
        ? gpioa->idr >> 3 : -1;
    /* Earlier Gotek revisions (all of which are LQFP64): 
     *  PC6 = Select, PC7 = Right, PC8 = Left. */
    if (mcu_package == MCU_LQFP64)
        x &= _rbit32(gpioc->idr) >> 23;
    x = ~x & 7;

#if (TARGET == TARGET_apple2) || defined(APPLE2_BOOTLOADER)
    /* Apple 2: QFN32 select pin PA10 is reassigned as stepper phase #0. */
    if (mcu_package == MCU_QFN32)
        return x;
#endif
    if (has_kc30_header) {
        /* KC30 Select pin, Artery models only: 
         *  PF6/PH2 = Select; except QFN32: PA10 = Select. */
        unsigned int kc30 = (mcu_package == MCU_QFN32
                             ? gpioa->idr >> (10-2)  /* PA10 */
                             : kc30_sel_gpio->idr >> (kc30_sel_pin-2));
        x |= ~kc30 & 4;
    }

    return x;
}

unsigned int board_get_rotary(void)
{
    unsigned int x = 3;
    if ((mcu_package != MCU_QFN32) && (ff_cfg.chgrst != CHGRST_pa14)) {
        /* Alternative location at PA13, PA14. */
        x &= gpioa->idr >> 13;
    }
    if (mcu_package == MCU_LQFP64) {
        /* Original rotary header at PC10, PC11. */
        x &= gpioc->idr >> 10;
    }
    if (has_kc30_header) {
        /* KC30 rotary pins PA6, PA15. */
        unsigned int kc30 = gpioa->idr;
        kc30 = ((kc30>>6)&1) | ((kc30>>(15-1))&2);
        x &= kc30;
    }
    return x;
}

uint32_t board_rotary_exti_mask;
void board_setup_rotary_exti(void)
{
    uint32_t m = 0;
    if ((mcu_package != MCU_QFN32) && (ff_cfg.chgrst != CHGRST_pa14)) {
        /* Alternative location at PA13, PA14. */
        exti_route_pa(13);
        exti_route_pa(14);
        m |= m(13) | m(14);
    }
    if (mcu_package == MCU_LQFP64) {
        /* Original rotary header at PC10, PC11. */
        exti_route_pc(10);
        exti_route_pc(11);
        m |= m(10) | m(11);
    }
    if (((has_kc30_header == 1) && (ff_cfg.motor_delay == MOTOR_ignore))
        || (has_kc30_header == 2) /* No conflict with motor on PB12 */) {
        /* KC30 rotary pins PA6, PA15. */
        exti_route_pa(6);
        exti_route_pa(15);
        m |= m(6) | m(15);
    }
    board_rotary_exti_mask = m;
    exti->rtsr |= m;
    exti->ftsr |= m;
    exti->imr |= m;
}

void board_jc_set_mode(unsigned int mode)
{
    if (mcu_package == MCU_QFN32) {
#if LEVEL == LEVEL_debug
        /* PA9 is used for serial tx */
#else
        gpio_configure_pin(gpioa, 9, mode);
#endif
    } else {
        gpio_configure_pin(gpiob, 1, mode);
    }
}

bool_t board_jc_strapped(void)
{
    if (mcu_package == MCU_QFN32) {
#if LEVEL == LEVEL_debug
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

#if LEVEL == LEVEL_debug
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
            mcu_package = MCU_QFN32;
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

        if (mcu_package == MCU_QFN32) {

            /* The sole QFN32 board is a KC30 Rev 1 design. */
            has_kc30_header = 1;

            pa_skip &= ~(1<<10); /* PA10 is not used as serial rx */
            pb_skip |= 1<<1; /* PB1 is a floppy input (WGATE) */

        } else {

            /* 48-pin package has PC12 permanently LOW. */
            if (!(id & 1))
                mcu_package = MCU_LQFP48;

            /* Check for KC30 Rev 2. */
            gpio_configure_pin(gpioc, 15, GPI_pull_down);
            delay_us(100);

            if (gpio_read_pin(gpioc, 15) == HIGH) {

                /* KC30 Rev 2. */
                has_kc30_header = 2;
                pb_skip |= 1<<12; /* PB12 is a floppy input (MOTOR) */

            } else {

                /* If PF7 is floating then we are running on a board with the
                 * optional rotary-encoder header (SFRKC30 Rev 1). On earlier
                 * boards PF6=VSS and PF7=VDD, hence we take care here. */
#if MCU == MCU_stm32f105 /* Only AT32F415 has the PF7 pin. */
                rcc->apb2enr |= RCC_APB2ENR_IOPFEN;
                gpio_configure_pin(gpiof, 7, GPI_pull_down);
                delay_us(100);
                if (gpio_read_pin(gpiof, 7) == LOW) {
                    /* KC30 Rev 1. */
                    has_kc30_header = 1;
                }
                gpio_configure_pin(gpiof, 7, GPI_floating);
#endif
            }

        }

        if (has_kc30_header)
            gpio_configure_pin(kc30_sel_gpio, kc30_sel_pin, GPI_pull_up);

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

#if TARGET == TARGET_apple2
#if LEVEL != LEVEL_debug
    /* Normal build: Two phases use UART RX/TX. */
    pa_skip |= m(9) | m(10);
#else
    /* Debug build: Move the two UART phases to the KC30 header. */
    pa_skip |= m(6) | m(15);
    has_kc30_header = 0;
#endif
#endif

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

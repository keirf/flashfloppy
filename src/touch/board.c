/*
 * touch/board.c
 * 
 * Touch board-specific setup and management.
 * 
 * Written & released by Keir Fraser <keir.xen@gmail.com>
 * 
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

void board_init(void)
{
    uint16_t id;

    /* Test whether PC13-15 are externally pulled low. We pull each line up to
     * 3.3v via the internal weak pullup (<50k resistance). Load on each line
     * is conservatively <50pF, allowing for LSE crystal load caps. Need to
     * wait time T for input to reach 1.71v to read as HIGH.
     * T = -RCln(1-Vthresh/Vcc) = -50k*50p*ln(1-1.71/3.3) ~= 1.9us. */
    gpioc->odr = 0xffffu;
    gpioc->crh = 0x88888888u; /* PC8-15: Input with pull-up */
    delay_us(5); /* 1.9us is a tiny delay so fine to pad it some more */
    id = (gpioc->idr >> 13) & 7; /* ID should now be stable at PC[15:13] */

    /* Analog Input: disables Schmitt Trigger Inputs hence zero load for any 
     * voltage at the input pin (and voltage build-up is clamped by protection 
     * diodes even if the pin floats). 
     * NB. STMF4xx spec states that Analog Input is not safe for 5v operation. 
     * It's unclear whether this might apply to STMF1xx devices too, so for 
     * safety's sake set Analog Input only on pins not driven to 5v. */
    gpioc->crh = gpioc->crl = 0; /* PC0-15: Analog Input */

    /* Selective external pulldowns define a board identifier.
     * Check if it's one we recognise and pull down any floating pins. */
    switch (board_id = id) {
    case BRDREV_LC150: /* LC Tech */
        /* PB8/9: unused, floating. */
        gpio_configure_pin(gpiob, 8, GPI_pull_down);
        gpio_configure_pin(gpiob, 9, GPI_pull_down);
        /* PB2 = BOOT1: externally tied. */
       break;
    case BRDREV_MM150: /* Maple Mini */
        /* PB1: LED connected to GND. */
        gpio_configure_pin(gpiob, 1, GPI_pull_down);
        /* PB8 = Button, PB9 = USB DISConnect: both externally tied. */
        break;
    case BRDREV_TB160: /* "Taobao" / Blue Pill / etc. */
        /* PA13/14: SW-debug, floating. */
        gpio_configure_pin(gpioa, 13, GPI_pull_down);
        gpio_configure_pin(gpioa, 14, GPI_pull_down);
        /* PB2 = BOOT1: externally tied. */
        break;
    default:
        printk("Unknown board ID %x\n", id);
        ASSERT(0);
    }
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

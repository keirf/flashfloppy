/*
 * io_test.c
 * 
 * An alternative main firmware to test the STM32 I/O pins.
 * 
 * Written & released by Keir Fraser <keir.xen@gmail.com>
 * 
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

int EXC_reset(void) __attribute__((alias("main")));

uint8_t board_id;

#define GPIOA 0x00
#define GPIOB 0x10
#define GPIOC 0x20

#define DOWN  0x40
#define UP    0x00

static GPIO gpio(uint8_t x)
{
    switch (x&0x30) {
    case GPIOA: return gpioa;
    case GPIOB: return gpiob;
    case GPIOC: return gpioc;
    }
    return (GPIO)0xdeadbeef;
}

static uint8_t inputs[] = {
    /* Weak pulldowns should be defeated by external 1k pullups on
     * supported signal lines. Drive Select B and Motor On are unconnected
     * on a standard Gotek and will be held permanently low by the pulldown. */
    GPIOA |  0 | DOWN, /* xx: Drive Select A */
    GPIOA |  3 | DOWN, /* xx: Drive Select B */
    GPIOA | 15 | DOWN, /* 16: Motor On */
    GPIOB |  0 | DOWN, /* 18: Direction */
    GPIOA |  1 | DOWN, /* 20: Step */
    GPIOA |  8 | DOWN, /* 22: Write Data */
    GPIOB |  9 | DOWN, /* 24: Write Gate */
    GPIOB |  4 | DOWN, /* 32: Side Select */

    /* Buttons and encoder inputs are switch-to-ground. So we pull them up. */
    GPIOC |  8 | UP   , /* Button: Down/Left */
    GPIOC |  7 | UP   , /* Button: Up/Right */
    GPIOC |  6 | UP   , /* Button: Select (Jumper JA) */
    GPIOC | 10 | UP   , /* Rotary CLK (J7-1) */
    GPIOC | 11 | UP   , /* Rotary DAT (J7-2) */
};

static uint8_t outputs[] = {
    GPIOB |  7        , /*  2: Disk Change/Density */
    GPIOB |  8        , /*  8: Index */
    GPIOB |  6        , /* 26: Track 0 */
    GPIOB |  5        , /* 28: Write Protect */
    GPIOA |  7        , /* 30: Read Data */
    GPIOB |  3        , /* 34: Disk Change/Ready */
};

static void io_test(bool_t assert)
{
    const static char char_map[] = "0123456789ABCDEF";

    uint8_t d[3] = { 0 };
    char p[20], *q = p;
    int i;

    for (i = 0; i < ARRAY_SIZE(inputs); i++) {
        uint8_t x = inputs[i];
        if (gpio_read_pin(gpio(x), x&15)) {
            *q++ = char_map[i];
            d[i/7] |= 1 << (i%7);
        } else {
            *q++ = ' ';
        }
    }

    *q = '\0';
    if (assert) {
        d[1] |= 0x40;
        d[2] |= 0x40;
    }

    switch (display_mode) {
    case DM_LED_7SEG:
        led_7seg_write_raw(d);
        break;
    case DM_LCD_OLED:
        lcd_write(0, 0, 0, p);
        lcd_write(1, 1, -1, assert ? "HI 888" : "LO");
        break;
    }
}

static void display_setting(bool_t on)
{
    switch (display_mode) {
    case DM_LED_7SEG:
        led_7seg_display_setting(on);
        break;
    case DM_LCD_OLED:
        lcd_backlight(on);
        lcd_sync();
        break;
    }
}

int main(void)
{
    bool_t assert = FALSE;
    int i;

    /* Relocate DATA. Initialise BSS. */
    if (_sdat != _ldat)
        memcpy(_sdat, _ldat, _edat-_sdat);
    memset(_sbss, 0, _ebss-_sbss);

    /* Initialise the world. */
    stm32_init();
    time_init();
    console_init();
    board_init();
    delay_ms(200); /* 5v settle */

    printk("\n** FF I/O Test for Gotek\n", fw_ver);
    printk("** Keir Fraser <keir.xen@gmail.com>\n");
    printk("** https://github.com/keirf/FlashFloppy\n\n");

    speaker_init();
    flash_ff_cfg_read();
    display_init();
    display_setting(TRUE);

    /* Standard Gotek: optional motor signal is PB15. */
    if (!gotek_enhanced())
        inputs[2] |= GPIOB;

    for (i = 0; i < ARRAY_SIZE(inputs); i++) {
        uint8_t x = inputs[i];
        gpio_configure_pin(gpio(x), x&15,
                           (x&DOWN) ? GPI_pull_down : GPI_pull_up);
    }

    for (i = 0; i < ARRAY_SIZE(outputs); i++) {
        uint8_t x = outputs[i];
        gpio_configure_pin(gpio(x), x&15, GPO_pushpull(_2MHz,FALSE));
    }

    for (;;) {

        time_t t = time_now();

        speaker_pulse();

        assert ^= 1;
        for (i = 0; i < ARRAY_SIZE(outputs); i++) {
            uint8_t x = outputs[i];
            gpio_write_pin(gpio(x), x&15, assert);
        }

        while (time_diff(t, time_now()) < time_ms(2000)) {
            io_test(assert);
            delay_ms(100);
        }
    }

    return 0;
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

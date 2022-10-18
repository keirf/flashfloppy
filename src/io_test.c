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

#define INVALID 0xff

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
};
#if 0 /* Buttons and encoder are handled via board.c */
    /* Buttons and encoder inputs are switch-to-ground. So we pull them up. */
    GPIOC |  8 | UP   , /* Button: Down/Left */
    GPIOC |  7 | UP   , /* Button: Up/Right */
    GPIOC |  6 | UP   , /* Button: Select (Jumper JA) */
    GPIOC | 10 | UP   , /* Rotary CLK (J7-1) */
    GPIOC | 11 | UP   , /* Rotary DAT (J7-2) */
#endif

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
    int i, b;

    /* 0-7 */
    for (i = 0; i < ARRAY_SIZE(inputs); i++) {
        uint8_t x = inputs[i];
        if ((x != INVALID) && gpio_read_pin(gpio(x), x&15)) {
            *q++ = char_map[i];
            d[i/7] |= 1 << (i%7);
        } else {
            *q++ = ' ';
        }
    }

    /* 8-12 */
    b = (~board_get_buttons() & 7) | (board_get_rotary() << 3);
    for (; i < 13; i++) {
        if (b & (1<<(i-8))) {
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

    switch (display_type) {
    case DT_LED_7SEG:
        led_7seg_write_raw(d);
        break;
    case DT_LCD_OLED:
        lcd_write(0, 0, 0, p);
        lcd_write(1, 1, -1, assert ? "HI 888" : "LO");
        break;
    }
}

static void display_setting(bool_t on)
{
    switch (display_type) {
    case DT_LED_7SEG:
        led_7seg_display_setting(on);
        break;
    case DT_LCD_OLED:
        lcd_backlight(on);
        lcd_sync();
        break;
    }
}

/* 32-bit sequence of length 2^32-1 (all 32-bit values except zero). */
static inline always_inline uint32_t lfsr(uint32_t x)
{
    return (x & 1) ? (x>>1) ^ 0x80000062 : (x>>1);
}

static void fatal(uint32_t *p, uint32_t exp, uint32_t saw)
{
    char msg[32];
    snprintf(msg, sizeof(msg), "%p", p);
    lcd_write(0, 0, -1, msg);
    snprintf(msg, sizeof(msg), "%08x %08x", exp, saw);
    lcd_write(0, 1, -1, msg);
    for (;;);
}

static void memory_test(void)
{
    uint32_t *s = (uint32_t *)_ebss;
    uint32_t *e = (uint32_t *)((char *)0x20000000 + ram_kb*1024);
    uint32_t *p;
    uint32_t _r = 0x12341234, r;
    char msg[32];
    unsigned int i;

    snprintf(msg, sizeof(msg), "%08x %08x", s, e);
    lcd_write(0, 1, -1, msg);

    for (i = 0;;i++) {
        snprintf(msg, sizeof(msg), "%06d %08x", i, _r);
        lcd_write(0, 0, -1, msg);
        r = _r;
        for (p = s; p < e; p++) {
            *p = r;
            r = lfsr(r);
        }
        delay_ms(10);
        r = _r;
        for (p = s; p < e; p++) {
            if (*p != r)
                fatal(p, r, *p);
            r = lfsr(r);
        }
        _r = r;
    }
}

int main(void)
{
    bool_t assert = FALSE;
    int i;

    /* Relocate DATA. Initialise BSS. */
    if (&_sdat[0] != &_ldat[0])
        memcpy(_sdat, _ldat, _edat-_sdat);
    memset(_sbss, 0, _ebss-_sbss);

    /* Initialise the world. */
    stm32_init();
    time_init();
    console_init();
    board_init();
    delay_ms(200); /* 5v settle */

    printk("\n** FF I/O Test %s for Gotek\n", fw_ver);
    printk("** Keir Fraser <keir.xen@gmail.com>\n");
    printk("** https://github.com/keirf/FlashFloppy\n\n");

    speaker_init();
    flash_ff_cfg_read();
    display_init();
    display_setting(TRUE);

    memory_test();


    if (mcu_package == MCU_QFN32) {
        inputs[6] = GPIOB | 1 | DOWN; /* wgate */
        outputs[0] = GPIOA | 14; /* pin_02, dskchg/den */
        outputs[2] = GPIOA | 13; /* pin_26, trk00 */
    }

    if (!gotek_enhanced()) {
        inputs[1] = INVALID; /* no SELB */
        if (has_kc30_header == 2) {
            inputs[2] = GPIOB | 12 | DOWN;
        } else {
            /* Standard Gotek: optional motor signal is PB15. */
            inputs[2] = GPIOB | 15 | DOWN;
        }
    }

    for (i = 0; i < ARRAY_SIZE(inputs); i++) {
        uint8_t x = inputs[i];
        if (x == INVALID)
            continue;
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

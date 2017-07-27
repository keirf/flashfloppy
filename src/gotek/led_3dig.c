/*
 * led_3dig.c
 * 
 * Drive 3-digit 7-segment display via TM1651 driver IC.
 * I2C-style serial protocol: DIO=PB10, CLK=PB11
 * 
 * TM1651 specified f_max is 500kHz with 50% duty cycle, so clock should change 
 * change value no more often than 1us. We clock with half-cycle 20us so we
 * are very conservative.
 * 
 * Written & released by Keir Fraser <keir.xen@gmail.com>
 * 
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

/* Full clock cycle is 8us (freq = 125kHZ) */
#define CYCLE 8

/* Brightness range is 0-7: 
 * 0 is very dim
 * 1-2 are easy on the eyes 
 * 3-7 are varying degrees of retina burn */
#define BRIGHTNESS 1

/* DAT = PB10, CLK = PB11 */
#define DAT_PIN 10
#define CLK_PIN 11

static const uint8_t letters[] = {
    0x77, 0x7c, 0x58, 0x5e, 0x79, 0x71, 0x6f, 0x74, 0x06, /* a-i */
    0x0e, 0x00, 0x38, 0x00, 0x54, 0x5c, 0x73, 0x67, 0x50, /* j-r */
    0x6d, 0x78, 0x1c, 0x00, 0x00, 0x76, 0x6e, 0x00        /* s-z */
};

static const uint8_t digits[] = {
    0x3f, 0x06, 0x5b, 0x4f, 0x66, 0x6d, 0x7d, 0x07, 0x7f, 0x6f
};

static void set_pin(uint8_t pin, uint8_t level)
{
    /* Simulate open drain with passive pull up. */
    if (level)
        gpio_configure_pin(gpiob, pin, GPI_pull_up);
    else
        gpio_configure_pin(gpiob, pin, GPO_opendrain(_2MHz, LOW));
}

#define set_dat(level) set_pin(DAT_PIN, level)
#define set_clk(level) set_pin(CLK_PIN, level)

static bool_t write(uint8_t x)
{
    bool_t fail = FALSE;
    uint16_t y = x | 0x100;

    /* 8 data bits, LSB first, driven onto DAT line while CLK is LOW.
     * Check for ACK during 9th CLK LOW half-period: we pull DAT HIGH
     * but TM1651 should drive DAT LOW. */
    do {
        set_clk(LOW);
        delay_us(CYCLE/4);

        set_dat(y & 1);
        delay_us(CYCLE/8);
        if (y == 1) {
            /* ACK: has TM1651 driven DAT LOW? */
            fail = gpio_read_pin(gpiob, DAT_PIN);
            /* Now we must drive it LOW ourselves before TM1651 releases. */
            set_dat(0);
        }
        delay_us(CYCLE/8);

        set_clk(HIGH);
        delay_us(CYCLE/2);

    } while (y >>= 1);

    return fail;
}

static void start(void)
{
    /* DAT HIGH-to-LOW while CLK is HIGH. */
    set_clk(LOW);
    delay_us(CYCLE/2);

    set_clk(HIGH);
    delay_us(CYCLE/4);

    set_dat(LOW);
    delay_us(CYCLE/4);
}

static void stop(void)
{
    /* DAT LOW-to-HIGH while CLK is HIGH. */
    set_clk(LOW);
    delay_us(CYCLE/2);

    set_clk(HIGH);
    delay_us(CYCLE/4);

    set_dat(HIGH);
    delay_us(CYCLE/4);
}

static bool_t send_cmd(uint8_t cmd)
{
    bool_t fail = TRUE;
    int retry;

    for (retry = 0; fail && (retry < 3); retry++) {
        start();
        fail = write(cmd);
        stop();
    }

    return fail;
}

void led_3dig_display_setting(bool_t enable)
{
    send_cmd(enable ? 0x88 + BRIGHTNESS : 0x80);
}

void led_3dig_write(const char *p)
{
    bool_t fail = TRUE;
    uint8_t d[3], c;
    int i, retry;

    for (i = 0; i < 3; i++) {
        c = *p++;
        if ((c >= '0') && (c <= '9')) {
            d[i] = digits[c - '0'];
        } else if ((c >= 'a') && (c <= 'z')) {
            d[i] = letters[c - 'a'];
        } else if ((c >= 'A') && (c <= 'Z')) {
            d[i] = letters[c - 'A'];
        } else if (c == '-') {
            d[i] = 0x40;
        } else {
            d[i] = 0;
        }
    }

    for (retry = 0; fail && (retry < 3); retry++) {
        start();
        fail = (write(0xc0)      /* set addr 0 */
                || write(d[0])   /* dat0 */
                || write(d[1])   /* dat1 */
                || write(d[2])   /* dat2 */
                || write(0x00)); /* dat3 */
        stop();
    }
}

bool_t led_3dig_init(void)
{
    set_dat(HIGH);
    set_clk(HIGH);

    /* Data command: write registers, auto-increment address. 
     * Also check the controller is sending ACKs. If not, we must assume 
     * no LED controller is attached. */
    if (send_cmd(0x40))
        return FALSE;

    /* Clear the registers. */
    led_3dig_write("    ");

    /* Display control: brightness. */
    send_cmd(0x88 + BRIGHTNESS);

    return TRUE;
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

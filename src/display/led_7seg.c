/*
 * led_7seg.c
 * 
 * Drive 3-digit 7-segment display via TM1651 driver IC.
 * I2C-style serial protocol: DIO=PB10, CLK=PB11
 * 
 * TM1651 specified f_max is 500kHz with 50% duty cycle, so clock should change 
 * change value no more often than 1us. We clock with half-cycle 20us so we
 * are very conservative.
 * 
 * Drive 2-digit 7-segment display via a pair of 74HC164 shift registers.
 * SERIAL_DATA=PB10, CLK=PB11.
 * 
 * Written & released by Keir Fraser <keir.xen@gmail.com>
 * 
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

/* TM1651, 74HC164: Full clock cycle is 8us (freq = 125kHZ) */
#define CYCLE 8

/* TM1651, 74HC164: DAT = PB10, CLK = PB11 */
#define DAT_PIN 10
#define CLK_PIN 11

/* TM1651, 74HC164: Alphanumeric segment arrangements.  */
static const uint8_t letters[] = {
    0x77, 0x7c, 0x58, 0x5e, 0x79, 0x71, 0x6f, 0x74, 0x04, /* a-i */
    0x0e, 0x08, 0x38, 0x40, 0x54, 0x5c, 0x73, 0x67, 0x50, /* j-r */
    0x6d, 0x78, 0x1c, 0x09, 0x41, 0x76, 0x6e, 0x00        /* s-z */
};

static const uint8_t digits[] = {
    0x3f, 0x06, 0x5b, 0x4f, 0x66, 0x6d, 0x7d, 0x07, 0x7f, 0x6f
};

static uint8_t nr_digits;


/*********
 * TM1651 (3-digit) display.
 */

/* Brightness range is 0-7: 
 * 0 is very dim
 * 1-2 are easy on the eyes 
 * 3-7 are varying degrees of retina burn */
#define TM1651_BRIGHTNESS 1

static void tm1651_set_pin(uint8_t pin, uint8_t level)
{
    /* Simulate open drain with passive pull up. */
    if (level)
        gpio_configure_pin(gpiob, pin, GPI_pull_up);
    else
        gpio_configure_pin(gpiob, pin, GPO_opendrain(_2MHz, LOW));
}

#define tm1651_set_dat(level) tm1651_set_pin(DAT_PIN, level)
#define tm1651_set_clk(level) tm1651_set_pin(CLK_PIN, level)

static bool_t tm1651_write(uint8_t x)
{
    bool_t fail = FALSE;
    uint16_t y = x | 0x100;

    /* 8 data bits, LSB first, driven onto DAT line while CLK is LOW.
     * Check for ACK during 9th CLK LOW half-period: we pull DAT HIGH
     * but TM1651 should drive DAT LOW. */
    do {
        tm1651_set_clk(LOW);
        delay_us(CYCLE/4);

        tm1651_set_dat(y & 1);
        delay_us(CYCLE/8);
        if (y == 1) {
            /* ACK: has TM1651 driven DAT LOW? */
            fail = gpio_read_pin(gpiob, DAT_PIN);
            /* Now we must drive it LOW ourselves before TM1651 releases. */
            tm1651_set_dat(0);
        }
        delay_us(CYCLE/8);

        tm1651_set_clk(HIGH);
        delay_us(CYCLE/2);

    } while (y >>= 1);

    return fail;
}

static void tm1651_start(void)
{
    /* DAT HIGH-to-LOW while CLK is HIGH. */
    tm1651_set_clk(LOW);
    delay_us(CYCLE/2);

    tm1651_set_clk(HIGH);
    delay_us(CYCLE/4);

    tm1651_set_dat(LOW);
    delay_us(CYCLE/4);
}

static void tm1651_stop(void)
{
    /* DAT LOW-to-HIGH while CLK is HIGH. */
    tm1651_set_clk(LOW);
    delay_us(CYCLE/2);

    tm1651_set_clk(HIGH);
    delay_us(CYCLE/4);

    tm1651_set_dat(HIGH);
    delay_us(CYCLE/4);
}

static bool_t tm1651_send_cmd(uint8_t cmd)
{
    bool_t fail = TRUE;
    int retry;

    for (retry = 0; fail && (retry < 3); retry++) {
        tm1651_start();
        fail = tm1651_write(cmd);
        tm1651_stop();
    }

    return fail;
}

static void tm1651_update_display(const uint8_t *d)
{
    bool_t fail = TRUE;
    int retry;

    for (retry = 0; fail && (retry < 3); retry++) {
        tm1651_start();
        fail = (tm1651_write(0xc0)      /* set addr 0 */
                || tm1651_write(d[0])   /* dat0 */
                || tm1651_write(d[1])   /* dat1 */
                || tm1651_write(d[2])   /* dat2 */
                || tm1651_write(0x00)); /* dat3 */
        tm1651_stop();
    }
}

static bool_t tm1651_init(void)
{
    tm1651_set_dat(HIGH);
    tm1651_set_clk(HIGH);

    /* Data command: write registers, auto-increment address. 
     * Also check the controller is sending ACKs. If not, we must assume 
     * no LED controller is attached. */
    return tm1651_send_cmd(0x40);
}


/*********
 * Shift register (2-digit, 74HC164-based) display.
 */

static void shiftreg_update_display_u16(uint16_t x)
{
    unsigned int i;

    /* 16 data bits to clock through the pair of 74HC164 registers. 
     * Data is clocked on rising edge of clock. */
    for (i = 0; i < 16; i++) {
        gpio_write_pin(gpiob, CLK_PIN, LOW);
        delay_us(CYCLE/4);
        gpio_write_pin(gpiob, DAT_PIN, (int16_t)x < 0);
        delay_us(CYCLE/4);
        gpio_write_pin(gpiob, CLK_PIN, HIGH);
        delay_us(CYCLE/2);
        x <<= 1;
    }

    /* Leave DAT high at rest, so Gotek's red LED isn't illuminated. */
    gpio_write_pin(gpiob, DAT_PIN, HIGH);
}

static uint16_t shiftreg_curval;

static void shiftreg_update_display(const uint8_t *d)
{
    uint16_t x = ((uint16_t)d[0] << 8) | d[1];
    shiftreg_curval = x;
    shiftreg_update_display_u16(x);
}

static void shiftreg_display_setting(bool_t enable)
{
    shiftreg_update_display_u16(enable ? shiftreg_curval : 0);
}

static void shiftreg_init(void)
{
    gpio_configure_pin(gpiob, DAT_PIN, GPO_pushpull(_2MHz, HIGH));
    gpio_configure_pin(gpiob, CLK_PIN, GPO_pushpull(_2MHz, HIGH));
}


/*********
 * Generic public API.
 */

int led_7seg_nr_digits(void)
{
    return nr_digits;
}

void led_7seg_display_setting(bool_t enable)
{
    if (nr_digits == 3)
        tm1651_send_cmd(enable ? 0x88 + TM1651_BRIGHTNESS : 0x80);
    else
        shiftreg_display_setting(enable);
}

void led_7seg_write_raw(const uint8_t *d)
{
    if (nr_digits == 3)
        tm1651_update_display(d);
    else
        shiftreg_update_display(d);
}

void led_7seg_write_string(const char *p)
{
    uint8_t d[3] = { 0 }, c;
    unsigned int i;

    for (i = 0; ((c = *p++) != '\0') && (i < sizeof(d)); i++) {
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

    led_7seg_write_raw(d);
}

void led_7seg_write_decimal(unsigned int val)
{
    char msg[4];
    snprintf(msg, sizeof(msg),
             (nr_digits == 3) ? "%03u" : "%02u",
             val % ((nr_digits == 3) ? 1000 : 100));
    led_7seg_write_string(msg);
}

void led_7seg_init(void)
{
    nr_digits = !tm1651_init() ? 3 : 2;
    if (nr_digits == 2)
        shiftreg_init();

    led_7seg_write_string("");
    led_7seg_display_setting(TRUE);
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

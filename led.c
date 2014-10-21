/*
 * led.c
 * 
 * Drive 4-digit display via TM1651 driver IC.
 * I2C-style serial protocol: DIO=PB10, CLK=PB11
 * 
 * Written & released by Keir Fraser <keir.xen@gmail.com>
 * 
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

#include "stm32f10x.h"
#include "intrinsics.h"

/* Brightness range is 0-7: 
 * 0 is very dim
 * 1-2 are easy on the eyes 
 * 3-7 are varying degrees of retina burn */
#define BRIGHTNESS 1

/* Specified f_max is 500kHz with 50% duty cycle, so clock should change 
 * change value no more often than 1us. Pick 2us here conservatively. 
 * Actually we can clock much faster than this and, conversely, very much 
 * slower too. */
#define DELAY_US 2

/* Serial bus */
#define DAT 10 /* PB10 */
#define CLK 11 /* PB11 */

static void write_clk(unsigned int val)
{
    gpio_write_pin(gpiob, CLK, val);
    delay_us(DELAY_US);
}

static void write_dat(unsigned int val)
{
    gpio_write_pin(gpiob, DAT, val);
    delay_us(DELAY_US);
}

static const uint8_t digits[] = {
    0x3f, 0x06, 0x5b, 0x4f, 0x66, 0x6d, 0x7d, 0x07, /* 0-7 */
    0x7f, 0x6f, 0x77, 0x7c, 0x39, 0x5e, 0x79, 0x71, /* 8-f */
};

static void write(uint8_t x)
{
    unsigned int i;

    /* Data, LSB first */
    for (i = 0; i < 8; i++) {
        write_dat(x&1);
        x >>= 1;
        write_clk(1);
        write_clk(0);
    }

    write_dat(0);
    write_clk(1);
    write_clk(0);
}

static void start(void)
{
    write_dat(0);
    write_clk(0);
}

static void stop(void)
{
    write_clk(1);
    write_dat(1);
}

void leds_write_hex(unsigned int x)
{
    start();
    write(0xc0);              /* set addr 0 */
    write(digits[(x>>8)&15]); /* dat0 */
    write(digits[(x>>4)&15]); /* dat1 */
    write(digits[(x>>0)&15]); /* dat2 */
    write(0x00);              /* dat3 */
    stop();
}

void leds_init(void)
{
    /* Prepare the bus. */
    gpio_configure_pin(gpiob, DAT, GPO_pushpull);
    gpio_configure_pin(gpiob, CLK, GPO_pushpull);
    write_clk(0);
    write_dat(0);
    stop();

    /* Data command: write registers, auto-increment address. */
    start();
    write(0x40);
    stop();

    /* Display control: low brightness. */
    start();
    write(0x88 + BRIGHTNESS);
    stop();
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

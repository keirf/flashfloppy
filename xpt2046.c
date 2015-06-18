/*
 * xpt2046.c
 * 
 * Drive the XPT2046 resistive touch panel IC.
 * 
 * Written & released by Keir Fraser <keir.xen@gmail.com>
 * 
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

#define gpio gpiob
#define PIN_IRQ 0
#define PIN_CS  1

/* We clock the SPI dead slow, giving plenty of settling time during 
 * sample acquisition. */
#define spi spi1
#define SPI_BR_DIV SPI_CR1_BR_DIV256 /* 281kHz, 3.6us cycle */
/* 8-bit mode, MSB first, CPOL Low, CPHA Leading Edge. */
#define SPI_CR1 (SPI_CR1_MSTR | /* master */                    \
                 SPI_CR1_SSM | SPI_CR1_SSI | /* software NSS */ \
                 SPI_CR1_SPE |                                  \
                 SPI_BR_DIV)

/* EXTI0: IRQ 6 */
void IRQ_6(void) __attribute__((alias("IRQ_touch")));

static void spi_acquire(void)
{
    spi->cr1 = SPI_CR1;
    gpio_write_pin(gpio, PIN_CS, 0);
}

static void spi_release(void)
{
    spi_quiesce(spi);
    gpio_write_pin(gpio, PIN_CS, 1);
}

static void get_xy_samples(uint8_t nr, uint16_t *px, uint16_t *py)
{
    uint16_t x, y;
    spi_acquire();
    (void)spi_xchg8(spi, 0x90);
    while (nr--) {
        x = (uint16_t)spi_xchg8(spi, 0) << 8;
        x |= spi_xchg8(spi, 0xd0);
        y = (uint16_t)spi_xchg8(spi, 0) << 8;
        y |= spi_xchg8(spi, nr ? 0x90 : 0);
        *px++ = (x >> 3) & 0xfff;
        *py++ = (y >> 3) & 0xfff;
    }
    spi_release();
}

bool_t touch_get_xy(uint16_t *px, uint16_t *py)
{
    uint16_t x[8], y[8];
    uint8_t i, j;

    /* Get raw samples. Ensure PENIRQ was active throughout. */
    if (gpio_read_pin(gpio, PIN_IRQ))
        return FALSE;
    get_xy_samples(8, x, y);
    if (gpio_read_pin(gpio, PIN_IRQ))
        return FALSE;

    /* Partial selection sort, to find median-of-7 in each dimension. Discard
     * the first sample; it's an outlier intolerably often. */
    for (i = 1; i < 5; i++) {
        for (j = i; j < 8; j++) {
            uint16_t t = x[i];
            if (t > x[j]) {
                x[i] = x[j];
                x[j] = t;
            }
            t = y[i];
            if (t > y[j]) {
                y[i] = y[j];
                y[j] = t;
            }
        }
    }

    /* Return the median. */
    *px = x[4];
    *py = y[4];

    return TRUE;
}

void touch_init(void)
{
    uint16_t x, y;

    /* Configure general-purpose I/Os. */
    gpio_configure_pin(gpio, PIN_IRQ, GPI_floating);
    gpio_configure_pin(gpio, PIN_CS, GPO_pushpull(_2MHz, HIGH));

    /* Floppy already initialised EXTI mappings for GPIOB. */

    /* ILI9341 already initialised SPI pins and general config. */

    /* Set PD0=PD1=0 (power-saving mode; PENIRQ active). */
    get_xy_samples(1, &x, &y);
}

static void IRQ_touch(void)
{
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

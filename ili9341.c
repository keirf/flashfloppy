/*
 * ili9341.c
 *
 * Drive the ILI9341 TFT LCD driver IC.
 *
 * Programming this device requires some cryptic initialisation sequence
 * which is taken from Adafruit's library. Therefore this file is licensed
 * under the following still-generous MIT terms:
 *
 * """
 * Adafruit invests time and resources providing this open source code,
 * please support Adafruit and open-source hardware by purchasing
 * products from Adafruit!
 *
 * Written by Limor Fried/Ladyada for Adafruit Industries.
 * Modified and adapted for STM32 by Keir Fraser <keir.xen@gmail.com>
 * MIT license, all text above must be included in any redistribution
 * """
 */

#if 1
/* Although ILI9341 is specified to run at only 10MHz for write cycles (and 
 * even less than that for read cycles, which we don't use), in practice parts 
 * seem to clock much faster and this success is echoed by other users. */
#define SPI_BR_DIV SPI_CR1_BR_DIV2 /* 36MHz(!) */
#define SPI_PIN_SPEED _50MHz
#else
/* 9MHz will do for now. Worried about signal quality, power requirements and
 * bandwidth requirements. */
#define SPI_BR_DIV SPI_CR1_BR_DIV8 /* 9MHz */
#define SPI_PIN_SPEED _10MHz
#endif


#define PIN_DCRS  1
#define PIN_RESET 2
#define PIN_CS    3
#define set_pin(pin, val) gpio_write_pin(gpioa, pin, val)

#define spi spi1

#define ILI9341_NOP     0x00
#define ILI9341_SWRESET 0x01
#define ILI9341_RDDID   0x04
#define ILI9341_RDDST   0x09
#define ILI9341_SLPIN   0x10
#define ILI9341_SLPOUT  0x11
#define ILI9341_PTLON   0x12
#define ILI9341_NORON   0x13
#define ILI9341_RDMODE  0x0A
#define ILI9341_RDMADCTL  0x0B
#define ILI9341_RDPIXFMT  0x0C
#define ILI9341_RDIMGFMT  0x0A
#define ILI9341_RDSELFDIAG  0x0F
#define ILI9341_INVOFF  0x20
#define ILI9341_INVON   0x21
#define ILI9341_GAMMASET 0x26
#define ILI9341_DISPOFF 0x28
#define ILI9341_DISPON  0x29
#define ILI9341_CASET   0x2A
#define ILI9341_PASET   0x2B
#define ILI9341_RAMWR   0x2C
#define ILI9341_RAMRD   0x2E
#define ILI9341_PTLAR   0x30
#define ILI9341_MADCTL  0x36
#define ILI9341_PIXFMT  0x3A
#define ILI9341_FRMCTR1 0xB1
#define ILI9341_FRMCTR2 0xB2
#define ILI9341_FRMCTR3 0xB3
#define ILI9341_INVCTR  0xB4
#define ILI9341_DFUNCTR 0xB6
#define ILI9341_PWCTR1  0xC0
#define ILI9341_PWCTR2  0xC1
#define ILI9341_PWCTR3  0xC2
#define ILI9341_PWCTR4  0xC3
#define ILI9341_PWCTR5  0xC4
#define ILI9341_VMCTR1  0xC5
#define ILI9341_VMCTR2  0xC7
#define ILI9341_RDID1   0xDA
#define ILI9341_RDID2   0xDB
#define ILI9341_RDID3   0xDC
#define ILI9341_RDID4   0xDD
#define ILI9341_GMCTRP1 0xE0
#define ILI9341_GMCTRN1 0xE1

#define BG_COL 0x0000u

extern const char font8x8[128][8];

static void spiwrite(uint16_t c)
{
    while (!(spi->sr & SPI_SR_TXE))
        cpu_relax();
    spi->dr = c;
}

static void spi_acquire(void)
{
    set_pin(PIN_CS, 0);
}

static void spi_release(void)
{
    spi_quiesce(spi);
    set_pin(PIN_CS, 1);
}

static void writecommand(uint8_t c)
{
    set_pin(PIN_DCRS, 0);
    spi_acquire();
    spiwrite(c);
    spi_release();
}

static void writedata(uint8_t c)
{
    set_pin(PIN_DCRS, 1);
    spi_acquire();
    spiwrite(c);
    spi_release();
}

static void set_addr_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    /* Column addr set */
    writecommand(ILI9341_CASET);
    writedata(x0 >> 8);
    writedata(x0 >> 0);
    writedata(x1 >> 8);
    writedata(x1 >> 0);

    /* Row addr set */
    writecommand(ILI9341_PASET);
    writedata(y0 >> 8);
    writedata(y0 >> 0);
    writedata(y1 >> 8);
    writedata(y1 >> 0);

    /* Write to RAM */
    writecommand(ILI9341_RAMWR);
}

static void fill_rect(
    uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t c)
{
    unsigned int i;
    set_addr_window(x, y, x+w-1, y+h-1);
    set_pin(PIN_DCRS, 1);
    spi_acquire();
    spi_16bit_frame(spi);
    for (i = 0; i < w*h; i++)
        spiwrite(c);
    spi_8bit_frame(spi);
    spi_release();
}

static void draw_char(uint16_t x, uint16_t y, unsigned char c)
{
    uint8_t i, j, k;

    set_addr_window(x, y, x+7, y+15);

    set_pin(PIN_DCRS, 1);
    spi_acquire();
    spi_16bit_frame(spi);

    for (j = 0; j < 16; j++) {
        k = font8x8[c][j/2];
        for (i = 0; i < 8; i++) {
            if (k & 1) {
                spiwrite(0xffff);
            } else {
                spiwrite(BG_COL);
            }
            k >>= 1;
        }
    }

    spi_8bit_frame(spi);
    spi_release();
}

static void draw_string(uint16_t x, uint16_t y, char *str)
{
    while (*str) {
        char c = *str++;
        draw_char(x, y, !(c & 0x80) ? c : 0);
        x += 8;
    }
}

/* Some cryptic command banging is required to set up the controller. 
 * Summarised here as <command>, <# data bytes>, <data...> */
const uint8_t init_seq[] = {
    0xef, 3, 0x03, 0x80, 0x02,
    0xcf, 3, 0x00, 0xc1, 0x30,
    0xed, 4, 0x64, 0x03, 0x12, 0x81,
    0xe8, 3, 0x85, 0x00, 0x78,
    0xcb, 5, 0x39, 0x2c, 0x00, 0x34, 0x02,
    0xf7, 1, 0x20,
    0xea, 2, 0x00, 0x00,
    ILI9341_PWCTR1, 1, 0x23,
    ILI9341_PWCTR2, 1, 0x10,
    ILI9341_VMCTR1, 2, 0x3e, 0x28,
    ILI9341_VMCTR2, 1, 0x86,
    ILI9341_MADCTL, 1, 0x28, /* 0xe8 here flips the display */
    ILI9341_PIXFMT, 1, 0x55,
    ILI9341_FRMCTR1, 2, 0x00, 0x18,
    ILI9341_DFUNCTR, 3, 0x08, 0x82, 0x27,
    0xf2, 1, 0x00, /* 3Gamma Function Disable */
    ILI9341_GAMMASET, 1, 0x01,
    ILI9341_GMCTRP1, 15, 0x0f, 0x31, 0x2b, 0x0c, 0x0e, 0x08, 0x4e,
    0xf1, 0x37, 0x07, 0x10, 0x03, 0x0e, 0x09, 0x00,
    ILI9341_GMCTRN1, 15, 0x00, 0x0e, 0x14, 0x03, 0x11, 0x07, 0x31,
    0xc1, 0x48, 0x08, 0x0f, 0x0c, 0x31, 0x36, 0x0f,
    ILI9341_SLPOUT, 0,
    0
};

void ili9341_init(void)
{
    const uint8_t *init_p;
    uint8_t i;

    /* Turn on the clocks. */
    rcc->apb2enr |= RCC_APB2ENR_SPI1EN;

    /* Configure general-purpose I/Os. */
    gpio_configure_pin(gpioa, PIN_DCRS, GPO_pushpull(SPI_PIN_SPEED, HIGH));
    gpio_configure_pin(gpioa, PIN_RESET, GPO_pushpull(_2MHz, HIGH));
    gpio_configure_pin(gpioa, PIN_CS, GPO_pushpull(SPI_PIN_SPEED, HIGH));

    /* Configure SPI I/Os. */
    gpio_configure_pin(gpioa, 5, AFO_pushpull(SPI_PIN_SPEED)); /* CK */
    gpio_configure_pin(gpioa, 6, GPI_pull_up); /* MISO */
    gpio_configure_pin(gpioa, 7, AFO_pushpull(SPI_PIN_SPEED)); /* MOSI */

    /* Configure SPI: 8-bit mode, MSB first, CPOL Low, CPHA Leading Edge. */
    spi->cr2 = 0;
    spi->cr1 = (SPI_CR1_MSTR | /* master */
                SPI_CR1_SSM | SPI_CR1_SSI | /* software NSS */
                SPI_CR1_SPE |
                SPI_BR_DIV);

    /* Drain SPI I/O. */
    while (!(spi->sr & SPI_SR_TXE))
        cpu_relax();
    (void)spi->dr;

    /* Reset. */
    delay_ms(5);
    set_pin(PIN_RESET, 0);
    delay_ms(20);
    set_pin(PIN_RESET, 1);
    delay_ms(150);

    /* Initialise. */
    for (init_p = init_seq; *init_p; ) {
        writecommand(*init_p++);
        for (i = *init_p++; i != 0; i--)
            writedata(*init_p++);
    }

    /* Wait a short while after Sleep Out command. */
    delay_ms(5);

    /* Clear the display, then switch it on. */
    fill_rect(0, 0, 320, 240, BG_COL);
    writecommand(ILI9341_DISPON);

    /* Example garbage. */
    draw_string(0, 100, "New Zealand Story.ADF\x09\x89");
    fill_rect(20, 20, 20, 20, 0xf800);
}

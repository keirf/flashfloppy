/*
 * sd_spi.c
 * 
 * Drive SD memory card in SPI mode via STM32 built-in SPI interface.
 * 
 * Written & released by Keir Fraser <keir.xen@gmail.com>
 * 
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

#include "fatfs/diskio.h"

#if 1
/* We can now switch to Default Speed (25MHz). Closest we can get is 36Mhz/2 = 
 * 18MHz. */
#define DEFAULT_SPEED_DIV SPI_CR1_BR_DIV2 /* 18MHz */
#define SPI_PIN_SPEED _50MHz
#else
/* Best speed I can reliably achieve right now is 9Mbit/s. */
#define DEFAULT_SPEED_DIV SPI_CR1_BR_DIV4 /* 9MHz */
#define SPI_PIN_SPEED _10MHz
#endif

#define CMD(n)  (0x40 | (n))
#define ACMD(n) (0xc0 | (n))

#define R1_MBZ         (1u<<7)
#define R1_ParamErr    (1u<<6)
#define R1_AddressErr  (1u<<5)
#define R1_EraseSeqErr (1u<<4)
#define R1_CRCErr      (1u<<3)
#define R1_IllegalCmd  (1u<<2)
#define R1_EraseReset  (1u<<1)
#define R1_IdleState   (1u<<0)

static DSTATUS status = STA_NOINIT;

#define CT_MMC  0x01
#define CT_SD1  0x02  /* SDC v1.xx */
#define CT_SD2  0x03  /* SDC v2.xx */
#define CT_BLOCK 0x04 /* Fixed-block interface */
#define CT_SDHC (CT_BLOCK | CT_SD2) /* SDHC is v2.xx and fixed-block-size */
static uint8_t cardtype;

#define spi spi2
#define PIN_CS 4

static void spi_acquire(void)
{
    gpio_write_pin(gpioa, PIN_CS, 0);
}

static void spi_release(void)
{
    spi_quiesce(spi);
    gpio_write_pin(gpioa, PIN_CS, 1);
    /* Need a dummy transfer as SD deselect is sync'ed to the clock. */
    (void)spi_recv8(spi);
    spi_quiesce(spi);
}

static uint8_t wait_ready(void)
{
    stk_time_t start = stk_now();
    uint8_t res;

    /* Wait 500ms for card to be ready. */
    do {
        res = spi_recv8(spi);
    } while ((res != 0xff) && (stk_timesince(start) < stk_ms(500)));

    return res;
}

/* CRC7 polynomial 0x09, as used to protect SD Commands. */
static const uint8_t crc7_table[256] = {
    0x00, 0x09, 0x12, 0x1b, 0x24, 0x2d, 0x36, 0x3f,
    0x48, 0x41, 0x5a, 0x53, 0x6c, 0x65, 0x7e, 0x77,
    0x19, 0x10, 0x0b, 0x02, 0x3d, 0x34, 0x2f, 0x26,
    0x51, 0x58, 0x43, 0x4a, 0x75, 0x7c, 0x67, 0x6e,
    0x32, 0x3b, 0x20, 0x29, 0x16, 0x1f, 0x04, 0x0d,
    0x7a, 0x73, 0x68, 0x61, 0x5e, 0x57, 0x4c, 0x45,
    0x2b, 0x22, 0x39, 0x30, 0x0f, 0x06, 0x1d, 0x14,
    0x63, 0x6a, 0x71, 0x78, 0x47, 0x4e, 0x55, 0x5c,
    0x64, 0x6d, 0x76, 0x7f, 0x40, 0x49, 0x52, 0x5b,
    0x2c, 0x25, 0x3e, 0x37, 0x08, 0x01, 0x1a, 0x13,
    0x7d, 0x74, 0x6f, 0x66, 0x59, 0x50, 0x4b, 0x42,
    0x35, 0x3c, 0x27, 0x2e, 0x11, 0x18, 0x03, 0x0a,
    0x56, 0x5f, 0x44, 0x4d, 0x72, 0x7b, 0x60, 0x69,
    0x1e, 0x17, 0x0c, 0x05, 0x3a, 0x33, 0x28, 0x21,
    0x4f, 0x46, 0x5d, 0x54, 0x6b, 0x62, 0x79, 0x70,
    0x07, 0x0e, 0x15, 0x1c, 0x23, 0x2a, 0x31, 0x38,
    0x41, 0x48, 0x53, 0x5a, 0x65, 0x6c, 0x77, 0x7e,
    0x09, 0x00, 0x1b, 0x12, 0x2d, 0x24, 0x3f, 0x36,
    0x58, 0x51, 0x4a, 0x43, 0x7c, 0x75, 0x6e, 0x67,
    0x10, 0x19, 0x02, 0x0b, 0x34, 0x3d, 0x26, 0x2f,
    0x73, 0x7a, 0x61, 0x68, 0x57, 0x5e, 0x45, 0x4c,
    0x3b, 0x32, 0x29, 0x20, 0x1f, 0x16, 0x0d, 0x04,
    0x6a, 0x63, 0x78, 0x71, 0x4e, 0x47, 0x5c, 0x55,
    0x22, 0x2b, 0x30, 0x39, 0x06, 0x0f, 0x14, 0x1d,
    0x25, 0x2c, 0x37, 0x3e, 0x01, 0x08, 0x13, 0x1a,
    0x6d, 0x64, 0x7f, 0x76, 0x49, 0x40, 0x5b, 0x52,
    0x3c, 0x35, 0x2e, 0x27, 0x18, 0x11, 0x0a, 0x03,
    0x74, 0x7d, 0x66, 0x6f, 0x50, 0x59, 0x42, 0x4b,
    0x17, 0x1e, 0x05, 0x0c, 0x33, 0x3a, 0x21, 0x28,
    0x5f, 0x56, 0x4d, 0x44, 0x7b, 0x72, 0x69, 0x60,
    0x0e, 0x07, 0x1c, 0x15, 0x2a, 0x23, 0x38, 0x31,
    0x46, 0x4f, 0x54, 0x5d, 0x62, 0x6b, 0x70, 0x79
};

static uint8_t crc7(uint8_t *buf, uint8_t count)
{
    uint8_t crc = 0;
    while (count--)
        crc = crc7_table[(crc << 1) ^ *buf++];
    return crc;
}

static uint8_t send_cmd(uint8_t cmd, uint32_t arg)
{
    uint8_t i, res, retry = 0;
    uint8_t buf[6];

    for (;;) {

        /* ACMDx == CMD55 + CMDx */
        if ((cmd & 0x80) && ((res = send_cmd(CMD(55), 0)) & ~R1_IdleState))
            return res;

        spi_acquire();

        if ((res = wait_ready()) != 0xff)
            return 0xff;

        buf[0] = cmd & 0x7f;
        buf[1] = arg >> 24;
        buf[2] = arg >> 16;
        buf[3] = arg >>  8;
        buf[4] = arg >>  0;
        buf[5] = (crc7(buf, 5) << 1) | 1;

        for (i = 0; i < 6; i++)
            spi_xmit8(spi, buf[i]);

        /* Resync with receive stream. (We ignored rx bytes, above). */
        spi_quiesce(spi);

        /* Wait up to 80 clocks for a valid response (MSB clear). */
        for (i = 0; i < 10; i++)
            if (!((res = spi_recv8(spi)) & R1_MBZ))
                break;

        /* Retry if no response or CRC error. */
        if (!(res & (R1_MBZ|R1_CRCErr)) || (++retry >= 3))
            break;

        /* Resync the SPI interface before retrying. */
        spi_release();
    }

    return res;
}

static bool_t datablock_recv(BYTE *buff, uint16_t bytes)
{
    bool_t ok;
    uint8_t token;
    uint32_t start = stk_now();

    /* Wait 100ms for data to be ready. */
    do {
        token = spi_recv8(spi);
    } while ((token == 0xff) && (stk_timesince(start) < stk_ms(100)));
    if (token != 0xfe) /* valid data token? */
        return 0;

    spi_16bit_frame(spi);

    spi->crcpr = 0x1021; /* CRC-CCITT */
    spi->cr1 |= SPI_CR1_CRCEN;

    /* Grab the data. */
    while (bytes) {
        uint16_t w = spi_recv16(spi);
        *buff++ = w >> 8;
        *buff++ = w;
        bytes -= 2;
    }

    /* Retrieve and check the CRC. */
    (void)spi_recv16(spi);
    spi_quiesce(spi);
    ok = !spi->rxcrcr;
    spi->cr1 &= ~SPI_CR1_CRCEN;

    spi_8bit_frame(spi);

    return ok;
}

DSTATUS disk_initialize(BYTE pdrv)
{
    uint32_t start, cr1;
    uint16_t rcv;
    uint8_t i;

    if (pdrv)
        return RES_PARERR;

    status |= STA_NOINIT;

    /* Turn on the clocks. */
    rcc->apb1enr |= RCC_APB1ENR_SPI2EN;

    /* Enable external I/O pins. */
    gpio_configure_pin(gpioa, PIN_CS, GPO_pushpull(SPI_PIN_SPEED, HIGH));
    gpio_configure_pin(gpiob, 13, AFO_pushpull(SPI_PIN_SPEED)); /* CK */
    gpio_configure_pin(gpiob, 14, GPI_pull_up); /* MISO */
    gpio_configure_pin(gpiob, 15, AFO_pushpull(SPI_PIN_SPEED)); /* MOSI */

    /* Configure SPI: 8-bit mode, MSB first, CPOL Low, CPHA Leading Edge. */
    spi->cr2 = 0;
    cr1 = (SPI_CR1_MSTR | /* master */
           SPI_CR1_SSM | SPI_CR1_SSI | /* software NSS */
           SPI_CR1_SPE);
    spi->cr1 = cr1 | SPI_CR1_BR_DIV128; /* ~281kHz (<400kHz) */

    /* Drain SPI I/O. */
    spi_quiesce(spi);

    /* Wait 80 cycles for card to ready itself. */
    for (i = 0; i < 10; i++)
        (void)spi_recv8(spi);

    /* Reset, enter idle state (SPI mode). */
    if (send_cmd(CMD(0), 0) != R1_IdleState)
        goto out;

    /* Enable CRC checking. Not all cards support this. */
    if ((send_cmd(CMD(59), 1) & ~R1_IllegalCmd) != R1_IdleState)
        goto out;

    /* Send interface condition (2.7-3.6v, check bits). 
     * This also validates that the card responds to v2.00-only commands. */
    if (send_cmd(CMD(8), 0x1aa) == R1_IdleState) {

        /* Command was understood. We have a v2.00-compliant card.
         * Get the 4-byte response and validate.  */
        for (i = rcv = 0; i < 4; i++)
            rcv = (rcv << 8) | spi_recv8(spi);
        if ((rcv & 0x1ff) != 0x1aa)
            goto out;

        /* Request SDHC/SDXC and start card initialisation. */
        start = stk_now();
        while (send_cmd(ACMD(41), 1u<<30)) {
            if (stk_timesince(start) >= stk_ms(1000))
                goto out; /* initialisation timeout */
        }

        /* Read OCR register, check for SDSD/SDHC/SDXC configuration. */
        if (send_cmd(CMD(58), 0) != 0)
            goto out;
        rcv = spi_recv8(spi); /* Only care about first byte (bits 31:24) */
        for (i = 0; i < 3; i++)
            (void)spi_recv8(spi);
        if (!(rcv & 0x80)) /* Bit 31: fail if card is still busy */
            goto out;
        cardtype = (rcv & 0x40) ? CT_SDHC : CT_SD2; /* Bit 30: SDHC? */

    } else {

        /* No valid response to CMD. Must be a v1.xx SDC or MMC.
         * Try initialisation with ACMD41. This will work if it's an SDC. */
        uint8_t cmd = ACMD(41);
        cardtype = CT_SD1;
        if (send_cmd(cmd, 0) & ~R1_IdleState) {
            /* Must be MMC: Fall back to CMD1. */
            cmd = CMD(1);
            cardtype = CT_MMC;
        }

        /* Wait for card initialisation. */
        start = stk_now();
        while (send_cmd(cmd, 0)) {
            if (stk_timesince(start) >= stk_ms(1000))
                goto out; /* initialisation timeout */
        }

    }

    /* Specify 512-byte block size. Unnecessary but harmless for SDHC. */
    if (send_cmd(CMD(16), 512) != 0)
        goto out;

    /* We're done: All good. */
    status &= ~STA_NOINIT;

out:
    spi_release();

    if (!(status & STA_NOINIT)) {
        delay_us(10); /* XXX small delay here stops SPI getting stuck?? */
        spi->cr1 = cr1 | DEFAULT_SPEED_DIV;
    } else {
        /* Disable SPI. */
        spi->cr1 = 0;
        rcc->apb1enr &= ~RCC_APB1ENR_SPI2EN;
        /* Configure external I/O pins as pulled-up inputs. */
        gpio_configure_pin(gpioa, PIN_CS, GPI_pull_up);
        gpio_configure_pin(gpiob, 13, GPI_pull_up); /* CK */
        gpio_configure_pin(gpiob, 14, GPI_pull_up); /* MISO */
        gpio_configure_pin(gpiob, 15, GPI_pull_up); /* MOSI */
    }

    printk("SD Card configured\n");
    return status;
}

DSTATUS disk_status (BYTE pdrv)
{
    return pdrv ? RES_PARERR : status;
}

DRESULT disk_read(BYTE pdrv, BYTE *buff, DWORD sector, UINT count)
{
    uint8_t retry = 0;
    UINT todo;
    BYTE *p;

    if (pdrv || !count)
        return RES_PARERR;
    if (status & STA_NOINIT)
        return RES_NOTRDY;

    if (!(cardtype & CT_BLOCK))
        sector <<= 9;

    do {
        todo = count;
        p = buff;

        /* READ_{MULTIPLE,SINGLE}_BLOCK */
        if (send_cmd(CMD((count > 1) ? 18 : 17), sector) != 0)
            continue;

        while (datablock_recv(p, 512) && --todo)
            p += 512;

        /* STOP_TRANSMISSION */
        if (count > 1)
            send_cmd(CMD(12), 0);

        spi_release();

    } while (todo && (++retry < 3));

    return todo ? RES_ERROR : RES_OK;
}

DRESULT disk_write(BYTE pdrv, const BYTE *buff, DWORD sector, UINT count)
{
    if (pdrv || !count)
        return RES_PARERR;
    if (status & STA_NOINIT)
        return RES_NOTRDY;

    if (!(cardtype & CT_BLOCK))
        sector <<= 9;

    return RES_PARERR;
}

DRESULT disk_ioctl(BYTE pdrv, BYTE ctrl, void *buff)
{
    DRESULT res;

    printk("ioctl %d %d\n", pdrv, ctrl);

    if (pdrv)
        return RES_PARERR;
    if (status & STA_NOINIT)
        return RES_NOTRDY;

    switch (ctrl) {
    case CTRL_SYNC:
        spi_acquire();
        if (wait_ready() == 0xff)
            res = RES_OK;
        spi_release();
        break;
    default:
        res = RES_PARERR;
        break;
    }

    return res;
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

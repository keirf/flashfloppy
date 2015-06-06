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

#if 0
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

static DSTATUS status = STA_NOINIT;

#define CT_MMC  0x01
#define CT_SD1  0x02  /* SDC v1.xx */
#define CT_SD2  0x03  /* SDC v2.xx */
#define CT_BLOCK 0x04 /* Fixed-block interface */
#define CT_SDHC (CT_BLOCK | CT_SD2) /* SDHC is v2.xx and fixed-block-size */
static uint8_t cardtype;

#define spi spi2
#define PIN_CS 4

static uint8_t spi_xchg_byte(uint8_t out)
{
    spi->dr = out;
    while (!(spi->sr & SPI_SR_RXNE))
        continue;
    return spi->dr;
}

#define spi_recv() spi_xchg_byte(0xff)
#define spi_xmit(x) spi_xchg_byte(x)

static void spi_acquire(void)
{
    gpio_write_pin(gpioa, PIN_CS, 0);
}

static void spi_release(void)
{
    gpio_write_pin(gpioa, PIN_CS, 1);
    /* Need a dummy transfer as SD deselect is sync'ed to the clock. */
    spi_recv();
}

static uint8_t wait_ready(void)
{
    uint8_t res;
    uint32_t start = stk->val, duration;

    /* Wait 500ms for card to be ready. */
    do {
        res = spi_recv();
        duration = (start - stk->val) & STK_MASK;
    } while ((res != 0xff) && (duration < stk_ms(500)));

    return res;
}

static uint8_t send_cmd(uint8_t cmd, uint32_t arg)
{
    uint8_t i, res;

    /* ACMDx == CMD55 + CMDx */
    if (cmd & 0x80) {
        if ((res = send_cmd(CMD(55), 0)) > 1)
            return res;
        cmd &= 0x7f;
    }

    spi_acquire();

    if ((res = wait_ready()) != 0xff)
        return 0xff;

    spi_xmit(cmd);
    spi_xmit(arg>>24);
    spi_xmit(arg>>16);
    spi_xmit(arg>> 8);
    spi_xmit(arg>> 0);
    /* Send a dummy CRC unless command requires it to be valid. 
     * Bit 0 is Stop bit (always set). */
    spi_xmit((cmd == CMD(0)) ? 0x95 : (cmd == CMD(8)) ? 0x87 : 0x01);

    /* Wait up to 80 clocks for a valid response (MSB clear). */
    for (i = 0; i < 10; i++)
        if (!((res = spi_recv()) & 0x80))
            break;

    return res;
}

static bool_t datablock_recv(BYTE *buff, uint16_t bytes)
{
    uint8_t token;
    uint32_t start = stk->val, duration;

    /* Wait 100ms for data to be ready. */
    do {
        token = spi_recv();
        duration = (start - stk->val) & STK_MASK;
    } while ((token == 0xff) && (duration < stk_ms(100)));
    if (token != 0xfe) /* valid data token? */
        return 0;

    /* Grab the data. */
    while (bytes) {
        *buff++ = spi_recv();
        *buff++ = spi_recv();
        *buff++ = spi_recv();
        *buff++ = spi_recv();
        bytes -= 4;
    }

    /* Discard the CRC. */
    spi_recv();
    spi_recv();

    return 1;
}

DSTATUS disk_initialize(BYTE pdrv)
{
    uint32_t start, duration, cr1;
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
    while (!(spi->sr & SPI_SR_TXE))
        cpu_relax();
    (void)spi->dr;

    /* Wait 80 cycles for card to ready itself. */
    for (i = 0; i < 10; i++)
        spi_recv();

    /* Reset, enter idle state (SPI mode). */
    if (send_cmd(CMD(0), 0) != 1)
        goto out;

    /* Send interface condition (2.7-3.6v, check bits). 
     * This also validates that the card responds to v2.00-only commands. */
    if (send_cmd(CMD(8), 0x1aa) == 1) {

        /* Command was understood. We have a v2.00-compliant card.
         * Get the 4-byte response and validate.  */
        for (i = rcv = 0; i < 4; i++)
            rcv = (rcv << 8) | spi_recv();
        if ((rcv & 0x1ff) != 0x1aa)
            goto out;

        /* Request SDHC/SDXC and start card initialisation. */
        start = stk->val;
        while (send_cmd(ACMD(41), 1u<<30)) {
            duration = (start - stk->val) & STK_MASK;
            if (duration >= stk_ms(1000))
                goto out; /* initialisation timeout */
        }

        /* Read OCR register, check for SDSD/SDHC/SDXC configuration. */
        if (send_cmd(CMD(58), 0) != 0)
            goto out;
        rcv = spi_recv(); /* Only care about first byte (bits 31:24) */
        for (i = 0; i < 3; i++)
            spi_recv();
        if (!(rcv & 0x80)) /* Bit 31: fail if card is still busy */
            goto out;
        cardtype = (rcv & 0x40) ? CT_SDHC : CT_SD2; /* Bit 30: SDHC? */

    } else {

        /* No valid response to CMD. Must be a v1.xx SDC or MMC.
         * Try initialisation with ACMD41. This will work if it's an SDC. */
        uint8_t cmd = ACMD(41);
        cardtype = CT_SD1;
        if (send_cmd(cmd, 0) <= 1) {
            /* Must be MMC: Fall back to CMD1. */
            cmd = CMD(1);
            cardtype = CT_MMC;
        }

        /* Wait for card initialisation. */
        start = stk->val;
        while (send_cmd(cmd, 0)) {
            duration = (start - stk->val) & STK_MASK;
            if (duration >= stk_ms(1000))
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
    bool_t multi_sector = (count > 1);

    if (pdrv || !count)
        return RES_PARERR;
    if (status & STA_NOINIT)
        return RES_NOTRDY;

    if (!(cardtype & CT_BLOCK))
        sector <<= 9;

    /* READ_{MULTIPLE,SINGLE}_BLOCK */
    if (send_cmd(CMD(multi_sector ? 18 : 17), sector) != 0)
        return RES_ERROR;
    while (count && datablock_recv(buff, 512)) {
        count--;
        buff += 512;
    }
    /* STOP_TRANSMISSION */
    if (multi_sector)
        send_cmd(CMD(12), 0);

    spi_release();

    return count ? RES_ERROR : RES_OK;
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

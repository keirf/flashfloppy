/*
 * lcd.c
 * 
 * 1. HD44780 LCD controller via a PCF8574 I2C backpack.
 * 2. SSD1306 OLED controller driving 128x32 bitmap display.
 * 
 * Written & released by Keir Fraser <keir.xen@gmail.com>
 * 
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

/* STM32 I2C peripheral. */
static volatile struct i2c *i2c;

const static struct i2c_cfg {
    uint8_t en;
    uint8_t scl;
    uint8_t sda;
    uint8_t error_irq;
    uint8_t event_irq;
    uint8_t dma_tx;
    uint8_t dma_rx;
} i2c1_cfg = {
    .en = 21, /* RCC_APB1ENR_I2C1EN */
    .scl = 6,
    .sda = 7,
    .error_irq = 32,
    .event_irq = 31,
    .dma_tx = 6,
    .dma_rx = 7,
}, i2c2_cfg = {
    .en = 22, /* RCC_APB1ENR_I2C2EN */
    .scl = 10,
    .sda = 11,
    .error_irq = 34,
    .event_irq = 33,
    .dma_tx = 4,
    .dma_rx = 5
}, *i2c_cfg;

#define SCL i2c_cfg->scl
#define SDA i2c_cfg->sda

#define PCLK_MHZ APB1_MHZ

/* I2C error ISR. */
#define I2C_ERROR_IRQ i2c_cfg->error_irq
void IRQ_34(void) __attribute__((alias("IRQ_i2c_error")));
void IRQ_32(void) __attribute__((alias("IRQ_i2c_error")));

/* I2C event ISR. */
#define I2C_EVENT_IRQ i2c_cfg->event_irq
void IRQ_33(void) __attribute__((alias("IRQ_i2c_event")));
void IRQ_31(void) __attribute__((alias("IRQ_i2c_event")));

/* DMA Tx. */
#define i2c_tx_dma dma1->ch[i2c_cfg->dma_tx-1]
#define DMA_TX_CGIF DMA_IFCR_CGIF(i2c_cfg->dma_tx)
#define DMA_TX_IRQ (i2c_cfg->dma_tx + 10)
void IRQ_14(void) __attribute__((alias("IRQ_dma_tx_tc")));
void IRQ_16(void) __attribute__((alias("IRQ_dma_tx_tc")));

/* DMA Rx. */
#define i2c_rx_dma dma1->ch[i2c_cfg->dma_rx-1]
#define DMA_RX_CGIF DMA_IFCR_CGIF(i2c_cfg->dma_rx)
#define DMA_RX_IRQ (i2c_cfg->dma_rx + 10)
void IRQ_15(void) __attribute__((alias("IRQ_dma_rx_tc")));
void IRQ_17(void) __attribute__((alias("IRQ_dma_rx_tc")));

#include "lcd_common.c"

static void i2c_stop(void);

static void dma_tx_tc_btf(void);

/* I2C Error ISR: Reset the peripheral and reinit everything. */
static void IRQ_i2c_error(void)
{
    /* Dump and clear I2C errors. */
    printk("I2C: Error (%04x)\n", (uint16_t)(i2c->sr1 & I2C_SR1_ERRORS));
    i2c->sr1 &= ~I2C_SR1_ERRORS;

    /* Clear the I2C peripheral. */
    i2c->cr1 = 0;
    i2c->cr1 = I2C_CR1_SWRST;

    /* Clear the DMA controller. */
    i2c_tx_dma.ccr = i2c_rx_dma.ccr = 0;
    dma1->ifcr = DMA_TX_CGIF | DMA_RX_CGIF;

    timer_cancel(&timeout_timer);

    lcd_init();
}

static void IRQ_i2c_event(void)
{
    uint16_t sr1 = i2c->sr1;

    if (sr1 & I2C_SR1_SB) {
        /* Send address. Clears SR1_SB. */
        uint8_t a = in_osd ? OSD_I2C_ADDR : i2c_addr;
        a <<= 1;
        if (in_osd == OSD_read)
            a |= 1;
        i2c->dr = a;
    }

    if (sr1 & I2C_SR1_ADDR) {
        /* Read SR2 clears SR1_ADDR. */
        (void)i2c->sr2;
        /* No more events: data phase is driven by DMA. */
        i2c->cr2 &= ~I2C_CR2_ITEVTEN;
    }

    if (sr1 & I2C_SR1_BTF) {
        /* DMA transfer fully complete: I2C is now idle. Disable further events
         * and return control to the DMA pipeline. */
        i2c->cr2 &= ~I2C_CR2_ITEVTEN;
        dma_tx_tc_btf();
    }
}

/* Start an I2C DMA sequence. */
static void dma_start(unsigned int sz)
{
    ASSERT(sz <= sizeof(buffer));

    if (in_osd == OSD_read) {
        i2c_rx_dma.cndtr = sz;
        i2c_rx_dma.ccr = (DMA_CCR_MSIZE_8BIT |
                          DMA_CCR_PSIZE_16BIT |
                          DMA_CCR_MINC |
                          DMA_CCR_DIR_P2M |
                          DMA_CCR_TCIE |
                          DMA_CCR_EN);
    } else {
        i2c_tx_dma.cndtr = sz;
        i2c_tx_dma.ccr = (DMA_CCR_MSIZE_8BIT |
                          DMA_CCR_PSIZE_16BIT |
                          DMA_CCR_MINC |
                          DMA_CCR_DIR_M2P |
                          DMA_CCR_TCIE |
                          DMA_CCR_EN);
    }

    /* Set the timeout timer in case the DMA hangs for any reason. */
    timer_set(&timeout_timer, time_now() + DMA_TIMEOUT);
}

/* Snapshot text buffer into the command buffer. */
static unsigned int osd_prep_buffer(void)
{
    uint16_t order = menu_mode ? 0x7903 : 0x7183;
    uint8_t *q = buffer;
    unsigned int row, rows, heights;
    int i;

    if (++in_osd == OSD_read) {
        i2c->cr2 |= I2C_CR2_LAST | I2C_CR2_ITEVTEN;
        i2c->cr1 |= I2C_CR1_ACK | I2C_CR1_START;
        return sizeof(struct i2c_osd_info);
    }

    if ((ff_cfg.osd_display_order != DORD_default)
        && (display_mode == DM_normal))
        order = ff_cfg.osd_display_order;

    heights = rows = 0;
    /* FF OSD takes three rows at most: its row count is two bits. */
    for (i = 2; i >= 0; i--) {
        /* Iterate over rows, bottom to top. */
        row = order >> (i<<2);
        /* Skip all trailing empty rows. */
        if ((rows == 0) && ((row&7) == 7))
            continue;
        /* Count this row and check if it is double height. */
        rows++;
        heights <<= 1;
        if (row & 8)
            heights |= 1;
    }

    *q++ = OSD_BACKLIGHT | !!_bl;
    *q++ = OSD_COLUMNS | lcd_columns;
    *q++ = OSD_ROWS | rows;
    *q++ = OSD_HEIGHTS | heights;
    *q++ = OSD_BUTTONS | osd_buttons_tx;
    *q++ = OSD_DATA;
    for (row = 0; row < rows; row++) {
        i = (order >> (row * DORD_shift)) & DORD_row;
        /* A blank row, 7, has no text[] of its own. */
        if (i < ARRAY_SIZE(text))
            memcpy(q, text[i], lcd_columns);
        else
            memset(q, ' ', lcd_columns);
        q += lcd_columns;
    }

    if (i2c_addr == 0)
        refresh_count++;

    in_osd = OSD_write;
    i2c->cr2 |= I2C_CR2_ITEVTEN;
    i2c->cr1 |= I2C_CR1_START;

    return q - buffer;
}

/* Snapshot text buffer into the command buffer. */
static unsigned int lcd_prep_buffer(void)
{
    const static uint8_t row_offs[] = { 0x00, 0x40, 0x14, 0x54 };
    uint16_t order;
    char *p;
    uint8_t *q = buffer;
    unsigned int i, row;

    if (i2c_row == lcd_rows) {
        i2c_row++;
        if (has_osd) {
            i2c_stop();
            return osd_prep_buffer();
        }
    }

    if (i2c_row > lcd_rows) {
        i2c_row = 0;
        refresh_count++;
        i2c_stop();
        i2c->cr2 |= I2C_CR2_ITEVTEN;
        i2c->cr1 |= I2C_CR1_START;
    }

    order = (lcd_rows == 2) ? 0x7710 : 0x2103;
    if ((ff_cfg.display_order != DORD_default) && (display_mode == DM_normal))
        order = ff_cfg.display_order;

    row = (order >> (i2c_row * DORD_shift)) & DORD_row;
    p = (_bl && row < ARRAY_SIZE(text)) ? text[row] : NULL;

    emit8(&q, CMD_SETDDRADDR | row_offs[i2c_row], 0);
    for (i = 0; i < lcd_columns; i++)
        emit8(&q, p ? *p++ : ' ', _RS);

    i2c_row++;

    return q - buffer;
}

static void IRQ_dma_tx_tc(void)
{
    /* Clear the DMA controller. */
    i2c_tx_dma.ccr = 0;
    dma1->ifcr = DMA_TX_CGIF;

    /* Wait for BTF. We then get called back on dma_tx_tc_btf().
     *
     * Q: Why do we always wait for BTF, even if we're issuing another DMA?
     * A: Otherwise the start of next DMA races BTF. If we lose that race, BTF
     * is set for the rest of the transfer (since we don't read SR1) and a
     * wait for BTF at the end of the final DMA transfer would erroneously
     * succeed immediately. We would then STOP too early and lose the last
     * byte of the transfer. */
    i2c->cr2 |= I2C_CR2_ITEVTEN;
}

static void dma_tx_tc_btf(void)
{
    unsigned int dma_sz;

    /* Prepare the DMA buffer and start the next DMA sequence. */
    in_osd = OSD_no;
    if (i2c_addr == 0) {
        i2c_stop();
        dma_sz = osd_prep_buffer();
    } else {
        dma_sz = is_oled_display ? oled_prep_buffer() : lcd_prep_buffer();
    }
    dma_start(dma_sz);
}

static void IRQ_dma_rx_tc(void)
{
    struct i2c_osd_info *info = (struct i2c_osd_info *)buffer;

    /* Clear the DMA controller. */
    i2c_rx_dma.ccr = 0;
    dma1->ifcr = DMA_RX_CGIF;

    /* Clean up I2C. */
    i2c->cr2 &= ~I2C_CR2_LAST;
    i2c->cr1 &= ~I2C_CR1_ACK;

    osd_buttons_rx = info->buttons;

    /* Now do the OSD write. */
    dma_start(osd_prep_buffer());
}

/* Wait for given status condition @s while also checking for errors. */
static bool_t i2c_wait(uint8_t s)
{
    stk_time_t t = stk_now();
    while ((i2c->sr1 & s) != s) {
        if (i2c->sr1 & I2C_SR1_ERRORS) {
            i2c->sr1 &= ~I2C_SR1_ERRORS;
            return FALSE;
        }
        if (stk_diff(t, stk_now()) > stk_ms(10)) {
            /* I2C bus seems to be locked up. */
            i2c_dead = TRUE;
            return FALSE;
        }
    }
    return TRUE;
}

/* Synchronously transmit the I2C START sequence. 
 * Caller must already have asserted I2C_CR1_START. */
#define I2C_RD TRUE
#define I2C_WR FALSE
static bool_t i2c_start(uint8_t a, bool_t rd)
{
    if (!i2c_wait(I2C_SR1_SB))
        return FALSE;
    i2c->dr = (a << 1) | rd;
    if (!i2c_wait(I2C_SR1_ADDR))
        return FALSE;
    (void)i2c->sr2;
    return TRUE;
}

/* Synchronously transmit the I2C STOP sequence. */
static void i2c_stop(void)
{
    i2c->cr1 |= I2C_CR1_STOP;
    while (i2c->cr1 & I2C_CR1_STOP)
        continue;
    if (is_artery_mcu) {
        i2c->cr1 = 0;
        i2c->cr1 = I2C_CR1_PE;
    }
}

/* Synchronously transmit an I2C byte. */
static bool_t i2c_sync_write(uint8_t b)
{
    i2c->dr = b;
    return i2c_wait(I2C_SR1_BTF);
}

static bool_t i2c_sync_write_txn(uint8_t addr, uint8_t *cmds, unsigned int nr)
{
    unsigned int i;

    if (!i2c_start(addr, I2C_WR))
        return FALSE;

    for (i = 0; i < nr; i++)
        if (!i2c_sync_write(*cmds++))
            return FALSE;

    return TRUE;
}

/* Check whether an I2C device is responding at given address. */
static bool_t i2c_probe(uint8_t a)
{
    i2c->cr1 |= I2C_CR1_START;
    if (!i2c_start(a, I2C_WR) || !i2c_sync_write(0))
        return FALSE;
    i2c_stop();
    return TRUE;
}

/* Enables interrupt @irq at I2C priority, and clears any pending interrupt. */
static void i2c_irq_enable(unsigned int irq)
{
    IRQx_set_prio(irq, I2C_IRQ_PRI);
    IRQx_clear_pending(irq);
    IRQx_enable(irq);
}

bool_t lcd_init(void)
{
    uint8_t a, *p;
    bool_t reinit = (i2c_addr != 0) || has_osd;

    i2c_dead = FALSE;
    i2c_row = 0;
    in_osd = OSD_no;
    osd_buttons_rx = 0;

    if (mcu_package == MCU_QFN32) {
        i2c = i2c1;
        i2c_cfg = &i2c1_cfg;
    } else {
        i2c = i2c2;
        i2c_cfg = &i2c2_cfg;
    }

    rcc->apb1enr |= 1<<i2c_cfg->en;

    /* Check we have a clear I2C bus. Both clock and data must be high. If SDA 
     * is stuck low then slave may be stuck in an ACK cycle. We can try to 
     * unwedge the slave in that case and drive it into the STOP condition. */
    gpio_configure_pin(gpiob, SCL, GPO_opendrain(_2MHz, HIGH));
    gpio_configure_pin(gpiob, SDA, GPO_opendrain(_2MHz, HIGH));
    delay_us(10);
    if (gpio_read_pin(gpiob, SCL) && !gpio_read_pin(gpiob, SDA)) {
        printk("I2C: SDA held by slave? Fixing... ");
        /* We will hold SDA low (as slave is) and also drive SCL low to end 
         * the current ACK cycle. */
        gpio_write_pin(gpiob, SDA, FALSE);
        gpio_write_pin(gpiob, SCL, FALSE);
        delay_us(10);
        /* Slave should no longer be driving SDA low (but we still are).
         * Now prepare for the STOP condition by setting SCL high. */
        gpio_write_pin(gpiob, SCL, TRUE);
        delay_us(10);
        /* Enter the STOP condition by setting SDA high while SCL is high. */
        gpio_write_pin(gpiob, SDA, TRUE);
        delay_us(10);
        printk("%s\n",
               !gpio_read_pin(gpiob, SCL) || !gpio_read_pin(gpiob, SDA)
               ? "Still held" : "Done");
    }

    /* Check the bus is not floating (or still stuck!). We shouldn't be able to 
     * pull the lines low with our internal weak pull-downs (min. 30kohm). */
    if (!reinit) {
        bool_t scl, sda;
        gpio_configure_pin(gpiob, SCL, GPI_pull_down);
        gpio_configure_pin(gpiob, SDA, GPI_pull_down);
        delay_us(10);
        scl = gpio_read_pin(gpiob, SCL);
        sda = gpio_read_pin(gpiob, SDA);
        if (!scl || !sda) {
            printk("I2C: Invalid bus SCL=%u SDA=%u\n", scl, sda);
            goto fail;
        }
    }

    gpio_configure_pin(gpiob, SCL, AFO_opendrain(_2MHz));
    gpio_configure_pin(gpiob, SDA, AFO_opendrain(_2MHz));

    /* Standard Mode (100kHz) */
    i2c->cr1 = 0;
    i2c->cr2 = I2C_CR2_FREQ(PCLK_MHZ);
    i2c->ccr = I2C_CCR_CCR(PCLK_MHZ*5);
    i2c->trise = (PCLK_MHZ <= 62) ? PCLK_MHZ + 1 : 63;
    i2c->cr1 = I2C_CR1_PE;

    if (!reinit) {

        /* First probe after I2C re-initialisation seems to fail, and so we 
         * fail to detect FF OSD. So issue a dummy probe first. */
        (void)i2c_probe(0);

        /* Probe the bus for I2C devices: We support a single LCD/OLED plus 
         * an FF OSD device. */
        has_osd = i2c_probe(OSD_I2C_ADDR);
        a = i2c_probe_range(0x20, 0x27) ?: i2c_probe_range(0x38, 0x3f);
        if ((a == 0) && (i2c_dead || !has_osd
                         || ((ff_cfg.display_type & 3) != DISPLAY_auto))) {
            printk("I2C: %s\n",
                   i2c_dead ? "Bus locked up?" : "No device found");
            has_osd = FALSE;
            goto fail;
        }

        /* Probe the FF OSD device if we found one. */
        if (has_osd) {
            /* Read: Retrieve the version number. */
            i2c->cr1 |= I2C_CR1_START;
            if (i2c_start(OSD_I2C_ADDR, I2C_RD) && i2c_wait(I2C_SR1_RXNE))
                osd_ver = i2c->dr;
            printk("I2C: FF OSD found (ver %x)\n", osd_ver);
        }

        is_oled_display = (ff_cfg.display_type & DISPLAY_oled) ? TRUE
            : (ff_cfg.display_type & DISPLAY_lcd) ? FALSE
            : ((a&~1) == OLED_ADDR);

        if (is_oled_display) {
            oled_height = (ff_cfg.display_type & DISPLAY_oled_64) ? 64 : 32;
            oled_font = ff_cfg.oled_font;
            lcd_columns = (oled_font == FONT_8x16) ? 16
                : (ff_cfg.display_type & DISPLAY_narrower) ? 16
                : (ff_cfg.display_type & DISPLAY_narrow) ? 18 : 21;
            lcd_rows = 4;
        } else {
            lcd_columns = (ff_cfg.display_type >> _DISPLAY_lcd_columns) & 63;
            lcd_rows = (ff_cfg.display_type >> _DISPLAY_lcd_rows) & 7;
        }

        if (a != 0) {
            printk("I2C: %s found at 0x%02x\n",
                   is_oled_display ? "OLED" : "LCD", a);
            i2c_addr = a;
        } else {
            is_oled_display = FALSE;
            lcd_columns = ff_cfg.osd_columns;
        }

        lcd_columns = max_t(uint8_t, lcd_columns, 16);
        lcd_columns = min_t(uint8_t, lcd_columns, 40);
        lcd_rows = max_t(uint8_t, lcd_rows, 2);
        lcd_rows = min_t(uint8_t, lcd_rows, 4);

        lcd_clear();

    }

    /* Enable the Event IRQ. */
    i2c_irq_enable(I2C_EVENT_IRQ);

    /* Enable the Error IRQ. */
    i2c_irq_enable(I2C_ERROR_IRQ);
    i2c->cr2 |= I2C_CR2_ITERREN;

    /* Initialise DMA1 channel 4 and its completion interrupt. */
    i2c_tx_dma.cmar = (uint32_t)(unsigned long)buffer;
    i2c_tx_dma.cpar = (uint32_t)(unsigned long)&i2c->dr;
    dma1->ifcr = DMA_TX_CGIF;
    i2c_irq_enable(DMA_TX_IRQ);

    /* Initialise DMA1 channel 5 and its completion interrupt. */
    i2c_rx_dma.cmar = (uint32_t)(unsigned long)buffer;
    i2c_rx_dma.cpar = (uint32_t)(unsigned long)&i2c->dr;
    dma1->ifcr = DMA_RX_CGIF;
    i2c_irq_enable(DMA_RX_IRQ);

    /* Timeout handler for if I2C transmission borks. */
    timer_init(&timeout_timer, timeout_fn, NULL);
    timer_set(&timeout_timer, time_now() + DMA_TIMEOUT);

    if (is_oled_display) {
        oled_init();
        return TRUE;
    } else if (i2c_addr == 0) {
        i2c->cr2 |= I2C_CR2_DMAEN;
        dma_start(osd_prep_buffer());
        return TRUE;
    }

    i2c->cr1 |= I2C_CR1_START;
    if (!i2c_start(i2c_addr, I2C_WR))
        goto fail;

    /* Initialise 4-bit interface, as in the datasheet. Do this synchronously
     * and with the required delays. */
    write4(3 << 4);
    delay_us(4100);
    write4(3 << 4);
    delay_us(100);
    write4(3 << 4);
    write4(2 << 4);

    /* More initialisation from the datasheet. Send by DMA. */
    p = buffer;
    emit8(&p, CMD_FUNCTIONSET | FS_2LINE, 0);
    emit8(&p, CMD_DISPLAYCTL, 0);
    emit8(&p, CMD_ENTRYMODE | 2, 0);
    emit8(&p, CMD_DISPLAYCTL | 4, 0); /* display on */
    i2c->cr2 |= I2C_CR2_DMAEN;
    dma_start(p - buffer);
    
    if (!reinit)
        lcd_backlight(TRUE);

    return TRUE;

fail:
    if (reinit)
        return FALSE;
    IRQx_disable(I2C_EVENT_IRQ);
    IRQx_disable(I2C_ERROR_IRQ);
    IRQx_disable(DMA_TX_IRQ);
    IRQx_disable(DMA_RX_IRQ);
    i2c->cr1 &= ~I2C_CR1_PE;
    gpio_configure_pin(gpiob, SCL, GPI_pull_up);
    gpio_configure_pin(gpiob, SDA, GPI_pull_up);
    rcc->apb1enr &= ~(1<<i2c_cfg->en);
    return FALSE;
}

#ifdef font_extra
/* Instead of storing the 8x16 font in the flash memory, build its glyphs at
 * runtime as a bold (shifted-by-1) version of 6x13.
 */
#ifndef OLED_FONT_8x16_SYNTH
#define OLED_FONT_8x16_SYNTH 0
#endif
extern const uint8_t oled_font_8x16[];
static void oled_convert_text_row_8x16(char *pc)
{
    unsigned int i, j, c;
    const uint8_t *p;
    uint8_t *q = buffer;
    const unsigned int w = 8;

    for (i = 0; i < lcd_columns; i++) {
        if ((c = *pc++ - 0x20) > 0x5e)
            c = '.' - 0x20;
        /* Testing a macro at runtime allows the build system to supply a
         * runtime condition.
         */
        if (OLED_FONT_8x16_SYNTH) {
            /* The 6x13 glyph made bold by a second copy a column to the
             * right, 7 columns by 13 rows: centered in the cell and rounded
             * up and left, it is at the left, a row down, where the 6x13
             * font already has it in its 16 rows. */
            p = &oled_font_6x13[c * 6 * 2];
            for (j = 0; j < w; j++) {
                q[j] = ((j < 6) ? p[j] : 0) | ((j-1 < 6) ? p[j-1] : 0);
                q[128+j] = ((j < 6) ? p[6+j] : 0) | ((j-1 < 6) ? p[5+j] : 0);
            }
        } else {
            p = &oled_font_8x16[c * w * 2];
            memcpy(q, p, w);
            memcpy(q+128, p+w, w);
        }
        q += w;
    }
}
#endif

static unsigned int oled_start_i2c(uint8_t *buf)
{
    static const uint8_t ssd1306_addr_cmds[] = {
        0x20, 0,      /* horizontal addressing mode */
        0x21, 0, 127, /* column address range: 0-127 */
        0x22, 0, 7,   /* page address range: 0-7 */
    }, ztech_addr_cmds[] = {
        0xda, 0x12,   /* alternate com pins config */
        0x21, 4, 131, /* column address range: 4-131 */
    }, sh1106_addr_cmds[] = {
        0x10          /* column address high nibble is zero */
    };

    uint8_t dynamic_cmds[4], *dc = dynamic_cmds;
    uint8_t *p = buf;

    /* Set up the display address range. */
    if (oled_model == OLED_sh1106) {
        p += oled_queue_cmds(p, sh1106_addr_cmds, sizeof(sh1106_addr_cmds));
        /* Column address: 0 or 2 (seems 128x64 displays are shifted by 2). */
        *dc++ = (oled_height == 64) ? 0x02 : 0x00;
        /* Page address: according to i2c_row. */
        *dc++ = 0xb0 + i2c_row;
    } else {
        p += oled_queue_cmds(p, ssd1306_addr_cmds, sizeof(ssd1306_addr_cmds));
    }

    /* Display on/off according to backlight setting. */
    *dc++ = _bl ? 0xaf : 0xae;

    p += oled_queue_cmds(p, dynamic_cmds, dc - dynamic_cmds);

    /* ZHONGJY_TECH 2.23" 128x32 display based on SSD1305 controller. 
     * It has alternate COM pin mapping and is offset horizontally. */
    if (ff_cfg.display_type & DISPLAY_ztech)
        p += oled_queue_cmds(p, ztech_addr_cmds, sizeof(ztech_addr_cmds));

    /* All subsequent bytes are data bytes. */
    *p++ = 0x40;

    /* Start the I2C transaction. */
    i2c->cr2 |= I2C_CR2_ITEVTEN;
    i2c->cr1 |= I2C_CR1_START;

    return p - buf;
}

static unsigned int ssd1306_prep_buffer(void)
{
    /* If we have completed a complete fill of the OLED display, start a new 
     * I2C transaction. The OLED display seems to occasionally silently lose 
     * a byte and then we lose sync with the display address. */
    if (i2c_row == (oled_height / 16)) {
        i2c_row++;
        if (has_osd) {
            i2c_stop();
            return osd_prep_buffer();
        }
    }

    if (i2c_row > (oled_height / 16)) {
        i2c_row = 0;
        refresh_count++;
        i2c_stop();
        return oled_start_i2c(buffer);
    }

    /* Convert one row of text[] into buffer[] writes. */
    oled_to_lcd_row(i2c_row);

    i2c_row++;

    return 256;
}

static unsigned int sh1106_prep_buffer(void)
{
    uint8_t *p = buffer;

    if (i2c_row == (oled_height / 8)) {
        i2c_row++;
        if (has_osd) {
            i2c_stop();
            return osd_prep_buffer();
        }
    }

    if (i2c_row > (oled_height / 8)) {
        i2c_row = 0;
        refresh_count++;
    }

    /* Convert one row of text[] into buffer[] writes. */
    oled_to_lcd_row(i2c_row/2);
    if (!(i2c_row & 1))
        memcpy(&buffer[128], &buffer[0], 128);

    /* Every 8 rows needs a new page address and hence new I2C transaction. */
    i2c_stop();
    p += oled_start_i2c(p);

    /* Patch the data bytes onto the end of the address setup sequence. */
    memcpy(p, &buffer[128], 128);
    p += 128;

    i2c_row++;

    return p - buffer;
}

/* Snapshot text buffer into the bitmap buffer. */
static unsigned int oled_prep_buffer(void)
{
    return (oled_model == OLED_sh1106)
        ? sh1106_prep_buffer()
        : ssd1306_prep_buffer();
}

static bool_t oled_probe_model(void)
{
    uint8_t cmd1[] = { 0x80, 0x00, /* Column 0 */
                       0xc0 };     /* Read one data */
    uint8_t cmd2[] = { 0x80, 0x00, /* Column 0 */
                       0xc0, 0x00 }; /* Write one data */

    int i;
    uint8_t x, px = 0;
    uint8_t *rand = (uint8_t *)emit8;

    for (i = 0; i < 3; i++) {
        /* 1st Write stage. */
        i2c->cr1 |= I2C_CR1_START;
        if (!i2c_sync_write_txn(i2c_addr, cmd1, sizeof(cmd1)))
            goto fail;
        /* Read stage. */
        i2c->cr1 |= I2C_CR1_START | I2C_CR1_ACK;
        if (!i2c_start(i2c_addr, I2C_RD) || !i2c_wait(I2C_SR1_RXNE))
            goto fail;
        i2c->cr1 &= ~I2C_CR1_ACK;  /* NACK and Restart after next byte */
        i2c->cr1 |= I2C_CR1_START;
        (void)i2c->dr; /* 1st read: Dummy */
        if (!i2c_wait(I2C_SR1_RXNE))
            goto fail;
        x = i2c->dr; /* 2nd read: Data */
        /* 2nd Write stage. */
        cmd2[3] = x ^ rand[i]; /* XOR the write with "randomness" */
        if (!i2c_sync_write_txn(i2c_addr, cmd2, sizeof(cmd2)))
            goto fail;
        /* Check we read what we wrote on previous iteration. */
        if (i && (x != px))
            break;
        /* Remember what we wrote, for next iteration. */
        px = cmd2[3];
    }
    i2c_stop();

    oled_model = (i == 3) ? OLED_sh1106 : OLED_ssd1306;
    printk("OLED: %s\n", (oled_model == OLED_sh1106) ? "SH1106" : "SSD1306");
    return TRUE;

fail:
    return FALSE;
}

static void oled_init_fast_mode(void)
{
    /* Disable I2C (currently in Standard Mode). */
    i2c->cr1 = 0;

    /* Fast Mode (400kHz). */
    i2c->cr2 = I2C_CR2_FREQ(PCLK_MHZ);
    i2c->ccr = I2C_CCR_FS | I2C_CCR_CCR(PCLK_MHZ*5/6);
    i2c->trise = PCLK_MHZ*3/10 + 1;
    i2c->cr1 = I2C_CR1_PE;
    i2c->cr2 |= I2C_CR2_ITERREN;
}

static void oled_init(void)
{
    static const uint8_t init_cmds[] = {
        0xd5, 0x80, /* default clock */
        0xd3, 0x00, /* display offset = 0 */
        0x40,       /* display start line = 0 */
        0x8d, 0x14, /* enable charge pump */
        0xda, 0x02, /* com pins configuration */
        0xd9, 0xf1, /* pre-charge period */
        0xdb, 0x20, /* vcomh detect (default) */
        0xa4,       /* output follows ram contents */
        0x2e,       /* deactivate scroll */
    }, norot_cmds[] = {
        0xa1,       /* segment mapping (reverse) */
        0xc8,       /* com scan direction (decrement) */
    }, rot_cmds[] = {
        0xa0,       /* segment mapping (default) */
        0xc0,       /* com scan direction (default) */
    };
    uint8_t dynamic_cmds[7], *dc;
    uint8_t *p = buffer;

    if (!(ff_cfg.display_type & DISPLAY_slow))
        oled_init_fast_mode();

    if ((oled_model == OLED_unknown) && !oled_probe_model())
        goto fail;

    /* Initialisation sequence for SSD1306/SH1106. */
    p += oled_queue_cmds(p, init_cmds, sizeof(init_cmds));

    /* Dynamically-generated initialisation commands. */
    dc = dynamic_cmds;
    *dc++ = (ff_cfg.display_type & DISPLAY_inverse) ? 0xa7 : 0xa6; /* Video */
    *dc++ = 0x81; /* Display Contrast */
    *dc++ = ff_cfg.oled_contrast;
    *dc++ = 0xa8; /* Multiplex ratio (lcd height - 1) */
    *dc++ = oled_height - 1;
    *dc++ = 0xda; /* COM pins configuration */
    *dc++ = (oled_height == 64) ? 0x12 : 0x02;
    p += oled_queue_cmds(p, dynamic_cmds, dc - dynamic_cmds);

    /* Display orientation. */
    dc = dynamic_cmds;
    memcpy(dc, (ff_cfg.display_type & DISPLAY_rotate) ? rot_cmds : norot_cmds,
           2);
    if (ff_cfg.display_type & DISPLAY_hflip)
        dc[0] ^= 1;
    p += oled_queue_cmds(p, dc, 2);

    /* Start off the I2C transaction. */
    p += oled_start_i2c(p);

    /* Send the initialisation command sequence by DMA. */
    i2c->cr2 |= I2C_CR2_DMAEN;
    dma_start(p - buffer);
    return;

fail:
    IRQx_set_pending(I2C_ERROR_IRQ);
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

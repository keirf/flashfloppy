/*
 * lcd_rp2350.c
 *
 * 1. HD44780 LCD controller via a PCF8574 I2C backpack.
 * 2. SSD1306 OLED controller driving 128x32 bitmap display.
 *
 * RP2350 back end: DesignWare I2C0 on GPIO20 (SDA) / GPIO21 (SCL), with
 * DMA-fed transmit. The FF OSD device (I2C slave mode) is not supported
 * on this platform.
 *
 * Adapted from lcd_at32f435.c, written & released by Keir Fraser.
 *
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

/* PCF8574 pin assignment: D7-D6-D5-D4-BL-EN-RW-RS */
#define _D7 (1u<<7)
#define _D6 (1u<<6)
#define _D5 (1u<<5)
#define _D4 (1u<<4)
#define _BL (1u<<3)
#define _EN (1u<<2)
#define _RW (1u<<1)
#define _RS (1u<<0)

/* HD44780 commands */
#define CMD_ENTRYMODE    0x04
#define CMD_DISPLAYCTL   0x08
#define CMD_DISPLAYSHIFT 0x10
#define CMD_FUNCTIONSET  0x20
#define CMD_SETCGRADDR   0x40
#define CMD_SETDDRADDR   0x80
#define FS_2LINE         0x08

/* RP2350 I2C0. */
#define i2c rp_i2c0

#define SDA 20
#define SCL 21

/* I2C event IRQ (STOP detect / abort). */
#define I2C_EVENT_IRQ I2C0_IRQ
DEFINE_IRQ(I2C_EVENT_IRQ, "IRQ_i2c_event");

/* I2C error handling runs from a software interrupt. */
#define I2C_ERROR_IRQ SPARE_IRQ_3
DEFINE_IRQ(I2C_ERROR_IRQ, "IRQ_i2c_error");

/* DMA Tx: channel 2 (0-1 are the floppy flux engine). */
#define DMA_TX_CH 2
#define i2c_tx_dma (dma->ch[DMA_TX_CH])

bool_t has_osd; /* always FALSE: FF OSD unsupported on RP2350 */
uint8_t osd_buttons_tx;
uint8_t osd_buttons_rx;

static uint8_t _bl;
static uint8_t i2c_addr;
static uint8_t i2c_dead;
static uint8_t i2c_row;
static bool_t is_oled_display;
static uint8_t oled_height;

#define OLED_ADDR 0x3c
enum { OLED_unknown, OLED_ssd1306, OLED_sh1106 };
static uint8_t oled_model;
static void oled_init(void);
static unsigned int oled_prep_buffer(void);

#define I2C_RD TRUE
#define I2C_WR FALSE
static void i2c_start(uint8_t a, unsigned int nr, bool_t rd);

static void i2c_tx_tc(void);

/* Count of display-refresh completions. For synchronisation/flush. */
static volatile uint8_t refresh_count;

/* I2C data buffer, and the 16-bit DATA_CMD staging area it is expanded
 * into for DMA to the DW I2C controller. */
static uint8_t buffer[256] aligned(4);
static uint16_t dma_buffer[256] aligned(4);

/* Text buffer, rendered into I2C data and placed into buffer[]. */
static char text[4][40];

/* Columns and rows of text. */
uint8_t lcd_columns, lcd_rows;

/* Current display mode: Affects row ordering and sizing. */
uint8_t display_mode = DM_banner;
#define menu_mode (display_mode == DM_menu)

/* Remaining bytes in the current synchronous transaction. */
static unsigned int sync_rem;

/* Occasionally the I2C/DMA engine seems to get stuck. Detect this with
 * a timeout timer and unwedge it by calling the I2C error handler. */
#define DMA_TIMEOUT time_ms(200)
static struct timer timeout_timer;
static void timeout_fn(void *unused)
{
    IRQx_set_pending(I2C_ERROR_IRQ);
}

#define I2C_ABORTED (1u<<6) /* raw_intr_stat.TX_ABRT */

static void i2c_disable(void)
{
    stk_time_t t = stk_now();
    i2c->enable = 0;
    while ((i2c->enable_status & 1) && (stk_diff(t, stk_now()) < stk_ms(5)))
        cpu_relax();
}

/* I2C Error ISR: Reset the peripheral and reinit everything. */
static void IRQ_i2c_error(void)
{
    /* Dump and clear I2C errors. */
    printk("I2C: Error (%08x)\n", i2c->tx_abrt_source);
    (void)i2c->clr_tx_abrt;
    (void)i2c->clr_intr;

    /* Clear the DMA channel. */
    dma->chan_abort = m(DMA_TX_CH);
    while (i2c_tx_dma.ctrl_trig & DMA_CTRL_BUSY)
        cpu_relax();
    i2c_tx_dma.al1_ctrl = 0;

    /* Clear the I2C peripheral. */
    i2c->intr_mask = 0;
    i2c_disable();

    timer_cancel(&timeout_timer);

    lcd_init();
}

static void IRQ_i2c_event(void)
{
    uint32_t stat = i2c->intr_stat;

    if (stat & I2C_INT_TX_ABRT) {
        (void)i2c->clr_tx_abrt;
        IRQx_set_pending(I2C_ERROR_IRQ);
        return;
    }

    if (stat & I2C_INT_STOP_DET) {
        (void)i2c->clr_stop_det;
        i2c_tx_tc();
    }
}

/* Start an I2C DMA sequence. */
static void dma_start(unsigned int sz)
{
    unsigned int i;

    ASSERT(sz <= ARRAY_SIZE(dma_buffer));

    /* Expand into 16-bit DATA_CMD values; STOP rides the final byte. */
    for (i = 0; i < sz; i++)
        dma_buffer[i] = buffer[i];
    dma_buffer[sz-1] |= I2C_DATA_CMD_STOP;

    i2c_start(i2c_addr, sz, I2C_WR);

    i2c->dma_tdlr = 8;
    i2c->dma_cr = I2C_DMA_CR_TDMAE;
    i2c->intr_mask = I2C_INT_STOP_DET | I2C_INT_TX_ABRT;

    i2c_tx_dma.read_addr = (uint32_t)dma_buffer;
    i2c_tx_dma.write_addr = (uint32_t)&i2c->data_cmd;
    i2c_tx_dma.trans_count = sz;
    i2c_tx_dma.ctrl_trig = DMA_CTRL_EN | DMA_CTRL_SIZE_HALF
        | DMA_CTRL_INCR_READ | DMA_CTRL_CHAIN_TO(DMA_TX_CH)
        | DMA_CTRL_TREQ_SEL(DREQ_I2C0_TX);

    /* Set the timeout timer in case the DMA hangs for any reason. */
    timer_set(&timeout_timer, time_now() + DMA_TIMEOUT);
}

/* Emit a 4-bit command to the HD44780 via the DMA buffer. */
static void emit4(uint8_t **p, uint8_t val)
{
    *(*p)++ = val;
    *(*p)++ = val | _EN;
    *(*p)++ = val;
}

/* Emit an 8-bit command to the HD44780 via the DMA buffer. */
static void emit8(uint8_t **p, uint8_t val, uint8_t signals)
{
    signals |= _bl;
    emit4(p, (val & 0xf0) | signals);
    emit4(p, (val << 4) | signals);
}

/* Snapshot text buffer into the command buffer. */
static unsigned int lcd_prep_buffer(void)
{
    const static uint8_t row_offs[] = { 0x00, 0x40, 0x14, 0x54 };
    uint16_t order;
    char *p;
    uint8_t *q = buffer;
    unsigned int i, row;

    if (i2c_row >= lcd_rows) {
        i2c_row = 0;
        refresh_count++;
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

static void i2c_tx_tc(void)
{
    unsigned int dma_sz;

    /* Prepare the DMA buffer and start the next DMA sequence. */
    dma_sz = is_oled_display ? oled_prep_buffer() : lcd_prep_buffer();
    dma_start(dma_sz);
}

/* Wait for room in the transmit FIFO, checking for errors. */
static bool_t i2c_wait_tx(void)
{
    stk_time_t t = stk_now();
    for (;;) {
        if (i2c->raw_intr_stat & I2C_ABORTED) {
            (void)i2c->clr_tx_abrt;
            return FALSE;
        }
        if (i2c->status & 2) /* TFNF */
            return TRUE;
        if (stk_diff(t, stk_now()) > stk_ms(10)) {
            /* I2C bus seems to be locked up. */
            i2c_dead = TRUE;
            return FALSE;
        }
    }
}

static void i2c_start(uint8_t a, unsigned int nr, bool_t rd)
{
    i2c_disable();
    i2c->tar = a;
    i2c->enable = 1;
    sync_rem = nr;
}

/* Synchronously wait for the STOP at the end of a transaction. */
static bool_t i2c_stop(void)
{
    stk_time_t t = stk_now();
    for (;;) {
        uint32_t stat = i2c->raw_intr_stat;
        if (stat & I2C_ABORTED) {
            (void)i2c->clr_tx_abrt;
            return FALSE;
        }
        if (stat & I2C_INT_STOP_DET) {
            (void)i2c->clr_stop_det;
            return TRUE;
        }
        if (stk_diff(t, stk_now()) > stk_ms(10)) {
            i2c_dead = TRUE;
            return FALSE;
        }
    }
}

/* Synchronously transmit an I2C byte. */
static bool_t i2c_sync_write(uint8_t b)
{
    uint32_t cmd = b;
    if (!i2c_wait_tx())
        return FALSE;
    if (sync_rem && (--sync_rem == 0))
        cmd |= I2C_DATA_CMD_STOP;
    i2c->data_cmd = cmd;
    return TRUE;
}

/* Synchronously receive an I2C byte. */
static bool_t i2c_sync_read(uint8_t *pb)
{
    stk_time_t t = stk_now();
    uint32_t cmd = I2C_DATA_CMD_READ;

    if (!i2c_wait_tx())
        return FALSE;
    if (sync_rem && (--sync_rem == 0))
        cmd |= I2C_DATA_CMD_STOP;
    i2c->data_cmd = cmd;

    for (;;) {
        if (i2c->raw_intr_stat & I2C_ABORTED) {
            (void)i2c->clr_tx_abrt;
            return FALSE;
        }
        if (i2c->rxflr != 0) {
            *pb = i2c->data_cmd;
            return TRUE;
        }
        if (stk_diff(t, stk_now()) > stk_ms(10)) {
            i2c_dead = TRUE;
            return FALSE;
        }
    }
}

static bool_t i2c_sync_write_txn(uint8_t addr, uint8_t *cmds, unsigned int nr)
{
    unsigned int i;

    i2c_start(addr, nr, I2C_WR);

    for (i = 0; i < nr; i++)
        if (!i2c_sync_write(*cmds++))
            return FALSE;

    return i2c_stop();
}

static bool_t i2c_sync_read_txn(uint8_t addr, uint8_t *rsp, unsigned int nr)
{
    unsigned int i;

    i2c_start(addr, nr, I2C_RD);

    for (i = 0; i < nr; i++)
        if (!i2c_sync_read(rsp+i))
            return FALSE;

    return i2c_stop();
}

/* Write a 4-bit nibble over D7-D4 (4-bit bus). */
static void write4(uint8_t val)
{
    i2c_sync_write(val);
    i2c_sync_write(val | _EN);
    i2c_sync_write(val);
}

/* Check whether an I2C device is responding at given address. */
static bool_t i2c_probe(uint8_t a)
{
    i2c_start(a, 1, I2C_WR);
    if (!i2c_sync_write(0))
        return FALSE;
    return i2c_stop();
}

/* Check given inclusive range of addresses for a responding I2C device. */
static uint8_t i2c_probe_range(uint8_t s, uint8_t e)
{
    uint8_t a;
    for (a = s; (a <= e) && !i2c_dead; a++)
        if (i2c_probe(a))
            return a;
    return 0;
}

void lcd_clear(void)
{
    memset(text, ' ', sizeof(text));
}

void lcd_write(int col, int row, int min, const char *str)
{
    char c, *p;
    uint32_t oldpri;

    if (min < 0)
        min = lcd_columns;

    p = &text[row][col];

    /* Prevent the text[] getting rendered while we're updating it. */
    oldpri = IRQ_save(I2C_IRQ_PRI);

    while ((c = *str++) && (col++ < lcd_columns)) {
        *p++ = c;
        min--;
    }
    while ((min-- > 0) && (col++ < lcd_columns))
        *p++ = ' ';

    IRQ_restore(oldpri);
}

void lcd_backlight(bool_t on)
{
    /* Will be picked up the next time text[] is rendered. */
    _bl = on ? _BL : 0;
}

void lcd_sync(void)
{
    uint8_t c = refresh_count;
    while ((uint8_t)(refresh_count - c) < 2)
        cpu_relax();
}

/* Configure the DW I2C controller in master mode at the given bus speed.
 * Counts are in clk_sys (144MHz) cycles. */
static void i2c_configure(bool_t fast)
{
    i2c_disable();
    i2c->con = I2C_CON_MASTER | I2C_CON_SLAVE_DISABLE | I2C_CON_RESTART_EN
        | I2C_CON_TX_EMPTY_CTRL
        | (fast ? I2C_CON_SPEED_FAST : I2C_CON_SPEED_STD);
    /* 100kHz */
    i2c->ss_scl_hcnt = 630;
    i2c->ss_scl_lcnt = 730;
    /* 400kHz */
    i2c->fs_scl_hcnt = 140;
    i2c->fs_scl_lcnt = 210;
    i2c->sda_hold = 72; /* 500ns */
    i2c->rx_tl = 0;
    i2c->tx_tl = 0;
    i2c->intr_mask = 0;
    (void)i2c->clr_intr;
}

bool_t lcd_init(void)
{
    uint8_t a, *p;
    bool_t reinit = (i2c_addr != 0);

    has_osd = FALSE; /* FF OSD is not supported on RP2350 */
    i2c_dead = FALSE;
    i2c_row = 0;
    osd_buttons_rx = 0;

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
     * pull the lines low with our internal weak pull-downs. */
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

    /* Hand the pins to the I2C controller (with pull-ups: external
     * pull-ups are still recommended for speed). */
    gpio_set_pad(SCL, PAD_IE | PAD_PUE | PAD_DRIVE_4MA | PAD_SCHMITT);
    gpio_set_pad(SDA, PAD_IE | PAD_PUE | PAD_DRIVE_4MA | PAD_SCHMITT);
    io_bank0->gpio[SCL].ctrl = GPIO_FUNC_I2C;
    io_bank0->gpio[SDA].ctrl = GPIO_FUNC_I2C;

    /* Standard Mode (100kHz) */
    i2c_configure(FALSE);

    if (!reinit) {

        /* First probe after I2C re-initialisation seems to fail. So issue
         * a dummy probe first. */
        (void)i2c_probe(0);

        /* Probe the bus for an LCD or OLED display. */
        a = i2c_probe_range(0x20, 0x27) ?: i2c_probe_range(0x38, 0x3f);
        if (a == 0) {
            printk("I2C: %s\n",
                   i2c_dead ? "Bus locked up?" : "No device found");
            goto fail;
        }

        is_oled_display = (ff_cfg.display_type & DISPLAY_oled) ? TRUE
            : (ff_cfg.display_type & DISPLAY_lcd) ? FALSE
            : ((a&~1) == OLED_ADDR);

        if (is_oled_display) {
            oled_height = (ff_cfg.display_type & DISPLAY_oled_64) ? 64 : 32;
            lcd_columns = (ff_cfg.oled_font == FONT_8x16) ? 16
                : (ff_cfg.display_type & DISPLAY_narrower) ? 16
                : (ff_cfg.display_type & DISPLAY_narrow) ? 18 : 21;
            lcd_rows = 4;
        } else {
            lcd_columns = (ff_cfg.display_type >> _DISPLAY_lcd_columns) & 63;
            lcd_rows = (ff_cfg.display_type >> _DISPLAY_lcd_rows) & 7;
        }

        printk("I2C: %s found at 0x%02x\n",
               is_oled_display ? "OLED" : "LCD", a);
        i2c_addr = a;

        lcd_columns = max_t(uint8_t, lcd_columns, 16);
        lcd_columns = min_t(uint8_t, lcd_columns, 40);
        lcd_rows = max_t(uint8_t, lcd_rows, 2);
        lcd_rows = min_t(uint8_t, lcd_rows, 4);

        lcd_clear();

    }

    /* Enable the Event IRQ. */
    IRQx_set_prio(I2C_EVENT_IRQ, I2C_IRQ_PRI);
    IRQx_clear_pending(I2C_EVENT_IRQ);
    IRQx_enable(I2C_EVENT_IRQ);

    /* Enable the Error IRQ. */
    IRQx_set_prio(I2C_ERROR_IRQ, I2C_IRQ_PRI);
    IRQx_clear_pending(I2C_ERROR_IRQ);
    IRQx_enable(I2C_ERROR_IRQ);

    /* Timeout handler for if I2C transmission borks. */
    timer_init(&timeout_timer, timeout_fn, NULL);
    timer_set(&timeout_timer, time_now() + DMA_TIMEOUT);

    if (is_oled_display) {
        oled_init();
        return TRUE;
    }

    /* Initialise 4-bit interface, as in the datasheet. Do this synchronously
     * and with the required delays. */
    i2c_start(i2c_addr, 4*3, I2C_WR);
    write4(3 << 4);
    delay_us(4100);
    write4(3 << 4);
    delay_us(100);
    write4(3 << 4);
    write4(2 << 4);
    i2c_stop();

    /* More initialisation from the datasheet. Send by DMA. */
    p = buffer;
    emit8(&p, CMD_FUNCTIONSET | FS_2LINE, 0);
    emit8(&p, CMD_DISPLAYCTL, 0);
    emit8(&p, CMD_ENTRYMODE | 2, 0);
    emit8(&p, CMD_DISPLAYCTL | 4, 0); /* display on */
    dma_start(p - buffer);

    if (!reinit)
        lcd_backlight(TRUE);

    return TRUE;

fail:
    if (reinit)
        return FALSE;
    IRQx_disable(I2C_EVENT_IRQ);
    IRQx_disable(I2C_ERROR_IRQ);
    i2c_disable();
    gpio_configure_pin(gpiob, SCL, GPI_pull_up);
    gpio_configure_pin(gpiob, SDA, GPI_pull_up);
    return FALSE;
}

extern const uint8_t oled_font_6x13[];
static void oled_convert_text_row_6x13(char *pc)
{
    unsigned int i, c;
    const uint8_t *p;
    uint8_t *q = buffer;
    const unsigned int w = 6;

    q[0] = q[128] = 0;
    q++;

    for (i = 0; i < lcd_columns; i++) {
        if ((c = *pc++ - 0x20) > 0x5e)
            c = '.' - 0x20;
        p = &oled_font_6x13[c * w * 2];
        memcpy(q, p, w);
        memcpy(q+128, p+w, w);
        q += w;
    }

    /* Fill remainder of buffer[] with zeroes. */
    memset(q, 0, 127-lcd_columns*w);
    memset(q+128, 0, 127-lcd_columns*w);
}

#ifdef font_extra
extern const uint8_t oled_font_8x16[];
static void oled_convert_text_row_8x16(char *pc)
{
    unsigned int i, c;
    const uint8_t *p;
    uint8_t *q = buffer;
    const unsigned int w = 8;

    for (i = 0; i < lcd_columns; i++) {
        if ((c = *pc++ - 0x20) > 0x5e)
            c = '.' - 0x20;
        p = &oled_font_8x16[c * w * 2];
        memcpy(q, p, w);
        memcpy(q+128, p+w, w);
        q += w;
    }
}
#endif

static void oled_convert_text_row(char *pc)
{
#ifdef font_extra
    if (ff_cfg.oled_font == FONT_8x16)
        oled_convert_text_row_8x16(pc);
    else
#endif
        oled_convert_text_row_6x13(pc);
}

static unsigned int oled_queue_cmds(
    uint8_t *buf, const uint8_t *cmds, unsigned int nr)
{
    uint8_t *p = buf;

    while (nr--) {
        *p++ = 0x80; /* Co=1, Command */
        *p++ = *cmds++;
    }

    return p - buf;
}

static void oled_double_height(uint8_t *dst, uint8_t *src, uint8_t mask)
{
    const uint8_t tbl[] = {
        0x00, 0x03, 0x0c, 0x0f, 0x30, 0x33, 0x3c, 0x3f,
        0xc0, 0xc3, 0xcc, 0xcf, 0xf0, 0xf3, 0xfc, 0xff
    };
    uint8_t x, *p, *q;
    unsigned int i;
    if ((mask == 3) && (src == dst)) {
        p = src + 128;
        q = dst + 256;
        for (i = 0; i < 128; i++) {
            x = *--p;
            *--q = tbl[x>>4];
        }
        p = src + 128;
        for (i = 0; i < 128; i++) {
            x = *--p;
            *--q = tbl[x&15];
        }
    } else {
        p = src;
        q = dst;
        if (mask & 1) {
            for (i = 0; i < 128; i++) {
                x = *p++;
                *q++ = tbl[x&15];
            }
        }
        if (mask & 2) {
            p = src;
            for (i = 0; i < 128; i++) {
                x = *p++;
                *q++ = tbl[x>>4];
            }
        }
    }
}

static unsigned int oled_start_i2c(uint8_t *buf)
{
    static const uint8_t ssd1306_addr_cmds[] = {
        0x20, 0,      /* horizontal addressing mode */
        0x21, 0, 127, /* column address range: 0-127 */
        0x22,         /* page address range: ?-? */
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
        /* Page address: according to i2c_row. */
        *dc++ = i2c_row;
        *dc++ = 7;
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

    return p - buf;
}

static int oled_to_lcd_row(int in_row)
{
    uint16_t order;
    int i = 0, row;
    bool_t large = FALSE;

    order = (oled_height == 32) ? 0x7710 : menu_mode ? 0x7903 : 0x7183;
    if ((ff_cfg.display_order != DORD_default) && (display_mode == DM_normal))
        order = ff_cfg.display_order;

    for (;;) {
        large = !!(order & DORD_double);
        i += large ? 2 : 1;
        if (i > in_row)
            break;
        order >>= DORD_shift;
    }

    /* Remap the row */
    row = order & DORD_row;
    if (row < lcd_rows) {
        oled_convert_text_row(text[row]);
    } else {
        memset(buffer, 0, 256);
    }

    return large ? i - in_row : 0;
}

/* Snapshot text buffer into the bitmap buffer. */
static unsigned int oled_prep_buffer(void)
{
    int size;
    uint8_t *p = buffer;

    if (i2c_row >= (oled_height / 8)) {
        i2c_row = 0;
        refresh_count++;
    }

    /* Convert one row of text[] into buffer[] writes. */
    size = oled_to_lcd_row(i2c_row/2);
    if (size != 0) {
        oled_double_height(&buffer[128], &buffer[(size == 1) ? 128 : 0],
                           (i2c_row & 1) + 1);
    } else {
        if (!(i2c_row & 1))
            memcpy(&buffer[128], &buffer[0], 128);
    }

    /* New I2C transaction. */
    p += oled_start_i2c(p);

    /* Patch the data bytes onto the end of the address setup sequence. */
    memcpy(p, &buffer[128], 128);
    p += 128;

    i2c_row++;

    return p - buffer;
}

static bool_t oled_probe_model(void)
{
    uint8_t cmd1[] = { 0x80, 0x00, /* Column 0 */
                       0xc0 };     /* Read one data */
    uint8_t cmd2[] = { 0x80, 0x00, /* Column 0 */
                       0xc0, 0x00 }; /* Write one data */
    uint8_t rsp[2];
    int i;
    uint8_t x, px = 0;
    uint8_t *rand = (uint8_t *)emit8;

    for (i = 0; i < 3; i++) {
        /* 1st Write stage. */
        if (!i2c_sync_write_txn(i2c_addr, cmd1, sizeof(cmd1)))
            goto fail;
        /* Read stage. */
        if (!i2c_sync_read_txn(i2c_addr, rsp, sizeof(rsp)))
            goto fail;
        x = rsp[1];
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

    oled_model = (i == 3) ? OLED_sh1106 : OLED_ssd1306;
    printk("OLED: %s\n", (oled_model == OLED_sh1106) ? "SH1106" : "SSD1306");
    return TRUE;

fail:
    return FALSE;
}

static void oled_init_fast_mode(void)
{
    /* Fast Mode (400kHz). */
    i2c_configure(TRUE);
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

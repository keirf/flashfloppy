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

/* FF OSD command set */
#define OSD_BACKLIGHT    0x00 /* [0] = backlight on */
#define OSD_DATA         0x02 /* next columns*rows bytes are text data */
#define OSD_ROWS         0x10 /* [3:0] = #rows */
#define OSD_HEIGHTS      0x20 /* [3:0] = 1 iff row is 2x height */
#define OSD_BUTTONS      0x30 /* [3:0] = button mask */
#define OSD_COLUMNS      0x40 /* [6:0] = #columns */
struct packed i2c_osd_info {
    uint8_t protocol_ver;
    uint8_t fw_major, fw_minor;
    uint8_t buttons;
};

/* STM32 I2C peripheral. */
static volatile struct i2c *i2c = i2c2;

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

bool_t has_osd;
uint8_t osd_buttons_tx;
uint8_t osd_buttons_rx;
#define OSD_no    0
#define OSD_read  1
#define OSD_write 2
static uint8_t in_osd, osd_ver;
#define OSD_I2C_ADDR 0x10

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

static void i2c_stop(void);

static void dma_tx_tc_btf(void);

/* Count of display-refresh completions. For synchronisation/flush. */
static volatile uint8_t refresh_count;

/* I2C data buffer. Data is DMAed to the I2C peripheral. */
static uint8_t buffer[256] aligned(4);

/* Text buffer, rendered into I2C data and placed into buffer[]. */
static char text[4][40];

/* Columns and rows of text. */
uint8_t lcd_columns, lcd_rows;

/* Current display mode: Affects row ordering and sizing. */
uint8_t display_mode = DM_banner;
#define menu_mode (display_mode == DM_menu)

/* Occasionally the I2C/DMA engine seems to get stuck. Detect this with 
 * a timeout timer and unwedge it by calling the I2C error handler. */
#define DMA_TIMEOUT time_ms(200)
static struct timer timeout_timer;
static void timeout_fn(void *unused)
{
    IRQx_set_pending(I2C_ERROR_IRQ);
}

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
static unsigned int osd_prep_buffer(void)
{
    uint16_t order = menu_mode ? 0x7903 : 0x7183;
    char *p;
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
    for (i = 3; i >= 0; i--) {
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
        p = text[(order >> (row * DORD_shift)) & DORD_row];
        memcpy(q, p, lcd_columns);
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
    p = (row < ARRAY_SIZE(text)) ? text[row] : NULL;

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
    i2c->cr1 |= I2C_CR1_START;
    if (!i2c_start(a, I2C_WR) || !i2c_sync_write(0))
        return FALSE;
    i2c_stop();
    return TRUE;
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
    i2c->cr2 = I2C_CR2_FREQ(36);
    i2c->ccr = I2C_CCR_CCR(180);
    i2c->trise = 37;
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
            lcd_columns = (ff_cfg.oled_font == FONT_8x16) ? 16
                : (ff_cfg.display_type & DISPLAY_narrower) ? 16
                : (ff_cfg.display_type & DISPLAY_narrow) ? 18 : 21;
            lcd_rows = 4;
        } else {
            lcd_columns = (ff_cfg.display_type >> _DISPLAY_lcd_columns) & 63;
            lcd_columns = max_t(uint8_t, lcd_columns, 16);
            lcd_columns = min_t(uint8_t, lcd_columns, 40);
            lcd_rows = (ff_cfg.display_type >> _DISPLAY_lcd_rows) & 7;
            lcd_rows = max_t(uint8_t, lcd_rows, 2);
            lcd_rows = min_t(uint8_t, lcd_rows, 4);
        }

        if (a != 0) {
            printk("I2C: %s found at 0x%02x\n",
                   is_oled_display ? "OLED" : "LCD", a);
            i2c_addr = a;
        } else {
            is_oled_display = FALSE;
            if (ff_cfg.display_type == DISPLAY_auto)
                lcd_columns = 40;
        }

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
    i2c->cr2 |= I2C_CR2_ITERREN;

    /* Initialise DMA1 channel 4 and its completion interrupt. */
    i2c_tx_dma.cmar = (uint32_t)(unsigned long)buffer;
    i2c_tx_dma.cpar = (uint32_t)(unsigned long)&i2c->dr;
    dma1->ifcr = DMA_TX_CGIF;
    IRQx_set_prio(DMA_TX_IRQ, I2C_IRQ_PRI);
    IRQx_clear_pending(DMA_TX_IRQ);
    IRQx_enable(DMA_TX_IRQ);

    /* Initialise DMA1 channel 5 and its completion interrupt. */
    i2c_rx_dma.cmar = (uint32_t)(unsigned long)buffer;
    i2c_rx_dma.cpar = (uint32_t)(unsigned long)&i2c->dr;
    dma1->ifcr = DMA_RX_CGIF;
    IRQx_set_prio(DMA_RX_IRQ, I2C_IRQ_PRI);
    IRQx_clear_pending(DMA_RX_IRQ);
    IRQx_enable(DMA_RX_IRQ);

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
    
    /* Wait for DMA engine to initialise RAM, then turn on backlight. */
    if (!reinit) {
        lcd_sync();
        lcd_backlight(TRUE);
    }

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

static unsigned int ssd1306_prep_buffer(void)
{
    int size;

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
    size = oled_to_lcd_row(i2c_row);
    if (size != 0)
        oled_double_height(buffer, &buffer[(size == 1) ? 128 : 0], 0x3);

    i2c_row++;

    return 256;
}

static unsigned int sh1106_prep_buffer(void)
{
    int size;
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
    size = oled_to_lcd_row(i2c_row/2);
    if (size != 0) {
        oled_double_height(&buffer[128], &buffer[(size == 1) ? 128 : 0],
                           (i2c_row & 1) + 1);
    } else {
        if (!(i2c_row & 1))
            memcpy(&buffer[128], &buffer[0], 128);
    }

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
    i2c->cr2 = I2C_CR2_FREQ(36);
    i2c->ccr = I2C_CCR_FS | I2C_CCR_CCR(30);
    i2c->trise = 12;
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
    const uint8_t *cmds;
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

    /* Display is right-way-up, or rotated. */
    cmds = (ff_cfg.display_type & DISPLAY_rotate) ? rot_cmds : norot_cmds;
    p += oled_queue_cmds(p, cmds, sizeof(rot_cmds));

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

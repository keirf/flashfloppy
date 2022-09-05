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
#define i2c i2c2

#define SCL 10
#define SDA 11

/* I2C error ISR. */
#define I2C_ERROR_IRQ 34
void IRQ_34(void) __attribute__((alias("IRQ_i2c_error")));

/* I2C event ISR. */
#define I2C_EVENT_IRQ 33
void IRQ_33(void) __attribute__((alias("IRQ_i2c_event")));

/* DMA Tx. */
#define DMA_TX_CH 4
#define i2c_tx_dma dma1->ch[DMA_TX_CH-1]

/* DMA Rx. */
#define DMA_RX_CH 5
#define i2c_rx_dma dma1->ch[DMA_RX_CH-1]

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

#define I2C_RD TRUE
#define I2C_WR FALSE
static void i2c_start(uint8_t a, unsigned int nr, bool_t rd);

static void i2c_tx_tc(void);
static void i2c_rx_tc(void);

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
    printk("I2C: Error (%04x)\n", (uint16_t)(i2c->isr & I2C_SR_ERRORS));
    i2c->icr = I2C_SR_ERRORS;

    /* Clear the I2C peripheral. */
    i2c->cr1 = 0;
    i2c->cr1 = I2C_CR1_PE;

    /* Clear the DMA controller. */
    i2c_tx_dma.ccr = i2c_rx_dma.ccr = 0;

    timer_cancel(&timeout_timer);

    lcd_init();
}

static void IRQ_i2c_event(void)
{
    uint16_t sr = i2c->isr;

    if (sr & I2C_SR_STOPF) {
        i2c->icr = I2C_SR_STOPF;
        if (sr & I2C_SR_NACKF) {
            /* I2C automatically STOPs on NACK. But it's an error as far 
             * as we're concerned, so punt it down that path. */
            i2c->icr = I2C_SR_NACKF;
            IRQx_set_pending(I2C_ERROR_IRQ);
        } else if (i2c->cr2 & I2C_CR2_RD_WRN) {
            i2c_rx_tc();
        } else {
            i2c_tx_tc();
        }
    }
}

/* Start an I2C DMA sequence. */
static void dma_start(unsigned int sz)
{
    unsigned int addr = in_osd ? OSD_I2C_ADDR : i2c_addr;

    ASSERT(sz <= sizeof(buffer));

    if (in_osd == OSD_read) {

        i2c_rx_dma.ccr = 0;
        i2c->cr1 = (I2C_CR1_RXDMAEN | I2C_CR1_ERRIE | I2C_CR1_STOPIE);

        i2c_rx_dma.cndtr = sz;
        i2c_rx_dma.ccr = (DMA_CCR_MSIZE_8BIT |
                          DMA_CCR_PSIZE_32BIT |
                          DMA_CCR_MINC |
                          DMA_CCR_DIR_P2M |
                          DMA_CCR_EN);

        i2c_start(addr, sz, I2C_RD);

    } else {

        i2c_tx_dma.ccr = 0;
        i2c->cr1 = (I2C_CR1_TXDMAEN | I2C_CR1_ERRIE | I2C_CR1_STOPIE);

        i2c_tx_dma.cndtr = sz;
        i2c_tx_dma.ccr = (DMA_CCR_MSIZE_8BIT |
                          DMA_CCR_PSIZE_32BIT |
                          DMA_CCR_MINC |
                          DMA_CCR_DIR_M2P |
                          DMA_CCR_EN);

        i2c_start(addr, sz, I2C_WR);

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
        memset(buffer, 0x11, sizeof(struct i2c_osd_info));
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
        if (has_osd)
            return osd_prep_buffer();
    }

    if (i2c_row > lcd_rows) {
        i2c_row = 0;
        refresh_count++;
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

static void i2c_tx_tc(void)
{
    unsigned int dma_sz;

    /* Prepare the DMA buffer and start the next DMA sequence. */
    in_osd = OSD_no;
    if (i2c_addr == 0) {
        dma_sz = osd_prep_buffer();
    } else {
        dma_sz = is_oled_display ? oled_prep_buffer() : lcd_prep_buffer();
    }
    dma_start(dma_sz);
}

static void i2c_rx_tc(void)
{
    struct i2c_osd_info *info = (struct i2c_osd_info *)buffer;

    osd_buttons_rx = info->buttons;

    /* Now do the OSD write. */
    dma_start(osd_prep_buffer());
}

/* Wait for given status condition @s while also checking for errors. */
static bool_t i2c_wait(uint8_t s)
{
    stk_time_t t = stk_now();
    while ((i2c->isr & s) != s) {
        if (i2c->isr & I2C_SR_ERRORS) {
            i2c->icr = I2C_SR_ERRORS;
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

static void i2c_start(uint8_t a, unsigned int nr, bool_t rd)
{
    uint32_t cr2 = 0;

    ASSERT(nr <= 255);

    i2c->cr1 &= ~I2C_CR1_PE;
    i2c->cr1 |= I2C_CR1_PE;

    if (rd)
        cr2 |= I2C_CR2_RD_WRN;
    cr2 |= I2C_CR2_NBYTES(nr) | I2C_CR2_SADD(a<<1) | I2C_CR2_AUTOEND;
    i2c->cr2 = cr2;
    i2c->cr2 |= I2C_CR2_START;
}

/* Synchronously transmit the I2C STOP sequence. */
static bool_t i2c_stop(void)
{
    if (!i2c_wait(I2C_SR_STOPF))
        return FALSE;
    i2c->icr = I2C_SR_STOPF;
    return TRUE;
}

/* Synchronously transmit an I2C byte. */
static bool_t i2c_sync_write(uint8_t b)
{
    if (!i2c_wait(I2C_SR_TXIS))
        return FALSE;
    i2c->txdr = b;
    return TRUE;
}

/* Synchronously transmitreceive an I2C byte. */
static bool_t i2c_sync_read(uint8_t *pb)
{
    if (!i2c_wait(I2C_SR_RXNE))
        return FALSE;
    *pb = i2c->rxdr;
    return TRUE;
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

bool_t lcd_init(void)
{
    uint8_t a, *p;
    bool_t reinit = (i2c_addr != 0) || has_osd;

    i2c_dead = FALSE;
    i2c_row = 0;
    in_osd = OSD_no;
    osd_buttons_rx = 0;

    rcc->apb1enr |= RCC_APB1ENR_I2C2EN;

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

    gpio_set_af(gpiob, SCL, 4);
    gpio_set_af(gpiob, SDA, 4);
    gpio_configure_pin(gpiob, SCL, AFO_opendrain(_2MHz));
    gpio_configure_pin(gpiob, SDA, AFO_opendrain(_2MHz));

    /* Standard Mode (100kHz) */
    i2c->timingr = I2C_TIMING_100k;
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
            (void)i2c_sync_read_txn(OSD_I2C_ADDR, &osd_ver, 1);
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

    dmamux1->cctrl[DMA_TX_CH-1] = DMAMUX_CCTRL_REQSEL(DMAMUX_REQ_I2C2_TX);
    dmamux1->cctrl[DMA_RX_CH-1] = DMAMUX_CCTRL_REQSEL(DMAMUX_REQ_I2C2_RX);

    /* Initialise DMA1 channel 4 and its completion interrupt. */
    i2c_tx_dma.cmar = (uint32_t)(unsigned long)buffer;
    i2c_tx_dma.cpar = (uint32_t)(unsigned long)&i2c->txdr;

    /* Initialise DMA1 channel 5 and its completion interrupt. */
    i2c_rx_dma.cmar = (uint32_t)(unsigned long)buffer;
    i2c_rx_dma.cpar = (uint32_t)(unsigned long)&i2c->rxdr;

    /* Timeout handler for if I2C transmission borks. */
    timer_init(&timeout_timer, timeout_fn, NULL);
    timer_set(&timeout_timer, time_now() + DMA_TIMEOUT);

    if (is_oled_display) {
        oled_init();
        return TRUE;
    } else if (i2c_addr == 0) {
        dma_start(osd_prep_buffer());
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
    i2c->cr1 = 0;
    gpio_configure_pin(gpiob, SCL, GPI_pull_up);
    gpio_configure_pin(gpiob, SDA, GPI_pull_up);
    rcc->apb1enr &= ~RCC_APB1ENR_I2C2EN;
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

    if (i2c_row == (oled_height / 8)) {
        i2c_row++;
        if (has_osd)
            return osd_prep_buffer();
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
    /* Disable I2C (currently in Standard Mode). */
    i2c->cr1 = 0;

    /* Fast Mode (400kHz). */
    i2c->timingr = I2C_TIMING_400k;
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

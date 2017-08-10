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

/* STM32 I2C peripheral. */
#define i2c i2c2
#define SCL 10
#define SDA 11

/* I2C error ISR. */
#define I2C_ERROR_IRQ 34
void IRQ_34(void) __attribute__((alias("IRQ_i2c_error")));

/* DMA completion ISR. */
#define DMA1_CH4_IRQ 14
void IRQ_14(void) __attribute__((alias("IRQ_dma1_ch4_tc")));

static uint8_t _bl;
static uint8_t i2c_addr;
static uint8_t i2c_dead;

#define OLED_ADDR 0x3c
static void oled_init(void);
static unsigned int oled_prep_buffer(void);

/* Count of DMA completions. For synchronisation/flush. */
static volatile uint8_t dma_count;

/* I2C data buffer. Data is DMAed to the I2C peripheral. */
static uint32_t buffer[512/4];

/* 16x2 text buffer, rendered into I2C data and placed into buffer[]. */
static char text[2][16];

/* Occasionally the I2C/DMA engine seems to get stuck. Detect this with 
 * a timeout timer and unwedge it by calling the I2C error handler. */
#define DMA_TIMEOUT stk_ms(200)
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
    dma1->ch4.ccr = 0;
    dma1->ifcr = DMA_IFCR_CGIF(4);

    timer_cancel(&timeout_timer);

    lcd_init();
}

/* Start an I2C DMA sequence. */
static void dma_start(unsigned int sz)
{
    dma1->ch4.cmar = (uint32_t)(unsigned long)buffer;
    dma1->ch4.cndtr = sz;
    dma1->ch4.ccr = (DMA_CCR_MSIZE_8BIT |
                     DMA_CCR_PSIZE_16BIT |
                     DMA_CCR_MINC |
                     DMA_CCR_DIR_M2P |
                     DMA_CCR_TCIE |
                     DMA_CCR_EN);

    /* Set the timeout timer in case the DMA hangs for any reason. */
    timer_set(&timeout_timer, stk_add(stk_now(), DMA_TIMEOUT));
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
    uint8_t *q = (uint8_t *)buffer;
    unsigned int i, j;

    for (i = 0; i < 2; i++) {
        emit8(&q, CMD_SETDDRADDR | (i*64), 0);
        for (j = 0; j < 16; j++)
            emit8(&q, text[i][j], _RS);
    }

    return q - (uint8_t *)buffer;
}

static void IRQ_dma1_ch4_tc(void)
{
    unsigned int dma_sz;

    /* Clear the DMA controller. */
    dma1->ch4.ccr = 0;
    dma1->ifcr = DMA_IFCR_CGIF(4);

    /* Prepare the DMA buffer and start the next DMA sequence. */
    dma_sz = (i2c_addr == OLED_ADDR) ? oled_prep_buffer() : lcd_prep_buffer();
    dma_start(dma_sz);

    dma_count++;
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

/* Synchronously transmit the I2C START sequence. */
static bool_t i2c_start(uint8_t a)
{
    i2c->cr1 |= I2C_CR1_START;
    if (!i2c_wait(I2C_SR1_SB))
        return FALSE;
    i2c->dr = a << 1;
    if (!i2c_wait(I2C_SR1_ADDR))
        return FALSE;
    (void)i2c->sr2;
    return TRUE;
}

/* Synchronously transmit an I2C command. */
static bool_t i2c_cmd(uint8_t cmd)
{
    i2c->dr = cmd;
    return i2c_wait(I2C_SR1_BTF);
}

/* Write a 4-bit nibble over D7-D4 (4-bit bus). */
static void write4(uint8_t val)
{
    i2c_cmd(val);
    i2c_cmd(val | _EN);
    i2c_cmd(val);
}

/* Check whether an I2C device is responding at given address. */
static bool_t i2c_probe(uint8_t a)
{
    if (!i2c_start(a) || !i2c_cmd(0))
        return FALSE;
    i2c->cr1 |= I2C_CR1_STOP;
    while (i2c->cr1 & I2C_CR1_STOP)
        continue;
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
    lcd_write(0, 0, 16, "");
    lcd_write(0, 1, 16, "");
}

void lcd_write(int col, int row, int min, const char *str)
{
    char c, *p = &text[row][col];
    uint32_t oldpri;

    /* Prevent the text[] getting rendered while we're updating it. */
    oldpri = IRQ_save(I2C_IRQ_PRI);

    while ((c = *str++) && (col++ < 16)) {
        *p++ = c;
        min--;
    }
    while ((min-- > 0) && (col++ < 16))
        *p++ = ' ';

    IRQ_restore(oldpri);
}

bool_t lcd_has_backlight(void)
{
    return (i2c_addr != OLED_ADDR);
}

void lcd_backlight(bool_t on)
{
    /* Will be picked up the next time text[] is rendered. */
    _bl = on ? _BL : 0;
}

void lcd_sync(void)
{
    uint8_t c = dma_count;
    /* Two IRQs: 1st: text[] -> buffer[]; 2nd: buffer[] -> I2C. */
    while ((uint8_t)(c - dma_count) < 2)
        cpu_relax();
}

bool_t lcd_init(void)
{
    uint8_t a, *p;
    bool_t reinit = (i2c_addr != 0);

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
        gpio_configure_pin(gpiob, SCL, GPI_pull_down);
        gpio_configure_pin(gpiob, SDA, GPI_pull_down);
        delay_us(10);
        if (!gpio_read_pin(gpiob, SCL) || !gpio_read_pin(gpiob, SDA)) {
            printk("I2C: Invalid bus\n");
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
        /* Probe the bus for an I2C device. */
        a = i2c_probe_range(0x20, 0x27) ?: i2c_probe_range(0x38, 0x3f);
        if (a == 0) {
            printk("I2C: %s\n",
                   i2c_dead ? "Bus locked up?" : "No device found");
            goto fail;
        }

        printk("I2C: %s found at 0x%02x\n",
               (a == OLED_ADDR) ? "OLED" : "LCD", a);
        i2c_addr = a;

        lcd_clear();
    }

    /* Enable the Error IRQ. */
    IRQx_set_prio(I2C_ERROR_IRQ, I2C_IRQ_PRI);
    IRQx_enable(I2C_ERROR_IRQ);
    i2c->cr2 |= I2C_CR2_ITERREN;

    /* Initialise DMA1 channel 4 and its completion interrupt. */
    dma1->ch4.cpar = (uint32_t)(unsigned long)&i2c->dr;
    dma1->ifcr = DMA_IFCR_CGIF(4);
    IRQx_set_prio(DMA1_CH4_IRQ, I2C_IRQ_PRI);
    IRQx_enable(DMA1_CH4_IRQ);

    /* Timeout handler for if I2C transmission borks. */
    timer_init(&timeout_timer, timeout_fn, NULL);
    timer_set(&timeout_timer, stk_add(stk_now(), DMA_TIMEOUT));

    if (!i2c_start(i2c_addr))
        goto fail;

    if (i2c_addr == OLED_ADDR) {
        oled_init();
        return TRUE;
    }

    /* Initialise 4-bit interface, as in the datasheet. Do this synchronously
     * and with the required delays. */
    write4(3 << 4);
    delay_us(4100);
    write4(3 << 4);
    delay_us(100);
    write4(3 << 4);
    write4(2 << 4);

    /* More initialisation from the datasheet. Send by DMA. */
    p = (uint8_t *)buffer;
    emit8(&p, CMD_FUNCTIONSET | FS_2LINE, 0);
    emit8(&p, CMD_DISPLAYCTL, 0);
    emit8(&p, CMD_ENTRYMODE | 2, 0);
    emit8(&p, CMD_DISPLAYCTL | 4, 0); /* display on */
    i2c->cr2 |= I2C_CR2_DMAEN;
    dma_start(p - (uint8_t *)buffer);
    
    /* Wait for DMA engine to initialise RAM, then turn on backlight. */
    if (!reinit) {
        lcd_sync();
        lcd_backlight(TRUE);
    }

    return TRUE;

fail:
    if (reinit)
        return FALSE;
    IRQx_disable(I2C_ERROR_IRQ);
    IRQx_disable(DMA1_CH4_IRQ);
    i2c->cr1 &= ~I2C_CR1_PE;
    gpio_configure_pin(gpiob, SCL, GPI_pull_up);
    gpio_configure_pin(gpiob, SDA, GPI_pull_up);
    rcc->apb1enr &= ~RCC_APB1ENR_I2C2EN;
    return FALSE;
}

/* ASCII 0x20-0x7f inclusive */
const static uint32_t oled_font[] = {
    0x00000000, 0x00000000, 0x00000000, 0x00000000, 
    0xfc380000, 0x000038fc, 0x0d000000, 0x0000000d, 
    0x001e0e00, 0x000e1e00, 0x00000000, 0x00000000, 
    0x20f8f820, 0x0020f8f8, 0x020f0f02, 0x00020f0f, 
    0x47447c38, 0x0098cc47, 0x38080c06, 0x00070f38, 
    0x80003030, 0x003060c0, 0x0103060c, 0x000c0c00, 
    0xe47cd880, 0x0040d8bc, 0x08080f07, 0x00080f07, 
    0x0e1e1000, 0x00000000, 0x00000000, 0x00000000, 
    0xf8f00000, 0x0000040c, 0x07030000, 0x0000080c, 
    0x0c040000, 0x0000f0f8, 0x0c080000, 0x00000307, 
    0xc0e0a080, 0x80a0e0c0, 0x01030200, 0x00020301, 
    0xe0808000, 0x008080e0, 0x03000000, 0x00000003, 
    0x00000000, 0x00000000, 0x1e100000, 0x0000000e, 
    0x80808080, 0x00808080, 0x00000000, 0x00000000, 
    0x00000000, 0x00000000, 0x0c000000, 0x0000000c, 
    0x80000000, 0x003060c0, 0x0103060c, 0x00000000, 
    0xc40cf8f0, 0x00f0f80c, 0x080c0703, 0x0003070c, 
    0xfc181000, 0x000000fc, 0x0f080800, 0x0008080f, 
    0xc4840c08, 0x00183c64, 0x08090f0e, 0x000c0c08, 
    0x44440c08, 0x00b8fc44, 0x08080c04, 0x00070f08, 
    0x98b0e0c0, 0x0080fcfc, 0x08000000, 0x00080f0f, 
    0x44447c7c, 0x0084c444, 0x08080c04, 0x00070f08, 
    0x444cf8f0, 0x0080c044, 0x08080f07, 0x00070f08, 
    0x84040c0c, 0x003c7cc4, 0x0f0f0000, 0x00000000, 
    0x4444fcb8, 0x00b8fc44, 0x08080f07, 0x00070f08, 
    0x44447c38, 0x00f8fc44, 0x08080800, 0x0003070c, 
    0x30000000, 0x00000030, 0x06000000, 0x00000006, 
    0x30000000, 0x00000030, 0x0e080000, 0x00000006, 
    0x60c08000, 0x00081830, 0x03010000, 0x00080c06, 
    0x20202000, 0x00202020, 0x01010100, 0x00010101, 
    0x30180800, 0x0080c060, 0x060c0800, 0x00000103, 
    0xc4041c18, 0x00183ce4, 0x0d000000, 0x0000000d, 
    0xc808f8f0, 0x00f0f8c8, 0x0b080f07, 0x00010b0b, 
    0x8c98f0e0, 0x00e0f098, 0x00000f0f, 0x000f0f00, 
    0x44fcfc04, 0x00b8fc44, 0x080f0f08, 0x00070f08, 
    0x040cf8f0, 0x00180c04, 0x080c0703, 0x00060c08, 
    0x04fcfc04, 0x00f0f80c, 0x080f0f08, 0x0003070c, 
    0x44fcfc04, 0x001c0ce4, 0x080f0f08, 0x000e0c08, 
    0x44fcfc04, 0x001c0ce4, 0x080f0f08, 0x00000000, 
    0x840cf8f0, 0x00988c84, 0x080c0703, 0x000f0708, 
    0x4040fcfc, 0x00fcfc40, 0x00000f0f, 0x000f0f00, 
    0xfc040000, 0x000004fc, 0x0f080000, 0x0000080f, 
    0x04000000, 0x0004fcfc, 0x08080f07, 0x0000070f, 
    0xc0fcfc04, 0x001c3ce0, 0x000f0f08, 0x000e0f01, 
    0x04fcfc04, 0x00000000, 0x080f0f08, 0x000e0c08, 
    0x7038fcfc, 0x00fcfc38, 0x00000f0f, 0x000f0f00, 
    0x7038fcfc, 0x00fcfce0, 0x00000f0f, 0x000f0f00, 
    0x0404fcf8, 0x00f8fc04, 0x08080f07, 0x00070f08, 
    0x44fcfc04, 0x00387c44, 0x080f0f08, 0x00000000, 
    0x0404fcf8, 0x00f8fc04, 0x0e080f07, 0x00273f3c, 
    0x44fcfc04, 0x0038fcc4, 0x000f0f08, 0x000f0f00, 
    0x44643c18, 0x00189cc4, 0x08080e06, 0x00070f08, 
    0xfc0c1c00, 0x001c0cfc, 0x0f080000, 0x0000080f, 
    0x0000fcfc, 0x00fcfc00, 0x08080f07, 0x00070f08, 
    0x0000fcfc, 0x00fcfc00, 0x0c060301, 0x00010306, 
    0xc000fcfc, 0x00fcfc00, 0x030e0f07, 0x00070f0e, 
    0xe0f03c0c, 0x000c3cf0, 0x01030f0c, 0x000c0f03, 
    0xc07c3c00, 0x003c7cc0, 0x0f080000, 0x0000080f, 
    0xc4840c1c, 0x001c3c64, 0x08090f0e, 0x000e0c08, 
    0xfcfc0000, 0x00000404, 0x0f0f0000, 0x00000808, 
    0xc0e07038, 0x00000080, 0x01000000, 0x000e0703, 
    0x04040000, 0x0000fcfc, 0x08080000, 0x00000f0f, 
    0x03060c08, 0x00080c06, 0x00000000, 0x00000000, 
    0x00000000, 0x00000000, 0x20202020, 0x20202020, 
    0x06020000, 0x0000080c, 0x00000000, 0x00000000, 
    0xa0a0a000, 0x0000c0e0, 0x08080f07, 0x00080f07, 
    0x20fcfc04, 0x0080c060, 0x080f0f00, 0x00070f08, 
    0x2020e0c0, 0x00406020, 0x08080f07, 0x00040c08, 
    0x2460c080, 0x0000fcfc, 0x08080f07, 0x00080f07, 
    0xa0a0e0c0, 0x00c0e0a0, 0x08080f07, 0x00040c08, 
    0xfcf84000, 0x00180c44, 0x0f0f0800, 0x00000008, 
    0x2020e0c0, 0x0020e0c0, 0x48486f27, 0x00003f7f, 
    0x40fcfc04, 0x00c0e020, 0x000f0f08, 0x000f0f00, 
    0xec200000, 0x000000ec, 0x0f080000, 0x0000080f, 
    0x00000000, 0x00ecec20, 0x40703000, 0x003f7f40, 
    0x80fcfc04, 0x002060c0, 0x010f0f08, 0x000c0e03, 
    0xfc040000, 0x000000fc, 0x0f080000, 0x0000080f, 
    0xc060e0e0, 0x00c0e060, 0x07000f0f, 0x000f0f00, 
    0x20c0e020, 0x00c0e020, 0x000f0f00, 0x000f0f00, 
    0x2020e0c0, 0x00c0e020, 0x08080f07, 0x00070f08, 
    0x20c0e020, 0x00c0e020, 0x487f7f40, 0x00070f08, 
    0x2020e0c0, 0x0020e0c0, 0x48080f07, 0x00407f7f, 
    0x60c0e020, 0x00c0e020, 0x080f0f08, 0x00000000, 
    0x20a0e040, 0x00406020, 0x09090c04, 0x00040e0b, 
    0xfcf82020, 0x00002020, 0x0f070000, 0x00040c08, 
    0x0000e0e0, 0x0000e0e0, 0x08080f07, 0x00080f07, 
    0x0000e0e0, 0x00e0e000, 0x080c0703, 0x0003070c, 
    0x8000e0e0, 0x00e0e000, 0x070c0f07, 0x00070f0c, 
    0x80c06020, 0x002060c0, 0x03070c08, 0x00080c07, 
    0x0000e0e0, 0x00e0e000, 0x48484f47, 0x001f3f68, 
    0xa0206060, 0x002060e0, 0x090b0e0c, 0x000c0c08, 
    0xf8404000, 0x000404bc, 0x07000000, 0x0008080f, 
    0xfc000000, 0x000000fc, 0x0f000000, 0x0000000f, 
    0xbc040400, 0x004040f8, 0x0f080800, 0x00000007, 
    0x06020604, 0x00020604, 0x00000000, 0x00000000, 
    0x3060c080, 0x0080c060, 0x04040707, 0x00070704, 
};

/* Snapshot text buffer into the bitmap buffer. */
static unsigned int oled_prep_buffer(void)
{
    const uint32_t *p;
    uint32_t *q = buffer;
    unsigned int i, j, c;

    for (i = 0; i < 2; i++) {
        for (j = 0; j < 16; j++) {
            if ((c = text[i][j] - 0x20) > 0x5f)
                c = '.' - 0x20;
            p = &oled_font[c * 4];
            *q++ = *p++;
            *q++ = *p++;
            q[32-2] = *p++;
            q[32-1] = *p++;
        }
        q += 32;
    }

    return sizeof(buffer);
}

static void oled_init(void)
{
    static const uint8_t init_cmds[] = {
        0xae,       /* display off */
        0xd5, 0x80, /* default clock */
        0xa8, 31,   /* multiplex ratio (lcd height - 1) */
        0xd3, 0x00, /* display offset = 0 */
        0x40,       /* display start line = 0 */
        0x8d, 0x14, /* enable charge pump */
        0x20, 0x00, /* horizontal addressing mode */
        0xa1,       /* segment mapping (reverse) */
        0xc8,       /* com scan direction (decrement) */
        0xda, 0x02, /* com pins configuration */
        0x81, 0x8f, /* display contrast */
        0xd9, 0xf1, /* pre-charge period */
        0xdb, 0x20, /* vcomh detect (default) */
        0xa4,       /* output follows ram contents */
        0xa6,       /* normal display output (inverse=off) */
        0x2e,       /* deactivate scroll */
        0xaf,       /* display on */
        0x21, 0, 127, /* column address range: 0-127 */
        0x22, 0, 3    /* page address range: 0-3 */
    };

    uint8_t *p = (uint8_t *)buffer;
    int i;

    /* Initialisation sequence for SSD1306. */
    for (i = 0; i < sizeof(init_cmds); i++) {
        *p++ = 0x80; /* Co=1, Command */
        *p++ = init_cmds[i];
    }

    /* All subsequent bytes are data bytes, forever more. */
    *p++ = 0x40;

    /* Send the initialisation command sequence by DMA. */
    i2c->cr2 |= I2C_CR2_DMAEN;
    dma_start(p - (uint8_t *)buffer);
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

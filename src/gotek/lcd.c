/*
 * lcd.c
 * 
 * HD44780 LCD controller via a PCF8574 I2C backpack.
 * 
 * Written & released by Keir Fraser <keir.xen@gmail.com>
 * 
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

/* Pin assignment: D7-D6-D5-D4-BL-EN-RW-RS */
#define _D7 (1u<<7)
#define _D6 (1u<<6)
#define _D5 (1u<<5)
#define _D4 (1u<<4)
#define _BL (1u<<3)
#define _EN (1u<<2)
#define _RW (1u<<1)
#define _RS (1u<<0)

#define CMD_DISPLAYCLEAR 0x01
#define CMD_RETURNHOME   0x02
#define CMD_ENTRYMODE    0x04
#define CMD_DISPLAYCTL   0x08
#define CMD_DISPLAYSHIFT 0x10
#define CMD_FUNCTIONSET  0x20
#define CMD_SETCGRADDR   0x40
#define CMD_SETDDRADDR   0x80

#define FS_2LINE         0x08

#define i2c i2c2

#define I2C_EVENT_IRQ 33
#define I2C_ERROR_IRQ 34
void IRQ_33(void) __attribute__((alias("IRQ_i2c_event")));
void IRQ_34(void) __attribute__((alias("IRQ_i2c_error")));

static uint8_t _bl;
static uint8_t addr = 0x20;
static uint8_t i2c_dead;

/* Ring buffer for I2C. */
#define I2CS_IDLE  0
#define I2CS_START 1
#define I2CS_ADDR  2
#define I2CS_DATA  3
#define I2CS_STOP  4
static uint8_t state;
static uint8_t buffer[256];
static uint16_t bc, bp;

static void IRQ_i2c_event(void)
{
    uint16_t sr1 = i2c->sr1 & I2C_SR1_EVENTS;

    if (sr1 & I2C_SR1_SB) {
        i2c->dr = addr<<1; /* clears I2C_SR1_SB */
        state = I2CS_ADDR;
    }

    if (sr1 & I2C_SR1_ADDR) {
        (void)i2c->sr2; /* clears I2C_SR1_ADDR */
        state = I2CS_DATA;
    }

    switch (state) {
    case I2CS_DATA:
        if (!(sr1 & I2C_SR1_TXE)) {
            i2c->cr2 |= I2C_CR2_ITBUFEN; /* request IRQ on TXE */
            break;
        }
        i2c->dr = buffer[bc++ % sizeof(buffer)]; /* clears TXE */
        state = I2CS_STOP;
        i2c->cr2 &= ~I2C_CR2_ITBUFEN;
        break;
    case I2CS_STOP:
        if ((sr1 & (I2C_SR1_TXE|I2C_SR1_BTF)) != (I2C_SR1_TXE|I2C_SR1_BTF))
            break;
        i2c->cr1 |= I2C_CR1_STOP; /* clears TXE and BTF */
        state = I2CS_IDLE;
        if (bc != bp) {
            while (i2c->cr1 & I2C_CR1_STOP)
                continue;
            state = I2CS_START;
            i2c->cr1 |= I2C_CR1_START;
        }
        break;
    }
}

static void IRQ_i2c_error(void)
{
    printk("I2C Error %04x\n", i2c->cr1);
    i2c->sr1 &= ~I2C_SR1_ERRORS;
    i2c->cr1 = 0;
    i2c->cr1 = I2C_CR1_SWRST;
    i2c->cr1 = 0;
    i2c->cr1 = I2C_CR1_PE;
    state = I2CS_IDLE;
    if (bc != bp) {
        state = I2CS_START;
        i2c->cr1 |= I2C_CR1_START;
    }
}

static void i2c_sync(void)
{
    while (state != I2CS_IDLE)
        cpu_relax();
    while (i2c->cr1 & I2C_CR1_STOP)
        cpu_relax();
}

static void i2c_delay_us(unsigned int usec)
{
    i2c_sync();
    delay_us(usec);
}

/* Send an I2C command to the PCF8574. */
static void i2c_cmd(uint8_t cmd)
{
    while ((bp - bc) == sizeof(buffer))
        cpu_relax();
    cmd |= _bl;
    IRQx_disable(I2C_EVENT_IRQ);
    buffer[bp++ % sizeof(buffer)] = cmd;
    if (state == I2CS_IDLE) {
        state = I2CS_START;
        i2c->cr1 |= I2C_CR1_START;
    }
    IRQx_enable(I2C_EVENT_IRQ);
}

/* Write a 4-bit nibble over D7-D4 (4-bit bus). */
static void write4(uint8_t val)
{
    i2c_cmd(val);
    i2c_cmd(val | _EN);
    i2c_cmd(val);
}

/* Write an 8-bit command over the 4-bit bus. */
static void write8(uint8_t val)
{
    write4(val & 0xf0);
    write4(val << 4);
}

/* Write an 8-bit RAM byte over the 4-bit bus. */
static void write8_ram(uint8_t val)
{
    write4((val & 0xf0) | _RS);
    write4((val << 4) | _RS);
}

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

/* Check whether an I2C device is responding at given address. */
static bool_t i2c_probe(uint8_t _addr)
{
    i2c->cr1 |= I2C_CR1_START;
    if (!i2c_wait(I2C_SR1_SB)) return FALSE;
    i2c->dr = _addr<<1;
    if (!i2c_wait(I2C_SR1_ADDR)) return FALSE;
    (void)i2c->sr2;
    if (!i2c_wait(I2C_SR1_TXE)) return FALSE;
    i2c->dr = _bl;
    if (!i2c_wait(I2C_SR1_TXE | I2C_SR1_BTF)) return FALSE;
    i2c->cr1 |= I2C_CR1_STOP;
    return TRUE;
}

void lcd_clear(void)
{
    write8(CMD_DISPLAYCLEAR);
    i2c_delay_us(2000); /* slow to clear */
}

void lcd_write(int col, int row, int min, const char *str)
{
    char c;
    write8(CMD_SETDDRADDR | (col + row*64));
    while ((c = *str++)) {
        write8_ram(c);
        min--;
    }
    while (min-- > 0)
        write8_ram(' ');
}

void lcd_backlight(bool_t on)
{
    _bl = on ? _BL : 0;
    i2c_cmd(0);
}

void lcd_sync(void)
{
    i2c_sync();
}

bool_t lcd_init(void)
{
    rcc->apb1enr |= RCC_APB1ENR_I2C2EN;

    gpio_configure_pin(gpiob, 10, AFO_opendrain(_2MHz)); /* PB10 = SCL2 */
    gpio_configure_pin(gpiob, 11, AFO_opendrain(_2MHz)); /* PB11 = SDA2 */

    /* Check we have a clear I2C bus. Both clock and data must be high. */
    if (!gpio_read_pin(gpiob, 10) || !gpio_read_pin(gpiob, 11)) {
        printk("I2C: Invalid bus state\n");
        goto fail;
    }

    /* Standard Mode (100kHz) */
    i2c->cr2 = I2C_CR2_FREQ(36);
    i2c->ccr = I2C_CCR_CCR(180);
    i2c->trise = 37;
    i2c->cr1 = I2C_CR1_PE;

    /* Probe the bus for an I2C device. */
    for (addr = 0x20; addr < 0x28; addr++)
        if (i2c_probe(addr) || i2c_dead)
            break;
    if (i2c_dead) {
        printk("I2C: Bus locked up?\n");
        goto fail;
    }
    if (addr == 0x28) {
        printk("I2C: No device found\n");
        goto fail;
    }

    printk("I2C: LCD found at %02x\n", addr);

    IRQx_set_prio(I2C_EVENT_IRQ, I2C_IRQ_PRI);
    IRQx_enable(I2C_EVENT_IRQ);
    IRQx_set_prio(I2C_ERROR_IRQ, I2C_IRQ_PRI);
    IRQx_enable(I2C_ERROR_IRQ);
    i2c->cr2 |= I2C_CR2_ITEVTEN | I2C_CR2_ITERREN;

    /* Initialise 4-bit interface, as in the datasheet. */
    write4(3 << 4);
    i2c_delay_us(4100);
    write4(3 << 4);
    i2c_delay_us(100);
    write4(3 << 4);
    write4(2 << 4);

    /* More initialisation from the datasheet. */
    write8(CMD_FUNCTIONSET | FS_2LINE);
    write8(CMD_DISPLAYCTL);
    lcd_clear();
    _bl = _BL;
    write8(CMD_ENTRYMODE | 2);
    write8(CMD_DISPLAYCTL | 4); /* display on */

    return TRUE;

fail:
    i2c->cr1 &= ~I2C_CR1_PE;
    gpio_configure_pin(gpiob, 10, GPI_pull_up);
    gpio_configure_pin(gpiob, 11, GPI_pull_up);
    rcc->apb1enr &= ~RCC_APB1ENR_I2C2EN;
    return FALSE;
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

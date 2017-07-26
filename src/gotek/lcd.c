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
static void IRQ_i2c_error(void);

static bool_t force_bl;
static uint8_t _bl;
static uint8_t addr;
static uint8_t i2c_dead;

/* Ring buffer for I2C. */
#define I2CS_IDLE  0
#define I2CS_START 1
#define I2CS_DATA  2
static uint8_t state;
static uint8_t buffer[256];
static uint16_t bc, bp;

static void IRQ_i2c_event(void)
{
    uint16_t sr1 = i2c->sr1 & I2C_SR1_EVENTS;

    /* HACK: To allow lcd_backlight() control from IRQ. */
    if (unlikely(force_bl)) {
        force_bl = FALSE;
        /* If buffer is empty, just replay the last command */
        if (bc == bp)
            bc--;
        /* If the state machine is idle, kick things off. */
        if (state == I2CS_IDLE) {
            state = I2CS_START;
            i2c->cr1 |= I2C_CR1_START;
        }
    }

    switch (state) {

    case I2CS_IDLE:
        printk("Unexpected I2C IRQ sr1=%04x\n", i2c->sr1);
        IRQ_i2c_error();
        break;

    case I2CS_START:
        if (sr1 & I2C_SR1_SB) {
            /* Send address. Clears SR1_SB. */
            i2c->dr = addr<<1;
        }
        if (sr1 & I2C_SR1_ADDR) {
            /* Read SR2 clears SR1_ADDR. */
            (void)i2c->sr2;
            /* Send data0. Clears SR1_TXE. */
            i2c->dr = buffer[bc++ % sizeof(buffer)] | _bl;
            state = I2CS_DATA;
        }
        break;

    case I2CS_DATA:
        if (!(sr1 & I2C_SR1_BTF))
            break;
        if (bc != bp) {
            /* Send dataN. Clears SR1_TXE and SR1_BTF. */
            i2c->dr = buffer[bc++ % sizeof(buffer)] | _bl;
        } else {
            /* Send STOP. Clears SR1_TXE and SR1_BTF. */
            i2c->cr1 |= I2C_CR1_STOP;
            while (i2c->cr1 & I2C_CR1_STOP)
                continue;
            if (bc != bp) {
                state = I2CS_START;
                i2c->cr1 |= I2C_CR1_START;
            } else {
                state = I2CS_IDLE;
            }
        }
        break;

    }
}

static void IRQ_i2c_error(void)
{
    printk("I2C Error cr1=%04x sr1=%04x\n", i2c->cr1, i2c->sr1);
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
    ASSERT(!in_exception());
    while (state != I2CS_IDLE)
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
    ASSERT(!in_exception());
    while ((uint16_t)(bp - bc) == sizeof(buffer))
        cpu_relax();
    buffer[bp++ % sizeof(buffer)] = cmd;
    barrier(); /* push command /then/ check state */
    if (state == I2CS_IDLE) {
        IRQx_disable(I2C_EVENT_IRQ);
        if ((state == I2CS_IDLE) && (bc != bp)) {
            state = I2CS_START;
            i2c->cr1 |= I2C_CR1_START;
        }
        IRQx_enable(I2C_EVENT_IRQ);
    }
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
static bool_t i2c_probe(uint8_t a)
{
    i2c->cr1 |= I2C_CR1_START;
    if (!i2c_wait(I2C_SR1_SB)) return FALSE;
    i2c->dr = a<<1;
    if (!i2c_wait(I2C_SR1_ADDR)) return FALSE;
    (void)i2c->sr2;
    i2c->dr = 0;
    if (!i2c_wait(I2C_SR1_BTF)) return FALSE;
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
    if (!in_exception()) {
        /* Send a dummy command for the new setting to piggyback on. */
        i2c_cmd(0);
    } else {
        /* We can't poke the command ring directly from IRQ context, so instead 
         * we have a sneaky side channel into the ISR. */
        IRQx_disable(I2C_EVENT_IRQ);
        force_bl = TRUE;
        IRQx_set_pending(I2C_EVENT_IRQ);
        IRQx_enable(I2C_EVENT_IRQ);
    }
}

void lcd_sync(void)
{
    i2c_sync();
}

bool_t lcd_init(void)
{
    uint8_t a;

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
    a = i2c_probe_range(0x20, 0x27) ?: i2c_probe_range(0x38, 0x3f);
    if (a == 0) {
        printk("I2C: %s\n", i2c_dead ? "Bus locked up?" : "No device found");
        goto fail;
    }

    printk("I2C: LCD found at %02x\n", a);
    addr = a;

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

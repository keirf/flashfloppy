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
#define SCL 10
#define SDA 11

#define I2C_EVENT_IRQ 33
#define I2C_ERROR_IRQ 34
void IRQ_33(void) __attribute__((alias("IRQ_i2c_event")));
void IRQ_34(void) __attribute__((alias("IRQ_i2c_error")));

static bool_t force_bl;
static uint8_t _bl;
static uint8_t addr;
static uint8_t i2c_dead;

/* Callouts to handle 128x32 OLED, which appears on i2c address 0x3c. */
#define OLED_ADDR 0x3c
void oled_clear(void);
void oled_write(int col, int row, int min, const char *str);
void oled_sync(void);

/* Ring buffer for I2C. */
#define I2CS_IDLE      0 /* Bus is idle, no transaction */
#define I2CS_START     1 /* START and ADDR phases */
#define I2CS_DATA      2 /* Data phase: ring buffer non-empty */
#define I2CS_DATA_WAIT 3 /* Data phase: ring buffer empty */
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
    }

    switch (state) {

    case I2CS_IDLE:
        ASSERT(!sr1);
        if (bc == bp)
            break;
        state = I2CS_START;
        i2c->cr1 |= I2C_CR1_START;
        break;

    case I2CS_START:
        ASSERT(bc != bp);
        if (sr1 & I2C_SR1_SB) {
            /* Send address. Clears SR1_SB. */
            i2c->dr = addr<<1;
            break;
        }
        ASSERT(!(sr1 & ~I2C_SR1_ADDR));
        if (!(sr1 & I2C_SR1_ADDR))
            break;
        /* Read SR2 clears SR1_ADDR. */
        (void)i2c->sr2;
        /* Send data0. Clears SR1_TXE. */
        i2c->dr = buffer[bc++ % sizeof(buffer)] | _bl;
        sr1 = 0;

    case I2CS_DATA:
    case I2CS_DATA_WAIT:
        ASSERT(!(sr1 & ~I2C_SR1_BTF));
        state = I2CS_DATA;
        if ((sr1 & I2C_SR1_BTF) && (bc != bp)) {
            /* Send dataN. Clears SR1_TXE and SR1_BTF. */
            i2c->dr = buffer[bc++ % sizeof(buffer)] | _bl;
        }
        if (bc == bp) {
            state = I2CS_DATA_WAIT;
            IRQx_disable(I2C_EVENT_IRQ);
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
    IRQx_set_pending(I2C_EVENT_IRQ);
    IRQx_enable(I2C_EVENT_IRQ);
}

static void i2c_sync(void)
{
    ASSERT(!in_exception());
    /* Wait for ring buffer to drain. */
    while (bc != bp)
        cpu_relax();
    /* Wait for last byte to be fully transmitted. */
    while ((state == I2CS_DATA_WAIT) && !(i2c->sr1 & I2C_SR1_BTF))
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
    switch (state) {
    case I2CS_IDLE:
        IRQx_set_pending(I2C_EVENT_IRQ);
    case I2CS_DATA_WAIT:
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
    if (addr == OLED_ADDR)
        return oled_clear();

    write8(CMD_DISPLAYCLEAR);
    i2c_delay_us(2000); /* slow to clear */
}

void lcd_write(int col, int row, int min, const char *str)
{
    char c;

    if (addr == OLED_ADDR)
        return oled_write(col, row, min, str);

    write8(CMD_SETDDRADDR | (col + row*64));
    while ((c = *str++) && (col++ < 16)) {
        write8_ram(c);
        min--;
    }
    while ((min-- > 0) && (col++ < 16))
        write8_ram(' ');
}

bool_t lcd_has_backlight(void)
{
    return (addr != OLED_ADDR);
}

void lcd_backlight(bool_t on)
{
    if (addr == OLED_ADDR)
        return;

    _bl = on ? _BL : 0;
    barrier(); /* set new flag /then/ kick i2c */
    if (!in_exception()) {
        /* Send a dummy command for the new setting to piggyback on. */
        i2c_cmd(0);
    } else {
        /* We can't poke the command ring directly from IRQ context, so instead 
         * we have a sneaky side channel into the ISR. */
        force_bl = TRUE;
        barrier(); /* set flag /then/ fire IRQ */
        IRQx_set_pending(I2C_EVENT_IRQ);
        IRQx_enable(I2C_EVENT_IRQ);
    }
}

void lcd_sync(void)
{
    if (addr == OLED_ADDR)
        return oled_sync();

    i2c_sync();
}

bool_t oled_init(uint8_t i2c_addr);

bool_t lcd_init(void)
{
    uint8_t a;

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
    gpio_configure_pin(gpiob, SCL, GPI_pull_down);
    gpio_configure_pin(gpiob, SDA, GPI_pull_down);
    delay_us(10);
    if (!gpio_read_pin(gpiob, SCL) || !gpio_read_pin(gpiob, SDA)) {
        printk("I2C: Invalid bus\n");
        goto fail;
    }

    gpio_configure_pin(gpiob, SCL, AFO_opendrain(_2MHz));
    gpio_configure_pin(gpiob, SDA, AFO_opendrain(_2MHz));

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

    printk("I2C: %s found at 0x%02x\n", (a == OLED_ADDR) ? "OLED" : "LCD", a);
    addr = a;

    if (a == OLED_ADDR)
        return oled_init(a);

    IRQx_set_prio(I2C_EVENT_IRQ, I2C_IRQ_PRI);
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
    lcd_backlight(TRUE);
    write8(CMD_ENTRYMODE | 2);
    write8(CMD_DISPLAYCTL | 4); /* display on */

    return TRUE;

fail:
    i2c->cr1 &= ~I2C_CR1_PE;
    gpio_configure_pin(gpiob, SCL, GPI_pull_up);
    gpio_configure_pin(gpiob, SDA, GPI_pull_up);
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

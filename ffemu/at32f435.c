/*
 * at32f435.c
 *
 * The AT32F435's display driver, built for that MCU beside the STM32F105's,
 * for which the rest of the firmware half is built, and run in its place
 * while emu_mcu says so; with a model of the AT32F435's I2C, a newer kind
 * than the STM32F105's. The driver's names are prefixed, and the lcd_*()
 * functions that the firmware calls are here, calling the one driver or the
 * other: the variables that the firmware reads stay the STM32F105 driver's,
 * and the AT32F435 driver's copies of them follow them.
 *
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

#define lcd_init at435_lcd_init
#define lcd_clear at435_lcd_clear
#define lcd_write at435_lcd_write
#define lcd_write_shifted at435_lcd_write_shifted
#define lcd_column_pixels at435_lcd_column_pixels
#define lcd_backlight at435_lcd_backlight
#define lcd_sync at435_lcd_sync
#define lcd_row_columns at435_lcd_row_columns
#define lcd_columns at435_lcd_columns
#define lcd_rows at435_lcd_rows
#define display_mode at435_display_mode
#define is_oled_display at435_is_oled_display
#define has_osd at435_has_osd
#define osd_buttons_tx at435_osd_buttons_tx
#define osd_buttons_rx at435_osd_buttons_rx
#define IRQ_33 at435_IRQ_33
#define IRQ_34 at435_IRQ_34

/* As the firmware declares the functions, which the driver calls before it
 * defines them. */
bool_t lcd_init(void);
void lcd_clear(void);

#include "../src/display/lcd_at32f435.c"

/* The driver's name for the I2C it uses, which the model below spells out. */
#undef i2c

#undef lcd_init
#undef lcd_clear
#undef lcd_write
#undef lcd_write_shifted
#undef lcd_column_pixels
#undef lcd_backlight
#undef lcd_sync
#undef lcd_row_columns
#undef lcd_columns
#undef lcd_rows
#undef display_mode
#undef is_oled_display
#undef has_osd
#undef osd_buttons_tx
#undef osd_buttons_rx
#undef IRQ_33
#undef IRQ_34

/* The register blocks of the kinds that only the AT32F435 has. Every input
 * reads high, as the I2C lines do with their pull-ups when the driver checks
 * them before it starts. */
struct flash emu435_flash;
struct pwr emu435_pwr;
struct rcc emu435_rcc;
struct gpio emu435_gpio[8] = { [0 ... 7] = { .idr = 0xffff } };
struct syscfg emu435_syscfg;
struct dmamux emu435_dmamux[2];
struct i2c emu435_i2c[2];

/* The pins are not modelled: the I2C below serves the bus whatever they
 * are set to. */
void at435_gpio_configure_pin(GPIO gpio, unsigned int pin, unsigned int mode)
{
}

void gpio_set_af(GPIO gpio, unsigned int pin, unsigned int af)
{
}

/*
 * I2C2, a master that counts the bytes of a transfer and may end it by
 * itself, and the DMA channels that feed it. As in hw.c, a write of the
 * firmware's to TXDR is noticed by a marker bit above the byte, which it
 * clears. A read of RXDR leaves no trace, so every byte that a transfer
 * reads is its last: the driver's one read of more than a byte, its probe
 * of an SH1106, looks at the last.
 */

#define TXDR_IDLE 0x100u
#define I2C_BYTE_NS 22500 /* 9 bit times at 400 kHz */

static volatile struct i2c * const bus = &emu435_i2c[1];
static volatile struct dma_chn * const bus_tx = &emu_dma[0].ch[DMA_TX_CH-1];
static volatile struct dma_chn * const bus_rx = &emu_dma[0].ch[DMA_RX_CH-1];

/* The write going on: its bytes still to come, and the DMA of them. */
static struct {
    bool_t on, dma;
    unsigned int left;
    uint64_t dma_done_ns;
} wr;

static void isr_update(uint32_t clear, uint32_t set)
{
    emu_reg_update(&bus->isr, clear, set);
}

/* The transfer's last byte has gone: AUTOEND stops it, else it waits. */
static void transfer_done(void)
{
    wr.on = wr.dma = FALSE;
    if (bus->cr2 & I2C_CR2_AUTOEND) {
        emu_i2c_dev_stop();
        isr_update(I2C_SR_TXIS, I2C_SR_STOPF);
    } else {
        isr_update(I2C_SR_TXIS, I2C_SR_TC);
    }
}

static void transfer_start(uint64_t now)
{
    uint32_t cr2 = bus->cr2;
    unsigned int i, nr = (cr2 >> 16) & 0xff;
    bool_t rd = !!(cr2 & I2C_CR2_RD_WRN);
    uint8_t b = 0, *p;

    __sync_fetch_and_and(&bus->cr2, ~I2C_CR2_START);
    isr_update(I2C_SR_TXIS | I2C_SR_RXNE | I2C_SR_TC | I2C_SR_STOPF
               | I2C_SR_NACKF, 0);
    bus->txdr = TXDR_IDLE;
    wr.on = wr.dma = FALSE;

    if (!emu_i2c_dev_start((cr2 >> 1) & 0x7f, rd)) {
        /* No acknowledge: the master stops by itself. */
        emu_i2c_dev_stop();
        isr_update(0, I2C_SR_NACKF | I2C_SR_STOPF);
        return;
    }

    if (rd) {
        p = ((bus->cr1 & I2C_CR1_RXDMAEN) && (bus_rx->ccr & DMA_CCR_EN))
            ? emu_ptr32(bus_rx->cmar) : NULL;
        for (i = 0; i < nr; i++) {
            b = emu_i2c_dev_read();
            if ((p != NULL) && (i < bus_rx->cndtr))
                p[i] = b;
        }
        if (p != NULL)
            bus_rx->cndtr = 0;
        bus->rxdr = b;
        isr_update(0, I2C_SR_RXNE);
        transfer_done();
        return;
    }

    wr.on = TRUE;
    wr.left = nr;
    if (nr == 0) {
        transfer_done();
    } else if ((bus->cr1 & I2C_CR1_TXDMAEN) && (bus_tx->ccr & DMA_CCR_EN)
               && (bus_tx->cndtr != 0)) {
        wr.dma = TRUE;
        wr.dma_done_ns = now + (uint64_t)bus_tx->cndtr * I2C_BYTE_NS;
    } else {
        isr_update(0, I2C_SR_TXIS);
    }
}

static void bus_write(uint64_t now)
{
    unsigned int i, nr;
    uint8_t *p;

    if (wr.dma) {
        if (now < wr.dma_done_ns)
            return;
        p = emu_ptr32(bus_tx->cmar);
        nr = min_t(unsigned int, bus_tx->cndtr, wr.left);
        for (i = 0; i < nr; i++)
            emu_i2c_dev_write(p[i]);
        bus_tx->cndtr = 0;
        __sync_fetch_and_or(&emu_dma[0].isr, DMA_ISR_TCIF(DMA_TX_CH)
                            | DMA_ISR_GIF(DMA_TX_CH));
        wr.dma = FALSE;
        wr.left -= nr;
    } else if (!(bus->txdr & TXDR_IDLE)) {
        emu_i2c_dev_write(bus->txdr);
        bus->txdr = TXDR_IDLE;
        wr.left--;
    } else {
        return;
    }
    if (wr.left == 0)
        transfer_done();
}

/* The variables of the display driver that the firmware sets, and those it
 * reads, between the firmware and the AT32F435's driver. */
static void follow(void)
{
    at435_display_mode = display_mode;
    at435_osd_buttons_tx = osd_buttons_tx;
    lcd_columns = at435_lcd_columns;
    lcd_rows = at435_lcd_rows;
    is_oled_display = at435_is_oled_display;
    has_osd = at435_has_osd;
    osd_buttons_rx = at435_osd_buttons_rx;
}

/* When the DMA of the write going on ends, or 0. */
uint64_t at435_bus_due_ns(void)
{
    return wr.dma ? wr.dma_done_ns : 0;
}

void at435_bus_sync(uint64_t now)
{
    uint32_t icr, cr1, isr;

    follow();

    /* Writing 1s to ICR clears the matching ISR flags. */
    icr = __sync_lock_test_and_set(&bus->icr, 0);
    if (icr)
        isr_update(icr, 0);

    if (!(bus->cr1 & I2C_CR1_PE)) {
        if (wr.on)
            emu_i2c_dev_stop();
        wr.on = wr.dma = FALSE;
        bus->isr = 0;
        bus->txdr = TXDR_IDLE;
        return;
    }

    if (bus->cr2 & I2C_CR2_STOP) {
        __sync_fetch_and_and(&bus->cr2, ~I2C_CR2_STOP);
        emu_i2c_dev_stop();
        wr.on = wr.dma = FALSE;
        isr_update(I2C_SR_TXIS | I2C_SR_TC, I2C_SR_STOPF);
    }
    if (bus->cr2 & I2C_CR2_START)
        transfer_start(now);
    if (wr.on)
        bus_write(now);

    /* Interrupt requests are levels, held for as long as their cause is. */
    cr1 = bus->cr1;
    isr = bus->isr;
    if (((cr1 & I2C_CR1_STOPIE) && (isr & I2C_SR_STOPF))
        || ((cr1 & I2C_CR1_TCIE) && (isr & I2C_SR_TC))
        || ((cr1 & I2C_CR1_NACKIE) && (isr & I2C_SR_NACKF))
        || ((cr1 & I2C_CR1_TXIE) && (isr & I2C_SR_TXIS))
        || ((cr1 & I2C_CR1_RXIE) && (isr & I2C_SR_RXNE)))
        emu_irqx_set_pending(I2C_EVENT_IRQ);
}

/*
 * The display driver that the firmware calls.
 */

bool_t f105_lcd_init(void);
void f105_lcd_clear(void);
void f105_lcd_write(int col, int row, int min, const char *str);
void f105_lcd_write_shifted(int col, int row, int nr, unsigned int px,
                            const char *str);
unsigned int f105_lcd_column_pixels(void);
void f105_lcd_backlight(bool_t on);
void f105_lcd_sync(void);
unsigned int f105_lcd_row_columns(unsigned int row);

#define at435 (emu_mcu == EMU_MCU_at32f435)

bool_t lcd_init(void)
{
    bool_t ok;

    if (!at435)
        return f105_lcd_init();
    follow();
    ok = at435_lcd_init();
    follow();
    return ok;
}

void lcd_clear(void)
{
    if (at435)
        at435_lcd_clear();
    else
        f105_lcd_clear();
}

void lcd_write(int col, int row, int min, const char *str)
{
    if (at435)
        at435_lcd_write(col, row, min, str);
    else
        f105_lcd_write(col, row, min, str);
}

void lcd_write_shifted(int col, int row, int nr, unsigned int px,
                       const char *str)
{
    if (at435)
        at435_lcd_write_shifted(col, row, nr, px, str);
    else
        f105_lcd_write_shifted(col, row, nr, px, str);
}

unsigned int lcd_column_pixels(void)
{
    return at435 ? at435_lcd_column_pixels() : f105_lcd_column_pixels();
}

void lcd_backlight(bool_t on)
{
    if (at435)
        at435_lcd_backlight(on);
    else
        f105_lcd_backlight(on);
}

void lcd_sync(void)
{
    if (at435)
        at435_lcd_sync();
    else
        f105_lcd_sync();
}

unsigned int lcd_row_columns(unsigned int row)
{
    if (!at435)
        return f105_lcd_row_columns(row);
    follow();
    return at435_lcd_row_columns(row);
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

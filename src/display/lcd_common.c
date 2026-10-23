/*
 * lcd_common.c
 *
 * What the display drivers have in common, whichever I2C controller they
 * drive: the HD44780 and FF OSD command sets, the text rows and their
 * rendering into OLED pixel columns. Each driver includes it after its own
 * definitions of the I2C peripheral.
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
#define OSD_ROWS         0x10 /* [1:0] = #rows */
#define OSD_HEIGHTS      0x20 /* [3:0] = 1 iff row is 2x height */
#define OSD_BUTTONS      0x30 /* [3:0] = button mask */
#define OSD_COLUMNS      0x40 /* [6:0] = #columns */
struct packed i2c_osd_info {
    uint8_t protocol_ver;
    uint8_t fw_major, fw_minor;
    uint8_t buttons;
};

bool_t has_osd;
volatile uint8_t osd_buttons_tx;
volatile uint8_t osd_buttons_rx;
#define OSD_no    0
#define OSD_read  1
#define OSD_write 2
static uint8_t in_osd, osd_ver;
#define OSD_I2C_ADDR 0x10

static uint8_t _bl;
static uint8_t i2c_addr;
static uint8_t i2c_dead;
static uint8_t i2c_row;
bool_t is_oled_display;
static uint8_t oled_height;
/* The font that lcd_columns is sized for. FF.CFG may change
 * ff_cfg.oled_font before it resets the system. */
static uint8_t oled_font;

#define OLED_ADDR 0x3c
enum { OLED_unknown, OLED_ssd1306, OLED_sh1106 };
static uint8_t oled_model;
static void oled_init(void);
static unsigned int oled_prep_buffer(void);

/* Count of display-refresh completions. For synchronisation/flush. */
static volatile uint8_t refresh_count;

/* I2C data buffer. Data is DMAed to the I2C peripheral. */
static uint8_t buffer[256] aligned(4);

/* Text buffer, rendered into I2C data and placed into buffer[]: rows 0 to 3
 * and 4, the image's name and track, as display-order names them. */
static char text[5][40];

/* The rows' layouts of half width, for a row that display-order draws double
 * width on an OLED, whose columns are 21 at most. */
static char text_wide[ARRAY_SIZE(text)][21];

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

/* Each driver's own, which the code below calls. */
static bool_t i2c_probe(uint8_t a);
static bool_t i2c_sync_write(uint8_t b);
#ifdef font_extra
static void oled_convert_text_row_8x16(char *pc);
#endif

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

/* Write a 4-bit nibble over D7-D4 (4-bit bus). */
static void write4(uint8_t val)
{
    i2c_sync_write(val);
    i2c_sync_write(val | _EN);
    i2c_sync_write(val);
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
    memset(text_wide, ' ', sizeof(text_wide));
}

void lcd_write(int col, int row, int min, const char *str)
{
    char c, *p;
    uint32_t oldpri;
    int columns = lcd_columns;

    if (row & LCD_WIDE) {
        columns /= 2;
        p = &text_wide[row & ~LCD_WIDE][col];
    } else {
        p = &text[row][col];
    }

    if (min < 0)
        min = columns;

    /* Prevent the text[] getting rendered while we're updating it. */
    oldpri = IRQ_save(I2C_IRQ_PRI);

    while ((c = *str++) && (col++ < columns)) {
        *p++ = c;
        min--;
    }
    while ((min-- > 0) && (col++ < columns))
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

#if TARGET == TARGET_apple2
#include "oled_spinner.c"

/* Draws the phase spinners of text row @pc over its rendering in buffer[],
 * which holds @half of a double-height row, or 0 for a row of normal
 * height, stretched to double width if @wide. */
static void oled_draw_spinners(const char *pc, unsigned int half,
                               bool_t wide)
{
    unsigned int i, w = (oled_font == FONT_8x16) ? 8 : 6, x = (w == 6);

    for (i = 0; i < (wide ? lcd_columns/2 : lcd_columns); i++, x += w)
        if (((uint8_t)pc[i] & ~0xf) == OLED_SPINNER)
            oled_spinner(&buffer[wide ? 2*x : x], w, pc[i] & 0xf, half);
}
#endif

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

static void oled_convert_text_row(char *pc)
{
#ifdef font_extra
    if (oled_font == FONT_8x16)
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

/* Stretches the left half of the text row in buffer[] to its full width. */
static void oled_double_width(void)
{
    int x;

    /* Each page from its right, so that no column is written before it is
     * read. */
    for (x = 255; x >= 0; x--)
        buffer[x] = buffer[(x & 128) | ((x & 127) >> 1)];
}

unsigned int lcd_row_columns(unsigned int row)
{
    /* As oled_to_lcd_row() takes display_order, on the normal screen. */
    if (is_oled_display && (display_mode == DM_normal)
        && (ff_cfg.display_order_wide & (1 << row)))
        return lcd_columns / 2;
    return lcd_columns;
}

/* Renders the 16 pixel rows @in_row of the display into buffer[], its top and
 * bottom pages: a text row, or half of a double-height one. */
static void oled_to_lcd_row(int in_row)
{
    uint16_t order;
    int i = 0, row, half;
    bool_t large = FALSE, wide;
    char *pc;

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
    wide = large && (lcd_row_columns(row) < lcd_columns);
    pc = (row >= ARRAY_SIZE(text)) ? NULL : wide ? text_wide[row] : text[row];
    if (pc != NULL) {
        oled_convert_text_row(pc);
        if (wide)
            oled_double_width();
    } else {
        memset(buffer, 0, 256);
    }

    half = large ? i - in_row : 0;
    if (large)
        oled_double_height(buffer, &buffer[(half == 1) ? 128 : 0], 0x3);

#if TARGET == TARGET_apple2
    if (pc != NULL)
        oled_draw_spinners(pc, half, wide);
#endif
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

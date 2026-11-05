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

/* Each row's Apple2 phase spinner, outside the character encoding. */
static struct oled_spinner {
    uint8_t row, col, width, phases;
} oled_spinners[2][ARRAY_SIZE(text)];

/* Of each text row, in its layouts of normal and of half width, the columns
 * that an OLED shows px pixels to the left in the normal display mode, from
 * col to col + nr - 1, the pixels so freed at their right from next's glyph:
 * see lcd_write_shifted(). */
static struct oled_shift {
    uint8_t col, nr, px;
    char next;
} oled_shift[2][ARRAY_SIZE(text)];

/* Columns and rows of text. */
uint8_t lcd_columns, lcd_rows;

/* Current display mode: Affects row ordering and sizing. */
uint8_t display_mode = DM_banner;
#define menu_mode (display_mode == DM_menu)
#define glyphs_mode (display_mode == DM_glyphs)
#define error_mode (display_mode >= DM_error)

/* An error screen's rows, the message in the status row: below the image's
 * name, or alone below a blank row, double height where the display has
 * room, and alone double height on any OLED. */
static uint16_t error_order(void)
{
    bool_t oled64 = is_oled_display && (oled_height == 64);

    if (display_mode == DM_error_name)
        return oled64 ? 0x7798 : 0x7710;
    return oled64 ? 0x779f : is_oled_display ? 0x7779 : 0x7717;
}

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
static void oled_glyph_8x16(uint8_t *q, unsigned int g, unsigned int from,
                            unsigned int to);
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

/* The targets that show only their own messages, in ASCII, give them as
 * they are. */
#if (TARGET == TARGET_bootloader) || (TARGET == TARGET_bl_update) \
    || (TARGET == TARGET_io_test)
#define lcd_char(c) (c)
#define osd_map_row(q) ((void)0)
#define lcd_cgram_block(p) ((void)0)
#else
/* Character @c of the text as the LCD's ROM shows it, but in the font test,
 * which shows the ROM's glyph of each code. */
#define lcd_char(c) (glyphs_mode ? (uint8_t)(c) \
    : charset_to_rom((uint8_t)(c), ff_cfg.lcd_charset))

/* Text row @q, lcd_columns long, in place as FF OSD's font shows it. */
static void osd_map_row(uint8_t *q)
{
    unsigned int i;

    if (glyphs_mode)
        return;
    for (i = 0; i < lcd_columns; i++)
        q[i] = charset_to_rom(q[i], CHARSET_osd);
}

/* A02 has no full block, which charset_to_rom() gives a character that the
 * ROM cannot show: CGRAM character 0 is one, written by its mirror 0x08. */
static void lcd_cgram_block(uint8_t **p)
{
    unsigned int i;

    if (ff_cfg.lcd_charset != LCDCS_eur)
        return;
    emit8(p, CMD_SETCGRADDR, 0);
    for (i = 0; i < 8; i++)
        emit8(p, 0x1f, _RS);
}
#endif

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
    memset(oled_spinners, 0, sizeof(oled_spinners));
    memset(oled_shift, 0, sizeof(oled_shift));
}

void lcd_write_spinner(int row, unsigned int col, unsigned int width,
                       unsigned int phases)
{
    struct oled_spinner *s = &oled_spinners[!!(row & LCD_WIDE)][row & ~LCD_WIDE];
    uint32_t oldpri = IRQ_save(I2C_IRQ_PRI);

    s->row = row & ~LCD_WIDE;
    s->col = col;
    s->width = width;
    s->phases = phases;
    IRQ_restore(oldpri);
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

void lcd_write_glyphs(int row, unsigned int base)
{
    unsigned int i;
    uint32_t oldpri = IRQ_save(I2C_IRQ_PRI);

    memset(text[row], ' ', lcd_columns);
    for (i = 0; i < 16; i++)
        text[row][i] = base + i;

    IRQ_restore(oldpri);
}

unsigned int lcd_shown_rows(void)
{
    /* FF OSD alone shows two: the font test gives it no more. */
    return is_oled_display ? oled_height / 16 : i2c_addr ? lcd_rows : 2;
}

void lcd_write_shifted(int col, int row, int nr, unsigned int px,
                       const char *str)
{
#ifdef LEAN_UI
    /* No scrolling by pixel. */
    lcd_write(col, row, -1, str);
#else
    struct oled_shift *s = &oled_shift[!!(row & LCD_WIDE)][row & ~LCD_WIDE];
    uint32_t oldpri = IRQ_save(I2C_IRQ_PRI);

    s->col = col;
    s->nr = nr;
    s->px = px;
    s->next = px ? str[nr] : ' ';
    lcd_write(col, row, -1, str);
    IRQ_restore(oldpri);
#endif
}

unsigned int lcd_column_pixels(void)
{
    return (oled_font == FONT_8x16) ? 8 : 6;
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

/* The glyph of character @c of the set of charset.h in the OLED fonts,
 * which hold the 31 characters at 0x01-0x1f, ASCII's 95, the 64 Latin cells
 * at 0x80-0xbf, and the Russian letters in the set's order, then a hollow
 * square, the glyph of every code without one. */
static unsigned int oled_font_index(unsigned int c)
{
#ifdef OLED_FONT_ASCII_ONLY
    if ((c >= 0x20) && (c <= 0x7e))
        return c - 0x20;
    return 95;
#else
    if (c == 0x01)
        return 0;
    if ((c >= 0x02) && (c <= 0x1f))
        return c - 0x01;
    if ((c >= 0x20) && (c <= 0x7e))
        return c - 0x20 + 31;
    if ((c >= 0x80) && (c <= 0xbf))
        return c - 0x80 + 31 + 95;
    if (c >= 0xc0)
        return c - 0xc0 + 31 + 95 + 64;
    return 31 + 95 + 64 + 64;
#endif
}

extern const struct oled_font oled_font_6x16;

/* The columns of glyph @g of font @f into @cols, twice the font's width:
 * the top page's, then the bottom page's, bit 0 a column's top pixel. */
static void oled_font_glyph(const struct oled_font *f, unsigned int g,
                            uint8_t *cols)
{
    const uint8_t *p = f->data + f->off[g];
    unsigned int w = f->w, top = *p >> 4, nr = (*p++ & 15) + 1;
    unsigned int x, y, row, bits = 0, nbits = 0;

    memset(cols, 0, 2*w);
    for (y = top; y < top + nr; y++) {
        while (nbits < w) {
            bits = (bits << 8) | *p++;
            nbits += 8;
        }
        nbits -= w;
        row = (bits >> nbits) & ((1u << w) - 1);
        for (x = 0; x < w; x++)
            if (row & (1u << (w - 1 - x)))
                cols[x + (y >> 3) * w] |= 1u << (y & 7);
    }
}

#if TARGET == TARGET_apple2
#include "oled_spinner.c"

/* Draw content @row's phase spinner over its rendering in buffer[], which
 * holds @half of a double-height row, or 0 for normal height. In a @wide row,
 * @stretched selects a two-cell spinner before stretching; otherwise only a
 * one-cell spinner is drawn over the stretched row. */
static void oled_draw_spinner(unsigned int row, unsigned int half,
                              bool_t wide, bool_t stretched)
{
    const struct oled_spinner *s = &oled_spinners[!!wide][row];
    unsigned int w = (oled_font == FONT_8x16) ? 8 : 6;
    unsigned int x = s->col * w + (w == 6);

    if (!s->width || (s->row != row) || (display_mode != DM_normal)
        || (wide && ((s->width == 2) != stretched)))
        return;
    oled_spinner(&buffer[(wide && !stretched) ? 2*x : x],
                 w, s->phases, half);
}
#endif

/* Writes pixel columns @from to @to - 1 of the glyph of @c to buffer[] at @q,
 * its top and bottom pages. */
static void oled_glyph(uint8_t *q, char c, unsigned int from,
                       unsigned int to)
{
    unsigned int g = oled_font_index((uint8_t)c);
    uint8_t glyph[12];

#ifdef font_extra
    if (oled_font == FONT_8x16) {
        oled_glyph_8x16(q, g, from, to);
        return;
    }
#endif
    oled_font_glyph(&oled_font_6x16, g, glyph);
    memcpy(q, glyph+from, to-from);
    memcpy(q+128, glyph+6+from, to-from);
}

/* Renders text row @pc into buffer[], its columns shifted as @s says in the
 * normal display mode. */
static void oled_convert_text_row(const char *pc, const struct oled_shift *s)
{
#ifdef LEAN_UI
    unsigned int i, w = lcd_column_pixels();
#else
    unsigned int i, from, w = lcd_column_pixels();
    unsigned int px = ((display_mode == DM_normal) || error_mode) ? s->px : 0;
#endif
    uint8_t *q = buffer;

    if (w == 6) {
        q[0] = q[128] = 0;
        q++;
    }

#ifdef LEAN_UI
    for (i = 0; i < lcd_columns; i++, q += w)
        oled_glyph(q, pc[i], 0, w);
#else
    for (i = 0; i <= lcd_columns; i++) {
        if (i == s->col + s->nr) {
            oled_glyph(q, s->next, 0, px);
            q += px;
        }
        if (i == lcd_columns)
            break;
        from = (i == s->col) ? px : 0;
        oled_glyph(q, pc[i], from, w);
        q += w - from;
    }
#endif

    /* Fill remainder of buffer[] with zeroes. */
    memset(q, 0, buffer+128-q);
    memset(q+128, 0, buffer+128-q);
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
    /* As oled_to_lcd_row() takes display_order on the normal screen, and on
     * an error screen its double rows by display-error-wide. */
    if (!is_oled_display)
        return lcd_columns;
    if ((display_mode == DM_normal)
        ? (ff_cfg.display_order_wide & (1 << row))
        : (error_mode && ff_cfg.display_error_wide
           && ((oled_height == 64) || (display_mode == DM_error))))
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

    order = glyphs_mode ? 0x3210 : (oled_height == 32) ? 0x7710
        : menu_mode ? 0x7903 : 0x7183;
    if ((ff_cfg.display_order != DORD_default) && (display_mode == DM_normal))
        order = ff_cfg.display_order;
    else if (error_mode)
        order = error_order();

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
        oled_convert_text_row(pc, &oled_shift[!!wide][row]);
#if TARGET == TARGET_apple2
        if (wide)
            oled_draw_spinner(row, 0, TRUE, TRUE);
#endif
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
        oled_draw_spinner(row, half, wide, FALSE);
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

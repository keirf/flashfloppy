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

static bool_t oled_small_font(void)
{
    return (oled_font == FONT_7x8) || (oled_font == FONT_8x8);
}

#ifdef OLED_FONT_CYR_ONLY
#define oled_reduced_font() TRUE
#else
#define oled_reduced_font() oled_small_font()
#endif

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
 * and, when supported, 4, the image's name and track, and 5, directory and
 * track. */
static char text[LCD_TEXT_ROWS][40];

#ifndef MINIMAL_UI
/* The rows' layouts of half width, for a row that display-order draws double
 * width on an OLED, whose columns are 21 at most. */
static char text_wide[ARRAY_SIZE(text)][21];
#endif

#if OLED_SPINNER_ENABLED
/* Each row's Apple2 phase spinner, outside the character encoding. */
static struct oled_spinner {
    uint8_t row, col, width, wide_col, wide_width, phases;
} oled_spinners[ARRAY_SIZE(text)];
#endif

#ifndef MINIMAL_UI
/* Of each text row, in its layouts of normal and of half width, the columns
 * that an OLED shows px pixels to the left in the normal display mode, from
 * col to col + nr - 1, the pixels so freed at their right from next's glyph:
 * see lcd_write_shifted(). */
static struct oled_shift {
    uint8_t col, nr, px, wide_col, wide_nr, wide_px;
    char next, wide_next;
} oled_shift[ARRAY_SIZE(text)];
#endif

/* Columns and rows of text. */
uint8_t lcd_columns, display_rows;

/* Current display mode: Affects row ordering and sizing. */
uint8_t display_mode = DM_banner;
#define normal_mode (display_mode <= DM_selector)
#define menu_mode (display_mode == DM_menu)
#define glyphs_mode (display_mode == DM_glyphs)
#ifndef MINIMAL_UI
#define DISPLAY_ORDER(a,b,c,d) \
    ((a) | ((b) << DORD_shift) | ((c) << (2 * DORD_shift)) | \
    ((d) << (3 * DORD_shift)))
#define error_mode (display_mode >= DM_error)
enum { ERR_NAME, ERR_STATUS, ERR_BLANK = 7 };
#define ERR_TALL(row) ((row) | DORD_double)
#define ERR_ORDER(a,b,c,d) \
    ((a) | ((b) << DORD_shift) | ((c) << (2 * DORD_shift)) | \
    ((d) << (3 * DORD_shift)))

/* An error screen's rows, the message in the status row: below the image's
 * name, or alone below a blank row, double height where the display has
 * room, and alone double height on any OLED. */
static uint16_t error_display_order(void)
{
    bool_t oled_4rows = is_oled_display && (display_rows == 4);

    if (display_mode == DM_error_name)
        return oled_4rows
            ? ERR_ORDER(
                ERR_TALL(ERR_NAME), ERR_TALL(ERR_STATUS), ERR_BLANK, ERR_BLANK)
            : ERR_ORDER(ERR_NAME, ERR_STATUS, ERR_BLANK, ERR_BLANK);
    if (oled_4rows)
        return ERR_ORDER(
            ERR_TALL(ERR_BLANK), ERR_TALL(ERR_STATUS), ERR_BLANK, ERR_BLANK);
    if (is_oled_display)
        return ERR_ORDER(ERR_TALL(ERR_STATUS), ERR_BLANK, ERR_BLANK, ERR_BLANK);
    return ERR_ORDER(ERR_BLANK, ERR_STATUS, ERR_BLANK, ERR_BLANK);
}
#undef ERR_TALL
#undef ERR_ORDER
#endif

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
#ifndef MINIMAL_UI
    memset(text_wide, ' ', sizeof(text_wide));
#endif
#if OLED_SPINNER_ENABLED
    memset(oled_spinners, 0, sizeof(oled_spinners));
#endif
#ifndef MINIMAL_UI
    memset(oled_shift, 0, sizeof(oled_shift));
#endif
}

#if OLED_SPINNER_ENABLED
void lcd_write_spinner(int row, unsigned int col, unsigned int width,
                       unsigned int phases)
{
    bool_t wide = row & LCD_WIDE;
    struct oled_spinner *s = &oled_spinners[row & ~LCD_WIDE];
    uint32_t oldpri = IRQ_save(I2C_IRQ_PRI);

    if (wide) {
        s->wide_col = col;
        s->wide_width = width;
    } else {
        s->row = row;
        s->col = col;
        s->width = width;
    }
    s->phases = phases;
    IRQ_restore(oldpri);
}
#endif

void lcd_write(int col, int row, int min, const char *str)
{
    char c, *p;
    uint32_t oldpri;
    int columns = lcd_columns;

#ifdef MINIMAL_UI
    if (row & LCD_WIDE)
        return;
    p = &text[row][col];
#else
    if (row & LCD_WIDE) {
        columns /= 2;
        p = &text_wide[row & ~LCD_WIDE][col];
    } else {
        p = &text[row][col];
    }
#endif

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

#ifndef MINIMAL_UI
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
    return (is_oled_display || i2c_addr) ? display_rows : 2;
}

void lcd_write_shifted(int col, int row, int nr, unsigned int px,
                       const char *str)
{
    bool_t wide = row & LCD_WIDE;
    struct oled_shift *s = &oled_shift[row & ~LCD_WIDE];
    uint32_t oldpri = IRQ_save(I2C_IRQ_PRI);

    if (wide) {
        s->wide_col = col;
        s->wide_nr = nr;
        s->wide_px = px;
        s->wide_next = px ? str[nr] : ' ';
    } else {
        s->col = col;
        s->nr = nr;
        s->px = px;
        s->next = px ? str[nr] : ' ';
    }
    lcd_write(col, row, -1, str);
    IRQ_restore(oldpri);
}

unsigned int lcd_column_pixels(void)
{
    if (oled_font == FONT_6x16)
        return 6;
    return (oled_font == FONT_7x8) ? 7 : 8;
}
#endif

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

/* Maps a combined content row to its plain form when the combined form is
 * unsupported. */
static unsigned int map_content_row(unsigned int row)
{
#ifdef MINIMAL_UI
    if (row == 4)
        row = 0;
    else if (row == 5)
        row = 3;
#endif
    return row;
}

/* Copy a text row for OSD, replacing the OLED spinner's cells by the phase
 * digit in the same position as on an LCD. */
static void osd_copy_text_row(uint8_t *q, unsigned int row)
{
#if OLED_SPINNER_ENABLED
    const struct oled_spinner *s = &oled_spinners[row];
    unsigned int col = s->col;
#endif

    memcpy(q, text[row], lcd_columns);
#if OLED_SPINNER_ENABLED
    if (!s->width || (s->row != row) || (display_mode != DM_normal))
        return;
    if (s->width == 2) {
        if (ff_cfg.show_tracks_short || (row == 4) || (row == 5))
            col++;
        else
            q[col++] = '.';
    }
    q[col] = "0123456789ABCDEF"[s->phases & 0xf];
#endif
}

/* The glyph of character @c in the active font. Reduced fonts hold ASCII,
 * the Russian letters, then their fallback. Small fonts map accented Latin to
 * ASCII; a Cyrillic-only build shows the fallback for it. */
static unsigned int oled_font_index(unsigned int c)
{
#ifdef OLED_FONT_ASCII_ONLY
    if ((c >= 0x20) && (c <= 0x7e))
        return c - 0x20;
    return 95;
#else
    if (oled_reduced_font()) {
#ifndef OLED_FONT_CYR_ONLY
        if (c == 0x01)
            c = '$';
        else if ((c < 0x20) || ((c >= 0x80) && (c < 0xc0)))
            c = charset_to_rom(c, CHARSET_osd);
#endif
        if ((c >= 0x20) && (c <= 0x7e))
            return c - 0x20;
        if (c >= 0xc0)
            return c - 0xc0 + 95;
        return 95 + 64;
    }
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
#ifdef font_extra
extern const struct oled_font oled_font_8x16;
extern const struct oled_font oled_font_7x8;
extern const struct oled_font oled_font_8x8;
#endif

#ifndef MINIMAL_UI
static const struct oled_font *oled_active_font(void)
{
#ifdef font_extra
    if (oled_font == FONT_8x16)
        return &oled_font_8x16;
    if (oled_font == FONT_7x8)
        return &oled_font_7x8;
    if (oled_font == FONT_8x8)
        return &oled_font_8x8;
#endif
    return &oled_font_6x16;
}
#endif

/* The columns of glyph @g of font @f into @cols, twice the font's width:
 * the top page's, then the bottom page's, bit 0 a column's top pixel. */
static void oled_font_glyph(const struct oled_font *f, unsigned int g,
                            uint8_t *cols)
{
    const uint8_t *p = f->data, *off = f->off + (g >> 1) * 3;
    const uint8_t *end = p + ((g & 1) ? (off[1] >> 4 | off[2] << 4)
        : (off[0] | (off[1] & 15) << 8));
    uint8_t raw[17];
    static uint8_t history[512];
    unsigned int h = 0, out = 0, expected = 0, w = f->w;
    unsigned int x, y, row, bits = 0, nbits = 0, top, nr;
    bool_t capture = FALSE;

    /* Rebuild the 512-byte history, then this glyph's packed bytes. */
    for (;;) {
        unsigned int control, len, distance = 0;
        if (p == end)
            capture = TRUE;
        control = *p++;
        if (control & 0x80) {
            len = ((control >> 1) & 0x3f) + 3;
            distance = ((control & 1) << 8 | *p++) + 1;
        } else {
            len = control + 1;
        }
        while (len--) {
            uint8_t value = distance ? history[(h - distance) & 0x1ff] : *p++;
            history[h & 0x1ff] = value;
            h++;
            if (capture) {
                raw[out++] = value;
                if (out == 1)
                    expected = 1 + ((((raw[0] & 15) + 1) * w + 7) >> 3);
                if (out == expected)
                    goto unpack_glyph;
            }
        }
    }

unpack_glyph:
    p = raw;
    top = *p >> 4;
    nr = (*p++ & 15) + 1;
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

#if OLED_SPINNER_ENABLED
#include "oled_spinner.c"

/* Draw content @row's phase spinner over its rendering in buffer[], which
 * holds @half of a double-height row, or 0 for normal height. In a @wide row,
 * @stretched selects a two-cell spinner before stretching; otherwise only a
 * one-cell spinner is drawn over the stretched row. */
static void oled_draw_spinner(unsigned int row, unsigned int half,
                              bool_t wide, bool_t stretched)
{
    const struct oled_spinner *s = &oled_spinners[row];
    unsigned int w = lcd_column_pixels();
    unsigned int col = wide ? s->wide_col : s->col;
    unsigned int width = wide ? s->wide_width : s->width;
    unsigned int x = col * w + (w == 6);
    bool_t small = oled_small_font();

    if (!width || (s->row != row) || (display_mode != DM_normal)
        || (wide && ((width == 2) != stretched)))
        return;
    oled_spinner(&buffer[(wide && !stretched) ? 2*x : x], w, s->phases, half,
                 small, small && half && !stretched);
}
#endif

#ifdef MINIMAL_UI
static void oled_convert_text_row_6x16(char *pc)
{
    unsigned int i;
    uint8_t *q = buffer;
    const unsigned int w = 6;

    q[0] = q[128] = 0;
    q++;

    for (i = 0; i < lcd_columns; i++) {
        uint8_t glyph[12];
        oled_font_glyph(
            &oled_font_6x16, oled_font_index((uint8_t)*pc++), glyph);
        memcpy(q, glyph, w);
        memcpy(q+128, glyph+w, w);
        q += w;
    }

    /* Fill remainder of buffer[] with zeroes. */
    memset(q, 0, 127-lcd_columns*w);
    memset(q+128, 0, 127-lcd_columns*w);
}

static void oled_convert_text_row(char *pc)
{
    oled_convert_text_row_6x16(pc);
}
#else
/* Writes pixel columns @from to @to - 1 of the glyph of @c to buffer[] at @q,
 * its top page and, for a 16-row font, its bottom page. */
static void oled_glyph(uint8_t *q, char c, unsigned int from,
                       unsigned int to)
{
    const struct oled_font *f = oled_active_font();
    unsigned int g = oled_font_index((uint8_t)c);
    uint8_t glyph[16];

    oled_font_glyph(f, g, glyph);
    memcpy(q, glyph+from, to-from);
    if (f->h == 16)
        memcpy(q+128, glyph+f->w+from, to-from);
}

/* Renders text row @pc into buffer[], its columns shifted as @s says in the
 * normal or selector display mode. */
static void oled_convert_text_row(const char *pc, const struct oled_shift *s,
                                  bool_t wide)
{
    unsigned int i, from, w = lcd_column_pixels();
    unsigned int col = wide ? s->wide_col : s->col;
    unsigned int nr = wide ? s->wide_nr : s->nr;
    unsigned int px = (normal_mode || error_mode)
        ? (wide ? s->wide_px : s->px) : 0;
    char next = wide ? s->wide_next : s->next;
    uint8_t *q = buffer;

    if (w == 6) {
        q[0] = q[128] = 0;
        q++;
    }

    for (i = 0; i <= lcd_columns; i++) {
        if (i == col + nr) {
            oled_glyph(q, next, 0, px);
            q += px;
        }
        if (i == lcd_columns)
            break;
        from = (i == col) ? px : 0;
        oled_glyph(q, pc[i], from, w);
        q += w - from;
    }

    /* Fill remainder of buffer[] with zeroes. */
    memset(q, 0, buffer+128-q);
    memset(q+128, 0, buffer+128-q);
}
#endif

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

#ifndef MINIMAL_UI
/* Stretches the left half of the text row in buffer[] to its full width. */
static void oled_double_width(void)
{
    int x;

    /* Each page from its right, so that no column is written before it is
     * read. */
    for (x = 255; x >= 0; x--)
        buffer[x] = buffer[(x & 128) | ((x & 127) >> 1)];
}
#endif

unsigned int lcd_row_columns(unsigned int row)
{
    /* As oled_to_lcd_row() takes display_order on the normal screen, and on
     * an error screen its double rows by display-error-wide. */
#ifdef MINIMAL_UI
    /* Only double height in minimal builds. */
    return lcd_columns;
#else
    if (!is_oled_display)
        return lcd_columns;
    if (normal_mode
        ? (lcd_display_order_wide() & (1 << row))
        : (error_mode && ff_cfg.display_error_wide
           && ((display_rows == 4) || (display_mode == DM_error))))
        return lcd_columns / 2;
    return lcd_columns;
#endif
}

/* Renders text-row unit @in_row into buffer[]: 8 pixels for a small font on
 * 128x32, else 16. A small font's rows are always double height on 128x64. */
static void oled_to_lcd_row(int in_row)
{
    uint16_t order, configured = lcd_display_order();
    int i = 0, row, half;
    bool_t large = FALSE;
    bool_t small = oled_small_font();
    bool_t compact = small && (oled_height == 32);
    bool_t auto_large = small && (oled_height == 64);
#ifndef MINIMAL_UI
    bool_t wide;
#endif
    char *pc;

    order = (oled_height == 32) ? 0x7710 : menu_mode ? 0x7903 : 0x7183;
    if ((configured != DORD_default) && normal_mode)
        order = configured;
#ifndef MINIMAL_UI
    else if (error_mode)
        order = error_display_order();
    else if (glyphs_mode)
        order = DISPLAY_ORDER(0, 1, 2, 3);
#endif

    for (;;) {
        large = auto_large || !!(order & DORD_double);
        i += auto_large ? 1 : large ? 2 : 1;
        if (i > in_row)
            break;
        order >>= DORD_shift;
    }

    /* Remap the row */
    row = map_content_row(order & DORD_row);
#ifndef MINIMAL_UI
    wide = large && (lcd_row_columns(row) < lcd_columns);
#endif
    pc = (row >= ARRAY_SIZE(text)) ? NULL : text[row];
#ifndef MINIMAL_UI
    if (wide && (row < ARRAY_SIZE(text)))
        pc = text_wide[row];
#endif
    if (pc != NULL) {
#ifdef MINIMAL_UI
        oled_convert_text_row(pc);
#else
        oled_convert_text_row(pc, &oled_shift[row], wide);
#endif
#if OLED_SPINNER_ENABLED
        if (wide)
            oled_draw_spinner(row, /*half=*/0, /*wide=*/TRUE, /*stretched=*/TRUE);
#endif
#ifndef MINIMAL_UI
        if (wide)
            oled_double_width();
#endif
    } else {
        memset(buffer, 0, 256);
    }

    half = large ? auto_large ? 2 : i - in_row : 0;
    if (large) {
        if (small)
            oled_double_height(buffer, buffer, 0x3);
        else
            oled_double_height(buffer, &buffer[(half == 1) ? 128 : 0], 0x3);
    }

#if OLED_SPINNER_ENABLED
    if (pc != NULL)
        oled_draw_spinner(row, half, wide, /*stretched=*/FALSE);
#endif
    if (compact && large && (half == 1))
        memcpy(buffer, buffer+128, 128);
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

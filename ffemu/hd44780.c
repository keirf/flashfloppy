/*
 * hd44780.c
 *
 * Model of a character LCD on the I2C bus: an HD44780 controller behind a
 * PCF8574 port expander, the usual backpack, whose pins carry RS, R/W, E and
 * the backlight in the low nibble and D4 to D7 in the high one. It decodes
 * the 4-bit protocol that the firmware clocks through the expander, keeps
 * the display RAM for the user interface, and draws the characters from the
 * font of the character ROM chosen, lcd_font_cyr.h or lcd_font_a00.h, which
 * also give the Unicode character of each code. The user-defined characters
 * of CGRAM, which the firmware never sets, are not kept.
 *
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

#include <assert.h>
#include <string.h>

#include "host.h"
#include "lcd_font_a00.h"
#include "lcd_font_cyr.h"

/* A PCF8574 with its address pins left high, as backpacks come; the
 * firmware would find a PCF8574A, at 0x3f, as well. */
#define I2C_ADDR 0x27

#define PIN_RS 0x01
#define PIN_RW 0x02
#define PIN_E 0x04
#define PIN_BL 0x08

/* A font's glyphs: five dots wide and eight tall, eight to a band, a '|'
 * between them in its rows, 'X' a dot. */
#define FONT_W 5
#define FONT_H 8
#define FONT_BAND 8
#define FONT_STRIDE (FONT_W + 1)
#define FONT_INK 'X'

/* The dots of a character cell and its pitch, a dot apart, and the margin
 * to the left of the first cell. */
#define PITCH_X (FONT_W + 1)
#define PITCH_Y (FONT_H + 1)
#define MARGIN_X 1

/* The height of @nr_rows rows of characters, from the top of the first to
 * the bottom of the last, whose cursor row is left out; and of the panel:
 * that with two dots or more above and below, rounded up to the four dots
 * of a braille cell, so that every rendering shows the same margins. */
#define INK_H(nr_rows) ((nr_rows) * PITCH_Y - 2)
#define PANEL_H(nr_rows) ((INK_H(nr_rows) + 4 + 3) & ~3u)

const char * const lcd_rom_name[LCD_ROM_nr] = {
    [LCD_ROM_cyr] = "cyr",
    [LCD_ROM_a00] = "a00"
};

const char * const lcd_rom_label[LCD_ROM_nr] = {
    [LCD_ROM_cyr] = "CYR: Latin and Cyrillic",
    [LCD_ROM_a00] = "A00: Latin and Japanese"
};

const char * const lcd_rom_help[LCD_ROM_nr] = {
    [LCD_ROM_cyr] = "Upper- and lower-case Cyrillic: ffemu/lcd_font_cyr.h",
    [LCD_ROM_a00] = "Latin and Japanese katakana: ffemu/lcd_font_a00.h"
};

static const struct {
    const char * const *rows;    /* the glyphs, in bands */
    const unsigned int *unicode; /* the character of each code */
    unsigned int first, nr;      /* the codes that it has */
    unsigned int nr_rows;
} fonts[LCD_ROM_nr] = {
    [LCD_ROM_cyr] = { lcd_font_cyr, lcd_font_cyr_unicode, LCD_FONT_CYR_FIRST,
                      ARRAY_SIZE(lcd_font_cyr_unicode),
                      ARRAY_SIZE(lcd_font_cyr) },
    [LCD_ROM_a00] = { lcd_font_a00, lcd_font_a00_unicode, LCD_FONT_A00_FIRST,
                      ARRAY_SIZE(lcd_font_a00_unicode),
                      ARRAY_SIZE(lcd_font_a00) }
};

/* The character ROM, set by the user interface. */
static int rom;

/* Written by the bus thread only. The user interface reads it without a
 * lock: @seq is odd while an update is in progress. */
static struct {
    volatile unsigned int seq;
    uint8_t ddram[128];
    bool on, backlight, two_lines, cursor, blink;
    unsigned int shift; /* display shift: the address shown first, 0-39 */
    unsigned int nr_updates;
} d;

static bool fitted;
static unsigned int cols = 16, rows = 2;

/* I2C transaction state. */
static bool selected, wrote;

/* The expander's pins, high from power-on, and the controller's bus: 8 bits
 * wide from reset, 4 once set so, the high nibble first. */
static uint8_t pins = 0xff, high;
static bool four_bit, low_next;

/* The address counter, into DDRAM or CGRAM, and the entry mode. */
static uint8_t ac;
static bool in_cgram, increment = true, shift_on_write;

static void update_begin(void)
{
    __sync_fetch_and_add(&d.seq, 1);
}

static void update_end(void)
{
    __sync_fetch_and_add(&d.seq, 1);
}

/* Moves the address counter one up or down: in two-line mode the lines are
 * 0x00-0x27 and 0x40-0x67, the one following the other. */
static void ac_step(bool up)
{
    if (in_cgram)
        ac = (ac + (up ? 1 : 63)) & 63;
    else if (!d.two_lines)
        ac = up ? ((ac >= 0x4f) ? 0 : ac + 1) : ((ac == 0) ? 0x4f : ac - 1);
    else if (up)
        ac = (ac == 0x27) ? 0x40 : (ac == 0x67) ? 0x00 : (ac + 1) & 0x7f;
    else
        ac = (ac == 0x40) ? 0x27 : (ac == 0x00) ? 0x67 : ac - 1;
}

static void shift_display(bool left)
{
    d.shift = (d.shift + (left ? 1 : 39)) % 40;
}

static void execute(bool rs, uint8_t b)
{
    if (rs) {
        if (!in_cgram) {
            d.ddram[ac] = b;
            wrote = true;
        }
        ac_step(increment);
        if (shift_on_write && !in_cgram)
            shift_display(increment);
        return;
    }

    if (b & 0x80) {
        ac = b & 0x7f;
        in_cgram = false;
    } else if (b & 0x40) {
        ac = b & 0x3f;
        in_cgram = true;
    } else if (b & 0x20) {
        four_bit = !(b & 0x10);
        low_next = false;
        d.two_lines = !!(b & 0x08);
    } else if (b & 0x10) {
        if (b & 0x08)
            shift_display(!(b & 0x04));
        else
            ac_step(!!(b & 0x04));
    } else if (b & 0x08) {
        d.on = !!(b & 0x04);
        d.cursor = !!(b & 0x02);
        d.blink = !!(b & 0x01);
    } else if (b & 0x04) {
        increment = !!(b & 0x02);
        shift_on_write = !!(b & 0x01);
    } else if (b & 0x02) {
        ac = 0;
        in_cgram = false;
        d.shift = 0;
    } else if (b & 0x01) {
        memset(d.ddram, ' ', sizeof(d.ddram));
        ac = 0;
        in_cgram = false;
        d.shift = 0;
        increment = true;
        wrote = true;
    }
}

/* A bus cycle, on the falling edge of E, with the pins @p as they were
 * while E was high. A read cycle, which the firmware never makes, only
 * counts as a nibble. */
static void bus_cycle(uint8_t p)
{
    uint8_t nibble = p >> 4;

    if (!four_bit) {
        /* D0 to D3 are not wired, and read high through their pull-ups. */
        if (!(p & PIN_RW))
            execute(p & PIN_RS, (nibble << 4) | 0x0f);
        return;
    }
    if (!low_next) {
        high = nibble;
        low_next = true;
        return;
    }
    low_next = false;
    if (!(p & PIN_RW))
        execute(p & PIN_RS, (high << 4) | nibble);
}

int hd44780_i2c_start(unsigned int addr, int rd)
{
    selected = fitted && (addr == I2C_ADDR);
    return selected;
}

void hd44780_i2c_write(uint8_t b)
{
    if (!selected)
        return;
    update_begin();
    d.backlight = !!(b & PIN_BL);
    if ((pins & PIN_E) && !(b & PIN_E))
        bus_cycle(pins);
    pins = b;
    update_end();
}

uint8_t hd44780_i2c_read(void)
{
    /* The expander's pins, which no one else drives. */
    return pins;
}

void hd44780_i2c_stop(void)
{
    if (selected && wrote) {
        update_begin();
        d.nr_updates++;
        update_end();
    }
    selected = wrote = false;
}

void hd44780_init(int display)
{
    unsigned int i, j, k;

    /* Each font a band of rows per eight codes, each row eight glyphs and
     * the '|' between them. */
    for (i = 0; i < LCD_ROM_nr; i++) {
        assert(fonts[i].nr_rows == fonts[i].nr / FONT_BAND * FONT_H);
        for (j = 0; j < fonts[i].nr_rows; j++) {
            assert(strlen(fonts[i].rows[j]) == FONT_BAND * FONT_STRIDE - 1);
            for (k = FONT_W; k < FONT_BAND * FONT_STRIDE - 1; k += FONT_STRIDE)
                assert(fonts[i].rows[j][k] == '|');
        }
    }
    fitted = true;
    cols = DISP_LCD_COLS(display);
    rows = DISP_LCD_ROWS(display);
    /* The controller's reset clears the display RAM, and leaves the display
     * off; the expander's pins come up high, the backlight with them. */
    memset(d.ddram, ' ', sizeof(d.ddram));
    d.backlight = true;
}

void hd44780_get_view(struct lcd_view *view)
{
    static typeof(d) snap;
    unsigned int seq, r, c;

    do {
        seq = d.seq;
        __sync_synchronize();
        memcpy(&snap, &d, sizeof(snap));
        __sync_synchronize();
    } while ((seq & 1) || (seq != d.seq));

    view->present = fitted;
    view->on = snap.on;
    view->backlight = snap.backlight;
    view->cols = cols;
    view->rows = rows;
    view->rom = rom;
    view->nr_updates = snap.nr_updates;

    /* A 4-row module is a two-line one inside, its third and fourth rows the
     * second halves of the lines; in one-line mode only the first line is
     * driven. */
    memset(view->text, ' ', sizeof(view->text));
    for (r = 0; r < rows; r++) {
        for (c = 0; c < cols; c++) {
            unsigned int pos = (r >> 1) * cols + c;
            if (snap.two_lines)
                view->text[r][c] = snap.ddram[((r & 1) ? 0x40 : 0)
                                              + (pos + snap.shift) % 40];
            else if (!(r & 1))
                view->text[r][c] = snap.ddram[(pos + snap.shift) % 80];
        }
    }
}

void hd44780_set_rom(int r)
{
    rom = r;
}

unsigned int hd44780_unicode(int r, uint8_t c)
{
    unsigned int i = c - fonts[r].first;

    return (i < fonts[r].nr) ? fonts[r].unicode[i] : 0;
}

void hd44780_size(unsigned int nr_cols, unsigned int nr_rows,
                  unsigned int *w, unsigned int *h)
{
    /* A dot more at the right, for braille's two-dot cells. */
    *w = MARGIN_X + nr_cols * PITCH_X + 1;
    *h = PANEL_H(nr_rows);
}

void hd44780_pixels(const struct lcd_view *v, const uint8_t **px,
                    unsigned int *w, unsigned int *h)
{
    /* What a character without a glyph here shows: a checkerboard. */
    static const char * const stub[FONT_H] = {
        "X.X.X", ".X.X.", "X.X.X", ".X.X.", "X.X.X", ".X.X.", "X.X.X", "....."
    };
    static uint8_t buf[(MARGIN_X + LCD_MAX_COLS * PITCH_X + 1)
                       * PANEL_H(LCD_MAX_ROWS)];
    unsigned int r, c, x, y, top;

    hd44780_size(v->cols, v->rows, w, h);
    top = (*h - INK_H(v->rows)) / 2;
    memset(buf, 0, *w * *h);
    *px = buf;
    if (!v->on)
        return;
    for (r = 0; r < v->rows; r++) {
        for (c = 0; c < v->cols; c++) {
            unsigned int i = v->text[r][c] - fonts[v->rom].first;
            for (y = 0; y < FONT_H; y++) {
                const char *row = (i < fonts[v->rom].nr)
                    ? fonts[v->rom].rows[(i / FONT_BAND) * FONT_H + y]
                    + (i % FONT_BAND) * FONT_STRIDE
                    : stub[y];
                for (x = 0; x < FONT_W; x++)
                    if (row[x] == FONT_INK)
                        buf[(top + r * PITCH_Y + y) * *w
                            + MARGIN_X + c * PITCH_X + x] = 1;
            }
        }
    }
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

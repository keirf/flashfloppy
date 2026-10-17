/*
 * led_7seg.c
 *
 * Model of the 7-segment LED display on the two lines DAT and CLK: three
 * digits on a TM1651 controller, which takes bytes on a two-wire protocol of
 * its own and answers each with an ACK, or two digits on a pair of 74HC164
 * shift registers. It decodes what the firmware clocks out, keeps the
 * segments for the user interface, and draws them as pixels from the
 * fonts of led_7seg_font.h.
 *
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "host.h"
#include "led_7seg_font.h"

const char ascii_pixel = ASCII_PIXEL;

/* Written by the firmware thread only. The user interface reads it without a
 * lock: @seq is odd while an update is in progress. */
static struct {
    volatile unsigned int seq;
    uint8_t seg[3];
    bool on;
    unsigned int brightness;
    unsigned int nr_updates;
} d;

static bool fitted;
static unsigned int nr_digits;

/* The lines as last seen, for telling edges. */
static bool clk = true, dat = true;

/* TM1651: a transaction runs from START to STOP, with bytes LSB first, each
 * followed by the ACK that this device gives while CLK is low. The first
 * byte is a command: data (0x40), address (0xc0 plus the digit, the data
 * bytes following it), or display control (0x80, with on in bit 3 and the
 * brightness below it). */
static bool in_tx, ack, writing, dirty;
static unsigned int nr_bits, nr_bytes, addr;
static uint8_t byte;

/* 74HC164: the bits as clocked in, and the ones shown. */
static uint16_t shift, shown;
static uint64_t last_edge_ns;

static void update_begin(void)
{
    d.seq++;
    __sync_synchronize();
}

static void update_end(void)
{
    __sync_synchronize();
    d.seq++;
}

static void tm1651_byte(uint8_t b)
{
    if (nr_bytes++ == 0) {
        switch (b & 0xc0) {
        case 0x40:
            writing = false;
            break;
        case 0xc0:
            writing = true;
            addr = b & 3;
            break;
        case 0x80:
            update_begin();
            d.on = !!(b & 8);
            d.brightness = b & 7;
            d.nr_updates++;
            update_end();
            break;
        }
        return;
    }
    if (writing && (addr < ARRAY_SIZE(d.seg))) {
        update_begin();
        /* Seven segment outputs: the display's decimal points stay dark. */
        d.seg[addr] = b & 0x7f;
        update_end();
        dirty = true;
    }
    addr++;
}

int emu_led_sync(int clk_now, int dat_now)
{
    bool c = clk_now, da = dat_now;
    bool clk_rise = c && !clk, clk_fall = !c && clk;
    uint64_t now;

    if (!fitted) {
        clk = c;
        dat = da;
        return 0;
    }

    if (nr_digits == 3) {
        if (c && clk) {
            /* DAT moves while CLK is high only for START and STOP. */
            if (dat && !da) {
                in_tx = true;
                ack = false;
                nr_bits = nr_bytes = 0;
                byte = 0;
            } else if (!dat && da && in_tx) {
                in_tx = false;
                ack = false;
                if (dirty) {
                    update_begin();
                    d.nr_updates++;
                    update_end();
                    dirty = false;
                }
            }
        } else if (in_tx && clk_rise) {
            if (ack) {
                /* The ninth clock ends the ACK. */
                ack = false;
                nr_bits = 0;
            } else {
                byte |= da << nr_bits;
                nr_bits++;
            }
        } else if (in_tx && clk_fall && (nr_bits == 8) && !ack) {
            ack = true;
            tm1651_byte(byte);
            byte = 0;
        }
        clk = c;
        dat = da;
        return ack;
    }

    /* A bit on each rising edge of CLK, MSB first, 16 for the two digits.
     * The registers show whatever they hold, so they are read once CLK has
     * rested, after a whole frame. */
    now = emu_time_ns();
    if (clk_rise) {
        shift = (shift << 1) | da;
        last_edge_ns = now;
    } else if ((now - last_edge_ns > 50000) && (shift != shown)) {
        shown = shift;
        update_begin();
        d.seg[0] = shift >> 8;
        d.seg[1] = shift;
        d.nr_updates++;
        update_end();
    }
    clk = c;
    dat = da;
    return 0;
}

void led_init(int display)
{
    fitted = true;
    nr_digits = DISP_DIGITS(display);
    /* The shift registers have no switch: their digits are what they hold. */
    d.on = (nr_digits == 2);
}

void led_get_view(struct led_view *view)
{
    static typeof(d) snap;
    unsigned int seq;

    do {
        seq = d.seq;
        __sync_synchronize();
        memcpy(&snap, &d, sizeof(snap));
        __sync_synchronize();
    } while ((seq & 1) || (seq != d.seq));

    view->present = fitted;
    view->on = snap.on;
    view->nr_digits = nr_digits;
    view->brightness = snap.brightness;
    view->nr_updates = snap.nr_updates;
    memcpy(view->seg, snap.seg, sizeof(view->seg));
}

static const struct {
    const char * const *rows;
    unsigned int nr;
} fonts[] = {
    [LED_FONT_ascii] = { led_font_ascii, ARRAY_SIZE(led_font_ascii) },
    [LED_FONT_half] = { led_font_half, ARRAY_SIZE(led_font_half) },
    [LED_FONT_braille] = { led_font_braille, ARRAY_SIZE(led_font_braille) }
};

/* The rows of font @font, LED_FONT_*, with its width, the pitch of the
 * digits, and its height; the rows must all be as long as the first. */
static const char * const *font_rows(unsigned int font, unsigned int *w,
                                     unsigned int *h)
{
    unsigned int y;

    *h = fonts[font].nr;
    *w = strlen(fonts[font].rows[0]);
    for (y = 1; y < *h; y++)
        assert(strlen(fonts[font].rows[y]) == *w);
    return fonts[font].rows;
}

/* The pixel buffers, allotted on first use for the largest font and all
 * the digits. */
static uint8_t *lit_px, *all_px;

static void allot_pixels(void)
{
    unsigned int f, w, h, n, max = 0;

    if (lit_px != NULL)
        return;
    for (f = 0; f < ARRAY_SIZE(fonts); f++) {
        font_rows(f, &w, &h);
        n = w * h * ARRAY_SIZE(d.seg);
        if (n > max)
            max = n;
    }
    lit_px = malloc(max);
    all_px = malloc(max);
    assert((lit_px != NULL) && (all_px != NULL));
}

void led_size(unsigned int nr_digits, unsigned int font, unsigned int *w,
              unsigned int *h)
{
    font_rows(font, w, h);
    *w *= nr_digits;
}

void led_pixels(const struct led_view *v, unsigned int font,
                const uint8_t **lit, const uint8_t **all, unsigned int *w,
                unsigned int *h)
{
    const char * const *rows = font_rows(font, w, h);
    unsigned int pitch = *w, i, x, y;

    allot_pixels();
    *lit = lit_px;
    *all = all_px;
    *w *= v->nr_digits;
    memset(lit_px, 0, *w * *h);
    memset(all_px, 0, *w * *h);
    for (i = 0; i < v->nr_digits; i++) {
        for (y = 0; y < *h; y++) {
            for (x = 0; x < pitch; x++) {
                char c = rows[y][x];
                unsigned int p = y * *w + i * pitch + x;
                if (c == '.')
                    continue;
                all_px[p] = 1;
                if (v->on && (v->seg[i] & (1u << (c - '0'))))
                    lit_px[p] = 1;
            }
        }
    }
}

void led_text(const struct led_view *v, char *buf, size_t size)
{
    /* The firmware's patterns, src/display/led_7seg.c: the digits, then the
     * letters a to z. */
    static const uint8_t digits[] = {
        0x3f, 0x06, 0x5b, 0x4f, 0x66, 0x6d, 0x7d, 0x07, 0x7f, 0x6f
    };
    static const uint8_t letters[] = {
        0x77, 0x7c, 0x58, 0x5e, 0x79, 0x71, 0x6f, 0x74, 0x04,
        0x0e, 0x08, 0x38, 0x40, 0x54, 0x5c, 0x73, 0x67, 0x50,
        0x6d, 0x78, 0x1c, 0x09, 0x41, 0x76, 0x6e, 0x52
    };
    unsigned int i, j, n = 0;

    for (i = 0; (i < v->nr_digits) && (n + 7 < size); i++) {
        uint8_t s = v->on ? v->seg[i] & 0x7f : 0;
        char c = '?';
        if (s == 0)
            c = ' ';
        for (j = 0; j < ARRAY_SIZE(digits); j++)
            if (digits[j] == s)
                c = '0' + j;
        for (j = 0; j < ARRAY_SIZE(letters); j++)
            if (letters[j] == s)
                c = 'a' + j;
        buf[n++] = c;
        if (v->on && (v->seg[i] & 0x80))
            buf[n++] = '.';
    }
    buf[n] = '\0';
}

void led_art(const struct led_view *v, char art[3][16])
{
    /* Where each segment, `a` to `g` and the point, is drawn in a digit's
     * cells, three rows of four columns. */
    static const struct { uint8_t row, col; char c; } cell[8] = {
        { 0, 1, '_' }, { 1, 2, '|' }, { 2, 2, '|' }, { 2, 1, '_' },
        { 2, 0, '|' }, { 1, 0, '|' }, { 1, 1, '_' }, { 2, 3, '.' }
    };
    unsigned int i, j, w = 4 * v->nr_digits;

    for (j = 0; j < 3; j++) {
        memset(art[j], ' ', w);
        art[j][w] = '\0';
    }
    for (i = 0; i < v->nr_digits; i++)
        for (j = 0; j < 8; j++)
            if (v->on && (v->seg[i] & (1u << j)))
                art[cell[j].row][4*i + cell[j].col] = cell[j].c;
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

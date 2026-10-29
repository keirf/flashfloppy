/*
 * oled_spinner.c
 *
 * The stepper phase spinner of the Apple2 mode on the OLED, included by
 * lcd_common.c, which draws it over a rendered text row for a character
 * OLED_SPINNER plus the phases on.
 *
 * Written & released by Mike Shevchenko <mike.shevchenko@gmail.com>
 *
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

/* A square over two cells whose sides are the phases, 0 the bottom, 1 the
 * left, 2 the top and 3 the right. The squares, one per font, are
 * symmetric, so only their top right quarters are kept, the middle row and
 * column included: a row of pixels per line, top down, 1 for a pixel. */
static const uint8_t oled_spinner_11[] = {
    0b111100,
    0b111100,
    0b000011,
    0b000011,
    0b000011,
    0b000011,
};
#ifdef font_extra
static const uint8_t oled_spinner_14[] = {
    0b1100000,
    0b1111000,
    0b0110000,
    0b0000010,
    0b0000110,
    0b0000111,
    0b0000011,
};
#endif

/* Draws the spinner for @phases into the two cells of @w columns at @q, the
 * square of the font's cells: a pixel of margin above it in the 16 pixel
 * rows of a row of normal height, or, for @half 2 or 1, as large in the
 * middle of a double-height row, of which buffer[] holds the top or bottom
 * half. A pixel belongs to the side whose triangle, cut by the diagonals,
 * holds it. */
static void oled_spinner(uint8_t *q, unsigned int w, unsigned int phases,
                         unsigned int half)
{
    const uint8_t *glyph = oled_spinner_11;
    unsigned int i, x, y, side, n = 11;
    uint32_t out;

#ifdef font_extra
    if (w == 8) {
        glyph = oled_spinner_14;
        n = 14;
    }
#endif

    /* Over both cells: a column outside the glyph, whose x is out of range
     * as an unsigned, comes out blank. */
    for (i = 0; i < 2*w; i++) {
        x = i - (2*w - n) / 2;
        out = 0;
        for (y = 0; (x < n) && (y < n); y++) {
            if (!(glyph[min(y, n-1-y)] & (1u << min(x, n-1-x))))
                continue;
            side = (y < x) ? ((y < n-1-x) ? 2 : 3) : ((y > n-1-x) ? 0 : 1);
            if (phases & (1u<<side))
                out |= 1u<<y;
        }
        out <<= 1;
        if (half == 2)
            out <<= 8;
        else if (half == 1)
            out >>= 8;
        q[i] = out;
        q[128+i] = out >> 8;
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

/*
 * osd.c
 *
 * FF OSD on the I2C bus, beside an OLED or an LCD: the frames of text that
 * the firmware writes to it, taken as FF OSD's own firmware takes them, and
 * what it reads back, FF OSD's version and buttons.
 *
 * Written & released by Mike Shevchenko <mike.shevchenko@gmail.com>
 *
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

#include <string.h>

#include "host.h"
#include "osd_font.h"

#define I2C_ADDR 0x10

/* The commands of a frame: the backlight, 0 off and 1 on, and 2 for the
 * text that follows, row after row; the columns, the rows, which rows are
 * of double height, and the firmware's buttons. */
#define OSD_BACKLIGHT 0x00
#define OSD_ROWS      0x10
#define OSD_HEIGHTS   0x20
#define OSD_BUTTONS   0x30
#define OSD_COLUMNS   0x40

/* Written by the bus side only. The user interface reads it without a
 * lock: @seq is odd while an update is in progress. */
static struct {
    volatile unsigned int seq;
    bool on;
    unsigned int cols, rows, heights;
    uint8_t text[OSD_MAX_ROWS][OSD_MAX_COLS];
    unsigned int nr_updates;
} d;

static bool fitted;

/* I2C transaction state: the device addressed, whether it was written, the
 * byte of the info that a read gives next, and the place of the next
 * character of text, its row counted from 1, 0 while commands come. */
static bool selected, wrote;
static unsigned int info_pos, text_x, text_y;

/* What a read gives: FF OSD's protocol and firmware version, as FF OSD
 * 1.9 has them, and its own buttons, none pressed. */
static const uint8_t info[] = { 0, 1, 9, 0 };

static void update_begin(void)
{
    __sync_fetch_and_add(&d.seq, 1);
}

static void update_end(void)
{
    __sync_fetch_and_add(&d.seq, 1);
}

int osd_i2c_start(unsigned int addr, int rd)
{
    selected = fitted && (addr == I2C_ADDR);
    if (selected) {
        info_pos = 0;
        text_y = 0;
    }
    return selected;
}

void osd_i2c_write(uint8_t b)
{
    if (!selected)
        return;
    wrote = true;
    update_begin();
    if (text_y != 0) {
        d.text[text_y - 1][text_x] = b;
        if (++text_x >= d.cols) {
            text_x = 0;
            if (++text_y > d.rows)
                text_y = 0;
        }
    } else if ((b & 0xc0) == OSD_COLUMNS) {
        d.cols = b & 0x3f;
        if (d.cols > OSD_MAX_COLS)
            d.cols = OSD_MAX_COLS;
    } else {
        switch (b & 0xf0) {
        case OSD_ROWS:
            /* FF OSD takes three rows at most, two bits of the count. */
            d.rows = b & 0x03;
            break;
        case OSD_HEIGHTS:
            d.heights = b & 0x0f;
            break;
        case OSD_BACKLIGHT:
            if ((b & 0x0f) <= 1) {
                d.on = b & 1;
            } else if ((b & 0x0f) == 2) {
                /* With no rows, the first row's characters are still
                 * taken, and what follows them as commands, as FF OSD
                 * does. */
                text_x = 0;
                text_y = 1;
            }
            break;
        }
    }
    update_end();
}

uint8_t osd_i2c_read(void)
{
    return (info_pos < sizeof(info)) ? info[info_pos++] : 0xff;
}

void osd_i2c_stop(void)
{
    if (selected && wrote) {
        update_begin();
        d.nr_updates++;
        update_end();
    }
    selected = wrote = false;
}

void osd_init(bool fit)
{
    fitted = fit;
    memset(d.text, ' ', sizeof(d.text));
}

void osd_get_view(struct osd_view *view)
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
    view->cols = snap.cols;
    view->rows = snap.rows;
    view->heights = snap.heights;
    view->nr_updates = snap.nr_updates;
    memcpy(view->text, snap.text, sizeof(view->text));
}

/* Lines of FF OSD's frame: two blank ones at the top, and each row's eight,
 * sixteen if of double height, then two blank ones. */
unsigned int osd_height(const struct osd_view *v)
{
    unsigned int r, h = 2;

    for (r = 0; r < v->rows; r++)
        h += ((v->heights & (1u << r)) ? 16 : 8) + 2;
    return h;
}

void osd_pixels(const struct osd_view *v, uint8_t *px, unsigned int w,
                unsigned int h)
{
    unsigned int r, y, line, n, x, c, b;
    const uint8_t *t;

    memset(px, 0, w * h);
    for (r = 0, y = 2; (r < v->rows) && (y < h); r++) {
        n = (v->heights & (1u << r)) ? 16 : 8;
        t = v->text[r];
        for (line = 0; (line < n) && (y + line < h); line++) {
            for (x = 0; (x < v->cols) && ((x + 1) * 8 <= w); x++) {
                /* As FF OSD does: anything but 0x20-0x7F is a space. */
                c = ((t[x] < 0x20) || (t[x] > 0x7f)) ? 0 : t[x] - 0x20;
                c = osd_font[c * 8 + ((n == 16) ? line / 2 : line)];
                for (b = 0; b < 8; b++)
                    px[(y + line) * w + x * 8 + b] = (c >> (7 - b)) & 1;
            }
        }
        y += n + 2;
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

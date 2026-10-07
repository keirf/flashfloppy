/*
 * ssd1306.c
 *
 * Model of an OLED display on the I2C bus, the only slave device that ffemu
 * fits: an SSD1306 or an SH1106 controller, on a panel of 32 or 64 rows. It
 * decodes the command and data stream that the firmware sends, and keeps the
 * display RAM for the user interface to draw.
 *
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

#include <string.h>

#include "host.h"

#define I2C_ADDR 0x3c
#define RAM_COLS 132 /* the SH1106's; the SSD1306 has 128 */
#define RAM_PAGES 8

const char * const display_name[DISP_nr] = {
    [DISP_ssd1306_32] = "ssd1306-128x32",
    [DISP_ssd1306_64] = "ssd1306-128x64",
    [DISP_sh1106_32] = "sh1106-128x32",
    [DISP_sh1106_64] = "sh1106-128x64"
};

const char * const display_label[DISP_nr] = {
    [DISP_ssd1306_32] = "OLED 128x32 on SSD1306",
    [DISP_ssd1306_64] = "OLED 128x64 on SSD1306",
    [DISP_sh1106_32] = "OLED 128x32 on SH1106",
    [DISP_sh1106_64] = "OLED 128x64 on SH1106"
};

/* Written by the firmware thread only. The user interface reads it without a
 * lock: @seq is odd while an update is in progress. */
static struct {
    volatile unsigned int seq;
    uint8_t ram[RAM_PAGES][RAM_COLS];
    bool on, inverse, all_on, seg_remap, com_dec;
    uint8_t mux, contrast;
    unsigned int nr_updates;
} d;

static bool fitted, sh1106;
static unsigned int ram_cols = 128, panel_rows = 32;

/* I2C transaction state. */
static bool selected, wrote_data;
static enum { CTL_none, CTL_cmd, CTL_data } ctl;
static bool ctl_single; /* Co bit: a control byte follows the next byte */
static unsigned int nr_reads; /* bytes read in this transaction */

/* Command decoder state. */
static uint8_t cmd, cmd_args[6], nr_args, nr_args_wanted;

/* RAM address state. */
static uint8_t col, page, col_start, col_end = 127;
static uint8_t page_start, page_end = RAM_PAGES-1, addr_mode = 2;

static void update_begin(void)
{
    __sync_fetch_and_add(&d.seq, 1);
}

static void update_end(void)
{
    __sync_fetch_and_add(&d.seq, 1);
}

static unsigned int cmd_nr_args(uint8_t c)
{
    switch (c) {
    case 0x81: case 0x8d: case 0xa8: case 0xad: case 0xd3: case 0xd5:
    case 0xd9: case 0xda: case 0xdb:
        return 1;
    case 0x20:
        return sh1106 ? 0 : 1;
    case 0x21: case 0x22: case 0xa3:
        return sh1106 ? 0 : 2;
    case 0x29: case 0x2a:
        return sh1106 ? 0 : 5;
    case 0x26: case 0x27:
        return sh1106 ? 0 : 6;
    }
    return 0;
}

static void cmd_execute(void)
{
    const uint8_t *a = cmd_args;

    update_begin();

    switch (cmd) {
    case 0x00 ... 0x0f: col = (col & 0xf0) | cmd; break;
    case 0x10 ... 0x1f: col = (col & 0x0f) | ((cmd & 0x0f) << 4); break;
    case 0xb0 ... 0xb7: page = cmd & 7; break;
    case 0x81: d.contrast = a[0]; break;
    case 0xa0: case 0xa1: d.seg_remap = cmd & 1; break;
    case 0xa4: case 0xa5: d.all_on = cmd & 1; break;
    case 0xa6: case 0xa7: d.inverse = cmd & 1; break;
    case 0xa8: d.mux = a[0] & 63; break;
    case 0xae: case 0xaf: d.on = cmd & 1; break;
    case 0xc0: case 0xc8: d.com_dec = !!(cmd & 8); break;
    }

    /* The SSD1306's addressing modes, which the SH1106 lacks. */
    if (!sh1106) {
        switch (cmd) {
        case 0x20: addr_mode = a[0] & 3; break;
        case 0x21:
            col_start = a[0] & 127;
            col_end = a[1] & 127;
            col = col_start;
            break;
        case 0x22:
            page_start = a[0] & (RAM_PAGES-1);
            page_end = a[1] & (RAM_PAGES-1);
            page = page_start;
            break;
        }
    }

    col %= ram_cols;
    update_end();
}

static void cmd_byte(uint8_t b)
{
    if (nr_args_wanted == 0) {
        cmd = b;
        nr_args = 0;
        nr_args_wanted = cmd_nr_args(b);
    } else {
        cmd_args[nr_args++] = b;
    }
    if (nr_args == nr_args_wanted) {
        cmd_execute();
        nr_args_wanted = 0;
    }
}

static void data_byte(uint8_t b)
{
    update_begin();

    d.ram[page][col] = b;
    wrote_data = true;

    switch (sh1106 ? 2 : addr_mode) {
    case 0: /* horizontal */
        if (col++ == col_end) {
            col = col_start;
            page = (page == page_end) ? page_start : page + 1;
        }
        break;
    case 1: /* vertical */
        if (page++ == page_end) {
            page = page_start;
            col = (col == col_end) ? col_start : col + 1;
        }
        break;
    default: /* page */
        col++;
        break;
    }
    col %= ram_cols;
    page &= RAM_PAGES-1;

    update_end();
}

int emu_i2c_dev_start(unsigned int addr, int rd)
{
    selected = fitted && (addr == I2C_ADDR);
    ctl = CTL_none;
    nr_reads = 0;
    return selected;
}

void emu_i2c_dev_write(uint8_t b)
{
    if (!selected)
        return;

    switch (ctl) {
    case CTL_none:
        ctl = (b & 0x40) ? CTL_data : CTL_cmd;
        ctl_single = !!(b & 0x80);
        break;
    case CTL_cmd:
        cmd_byte(b);
        if (ctl_single)
            ctl = CTL_none;
        break;
    case CTL_data:
        data_byte(b);
        if (ctl_single)
            ctl = CTL_none;
        break;
    }
}

uint8_t emu_i2c_dev_read(void)
{
    /* An SH1106 reads back its RAM, after a dummy byte. Over I2C an SSD1306
     * returns only its status: the firmware tells the two apart by that. */
    if (sh1106)
        return (nr_reads++ == 0) ? 0 : d.ram[page][col];
    return d.on ? 0x00 : 0x40;
}

void emu_i2c_dev_stop(void)
{
    if (selected && wrote_data) {
        update_begin();
        d.nr_updates++;
        update_end();
    }
    selected = wrote_data = false;
    ctl = CTL_none;
}

void oled_init(int display)
{
    fitted = true;
    sh1106 = DISP_IS_SH1106(display);
    ram_cols = sh1106 ? 132 : 128;
    panel_rows = DISP_HEIGHT(display);
    d.mux = 63;
    d.contrast = 0x7f;
}

void oled_get_view(struct oled_view *view, uint8_t *px)
{
    static typeof(d) snap;
    unsigned int seq, x, y, h, off;

    do {
        seq = d.seq;
        __sync_synchronize();
        memcpy(&snap, &d, sizeof(snap));
        __sync_synchronize();
    } while ((seq & 1) || (seq != d.seq));

    h = snap.mux + 1;
    view->present = fitted;
    view->on = snap.on;
    view->inverse = snap.inverse;
    view->chip = sh1106 ? "SH1106" : "SSD1306";
    view->height = panel_rows;
    view->driven = h;
    view->contrast = snap.contrast;
    view->nr_updates = snap.nr_updates;

    /* The panel is mounted so that the reversed segment and COM directions,
     * which the firmware selects by default, give an upright picture. A
     * 128x64 panel on an SH1106 shows the middle 128 of its 132 columns. */
    off = (sh1106 && (panel_rows == 64)) ? 2 : 0;
    memset(px, 0, OLED_W * OLED_MAX_H);
    for (y = 0; (y < h) && (y < panel_rows); y++) {
        unsigned int ry = snap.com_dec ? y : h-1-y;
        for (x = 0; x < OLED_W; x++) {
            unsigned int rx = snap.seg_remap ? off + x : ram_cols-1-off-x;
            bool lit = snap.all_on || ((snap.ram[ry>>3][rx] >> (ry&7)) & 1);
            px[y*OLED_W + x] = snap.on && (lit ^ snap.inverse);
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

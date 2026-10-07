/*
 * tui.c
 *
 * The terminal user interface of ffemu, built on curses: the OLED display,
 * the state of the emulated device, the key help and the firmware's console
 * log. It runs in a thread of its own and is the only code that calls
 * curses.
 *
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

#include <ctype.h>
#include <curses.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wchar.h>
#include <sys/stat.h>

#include "host.h"

static pthread_t thread;
static volatile int started, stop_request, stopped;

/* What leaving curses sends to the terminal, for a signal handler, which
 * cannot call curses. */
static char fatal_seq[128];
static size_t fatal_len;

static void prepare_fatal_seq(void)
{
    static const char * const caps[] = { "sgr0", "cnorm", "rmkx", "rmcup" };
    unsigned int i;

    for (i = 0; i < ARRAY_SIZE(caps); i++) {
        const char *s = tigetstr(caps[i]);
        size_t n;
        if ((s == NULL) || (s == (char *)-1))
            continue;
        n = strlen(s);
        if (fatal_len + n <= sizeof(fatal_seq)) {
            memcpy(&fatal_seq[fatal_len], s, n);
            fatal_len += n;
        }
    }
}

void tui_stop_fatal(void)
{
    if (started && !stopped && (fatal_len != 0))
        (void)!write(STDOUT_FILENO, fatal_seq, fatal_len);
}

/* The colors of Turbo Vision, as in the Borland Pascal 7 IDE: windows of
 * yellow text on blue, a menu bar and a status line of black on light gray
 * with red hotkeys, white on red for alarm. The OLED itself is cyan on
 * black, and the space beside it keeps the terminal's own background. */
enum {
    CP_text = 1,  /* light gray on blue: labels, log, frames */
    CP_value,     /* yellow on blue: values, key names */
    CP_display,   /* the color of the settings on black: the OLED pixels */
    CP_dframe,    /* light gray on black: the frame around the OLED */
    CP_path,      /* light cyan on blue */
    CP_bar,       /* black on light gray: menu bar and status line */
    CP_hotkey,    /* dark red on light gray: hotkeys in the status line */
    CP_warn,      /* bright white on red */
    CP_good,      /* bright green on blue */
    CP_ctl_off,   /* dark blue on light gray: a control at rest */
    CP_ctl_latch, /* dark red on light gray: a latchable button */
    CP_ctl_on,    /* black on bright yellow: a control in use */
    CP_crop,      /* bright green on blue: a line is cut short */
    CP_note,      /* dark red on black: a remark in the OLED's frame */
    CP_focus,     /* bright white on blue: the frame of the focused window */
    CP_dialog,    /* black on light gray: a dialog */
    CP_dlg_frame, /* bright white on light gray: the dialog's frame */
    CP_button,    /* black on green: a dialog button */
    CP_button_on, /* bright white on green: the highlighted button */
    CP_shadow,    /* dark gray on black: what a dialog's shadow falls on */
    CP_dark,      /* dark gray on blue: something at rest, use DARK_GRAY */
    CP_bad,       /* red on blue: bright with BRIGHT(), for "Ejected" */
    CP_off,       /* light gray on dark gray: a window with nothing to show */
    CP_scroll,    /* blue on cyan: a scroll bar */
    CP_raw,       /* bright magenta on blue: flash bytes FF.CFG cannot say */
    CP_bar_crop,  /* bright green on light gray: the status line is cut */
    CP_dlg_hot,   /* bright yellow on light gray: a hotkey in a dialog */
    CP_cluster,   /* black on cyan: radio buttons and check boxes */
    CP_cluster_hi, /* bright white on cyan: the one of them in focus */
    CP_cluster_hot, /* bright yellow on cyan: a hotkey among them */
    CP_input,     /* bright white on blue: an input line */
    CP_button_def, /* bright cyan on green: the default button */
    CP_nr
};

/* Each pair has a twin with the bright version of its ink, which BRIGHT()
 * takes where the terminal has 16 colors: not all terminals show bold text
 * in bright colors, and some show it in a heavier font instead. */
#define CP_BRIGHT 64
#define BRIGHT(cp) ((COLORS >= 16) ? COLOR_PAIR((cp) + CP_BRIGHT) \
                    : COLOR_PAIR(cp) | A_BOLD)

/* Dark gray: bright black where the terminal has 16 colors, else black
 * made bright by bold, which most terminals do. */
#define DARK_GRAY (COLOR_PAIR(CP_dark) | ((COLORS >= 16) ? 0 : A_BOLD))

/* Controls, Status and Flash mem are side by side, of equal height; Status
 * is never narrower than its board name needs. */
#define STATUS_MIN_W 42
#define PANE_ROWS 8

/* The letters that select the rendering styles, in their order. */
#define STYLE_KEYS "qwe"

/* The keys that set the display color, as the Controls window shows them. */
#define COLOR_KEYS "1..7"

/* The key that opens the dialog to choose the display fitted. */
#define DISPLAY_KEY '0'

/* The key that opens the dialog to edit the configuration in flash. */
#define FLASH_KEY '9'

/* The dialog on screen, if any, its highlighted button (0 is the first),
 * and the display that the display dialog has selected. */
static enum {
    DLG_none, DLG_quit, DLG_display, DLG_rows, DLG_flash, DLG_emul
} dialog;
static int dialog_button;
static int dialog_display;
static void draw_dialog(void);
static void draw_flash_dialog(void);

#define FRAME_MS 20
#define SPEAKER_SHOWN_NS 120000000u
#define USB_ACCESS_SHOWN_NS 120000000u

/* Right edge of the window being drawn, where a cut line is marked. */
static int pane_right = -1;

/* Everything in ASCII, for terminals and fonts that lack the rest: when the
 * display is drawn in ASCII. */
static bool plain(void)
{
    return config.style == STYLE_ascii;
}

/* The ASCII stand-in for @c. */
static wchar_t ascii_of(wchar_t c)
{
    if (c < 0x7f)
        return c;
    switch (c) {
    case 0x2500:
        return L'-';
    case 0x2502: case 0x2551:
        return L'|';
    case 0x2550: /* the active frame's top and bottom */
        return L'=';
    case 0x250c: case 0x2510: case 0x2514: case 0x2518:
    case 0x2554: case 0x2557: case 0x255a: case 0x255d:
        return L'+';
    case 0x2800:
        return L' ';
    case 0x2592:
        return L':';
    case 0x2588: /* the scroll bar's thumb */
        return L'#';
    case 0x2022:
        return L'*';
    }
    if ((c > 0x2800) && (c <= 0x28ff))
        return L'#'; /* Braille, where the window is too small for ASCII */
    if ((c >= 0x2580) && (c <= 0x259f))
        return L'*';
    return L'?';
}

static void put_wide(int y, int x, attr_t attr, const wchar_t *ws, int n)
{
    wchar_t ascii[512];
    int i;

    if ((y < 0) || (y >= LINES) || (x < 0) || (x >= COLS))
        return;
    if (n > COLS - x)
        n = COLS - x;
    if (plain()) {
        if (n > (int)ARRAY_SIZE(ascii) - 1)
            n = ARRAY_SIZE(ascii) - 1;
        for (i = 0; (i < n) && (ws[i] != L'\0'); i++)
            ascii[i] = ascii_of(ws[i]);
        ascii[i] = L'\0';
        ws = ascii;
        n = i;
    }
    attron(attr);
    mvaddnwstr(y, x, ws, n);
    attroff(attr);
}

/* Writes at most @w columns of @s at (@y,@x), clipped to the screen, and
 * marks the line as cut if it did not fit. */
static void put(int y, int x, int w, attr_t attr, const char *fmt, ...)
{
    static const wchar_t cut[] = L">";
    char s[512];
    wchar_t ws[512];
    va_list ap;
    size_t n;

    if ((y < 0) || (y >= LINES) || (x < 0) || (x >= COLS))
        return;
    if (w > COLS - x)
        w = COLS - x;
    if (w <= 0)
        return;

    va_start(ap, fmt);
    vsnprintf(s, sizeof(s), fmt, ap);
    va_end(ap);

    n = mbstowcs(ws, s, ARRAY_SIZE(ws) - 1);
    if (n == (size_t)-1)
        return;
    ws[n] = L'\0';

    /* Text with no color of its own is window text. */
    if (PAIR_NUMBER(attr) == 0)
        attr |= COLOR_PAIR(CP_text);
    put_wide(y, x, attr, ws, w);

    if ((n > (size_t)w) && (pane_right >= 0))
        put_wide(y, pane_right, BRIGHT(CP_crop), cut, 1);
}

/* A window of @w by @h cells, with a title centered in its top edge, its
 * inside filled with blanks of @interior and its edges drawn in @attr. The
 * window with the keyboard focus has a double-line frame, as in Turbo
 * Vision. */
static void frame(int y, int x, int w, int h, const char *title,
                  attr_t interior, attr_t attr)
{
    /* Corners and edges: top left, top right, bottom left, bottom right,
     * horizontal, vertical. */
    static const wchar_t single[6] = {
        0x250c, 0x2510, 0x2514, 0x2518, 0x2500, 0x2502 };
    static const wchar_t dual[6] = {
        0x2554, 0x2557, 0x255a, 0x255d, 0x2550, 0x2551 };
    const wchar_t *box = (PAIR_NUMBER(attr) == CP_focus) ? dual : single;
    wchar_t line[512];
    int i, tw = strlen(title) + 2;

    if ((w < 2) || (h < 2))
        return;
    if (w > (int)ARRAY_SIZE(line))
        w = ARRAY_SIZE(line);

    for (i = 1; i < w-1; i++)
        line[i] = box[4];
    line[0] = box[0];
    line[w-1] = box[1];
    put_wide(y, x, attr, line, w);
    line[0] = box[2];
    line[w-1] = box[3];
    put_wide(y + h-1, x, attr, line, w);
    for (i = 1; i < w-1; i++)
        line[i] = L' ';
    for (i = 1; i < h-1; i++) {
        put_wide(y + i, x + 1, interior, line + 1, w - 2);
        put_wide(y + i, x, attr, box + 5, 1);
        put_wide(y + i, x + w-1, attr, box + 5, 1);
    }

    pane_right = -1;
    if (tw > w - 4)
        tw = w - 4;
    put(y, x + (w - tw) / 2, tw, attr, " %s ", title);
    pane_right = x + w - 1;
}

/* @path as a shell shows it: under the home directory as ~/... */
static const char *tilde(const char *path, char *buf, size_t size)
{
    const char *home = getenv("HOME");
    size_t n = (home != NULL) ? strlen(home) : 0;

    while ((n > 1) && (home[n-1] == '/'))
        n--;
    if ((n == 0) || strncmp(path, home, n)
        || ((path[n] != '/') && (path[n] != '\0')))
        return path;
    snprintf(buf, size, "~%s", path + n);
    return buf;
}

/* The keys of the actions, and their names as the screen shows them. */
static const int key_code[KEY_ACT_nr] = {
    [KEY_ACT_select] = '\n',
    [KEY_ACT_left] = KEY_LEFT,
    [KEY_ACT_right] = KEY_RIGHT,
    [KEY_ACT_cw] = KEY_DOWN,
    [KEY_ACT_ccw] = KEY_UP,
    [KEY_ACT_remove] = KEY_DC,
    [KEY_ACT_insert] = KEY_IC,
    [KEY_ACT_reset] = KEY_HOME,
    [KEY_ACT_quit] = 27,
    [KEY_ACT_latch] = ' '
};
static const char * const key_label[KEY_ACT_nr] = {
    [KEY_ACT_select] = "Enter",
    [KEY_ACT_left] = "<",
    [KEY_ACT_right] = ">",
    [KEY_ACT_cw] = "v",
    [KEY_ACT_ccw] = "^",
    [KEY_ACT_remove] = "Del",
    [KEY_ACT_insert] = "Ins",
    [KEY_ACT_reset] = "Home",
    [KEY_ACT_quit] = "Esc",
    [KEY_ACT_latch] = "Space"
};

/*
 * The display.
 */

/* Size in character cells of a display of @h pixel rows, in rendering
 * style @style. */
static void display_cells(int style, unsigned int h, int *cw, int *ch)
{
    switch (style) {
    case STYLE_braille:
        *cw = OLED_W / 2;
        *ch = h / 4;
        break;
    case STYLE_ascii:
        *cw = OLED_W;
        *ch = h;
        break;
    default:
        *cw = OLED_W;
        *ch = h / 2;
        break;
    }
}

/* The preferred style, unless its display does not fit the terminal: then
 * the narrowest one. */
static int style_in_use(unsigned int h)
{
    int cw, ch;

    display_cells(config.style, h, &cw, &ch);
    return (cw + 2 > COLS) ? STYLE_braille : config.style;
}

/* Draws the display at (@y0,@x0) in @style; @too_small if it does not fit
 * the window even so. */
static void draw_display(int y0, int x0, int style, bool too_small,
                         const struct oled_view *v, const uint8_t *px)
{
    static const char * const style_label[STYLE_nr + 1] = {
        [STYLE_braille] = "Braille",
        [STYLE_half] = "Half blocks",
        [STYLE_ascii] = "ASCII"
    };
#define PX(x,y) px[(y)*OLED_W + (x)]
    static const wchar_t half[4] = { L' ', 0x2580, 0x2584, 0x2588 };
    attr_t attr = (config.display_color & 8) ? BRIGHT(CP_display)
        : COLOR_PAIR(CP_display);
    wchar_t line[OLED_W];
    int cw, ch, x, y, hint_y;
    char title[96];
    bool mismatch = v->present && (v->driven != v->height);
    int tw;
#define MISMATCH " - display type mismatch"

    display_cells(style, v->height, &cw, &ch);

    snprintf(title, sizeof(title), "OLED 128x%u on %s%s%s", v->height,
             v->chip, v->on ? "" : " - off", mismatch ? MISMATCH : "");
    frame(y0, x0, cw + 2, ch + 2, title, attr, COLOR_PAIR(CP_dframe));

    /* The firmware set up for another display than the one fitted: in red,
     * where frame() put the end of the title. */
    tw = strlen(title) + 2;
    if (mismatch && (tw <= cw + 2 - 4))
        put(y0, x0 + (cw + 2 - tw) / 2 + 1 + strlen(title) - strlen(MISMATCH),
            strlen(MISMATCH), BRIGHT(CP_note), "%s", MISMATCH);

    /* The rendering style and its key, for the bottom left corner; or that
     * the window is too small, on the lowest line left above the status
     * line if the bottom is cut off. */
    if (too_small)
        snprintf(title, sizeof(title), " Window too small ");
    else
        snprintf(title, sizeof(title), " %c: %s ",
                 toupper(STYLE_KEYS[config.style - 1]),
                 (style == config.style) ? style_label[style]
                 : "Window too small");
    too_small = too_small || (style != config.style);
    for (y = 0; y < ch; y++) {
        for (x = 0; x < cw; x++) {
            switch (style) {
            case STYLE_braille: {
                /* Dots are numbered down the left column, then the right,
                 * except for the bottom pair, which was added later. */
                static const uint8_t dot[4][2] = {
                    { 0x01, 0x08 }, { 0x02, 0x10 },
                    { 0x04, 0x20 }, { 0x40, 0x80 } };
                unsigned int i, j, bits = 0;
                for (i = 0; i < 4; i++)
                    for (j = 0; j < 2; j++)
                        if (PX(2*x + j, 4*y + i))
                            bits |= dot[i][j];
                line[x] = 0x2800 + bits;
                break;
            }
            case STYLE_ascii:
                line[x] = PX(x, y) ? L'#' : L' ';
                break;
            default:
                line[x] = half[PX(x, 2*y) | (PX(x, 2*y + 1) << 1)];
                break;
            }
        }
        put_wide(y0 + 1 + y, x0 + 1, attr, line, cw);
    }
#undef PX
#undef MISMATCH

    /* Over the pixels if the bottom is cut off. */
    hint_y = y0 + ch + 1;
    if ((hint_y > LINES - 2) && (LINES - 2 > y0))
        hint_y = LINES - 2;
    put(hint_y, x0 + 2, strlen(title),
        COLOR_PAIR(too_small ? CP_note : CP_dframe), "%s", title);
}

/*
 * The status window.
 */

/* One "label value" line. */
static void field(int *y, int x, int w, const char *label, attr_t attr,
                  const char *fmt, ...)
{
    char s[512];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(s, sizeof(s), fmt, ap);
    va_end(ap);

    put(*y, x, w, 0, "%s", label);
    put(*y, x + 10, w - 10, attr ? attr : BRIGHT(CP_value),
        "%s", s);
    (*y)++;
}

/* @path at (@y,@x) in bright cyan, or its end in @w columns if it is too
 * long, with a green '<' in the frame to the left of @x, as a Turbo Vision
 * input line has. */
static void left_path(int y, int x, int w, const char *path)
{
    static const wchar_t cut[] = L"<";
    wchar_t ws[600];
    size_t n = mbstowcs(ws, path, ARRAY_SIZE(ws) - 1);

    if ((n == (size_t)-1) || (w < 1))
        return;
    ws[n] = L'\0';
    if ((int)n <= w) {
        put_wide(y, x, BRIGHT(CP_path), ws, n);
        return;
    }
    put_wide(y, x - 1, BRIGHT(CP_crop), cut, 1);
    put_wide(y, x, BRIGHT(CP_path), ws + n - w, w);
}

static void draw_status(int y, int x, int w)
{
    struct usb_info usb;
    char path[600], line[128];

    frame(y, x, w, PANE_ROWS + 2, "Status", COLOR_PAIR(CP_text),
          COLOR_PAIR(CP_text));
    y++;
    x++;
    w -= 2;

    field(&y, x, w, "Firmware", 0, "FlashFloppy %s", emu_fw_version());
    field(&y, x, w, "Board", 0, "%s", emu_board_name());
    y++;

    usb_get_info(&usb);
    if (usb.inserted) {
        /* The FF.CFG that the firmware reads follows, as a path. */
        char size[32];
        int n = (usb.kind == USB_dir)
            ? snprintf(line, sizeof(line),
                       "%u file%s, %u dir%s%s%s", usb.nr_files,
                       (usb.nr_files == 1) ? "" : "s", usb.nr_dirs,
                       (usb.nr_dirs == 1) ? "" : "s",
                       (usb.nr_skipped || usb.nr_case_dups)
                       ? ", some left out" : "",
                       usb.ff_cfg[0] ? "," : "")
            : snprintf(line, sizeof(line), "%s %s%s",
                       size_text(usb.image_bytes, size, sizeof(size)),
                       (usb.kind == USB_image) ? "image" : "disk",
                       usb.ff_cfg[0] ? "," : "");
        field(&y, x, w, "USB drive", BRIGHT(CP_good), "%s",
              line);
        if (usb.ff_cfg[0] && (10 + n + 1 < w))
            put(y - 1, x + 10 + n + 1, w - 10 - n - 1,
                BRIGHT(CP_path), "%s", usb.ff_cfg);
    } else {
        field(&y, x, w, "USB drive", BRIGHT(CP_bad),
              "Ejected");
    }
    left_path(y++, x, w, tilde(usb_path_name, path, sizeof(path)));
    /* Why the drive could not be inserted, if it could not, in place of the
     * gap below. */
    if (!usb.inserted && usb.error[0])
        field(&y, x, w, "", BRIGHT(CP_warn), "%s", usb.error);
    else
        y++;

    put(y++, x, w, 0, "Settings and flash mem:%s",
        config.loaded ? "" : " none");
    if (config.loaded)
        left_path(y, x, w, tilde(config.path, path, sizeof(path)));
}

/* Green arrows in the top and bottom frame of a window at (@y,@x) of @h,
 * over its first column of text, where lines are out of view above or
 * below. */
static void scroll_marks(int y, int x, int h, bool above, bool below)
{
    if (above)
        put(y, x + 1, 1, BRIGHT(CP_crop), "^");
    if (below)
        put(y + h - 1, x + 1, 1, BRIGHT(CP_crop), "v");
}

/* A Turbo Vision scroll bar in the right frame of a window at (@y,@x) of @w
 * by @h, which shows its lines from @top on, of @nr_lines. */
static void scroll_bar(int y, int x, int w, int h, int top, int nr_lines)
{
    /* Not the triangles of the DOS character set: many fonts draw them
     * twice as wide. */
    static const wchar_t up[] = { L'^' }, down[] = { L'v' };
    static const wchar_t track[] = { 0x2592 }, thumb[] = { 0x2588 };
    attr_t attr = COLOR_PAIR(CP_scroll);
    int len = h - 4, max_top = nr_lines - (h - 2), i;

    if (len < 1)
        return;
    put_wide(y + 1, x + w - 1, attr, up, 1);
    for (i = 0; i < len; i++)
        put_wide(y + 2 + i, x + w - 1, attr, track, 1);
    put_wide(y + 2 + ((max_top > 0) ? (top * (len - 1) + max_top / 2)
                      / max_top : 0), x + w - 1, attr, thumb, 1);
    put_wide(y + h - 2, x + w - 1, attr, down, 1);
}

/* The codes that curses gives Ctrl+PgUp and Ctrl+PgDn. */
static int key_ctrl_pgup = -1, key_ctrl_pgdn = -1;

/* The code of the key that terminfo names @cap, or that sends @seq if it has
 * no such name; @code if curses knows neither, which it then learns. */
static int ctrl_key(const char *cap, const char *seq, int code)
{
    const char *s = tigetstr(cap);
    int k;

    if ((s == NULL) || (s == (char *)-1))
        s = seq;
    k = key_defined(s);
    if (k > 0)
        return k;
    define_key(s, code);
    return code;
}

/* The first line that the Flash mem window shows: PgUp and PgDn move it. */
static int flash_top;

/* The keys that page a window at (@x) of @w, at the right end of its bottom
 * frame @y: PgUp/PgDn, after modifier @mod and a '+' unless it is NULL.
 * Returns the columns taken, or 0 if they do not fit. */
static int page_keys(int y, int x, int w, const char *mod)
{
    int m = mod ? strlen(mod) : 0, n = 11 + (mod ? m + 1 : 0);
    int sx = x + w - 2 - n;

    if (sx <= x + 2)
        return 0;
    pane_right = -1;
    put(y, sx, 1, COLOR_PAIR(CP_text), " ");
    if (mod != NULL) {
        put(y, sx + 1, m, BRIGHT(CP_value), "%s", mod);
        put(y, sx + 1 + m, 1, COLOR_PAIR(CP_text), "+");
        sx += m + 1;
    }
    put(y, sx + 1, 4, BRIGHT(CP_value), "PgUp");
    put(y, sx + 5, 1, COLOR_PAIR(CP_text), "/");
    put(y, sx + 6, 4, BRIGHT(CP_value), "PgDn");
    put(y, sx + 10, 1, COLOR_PAIR(CP_text), " ");
    return n;
}

/* The configuration in flash, option by option as FF.CFG names them: those
 * at their defaults in gray, and in magenta what FF.CFG cannot say. */
static void draw_flash(int y, int x, int w)
{
    uint8_t cfg[256];
    char value[64];
    unsigned int len = emu_flash_load(cfg, sizeof(cfg)), flags;
    int row, nr_lines = emu_flash_nr_options();
    const char *name;
    attr_t attr;

    if (w < 12)
        return;
    if (len == 0) {
        frame(y, x, w, PANE_ROWS + 2, "Flash mem - empty", COLOR_PAIR(CP_off),
              COLOR_PAIR(CP_off));
        return;
    }
    frame(y, x, w, PANE_ROWS + 2, "Flash mem", COLOR_PAIR(CP_text),
          COLOR_PAIR(CP_text));
    if (len != emu_flash_cfg_size()) {
        put(y + 1, x + 1, w - 2, BRIGHT(CP_raw),
            "%u bytes, not a configuration", len);
        return;
    }

    if (flash_top > nr_lines - PANE_ROWS)
        flash_top = nr_lines - PANE_ROWS;
    if (flash_top < 0)
        flash_top = 0;

    /* The scroll bar first: the '>' of a line cut short goes over it. */
    if (nr_lines > PANE_ROWS)
        scroll_bar(y, x, w, PANE_ROWS + 2, flash_top, nr_lines);
    for (row = 0; (row < PANE_ROWS) && (flash_top + row < nr_lines); row++) {
        name = emu_flash_option(flash_top + row, cfg, value, sizeof(value),
                                &flags);
        attr = (flags & (EMU_OPT_raw | EMU_OPT_hex_only))
            ? BRIGHT(CP_raw)
            : (flags & EMU_OPT_changed) ? BRIGHT(CP_value)
            : COLOR_PAIR(CP_text);
        put(y + 1 + row, x + 1, w - 2, attr, "%s = %s", name,
            (flags & EMU_OPT_hex_only) ? "(only in the hex)" : value);
    }
    if (nr_lines <= PANE_ROWS)
        return;

    scroll_marks(y, x, PANE_ROWS + 2, flash_top > 0,
                 flash_top + PANE_ROWS < nr_lines);

    /* The keys that page, in the bottom right corner. */
    page_keys(y + PANE_ROWS + 1, x, w, NULL);
}

/*
 * The controls window.
 */

/* TRUE for a while after each pulse to the speaker. */
static bool beeper_active(void)
{
    static unsigned int seen;
    static uint64_t at;
    unsigned int pulses = emu_out_speaker;
    uint64_t now = emu_time_ns();

    if (pulses != seen) {
        seen = pulses;
        at = now;
    }
    return at && (now - at < SPEAKER_SHOWN_NS);
}

/* The front panel as a picture: the left and right buttons, the encoder
 * with its push button and its two turning directions, and the beeper.
 * What is in use lights up. */
/* The USB port seen from the front, with its state below: light gray while
 * a drive is in, yellow for a moment when the firmware reads or writes it. */
static void usb_port(int y, int x, int w)
{
    static const wchar_t top[] = { 0x250c, 0x2500, 0x2500 };
    static const wchar_t end[] = { 0x2500, 0x2500, 0x2510 };
    static const wchar_t side[] = { 0x2502 }, bottom[] = {
        0x2514, 0x2500, 0x2500, 0x2500, 0x2500, 0x2500, 0x2500, 0x2500,
        0x2518 };
    static const wchar_t tongue[] = {
        0x2580, 0x2580, 0x2580, 0x2580, 0x2580, 0x2580, 0x2580 };
    static unsigned long nr_access;
    static uint64_t access_at;
    struct usb_info usb;
    attr_t attr;
    uint64_t now = emu_time_ns();

    if (w < 9)
        return;
    usb_get_info(&usb);
    if (usb.nr_reads + usb.nr_writes != nr_access) {
        nr_access = usb.nr_reads + usb.nr_writes;
        access_at = now;
    }
    if (!usb.inserted)
        attr = DARK_GRAY;
    else if (access_at && (now - access_at < USB_ACCESS_SHOWN_NS))
        attr = BRIGHT(CP_value);
    else
        attr = COLOR_PAIR(CP_text);

    put_wide(y, x, attr, top, 3);
    put(y, x + 3, 3, COLOR_PAIR(CP_text), "USB");
    put_wide(y, x + 6, attr, end, 3);
    put_wide(y + 1, x, attr, side, 1);
    put_wide(y + 1, x + 1, attr, tongue, 7);
    put_wide(y + 1, x + 8, attr, side, 1);
    put_wide(y + 2, x, attr, bottom, 9);
    if (usb.inserted)
        put(y + 3, x, w, BRIGHT(CP_good), "Inserted");
    else
        put(y + 3, x, w, BRIGHT(CP_bad), "Ejected");
}

static void picture(int y, int x, int w)
{
    unsigned int b = emu_in_buttons, r = ui_rotary_flash();
    bool beep = beeper_active();
    attr_t lit = BRIGHT(CP_value);
#define CTL(active) COLOR_PAIR((active) ? CP_ctl_on : CP_ctl_off)
    /* A button at rest in latch mode, which its key would hold down. */
#define BTN(active) (((active) || !ui_latched()) ? CTL(active) \
                     : COLOR_PAIR(CP_ctl_latch))

    put(y, x + 16, w - 16, CTL(r & UI_ROTARY_ccw), " ^ ");
    put(y + 1, x + 2, w - 2, BTN(b & EMU_B_LEFT), " < ");
    put(y + 1, x + 8, w - 8, BTN(b & EMU_B_RIGHT), " > ");
    put(y + 1, x + 14, w - 14, 0, "(");
    put(y + 1, x + 15, w - 15, BTN(b & EMU_B_SELECT), "Ent");
    put(y + 1, x + 18, w - 18, 0, ")");
    put(y + 2, x + 16, w - 16, CTL(r & UI_ROTARY_cw), " v ");
#undef BTN
#undef CTL

    /* The beeper: inner parentheses that light up, outer ones that appear. */
    put(y + 1, x + 23, w - 23, lit, beep ? "(" : " ");
    put(y + 1, x + 24, w - 24, beep ? lit : DARK_GRAY, "()");
    put(y + 1, x + 26, w - 26, lit, beep ? ")" : " ");
    put(y + 2, x + 22, w - 22, 0, "Beeper");
}

/* The keys of the device that the picture does not show; the keys of ffemu
 * itself are on the status line. */
static const struct {
    int act;
    const char *what;
} device_keys[] = {
    { KEY_ACT_latch, "Latch buttons (long/combined press)" },
    { KEY_ACT_insert, "Insert USB drive, re-reading files" },
    { KEY_ACT_remove, "Eject USB drive" },
    { KEY_ACT_reset, "Power cycle" }
};

/* Columns from the USB port's left edge to the end of the beeper. */
#define PICTURE_W 38

/* The widest key name, which the descriptions line up after. */
static unsigned int key_column(void)
{
    unsigned int i, col = 0;

    for (i = 0; i < ARRAY_SIZE(device_keys); i++)
        if (strlen(key_label[device_keys[i].act]) > col)
            col = strlen(key_label[device_keys[i].act]);
    return col;
}

/* The width of the Controls window, frame included. */
static int keys_width(void)
{
    unsigned int i, col = key_column(), w = 1 + PICTURE_W;

    for (i = 0; i < ARRAY_SIZE(device_keys); i++)
        if (col + 1 + strlen(device_keys[i].what) > w)
            w = col + 1 + strlen(device_keys[i].what);
    return w + 2;
}

static void draw_keys(int y, int x, int w)
{
    unsigned int i, col = key_column();

    frame(y, x, w, PANE_ROWS + 2, "Controls", COLOR_PAIR(CP_text),
          BRIGHT(CP_focus));
    y++;
    x++;
    w -= 2;

    /* A drawing, not text: one space from the frame. */
    usb_port(y, x + 1, w - 1);
    picture(y, x + 11, w - 11);
    y += 4;

    /* The latch key lights up while its mode lasts. */
    for (i = 0; i < ARRAY_SIZE(device_keys); i++) {
        int act = device_keys[i].act;
        bool lit = (act == KEY_ACT_latch) && ui_latched();
        put(y, x, w, lit ? COLOR_PAIR(CP_ctl_on)
            : BRIGHT(CP_value), "%s", key_label[act]);
        put(y++, x + col + 1, w - col - 1, 0, "%s", device_keys[i].what);
    }
}

/*
 * The firmware console window.
 */

/* The file that the console goes to, in the bottom left corner of the
 * frame at (@y,@x) of width @w; the home directory as ~, or the file name
 * alone where the whole path does not fit. */
/* File @name in the bottom left corner of a frame at (@y,@x) of @w, as a
 * path from the home directory, its start cut off if that is too long. */
static void path_label(int y, int x, int w, const char *name)
{
    char path[600];
    const char *s;
    bool cut;
    size_t n;

    if ((name == NULL) || (w < 12))
        return;
    s = tilde(name, path, sizeof(path));
    n = strlen(s);
    cut = ((int)n + 2 > w - 4);
    if (cut)
        s += n - (w - 4 - 5);

    pane_right = -1;
    put(y, x + 2, strlen(s) + (cut ? 5 : 2), COLOR_PAIR(CP_text), " %s%s ",
        cut ? "..." : "", s);
    pane_right = x + w - 1;
}

/* Line @s of @n bytes at (@y,@x) in @w columns, in @attr: a carriage return
 * at its end, of a CRLF, dropped; a tab as a space; and the other bytes that
 * are not printable ASCII, a lone carriage return among them, as a '?' in
 * magenta. */
static void put_line(int y, int x, int w, attr_t attr, const char *s,
                     size_t n)
{
    char run[512];
    size_t i, k = 0;
    int col = 0;

    if ((n != 0) && (s[n-1] == '\r'))
        n--;
    /* Runs of printable characters, between the question marks. */
    for (i = 0; (i < n) && (col + (int)k < w); i++) {
        unsigned char c = (s[i] == '\t') ? ' ' : s[i];
        if ((c >= ' ') && (c <= '~')) {
            if (k < sizeof(run) - 1)
                run[k++] = c;
            continue;
        }
        run[k] = '\0';
        put(y, x + col, w - col, attr, "%s", run);
        col += k;
        k = 0;
        put(y, x + col, 1, BRIGHT(CP_raw), "?");
        col++;
    }
    run[k] = '\0';
    put(y, x + col, w - col, attr, "%s", run);

    /* More than fits: marked as put() marks a line cut short. */
    if ((i < n) && (pane_right >= 0))
        put_wide(y, pane_right, BRIGHT(CP_crop), L">", 1);
}

static void draw_log(int y, int x, int w, int h)
{
    static char tail[8192];
    unsigned int head = log_head, len, i, nr_lines = 0;
    int rows = h - 2, row;
    bool truncated;
    char *p, *line;

    if ((rows < 1) || (w < 8))
        return;
    frame(y, x, w, h, "Firmware console", COLOR_PAIR(CP_text),
          COLOR_PAIR(CP_text));
    path_label(y + h - 1, x, w, log_file_name);

    len = (head < sizeof(tail) - 1) ? head : sizeof(tail) - 1;
    truncated = len < head; /* older output than the tail holds */
    for (i = 0; i < len; i++)
        tail[i] = log_ring[(head - len + i) % LOG_SIZE];
    tail[len] = '\0';

    /* Drop the unfinished last line's newline, then find the first of the
     * last @rows lines. */
    if ((len != 0) && (tail[len-1] == '\n'))
        tail[--len] = '\0';
    for (p = tail + len; p > tail; p--) {
        if ((p[-1] == '\n') && (++nr_lines == (unsigned int)rows))
            break;
    }
    scroll_marks(y, x, h, (p > tail) || truncated, false);

    for (row = 0, line = p; (row < rows) && (line != NULL); row++) {
        char *end = strchr(line, '\n');
        if (end != NULL)
            *end++ = '\0';
        /* The emulator's own lines in green, the firmware's in yellow. */
        put_line(y + 1 + row, x + 1, w - 2,
                 strncmp(line, HOST_LOG_PREFIX, strlen(HOST_LOG_PREFIX))
                 ? BRIGHT(CP_value) : BRIGHT(CP_good), line, strlen(line));
        line = end;
    }
}

/*
 * The screen.
 */

/* One "key description" item of the status line; returns the next free
 * column. */
static int hotkey(int x, const char *key, const char *what)
{
    int i;

    /* Slashes and dots between the keys are not keys themselves. */
    for (i = 0; key[i] != '\0'; i++)
        put(LINES - 1, x + 1 + i, 1, COLOR_PAIR(strchr("/.", key[i])
                                                ? CP_bar : CP_hotkey),
            "%c", key[i]);
    x += strlen(key) + 2;
    put(LINES - 1, x, COLS, COLOR_PAIR(CP_bar), "%s", what);
    return x + strlen(what) + 1;
}

/* The menu bar at the top, and the status line with its hotkeys at the
 * bottom. */
static void draw_bars(void)
{
    static const char *name = "ffemu", *ver = " v" FFEMU_VERSION;
    static const char *what = " - flashfloppy fw on a PC";
    /* "KIBER-MUZEJ, MUROM" in Cyrillic capitals, or in English where
     * everything is ASCII. */
    static const wchar_t cyrillic[] = L"\u041a\u0418\u0411\u0415\u0420-"
        L"\u041c\u0423\u0417\u0415\u0419, \u041c\u0423\u0420\u041e\u041c";
    const wchar_t *museum = plain() ? L"CYBER-MUSEUM, MUROM" : cyrillic;
    int name_w = strlen(name) + strlen(ver), what_w = strlen(what);
    int museum_w = wcslen(museum);
    char keys[16];
    int x = 0, i;

    pane_right = -1;

    /* Whole parts only: the description goes first, then the museum. */
    put(0, 0, COLS, COLOR_PAIR(CP_bar), "%*s", COLS, "");
    put(0, 1, COLS - 2, BRIGHT(CP_bar), "%s", name);
    put(0, 1 + strlen(name), COLS - 2 - strlen(name), COLOR_PAIR(CP_bar),
        "%s", ver);
    if (1 + name_w + what_w + 2 + museum_w + 1 <= COLS)
        put(0, 1 + name_w, what_w, COLOR_PAIR(CP_bar), "%s", what);
    if (1 + name_w + 2 + museum_w + 1 <= COLS)
        put_wide(0, COLS - 1 - museum_w, COLOR_PAIR(CP_bar), museum,
                 museum_w);

    put(LINES - 1, 0, COLS, COLOR_PAIR(CP_bar), "%*s", COLS, "");
    /* The keys of ffemu itself; the device's are in the Controls window. */
    x = hotkey(x, key_label[KEY_ACT_quit], "Exit");
    for (i = 0; i < STYLE_nr; i++) {
        keys[2*i] = toupper(STYLE_KEYS[i]);
        keys[2*i + 1] = '/';
    }
    keys[2*STYLE_nr - 1] = '\0';
    x = hotkey(x, keys, "Rendering");
    x = hotkey(x, "Ctrl+L", "Refresh");
    /* Shift is a key too; the brackets say that it is optional. */
    put(LINES - 1, x + 1, COLS, COLOR_PAIR(CP_bar), "[");
    put(LINES - 1, x + 2, COLS, COLOR_PAIR(CP_hotkey), "Shift");
    put(LINES - 1, x + 7, COLS, COLOR_PAIR(CP_bar), "+]");
    x = hotkey(x + 8, COLOR_KEYS, "Color");
    snprintf(keys, sizeof(keys), "%c", FLASH_KEY);
    x = hotkey(x, keys, "Flash mem");
    snprintf(keys, sizeof(keys), "%c", DISPLAY_KEY);
    x = hotkey(x, keys, "Display");
    /* Cut short by a narrow window: a mark in the last column. */
    if (x - 1 > COLS)
        put(LINES - 1, COLS - 1, 1, BRIGHT(CP_bar_crop), ">");
}

/* The first line that the FF.CFG window shows, and how many it shows:
 * Ctrl+PgUp and Ctrl+PgDn move it. */
static int ff_cfg_top, ff_cfg_rows = 1;

/* FF.CFG as last read from the host, read again when it changes. */
static struct {
    char path[700];
    time_t mtime;
    off_t size;
    char text[16384];
} ff_cfg_file;

static const char *ff_cfg_text(const char *path)
{
    struct stat st;
    FILE *f;
    size_t n = 0;

    if (stat(path, &st) != 0)
        return NULL;
    if (!strcmp(path, ff_cfg_file.path) && (st.st_mtime == ff_cfg_file.mtime)
        && (st.st_size == ff_cfg_file.size))
        return ff_cfg_file.text;

    f = fopen(path, "rb");
    if (f == NULL)
        return NULL;
    n = fread(ff_cfg_file.text, 1, sizeof(ff_cfg_file.text) - 1, f);
    fclose(f);
    ff_cfg_file.text[n] = '\0';
    if (strcmp(path, ff_cfg_file.path))
        ff_cfg_top = 0;
    snprintf(ff_cfg_file.path, sizeof(ff_cfg_file.path), "%s", path);
    ff_cfg_file.mtime = st.st_mtime;
    ff_cfg_file.size = st.st_size;
    return ff_cfg_file.text;
}

/* The FF.CFG that the firmware reads, beside the display: named by its path
 * on the drive, with its path on the host in the corner. */
static void draw_ff_cfg(int y, int x, int w, int h)
{
    struct usb_info usb;
    char path[700];
    const char *text = NULL, *p;
    int row, rows = h - 2, nr_lines = 0, keys = 0;

    if ((w < 16) || (h < 3))
        return;

    usb_get_info(&usb);
    if (usb.ff_cfg[0] && (usb.kind != USB_dir)) {
        text = usb_ff_cfg_text();
    } else if (usb.ff_cfg[0]) {
        snprintf(path, sizeof(path), "%s/%s", usb_path, usb.ff_cfg);
        text = ff_cfg_text(path);
    }
    if (text == NULL) {
        frame(y, x, w, h, "FF.CFG - absent", COLOR_PAIR(CP_off),
              COLOR_PAIR(CP_off));
        return;
    }

    frame(y, x, w, h, usb.ff_cfg, COLOR_PAIR(CP_text), COLOR_PAIR(CP_text));
    /* In an image or on a disk, the corner names that. */
    if (usb.kind == USB_dir)
        snprintf(path, sizeof(path), "%s/%s", usb_path_name, usb.ff_cfg);
    else
        snprintf(path, sizeof(path), "%s", usb_path_name);

    for (p = text; *p != '\0'; p += strcspn(p, "\n"), p += (*p == '\n'))
        nr_lines++;
    ff_cfg_rows = rows;
    if (ff_cfg_top > nr_lines - rows)
        ff_cfg_top = nr_lines - rows;
    if (ff_cfg_top < 0)
        ff_cfg_top = 0;

    /* The scroll bar first: the '>' of a line cut short goes over it. */
    if (nr_lines > rows)
        scroll_bar(y, x, w, h, ff_cfg_top, nr_lines);
    for (row = 0, p = text; row < ff_cfg_top; row++)
        p += strcspn(p, "\n") + 1;
    for (row = 0; (row < rows) && (*p != '\0'); row++) {
        put_line(y + 1 + row, x + 1, w - 2, BRIGHT(CP_value), p,
                 strcspn(p, "\n"));
        p += strcspn(p, "\n");
        if (*p == '\n')
            p++;
    }

    /* The arrows that say there is more take the end of the bottom frame. */
    if (nr_lines > rows)
        keys = page_keys(y + h - 1, x, w, "Ctrl");
    path_label(y + h - 1, x, keys ? w - keys - 1 : w, path);
    scroll_marks(y, x, h, ff_cfg_top > 0, ff_cfg_top + rows < nr_lines);
}

static void redraw(void)
{
    static uint8_t px[OLED_W * OLED_MAX_H];
    struct oled_view v;
    int style, cw, ch, dh, y, kw, sw, bottom = LINES - 1;

    oled_get_view(&v, px);
    style = style_in_use(v.height);
    display_cells(style, v.height, &cw, &ch);
    dh = ch + 2;

    erase();

    draw_display(1, 0, style, (cw + 2 > COLS) || (1 + dh > bottom), &v, px);
    draw_ff_cfg(1, cw + 2, COLS - cw - 2, (1 + dh > bottom) ? bottom - 1 : dh);
    draw_bars();

    /* Controls as wide as they need, then Status and Flash mem sharing the
     * rest, below the display; the log below them at full width, showing
     * its newest lines. */
    y = 1 + dh;
    kw = (keys_width() < COLS) ? keys_width() : COLS;
    sw = (COLS - kw) / 2;
    if (sw < STATUS_MIN_W)
        sw = (COLS - kw < STATUS_MIN_W) ? COLS - kw : STATUS_MIN_W;
    draw_keys(y, 0, kw);
    draw_status(y, kw, sw);
    draw_flash(y, kw + sw, COLS - kw - sw);
    y += PANE_ROWS + 2;
    draw_log(y, 0, COLS, bottom - y);

    draw_dialog();
    refresh();
}

/*
 * The quit dialog.
 */

/* Dims what the shadow of a dialog at (@y,@x) of @w by @h falls on: one row
 * below it and two columns to its right, as in Turbo Vision. */
static void shadow(int y, int x, int w, int h)
{
    int sy, sx;

    for (sy = y + 1; sy <= y + h; sy++) {
        for (sx = x + 2; sx <= x + w + 1; sx++) {
            cchar_t c;
            wchar_t wc[CCHARW_MAX];
            attr_t a;
            short pair;
            bool inside = (sy < y + h) && (sx < x + w);
            if (inside || (sy >= LINES) || (sx >= COLS))
                continue;
            if ((mvin_wch(sy, sx, &c) == ERR)
                || (getcchar(&c, wc, &a, &pair, NULL) == ERR))
                continue;
            setcchar(&c, wc, (COLORS >= 16) ? 0 : A_BOLD,
                     CP_shadow + ((COLORS >= 16) ? CP_BRIGHT : 0), NULL);
            mvadd_wch(sy, sx, &c);
        }
    }
}

/* A Turbo Vision button: its face, and a black shadow on the dialog's gray:
 * the lower half of the cell to the right, the upper half of the cells
 * below, offset by one. */
static void button(int y, int x, attr_t attr, const char *text)
{
    static const wchar_t right[] = { 0x2584, 0 };
    wchar_t below[16];
    int i, n = strlen(text);

    put(y, x, n, attr, "%s", text);
    if (plain())
        return; /* half blocks, which ASCII has nothing for */
    put_wide(y, x + n, COLOR_PAIR(CP_dialog), right, 1);
    for (i = 0; i < n; i++)
        below[i] = 0x2580;
    put_wide(y + 1, x + 1, COLOR_PAIR(CP_dialog), below, n);
}

/* A box of @w by @h in the middle of the screen, @inside, with a double-line
 * frame in @frame_attr with @title in it; returns its top left corner in
 * @py, @px. */
static void colored_box(int w, int h, const char *title, attr_t inside,
                        attr_t frame_attr, int *py, int *px)
{
    static const wchar_t dual[6] = {
        0x2554, 0x2557, 0x255a, 0x255d, 0x2550, 0x2551 };
    int y, x, i;
    wchar_t line[256];

    if (w > (int)ARRAY_SIZE(line))
        w = ARRAY_SIZE(line);
    y = (LINES - h) / 2;
    x = (COLS - w) / 2;
    pane_right = -1;
    shadow(y, x, w, h);
    for (i = 0; i < h; i++)
        put(y + i, x, w, inside, "%*s", w, "");

    for (i = 1; i < w-1; i++)
        line[i] = dual[4];
    line[0] = dual[0];
    line[w-1] = dual[1];
    put_wide(y, x, frame_attr, line, w);
    line[0] = dual[2];
    line[w-1] = dual[3];
    put_wide(y + h-1, x, frame_attr, line, w);
    for (i = 1; i < h-1; i++) {
        put_wide(y + i, x, frame_attr, dual + 5, 1);
        put_wide(y + i, x + w-1, frame_attr, dual + 5, 1);
    }
    put(y, x + (w - strlen(title) - 2) / 2, w, frame_attr, " %s ", title);

    *py = y;
    *px = x;
}

/* A gray Turbo Vision dialog of @w by @h in the middle of the screen, with
 * a white double-line frame with @title in it; returns its top left corner
 * in @py, @px. */
static void dialog_box(int w, int h, const char *title, int *py, int *px)
{
    colored_box(w, h, title, COLOR_PAIR(CP_dialog), BRIGHT(CP_dlg_frame), py,
                px);
}

/* A message box in red with @msg, in the middle of the screen. */
static void message_box(const char *title, const char *msg)
{
    int w = strlen(msg) + 8, y, x;

    colored_box(w, 5, title, COLOR_PAIR(CP_warn), BRIGHT(CP_warn), &y, &x);
    put(y + 2, x + 4, w - 5, BRIGHT(CP_warn), "%s", msg);
}

/* The face of button @nr of a dialog: green, or white when highlighted. */
static attr_t button_attr(int nr)
{
    return (dialog_button == nr) ? BRIGHT(CP_button_on)
        : COLOR_PAIR(CP_button);
}

static void draw_quit_dialog(void)
{
    static const char *text = "Exit ffemu?";
    int w = 32, y, x;

    dialog_box(w, 7, "Confirm", &y, &x);
    put(y + 2, x + (w - strlen(text)) / 2, w, COLOR_PAIR(CP_dialog), "%s",
        text);
    button(y + 4, x + 6, button_attr(0), "  Yes  ");
    button(y + 4, x + w - 6 - 6, button_attr(1), "  No  ");
}

/* The displays to choose from, as radio buttons, and what choosing does. */
static void draw_display_dialog(void)
{
    int w = 47, y, x, i;

    dialog_box(w, 12, "Display type", &y, &x);
    for (i = 0; i < DISP_nr; i++)
        put(y + 2 + i, x + 4, w - 5, COLOR_PAIR(CP_dialog)
            | ((i == dialog_display) ? A_BOLD : 0), "(%c) %s",
            (i == dialog_display) ? '*' : ' ', display_label[i]);
    put(y + 7, x + 3, w - 4, COLOR_PAIR(CP_dialog), "%s",
        "Changing the display restarts the device.");
    button(y + 9, x + 10, button_attr(0), "  OK  ");
    button(y + 9, x + w - 10 - 10, button_attr(1), "  Cancel  ");
}

/* The configuration in flash when the display changed, which may not suit
 * the new one: the rows dialog offers to fix it. */
static uint8_t rows_cfg[256];
/* Whether the display has changed, which needs a power cycle anyway. */
static bool rows_restart;

/* The value of option @name in @cfg, into @buf. */
static void option_value(const void *cfg, const char *name, char *buf,
                         size_t size)
{
    unsigned int i, flags;

    for (i = 0; i < emu_flash_nr_options(); i++)
        if (!strcmp(emu_flash_option(i, cfg, buf, size, &flags), name))
            return;
    buf[0] = '\0';
}

static void draw_rows_dialog(void)
{
    uint8_t cfg[256];
    char now[64], then[64], line1[96], line2[96];
    int w, y, x;

    memcpy(cfg, rows_cfg, sizeof(cfg));
    option_value(cfg, "display-type", now, sizeof(now));
    emu_flash_set_oled_rows(cfg, DISP_HEIGHT(config.display));
    option_value(cfg, "display-type", then, sizeof(then));
    snprintf(line1, sizeof(line1), "Flash mem has display-type = %s.", now);
    snprintf(line2, sizeof(line2), "Change it to %s?", then);
    w = ((strlen(line1) > strlen(line2)) ? strlen(line1) : strlen(line2)) + 8;
    if (w < 32)
        w = 32;

    dialog_box(w, 8, "Display type", &y, &x);
    put(y + 2, x + 4, w - 5, COLOR_PAIR(CP_dialog), "%s", line1);
    put(y + 3, x + 4, w - 5, COLOR_PAIR(CP_dialog), "%s", line2);
    button(y + 5, x + w / 2 - 12, button_attr(0), "  Yes  ");
    button(y + 5, x + w / 2 + 4, button_attr(1), "  No  ");
}

/* The display that display-type in flash asks for, once the Flash mem
 * dialog has changed it to one that the display fitted does not suit: the
 * emul dialog offers to fit it instead. */
static int emul_display;

static void draw_emul_dialog(void)
{
    uint8_t cfg[256];
    char now[64], line1[96], line2[96];
    int w, y, x;

    emu_flash_get(cfg);
    option_value(cfg, "display-type", now, sizeof(now));
    snprintf(line1, sizeof(line1), "Flash mem now has display-type = %s.",
             now);
    snprintf(line2, sizeof(line2), "Fit %s instead of %s?",
             display_label[emul_display], display_label[config.display]);
    w = ((strlen(line1) > strlen(line2)) ? strlen(line1) : strlen(line2)) + 8;
    if (w < 32)
        w = 32;

    dialog_box(w, 8, "Display type", &y, &x);
    put(y + 2, x + 4, w - 5, COLOR_PAIR(CP_dialog), "%s", line1);
    put(y + 3, x + 4, w - 5, COLOR_PAIR(CP_dialog), "%s", line2);
    button(y + 5, x + w / 2 - 12, button_attr(0), "  Yes  ");
    button(y + 5, x + w / 2 + 4, button_attr(1), "  No  ");
}

static void draw_dialog(void)
{
    switch (dialog) {
    case DLG_none:
        break;
    case DLG_quit:
        draw_quit_dialog();
        break;
    case DLG_display:
        draw_display_dialog();
        break;
    case DLG_rows:
        draw_rows_dialog();
        break;
    case DLG_flash:
        draw_flash_dialog();
        break;
    case DLG_emul:
        draw_emul_dialog();
        break;
    }
}

/* What a key did in a dialog. */
enum {
    DK_none, DK_taken, DK_quit, DK_display, DK_rows_yes, DK_rows_no,
    DK_emul_yes, DK_emul_no
};

/* Gives @key to the dialog, if one is open. */
static int dialog_key(int key)
{
    if (dialog == DLG_none)
        return DK_none;

    switch (key) {
    case KEY_LEFT: case KEY_RIGHT: case '\t': case KEY_BTAB:
        dialog_button ^= 1;
        return DK_taken;
    }

    if ((dialog == DLG_quit) || (dialog == DLG_rows) || (dialog == DLG_emul)) {
        if ((key == 'y') || (key == 'Y')) {
            dialog_button = 0;
            key = '\n';
        } else if ((key == 'n') || (key == 'N')) {
            dialog_button = 1;
            key = '\n';
        }
    } else if ((key == KEY_UP) && (dialog_display > 0)) {
        dialog_display--;
    } else if ((key == KEY_DOWN) && (dialog_display < DISP_nr - 1)) {
        dialog_display++;
    }

    /* The rows and emul dialogs have no way back: the display or the flash
     * has changed already. */
    if ((dialog == DLG_rows) || (dialog == DLG_emul)) {
        bool yes = (key == '\n') && (dialog_button == 0);
        if ((key != 27) && (key != '\n'))
            return DK_taken;
        if (dialog == DLG_emul) {
            dialog = DLG_none;
            return yes ? DK_emul_yes : DK_emul_no;
        }
        dialog = DLG_none;
        return yes ? DK_rows_yes : DK_rows_no;
    }

    if (key == 27)
        dialog = DLG_none;
    if (key == '\n') {
        int done = (dialog == DLG_quit) ? DK_quit : DK_display;
        dialog = DLG_none;
        return (dialog_button == 0) ? done : DK_taken;
    }
    return DK_taken;
}

/*
 * Keys and the thread.
 */

static void shut_down(void)
{
    endwin();
    stopped = 1;
}

/* The terminal must be back to normal before the program goes. */
static void leave(int act)
{
    shut_down();
    ui_action(act);
    for (;;)
        pause();
}

/* Sets the display color to the one of the ZX Spectrum's colors 1 to 7. */
/* Sets up the bright twin of pair @cp. */
static void bright_twin(int cp)
{
    short fg, bg;

    if ((COLORS < 16) || (cp + CP_BRIGHT >= COLOR_PAIRS)
        || (pair_content(cp, &fg, &bg) == ERR))
        return;
    init_pair(cp + CP_BRIGHT, ((fg >= 0) && (fg < 8)) ? fg + 8 : fg, bg);
}

static void set_display_color(unsigned int zx, bool bright)
{
    static const uint8_t color_of_zx[8] = {
        COLOR_BLACK, COLOR_BLUE, COLOR_RED, COLOR_MAGENTA, COLOR_GREEN,
        COLOR_CYAN, COLOR_YELLOW, COLOR_WHITE
    };

    config.display_color = color_of_zx[zx] | (bright ? 8 : 0);
    if (has_colors()) {
        init_pair(CP_display, color_of_zx[zx], COLOR_BLACK);
        bright_twin(CP_display);
    }
    rc_save();
}

/* Sets the display color if @key is one of 1 to 7, bright, or the same key
 * with Shift, dim, on a US or a Russian layout. */
static bool color_key(int key)
{
    static const char us[] = "!@#$%^&", ru[] = "!\"#;%:?";
    const char *p;

    if ((key >= '1') && (key <= '7')) {
        set_display_color(key - '0', true);
        return true;
    }
    if ((key <= 0) || (key >= 0x80))
        return false;
    if ((p = strchr(us, key)) != NULL)
        set_display_color(p - us + 1, false);
    else if ((p = strchr(ru, key)) != NULL)
        set_display_color(p - ru + 1, false);
    return p != NULL;
}

/* Sets the rendering style if @key is one of its letters, in either case. */
static bool style_key(int key)
{
    const char *p;

    if ((key <= 0) || (key >= 0x80))
        return false;
    p = strchr(STYLE_KEYS, tolower(key));
    if (p == NULL)
        return false;
    config.style = p - STYLE_KEYS + 1;
    rc_save();
    /* The display's cells change size: repaint everything. */
    clearok(stdscr, TRUE);
    return true;
}

/* A character typed as a key: those of a Russian layout that ffemu uses
 * stand for the keys in their places on a US layout, and the rest that are
 * not ASCII for nothing (-1). */
static int key_of_char(wint_t ch)
{
    static const struct {
        wint_t ru;
        char us;
    } map[] = {
        { 0x439, 'q' }, { 0x419, 'Q' }, { 0x446, 'w' }, { 0x426, 'W' },
        { 0x443, 'e' }, { 0x423, 'E' },
        { 0x2116, '#' } /* the numero sign, Shift+3 */
    };
    unsigned int i;

    if (ch < 0x80)
        return ch;
    for (i = 0; i < ARRAY_SIZE(map); i++)
        if (ch == map[i].ru)
            return map[i].us;
    return -1;
}

/* Drops the rest of a key sequence that curses does not know, @c being its
 * first character after the Esc. */
static void drop_sequence(wint_t c)
{
    unsigned int n;

    for (n = 1; n < 32; n++) {
        if ((n > 1) && (c >= 0x40) && (c <= 0x7e))
            break;
        if (get_wch(&c) == ERR)
            break;
    }
}

/* Fits display @display, if another than the one fitted, and power-cycles
 * the device; first asking to change display-type in flash if that does not
 * suit the display, even the one fitted already. */
static void set_display(int display)
{
    rows_restart = (display != config.display);
    if (rows_restart) {
        config.display = display;
        rc_save();
    }
    emu_flash_get(rows_cfg);
    if (!emu_flash_display_fits(rows_cfg, DISP_HEIGHT(display))) {
        dialog = DLG_rows;
        dialog_button = 0;
        return;
    }
    if (rows_restart)
        leave(KEY_ACT_reset);
}

/*
 * The Flash mem dialog: the options of the configuration in flash whose
 * effect ffemu can show, edited as FF.CFG has them. OK writes those changed
 * into flash and power-cycles the device.
 */

enum { FV_radio, FV_check, FV_input, FV_ok, FV_cancel };

/* The width of each column, so that the dialog with its shadow fits 99
 * columns; and in each, where the fields of input lines start. */
static const int fd_col_w[3] = { 31, 31, 26 };
static int fd_field[3];
#define FD_W(v) fd_col_w[(v)->col]

#define FD_ITEMS 8

static struct fview {
    int type, col;
    const char *label;  /* '~' around the hotkey letter; NULL for none */
    const char *opt;    /* the option it edits */
    const char *help;   /* for the bottom line */
    /* Radio buttons: the values; check boxes: what each checks, a suffix or
     * flag of @opt, or @opt itself as yes or no if @boolean. */
    const char *items[FD_ITEMS];
    bool boolean, gap;  /* gap: an empty row follows */
    /* Laid out, and set from flash, when the dialog opens. */
    int y, x;
    unsigned int shown; /* check boxes: the items shown */
    int cur;            /* the chosen radio button, or check box in focus */
    unsigned int on;    /* the check boxes checked */
    char extra[64];     /* radio buttons: a value from flash that none has */
    char raw[64];       /* a boolean that is neither yes nor no */
    char text[64];      /* an input line */
} fviews[] = {
    { FV_radio, 0, "display-~t~ype", "display-type",
      "Display: found by itself, or an OLED of 32 or 64 rows",
      { "auto", "oled-128x32", "oled-128x64" }, false, true },
    { FV_check, 0, "display-type suffi~x~es", "display-type",
      "OLED view: turned 180 degrees, flipped, narrowed to the Gotek's "
      "cutout, inverse",
      { "rotate", "hflip", "narrow", "narrower", "inverse", "ztech",
        "slow" }, false, true },
    { FV_radio, 0, "oled-~f~ont", "oled-font",
      "OLED font: narrow 6x13, or wide 8x16",
      { "6x13", "8x16" }, false, true },
    { FV_input, 0, "~d~isplay-order", "display-order",
      "Rows top down: content 0-3, 7 for blank, d for double height; or "
      "default", { NULL }, false, false },
    { FV_input, 0, "display-~o~ff-secs", "display-off-secs",
      "Display off after seconds without activity: 0 always off, 255 never",
      { NULL }, false, false },
    { FV_input, 0, "display-scroll-~r~ate", "display-scroll-rate",
      "Long names scroll a step every so many milliseconds, 100 or more",
      { NULL }, false, false },
    { FV_input, 0, "display-scroll-~p~ause", "display-scroll-pause",
      "Pause at both ends of a scroll, milliseconds: 0 scrolls endlessly",
      { NULL }, false, false },
    { FV_input, 0, "nav-scroll-r~a~te", "nav-scroll-rate",
      "While navigating, long names scroll a step every so many "
      "milliseconds", { NULL }, false, false },
    { FV_input, 0, "nav-scroll-pa~u~se", "nav-scroll-pause",
      "While navigating, pause before a long name scrolls, milliseconds",
      { NULL }, false, false },
    { FV_radio, 1, "nav-~m~ode", "nav-mode",
      "Navigation: native through images and folders, indexed through "
      "DSKA0000..., default by HxC config", { "default", "indexed", "native" },
      false, true },
    { FV_radio, 1, "folder-~s~ort", "folder-sort",
      "Sort folders always (big ones may be cut), never (FAT order), or "
      "small ones only", { "always", "never", "small" }, false, true },
    { FV_radio, 1, "sort-pr~i~ority", "sort-priority",
      "Folders before files, files before folders, or no difference",
      { "folders", "files", "none" }, false, true },
    { FV_check, 1, NULL, "nav-loop",
      "Wrap around at the first and the last slot or folder entry",
      { "nav-~l~oop" }, true, true },
    { FV_input, 1, "autos~e~lect-file-secs", "autoselect-file-secs",
      "Open the current file after so many seconds: 0 never",
      { NULL }, false, false },
    { FV_input, 1, "autoselect-folder-se~c~s", "autoselect-folder-secs",
      "Open the current folder after so many seconds: 0 never",
      { NULL }, false, false },
    { FV_input, 1, "i~n~dexed-prefix", "indexed-prefix",
      "Image name prefix in indexed mode, up to 7 characters",
      { NULL }, false, false },
    { FV_radio, 2, "t~w~obutton-action", "twobutton-action",
      "Two buttons: Prev/Next with both for slot 0 or eject, +10/+1, or "
      "rotary", { "zero", "eject", "rotary", "rotary-fast", "htu" },
      false, false },
    { FV_check, 2, NULL, "twobutton-action",
      "Swap the two buttons", { "re~v~erse" }, false, true },
    { FV_radio, 2, "ima~g~e-on-startup", "image-on-startup",
      "Image at startup: the last selected, static from INIT_A.CFG, or the "
      "first", { "last", "static", "init" }, false, false },
    { FV_check, 2, NULL, "ejected-on-startup",
      "Start with the image ejected", { "e~j~ected-on-startup" }, true, true },
    { FV_input, 2, "notif~y~-volume", "notify-volume",
      "Volume of the insert, eject and slot beeps: 0 to 15",
      { NULL }, false, false },
    { FV_check, 2, NULL, "notify-volume",
      "Beep the number of the slot when an image is mounted",
      { "slotnr" }, false, true },
    { FV_ok, 0, NULL, NULL,
      "Write the changed options into flash, and power-cycle the device",
      { NULL }, false, false },
    { FV_cancel, 0, NULL, NULL, "Close, leaving flash as it is",
      { NULL }, false, false }
};
#define FD_NR ARRAY_SIZE(fviews)

/* The options that the dialog edits, and their values when it opened. */
static const char * const fd_opts[] = {
    "display-type", "oled-font", "display-order", "display-off-secs",
    "display-scroll-rate", "display-scroll-pause", "nav-scroll-rate",
    "nav-scroll-pause", "nav-mode", "folder-sort", "sort-priority",
    "nav-loop", "autoselect-file-secs", "autoselect-folder-secs",
    "indexed-prefix", "twobutton-action", "image-on-startup",
    "ejected-on-startup", "notify-volume"
};
static char fd_orig[ARRAY_SIZE(fd_opts)][64];

static int fd_focus, fd_w, fd_h;
static char fd_error[160];

/* @s without its '~' marks, into @buf. */
static const char *unmarked(const char *s, char *buf, size_t size)
{
    size_t n = 0;

    for (; (*s != '\0') && (n < size - 1); s++)
        if (*s != '~')
            buf[n++] = *s;
    buf[n] = '\0';
    return buf;
}

/* The hotkey letter in @s, or 0. */
static int hotkey_of(const char *s)
{
    const char *p = s ? strchr(s, '~') : NULL;

    return p ? tolower((unsigned char)p[1]) : 0;
}

/* @s at (@y,@x), its letter between '~' marks in @hot. */
static void put_marked(int y, int x, int w, attr_t attr, attr_t hot,
                       const char *s)
{
    bool in = false;

    for (; (*s != '\0') && (w > 0); s++) {
        if (*s == '~') {
            in = !in;
            continue;
        }
        put(y, x++, 1, in ? hot : attr, "%c", *s);
        w--;
    }
}

static int fd_nr_items(const struct fview *v)
{
    int n = 0;

    while ((n < FD_ITEMS) && (v->items[n] != NULL))
        n++;
    return n;
}

/* The rows of a cluster: radio buttons with the value from flash that none
 * has, or the check boxes shown. */
static int fd_rows(const struct fview *v)
{
    int n = 0, i;

    if (v->type == FV_radio)
        return fd_nr_items(v) + (v->extra[0] ? 1 : 0);
    for (i = 0; i < FD_ITEMS; i++)
        n += (v->shown >> i) & 1;
    return n;
}

/* The item of check box row @row. */
static int fd_item_of_row(const struct fview *v, int row)
{
    int i;

    for (i = 0; i < FD_ITEMS; i++)
        if (((v->shown >> i) & 1) && (row-- == 0))
            return i;
    return 0;
}

static struct fview *fd_find(int type, const char *opt)
{
    unsigned int i;

    for (i = 0; i < FD_NR; i++)
        if ((fviews[i].type == type) && fviews[i].opt
            && !strcmp(fviews[i].opt, opt))
            return &fviews[i];
    return NULL;
}

/* The value of radio buttons @v as chosen. */
static const char *fd_radio_value(const struct fview *v)
{
    return (v->cur < fd_nr_items(v)) ? v->items[v->cur] : v->extra;
}

static void fd_radio_set(struct fview *v, const char *value)
{
    int i;

    for (i = 0; i < fd_nr_items(v); i++) {
        if (!strcmp(v->items[i], value)) {
            v->cur = i;
            return;
        }
    }
    snprintf(v->extra, sizeof(v->extra), "%s", value);
    v->cur = fd_nr_items(v);
}

/* Sets check box @token of the one for @opt, if it is there. */
static void fd_check_set(const char *opt, const char *token)
{
    struct fview *v = fd_find(FV_check, opt);
    char item[32];
    int i;

    for (i = 0; v && (i < fd_nr_items(v)); i++) {
        if (!strcmp(unmarked(v->items[i], item, sizeof(item)), token)) {
            v->on |= 1u << i;
            v->shown |= 1u << i;
        }
    }
}

/* Sets the dialog up from @value, the value of option @opt in flash. */
static void fd_load(const char *opt, const char *value)
{
    char s[64], *p, *next;
    size_t n = strnlen(value, sizeof(s) - 1);
    struct fview *v;

    memcpy(s, value, n);
    s[n] = '\0';

    if (!strcmp(opt, "display-type")) {
        /* oled-128xNN, then its suffixes. */
        if (!strncmp(s, "oled-128x", 9) && (strlen(s) >= 11)) {
            for (p = s + 11; *p == '-'; p = next) {
                next = strchr(p + 1, '-');
                if (next)
                    *next = '\0';
                fd_check_set(opt, p + 1);
                if (!next)
                    break;
                *next = '-';
            }
            s[11] = '\0';
        }
        fd_radio_set(fd_find(FV_radio, opt), s);
        return;
    }
    if (!strcmp(opt, "twobutton-action") || !strcmp(opt, "notify-volume")) {
        /* A value, then flags after commas; or a raw number. */
        if (strncmp(s, "0x", 2)) {
            for (p = strchr(s, ','); p != NULL; p = next) {
                *p++ = '\0';
                next = strchr(p, ',');
                if (next)
                    *next = '\0';
                fd_check_set(opt, p);
                if (next)
                    *next = ',';
            }
        }
        if ((v = fd_find(FV_radio, opt)) != NULL)
            fd_radio_set(v, s);
        else if ((v = fd_find(FV_input, opt)) != NULL)
            snprintf(v->text, sizeof(v->text), "%s", s);
        return;
    }
    if ((v = fd_find(FV_radio, opt)) != NULL) {
        fd_radio_set(v, s);
    } else if ((v = fd_find(FV_check, opt)) != NULL) {
        if (!strcmp(s, "yes"))
            v->on = 1;
        else if (strcmp(s, "no"))
            snprintf(v->raw, sizeof(v->raw), "%s", s);
    } else if ((v = fd_find(FV_input, opt)) != NULL) {
        /* A string without its quotes. */
        p = s;
        if ((p[0] == '"') && (strlen(p) >= 2)) {
            p[strlen(p) - 1] = '\0';
            p++;
        }
        snprintf(v->text, sizeof(v->text), "%s", p);
    }
}

/* The value of option @opt as the dialog has it, into @buf. */
static void fd_value(const char *opt, char *buf, size_t size)
{
    struct fview *r = fd_find(FV_radio, opt), *c = fd_find(FV_check, opt);
    struct fview *in = fd_find(FV_input, opt);
    const char *base = r ? fd_radio_value(r) : in ? in->text : "";
    char item[32];
    size_t n;
    int i;

    if (c && c->boolean) {
        snprintf(buf, size, "%s", c->raw[0] ? c->raw
                 : c->on ? "yes" : "no");
        return;
    }
    if (!strcmp(opt, "indexed-prefix")) {
        snprintf(buf, size, "\"%s\"", base);
        return;
    }
    snprintf(buf, size, "%s", base);
    if (!c || !strncmp(base, "0x", 2))
        return;
    if (!strcmp(opt, "display-type") && strncmp(base, "oled-", 5))
        return;
    for (i = 0; i < fd_nr_items(c); i++) {
        if (!(c->on & (1u << i)))
            continue;
        n = strlen(buf);
        snprintf(buf + n, size - n, "%s%s",
                 strcmp(opt, "display-type") ? "," : "-",
                 unmarked(c->items[i], item, sizeof(item)));
    }
}

/* What the dialog edits, above its columns. */
static const char * const fd_explanation[2] = {
    "Here are settings stored in the device flash memory.",
    "The values are overwritten by FF.CFG on start."
};

static void fd_layout(void)
{
    int col_y[3] = { 5, 5, 5 }, col_x[3], h = 0, i, n;
    char label[40];
    struct fview *v;

    col_x[0] = 2;
    col_x[1] = col_x[0] + fd_col_w[0] + 2;
    col_x[2] = col_x[1] + fd_col_w[1] + 2;
    fd_field[0] = fd_field[1] = fd_field[2] = 0;
    for (i = 0; i < (int)FD_NR; i++) {
        v = &fviews[i];
        if ((v->type != FV_input) || !v->label)
            continue;
        n = strlen(unmarked(v->label, label, sizeof(label))) + 1;
        if (n > fd_field[v->col])
            fd_field[v->col] = n;
    }

    for (i = 0; i < (int)FD_NR; i++) {
        v = &fviews[i];
        if ((v->type == FV_ok) || (v->type == FV_cancel))
            continue;
        v->x = col_x[v->col];
        v->y = col_y[v->col];
        col_y[v->col] += (v->label && (v->type != FV_input) ? 1 : 0)
            + ((v->type == FV_input) ? 1 : fd_rows(v)) + (v->gap ? 1 : 0);
        if (col_y[v->col] > h)
            h = col_y[v->col];
    }
    fd_w = col_x[2] + fd_col_w[2] + 2;
    fd_h = h + 5;
    fviews[FD_NR - 2].y = fviews[FD_NR - 1].y = fd_h - 4;
    fviews[FD_NR - 2].x = fd_w / 2 - 14;
    fviews[FD_NR - 1].x = fd_w / 2 + 4;
}

static void fd_open(void)
{
    uint8_t cfg[256];
    unsigned int i;

    for (i = 0; i < FD_NR; i++) {
        struct fview *v = &fviews[i];
        v->cur = 0;
        v->on = 0;
        v->extra[0] = v->raw[0] = v->text[0] = '\0';
        /* Check boxes for what ffemu does not emulate show if checked. */
        v->shown = (v->type == FV_check)
            ? (1u << fd_nr_items(v)) - 1 : 0;
        if ((v->type == FV_check) && !strcmp(v->opt, "display-type"))
            v->shown = 0x1f;
    }
    emu_flash_get(cfg);
    for (i = 0; i < ARRAY_SIZE(fd_opts); i++) {
        option_value(cfg, fd_opts[i], fd_orig[i], sizeof(fd_orig[i]));
        fd_load(fd_opts[i], fd_orig[i]);
    }
    fd_layout();
    fd_focus = 0;
    fd_error[0] = '\0';
    dialog = DLG_flash;
}

/* Writes the options changed into flash: TRUE if any were, FALSE if none
 * were or one cannot be, which @fd_error then says. */
static bool fd_apply(void)
{
    uint8_t cfg[256];
    char value[80];
    bool changed = false;
    unsigned int i, j;

    emu_flash_get(cfg);
    for (i = 0; i < ARRAY_SIZE(fd_opts); i++) {
        fd_value(fd_opts[i], value, sizeof(value));
        /* An option that only the hex holds stays as it is if untouched. */
        if (!strcmp(value, fd_orig[i])
            || (!fd_orig[i][0] && !strcmp(value, "\"\"")))
            continue;
        if (emu_flash_set_option(cfg, fd_opts[i], value) != EMU_SET_ok) {
            snprintf(fd_error, sizeof(fd_error),
                     "%s = %s: not a value that it takes", fd_opts[i],
                     value);
            for (j = 0; j < FD_NR; j++)
                if (fviews[j].opt && !strcmp(fviews[j].opt, fd_opts[i]))
                    break;
            fd_focus = j;
            return false;
        }
        changed = true;
    }
    if (changed)
        emu_flash_save(cfg, emu_flash_cfg_size());
    return changed;
}

/* Opens the emul dialog if display-type in flash has changed and no longer
 * suits the display fitted, while another emulated display would suit it:
 * the same chip with the other number of rows. */
static bool fd_offer_display(const char *old_type)
{
    uint8_t cfg[256];
    char type[64];
    unsigned int rows;
    int d;

    emu_flash_get(cfg);
    option_value(cfg, "display-type", type, sizeof(type));
    if (!strcmp(type, old_type)
        || emu_flash_display_fits(cfg, DISP_HEIGHT(config.display)))
        return false;
    rows = emu_flash_display_fits(cfg, 64) ? 64
        : emu_flash_display_fits(cfg, 32) ? 32 : 0;
    for (d = 0; d < DISP_nr; d++)
        if ((DISP_HEIGHT(d) == rows)
            && (DISP_IS_SH1106(d) == DISP_IS_SH1106(config.display)))
            break;
    if (d == DISP_nr)
        return false;
    emul_display = d;
    dialog = DLG_emul;
    dialog_button = 0;
    return true;
}

/* Whether input line @v takes a plain number. */
static bool fd_numeric(const struct fview *v)
{
    static const char * const opts[] = {
        "display-off-secs", "display-scroll-rate", "display-scroll-pause",
        "nav-scroll-rate", "nav-scroll-pause", "autoselect-file-secs",
        "autoselect-folder-secs", "notify-volume"
    };
    unsigned int i;

    for (i = 0; i < ARRAY_SIZE(opts); i++)
        if (!strcmp(v->opt, opts[i]))
            return true;
    return false;
}

/* Whether the dialog with its shadow fits the window. */
static bool fd_fits(void)
{
    return (fd_w + 2 <= COLS) && (fd_h + 1 <= LINES);
}

static void fd_key(int key)
{
    struct fview *v = &fviews[fd_focus];
    int n = (v->type == FV_radio) || (v->type == FV_check) ? fd_rows(v) : 0;
    uint8_t cfg[256];
    char old_type[64];
    unsigned int i;

    /* Only the message is on the screen: nothing to edit blindly. */
    if (!fd_fits()) {
        if ((key == 27) || (key == '\n'))
            dialog = DLG_none;
        return;
    }

    fd_error[0] = '\0';
    switch (key) {
    case 27:
        dialog = DLG_none;
        return;
    case '\t':
        fd_focus = (fd_focus + 1) % FD_NR;
        return;
    case KEY_BTAB:
        fd_focus = (fd_focus + FD_NR - 1) % FD_NR;
        return;
    case '\n':
        if (v->type == FV_cancel) {
            dialog = DLG_none;
            return;
        }
        emu_flash_get(cfg);
        option_value(cfg, "display-type", old_type, sizeof(old_type));
        if (fd_apply()) {
            dialog = DLG_none;
            if (!fd_offer_display(old_type))
                leave(KEY_ACT_reset);
        } else if (!fd_error[0]) {
            dialog = DLG_none;
        }
        return;
    case KEY_UP:
        if ((n != 0) && (v->cur > 0))
            v->cur--;
        else if (n == 0)
            fd_focus = (fd_focus + FD_NR - 1) % FD_NR;
        return;
    case KEY_DOWN:
        if ((n != 0) && (v->cur < n - 1))
            v->cur++;
        else if (n == 0)
            fd_focus = (fd_focus + 1) % FD_NR;
        return;
    case KEY_LEFT: case KEY_RIGHT:
        if ((v->type == FV_ok) || (v->type == FV_cancel))
            fd_focus = (v->type == FV_ok) ? FD_NR - 1 : FD_NR - 2;
        return;
    case ' ':
        if (v->type == FV_check) {
            v->on ^= 1u << fd_item_of_row(v, v->cur);
            v->raw[0] = '\0';
            return;
        }
        break;
    case KEY_BACKSPACE: case 127: case 8:
        if ((v->type == FV_input) && v->text[0])
            v->text[strlen(v->text) - 1] = '\0';
        return;
    }

    if ((key < ' ') || (key > '~'))
        return;
    if (v->type == FV_input) {
        size_t len = strlen(v->text);
        if (fd_numeric(v) && !isdigit(key))
            return;
        if ((int)len < FD_W(v) - fd_field[v->col] - 2) {
            v->text[len] = key;
            v->text[len + 1] = '\0';
        }
        return;
    }
    for (i = 0; i < FD_NR; i++) {
        if ((hotkey_of(fviews[i].label) == tolower(key))
            || (!fviews[i].label
                && (hotkey_of(fviews[i].items[0]) == tolower(key)))) {
            fd_focus = i;
            return;
        }
    }
}

static void draw_flash_dialog(void)
{
    static const wchar_t radio_on[] = { '(', 0x2022, ')' };
    attr_t label, item;
    int y0, x0, i, r, y, x;

    if (!fd_fits()) {
        message_box("Flash mem", "Window too small for dialog.");
        return;
    }
    dialog_box(fd_w, fd_h, "Flash mem", &y0, &x0);
    for (i = 0; i < (int)ARRAY_SIZE(fd_explanation); i++)
        put(y0 + 2 + i, x0 + 2, fd_w - 4, COLOR_PAIR(CP_dialog), "%s",
            fd_explanation[i]);
    for (i = 0; i < (int)FD_NR; i++) {
        struct fview *v = &fviews[i];
        bool focused = (i == fd_focus);
        y = y0 + v->y;
        x = x0 + v->x;
        label = focused ? BRIGHT(CP_dlg_frame)
            : COLOR_PAIR(CP_dialog);

        switch (v->type) {
        case FV_radio:
        case FV_check:
            if (v->label)
                put_marked(y++, x, FD_W(v), label,
                           BRIGHT(CP_dlg_hot), v->label);
            for (r = 0; r < fd_rows(v); r++, y++) {
                int k = (v->type == FV_radio) ? r : fd_item_of_row(v, r);
                item = (focused && (r == v->cur))
                    ? BRIGHT(CP_cluster_hi)
                    : COLOR_PAIR(CP_cluster);
                put(y, x, FD_W(v), COLOR_PAIR(CP_cluster), "%*s", FD_W(v),
                    "");
                if (v->type == FV_radio) {
                    if (r == v->cur)
                        put_wide(y, x + 1, item, radio_on, 3);
                    else
                        put(y, x + 1, 3, item, "( )");
                } else {
                    put(y, x + 1, 3, item, "[%c]",
                        v->raw[0] ? '?' : (v->on >> k) & 1 ? 'X' : ' ');
                }
                if (k < fd_nr_items(v))
                    put_marked(y, x + 5, FD_W(v) - 6, item,
                               BRIGHT(CP_cluster_hot),
                               v->items[k]);
                else
                    put(y, x + 5, FD_W(v) - 6, item, "%s (from flash)",
                        v->extra);
            }
            break;
        case FV_input: {
            /* The end of the text, if it is longer than the field, and
             * then the cursor, in the field. */
            int fx = x + fd_field[v->col], fw = FD_W(v) - fd_field[v->col];
            int room = fw - 1 - (focused ? 1 : 0);
            int len = strlen(v->text);
            const char *t = v->text + ((len > room) ? len - room : 0);
            put_marked(y, x, fd_field[v->col] - 1, label,
                       BRIGHT(CP_dlg_hot), v->label);
            put(y, fx, fw, BRIGHT(CP_input), " %-*s", fw - 1, t);
            if (focused)
                put(y, fx + 1 + strlen(t), 1, COLOR_PAIR(CP_ctl_on), " ");
            break;
        }
        case FV_ok:
            button(y, x, focused ? BRIGHT(CP_button_on)
                   : (fd_focus == (int)FD_NR - 1) ? COLOR_PAIR(CP_button)
                   : BRIGHT(CP_button_def), "   OK   ");
            break;
        case FV_cancel:
            button(y, x, focused ? BRIGHT(CP_button_on)
                   : COLOR_PAIR(CP_button), " Cancel ");
            break;
        }
    }

    /* What the option in focus is, or what is wrong, on the bottom line. */
    put(LINES - 1, 0, COLS, COLOR_PAIR(CP_bar), "%*s", COLS, "");
    put(LINES - 1, 1, COLS - 2, fd_error[0] ? COLOR_PAIR(CP_hotkey)
        : COLOR_PAIR(CP_bar), "%s",
        fd_error[0] ? fd_error : fviews[fd_focus].help);
}

static void handle_key(int key)
{
    unsigned int i;

    if ((key == '\r') || (key == KEY_ENTER))
        key = '\n';

    /* Ctrl+L, or the window resized: the next refresh repaints everything,
     * for a terminal that has lost part of the screen. */
    if ((key == ('L' & 0x1f)) || (key == KEY_RESIZE)) {
        clearok(stdscr, TRUE);
        return;
    }

    if (dialog == DLG_flash) {
        fd_key(key);
        return;
    }

    switch (dialog_key(key)) {
    case DK_taken:
        return;
    case DK_quit:
        leave(KEY_ACT_quit);
    case DK_display:
        set_display(dialog_display);
        return;
    case DK_rows_yes:
        emu_flash_set_oled_rows(rows_cfg, DISP_HEIGHT(config.display));
        emu_flash_save(rows_cfg, emu_flash_cfg_size());
        leave(KEY_ACT_reset);
    case DK_rows_no:
        if (rows_restart)
            leave(KEY_ACT_reset);
        return;
    case DK_emul_yes:
        config.display = emul_display;
        rc_save();
        leave(KEY_ACT_reset);
    case DK_emul_no:
        leave(KEY_ACT_reset);
    }

    if ((key == key_ctrl_pgup) || (key == key_ctrl_pgdn)) {
        int step = (ff_cfg_rows > 1) ? ff_cfg_rows - 1 : 1;
        ff_cfg_top += (key == key_ctrl_pgdn) ? step : -step;
        return;
    }

    if ((key == KEY_PPAGE) || (key == KEY_NPAGE)) {
        /* A page less a line, which stays in view. */
        flash_top += (key == KEY_NPAGE) ? PANE_ROWS - 1 : 1 - PANE_ROWS;
        return;
    }

    if (key == FLASH_KEY) {
        fd_open();
        return;
    }

    if (key == DISPLAY_KEY) {
        dialog = DLG_display;
        dialog_button = 0;
        dialog_display = config.display;
        return;
    }

    for (i = 0; i < KEY_ACT_nr; i++) {
        if (key != key_code[i])
            continue;
        if (i == KEY_ACT_quit) {
            dialog = DLG_quit;
            dialog_button = 0;
        } else if (i == KEY_ACT_reset) {
            leave(i);
        } else {
            ui_action(i);
        }
        return;
    }

    if (!style_key(key))
        color_key(key);
}

static void *tui_thread(void *unused)
{
    int key, rc, i;
    wint_t ch;

    initscr();
    cbreak();
    noecho();
    nonl();
    keypad(stdscr, TRUE);
    prepare_fatal_seq();
    key_ctrl_pgup = ctrl_key("kPRV5", "\033[5;5~", KEY_MAX + 0x101);
    key_ctrl_pgdn = ctrl_key("kNXT5", "\033[6;5~", KEY_MAX + 0x102);
    curs_set(0);
    /* Long enough for the rest of a key's sequence to come late, as it can
     * over ssh to a virtual machine; a lone Esc is held back that long. */
    set_escdelay(200);
    timeout(FRAME_MS);

    if (has_colors()) {
        start_color();
        /* Else curses takes white on black for what nothing is drawn on. */
        use_default_colors();
        init_pair(CP_text, COLOR_WHITE, COLOR_BLUE);
        init_pair(CP_value, COLOR_YELLOW, COLOR_BLUE);
        init_pair(CP_display, config.display_color & 7, COLOR_BLACK);
        init_pair(CP_dframe, COLOR_WHITE, COLOR_BLACK);
        init_pair(CP_path, COLOR_CYAN, COLOR_BLUE);
        init_pair(CP_bar, COLOR_BLACK, COLOR_WHITE);
        init_pair(CP_hotkey, COLOR_RED, COLOR_WHITE);
        init_pair(CP_warn, COLOR_WHITE, COLOR_RED);
        init_pair(CP_good, COLOR_GREEN, COLOR_BLUE);
        init_pair(CP_ctl_off, COLOR_BLUE, COLOR_WHITE);
        init_pair(CP_ctl_latch, COLOR_RED, COLOR_WHITE);
        /* Bright yellow is color 11 where the terminal has 16 colors. */
        init_pair(CP_ctl_on, COLOR_BLACK, (COLORS >= 16) ? 11 : COLOR_YELLOW);
        init_pair(CP_crop, COLOR_GREEN, COLOR_BLUE);
        init_pair(CP_note, COLOR_RED, COLOR_BLACK);
        init_pair(CP_focus, COLOR_WHITE, COLOR_BLUE);
        init_pair(CP_dialog, COLOR_BLACK, COLOR_WHITE);
        init_pair(CP_dlg_frame, COLOR_WHITE, COLOR_WHITE);
        init_pair(CP_button, COLOR_BLACK, COLOR_GREEN);
        init_pair(CP_button_on, COLOR_WHITE, COLOR_GREEN);
        init_pair(CP_shadow, COLOR_BLACK, COLOR_BLACK);
        init_pair(CP_dark, (COLORS >= 16) ? 8 : COLOR_BLACK, COLOR_BLUE);
        init_pair(CP_bad, COLOR_RED, COLOR_BLUE);
        init_pair(CP_off, COLOR_WHITE, (COLORS >= 16) ? 8 : COLOR_BLACK);
        init_pair(CP_scroll, COLOR_BLUE, COLOR_CYAN);
        init_pair(CP_raw, COLOR_MAGENTA, COLOR_BLUE);
        init_pair(CP_bar_crop, COLOR_GREEN, COLOR_WHITE);
        init_pair(CP_dlg_hot, COLOR_YELLOW, COLOR_WHITE);
        init_pair(CP_cluster, COLOR_BLACK, COLOR_CYAN);
        init_pair(CP_cluster_hi, COLOR_WHITE, COLOR_CYAN);
        init_pair(CP_cluster_hot, COLOR_YELLOW, COLOR_CYAN);
        init_pair(CP_input, COLOR_WHITE, COLOR_BLUE);
        init_pair(CP_button_def, COLOR_CYAN, COLOR_GREEN);
        for (i = 1; i < CP_nr; i++)
            bright_twin(i);
    }

    for (;;) {
        if (stop_request) {
            shut_down();
            for (;;)
                pause();
        }

        /* Waits for a key for up to one frame time. Characters come as
         * Unicode, since a layout sets what Shift+digit gives. */
        rc = get_wch(&ch);
        while (rc != ERR) {
            timeout(0);
            if ((rc == OK) && (ch == 27)) {
                /* The Esc key alone, or a key sequence that curses does not
                 * know for this terminal. */
                wint_t next;
                if (get_wch(&next) != ERR) {
                    drop_sequence(next);
                    rc = get_wch(&ch);
                    continue;
                }
            }
            key = (rc == KEY_CODE_YES) ? (int)ch : key_of_char(ch);
            if (key >= 0)
                handle_key(key);
            rc = get_wch(&ch);
        }
        timeout(FRAME_MS);

        ui_poll();
        redraw();
    }

    return NULL;
}

void tui_start(void)
{
    started = 1;
    if (pthread_create(&thread, NULL, tui_thread, NULL) != 0) {
        started = 0;
        cpu_quit(3, "ffemu: cannot create a thread");
    }
}

void tui_stop(void)
{
    unsigned int i;

    if (!started || stopped)
        return;

    if (pthread_equal(pthread_self(), thread)) {
        shut_down();
        return;
    }

    /* Curses is not thread-safe: have its thread leave it. */
    stop_request = 1;
    for (i = 0; (i < 500) && !stopped; i++)
        usleep(1000);
    if (!stopped)
        shut_down();
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

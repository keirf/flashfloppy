/*
 * script.c
 *
 * Drives ffemu from commands on standard input, in place of the terminal
 * user interface. This is how ffemu is tested where there is no terminal;
 * the commands are listed by "ffemu --help".
 *
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "host.h"

/* The names of the actions in the "key" command. */
static const char * const action_name[KEY_ACT_nr] = {
    [KEY_ACT_select] = "select",
    [KEY_ACT_left] = "left",
    [KEY_ACT_right] = "right",
    [KEY_ACT_cw] = "cw",
    [KEY_ACT_ccw] = "ccw",
    [KEY_ACT_remove] = "remove",
    [KEY_ACT_insert] = "insert",
    [KEY_ACT_reset] = "reset",
    [KEY_ACT_quit] = "quit",
    [KEY_ACT_latch] = "latch",
    [KEY_ACT_relaunch] = "relaunch"
};

static unsigned int log_printed;

/* When the current sleep ends, or zero. A reset by the firmware itself cuts
 * a sleep short: the rest of it is then slept after the power cycle. */
static volatile uint64_t sleep_end;
/* Whether commands come from stdin, the user interface not running. */
static bool started;

unsigned int script_sleep_left(void)
{
    uint64_t now = emu_time_ns(), end = sleep_end;
    return (end > now) ? (end - now) / 1000000u : 0;
}

/* On the virtual clock, a script lets the firmware run a slice at a time,
 * serving the emulated inputs in between, as ui_poll() does every 2 ms of the
 * host's time otherwise. */
#define VIRT_SLICE_NS 10000000u

/* Lets the firmware run for @ms, keeping the emulated inputs serviced. */
static void run_for(unsigned int ms)
{
    uint64_t next;

    sleep_end = emu_time_ns() + (uint64_t)ms * 1000000u;

    do {
        ui_poll();
        if (cpu_clock_virtual()) {
            next = emu_time_ns() + VIRT_SLICE_NS;
            cpu_run_until((next < sleep_end) ? next : sleep_end);
        } else {
            usleep(2000);
        }
    } while (emu_time_ns() < sleep_end);
    ui_poll();

    sleep_end = 0;
}

static void park(void)
{
    for (;;)
        pause();
}

/* Code point @cp into @p in UTF-8, returning the bytes. */
static unsigned int utf8(char *p, unsigned int cp)
{
    if (cp < 0x80) {
        p[0] = cp;
        return 1;
    }
    if (cp < 0x800) {
        p[0] = 0xc0 | (cp >> 6);
        p[1] = 0x80 | (cp & 0x3f);
        return 2;
    }
    p[0] = 0xe0 | (cp >> 12);
    p[1] = 0x80 | ((cp >> 6) & 0x3f);
    p[2] = 0x80 | (cp & 0x3f);
    return 3;
}

/* Row @y of the LCD @cv into @row, each code as the Unicode character that
 * its ROM's glyph stands for, in UTF-8, a white square for one with none. */
static void lcd_row(const struct lcd_view *cv, unsigned int y, char *row)
{
    unsigned int x, n = 0;

    for (x = 0; x < cv->cols; x++) {
        unsigned int cp = hd44780_char(cv, cv->text[y][x]);
        n += utf8(row + n, !cv->on ? ' ' : cp ? cp : LCD_MISSING);
    }
    row[n] = '\0';
}

/* The display as text: an OLED as its pixels, the ASCII rendering's character
 * for lit; the LED display as its digits, then as pixels in that shape; the
 * LCD as its rows between bars. */
static void dump_display(void)
{
    static uint8_t px[OLED_W * OLED_MAX_H];
    const uint8_t *src = px;
    struct oled_view v;
    struct led_view lv;
    struct lcd_view cv;
    unsigned int x, y, w, h;
    char row[512], text[16];

    if (DISP_IS_LCD(config.display)) {
        hd44780_get_view(&cv);
        printf("display: LCD %ux%u %s backlight %s rom=%s updates=%u\n",
               cv.cols, cv.rows, cv.on ? "on" : "off",
               cv.backlight ? "on" : "off", lcd_rom_name[cv.rom],
               cv.nr_updates);
        for (y = 0; y < cv.rows; y++) {
            lcd_row(&cv, y, row);
            printf("|%s|\n", row);
        }
        return;
    }
    if (DISP_IS_LED(config.display)) {
        char art[3][16];
        led_get_view(&lv);
        led_text(&lv, text, sizeof(text));
        printf("display: LED %u digits %s \"%s\" segments %02x %02x %02x "
               "updates=%u\n", lv.nr_digits, lv.on ? "on" : "off", text,
               lv.seg[0], lv.seg[1], lv.seg[2], lv.nr_updates);
        led_art(&lv, art);
        for (y = 0; y < 3; y++)
            puts(art[y]);
        return;
    }
    oled_get_view(&v, px);
    printf("display: 128x%u %s contrast=%u%s updates=%u\n", v.height,
           v.on ? "on" : "off", v.contrast,
           v.inverse ? " inverse" : "", v.nr_updates);
    w = OLED_W;
    h = v.height;
    if (w > sizeof(row) - 1)
        w = sizeof(row) - 1;
    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++)
            row[x] = src[y*w + x] ? ascii_pixel : '.';
        row[w] = '\0';
        puts(row);
    }
}

static void print_log(void)
{
    unsigned int head = log_head;

    if (head - log_printed > LOG_SIZE)
        log_printed = head - LOG_SIZE;
    for (; log_printed != head; log_printed++)
        putchar(log_ring[log_printed % LOG_SIZE]);
}

/* Where the console output that "wait" searches begins: after its last
 * match. */
static unsigned int log_waited;

/* Whether the console output since the last match contains @text; the next
 * search then begins after this match. */
static bool log_matches(const char *text)
{
    static char buf[LOG_SIZE + 1];
    unsigned int head = log_head, n = 0;
    const char *p;

    if (head - log_waited > LOG_SIZE)
        log_waited = head - LOG_SIZE;
    for (; n != head - log_waited; n++)
        buf[n] = log_ring[(log_waited + n) % LOG_SIZE];
    buf[n] = '\0';
    if ((p = strstr(buf, text)) == NULL)
        return false;
    log_waited += (p - buf) + strlen(text);
    return true;
}

/* Whether @text is on the display: in the LED display's digits as "dump"
 * prints them, or in a row of the LCD; never on an OLED. */
static bool display_matches(const char *text)
{
    char row[512];
    unsigned int y;

    if (DISP_IS_LCD(config.display)) {
        struct lcd_view cv;
        hd44780_get_view(&cv);
        for (y = 0; y < cv.rows; y++) {
            lcd_row(&cv, y, row);
            if (strstr(row, text) != NULL)
                return true;
        }
    } else if (DISP_IS_LED(config.display)) {
        struct led_view lv;
        led_get_view(&lv);
        led_text(&lv, row, sizeof(row));
        return strstr(row, text) != NULL;
    }
    return false;
}

/* Whether the LED display's digits are @hex, the segments of each as a hex
 * byte, as "dump" prints them. */
static bool segments_match(const char *hex)
{
    struct led_view lv;
    unsigned int i, seg;
    int n;

    if (!DISP_IS_LED(config.display))
        return false;
    led_get_view(&lv);
    for (i = 0; i < lv.nr_digits; i++, hex += n)
        if ((sscanf(hex, "%x%n", &seg, &n) != 1) || (seg != lv.seg[i]))
            return false;
    return true;
}

/* Lets the firmware run until the condition @kind on @text holds, checked
 * every 10 ms, or @ms pass, and prints which. */
static void wait_for(unsigned int ms, const char *kind, const char *text)
{
    uint64_t start = emu_time_ns();
    bool ok;

    for (;;) {
        ok = !strcmp(kind, "log") ? log_matches(text)
            : !strcmp(kind, "display") ? display_matches(text)
            : segments_match(text);
        if (ok || (emu_time_ns() - start >= (uint64_t)ms * 1000000u))
            break;
        run_for(10);
    }
    printf("wait %s %s: %s after %u ms\n", kind, text,
           ok ? "ok" : "timed out",
           (unsigned int)((emu_time_ns() - start) / 1000000u));
}

/* The signals of the host computer, active ones in capitals, and the drive
 * as the firmware sees it. */
static void print_fdd(void)
{
    unsigned int in = emu_in_fdd, cyl, side;
    const char *image;
    char ph[5], kept[5];
    int sel;

    if (FFEMU_APPLE2)
        printf("fdd: %s %s, phases %s, kept %s\n",
               (in & EMU_FDD_SEL) ? "DE" : "de",
               (in & EMU_FDD_WRITE) ? "WRQ" : "wrq",
               ui_fdd_phase_text(in, ph),
               ui_fdd_phase_text(ui_fdd_phases(), kept));
    else
        printf("fdd: %s %s %s %s %s\n", (in & EMU_FDD_SEL) ? "DS" : "ds",
               (in & EMU_FDD_MOTOR) ? "MO" : "mo",
               (in & EMU_FDD_WRITE) ? "WRG" : "wrg",
               (in & EMU_FDD_DIR) ? "DIR" : "dir",
               (in & EMU_FDD_SIDE) ? "SID" : "sid");
    emu_fdd_status(&cyl, &side, &sel, &image);
    if (FFEMU_APPLE2)
        printf("drive: cyl %u, %senabled, floppy %s\n", cyl,
               sel ? "" : "not ", image ? image : "none");
    else
        printf("drive: cyl %u, side %u, %sselected, floppy %s\n", cyl, side,
               sel ? "" : "not ", image ? image : "none");
}

/* FF OSD as text: its rows between bars, each code as FF OSD draws it,
 * 0x7F as the checkerboard that its glyph is, in UTF-8. */
static void dump_osd(void)
{
    struct osd_view v;
    unsigned int x, y, n;
    char row[OSD_MAX_COLS * 3 + 1];

    osd_get_view(&v);
    if (!v.present) {
        printf("osd: none\n");
        return;
    }
    printf("osd: %ux%u %s heights=%x updates=%u\n", v.cols, v.rows,
           v.on ? "on" : "off", v.heights, v.nr_updates);
    for (y = 0; y < v.rows; y++) {
        for (x = n = 0; x < v.cols; x++) {
            uint8_t c = v.text[y][x];
            n += utf8(row + n, !v.on ? ' ' : (c == 0x7f) ? 0x2592
                      : ((c < 0x20) || (c > 0x7f)) ? ' ' : c);
        }
        row[n] = '\0';
        printf("|%s|\n", row);
    }
}

/* The section of IMG.CFG that the firmware applies to the image mounted,
 * its lines between bars. */
static void dump_imgcfg(void)
{
    struct imgcfg_view v;
    const char *p;
    size_t n;

    imgcfg_get_view(&v);
    if (v.state == IMGCFG_none) {
        printf("imgcfg: none\n");
        return;
    }
    if (v.state == IMGCFG_no_section) {
        printf("imgcfg: no section applies\n");
        return;
    }
    if (v.header[0] == '\0')
        printf("imgcfg: default section, %u lines\n", v.nr_lines);
    else
        printf("imgcfg: section %s, %u lines\n", v.header, v.nr_lines);
    for (p = v.text; *p != '\0'; p += n, p += (*p == '\n')) {
        n = strcspn(p, "\n");
        printf("|%.*s|\n", (int)(((n != 0) && (p[n-1] == '\r')) ? n - 1 : n),
               p);
    }
}

static void print_status(void)
{
    static uint8_t px[OLED_W * OLED_MAX_H];
    struct oled_view v;
    struct usb_info usb;
    char flash[2048];

    if (DISP_IS_LCD(config.display)) {
        struct lcd_view cv;
        hd44780_get_view(&cv);
        printf("display: HD44780 %ux%u, ROM %s\n", cv.cols, cv.rows,
               lcd_rom_name[cv.rom]);
    } else if (DISP_IS_LED(config.display)) {
        struct led_view lv;
        char text[16];
        unsigned int i;
        led_get_view(&lv);
        led_text(&lv, text, sizeof(text));
        printf("display: %s %u digits",
               (config.display == DISP_tm1651) ? "TM1651" : "74HC164",
               lv.nr_digits);
        /* The shift registers have neither a switch nor a brightness. */
        if (config.display == DISP_tm1651)
            printf(", %s, brightness %u", lv.on ? "on" : "off",
                   lv.brightness);
        printf(", showing \"%s\", segments", text);
        for (i = 0; i < lv.nr_digits; i++)
            printf(" %02x", lv.seg[i]);
        putchar('\n');
    } else {
        oled_get_view(&v, px);
        printf("display: %s 128x%u, driven 128x%u\n", v.chip, v.height,
               v.driven);
    }
    printf("osd: %s\n", config.osd && !DISP_IS_LED(config.display)
           ? "FF OSD 1.9 at 0x10" : "none");
    flash_summary(flash, sizeof(flash));
    printf("flash: %s\n", flash);

    usb_get_info(&usb);
    printf("firmware: %s, %s, on %s\n", emu_fw_version(), emu_fw_target(),
           emu_board_name());
    if (usb.inserted && (usb.kind == USB_dir))
        printf("usb: inserted, FAT%u, short names in %s by %s rules, %s, "
               "%u files, %u dirs, %u left out, "
               "%u case duplicates left out, %u left out for lack of room, "
               "%u names losing characters, "
               "%lu reads, %lu writes, ff.cfg: %s\n", usb.fat_bits,
               usb_sfn_name[config.usb_sfn],
               usb_by_name[(config.usb_sfn == USB_SFN_utf8)
                           ? USB_BY_windows : config.usb_by],
               config.usb_lfn ? "long names" : "no long names",
               usb.nr_files, usb.nr_dirs, usb.nr_skipped, usb.nr_case_dups,
               usb.nr_root_full + usb.nr_drive_full, usb.nr_lacking,
               usb.nr_reads, usb.nr_writes,
               usb.ff_cfg[0] ? usb.ff_cfg : "none");
    else if (usb.inserted)
        printf("usb: inserted, %s of %llu sectors, %lu reads, %lu writes, "
               "ff.cfg: %s\n", (usb.kind == USB_image) ? "image" : "disk",
               (unsigned long long)usb.image_bytes / 512, usb.nr_reads,
               usb.nr_writes, usb.ff_cfg[0] ? usb.ff_cfg : "none");
    else
        printf("usb: removed%s%s\n", usb.error[0] ? ", " : "", usb.error);
    print_fdd();
    printf("buttons: %u, speaker pulses: %u, heap: %u of %u\n",
           emu_in_buttons, emu_out_speaker, emu_arena_used(),
           emu_arena_size());
}

/* Reads one line byte by byte, so that a power cycle, which re-executes the
 * program, finds the rest of the script still unread. */
static bool read_line(char *line, size_t size)
{
    size_t n = 0;
    char c;

    while (read(STDIN_FILENO, &c, 1) == 1) {
        if (c == '\n') {
            line[n] = '\0';
            return true;
        }
        if ((n < size - 1) && (c != '\r'))
            line[n++] = c;
    }

    line[n] = '\0';
    return n != 0;
}

/* What each option takes, a line each: its name, its kind, and its names
 * and its flags after `+`, as the Flash mem dialog offers them. */
static void print_options(void)
{
    static const char * const kind[] = {
        [EMU_KIND_text] = "text", [EMU_KIND_number] = "number",
        [EMU_KIND_string] = "string", [EMU_KIND_yes_no] = "yes-no",
        [EMU_KIND_named] = "named"
    };
    struct emu_option_info o;
    char value[64];
    unsigned int i, j, flags;
    uint8_t cfg[256];

    emu_flash_cfg_defaults(cfg);
    for (i = 0; i < emu_flash_nr_options(); i++) {
        emu_flash_option_info(i, &o);
        printf("option %s %s",
               emu_flash_option(i, cfg, value, sizeof(value), &flags),
               kind[o.kind]);
        for (j = 0; j < o.nr_values; j++)
            printf(" %s", o.values[j].name);
        for (j = 0; j < o.nr_flags; j++)
            printf(" +%s", o.flags[j].name);
        printf("\n");
    }
}

static void *script_thread(void *arg_sleep_ms)
{
    char line[512], arg[64];
    unsigned int i, ms = (uintptr_t)arg_sleep_ms;
    int n;

    crash_name_thread("script");
    if (ms != 0)
        run_for(ms);

    while (read_line(line, sizeof(line))) {

        ui_poll();

        if (sscanf(line, "sleep %u", &ms) == 1) {
            run_for(ms);
        } else if ((sscanf(line, "wait %u %63s %n", &ms, arg, &n) == 2)
                   && (line[n] != '\0')
                   && (!strcmp(arg, "log") || !strcmp(arg, "display")
                       || !strcmp(arg, "segments"))) {
            wait_for(ms, arg, line + n);
        } else if (sscanf(line, "key %63s", arg) == 1) {
            for (i = 0; i < FDD_ACT_nr; i++)
                if (ui_fdd_has(i) && !strcmp(arg, fdd_action_name[i]))
                    break;
            if (i < FDD_ACT_nr) {
                ui_fdd_action(i);
                continue;
            }
            for (i = 0; i < KEY_ACT_nr; i++)
                if (!strcmp(arg, action_name[i]))
                    break;
            if (i == KEY_ACT_nr) {
                fprintf(stderr, "ffemu: no such action: %s\n", arg);
                continue;
            }
            fflush(NULL);
            ui_action(i);
            if ((i == KEY_ACT_reset) || (i == KEY_ACT_relaunch)
                || (i == KEY_ACT_quit))
                park();
        } else if (sscanf(line, "usb-fat %63s", arg) == 1) {
            if (strcmp(arg, "fat32") && strcmp(arg, "fat16")) {
                fprintf(stderr, "ffemu: no such file system: %s\n", arg);
                continue;
            }
            ui_usb_set_fat16(!strcmp(arg, "fat16"));
        } else if (sscanf(line, "usb-drive-sfn-writing %63s", arg) == 1) {
            /* Before usb-drive-sfn, whose format would take this too. */
            for (i = 0; (i < USB_BY_nr) && strcmp(arg, usb_by_name[i]); i++)
                continue;
            if (i == USB_BY_nr) {
                fprintf(stderr, "ffemu: no such system: %s\n", arg);
                continue;
            }
            ui_usb_set_drive(config.usb_fat16, config.usb_sfn,
                             config.usb_lfn, i);
        } else if (sscanf(line, "usb-drive-sfn %63s", arg) == 1) {
            for (i = 0; (i < USB_SFN_nr) && strcmp(arg, usb_sfn_name[i]); i++)
                continue;
            if (i == USB_SFN_nr) {
                fprintf(stderr, "ffemu: no such code page: %s\n", arg);
                continue;
            }
            ui_usb_set_drive(config.usb_fat16, i, config.usb_lfn,
                             config.usb_by);
        } else if (sscanf(line, "usb-drive-lfn %63s", arg) == 1) {
            if (strcmp(arg, "yes") && strcmp(arg, "no")) {
                fprintf(stderr, "ffemu: no such usb-drive-lfn: %s\n", arg);
                continue;
            }
            ui_usb_set_drive(config.usb_fat16, config.usb_sfn,
                             !strcmp(arg, "yes"), config.usb_by);
        } else if (!strcmp(line, "dump")) {
            dump_display();
        } else if (!strcmp(line, "osd")) {
            dump_osd();
        } else if (!strcmp(line, "imgcfg")) {
            dump_imgcfg();
        } else if (!strcmp(line, "log")) {
            print_log();
        } else if (!strcmp(line, "status")) {
            print_status();
        } else if (!strcmp(line, "check-dialogs")) {
            ui_check_dialogs();
        } else if (!strcmp(line, "options")) {
            print_options();
        } else if (!strcmp(line, "flash")) {
            static char section[8192];
            rc_flash_section(section, sizeof(section));
            fputs(section, stdout);
        } else if (!strncmp(line, "save ", 5)) {
            char err[600];
            if (!usb_save(line + 5, err, sizeof(err)))
                fprintf(stderr, "ffemu: cannot save the USB drive: %s\n",
                        err);
        } else if (!strncmp(line, "unpack ", 7)) {
            char err[600];
            if (!usb_unpack(line + 7, err, sizeof(err)))
                fprintf(stderr, "ffemu: cannot unpack the USB drive: %s\n",
                        err);
        } else if (!strcmp(line, "crash")) {
            /* A fault, which tests the crash report. */
            volatile int *volatile p = NULL;
            *p = 0;
        } else if ((line[0] != '\0') && (line[0] != '#')) {
            fprintf(stderr, "ffemu: no such command: %s\n", line);
        }

        fflush(NULL);
    }

    cpu_request_quit();
    park();
    return NULL;
}

void script_start(unsigned int sleep_ms)
{
    pthread_t thread;

    started = true;
    if (pthread_create(&thread, NULL, script_thread,
                       (void *)(uintptr_t)sleep_ms) != 0)
        cpu_quit(3, "ffemu: cannot create a thread");
}

void script_end(void)
{
    if (started)
        (void)!write(STDOUT_FILENO, "quit\n", 5);
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

/*
 * host.h
 *
 * Declarations shared within the host half of ffemu.
 *
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

#ifndef FFEMU_HOST_H
#define FFEMU_HOST_H

/* The program name, which also names its directories of settings and logs. */
#ifndef FFEMU_NAME
#define FFEMU_NAME "ffemu"
#endif
/* 1 where the firmware is that of the Apple2 target. */
#ifndef FFEMU_APPLE2
#define FFEMU_APPLE2 0
#endif

#include <stdbool.h>
#include <stdio.h>
#include "emu.h"

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

/*
 * cpu.c
 */

/* Prepares interrupt emulation. Call before any other thread is created. */
void cpu_init(char **argv);
/* Starts the 1 ms tick in the calling thread, which then runs the firmware. */
void cpu_start(void);
/* Power-cycles the emulated device by re-executing the program. */
void cpu_restart(void) __attribute__((noreturn));
/* Leaves the program, restoring the terminal. @msg may be NULL. */
void cpu_quit(int code, const char *msg) __attribute__((noreturn));
/* As the two above, for threads other than the one running the firmware:
 * the firmware thread acts on the request at its next tick. */
void cpu_request_restart(void);
void cpu_request_quit(void);
/* As a new start of the program with the same arguments: ffemu and the
 * device both begin afresh, from the settings file. */
void cpu_request_relaunch(void);
/* User interface state carried over a power cycle, or NULL on a fresh start:
 * the rendering style, whether the USB drive is inserted, how much of a
 * script's sleep is left, the floppy signals and the latched buttons. */
const char *cpu_restart_state(void);

/* Firmware console output: a ring that only ever grows at log_head. */
#define LOG_SIZE 16384
extern char log_ring[LOG_SIZE];
extern volatile unsigned int log_head;
/* The file that the console output also goes to, or NULL. */
extern const char *log_file_name;

/* The firmware's configuration in its flash memory, as hex, into @buf; and
 * from hex, as the settings file keeps it. */
void flash_to_hex(char *buf, size_t size);
void flash_from_hex(const char *hex);
/* Sets the flash memory to @size bytes from @buf, as emu_flash_save() does
 * but without writing the settings file. */
void flash_set(const void *buf, unsigned int size);
/* Up to @n bytes from @hex into @p; returns how many there were. */
size_t hex_to_bytes(void *p, size_t n, const char *hex);
/* Its options that differ from the defaults, as Status shows them. */
void flash_summary(char *buf, size_t size);

/*
 * ssd1306.c
 */

#define OLED_W 128
#define OLED_MAX_H 64

/* The displays that ffemu can fit: an OLED, by its controller and the
 * panel's height, a character LCD, by its columns and rows, or a 7-segment
 * LED display, by its controller and its digits. */
enum { DISP_ssd1306_32, DISP_ssd1306_64, DISP_sh1106_32, DISP_sh1106_64,
       DISP_lcd_16x2, DISP_lcd_20x2, DISP_lcd_20x4, DISP_lcd_40x2,
       DISP_74hc164, DISP_tm1651, DISP_nr };
#define DISP_IS_OLED(d) ((d) <= DISP_sh1106_64)
#define DISP_IS_LCD(d) (((d) >= DISP_lcd_16x2) && ((d) <= DISP_lcd_40x2))
#define DISP_IS_LED(d) ((d) >= DISP_74hc164)
#define DISP_IS_SH1106(d) (((d) == DISP_sh1106_32) || ((d) == DISP_sh1106_64))
/* Of an OLED. */
#define DISP_HEIGHT(d) (((d) & 1) ? 64 : 32)
/* Of an LCD. */
#define DISP_LCD_COLS(d) \
    (((d) == DISP_lcd_16x2) ? 16 : ((d) == DISP_lcd_40x2) ? 40 : 20)
#define DISP_LCD_ROWS(d) (((d) == DISP_lcd_20x4) ? 4 : 2)
/* Of an LED display. */
#define DISP_DIGITS(d) (((d) == DISP_tm1651) ? 3 : 2)
/* Their names in the settings file, and on the screen. */
extern const char * const display_name[DISP_nr];
extern const char * const display_label[DISP_nr];

struct oled_view {
    bool present;    /* a display is fitted */
    bool on;         /* display is switched on */
    bool inverse;
    const char *chip;      /* the controller */
    unsigned int height;   /* rows of the panel: 32 or 64 */
    unsigned int driven;   /* rows that the firmware drives */
    unsigned int contrast; /* 0-255 */
    unsigned int nr_updates; /* counts completed refreshes */
};

/* Fits display @display, DISP_*. */
void oled_init(int display);
/* Snapshot for drawing: @px gets OLED_MAX_H rows of OLED_W bytes, 0 or 1. */
void oled_get_view(struct oled_view *view, uint8_t *px);
/* The device on the I2C bus, as emu_i2c_dev_*() of emu.h: each answers
 * only when fitted. */
int oled_i2c_start(unsigned int addr, int rd);
void oled_i2c_write(uint8_t b);
uint8_t oled_i2c_read(void);
void oled_i2c_stop(void);

/*
 * hd44780.c
 */

#define LCD_MAX_COLS 40
#define LCD_MAX_ROWS 4

/* The character ROMs that ffemu has for the LCD, by their names in the
 * settings file and on the screen, with a line of help for each. */
enum { LCD_ROM_cyr, LCD_ROM_a00, LCD_ROM_nr };
extern const char * const lcd_rom_name[LCD_ROM_nr];
extern const char * const lcd_rom_label[LCD_ROM_nr];
extern const char * const lcd_rom_help[LCD_ROM_nr];

struct lcd_view {
    bool present;            /* an LCD is fitted */
    bool on;                 /* the controller shows the characters */
    bool backlight;
    unsigned int cols, rows; /* of the module */
    int rom;                 /* LCD_ROM_*: its character ROM */
    unsigned int nr_updates; /* counts transactions that wrote characters */
    uint8_t text[LCD_MAX_ROWS][LCD_MAX_COLS]; /* the characters shown */
};

/* Fits display @display, one of DISP_lcd_*. */
void hd44780_init(int display);
void hd44780_get_view(struct lcd_view *view);
int hd44780_i2c_start(unsigned int addr, int rd);
void hd44780_i2c_write(uint8_t b);
uint8_t hd44780_i2c_read(void);
void hd44780_i2c_stop(void);
/* Sets the character ROM, LCD_ROM_*. */
void hd44780_set_rom(int rom);
/* The Unicode code point that code @c stands for in ROM @rom, a space for
 * a blank glyph: 0 for none, a user-defined character or a glyph that no
 * single character matches. */
unsigned int hd44780_unicode(int rom, uint8_t c);
/* What text shows for a code without a character: a white square, the
 * usual mark of a missing one. */
#define LCD_MISSING 0x25a1
/* The size in pixels of an LCD of @nr_cols by @nr_rows characters. */
void hd44780_size(unsigned int nr_cols, unsigned int nr_rows,
                  unsigned int *w, unsigned int *h);
/* The characters of @v as pixels, @w by @h bytes, 1 for a dark dot, in a
 * buffer of this module's that the next call reuses; a character without a
 * glyph, a user-defined one included, as a checkerboard. */
void hd44780_pixels(const struct lcd_view *v, const uint8_t **px,
                    unsigned int *w, unsigned int *h);

/*
 * led_7seg.c
 */

struct led_view {
    bool present;            /* an LED display is fitted */
    bool on;
    unsigned int nr_digits;  /* 3 or 2 */
    unsigned int brightness; /* 0-7, of the TM1651 */
    unsigned int nr_updates; /* counts writes of the digits */
    uint8_t seg[3];          /* each digit's segments: a to g, the point */
};

/* The fonts of the digits, one per rendering (led_7seg_font.h). */
enum { LED_FONT_ascii, LED_FONT_half, LED_FONT_braille };
/* The character of a lit pixel in the ASCII rendering (led_7seg_font.h). */
extern const char ascii_pixel;

/* Fits display @display, DISP_74hc164 or DISP_tm1651. */
void led_init(int display);
void led_get_view(struct led_view *view);
/* The size in pixels of @nr_digits digits in font @font. */
void led_size(unsigned int nr_digits, unsigned int font, unsigned int *w,
              unsigned int *h);
/* The digits of @v as pixels in font @font: @lit the segments that are
 * on, @all every segment, each @w by @h bytes, 0 or 1, in buffers of
 * this module's that the next call reuses. */
void led_pixels(const struct led_view *v, unsigned int font,
                const uint8_t **lit, const uint8_t **all, unsigned int *w,
                unsigned int *h);
/* The digits of @v as text, a point after a digit whose point is lit,
 * '?' for a pattern that is no digit or letter of the firmware's. */
void led_text(const struct led_view *v, char *buf, size_t size);

/*
 * fatimg.c
 */

/* What the USB drive is made from. */
enum { USB_dir, USB_image, USB_disk };

struct usb_info {
    bool inserted;
    uint8_t kind; /* USB_* */
    unsigned int nr_files, nr_dirs, nr_skipped;
    unsigned int nr_case_dups; /* left out: as names they equal others */
    unsigned long nr_reads, nr_writes; /* transfers since insertion */
    unsigned int nr_kept;  /* sectors written before the power cycle, kept */
    bool writes_lost;      /* such sectors were dropped: the files changed */
    uint64_t image_bytes;
    char ff_cfg[16];  /* the FF.CFG that the firmware reads, as on the drive */
    unsigned int ff_cfg_gen; /* changes whenever FF.CFG is to be read again */
    char case_dup[160]; /* the first of those left out, as a path */
    char error[160];  /* why the last usb_insert() failed, or "" */
};

/* Inserts a drive made from @path: a FAT volume built from the files under
 * it if it is a directory, else an image file or a disk, read as it is.
 * What the firmware wrote to the previous drive is kept if @keep_writes,
 * provided that the drive is still the same. */
bool usb_insert(const char *path, bool keep_writes);
/* The store of the firmware's writes, to carry it over a power cycle. */
void usb_get_store(int *fd, uint32_t *layout);
void usb_set_store(int fd, uint32_t layout);
void usb_remove(void);
/* On every eject, of a drive that is in or already out: the FF.CFG of a
 * drive made from a directory is looked for again, for showing what the
 * next insertion would give the firmware. */
void usb_eject_ff_cfg(void);
void usb_get_info(struct usb_info *info);
/* The text of FF.CFG on a drive from an image or a disk, read when it was
 * inserted; NULL for a directory, or if it has none. */
const char *usb_ff_cfg_text(void);
/* The text of the file with short name @name83 (8.3, blank-padded) in the
 * folder FF, or in the root if the drive has no such folder, as the
 * firmware sees the drive now, its writes included; false if there is no
 * drive or no such file. */
bool usb_read_text(const char *name83, char *buf, size_t size);
/* While the drive is out, usb_get_info() and usb_ff_cfg_text() tell of the
 * FF.CFG of the last drive. */
/* Where FF.CFG is, or would be, under directory @dir, as a path relative
 * to it: in the folder FF if there is one, else in @dir itself. */
void usb_ff_cfg_place(const char *dir, char *buf, size_t size);
/* Writes the drive as the firmware sees it to file @path; if that fails,
 * says why in @err. */
bool usb_save(const char *path, char *err, size_t size);

/*
 * cfgfile.c
 */

/* Writes FF.CFG on the USB drive from configuration @cfg, as the flash
 * memory keeps one: every option, or only those that differ from the
 * defaults. Says what it did, or why it could not, in @msg. */
bool ff_cfg_write(const void *cfg, bool all, char *msg, size_t size);
/* The value of option @name in the drive's FF.CFG, as the firmware sees the
 * file now, into @value; false if there is no drive, file or such line. */
bool ff_cfg_file_option(const char *name, char *value, size_t size);
/* Sets option @name to @value in the drive's FF.CFG, where it has a line
 * for it, on a drive made from a directory or a disk the system has
 * mounted: the line rewritten in place, the old file kept as .BAK. Says
 * what it did, or why it could not, in @msg. */
bool ff_cfg_set_option(const char *name, const char *value, char *msg,
                       size_t size);

/*
 * usbdisk.c
 */

/* Finds the USB drives attached, and prints each to @f, unless NULL,
 * as a line that ends with its /dev name: the first into @dev, and its line
 * into @line. Returns their number. */
int usb_disk_find(char *dev, size_t size, char *line, size_t line_size,
                  FILE *f);
/* Where the system has disk @dev, or its first partition, mounted, as a
 * directory into @buf; false if nowhere. */
bool usb_disk_mount(const char *dev, char *buf, size_t size);

/*
 * rc.c
 */

/* How the display is drawn, from the smallest to the largest. */
enum {
    STYLE_braille = 1, /* 2x4 pixels per character */
    STYLE_half,        /* 1x2 */
    STYLE_ascii,       /* 1x1, for terminals without the block characters */
    STYLE_nr = STYLE_ascii
};

enum {
    KEY_ACT_select, KEY_ACT_left, KEY_ACT_right, KEY_ACT_cw, KEY_ACT_ccw,
    KEY_ACT_remove, KEY_ACT_insert, KEY_ACT_reset, KEY_ACT_quit,
    KEY_ACT_latch, KEY_ACT_relaunch,
    KEY_ACT_nr
};

struct config {
    int style;              /* STYLE_* */
    int display;            /* DISP_*: the display fitted */
    int lcd_rom;            /* LCD_ROM_*: the LCD's character ROM */
    int display_color;      /* a curses color, plus 8 if bright */
    unsigned int hold_ms;   /* how long a key press holds a button down */
    char path[512];         /* where the settings file is, or would be */
    bool loaded;            /* the file exists */
};

extern struct config config;
extern const char * const style_name[STYLE_nr + 1];

/* Reads the settings file, writing it with the defaults if it is missing. */
void rc_load(void);
/* Writes the present settings into the file, keeping its other lines. */
void rc_save(void);
/* The [Flash mem] section of the settings file, as the flash is now. */
void rc_flash_section(char *buf, size_t size);
/* Creates @dir and the directories above it that are missing. */
void make_dirs(char *dir);
/* The user's home directory, also when run by sudo to read a disk. */
const char *user_home(void);
/* Gives a file that ffemu created under sudo to the user who ran sudo. */
void own_file(const char *path);

/*
 * ffemu.c
 */

/* The host directory, image file or disk that the USB drive is made from,
 * as given and as an absolute path for showing. */
extern const char *usb_path, *usb_path_name;
/* @bytes as megabytes or gigabytes, for showing. */
const char *size_text(uint64_t bytes, char *buf, size_t size);

/* Performs a front-panel action, KEY_ACT_*. User interface thread only. */
void ui_action(int act);
/* Call often: releases the buttons and completes a re-insertion. */
void ui_poll(void);
/* The encoder directions turned a moment ago, to show: UI_ROTARY_*. */
#define UI_ROTARY_cw 1
#define UI_ROTARY_ccw 2
unsigned int ui_rotary_flash(void);
/* Latch mode, KEY_ACT_latch: a button stays down until its key comes again,
 * or until the mode ends, which releases them all. */
bool ui_latched(void);
/* Latch mode and the buttons it holds down, which a power cycle keeps, as
 * fingers stay on real buttons, and back. */
unsigned int ui_latch_save(void);
void ui_latch_restore(unsigned int latch);

/* What the host computer does on the floppy interface, each a change of its
 * signals in emu_in_fdd or a STEP pulse; the firmware's target has some of
 * them only. */
enum {
    FDD_ACT_sel,       /* toggles drive select, or Apple2 drive enable */
    FDD_ACT_motor,     /* Shugart: toggles motor on */
    FDD_ACT_dir,       /* Shugart: toggles the step direction */
    FDD_ACT_step,      /* Shugart: sends a STEP pulse */
    FDD_ACT_side,      /* Shugart: toggles side select */
    FDD_ACT_phase_in,  /* Apple2: the phases' next state, inward */
    FDD_ACT_phase_out, /* Apple2: their previous state, outward */
    FDD_ACT_release,   /* Apple2: all phases off, the state kept */
    FDD_ACT_nr
};
extern const char * const fdd_action_name[FDD_ACT_nr];
/* Whether the firmware's target has action @act. */
bool ui_fdd_has(int act);
/* Performs FDD_ACT_* @act. User interface thread only. */
void ui_fdd_action(int act);
/* Whether a STEP pulse was sent a moment ago, to show. */
bool ui_fdd_stepping(void);
/* Apple2: the phases of the state kept in their cycle, which they show
 * unless released, as EMU_FDD_PH0 and up. */
unsigned int ui_fdd_phases(void);
/* Phases @phases, as EMU_FDD_PH0 and up, as text such as "01..". */
const char *ui_fdd_phase_text(unsigned int phases, char buf[5]);
/* The FDD signals and the kept phase state, for a power cycle, and back. */
void ui_fdd_save(unsigned int *in, unsigned int *pos);
void ui_fdd_restore(unsigned int in, unsigned int pos);
/* A line for the firmware log pane, from the emulator itself, which starts
 * it with HOST_LOG_PREFIX. */
void host_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
#define HOST_LOG_PREFIX "ffemu: "

/*
 * tui.c
 */

/* Starts the user interface in a thread of its own. */
void tui_start(void);
/* Restores the terminal. Safe to call from any thread, more than once. */
void tui_stop(void);
/* Puts the terminal's screen back as far as a signal handler can, for a
 * signal that ends the program. */
void tui_stop_fatal(void);

/*
 * script.c
 */

/* Runs commands from stdin instead of the user interface, for testing. The
 * script first sleeps for @sleep_ms. */
void script_start(unsigned int sleep_ms);
/* Milliseconds left of the script's current sleep. */
unsigned int script_sleep_left(void);

#endif /* FFEMU_HOST_H */

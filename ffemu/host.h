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
/* User interface state carried over a power cycle, or NULL on a fresh start:
 * the rendering style, whether the USB drive is inserted, and how much of a
 * script's sleep is left. */
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

/* The displays that ffemu can fit: the controller and the panel's height. */
enum { DISP_ssd1306_32, DISP_ssd1306_64, DISP_sh1106_32, DISP_sh1106_64,
       DISP_nr };
#define DISP_IS_SH1106(d) ((d) >= DISP_sh1106_32)
#define DISP_HEIGHT(d) (((d) & 1) ? 64 : 32)
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
/* While the drive is out, usb_get_info() and usb_ff_cfg_text() tell of the
 * FF.CFG of the last drive. */
/* Writes the drive as the firmware sees it to file @path; if that fails,
 * says why in @err. */
bool usb_save(const char *path, char *err, size_t size);

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
    KEY_ACT_latch,
    KEY_ACT_nr
};

struct config {
    int style;              /* STYLE_* */
    int display;            /* DISP_*: the display fitted */
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

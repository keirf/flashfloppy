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
    [KEY_ACT_latch] = "latch"
};

static unsigned int log_printed;

/* When the current sleep ends, or zero. A reset by the firmware itself cuts
 * a sleep short: the rest of it is then slept after the power cycle. */
static volatile uint64_t sleep_end;

unsigned int script_sleep_left(void)
{
    uint64_t now = emu_time_ns(), end = sleep_end;
    return (end > now) ? (end - now) / 1000000u : 0;
}

/* Lets the firmware run for @ms, keeping the emulated inputs serviced. */
static void run_for(unsigned int ms)
{
    sleep_end = emu_time_ns() + (uint64_t)ms * 1000000u;

    do {
        ui_poll();
        usleep(2000);
    } while (emu_time_ns() < sleep_end);
    ui_poll();

    sleep_end = 0;
}

static void park(void)
{
    for (;;)
        pause();
}

static void dump_display(void)
{
    static uint8_t px[OLED_W * OLED_MAX_H];
    struct oled_view v;
    unsigned int x, y;
    char row[OLED_W + 1];

    oled_get_view(&v, px);
    printf("display: 128x%u %s contrast=%u%s updates=%u\n", v.height,
           v.on ? "on" : "off", v.contrast, v.inverse ? " inverse" : "",
           v.nr_updates);
    for (y = 0; y < v.height; y++) {
        for (x = 0; x < OLED_W; x++)
            row[x] = px[y*OLED_W + x] ? '#' : '.';
        row[OLED_W] = '\0';
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

static void print_status(void)
{
    static uint8_t px[OLED_W * OLED_MAX_H];
    struct oled_view v;
    struct usb_info usb;
    char flash[2048];

    oled_get_view(&v, px);
    printf("display: %s 128x%u, driven 128x%u\n", v.chip, v.height, v.driven);
    flash_summary(flash, sizeof(flash));
    printf("flash: %s\n", flash);

    usb_get_info(&usb);
    printf("firmware: %s on %s\n", emu_fw_version(), emu_board_name());
    if (usb.inserted && (usb.kind == USB_dir))
        printf("usb: inserted, %u files, %u dirs, %u left out, "
               "%u case duplicates left out, %lu reads, %lu writes, "
               "ff.cfg: %s\n", usb.nr_files, usb.nr_dirs, usb.nr_skipped,
               usb.nr_case_dups, usb.nr_reads, usb.nr_writes,
               usb.ff_cfg[0] ? usb.ff_cfg : "none");
    else if (usb.inserted)
        printf("usb: inserted, %s of %llu sectors, %lu reads, %lu writes, "
               "ff.cfg: %s\n", (usb.kind == USB_image) ? "image" : "disk",
               (unsigned long long)usb.image_bytes / 512, usb.nr_reads,
               usb.nr_writes, usb.ff_cfg[0] ? usb.ff_cfg : "none");
    else
        printf("usb: removed%s%s\n", usb.error[0] ? ", " : "", usb.error);
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

static void *script_thread(void *arg_sleep_ms)
{
    char line[512], arg[64];
    unsigned int i, ms = (uintptr_t)arg_sleep_ms;

    if (ms != 0)
        run_for(ms);

    while (read_line(line, sizeof(line))) {

        ui_poll();

        if (sscanf(line, "sleep %u", &ms) == 1) {
            run_for(ms);
        } else if (sscanf(line, "key %63s", arg) == 1) {
            for (i = 0; i < KEY_ACT_nr; i++)
                if (!strcmp(arg, action_name[i]))
                    break;
            if (i == KEY_ACT_nr) {
                fprintf(stderr, "ffemu: no such action: %s\n", arg);
                continue;
            }
            fflush(NULL);
            ui_action(i);
            if ((i == KEY_ACT_reset) || (i == KEY_ACT_quit))
                park();
        } else if (!strcmp(line, "dump")) {
            dump_display();
        } else if (!strcmp(line, "log")) {
            print_log();
        } else if (!strcmp(line, "status")) {
            print_status();
        } else if (!strcmp(line, "flash")) {
            static char section[8192];
            rc_flash_section(section, sizeof(section));
            fputs(section, stdout);
        } else if (!strncmp(line, "save ", 5)) {
            char err[600];
            if (!usb_save(line + 5, err, sizeof(err)))
                fprintf(stderr, "ffemu: cannot save the USB drive: %s\n",
                        err);
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

    if (pthread_create(&thread, NULL, script_thread,
                       (void *)(uintptr_t)sleep_ms) != 0)
        cpu_quit(3, "ffemu: cannot create a thread");
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

/*
 * ffemu.c
 *
 * Runs the FlashFloppy user interface in a terminal, with no Gotek hardware:
 * the firmware's own code draws on an emulated OLED display and reads an
 * emulated USB drive, while the keyboard stands in for the buttons and
 * the rotary encoder. Nothing is connected to the floppy interface.
 *
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

#include <errno.h>
#include <fcntl.h>
#include <langinfo.h>
#include <locale.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#include "host.h"

const char *usb_path = ".", *usb_path_name = ".";

const char *size_text(uint64_t bytes, char *buf, size_t size)
{
    if (bytes >= (1u << 30))
        snprintf(buf, size, "%.1f GB", bytes / 1073741824.0);
    else
        snprintf(buf, size, "%.1f MB", bytes / 1048576.0);
    return buf;
}

/* Buttons are held down until these times; zero when released. */
static uint64_t hold_until[3];
/* The encoder directions are shown as turned until these times. */
static uint64_t rotary_until[2];
/* A drive that was inserted again while inserted is first seen removed. */
static uint64_t reinsert_at;
#define REINSERT_NS 700000000u

void host_log(const char *fmt, ...)
{
    char msg[512];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);

    emu_log(HOST_LOG_PREFIX);
    emu_log(msg);
    emu_log("\n");
}

static bool latched;

bool ui_latched(void)
{
    return latched;
}

static void press(unsigned int nr)
{
    if (latched) {
        hold_until[nr] = 0;
        __sync_fetch_and_xor(&emu_in_buttons, 1u << nr);
        return;
    }
    hold_until[nr] = emu_time_ns() + (uint64_t)config.hold_ms * 1000000u;
    __sync_fetch_and_or(&emu_in_buttons, 1u << nr);
}

static void insert(bool keep_writes)
{
    struct usb_info info;
    bool ok = usb_insert(usb_path, keep_writes);
    char size[32];

    usb_get_info(&info);
    if (!ok) {
        host_log("USB drive: %s", info.error);
        return;
    }
    if (info.kind == USB_dir)
        host_log("USB drive inserted: %u files in %u folders, from %s",
                 info.nr_files, info.nr_dirs + 1, usb_path);
    else
        host_log("USB drive inserted: %s %s, from %s",
                 size_text(info.image_bytes, size, sizeof(size)),
                 (info.kind == USB_image) ? "image" : "disk", usb_path);
    if (info.nr_skipped != 0)
        host_log("USB drive: %u entr%s left out (unusable names, special "
                 "files, 4 GB or larger, or too many)", info.nr_skipped,
                 (info.nr_skipped == 1) ? "y" : "ies");
    if (info.nr_case_dups != 0)
        host_log("USB drive: %u entr%s left out as differing from others "
                 "only in letter case, such as %s", info.nr_case_dups,
                 (info.nr_case_dups == 1) ? "y" : "ies", info.case_dup);
    if (info.nr_kept != 0)
        host_log("USB drive: kept %u sectors that the firmware wrote before "
                 "the power cycle", info.nr_kept);
    if (info.writes_lost)
        host_log("USB drive: the files have changed, so what the firmware "
                 "wrote before the power cycle is dropped");
}

void ui_action(int act)
{
    switch (act) {
    case KEY_ACT_left:
        press(0);
        break;
    case KEY_ACT_right:
        press(1);
        break;
    case KEY_ACT_select:
        press(2);
        break;
    case KEY_ACT_cw:
        __sync_fetch_and_add(&emu_in_rotary, 1);
        rotary_until[0] = emu_time_ns() + (uint64_t)config.hold_ms * 1000000u;
        break;
    case KEY_ACT_ccw:
        __sync_fetch_and_sub(&emu_in_rotary, 1);
        rotary_until[1] = emu_time_ns() + (uint64_t)config.hold_ms * 1000000u;
        break;
    case KEY_ACT_remove:
        reinsert_at = 0;
        if (emu_usb_inserted()) {
            usb_remove();
            host_log("USB drive ejected");
        }
        break;
    case KEY_ACT_insert:
        if (emu_usb_inserted()) {
            usb_remove();
            host_log("USB drive ejected, to be inserted again");
            reinsert_at = emu_time_ns() + REINSERT_NS;
        } else if (reinsert_at == 0) {
            insert(false);
        }
        break;
    case KEY_ACT_reset:
        cpu_request_restart();
        break;
    case KEY_ACT_quit:
        cpu_request_quit();
        break;
    case KEY_ACT_latch:
        if (latched)
            __sync_fetch_and_and(&emu_in_buttons, ~7u);
        latched = !latched;
        break;
    }
}

unsigned int ui_rotary_flash(void)
{
    uint64_t now = emu_time_ns();

    return ((now < rotary_until[0]) ? UI_ROTARY_cw : 0)
        | ((now < rotary_until[1]) ? UI_ROTARY_ccw : 0);
}

void ui_poll(void)
{
    uint64_t now = emu_time_ns();
    unsigned int i;

    for (i = 0; i < ARRAY_SIZE(hold_until); i++) {
        if ((hold_until[i] != 0) && (now >= hold_until[i])) {
            hold_until[i] = 0;
            __sync_fetch_and_and(&emu_in_buttons, ~(1u << i));
        }
    }

    if ((reinsert_at != 0) && (now >= reinsert_at)) {
        reinsert_at = 0;
        insert(false);
    }
}

static void usage(FILE *f)
{
    fprintf(f,
            "Usage: ffemu [<directory> | <image> | <disk>]\n"
            "\n"
            "Runs the FlashFloppy user interface in the terminal, with no "
            "Gotek hardware.\n"
            "The USB drive holds the files of <directory>, by default "
            "the current\n"
            "one, or is read from an image file or a disk such as %s.\n"
            "Whatever the firmware writes to it never reaches these.\n"
            "\n"
            "Settings: %s\n"
            "\n"
            "When standard input is not a terminal, commands are read from "
            "it instead:\n"
            "  key <action>: select, left, right, cw, ccw, remove, insert, "
            "reset or quit\n"
            "  sleep <ms>: let the firmware run\n"
            "  dump: print the display\n"
            "  log: print the firmware's console output so far\n"
            "  status: print the state of the emulated device\n"
            "  flash: print the flash memory as in the settings\n"
            "  save <file>: write the USB drive as the firmware sees it\n",
#ifdef __CYGWIN__
            "/dev/sdb (\\\\.\\PhysicalDrive1)",
#else
            "/dev/sdb",
#endif
            config.path);
}

int main(int argc, char **argv)
{
    const char *state, *store = getenv("FFEMU_USB");
    int style, usb = 1, fd;
    unsigned int sleep_ms = 0, layout;
    struct stat st;

    /* The display is drawn with Unicode block and Braille characters. */
    setlocale(LC_ALL, "");
    if (strcmp(nl_langinfo(CODESET), "UTF-8")
        && !setlocale(LC_CTYPE, "C.UTF-8"))
        setlocale(LC_CTYPE, "en_US.UTF-8");

    cpu_init(argv);
    rc_load();

    if ((argc > 2) || ((argc == 2) && (argv[1][0] == '-'))) {
        bool help = (argc == 2) && (!strcmp(argv[1], "-h")
                                    || !strcmp(argv[1], "--help"));
        usage(help ? stdout : stderr);
        return help ? 0 : 2;
    }
    if (argc == 2)
        usb_path = argv[1];
    if (stat(usb_path, &st) != 0) {
        fprintf(stderr, "ffemu: %s: %s\n", usb_path, strerror(errno));
        return 2;
    }
    if (!S_ISDIR(st.st_mode) && !S_ISREG(st.st_mode)
        && !S_ISBLK(st.st_mode)) {
        fprintf(stderr, "ffemu: %s is not a directory, an image or a disk\n",
                usb_path);
        return 2;
    }
    if (!S_ISDIR(st.st_mode)) {
        int e, image = open(usb_path, O_RDONLY);
        if (image < 0) {
            e = errno;
            fprintf(stderr, "ffemu: %s: %s\n", usb_path, strerror(e));
            if (S_ISBLK(st.st_mode) && ((e == EACCES) || (e == EPERM)))
                fprintf(stderr, "ffemu: to read a disk, run it %s\n",
#ifdef __CYGWIN__
                        "in a terminal started as administrator"
#else
                        "with sudo"
#endif
                    );
            return 2;
        }
        close(image);
    }
    usb_path_name = realpath(usb_path, NULL) ?: usb_path;

    state = cpu_restart_state();
    if ((state != NULL)
        && (sscanf(state, "%d,%d,%u", &style, &usb, &sleep_ms) == 3)
        && (style >= 1) && (style <= STYLE_nr))
        config.style = style;

    if ((store != NULL) && (sscanf(store, "%d,%x", &fd, &layout) == 2))
        usb_set_store(fd, layout);
    unsetenv("FFEMU_USB");

    oled_init(config.display);
    if (usb)
        insert(state != NULL);

    if (isatty(STDIN_FILENO) && isatty(STDOUT_FILENO))
        tui_start();
    else
        script_start(sleep_ms);

    cpu_start();
    ff_main();
    cpu_quit(3, "ffemu: the firmware's main() returned");
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

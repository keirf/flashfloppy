/*
 * ffemu.c
 *
 * Runs the FlashFloppy user interface in a terminal, with no Gotek hardware:
 * the firmware's own code draws on an emulated display and reads an
 * emulated USB drive, while the keyboard stands in for the buttons and
 * the rotary encoder, and for the signals of a host computer that select the
 * drive and move its head. No data flows on the floppy interface.
 *
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <langinfo.h>
#include <locale.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/utsname.h>

#include "host.h"

const char *usb_path, *usb_path_name;

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
    char msg[512], *p, *q;
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    for (p = q = msg; *p; p++)
        if (*p != MSG_USER)
            *q++ = *p;
    *q = '\0';

    emu_log(HOST_LOG_PREFIX);
    emu_log(msg);
    emu_log("\n");
}

static bool latched;

bool ui_latched(void)
{
    return latched;
}

unsigned int ui_latch_save(void)
{
    return latched ? 8u | (emu_in_buttons & 7u) : 0;
}

void ui_latch_restore(unsigned int latch)
{
    latched = (latch & 8u) != 0;
    if (latched)
        __sync_fetch_and_or(&emu_in_buttons, latch & 7u);
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
        host_log("USB drive inserted: %u files in %u folders, FAT%u, from %s",
                 info.nr_files, info.nr_dirs + 1, info.fat_bits, usb_path);
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
    if (info.nr_root_full != 0)
        host_log("USB drive: %u entr%s left out, as copying would leave "
                 "them: the FAT16 root folder holds 512 entries; first %s",
                 info.nr_root_full, (info.nr_root_full == 1) ? "y" : "ies",
                 info.root_full);
    if (info.nr_drive_full != 0)
        host_log("USB drive: %u file%s left out, as copying would leave "
                 "them: a FAT16 drive holds 2 GB; first %s",
                 info.nr_drive_full, (info.nr_drive_full == 1) ? "" : "s",
                 info.drive_full);
    if (info.nr_kept != 0)
        host_log("USB drive: kept %u sectors that the firmware wrote before "
                 "the power cycle", info.nr_kept);
    if (info.writes_lost)
        host_log("USB drive: the files have changed, so what the firmware "
                 "wrote before the power cycle is dropped");
    usb_log_lacking();
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
        usb_eject_ff_cfg();
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
    case KEY_ACT_relaunch:
        cpu_request_relaunch();
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

const char * const fdd_action_name[FDD_ACT_nr] = {
    [FDD_ACT_sel] = "sel",
    [FDD_ACT_write] = "write",
    [FDD_ACT_motor] = "motor",
    [FDD_ACT_dir] = "dir",
    [FDD_ACT_step] = "step",
    [FDD_ACT_side] = "side",
    [FDD_ACT_phase_in] = "phase-in",
    [FDD_ACT_phase_out] = "phase-out",
    [FDD_ACT_release] = "release"
};

/* The STEP pulse is shown until this time. */
static uint64_t step_until;
/* Apple2: the position of the phases in their cycle of eight states, from
 * phase 0 alone, through phases 0 and 1, to phase 1 alone, and on. */
static unsigned int phase_pos;

const char * const mcu_name[MCU_nr] = {
    [EMU_MCU_at32f415] = "415",
    [EMU_MCU_at32f435] = "435"
};
const char * const mcu_label[MCU_nr] = {
    [EMU_MCU_at32f415] = "AT32F415, as STM32F105",
    [EMU_MCU_at32f435] = "AT32F435"
};
const char * const mcu_help[MCU_nr] = {
    [EMU_MCU_at32f415] = "The board's own MCU, whose code is the STM32F105's",
    [EMU_MCU_at32f435] = "That of AT32F435 boards: its I2C is of a newer kind"
};

void ui_usb_set_drive(bool fat16, int sfn, bool lfn, int by)
{
    struct usb_info info;

    if ((fat16 == config.usb_fat16) && (sfn == config.usb_sfn)
        && (lfn == config.usb_lfn) && (by == config.usb_by))
        return;
    config.usb_fat16 = fat16;
    config.usb_sfn = sfn;
    config.usb_lfn = lfn;
    config.usb_by = by;
    rc_save();
    usb_get_info(&info);
    if (info.inserted && (info.kind == USB_dir))
        ui_action(KEY_ACT_insert);
}

void ui_usb_set_fat16(bool fat16)
{
    ui_usb_set_drive(fat16, config.usb_sfn, config.usb_lfn, config.usb_by);
}

bool ui_fdd_has(int act)
{
    if ((act == FDD_ACT_sel) || (act == FDD_ACT_write))
        return true;
    return FFEMU_APPLE2 ? (act >= FDD_ACT_phase_in) : (act < FDD_ACT_phase_in);
}

unsigned int ui_fdd_phases(void)
{
    unsigned int ph = 1u << (phase_pos / 2);

    if (phase_pos & 1)
        ph |= 1u << ((phase_pos / 2 + 1) & 3);
    return ph * EMU_FDD_PH0;
}

void ui_fdd_action(int act)
{
    unsigned int in = emu_in_fdd, phases = 15 * EMU_FDD_PH0;

    if (!ui_fdd_has(act))
        return;

    /* This thread alone writes the signals. */
    switch (act) {
    case FDD_ACT_sel:
        emu_in_fdd = in ^ EMU_FDD_SEL;
        break;
    case FDD_ACT_write:
        emu_in_fdd = in ^ EMU_FDD_WRITE;
        break;
    case FDD_ACT_motor:
        emu_in_fdd = in ^ EMU_FDD_MOTOR;
        break;
    case FDD_ACT_dir:
        emu_in_fdd = in ^ EMU_FDD_DIR;
        break;
    case FDD_ACT_step:
        step_until = emu_time_ns() + (uint64_t)config.hold_ms * 1000000u;
        __sync_fetch_and_add(&emu_in_step, 1);
        break;
    case FDD_ACT_side:
        emu_in_fdd = in ^ EMU_FDD_SIDE;
        break;
    case FDD_ACT_phase_in:
    case FDD_ACT_phase_out:
        phase_pos = (phase_pos + ((act == FDD_ACT_phase_in) ? 1 : 7)) % 8;
        emu_in_fdd = (in & ~phases) | ui_fdd_phases();
        break;
    case FDD_ACT_release:
        emu_in_fdd = in & ~phases;
        break;
    }
}

const char *ui_fdd_phase_text(unsigned int phases, char buf[5])
{
    unsigned int i;

    for (i = 0; i < 4; i++)
        buf[i] = (phases & (EMU_FDD_PH0 << i)) ? '0' + i : '.';
    buf[4] = '\0';
    return buf;
}

bool ui_fdd_stepping(void)
{
    return emu_time_ns() < step_until;
}

void ui_fdd_save(unsigned int *in, unsigned int *pos)
{
    *in = emu_in_fdd;
    *pos = phase_pos;
}

void ui_fdd_restore(unsigned int in, unsigned int pos)
{
    emu_in_fdd = in;
    phase_pos = pos % 8;
}

/* The I2C bus, whose slaves are the display fitted, an OLED or an LCD,
 * and FF OSD if on: the device that answered the last START takes the
 * transaction. */
static enum { I2C_oled, I2C_lcd, I2C_osd } i2c_dev;

int emu_i2c_dev_start(unsigned int addr, int rd)
{
    if (hd44780_i2c_start(addr, rd))
        i2c_dev = I2C_lcd;
    else if (osd_i2c_start(addr, rd))
        i2c_dev = I2C_osd;
    else
        i2c_dev = I2C_oled;
    return (i2c_dev != I2C_oled) || oled_i2c_start(addr, rd);
}

void emu_i2c_dev_write(uint8_t b)
{
    if (i2c_dev == I2C_lcd)
        hd44780_i2c_write(b);
    else if (i2c_dev == I2C_osd)
        osd_i2c_write(b);
    else
        oled_i2c_write(b);
}

uint8_t emu_i2c_dev_read(void)
{
    return (i2c_dev == I2C_lcd) ? hd44780_i2c_read()
        : (i2c_dev == I2C_osd) ? osd_i2c_read() : oled_i2c_read();
}

void emu_i2c_dev_stop(void)
{
    hd44780_i2c_stop();
    osd_i2c_stop();
    oled_i2c_stop();
}

static void usage(FILE *f)
{
    char fdd[128] = "";
    int i, n = 0;

    for (i = 0; i < FDD_ACT_nr; i++)
        if (ui_fdd_has(i))
            n++;
    for (i = 0; i < FDD_ACT_nr; i++) {
        if (!ui_fdd_has(i))
            continue;
        n--;
        snprintf(fdd + strlen(fdd), sizeof(fdd) - strlen(fdd), "%s%s",
                 fdd_action_name[i], (n > 1) ? ", " : (n == 1) ? " or " : "");
    }

    fprintf(f,
            "Usage: " FFEMU_NAME " [<directory> | <image> | <disk>]\n"
            "\n"
            "Runs the FlashFloppy user interface in the terminal, with no "
            "Gotek hardware,\n"
            "for the %s firmware.\n"
            "The USB drive holds the files of <directory>, or is read "
            "from an image\n"
            "file or a disk such as %s.\n"
            "Without an argument, it is the USB drive attached, if "
            "there is one; if\n"
            "there are more, they are listed, each with its disk to give.\n"
            "Whatever the firmware writes to it never reaches these.\n"
            "\n"
            "Settings: %s\n"
            "\n"
            "When standard input is not a terminal, commands are read from "
            "it instead:\n"
            "  key <action>: select, left, right, cw, ccw, remove, insert, "
            "reset or quit,\n"
            "    or on the floppy interface: %s\n"
            "  sleep <ms>: let the firmware run\n"
            "  wait <ms> <log|display|segments> <text>: let it run until "
            "the console output\n"
            "    since the last match, the display's text or the LED's "
            "segments, such as\n"
            "    \"71 40 71\", show <text>, for at most <ms>; print which\n"
            "  dump: print the display\n"
            "  log: print the firmware's console output so far\n"
            "  status: print the state of the emulated device\n"
            "  flash: print the flash memory as in the settings\n"
            "  save <file>: write the USB drive as the firmware sees it\n",
            emu_fw_target(),
#ifdef __CYGWIN__
            "/dev/sdb (\\\\.\\PhysicalDrive1)",
#else
            "/dev/sdb",
#endif
            config.path, fdd);
}

/* Where ffemu runs: the system, or on Windows the runtime's installation,
 * known by files in its root; and the terminal. */
static void say_where(void)
{
    const char *tty = ttyname(STDIN_FILENO), *program = getenv("TERM_PROGRAM");
    const char *term = getenv("TERM"), *where;
    char sys[80], in[96];
    struct utsname u;

    if (bare_windows())
        where = "on Windows without MSYS2, Cygwin or Git Bash";
#ifdef __CYGWIN__
    else if (access("/cmd/git.exe", F_OK) == 0)
        where = "in Git Bash";
    else if (access("/usr/bin/pacman.exe", F_OK) == 0)
        where = "in MSYS2";
#endif
    else {
        snprintf(sys, sizeof(sys), "on %s",
                 (uname(&u) == 0) ? u.sysname : "Unix");
        where = sys;
    }

    if ((tty != NULL) && !strncmp(tty, "/dev/cons", 9))
        snprintf(in, sizeof(in), "%s", getenv("WT_SESSION")
                 ? "Windows Terminal" : "a Windows console window");
    else if ((program != NULL) && (program[0] != '\0'))
        snprintf(in, sizeof(in), "the %s terminal", program);
    else if (getenv("VTE_VERSION") != NULL)
        snprintf(in, sizeof(in), "GNOME Terminal or another VTE terminal");
    else
        snprintf(in, sizeof(in), "a terminal of type %s",
                 term ? term : "unknown");
    printf("Running %s, in %s.\n", where, in);
}

/* Waits for a key pressed from now on, taken without Enter and not echoed;
 * ends the program if the terminal goes away. */
static void wait_key(void)
{
    static const char spin[] = "-\\|/";
    struct termios old, raw;
    bool set = (tcgetattr(STDIN_FILENO, &old) == 0);
    struct pollfd p = { .fd = STDIN_FILENO, .events = POLLIN };
    char turn[2] = { '\b', 0 };
    unsigned int step = 0;
    ssize_t n = 0;
    int rc;
    char c;

    printf("Press any key to start... %c", spin[0]);
    fflush(stdout);
    if (set) {
        raw = old;
        raw.c_lflag &= ~(ICANON | ECHO);
        raw.c_cc[VMIN] = 1;
        raw.c_cc[VTIME] = 0;
        tcsetattr(STDIN_FILENO, TCSANOW, &raw);
    }
    /* Keys typed before the question do not answer it. */
    tcflush(STDIN_FILENO, TCIFLUSH);
    /* A spinner turns while ffemu waits; a write of it that fails means the
     * terminal has gone with no hangup signal. */
    for (;;) {
        rc = poll(&p, 1, 500);
        if (rc > 0) {
            n = read(STDIN_FILENO, &c, 1);
            if ((n >= 0) || (errno != EINTR))
                break;
        } else if (rc == 0) {
            turn[1] = spin[++step % 4];
            if ((write(STDOUT_FILENO, turn, 2) < 0) && (errno != EINTR)
                && (errno != EAGAIN))
                raise(SIGHUP);
        } else if (errno != EINTR) {
            break;
        }
    }
    /* The rest of a key that sends a sequence. */
    tcflush(STDIN_FILENO, TCIFLUSH);
    if (set)
        tcsetattr(STDIN_FILENO, TCSANOW, &old);
    printf("\b \n");
    if (n != 1)
        exit(0);
}

/* The first run in a terminal: where ffemu runs and keeps its files, to be
 * read before the user interface takes the screen. */
static void first_run_note(void)
{
    char buf[1024];

    say_where();
    printf("The ffemu config file created: %s\n",
           user_path(config.path, buf, sizeof(buf)));
    printf("The log will appear here: %s\n",
           user_path(log_file_name, buf, sizeof(buf)));
    if (usb_drive_dir()[0] != '\0')
        printf("The firmware's writes to the USB drive will appear here: "
               "%s\n", user_path(usb_drive_dir(), buf, sizeof(buf)));
    if (usb_saved_dir()[0] != '\0')
        printf("The Externals dialog saves or unpacks the drive here: %s\n",
               user_path(usb_saved_dir(), buf, sizeof(buf)));
    printf("Temporary files go here: %s\n",
           user_path(usb_store_dir(), buf, sizeof(buf)));
    wait_key();
}

int main(int argc, char **argv)
{
    const char *state, *store = getenv("FFEMU_USB");
    int style, usb = 1, fd;
    unsigned int sleep_ms = 0, layout, fdd, phase_pos, latch;
    struct stat st;

    /* The display is drawn with Unicode block and Braille characters. */
    setlocale(LC_ALL, "");
    if (strcmp(nl_langinfo(CODESET), "UTF-8")
        && !setlocale(LC_CTYPE, "C.UTF-8"))
        setlocale(LC_CTYPE, "en_US.UTF-8");

    cpu_init(argv);
    rc_load();
    if (config.created && (log_file_name == NULL)) {
        char buf[1024];
        if (log_file_tried != NULL)
            fprintf(stderr, "ffemu: cannot create the log file %s: %s\n",
                    user_path(log_file_tried, buf, sizeof(buf)),
                    strerror(log_file_errno));
        else
            fprintf(stderr, "ffemu: no folder for the log file: neither "
                    "XDG_STATE_HOME nor HOME is set\n");
        return 1;
    }

    if ((argc > 2) || ((argc == 2) && (argv[1][0] == '-'))) {
        bool help = (argc == 2) && (!strcmp(argv[1], "-h")
                                    || !strcmp(argv[1], "--help"));
        usage(help ? stdout : stderr);
        return help ? 0 : 2;
    }
    if (argc == 2) {
        usb_path = argv[1];
    } else {
        static char dev[32];
        char line[256];
        int nr = usb_disk_find(dev, sizeof(dev), line, sizeof(line), NULL);
        if (nr != 1) {
            fprintf(stderr, nr ? "ffemu: %d USB drives; give one of "
                    "these disks:\n"
                    : "ffemu: no USB drive attached; give a "
                    "directory, an image or a disk\n", nr);
            usb_disk_find(dev, sizeof(dev), line, sizeof(line), stderr);
            return 2;
        }
        host_log("USB drive attached: %s", line);
        usb_path = dev;
    }
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
            if (S_ISBLK(st.st_mode) && ((e == EACCES) || (e == EPERM))) {
                fprintf(stderr, "ffemu: to read a disk, run it %s\n",
#ifdef __CYGWIN__
                        "in a terminal started as administrator"
#else
                        "with sudo"
#endif
                    );
#ifdef __CYGWIN__
                /* Windows lets anyone read a volume on removable media. */
                if (!isdigit((unsigned char)usb_path[strlen(usb_path) - 1]))
                    fprintf(stderr, "ffemu: or, for a USB drive, give "
                            "its partition, such as %s1\n", usb_path);
#endif
            }
            return 2;
        }
        close(image);
    }
    usb_path_name = realpath(usb_path, NULL) ?: usb_path;

    state = cpu_restart_state();
    if ((state != NULL)
        && (sscanf(state, "%d,%d,%u,%x,%u,%x", &style, &usb, &sleep_ms,
                   &fdd, &phase_pos, &latch) == 6)
        && (style >= 1) && (style <= STYLE_nr)) {
        config.style = style;
        ui_fdd_restore(fdd, phase_pos);
        ui_latch_restore(latch);
    }

    if ((store != NULL) && (sscanf(store, "%d,%x", &fd, &layout) == 2))
        usb_set_store(fd, layout);
    unsetenv("FFEMU_USB");

    if (DISP_IS_LED(config.display))
        led_init(config.display);
    else if (DISP_IS_LCD(config.display))
        hd44780_init(config.display);
    else
        oled_init(config.display);
    /* FF OSD shares the bus with an OLED or an LCD, the header of a
     * 7-segment display being another. */
    osd_init(config.osd && !DISP_IS_LED(config.display));
    hd44780_set_rom(config.lcd_rom);
    emu_oled_synth_8x16 = config.oled_synth;
    emu_mcu = config.mcu;
    if (usb)
        insert(state != NULL);

    if (isatty(STDIN_FILENO) && isatty(STDOUT_FILENO)) {
        if (config.created)
            first_run_note();
        tui_start();
    } else {
        script_start(sleep_ms);
    }

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

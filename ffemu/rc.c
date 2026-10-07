/*
 * rc.c
 *
 * Settings, kept in ffemurc in ${XDG_CONFIG_HOME:-~/.config}/ffemu/: lines of
 * "name = value", a '#' starting a comment line. The file is written with the
 * defaults and a comment on each setting when it does not exist, and a
 * setting changed from the keyboard is written back into it, other lines
 * kept as they are. Its last section, [Flash mem], mirrors the emulated
 * flash memory, and is written anew whenever the flash changes.
 *
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

#include <ctype.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pwd.h>
#include <unistd.h>
#include <sys/stat.h>

#include "host.h"

struct config config;

const char * const style_name[STYLE_nr + 1] = {
    [STYLE_braille] = "braille",
    [STYLE_half] = "half",
    [STYLE_ascii] = "ascii"
};

/* The colors of curses, in its order; "bright-" before a name adds 8. */
static const char * const color_name[8] = {
    "black", "red", "green", "yellow", "blue", "magenta", "cyan", "white"
};
#define BRIGHT "bright-"

enum { SET_style, SET_display_color, SET_hold_ms, SET_display, SET_nr };

static const struct setting {
    const char *name, *comment;
} settings[SET_nr] = {
    [SET_style] = {
        "style",
        "How the display is drawn: braille, half (half blocks) or ascii. "
        "Keys Q W E." },
    [SET_display_color] = {
        "display-color",
        "Display color: [bright-]blue, red, magenta, green, cyan, yellow or "
        "white. Keys [Shift+]1..7." },
    [SET_hold_ms] = {
        "hold-ms",
        "How long a key press holds a button down, in milliseconds: 10 to "
        "5000." },
    [SET_display] = {
        "display",
        "Display type: ssd1306-128x32, ssd1306-128x64, sh1106-128x32 or "
        "sh1106-128x64. Key 0." }
};

/* The value of setting @set as the file has it, into @buf. */
static const char *value_of(int set, char *buf, size_t size)
{
    switch (set) {
    case SET_style:
        snprintf(buf, size, "%s", style_name[config.style]);
        break;
    case SET_display_color:
        snprintf(buf, size, "%s%s", (config.display_color & 8) ? BRIGHT : "",
                 color_name[config.display_color & 7]);
        break;
    case SET_hold_ms:
        snprintf(buf, size, "%u", config.hold_ms);
        break;
    case SET_display:
        snprintf(buf, size, "%s", display_name[config.display]);
        break;
    }
    return buf;
}

const char *user_home(void)
{
    static char home[512];
    const char *sudo = getenv("SUDO_USER");
    struct passwd *pw;

    if ((geteuid() == 0) && (sudo != NULL)
        && ((pw = getpwnam(sudo)) != NULL)) {
        snprintf(home, sizeof(home), "%s", pw->pw_dir);
        return home;
    }
    return getenv("HOME");
}

void own_file(const char *path)
{
    const char *uid = getenv("SUDO_UID"), *gid = getenv("SUDO_GID");
    int rc;

    if ((geteuid() == 0) && (uid != NULL) && (gid != NULL)) {
        rc = chown(path, atoi(uid), atoi(gid));
        (void)rc;
    }
}

void make_dirs(char *dir)
{
    char *p;

    for (p = dir + 1; *p != '\0'; p++) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(dir, 0755) == 0)
                own_file(dir);
            *p = '/';
        }
    }
    if (mkdir(dir, 0755) == 0)
        own_file(dir);
}

/* Strips blanks from both ends of @s, in place. */
static char *trim(char *s)
{
    char *e;

    while (isspace((unsigned char)*s))
        s++;
    for (e = s + strlen(s); (e > s) && isspace((unsigned char)e[-1]); e--)
        continue;
    *e = '\0';
    return s;
}

/* The setting that a "name = value" line @line is about, or -1. */
static int setting_of(const char *line)
{
    const char *eq = strchr(line, '=');
    size_t n;
    int i;

    if (eq == NULL)
        return -1;
    for (n = eq - line; (n != 0) && isspace((unsigned char)line[n-1]); n--)
        continue;
    for (i = 0; i < SET_nr; i++)
        if ((strlen(settings[i].name) == n)
            && !strncmp(line, settings[i].name, n))
            return i;
    return -1;
}

/* Returns FALSE if the line makes no sense. */
static bool parse_line(char *line)
{
    char *value = strchr(line, '=');
    int set = setting_of(line);
    unsigned int i;

    if (set < 0)
        return false;
    value = trim(value + 1);

    switch (set) {
    case SET_style:
        for (i = 1; i <= STYLE_nr; i++) {
            if (!strcmp(value, style_name[i])) {
                config.style = i;
                return true;
            }
        }
        return false;
    case SET_display_color: {
        unsigned int bright = 0;
        if (!strncmp(value, BRIGHT, strlen(BRIGHT))) {
            value += strlen(BRIGHT);
            bright = 8;
        }
        /* Not black, which would hide the pixels. */
        for (i = 1; i < ARRAY_SIZE(color_name); i++) {
            if (!strcmp(value, color_name[i])) {
                config.display_color = i | bright;
                return true;
            }
        }
        return false;
    }
    case SET_hold_ms: {
        int ms = atoi(value);
        if ((ms < 10) || (ms > 5000))
            return false;
        config.hold_ms = ms;
        return true;
    }
    case SET_display:
        for (i = 0; i < DISP_nr; i++) {
            if (!strcmp(value, display_name[i])) {
                config.display = i;
                return true;
            }
        }
        return false;
    }

    return false;
}

/* The longest value, which is that of the flash, and the longest line. */
#define RC_VALUE_MAX 2048
#define RC_LINE_MAX 4096

/* Writes @text to the settings file, by way of a new file put in its place. */
static bool write_file(const char *text)
{
    char dir[sizeof(config.path)], tmp[sizeof(config.path) + 8], *slash;
    FILE *f;
    bool ok;

    snprintf(dir, sizeof(dir), "%s", config.path);
    slash = strrchr(dir, '/');
    if (slash != NULL) {
        *slash = '\0';
        make_dirs(dir);
    }

    snprintf(tmp, sizeof(tmp), "%s.new", config.path);
    f = fopen(tmp, "w");
    if (f == NULL) {
        host_log("Cannot write %s: %s", tmp, strerror(errno));
        return false;
    }
    ok = (fputs(text, f) >= 0);
    ok = (fclose(f) == 0) && ok;
    if (ok)
        own_file(tmp);
    if (ok && (rename(tmp, config.path) != 0)) {
        host_log("Cannot replace %s: %s", config.path, strerror(errno));
        ok = false;
    }
    if (!ok)
        remove(tmp);
    return ok;
}

/* Appends to @out the line of setting @set. */
static void append_value(char *out, size_t size, int set)
{
    char value[RC_VALUE_MAX];
    size_t n = strlen(out);

    snprintf(out + n, size - n, "%s = %s\n", settings[set].name,
             value_of(set, value, sizeof(value)));
}

/*
 * The [Flash mem] section: the bytes of the flash as hex, and its options as
 * FF.CFG sets them. On loading, the hex is applied first and the options on
 * top, so that bytes no option covers come from the hex.
 */

#define FLASH_SECTION "[Flash mem]"
#define FLASH_COMMENT \
    "# The firmware's configuration in its flash memory, rewritten\n" \
    "# whenever the firmware writes it: its bytes as hex, and its options\n" \
    "# as FF.CFG sets them, which win where the two differ. Nothing for\n" \
    "# empty flash.\n"

/* The section as read. */
static struct {
    char hex[600];
    bool has_hex;
    unsigned int nr;
    struct {
        char name[40], value[80];
        unsigned int line;
    } opt[64];
} flash_lines;

/* Splits "name = value" @p, returning the value, or NULL. */
static char *split_line(char *p, char **name)
{
    char *eq = strchr(p, '=');

    if (eq == NULL)
        return NULL;
    *eq = '\0';
    *name = trim(p);
    return trim(eq + 1);
}

/* Takes line @nr of the section, @p. */
static void read_flash_line(char *p, unsigned int nr)
{
    char *name, *value = split_line(p, &name);

    if (value == NULL) {
        host_log("ffemurc: line %u not understood", nr);
    } else if (!strcmp(name, "hex")) {
        snprintf(flash_lines.hex, sizeof(flash_lines.hex), "%s", value);
        flash_lines.has_hex = true;
    } else if (flash_lines.nr < ARRAY_SIZE(flash_lines.opt)) {
        snprintf(flash_lines.opt[flash_lines.nr].name,
                 sizeof(flash_lines.opt[0].name), "%s", name);
        snprintf(flash_lines.opt[flash_lines.nr].value,
                 sizeof(flash_lines.opt[0].value), "%s", value);
        flash_lines.opt[flash_lines.nr++].line = nr;
    } else {
        host_log("ffemurc: line %u: too many lines in %s", nr, FLASH_SECTION);
    }
}

/* Puts what the section says into the emulated flash. */
static void load_flash(void)
{
    uint8_t cfg[256], tmp[256];
    unsigned int size = emu_flash_cfg_size(), len = 0, nr_set = 0, i;

    if (flash_lines.has_hex) {
        len = hex_to_bytes(cfg, sizeof(cfg), flash_lines.hex);
        if (2 * len != strlen(flash_lines.hex))
            host_log("ffemurc: %s hex not understood beyond byte %u",
                     FLASH_SECTION, len);
    }
    if (flash_lines.nr != 0) {
        if (len != size) {
            if (flash_lines.has_hex)
                host_log("ffemurc: %s hex has %u bytes, not %u: the options "
                         "apply to the defaults", FLASH_SECTION, len, size);
            emu_flash_cfg_defaults(cfg);
            len = size;
        }
        for (i = 0; i < flash_lines.nr; i++) {
            const char *name = flash_lines.opt[i].name;
            const char *value = flash_lines.opt[i].value;
            unsigned int line = flash_lines.opt[i].line;
            memcpy(tmp, cfg, size);
            switch (emu_flash_set_option(tmp, name, value)) {
            case EMU_SET_unknown:
                host_log("ffemurc: line %u: %s is not in this firmware's "
                         "flash: kept, but not applied", line, name);
                continue;
            case EMU_SET_bad:
                host_log("ffemurc: line %u: %s = %s does not fit the flash: "
                         "not applied", line, name, value);
                continue;
            }
            if (flash_lines.has_hex && memcmp(tmp, cfg, size))
                host_log("ffemurc: line %u: %s differs from the hex: the "
                         "option wins", line, name);
            memcpy(cfg, tmp, size);
            nr_set++;
        }
        if (!flash_lines.has_hex && (nr_set < emu_flash_nr_options()))
            host_log("ffemurc: %s lacks %u options, which take the "
                     "firmware's defaults", FLASH_SECTION,
                     emu_flash_nr_options() - nr_set);
    }
    flash_set(cfg, len);
}

/* A line of the section, @p, for an option that this firmware's flash has
 * not got: kept in the section as it is. */
static bool foreign_option(const char *line)
{
    char copy[RC_LINE_MAX], *name, *value;
    uint8_t cfg[256];

    snprintf(copy, sizeof(copy), "%s", line);
    value = split_line(trim(copy), &name);
    if ((value == NULL) || (*name == '#') || !strcmp(name, "hex"))
        return false;
    emu_flash_cfg_defaults(cfg);
    return emu_flash_set_option(cfg, name, value) == EMU_SET_unknown;
}

/* Appends the section, as the flash is now, to @out, and then @kept. */
static void append_flash(char *out, size_t size, const char *kept)
{
    uint8_t cfg[256];
    char value[600];
    unsigned int len = emu_flash_load(cfg, sizeof(cfg)), i, flags;
    const char *name;
    size_t n = strlen(out);

    /* One empty line before the section, however many there were. */
    while ((n >= 2) && (out[n-1] == '\n') && (out[n-2] == '\n'))
        out[--n] = '\0';
    snprintf(out + n, size - n, "\n%s\n%s", FLASH_SECTION, FLASH_COMMENT);

    if (len != 0) {
        flash_to_hex(value, sizeof(value));
        n = strlen(out);
        snprintf(out + n, size - n, "hex = %s\n", value);
    }
    for (i = 0; (len == emu_flash_cfg_size()) && (i < emu_flash_nr_options());
         i++) {
        name = emu_flash_option(i, cfg, value, sizeof(value), &flags);
        n = strlen(out);
        if (flags & EMU_OPT_hex_only)
            snprintf(out + n, size - n, "# %s: only in the hex\n", name);
        else
            snprintf(out + n, size - n, "%s = %s\n", name, value);
    }
    n = strlen(out);
    snprintf(out + n, size - n, "%s", kept);
}

/* Appends to @out the lines of setting @set: its comment and its value. */
static void append_setting(char *out, size_t size, int set)
{
    size_t n = strlen(out);

    snprintf(out + n, size - n, "%s# %s\n", n ? "\n" : "",
             settings[set].comment);
    append_value(out, size, set);
}

void rc_flash_section(char *buf, size_t size)
{
    buf[0] = 0;
    append_flash(buf, size, "");
}

void rc_save(void)
{
    /* The firmware thread writes the flash, the user interface the rest. */
    static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
    static char out[16384], kept[4096];
    char line[RC_LINE_MAX];
    bool seen[SET_nr] = { false }, in_flash = false;
    sigset_t tick, saved;
    size_t n;
    FILE *f;
    int i;

    /* The tick could cancel the firmware's call that got here, with the
     * lock held. */
    sigemptyset(&tick);
    sigaddset(&tick, SIGALRM);
    pthread_sigmask(SIG_BLOCK, &tick, &saved);
    pthread_mutex_lock(&lock);

    f = fopen(config.path, "r");
    out[0] = kept[0] = '\0';
    if (f == NULL) {
        snprintf(out, sizeof(out),
                 "# Settings of ffemu, rewritten when changed in the UI.\n");
    } else {
        /* The line of a setting gets its present value; the rest stay. */
        while (fgets(line, sizeof(line), f) != NULL) {
            char copy[sizeof(line)], *p;
            int set;
            line[strcspn(line, "\r\n")] = '\0';
            snprintf(copy, sizeof(copy), "%s", line);
            p = trim(copy);
            /* The section is written anew, save lines for options that
             * the flash has not got. */
            if (*p == '[')
                in_flash = !strcmp(p, FLASH_SECTION);
            if (in_flash) {
                n = strlen(kept);
                if (foreign_option(p)
                    && (n + strlen(line) + 2 <= sizeof(kept))) {
                    memcpy(kept + n, line, strlen(line));
                    memcpy(kept + n + strlen(line), "\n", 2);
                }
                continue;
            }
            set = (*p == '#') ? -1 : setting_of(p);
            if ((set >= 0) && !seen[set]) {
                seen[set] = true;
                append_value(out, sizeof(out), set);
            } else {
                n = strlen(out);
                snprintf(out + n, sizeof(out) - n, "%s\n", line);
            }
        }
        fclose(f);
    }

    for (i = 0; i < SET_nr; i++)
        if (!seen[i])
            append_setting(out, sizeof(out), i);
    append_flash(out, sizeof(out), kept);

    config.loaded = write_file(out) || config.loaded;

    pthread_mutex_unlock(&lock);
    pthread_sigmask(SIG_SETMASK, &saved, NULL);
}

void rc_load(void)
{
    const char *xdg = getenv("XDG_CONFIG_HOME"), *home = user_home();
    char line[RC_LINE_MAX], *p;
    unsigned int nr = 0;
    bool in_flash = false;
    FILE *f;

    config.style = STYLE_braille;
    config.display = DISP_ssd1306_32;
    config.display_color = 8 | 6; /* bright cyan */
    config.hold_ms = 150;

    if ((xdg != NULL) && (xdg[0] != '\0'))
        snprintf(config.path, sizeof(config.path), "%s/ffemu/ffemurc", xdg);
    else
        snprintf(config.path, sizeof(config.path), "%s/.config/ffemu/ffemurc",
                 home ? home : "");

    f = fopen(config.path, "r");
    if (f == NULL) {
        /* The first run: write the defaults, to be seen and edited. */
        if (errno == ENOENT)
            rc_save();
        return;
    }
    config.loaded = true;

    while (fgets(line, sizeof(line), f) != NULL) {
        nr++;
        p = trim(line);
        if ((*p == '\0') || (*p == '#'))
            continue;
        if (*p == '[') {
            in_flash = !strcmp(p, FLASH_SECTION);
            if (!in_flash)
                host_log("ffemurc: line %u: unknown section %s", nr, p);
        } else if (in_flash) {
            read_flash_line(p, nr);
        } else if (!parse_line(p)) {
            host_log("ffemurc: line %u not understood", nr);
        }
    }

    fclose(f);

    /* After a power cycle, the flash comes from the program before. */
    if (cpu_restart_state() == NULL)
        load_flash();
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

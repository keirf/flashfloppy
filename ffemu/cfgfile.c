/*
 * cfgfile.c
 *
 * Writes FF.CFG on the USB drive from the configuration in the emulated
 * flash memory, as the Flash mem dialog's buttons ask: every option, or
 * only those that differ from the defaults. The file is generated afresh
 * from the firmware's example FF.CFG, which documents each option above its
 * line, with the values taken from flash; the previous file is kept as
 * FF.CFG.BAK. A drive made from a directory is written in that directory;
 * a real drive where the system has it mounted; an image file not at all.
 *
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <unistd.h>

#include "version.h"
#include "host.h"

/* examples/FF.CFG of the firmware, built into the program. */
extern const char ff_cfg_example[];

/* The name of the option that @line sets, into @name, or false. */
static bool option_line(const char *line, unsigned int len, char *name,
                        size_t size)
{
    unsigned int i = 0, n = 0;

    while ((i < len) && ((line[i] == ' ') || (line[i] == '\t')))
        i++;
    while ((i < len) && (n + 1 < size)
           && (isalnum((unsigned char)line[i]) || (line[i] == '-')))
        name[n++] = line[i++];
    name[n] = '\0';
    while ((i < len) && ((line[i] == ' ') || (line[i] == '\t')))
        i++;
    return (n != 0) && (i < len) && (line[i] == '=');
}

/* The index of the flash option named @name, or -1. */
static int option_index(const void *cfg, const char *name)
{
    char value[80];
    unsigned int i, flags;

    for (i = 0; i < emu_flash_nr_options(); i++)
        if (!strcmp(emu_flash_option(i, cfg, value, sizeof(value), &flags),
                    name))
            return i;
    return -1;
}

/* Writes the file to @f: the example's lines, each option's value replaced
 * by the one in @cfg, and unless @all the options left out, with their
 * comments, whose value is what @flash holds: the firmware keeps the flash
 * value of an option the file does not name, so the rest need no line.
 * Returns the number of options written. */
static unsigned int generate(FILE *f, const uint8_t *cfg,
                             const uint8_t *flash, bool all)
{
    const char *p = ff_cfg_example, *pending = p, *next;
    char name[32], value[80], held[80], date[32];
    unsigned int nr = 0, flags, hflags, len;
    time_t now = time(NULL);
    int i;

    strftime(date, sizeof(date), "%Y-%m-%d %H:%M", localtime(&now));
    fprintf(f, "## Written by ffemu %s on %s from the flash memory: %s.\r\n"
            "\r\n", FFEMU_VERSION, date,
            all ? "every option"
            : "the options that differ from flash");

    /* @pending is where the comment lines above the next option begin. */
    for (; *p != '\0'; p = next) {
        len = strcspn(p, "\n");
        next = p + len + (p[len] == '\n');
        if (len && (p[len - 1] == '\r'))
            len--;
        if (option_line(p, len, name, sizeof(name))) {
            i = option_index(cfg, name);
            if (i >= 0) {
                emu_flash_option(i, cfg, value, sizeof(value), &flags);
                emu_flash_option(i, flash, held, sizeof(held), &hflags);
                if (all || strcmp(value, held)) {
                    fwrite(pending, 1, p - pending, f);
                    if (flags & EMU_OPT_hex_only)
                        fprintf(f, "# %s: a value FF.CFG cannot give, kept in "
                                "flash as it is\r\n", name);
                    else
                        fprintf(f, "%s = %s\r\n", name, value);
                    if (!all)
                        fputs("\r\n", f);
                    nr++;
                }
            }
            pending = next;
        } else if ((len == 0) || (p[0] != '#') || (p[1] == '#')) {
            /* A blank line, a section header or text: kept only in the
             * full file, and the comments above it belong to nothing. */
            if (all) {
                fwrite(pending, 1, p - pending, f);
                fwrite(p, 1, len, f);
                fputs("\r\n", f);
            }
            pending = next;
        }
    }
    return nr;
}

bool ff_cfg_write(const void *cfg, bool all, char *msg, size_t size)
{
    struct usb_info usb;
    uint8_t flash[256];
    char place[16], dir[4000], path[4096], tmp[4104], bak[4104];
    struct stat st;
    bool had_old;
    unsigned int nr;
    FILE *f;

    usb_get_info(&usb);
    if (usb.kind == USB_dir) {
        snprintf(dir, sizeof(dir), "%s", usb_path);
    } else if (usb.kind == USB_image) {
        snprintf(msg, size, "FF.CFG cannot be written into an image file");
        return false;
    } else if (!usb_disk_mount(usb_path, dir, sizeof(dir))) {
        snprintf(msg, size, "%s is not mounted by the system; mount it, and "
                 "FF.CFG is written there", usb_path);
        return false;
    }
    usb_ff_cfg_place(dir, place, sizeof(place));
    /* A mount point may end in a slash already, as "J:/" does. */
    if (dir[strlen(dir) - 1] == '/')
        dir[strlen(dir) - 1] = '\0';
    snprintf(path, sizeof(path), "%s/%s", dir, place);
    snprintf(tmp, sizeof(tmp), "%s.new", path);
    snprintf(bak, sizeof(bak), "%s.BAK", path);

    f = fopen(tmp, "wb");
    if (f == NULL) {
        snprintf(msg, size, "%s: %s", tmp, strerror(errno));
        return false;
    }
    emu_flash_get(flash);
    nr = generate(f, cfg, flash, all);
    if (fclose(f) != 0) {
        snprintf(msg, size, "%s: %s", tmp, strerror(errno));
        remove(tmp);
        return false;
    }

    /* The old file becomes the .BAK, in place of the previous one. */
    had_old = (stat(path, &st) == 0);
    if (had_old && (remove(bak) != 0) && (errno != ENOENT)) {
        snprintf(msg, size, "%s: %s", bak, strerror(errno));
        remove(tmp);
        return false;
    }
    if (had_old && (rename(path, bak) != 0)) {
        snprintf(msg, size, "%s: %s", bak, strerror(errno));
        remove(tmp);
        return false;
    }
    if (rename(tmp, path) != 0) {
        snprintf(msg, size, "%s: %s", path, strerror(errno));
        remove(tmp);
        return false;
    }
    snprintf(msg, size, "%s/%s written with %u option%s%s", dir, place, nr,
             (nr == 1) ? "" : "s",
             had_old ? ", the old one kept as .BAK" : "");
    return true;
}

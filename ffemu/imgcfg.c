/*
 * imgcfg.c
 *
 * The section of IMG.CFG that the firmware applies to the image mounted,
 * chosen as the firmware's img.c chooses it: among the sections whose tag
 * is the image's, the part of its name after its last dot, or empty, and
 * whose size, if given after "::", is the image's, the first with the best
 * score, a tag match worth 4, a size match 2, the empty tag 1, and a
 * mismatch -100.
 *
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "host.h"

/* IMG.CFG as read from the drive, and the section chosen, copied out. */
static char file[16384], section[sizeof(file)];
static struct imgcfg_view cur;

/* What the choice was last made for: the drive's state and the image. */
static unsigned long writes_seen = ~0ul;
static unsigned int gen_seen, size_seen;
static char name_seen[64];

/* Whether the firmware reads IMG.CFG for an image of extension @type: the
 * raw sector images, and .dsk, which it tries as a raw image when the DSK
 * format does not take it. */
static bool raw_type(const char *type)
{
    static const char * const raw[] = { "img", "ima", "out", "dsk" };
    unsigned int i;

    for (i = 0; i < ARRAY_SIZE(raw); i++)
        if (!strcasecmp(type, raw[i]))
            return true;
    return false;
}

/* Whether @c may be in a section's tag: the characters that the firmware's
 * parser takes for a name. */
static bool tag_char(char c)
{
    return isalnum((unsigned char)c) || (strchr(",-._:", c) != NULL);
}

/* The firmware's score of the section line @line, whose tag it copies into
 * @tag, for the image whose tag is @img_tag, NULL if it has none, and whose
 * size is @size. */
static int score(const char *line, char *tag, size_t tag_size,
                 const char *img_tag, unsigned int size)
{
    const char *p = line + 1, *q;
    size_t n;
    int active = 0;

    for (n = 0; tag_char(p[n]); n++)
        continue;
    if (n > tag_size - 1)
        n = tag_size - 1;
    memcpy(tag, p, n);
    tag[n] = '\0';
    /* A size after "::" is worth less than the tag, and a mismatch on it
     * clobbers the section; the tag ends there. */
    q = strstr(tag, "::");
    if (q != NULL) {
        active += ((unsigned int)strtol(q + 2, NULL, 10) == size) ? 2 : -100;
        n = q - tag;
    }
    if ((img_tag != NULL) && !strncasecmp(tag, img_tag, n)
        && (img_tag[n] == '\0'))
        active += 4;
    else if (n == 0)
        active += 1;
    else
        active -= 100;
    return active;
}

/* Chooses the section of @file for the image: its text into @section, its
 * header into @cur. */
static void choose(const char *img_tag, unsigned int size)
{
    const char *p, *start = NULL, *end = NULL, *s;
    char tag[64];
    int match = 0, active;

    cur.header[0] = '\0';
    cur.state = IMGCFG_no_section;
    for (p = file; *p != '\0'; p += strcspn(p, "\n"), p += (*p == '\n')) {
        for (s = p; (*s == ' ') || (*s == '\t'); s++)
            continue;
        if (*s != '[')
            continue;
        if ((start != NULL) && (end == NULL))
            end = p;
        active = score(s, tag, sizeof(tag), img_tag, size);
        if (active <= match)
            continue;
        /* The best so far, which a later one may still beat. */
        match = active;
        start = p;
        end = NULL;
        if (tag[0] == '\0')
            cur.header[0] = '\0';
        else
            snprintf(cur.header, sizeof(cur.header), "[%s]", tag);
    }
    if (start == NULL)
        return;
    if (end == NULL)
        end = file + strlen(file);
    /* Without the blank lines before the next section. */
    while (end > start) {
        for (s = end - 1; (s > start) && (s[-1] != '\n'); s--)
            continue;
        if (strspn(s, " \t\r\n") < (size_t)(end - s))
            break;
        end = s;
    }
    memcpy(section, start, end - start);
    section[end - start] = '\0';
    cur.state = IMGCFG_section;
    cur.text = section;
    cur.nr_lines = 0;
    for (p = section; *p != '\0'; p += strcspn(p, "\n"), p += (*p == '\n'))
        cur.nr_lines++;
}

void imgcfg_get_view(struct imgcfg_view *v)
{
    struct usb_info usb;
    const char *name, *type, *dot;
    unsigned int size;
    long n;

    usb_get_info(&usb);
    if (!usb.inserted || !emu_fdd_image(&name, &type, &size)
        || !raw_type(type)) {
        writes_seen = ~0ul;
        memset(v, 0, sizeof(*v));
        v->state = IMGCFG_none;
        return;
    }
    if ((usb.nr_writes != writes_seen) || (usb.ff_cfg_gen != gen_seen)
        || (size != size_seen) || strcmp(name, name_seen)) {
        writes_seen = usb.nr_writes;
        gen_seen = usb.ff_cfg_gen;
        size_seen = size;
        snprintf(name_seen, sizeof(name_seen), "%s", name);
        memset(&cur, 0, sizeof(cur));
        n = usb_read_file("IMG     CFG", file, sizeof(file) - 1);
        if (n < 0) {
            cur.state = IMGCFG_none;
        } else {
            file[n] = '\0';
            dot = strrchr(name, '.');
            choose(dot ? dot + 1 : NULL, size);
        }
    }
    *v = cur;
}

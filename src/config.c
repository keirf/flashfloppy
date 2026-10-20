/*
 * config.c
 * 
 * INI-style config file parsing.
 * 
 * Written & released by Keir Fraser <keir.xen@gmail.com>
 * 
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

/* Alphanumeric plus ",-._:" */
static int isvalid(char c)
{
    const static char map[] = {
        /* , (2c) - (2d) . (2e) 0-9 (30-39) : (3a)
         *  A-Z (41-5a) _ (5f) a-z (61-7a) */
        0x00, 0x00, 0x00, 0x00, /* 0x00-0x1f */
        0x00, 0x0e, 0xff, 0xe0, /* 0x20-0x3f */
        0x7f, 0xff, 0xff, 0xe1, /* 0x40-0x5f */
        0x7f, 0xff, 0xff, 0xe0  /* 0x60-0x7f */
    };
    return ((c/8) < sizeof(map)) ? (int8_t)(map[c/8] << (c&7)) < 0 : FALSE;
}

/* The index of @s among @list's words, each ended by a NUL, the list by an
 * empty one; the number of words if none matches. */
unsigned int word_index(const char *s, const char *list)
{
    unsigned int i;

    for (i = 0; *list != '\0'; i++, list += strlen(list) + 1)
        if (!strcmp(s, list))
            break;
    return i;
}

int get_next_opt(struct opts *opts)
{
    char *p, c;
    unsigned int i;
    bool_t section;

    F_read(opts->file, &c, 1, NULL);
next_line:
    if (c == '\0')
        return OPT_eof;
    /* Skip leading whitespace. */
    while (isspace(c))
        F_read(opts->file, &c, 1, NULL);

    /* Option name parsing. */
    section = (c == '['); /* "[section]" */
    if (section)
        F_read(opts->file, &c, 1, NULL);
    p = opts->arg;
    while (isvalid(c) && ((p-opts->arg) < (opts->argmax-1))) {
        *p++ = c;
        F_read(opts->file, &c, 1, NULL);
    }
    *p = '\0';
    if (section)
        return OPT_section;
    /* Look for a match in the accepted options list. */
    i = word_index(opts->arg, opts->opts);
    if (i >= opts->nr) {
        /* No match? Then skip to next line and try again. */
        while ((c != '\r') && (c != '\n') && (c != '\0'))
            F_read(opts->file, &c, 1, NULL);
        goto next_line;
    }

    /* Skip whitespace (and equals) between option name and value. */
    while ((c == ' ') || (c == '\t') || (c == '='))
        F_read(opts->file, &c, 1, NULL);

    /* Option value parsing: */
    p = opts->arg;
    if (c == '"') {
        /* Arbitrary quoted string. Anything goes except NL/CR. */
        F_read(opts->file, &c, 1, NULL);
        while ((c != '\r') && (c != '\n') && (c != '"') && (c != '\0')
               && ((p-opts->arg) < (opts->argmax-1))) {
            *p++ = c;
            F_read(opts->file, &c, 1, NULL);
        }
    } else {
        /* Non-quoted value: restricted character set. */
        while (isvalid(c) && ((p-opts->arg) < (opts->argmax-1))) {
            *p++ = c;
            F_read(opts->file, &c, 1, NULL);
        }
    }
    *p = '\0';

    return i;
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

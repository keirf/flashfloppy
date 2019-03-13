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

/* Alphanumeric plus ",-_:" */
static int isvalid(char c)
{
    const static char map[] = {
        /* , (2c) - (2d) 0-9 (30-39) : (3a) A-Z (41-5a) _ (5f) a-z (61-7a) */
        0x00, 0x00, 0x00, 0x00, /* 0x00-0x1f */
        0x00, 0x0c, 0xff, 0xe0, /* 0x20-0x2f */
        0x7f, 0xff, 0xff, 0xe1, /* 0x30-0x3f */
        0x7f, 0xff, 0xff, 0xe0  /* 0x40-0x4f */
    };
    return ((c/8) < sizeof(map)) ? (int8_t)(map[c/8] << (c&7)) < 0 : FALSE;
}

int get_next_opt(struct opts *opts)
{
    char *p, c;
    const struct opt *opt;
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
    for (opt = opts->opts; opt->name; opt++)
        if (!strcmp(opt->name, opts->arg))
            break;
    if (!opt->name) {
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

    return opt - opts->opts;
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

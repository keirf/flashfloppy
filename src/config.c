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

static int isvalid(char c)
{
    return (((c >= 'A') && (c <= 'Z'))
            || ((c >= 'a') && (c <= 'z'))
            || ((c >= '0') && (c <= '9'))
            || (c == '-') || (c == ','));
}

int get_next_opt(struct opts *opts)
{
    char *p, c;
    const struct opt *opt;

    F_read(opts->file, &c, 1, NULL);
next_line:
    if (c == '\0')
        return -1; /* eof */
    /* Skip leading whitespace. */
    while (isspace(c))
        F_read(opts->file, &c, 1, NULL);

    /* Option name parsing. */
    p = opts->arg;
    while (isvalid(c) && ((p-opts->arg) < (opts->argmax-1))) {
        *p++ = c;
        F_read(opts->file, &c, 1, NULL);
    }
    *p = '\0';
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
        /* Non-quoted value: pretty much alphanumeric only is accepted. */
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

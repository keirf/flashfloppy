/*
 * logfile.c
 * 
 * printf-style interface to a log file.
 * 
 * Written & released by Keir Fraser <keir.xen@gmail.com>
 * 
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

/* We buffer logging in a ring buffer. */
static char ring[2048];
#define MASK(x) ((x)&(sizeof(ring)-1))
static unsigned int cons, prod;

/* Shut loggers up while we are sending to the logging file. */
static bool_t quiesce;

int vprintk(const char *format, va_list ap)
{
    static char str[128];
    char *p, c;
    int n;

    IRQ_global_disable();

    n = vsnprintf(str, sizeof(str), format, ap);

    if (quiesce)
        goto out;

    p = str;
    while ((c = *p++) != '\0') {
        switch (c) {
        case '\r': /* CR: ignore as we generate our own CR/LF */
            break;
        case '\n': /* LF: convert to CR/LF (usual terminal behaviour) */
            ring[MASK(prod++)] = '\r';
            /* fall through */
        default:
            ring[MASK(prod++)] = c;
            break;
        }
    }

out:
    IRQ_global_enable();

    return n;
}

int printk(const char *format, ...)
{
    va_list ap;
    int n;

    va_start(ap, format);
    n = vprintk(format, ap);
    va_end(ap);

    return n;
}

void logfile_flush(FIL *file)
{
    unsigned int nr;
    char msg[20];

    F_open(file, "FFLOG.TXT", FA_OPEN_APPEND|FA_WRITE);

    quiesce = TRUE;
    barrier();

    if ((unsigned int)(prod-cons) > sizeof(ring)) {
        nr = prod - cons - sizeof(ring);
        snprintf(msg, sizeof(msg), "\r\n[lost %u]\r\n", nr);
        F_write(file, msg, strlen(msg), NULL);
        cons += nr;
    }

    while (cons != prod) {
        nr = min_t(unsigned int, prod-cons, sizeof(ring)-MASK(cons));
        F_write(file, &ring[MASK(cons)], nr, NULL);
        cons += nr;
    }

    barrier();
    quiesce = FALSE;

    F_close(file);
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

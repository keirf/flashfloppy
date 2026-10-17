/*
 * ft_main.c
 *
 * The test runner, on the host's C library: runs the registered tests,
 * reports their failures, and gives the firmware the few things that need
 * the host: cancellable calls and a target for its assertions.
 *
 * Usage: fftest [-v] [name...], where a name runs the tests that contain it;
 * fftest-apple2 likewise.
 *
 * Written & released by Mike Shevchenko <mike.shevchenko.kz@gmail.com>
 *
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

#include <setjmp.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ft.h"

/* The program's name in its output. */
#ifndef FT_NAME
#define FT_NAME "fftest"
#endif

#define MAX_TESTS 256

static struct test {
    const char *name;
    void (*fn)(void);
} tests[MAX_TESTS];
static unsigned int nr_tests;

static const char *cur_test;
static unsigned int nr_failures;
static jmp_buf abort_jb;

void ft_register(const char *name, void (*fn)(void))
{
    if (nr_tests == MAX_TESTS) {
        fprintf(stderr, FT_NAME ": too many tests\n");
        exit(2);
    }
    tests[nr_tests].name = name;
    tests[nr_tests].fn = fn;
    nr_tests++;
}

static void vfail(const char *file, int line, const char *fmt, va_list ap)
{
    const char *base = strrchr(file, '/');

    if (nr_failures++ == 0)
        printf("FAIL %s\n", cur_test);
    printf("  %s:%d: ", base ? base + 1 : file, line);
    vprintf(fmt, ap);
    printf("\n");
}

void ft_fail(const char *file, int line, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vfail(file, line, fmt, ap);
    va_end(ap);
}

void ft_abort(const char *file, int line, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vfail(file, line, fmt, ap);
    va_end(ap);
    longjmp(abort_jb, 1);
}

static void dump_row(const char *expr, const uint8_t *p, unsigned long from,
                     unsigned long to, unsigned long mark)
{
    unsigned long i;
    printf("    %s at %lu:", expr, from);
    for (i = from; i < to; i++)
        printf((i == mark) ? " [%02x]" : " %02x", p[i]);
    printf("\n");
}

int ft_check_mem(const char *file, int line, const char *a_expr,
                 const char *b_expr, const void *a, const void *b,
                 unsigned long len)
{
    const uint8_t *pa = a, *pb = b;
    unsigned long i, first, nr = 0, from, to;

    for (i = 0; (i < len) && (pa[i] == pb[i]); i++)
        continue;
    if (i == len)
        return 1;
    first = i;
    for (; i < len; i++)
        nr += (pa[i] != pb[i]);
    ft_fail(file, line, "%s and %s differ at byte %lu of %lu"
            " (%lu bytes differ)", a_expr, b_expr, first, len, nr);
    from = first & ~15ul;
    to = (from + 16 < len) ? from + 16 : len;
    dump_row(a_expr, pa, from, to, first);
    dump_row(b_expr, pb, from, to, first);
    return 0;
}

int ft_check_str(const char *file, int line, const char *a_expr,
                 const char *b_expr, const char *a, const char *b)
{
    if (!strcmp(a, b))
        return 1;
    ft_fail(file, line, "%s == %s failed: \"%s\" != \"%s\"",
            a_expr, b_expr, a, b);
    return 0;
}

/* The firmware's ASSERT(). */
void emu_illegal(const char *file, int line) __attribute__((noreturn));
void emu_illegal(const char *file, int line)
{
    ft_abort(file, line, "firmware assertion failed");
}

/* Cancellable calls: the firmware abandons a call tree when a file
 * operation fails. Here that is a jump back to the caller. */
struct cancellation {
    uint32_t *sp; /* as in the firmware: non-NULL while the call is active */
};

static jmp_buf cancel_jb[4];
static unsigned int cancel_depth;

int call_cancellable_fn(struct cancellation *c, int (*fn)(void *), void *arg)
{
    volatile unsigned int depth = cancel_depth;
    volatile int rc = -1;

    if (depth == sizeof(cancel_jb) / sizeof(cancel_jb[0]))
        ft_abort(__FILE__, __LINE__, "cancellable calls nested too deep");
    if (setjmp(cancel_jb[depth]) == 0) {
        cancel_depth = depth + 1;
        c->sp = (uint32_t *)&cancel_jb[depth];
        rc = (*fn)(arg);
    }
    c->sp = NULL;
    cancel_depth = depth;
    return rc;
}

void cancel_call(struct cancellation *c)
{
    jmp_buf *jb = (jmp_buf *)c->sp;
    if (jb == NULL)
        return;
    c->sp = NULL;
    longjmp(*jb, 1);
}

static int by_name(const void *a, const void *b)
{
    return strcmp(((const struct test *)a)->name,
                  ((const struct test *)b)->name);
}

static int selected(const char *name, int argc, char **argv)
{
    int i, any = 0;
    for (i = 1; i < argc; i++) {
        if (argv[i][0] == '-')
            continue;
        any = 1;
        if (strstr(name, argv[i]))
            return 1;
    }
    return !any;
}

int main(int argc, char **argv)
{
    unsigned int i, nr_run = 0, nr_failed = 0;
    int verbose = 0;

    for (i = 1; i < (unsigned int)argc; i++)
        if (!strcmp(argv[i], "-v"))
            verbose = 1;

    qsort(tests, nr_tests, sizeof(tests[0]), by_name);
    for (i = 0; i < nr_tests; i++) {
        if (!selected(tests[i].name, argc, argv))
            continue;
        cur_test = tests[i].name;
        nr_failures = 0;
        cancel_depth = 0;
        ft_fw_log_clear();
        if (setjmp(abort_jb) == 0)
            (*tests[i].fn)();
        nr_run++;
        if (nr_failures) {
            nr_failed++;
            printf("  firmware console:\n%s", ft_fw_log());
        } else if (verbose) {
            printf("ok   %s\n", cur_test);
        }
    }

    printf(FT_NAME ": %u tests, %u failed\n", nr_run, nr_failed);
    return nr_failed ? 1 : 0;
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

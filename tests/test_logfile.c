/*
 * test_logfile.c
 *
 * The log file of the logfile build, src/logfile.c, included under other
 * names for what this build has: the harness's printk() and vprintk(),
 * and the logfile_flush() that is a no-op at other levels.
 *
 * Written & released by Mike Shevchenko <mike.shevchenko@gmail.com>
 *
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

#include "harness.h"

#undef logfile_flush
#define printk lf_printk
#define vprintk lf_vprintk
#define logfile_flush lf_flush
#include "../src/logfile.c"
#undef printk
#undef vprintk
#undef logfile_flush

static FIL log_file;

static int flush_fn(void *unused)
{
    lf_flush(&log_file);
    return 0;
}

/* Empties the drive but for a file to mount it by, which @full follows
 * with one of every cluster left, mounts it and empties the ring. */
static void log_reset(bool_t full)
{
    char c;

    drive_reset();
    drive_add("A.TXT", "a", 1);
    if (full)
        drive_fill("FULL.BIN");
    drive_get("A.TXT", &c, 1);
    cons = prod;
}

TEST(logfile_flush_appends_ring)
{
    char buf[64];
    uint32_t n;

    log_reset(FALSE);
    lf_printk("one %u\n", 1);
    CHECK_EQ(F_call_cancellable(flush_fn, NULL), FR_OK);
    lf_printk("two\n");
    CHECK_EQ(F_call_cancellable(flush_fn, NULL), FR_OK);
    n = drive_get("FFLOG.TXT", buf, sizeof(buf) - 1);
    buf[n] = '\0';
    CHECK_STR(buf, "one 1\r\ntwo\r\n");
}

TEST(logfile_flush_failing_leaves_logging_on)
{
    unsigned int before;

    log_reset(TRUE);
    lf_printk("lost\n");
    /* The full drive cancels the flush and is passed on. */
    CHECK_EQ(F_call_cancellable(flush_fn, NULL), FR_DISK_FULL);
    CHECK(!quiesce);
    before = prod;
    lf_printk("x");
    CHECK_EQ(prod, before + 1);
}

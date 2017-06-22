/*
 * tests.c
 * 
 * Performance tests.
 * 
 * Written & released by Keir Fraser <keir.xen@gmail.com>
 * 
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

extern FIL file;

/*
 * Stats accumulation and pretty printing
 */

struct stats {
    unsigned int min_us, max_us;
    unsigned int tot_us;
    unsigned int nr_ops;
};

static void stats_init(struct stats *stats)
{
    memset(stats, 0, sizeof(*stats));
    stats->min_us = ~0u;
}

static void stats_update(struct stats *stats, unsigned int op_us)
{
    stats->nr_ops++;
    if (op_us > stats->max_us)
        stats->max_us = op_us;
    if (op_us < stats->min_us)
        stats->min_us = op_us;
    stats->tot_us += op_us;
}

static void stats_print(struct stats *stats)
{
    printk("Min: %uus Max: %uus Mean: %uus\n",
           stats->min_us, stats->max_us,
           stats->tot_us / stats->nr_ops);
}

/*
 * Monotonic microsecond-resolution time
 */

static struct {
    struct timer timer;
    volatile stk_time_t stk_stamp;
    volatile uint32_t sys_us_stamp;
} time;

static uint32_t _time_now(stk_time_t _stk_now)
{
    uint32_t stk_stamp, sys_us_stamp;

    do {
        stk_stamp = time.stk_stamp;
        sys_us_stamp = time.sys_us_stamp;
    } while (stk_stamp != time.stk_stamp);

    return sys_us_stamp + stk_diff(stk_stamp, _stk_now) / STK_MHZ;
}

static uint32_t time_now(void)
{
    return _time_now(stk_now());
}

static void time_fn(void *unused)
{
    stk_time_t stk_stamp = stk_now();
    uint32_t sys_us_stamp = _time_now(stk_stamp);
    time.sys_us_stamp = sys_us_stamp;
    time.stk_stamp = stk_stamp;
    timer_set(&time.timer,
              stk_add(time.timer.deadline, stk_ms(500)));
}

static void time_init(void)
{
    memset(&time, 0, sizeof(time));
    timer_init(&time.timer, time_fn, NULL);
    timer_set(&time.timer, stk_add(stk_now(), stk_ms(500)));
}

/*
 * PRNG
 */

static uint32_t random(uint32_t *rnd)
{
    uint32_t _rnd = *rnd;
    if (_rnd & 1)
        _rnd = (_rnd >> 1) ^ 0x80000062;
    else
        _rnd = (_rnd >> 1);
    return *rnd = _rnd;
}

/*
 * I/O Tests
 */

#define TEST_MB 1
#define TEST_SZ (TEST_MB*1024*1024)
static void speed_subtests(FIL *fp, struct stats *stats,
                           char *buf, unsigned int bufsz,
                           int do_write, unsigned int blksz)
{
    unsigned int i, j;
    uint32_t t[2], rnd = 0x12345678;

    F_lseek(fp, 0);
    stats_init(stats);
    t[0] = time_now();
    for (i = 0; i < TEST_SZ / blksz; i++) {
        if (do_write)
            F_write(fp, buf, blksz, NULL);
        else
            F_read(fp, buf, blksz, NULL);
        t[1] = time_now();
        stats_update(stats, t[1]-t[0]);
        t[0] = t[1];
    }
    printk("Sequential %u-byte %ss (%uMB total):: ", blksz,
           do_write ? "write" : "read", TEST_MB);
    stats_print(stats);
    
    stats_init(stats);
    t[0] = time_now();
    for (i = 0; i < TEST_SZ / blksz; i++) {
        F_lseek(fp, (random(&rnd)&(TEST_SZ-1))&~(blksz-1));
        if (do_write)
            F_write(fp, buf, blksz, NULL);
        else
            F_read(fp, buf, blksz, NULL);
        t[1] = time_now();
        stats_update(stats, t[1]-t[0]);
        t[0] = t[1];
    }
    printk("Random %u-byte %ss (%uMB total):: ", blksz,
           do_write ? "write" : "read", TEST_MB);
    stats_print(stats);
    
    if (blksz >= bufsz)
        return;

    stats_init(stats);
    t[0] = time_now();
    for (i = 0; i < TEST_SZ / bufsz; i++) {
        uint32_t off = (random(&rnd)&(TEST_SZ-1))&~(bufsz-1);
        for (j = 0; j < bufsz/blksz; j++) {
            F_lseek(fp, off + ((random(&rnd)&(bufsz-1))&~(blksz-1)));
            if (do_write)
                F_write(fp, buf, blksz, NULL);
            else
                F_read(fp, buf, blksz, NULL);
            t[1] = time_now();
            stats_update(stats, t[1]-t[0]);
            t[0] = t[1];
        }
    }
    printk("Clustered Random %u-byte %ss (%uMB total):: ", blksz,
           do_write ? "write" : "read", TEST_MB);
    stats_print(stats);
}

void speed_tests(void)
{
    static char buf[8192];
    struct stats stats;
    unsigned int i;
    uint32_t t[2];

    time_init();

    for (i = 0; i < sizeof(buf); i++)
        buf[i] = i;

    F_open(&file, "speed_test", FA_READ|FA_WRITE|FA_CREATE_ALWAYS);

    stats_init(&stats);
    t[0] = time_now();
    for (i = 0; i < TEST_SZ / sizeof(buf); i++) {
        F_write(&file, buf, sizeof(buf), NULL);
        t[1] = time_now();
        stats_update(&stats, t[1]-t[0]);
        t[0] = t[1];
    }
    printk("Sequential create (%uMB total, 8kB block size):: ", TEST_MB);
    stats_print(&stats);

    speed_subtests(&file, &stats, buf, sizeof(buf), 0, 512);
    speed_subtests(&file, &stats, buf, sizeof(buf), 0, sizeof(buf));
    speed_subtests(&file, &stats, buf, sizeof(buf), 1, 512);
    speed_subtests(&file, &stats, buf, sizeof(buf), 1, sizeof(buf));

    F_close(&file);
    F_unlink("speed_test");

    speed_tests_cancel();
}

void speed_tests_cancel(void)
{
    if (time.timer.cb_fn != NULL)
        timer_cancel(&time.timer);
    memset(&time, 0, sizeof(time));
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

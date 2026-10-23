/*
 * cpu.c
 *
 * The emulated CPU core as far as the firmware can tell: an interrupt
 * controller with priorities, a 1 ms tick that preempts the firmware the way
 * interrupts do, cancellable calls, time, and reset.
 *
 * The firmware runs in the main thread. Its interrupt handlers run in that
 * same thread, either from the tick signal or at the points where the
 * firmware unmasks interrupts, so they nest on the firmware's stack just as
 * exceptions do on the real MCU.
 *
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

#include <errno.h>
#include <fcntl.h>
#include <setjmp.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>
#include <sched.h>
#include <sys/stat.h>
#include <sys/time.h>
#ifdef __CYGWIN__
#include <dlfcn.h>
#endif

#include "host.h"

volatile unsigned int emu_in_buttons;
volatile int emu_in_rotary;
volatile unsigned int emu_out_speaker;
/* As connected to a computer that has just selected the drive; Shugart
 * steps go inward. */
volatile unsigned int emu_in_fdd = EMU_FDD_SEL
    | (FFEMU_APPLE2 ? 0 : EMU_FDD_DIR);
volatile unsigned int emu_in_step;
volatile int emu_mcu;
volatile int emu_oled_synth_8x16;

char log_ring[LOG_SIZE];
volatile unsigned int log_head;
const char *log_file_name, *log_file_tried;
int log_file_errno = ENOENT;
static int log_fd = -1;

static char **saved_argv;
static const char *restart_state;
static struct timespec t0;

enum { REQ_none, REQ_restart, REQ_quit };
static volatile int request;

/*
 * Interrupt controller.
 */

#define NR_IRQS 64
#define PRI_THREAD 0x100u /* lower than any interrupt priority */

static volatile uint32_t pend[NR_IRQS/32], enab[NR_IRQS/32];
static volatile uint8_t prio[NR_IRQS]; /* priority << 4, as in NVIC_IPR */
static volatile unsigned int primask;  /* IRQ_global_disable() in effect */
static volatile unsigned int basepri;  /* 0, or the masked priority << 4 */
static volatile unsigned int cur_prio = PRI_THREAD;
static volatile unsigned int irq_depth;

static void cancel_deferred(void);
static void stop_ticks(void);
static void restart(void) __attribute__((noreturn));
static void take_request(void) __attribute__((noreturn));

/* Runs every interrupt handler that is pending, enabled and not masked. */
static void irq_run(void)
{
    /* Not from a tick that came in the middle of an update of the models,
     * which a handler waiting on a peripheral would never see finish: every
     * update is followed by a run of the handlers anyway. */
    if (emu_hw_busy())
        return;

    for (;;) {
        unsigned int n, best = NR_IRQS, limit = cur_prio, saved;
        uint32_t bit;

        if (primask)
            return;
        if (basepri && (basepri < limit))
            limit = basepri;
        for (n = 0; n < NR_IRQS; n++) {
            if ((pend[n>>5] & enab[n>>5] & (1u << (n&31)))
                && (prio[n] < limit)) {
                best = n;
                limit = prio[n];
            }
        }
        if (best == NR_IRQS)
            break;

        /* A nested tick may have taken it in the meantime. */
        bit = 1u << (best&31);
        if (!(__sync_fetch_and_and(&pend[best>>5], ~bit) & bit))
            continue;

        saved = cur_prio;
        cur_prio = limit;
        irq_depth++;
        emu_irq_vector(best);
        irq_depth--;
        cur_prio = saved;

        /* Let the peripherals react to what the handler did. */
        emu_hw_sync();
    }

    cancel_deferred();
}

void emu_irq_poll(void)
{
    irq_run();
}

int emu_in_exception(void)
{
    return irq_depth != 0;
}

void emu_irq_global(int enable)
{
    primask = !enable;
    if (enable)
        irq_run();
}

unsigned int emu_irq_save(unsigned int newpri)
{
    unsigned int old = basepri;
    newpri <<= 4;
    if (!old || (old > newpri))
        basepri = newpri;
    return old;
}

void emu_irq_restore(unsigned int oldpri)
{
    basepri = oldpri;
    irq_run();
}

void emu_irqx_enable(unsigned int x)
{
    __sync_fetch_and_or(&enab[x>>5], 1u << (x&31));
    irq_run();
}

void emu_irqx_disable(unsigned int x)
{
    __sync_fetch_and_and(&enab[x>>5], ~(1u << (x&31)));
}

int emu_irqx_is_enabled(unsigned int x)
{
    return (enab[x>>5] >> (x&31)) & 1;
}

void emu_irqx_set_pending(unsigned int x)
{
    __sync_fetch_and_or(&pend[x>>5], 1u << (x&31));
    /* The peripheral models only mark interrupts. Handlers run once the
     * models are done, since a handler may wait on a peripheral, and only on
     * the firmware's thread. */
    if (!emu_on_hw_thread() && !emu_hw_busy())
        irq_run();
}

void emu_irqx_clear_pending(unsigned int x)
{
    __sync_fetch_and_and(&pend[x>>5], ~(1u << (x&31)));
}

int emu_irqx_is_pending(unsigned int x)
{
    return (pend[x>>5] >> (x&31)) & 1;
}

void emu_irqx_set_prio(unsigned int x, unsigned int pri)
{
    prio[x] = pri << 4;
}

unsigned int emu_irqx_get_prio(unsigned int x)
{
    return prio[x] >> 4;
}

/*
 * Cancellable calls: the firmware abandons a call tree, from within it or
 * from an interrupt handler, when the drive is pulled or a file operation
 * fails. Here that is a non-local jump back to the caller.
 */

struct cancellation {
    uint32_t *sp; /* as in the firmware: non-NULL while the call is active */
};

static struct cancel_slot {
    sigjmp_buf jb;
} cancel_slot[4];
static volatile unsigned int nr_cancel_slots;
static struct cancel_slot * volatile cancel_request;

int call_cancellable_fn(struct cancellation *c, int (*fn)(void *), void *arg)
{
    struct cancel_slot *s = &cancel_slot[nr_cancel_slots];
    volatile unsigned int depth = nr_cancel_slots;
    int rc;

    if (depth >= ARRAY_SIZE(cancel_slot))
        cpu_quit(3, "ffemu: cancellable calls nested too deep");

    if (sigsetjmp(s->jb, 1) == 0) {
        nr_cancel_slots = depth + 1;
        c->sp = (uint32_t *)s;
        rc = (*fn)(arg);
    } else {
        rc = -1;
    }

    c->sp = NULL;
    nr_cancel_slots = depth;
    return rc;
}

void cancel_call(struct cancellation *c)
{
    struct cancel_slot *s = (struct cancel_slot *)c->sp;

    if (s == NULL)
        return;
    c->sp = NULL;

    /* From a handler, the firmware lets the handler finish first. */
    cancel_request = s;
    if (!irq_depth)
        cancel_deferred();
}

/* Performs a requested cancellation once no handler is running and the
 * peripheral models are not in the middle of an update. */
static void cancel_deferred(void)
{
    struct cancel_slot *s = cancel_request;

    if ((s == NULL) || irq_depth || emu_hw_busy())
        return;
    cancel_request = NULL;
    siglongjmp(s->jb, 1);
}

/*
 * Time.
 */

/* The host's time since the start. */
static uint64_t host_ns(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)(t.tv_sec - t0.tv_sec) * 1000000000u
        + t.tv_nsec - t0.tv_nsec;
}

/* The virtual clock, FFEMU_CLOCK=virtual, of scripts: the emulated time moves
 * on only as the firmware reads SysTick and waits, so that it does not depend
 * on how fast the host runs the firmware, and only as far as the script lets
 * it run, where the firmware waits for the script: a script gives the same
 * output on every run. */
static bool virtual_clock;
static volatile uint64_t virt_now, virt_limit;
/* Set while the firmware waits for the script to let it run further. */
static volatile bool virt_waiting;
/* How far a read of SysTick moves the clock, as the loop it is in would
 * take a while; how far a pause of a busy loop does; and the longest step of
 * a wait, after which the peripherals' models run, unless one of their
 * deadlines ends it sooner. */
#define VIRT_READ_NS 1000u
#define VIRT_RELAX_NS 10000u
#define VIRT_STEP_NS 1000000u

/* A firmware that neither moves the virtual clock nor calls the host half
 * spins on a register or a variable, which its basic blocks show: after
 * VIRT_SYNC_BLOCKS of them the models run, as a wait on a register, such as
 * for the end of an I2C STOP, needs, and what they pend; after
 * VIRT_STILL_BLOCKS, more than a stretch of the firmware's own work, the
 * clock moves on to the models' next deadline, VIRT_SPIN_NS at most, and what
 * comes due runs; after VIRT_SPINNING_BLOCKS where the last such move left
 * it, as in a loop that waits for a button. */
#define VIRT_SYNC_BLOCKS 1000u
#define VIRT_STILL_BLOCKS 100000u
#define VIRT_SPINNING_BLOCKS 5000u
#define VIRT_SPIN_NS 10000000u
/* The blocks since the last move or call, and the counts at which the
 * models run next and the clock moves. */
static unsigned long virt_blocks, virt_check_at = ~0ul, virt_move_at;
static pthread_t fw_tid;

static void virt_recount(unsigned long move_at)
{
    virt_blocks = 0;
    virt_move_at = move_at;
    virt_check_at = VIRT_SYNC_BLOCKS;
}

uint64_t emu_time_ns(void)
{
    return virtual_clock ? virt_now : host_ns();
}

/* Moves the virtual clock on to @t, after waiting while the script lets the
 * firmware run no further, or carries out a request to quit or restart then:
 * on Cygwin the tick that otherwise does so was seen held back for seconds. */
static void virt_move(uint64_t t)
{
    uint64_t limit;

    /* One read of the limit, which the script may raise meanwhile. */
    while (t > (limit = virt_limit)) {
        if (virt_now < limit)
            virt_now = limit;
        if (request != REQ_none)
            take_request();
        virt_waiting = true;
        sched_yield();
    }
    virt_waiting = false;
    if (t > virt_now)
        virt_now = t;
    virt_recount(VIRT_STILL_BLOCKS);
}

/* The clock's next stop, @t or the models' next deadline, as of their last
 * sync, if that comes first. */
static uint64_t virt_next(uint64_t t)
{
    uint64_t due = emu_hw_due_ns();

    return ((due > virt_now) && (due < t)) ? due : t;
}

static void virt_check(void)
{
    /* Not in another thread, as a script's command may run firmware code
     * while the firmware waits for it, nor in the middle of a sync. */
    if (!pthread_equal(pthread_self(), fw_tid) || emu_hw_busy())
        return;
    if (virt_blocks < virt_move_at) {
        virt_check_at = virt_blocks + VIRT_SYNC_BLOCKS;
        if (virt_check_at > virt_move_at)
            virt_check_at = virt_move_at;
        emu_hw_sync();
        irq_run();
        return;
    }
    emu_hw_sync();
    virt_move(virt_next(virt_now + VIRT_SPIN_NS));
    emu_hw_sync();
    irq_run();
    virt_recount(VIRT_SPINNING_BLOCKS);
}

/* Called by the firmware's code at each of its basic blocks. */
void __sanitizer_cov_trace_pc(void)
{
    if (++virt_blocks >= virt_check_at)
        virt_check();
}

void emu_stk_read(void)
{
    if (virtual_clock)
        virt_move(virt_now + VIRT_READ_NS);
}

bool cpu_clock_virtual(void)
{
    return virtual_clock;
}

void cpu_run_until(uint64_t t)
{
    uint64_t seen = virt_now, moved = host_ns();

    if (t > virt_limit)
        virt_limit = t;
    /* Until the firmware waits for more, so that what the script does next
     * finds it at the same point on every run. */
    while ((virt_now < t) || !virt_waiting) {
        sched_yield();
        if (virt_now != seen) {
            seen = virt_now;
            moved = host_ns();
        } else if (host_ns() - moved > 10000000000u) {
            host_log("The firmware has not moved the virtual clock on for "
                     "10 s: going on with the script");
            return;
        }
    }
}

/* Set where the host sleeps in steps of milliseconds, as a virtual machine on
 * a Windows host may (15.6 ms): sleeping there makes the firmware's overdue
 * timers run in bursts, which lose the encoder's steps, so ffemu spins while
 * the user is at work, see user_active(). */
static bool coarse_sleep;

/* How long ffemu spins on such a host after the user's last input. */
#define ACTIVE_NS 3000000000u

/* Whether the user has given input lately: pressed a button or a key of the
 * floppy interface, turned the encoder, which steps on while detents are
 * left, or inserted or removed the USB drive. */
static bool user_active(void)
{
    static unsigned int buttons, fdd, step;
    static int usb;
    static uint64_t until;
    unsigned int b = emu_in_buttons, f = emu_in_fdd, s = emu_in_step;
    int u = emu_usb_inserted();
    uint64_t now = host_ns();

    if ((b != buttons) || (f != fdd) || (s != step) || (u != usb)
        || (emu_in_rotary != 0)) {
        buttons = b;
        fdd = f;
        step = s;
        usb = u;
        until = now + ACTIVE_NS;
    }
    return now < until;
}

static void sleep_us(unsigned int us)
{
    struct timespec t = { us / 1000000, (us % 1000000) * 1000 };
    /* A tick cuts the sleep short, which is what the callers want. */
    nanosleep(&t, NULL);
}

static void go_coarse(uint64_t step_ns)
{
    coarse_sleep = true;
    host_log("This host sleeps in steps of %u ms: keeping time by "
             "spinning after each input, which keeps a CPU core busy then",
             (unsigned int)((step_ns + 500000) / 1000000));
}

/* Notes a sleep that came @late ns after its end. A host whose sleeps are
 * coarse may pass the measurement at the start by chance, as a virtual
 * machine does now and then, so sleeps that overshoot many times in a row
 * mean coarse too. */
static void note_sleep(uint64_t late)
{
    static unsigned int overshoots;

    overshoots = (late > 2000000) ? overshoots + 1 : 0;
    if (overshoots == 8)
        go_coarse(late);
}

/* Gives up the CPU for a moment, as the host allows. */
static void pause_briefly(void)
{
    uint64_t t;

    if (coarse_sleep) {
        sched_yield();
        return;
    }
    t = host_ns();
    sleep_us(100);
    note_sleep(host_ns() - t);
}

/* Sleeps until @end, in the host's time since the start, or until a tick cuts
 * the sleep short: how late it woke, 0 if early. */
static uint64_t sleep_until(uint64_t end)
{
    uint64_t ns = (uint64_t)t0.tv_nsec + end, now;
    struct timespec t = { t0.tv_sec + ns / 1000000000u, ns % 1000000000u };

    clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &t, NULL);
    now = host_ns();
    return (now > end) ? now - end : 0;
}

/* The firmware's calls into the host half from its thread, outside its
 * interrupt handlers: a loop that spins on a variable alone makes none. */
static volatile unsigned long progress;

void emu_fw_progress(void)
{
    if (irq_depth == 0)
        progress++;
    if (virtual_clock)
        virt_recount(VIRT_STILL_BLOCKS);
}

/* A wait shorter than this spins, since a sleep would overshoot it many times
 * over; a longer one sleeps until its end. */
#define SLEEP_MIN_NS 300000u
/* A wait that sleeps, called within this of where the previous one was to
 * end, as in a loop of waits, counts from there: the loop keeps its pace,
 * though each sleep overshoots by up to a tick of the host's timer. */
#define CHAIN_NS 2000000u
static uint64_t idle_end;

/* Set while the firmware's thread sleeps on a host whose sleeps are coarse. */
static volatile bool coarse_idle;

int emu_timers_on_time(void)
{
    return !coarse_sleep || (!coarse_idle && user_active());
}

/* A wait of the firmware's on the virtual clock: in steps, after each of
 * which the peripherals' models run. */
static void virt_idle(uint64_t ns)
{
    uint64_t end = virt_now + ns, step;

    for (;;) {
        emu_fw_progress();
        emu_hw_sync();
        irq_run();
        if (virt_now >= end)
            break;
        step = virt_now + VIRT_STEP_NS;
        virt_move(virt_next((step < end) ? step : end));
    }
}

void emu_idle_ns(uint64_t ns)
{
    uint64_t now = emu_time_ns(), end = now + ns, late;
    bool sleeps;

    if (virtual_clock) {
        virt_idle(ns);
        return;
    }

    sleeps = (!coarse_sleep || !user_active())
        && (ns >= SLEEP_MIN_NS);
    if (sleeps) {
        if (now - idle_end < CHAIN_NS)
            end = idle_end + ns;
        idle_end = end;
    }
    coarse_idle = coarse_sleep && sleeps;

    for (;;) {
        emu_fw_progress();
        emu_hw_sync();
        irq_run();
        now = emu_time_ns();
        if (now >= end)
            break;
        if (sleeps) {
            late = sleep_until(end);
            if (!coarse_sleep)
                note_sleep(late);
        } else if (coarse_sleep) {
            pause_briefly();
        }
    }
    coarse_idle = false;
}

void emu_relax(void)
{
    emu_fw_progress();
    emu_hw_sync();
    irq_run();
    if (virtual_clock)
        virt_move(virt_now + VIRT_RELAX_NS);
    else
        pause_briefly();
}

/*
 * The peripherals also run on a thread of their own, as the hardware runs
 * beside the CPU. A handler that waits on a peripheral, as the display driver
 * waits for the end of an I2C STOP, is otherwise released only by a nested
 * tick, which Cygwin defers until the spinning thread calls into it.
 */

static pthread_t hw_tid;
static volatile bool hw_started;

int emu_on_hw_thread(void)
{
    return hw_started && pthread_equal(pthread_self(), hw_tid);
}

static void *hw_thread(void *unused)
{
    /* It must know itself before it runs a model, which pends interrupts. */
    while (!hw_started)
        sleep_us(100);
    for (;;) {
        emu_hw_sync_bus();
        sleep_us(100);
    }
    return NULL;
}

/* Finds out whether the host's sleeps are coarse: the shortest of a few. */
static void measure_sleep(void)
{
    uint64_t best = ~(uint64_t)0, t;
    unsigned int i;

    for (i = 0; i < 5; i++) {
        t = host_ns();
        sleep_us(100);
        t = host_ns() - t;
        if (t < best)
            best = t;
    }

    if (best > 2000000)
        go_coarse(best);
}

/* The terminal's modes at the first start, carried over power cycles. */
static struct termios tty_mode;
static bool tty_mode_saved;

static void tty_restore(void)
{
    if (tty_mode_saved)
        tcsetattr(STDIN_FILENO, TCSANOW, &tty_mode);
}

/* A signal that ends the program: the terminal gets its screen and modes
 * back, as far as a signal handler can do that, and then the signal takes
 * its course. */
static void on_fatal(int sig)
{
    static const char crashed[] = "\nffemu: crashed with signal ";
    char nr[3] = { '0' + (sig / 10) % 10, '0' + sig % 10, '\n' };

    tui_stop_fatal();
    tty_restore();

    /* A fault re-raised from within the tick's handler can end the program
     * with status 0 on Cygwin: say so, and leave with a signal's status. */
    if ((sig == SIGSEGV) || (sig == SIGBUS) || (sig == SIGILL)
        || (sig == SIGFPE) || (sig == SIGABRT)) {
        (void)!write(STDERR_FILENO, crashed, sizeof(crashed) - 1);
        (void)!write(STDERR_FILENO, nr, sizeof(nr));
        _exit(128 + sig);
    }

    signal(sig, SIG_DFL);
    raise(sig);
}

/* A firmware that waits for a button by spinning on a variable that only its
 * interrupts change makes no progress: after SPIN_NS of that, the tick sleeps
 * for it, a while at a time, waking at once when an input changes. */
#define SPIN_NS 20000000u
#define THROTTLE_NS 50000000u
#define THROTTLE_STEP_US 2000
static volatile bool throttling;

static void throttle(void)
{
    static unsigned long seen;
    static uint64_t still_since;
    unsigned int buttons = emu_in_buttons, fdd = emu_in_fdd;
    int rotary = emu_in_rotary, usb = emu_usb_inserted();
    uint64_t now = host_ns(), end = now + THROTTLE_NS;

    /* In a handler, the firmware waits on a peripheral, which the models
     * release soon enough. */
    if ((progress != seen) || (irq_depth != 0)) {
        seen = progress;
        still_since = now;
        return;
    }
    if (now - still_since < SPIN_NS)
        return;

    throttling = true;
    do {
        sleep_us(THROTTLE_STEP_US);
        if ((request != REQ_none) || (emu_in_buttons != buttons)
            || (emu_in_rotary != rotary) || (emu_in_fdd != fdd)
            || (emu_usb_inserted() != usb)) {
            /* A full SPIN_NS at the tick's own pace, for the firmware's
             * debouncing and the encoder's steps. */
            still_since = host_ns();
            break;
        }
    } while (host_ns() < end);
    throttling = false;
}

/* Carries out a request to quit or restart, in the firmware's thread. */
static void take_request(void)
{
    stop_ticks();
    tui_stop();
    if (request == REQ_restart)
        restart();
    _exit(0);
}

static void on_tick(int sig)
{
    int saved_errno = errno;
    if (request != REQ_none)
        take_request();
    /* On the virtual clock the firmware's own reads, waits and blocks run
     * the models; a tick within the throttle's sleep has nothing to run. */
    if (virtual_clock || throttling) {
        errno = saved_errno;
        return;
    }
    throttle();
    emu_hw_sync();
    irq_run();
    errno = saved_errno;
}

/*
 * Firmware console and configuration flash.
 */

void emu_log(const char *s)
{
    const char *p = s;
    char c;

    while ((c = *p++) != '\0')
        log_ring[__sync_fetch_and_add(&log_head, 1) % LOG_SIZE] = c;

    /* write() rather than stdio: this may run in the tick's signal handler. */
    if (log_fd >= 0) {
        ssize_t n = write(log_fd, s, p - 1 - s);
        (void)n;
    }
}

/* The tail of console.log at @path into the ring, which shows it again
 * after a reset of the firmware's own as it did before. */
static void load_log_tail(const char *path)
{
    static char buf[LOG_SIZE];
    struct stat st;
    ssize_t n = 0, i;
    off_t size;
    int fd = open(path, O_RDONLY | O_CLOEXEC);

    if (fd < 0)
        return;
    if (fstat(fd, &st) == 0) {
        size = (st.st_size > LOG_SIZE) ? LOG_SIZE : st.st_size;
        if (lseek(fd, st.st_size - size, SEEK_SET) >= 0)
            n = read(fd, buf, size);
    }
    close(fd);
    for (i = 0; i < n; i++)
        log_ring[log_head++ % LOG_SIZE] = buf[i];
    if ((n > 0) && (buf[n - 1] != '\n'))
        emu_log("\n");
}

/* The console also goes to console.log in ${XDG_STATE_HOME:-~/.local/state}
 * /ffemu-shugart/ (or /ffemu-apple2/), begun anew on every start and power
 * cycle, the previous one becoming console.bak, replacing the one before it;
 * after a reset of the firmware's own, both go on after LOG_FW_RESET. */
static void open_log(void)
{
    const char *state = getenv("XDG_STATE_HOME"), *home = user_home();
    static char path[600];
    char dir[512], bak[600];
    bool fw_reset = getenv("FFEMU_FW_RESET") != NULL;

    unsetenv("FFEMU_FW_RESET");
    if ((state != NULL) && (state[0] != '\0'))
        snprintf(dir, sizeof(dir), "%s/" FFEMU_NAME, state);
    else if ((home != NULL) && (home[0] != '\0'))
        snprintf(dir, sizeof(dir), "%s/.local/state/" FFEMU_NAME, home);
    else
        return;
    make_dirs(dir);

    snprintf(path, sizeof(path), "%s/console.log", dir);
    snprintf(bak, sizeof(bak), "%s/console.bak", dir);
    if (!fw_reset) {
        unlink(bak);
        rename(path, bak);
    }

    log_file_tried = path;
    log_fd = open(path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC
                  | (fw_reset ? 0 : O_TRUNC), 0644);
    if (log_fd >= 0) {
        own_file(path);
        log_file_name = path;
    } else {
        log_file_errno = errno;
    }
    if (fw_reset) {
        load_log_tail(path);
        emu_log(LOG_FW_RESET "\n");
    }
}

static uint8_t flash_data[256];
static unsigned int flash_len;

unsigned int emu_flash_load(void *buf, unsigned int size)
{
    if (size > flash_len)
        size = flash_len;
    memcpy(buf, flash_data, size);
    return size;
}

void flash_set(const void *buf, unsigned int size)
{
    if (size > sizeof(flash_data))
        size = sizeof(flash_data);
    memcpy(flash_data, buf, size);
    flash_len = size;
}

void emu_flash_save(const void *buf, unsigned int size)
{
    flash_set(buf, size);
    /* Kept like the real flash memory: across runs. */
    rc_save();
}

void emu_flash_dumped(int settings)
{
    host_log("FLASH.DMP virtually written by fw: %s", settings
             ? "the flash settings in slot 0, the rest blank"
             : "no flash settings, all slots blank");
}

/* @n bytes at @p as hex, into @buf of 2*@n+1 bytes. */
static void to_hex(char *buf, const void *p, size_t n)
{
    size_t i;

    buf[0] = '\0';
    for (i = 0; i < n; i++)
        snprintf(&buf[2*i], 3, "%02X", ((const uint8_t *)p)[i]);
}

size_t hex_to_bytes(void *p, size_t n, const char *hex)
{
    unsigned int byte;
    size_t i;

    for (i = 0; (i < n) && (sscanf(&hex[2*i], "%2x", &byte) == 1); i++)
        ((uint8_t *)p)[i] = byte;
    return i;
}

void flash_to_hex(char *buf, size_t size)
{
    to_hex(buf, flash_data, (flash_len < size / 2) ? flash_len
           : (size - 1) / 2);
}

void flash_summary(char *buf, size_t size)
{
    if (!emu_flash_describe(buf, size, ' ', 0))
        snprintf(buf, size, "none (factory defaults)");
    else if (buf[0] == '\0')
        snprintf(buf, size, "all options at their defaults");
}

void flash_from_hex(const char *hex)
{
    flash_len = hex_to_bytes(flash_data, sizeof(flash_data), hex);
}

/*
 * Process control.
 */

static void block_tick(int how)
{
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGALRM);
    pthread_sigmask(how, &set, NULL);
}

static void stop_ticks(void)
{
    struct itimerval it;
    block_tick(SIG_BLOCK);
    memset(&it, 0, sizeof(it));
    setitimer(ITIMER_REAL, &it, NULL);
}

void cpu_quit(int code, const char *msg)
{
    stop_ticks();
    tui_stop();
    if (msg != NULL)
        fprintf(stderr, "%s\n", msg);
    fflush(NULL);
    _exit(code);
}

void emu_illegal(const char *file, int line)
{
    char msg[256];
    unsigned int i, start, head = log_head;

    stop_ticks();
    tui_stop();

    /* The tail of the firmware console usually says why. */
    start = (head > 600) ? head - 600 : 0;
    for (i = start; i < head; i++)
        fputc(log_ring[i % LOG_SIZE], stderr);
    snprintf(msg, sizeof(msg),
             "\nffemu: firmware assertion failed at %s:%d", file, line);
    cpu_quit(3, msg);
}

/* The terminal's modes at the first start, for the new process. */
static void pass_tty_mode(void)
{
    char hex[2*sizeof(tty_mode)+1];

    if (tty_mode_saved) {
        to_hex(hex, &tty_mode, sizeof(tty_mode));
        setenv("FFEMU_TTY", hex, 1);
    }
}

/* What survives a power cycle travels in the environment of the new process:
 * the configuration flash, and the state of the user interface, including
 * the signals of the host computer on the floppy interface. */
static void prepare_restart(void)
{
    char hex[2*sizeof(flash_data)+1], state[48];
    struct usb_info usb;
    unsigned int fdd, phase_pos;
    uint32_t layout;
    int fd;

    usb_get_store(&fd, &layout);
    snprintf(state, sizeof(state), "%d,%x", fd, layout);
    setenv("FFEMU_USB", state, 1);

    usb_get_info(&usb);
    ui_fdd_save(&fdd, &phase_pos);
    snprintf(state, sizeof(state), "%d,%d,%u,%x,%u,%x", config.style,
             usb.inserted, script_sleep_left(), fdd, phase_pos,
             ui_latch_save());
    setenv("FFEMU_STATE", state, 1);

    flash_to_hex(hex, sizeof(hex));
    setenv("FFEMU_FLASH", hex, 1);

    pass_tty_mode();
    fflush(NULL);
}

static void restart(void)
{
#ifdef __CYGWIN__
    /* Cygwin names the program without its .exe, which a directory of that
     * name beside it, such as its settings folder, would take instead. */
    char self[4096];
    ssize_t n = readlink("/proc/self/exe", self, sizeof(self) - 5);
    if (n > 0) {
        strcpy(self + n, ".exe");
        execv(self, saved_argv);
    }
#endif
    execv("/proc/self/exe", saved_argv);
    execvp(saved_argv[0], saved_argv);
    _exit(3);
}

void cpu_restart(void)
{
    stop_ticks();
    tui_stop();
    prepare_restart();
    restart();
}

/* The requesting thread does everything that a signal handler must not, and
 * leaves to the tick handler only what must happen in the firmware thread. */
void cpu_request_restart(void)
{
    prepare_restart();
    request = REQ_restart;
}

void cpu_request_quit(void)
{
    fflush(NULL);
    request = REQ_quit;
}

/* Only the terminal's modes go along; the store of the firmware's writes to
 * the USB drive closes on the exec, and the new process begins its own. */
void cpu_request_relaunch(void)
{
    uint32_t layout;
    int fd;

    usb_get_store(&fd, &layout);
    if (fd >= 0)
        fcntl(fd, F_SETFD, FD_CLOEXEC);
    pass_tty_mode();
    fflush(NULL);
    request = REQ_restart;
}

void emu_reset(void)
{
    /* The firmware's own reset, which the console goes on through. */
    setenv("FFEMU_FW_RESET", "1", 1);
    cpu_restart();
}

const char *cpu_restart_state(void)
{
    return restart_state;
}

void cpu_init(char **argv)
{
    const char *hex = getenv("FFEMU_FLASH"), *tty = getenv("FFEMU_TTY");
    const char *clock = getenv("FFEMU_CLOCK");

    saved_argv = argv;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    open_log();

    /* The virtual clock is for scripts: the TUI runs in the host's time. */
    if ((clock != NULL) && !strcmp(clock, "virtual")) {
        if (isatty(STDIN_FILENO) && isatty(STDOUT_FILENO))
            host_log("FFEMU_CLOCK=virtual is for scripts: the clock is the "
                     "host's");
        else
            virtual_clock = true;
    }

    if (getenv("FFEMU_STATE") != NULL) {
        restart_state = strdup(getenv("FFEMU_STATE"));
        unsetenv("FFEMU_STATE");
    }

    if (hex != NULL) {
        flash_from_hex(hex);
        unsetenv("FFEMU_FLASH");
    }

    /* After a power cycle the terminal gets the modes of the first start
     * back before curses takes them as its own, whatever the program before
     * left. */
    if (tty != NULL) {
        tty_mode_saved = (hex_to_bytes(&tty_mode, sizeof(tty_mode), tty)
                          == sizeof(tty_mode));
        tty_restore();
        unsetenv("FFEMU_TTY");
    } else {
        tty_mode_saved = (tcgetattr(STDIN_FILENO, &tty_mode) == 0);
    }

#ifdef __CYGWIN__
    {
        /* Windows timers tick at 64 Hz unless a process asks for better. */
        void *winmm = dlopen("winmm.dll", RTLD_LAZY);
        unsigned int (*begin_period)(unsigned int) = NULL;
        if (winmm != NULL)
            begin_period = (unsigned int (*)(unsigned int))
                dlsym(winmm, "timeBeginPeriod");
        if (begin_period != NULL)
            (*begin_period)(1);
    }
#endif

    /* The virtual clock keeps the firmware's timers on time by itself. */
    if (!virtual_clock)
        measure_sleep();

    signal(SIGHUP, on_fatal);
    signal(SIGINT, on_fatal);
    signal(SIGQUIT, on_fatal);
    signal(SIGTERM, on_fatal);
    signal(SIGILL, on_fatal);
    signal(SIGABRT, on_fatal);
    signal(SIGFPE, on_fatal);
    signal(SIGBUS, on_fatal);
    signal(SIGSEGV, on_fatal);

    /* Threads created from now on must not take the tick. */
    block_tick(SIG_BLOCK);
}

void cpu_start(void)
{
    struct sigaction sa;
    struct itimerval it;

    /* SA_NODEFER: a handler that waits on a peripheral is released by the
     * next tick, which therefore must be able to nest. */
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_tick;
    sa.sa_flags = SA_NODEFER | SA_RESTART;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGALRM, &sa, NULL);

    it.it_interval.tv_sec = it.it_value.tv_sec = 0;
    it.it_interval.tv_usec = it.it_value.tv_usec = 1000;
    setitimer(ITIMER_REAL, &it, NULL);

    fw_tid = pthread_self();
    if (virtual_clock)
        virt_recount(VIRT_STILL_BLOCKS);
    /* Created while the tick is still blocked, which it inherits; on the
     * virtual clock the models run in the firmware's thread alone. */
    if (!virtual_clock
        && (pthread_create(&hw_tid, NULL, hw_thread, NULL) == 0))
        hw_started = true;

    block_tick(SIG_UNBLOCK);
}

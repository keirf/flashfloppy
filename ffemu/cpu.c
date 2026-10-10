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

char log_ring[LOG_SIZE];
volatile unsigned int log_head;
const char *log_file_name;
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

uint64_t emu_time_ns(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)(t.tv_sec - t0.tv_sec) * 1000000000u
        + t.tv_nsec - t0.tv_nsec;
}

/* Set where the host sleeps in steps of milliseconds, as a virtual machine on
 * a Windows host may (15.6 ms): sleeping there makes the firmware's overdue
 * timers run in bursts, which lose the encoder's steps, so ffemu spins. */
static bool coarse_sleep;

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
             "spinning, which keeps a CPU core busy",
             (unsigned int)((step_ns + 500000) / 1000000));
}

/* Gives up the CPU for a moment, as the host allows. A host whose sleeps are
 * coarse may pass the measurement at the start by chance, as a virtual
 * machine does now and then, so sleeps that overshoot many times in a row
 * mean coarse too. */
static void pause_briefly(void)
{
    static unsigned int overshoots;
    uint64_t t;

    if (coarse_sleep) {
        sched_yield();
        return;
    }
    t = emu_time_ns();
    sleep_us(100);
    t = emu_time_ns() - t;
    overshoots = (t > 2000000) ? overshoots + 1 : 0;
    if (overshoots == 8)
        go_coarse(t);
}

void emu_idle_ns(uint64_t ns)
{
    uint64_t now = emu_time_ns(), end = now + ns;

    for (;;) {
        emu_hw_sync();
        irq_run();
        now = emu_time_ns();
        if (now >= end)
            break;
        /* Spin for the last bit, where even a short sleep would overshoot. */
        if (coarse_sleep || ((end - now) > 300000))
            pause_briefly();
    }
}

void emu_relax(void)
{
    emu_hw_sync();
    irq_run();
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
        t = emu_time_ns();
        sleep_us(100);
        t = emu_time_ns() - t;
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

static void on_tick(int sig)
{
    int saved_errno = errno;
    if (request != REQ_none) {
        stop_ticks();
        tui_stop();
        if (request == REQ_restart)
            restart();
        _exit(0);
    }
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

/* The console also goes to console.log in ${XDG_STATE_HOME:-~/.local/state}
 * /ffemu/ (or /ffemu-apple2/), begun anew on every start and power cycle;
 * the previous one becomes console.bak, replacing the one before it. */
static void open_log(void)
{
    const char *state = getenv("XDG_STATE_HOME"), *home = user_home();
    static char path[600];
    char dir[512], bak[600];

    if ((state != NULL) && (state[0] != '\0'))
        snprintf(dir, sizeof(dir), "%s/" FFEMU_NAME, state);
    else if ((home != NULL) && (home[0] != '\0'))
        snprintf(dir, sizeof(dir), "%s/.local/state/" FFEMU_NAME, home);
    else
        return;
    make_dirs(dir);

    snprintf(path, sizeof(path), "%s/console.log", dir);
    snprintf(bak, sizeof(bak), "%s/console.bak", dir);
    unlink(bak);
    rename(path, bak);

    log_fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_APPEND | O_CLOEXEC,
                  0644);
    if (log_fd >= 0) {
        own_file(path);
        log_file_name = path;
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
    snprintf(state, sizeof(state), "%d,%d,%u,%x,%u", config.style,
             usb.inserted, script_sleep_left(), fdd, phase_pos);
    setenv("FFEMU_STATE", state, 1);

    flash_to_hex(hex, sizeof(hex));
    setenv("FFEMU_FLASH", hex, 1);

    if (tty_mode_saved) {
        to_hex(hex, &tty_mode, sizeof(tty_mode));
        setenv("FFEMU_TTY", hex, 1);
    }

    fflush(NULL);
}

static void restart(void)
{
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

void emu_reset(void)
{
    cpu_restart();
}

const char *cpu_restart_state(void)
{
    return restart_state;
}

void cpu_init(char **argv)
{
    const char *hex = getenv("FFEMU_FLASH"), *tty = getenv("FFEMU_TTY");

    saved_argv = argv;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    open_log();

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

    /* Created while the tick is still blocked, which it inherits. */
    if (pthread_create(&hw_tid, NULL, hw_thread, NULL) == 0)
        hw_started = true;

    block_tick(SIG_UNBLOCK);
}

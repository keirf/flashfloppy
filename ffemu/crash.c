/*
 * crash.c
 *
 * The report of a crash, written by write() alone, as a signal handler may:
 * the signal, the thread, the registers that tell a trap's story, and the
 * stack, each frame as its address, which addr2line turns into a function and
 * a line, and its module. On Linux the stack comes from glibc's backtrace();
 * on Cygwin, which has none, it is unwound from the fault's own context by
 * the unwind tables that Windows keeps for x86-64 code. On Windows an
 * exception that reaches no handler of Cygwin's is reported the same way
 * before it ends the program.
 *
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

#include <signal.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <ucontext.h>
#include <unistd.h>
#ifdef __CYGWIN__
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <execinfo.h>
#endif

#include "host.h"

#define MAX_FRAMES 32
#define ALT_STACK_SIZE (64 * 1024)

/* Where the report goes: standard error, and the console log if open. */
static int report_fd[2] = { STDERR_FILENO, -1 };
static void (*restore_terminal)(void);
/* The name of the calling thread, if it gave one. */
static __thread const char *thread_name;
/* A fault while reporting one ends the program plainly. */
static volatile sig_atomic_t reporting;

static void put(const char *s)
{
    unsigned int i;

    for (i = 0; i < ARRAY_SIZE(report_fd); i++)
        if (report_fd[i] >= 0)
            (void)!write(report_fd[i], s, strlen(s));
}

static void put_hex(uintptr_t v)
{
    char buf[2 + 2 * sizeof(v) + 1], *p = &buf[sizeof(buf) - 1];

    *p = '\0';
    do {
        *--p = "0123456789abcdef"[v & 15];
        v >>= 4;
    } while (v != 0);
    *--p = 'x';
    *--p = '0';
    put(p);
}

static void put_dec(unsigned int v)
{
    char buf[12], *p = &buf[sizeof(buf) - 1];

    *p = '\0';
    do {
        *--p = '0' + v % 10;
        v /= 10;
    } while (v != 0);
    put(p);
}

static const char *signal_name(int sig)
{
    switch (sig) {
    case SIGSEGV: return "SIGSEGV";
    case SIGBUS: return "SIGBUS";
    case SIGILL: return "SIGILL";
    case SIGFPE: return "SIGFPE";
    case SIGABRT: return "SIGABRT";
    default: return "";
    }
}

/* The registers that tell where a fault stopped: the instruction, the stack
 * and the flags. */
static void put_registers(uintptr_t ip, uintptr_t sp, uintptr_t flags)
{
    put("  rip ");
    put_hex(ip);
    put(" rsp ");
    put_hex(sp);
    put(" eflags ");
    put_hex(flags);
    put("\n");
}

#ifdef __CYGWIN__

/* Frame @pc as its address, which ffemu's image, not relocated, shares with
 * addr2line, and its module and offset if it lies in one, which it returns.
 * Naming the module takes the loader's lock: a fault in the loader is
 * reported without names. */
static bool put_frame(DWORD64 pc)
{
    bool named = false;
    HMODULE m;
    char path[MAX_PATH], *name;

    put("  ");
    put_hex(pc);
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
                           | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCSTR)pc, &m)
        && GetModuleFileNameA(m, path, sizeof(path))) {
        name = strrchr(path, '\\');
        put(" ");
        put((name != NULL) ? name + 1 : path);
        put("+");
        put_hex(pc - (DWORD64)m);
        named = true;
    }
    put("\n");
    return named;
}

/* Whether the 8 bytes at @p can be read, as the unwinding reads the stack. */
static bool readable(DWORD64 p)
{
    MEMORY_BASIC_INFORMATION mi;

    return (VirtualQuery((LPCVOID)p, &mi, sizeof(mi)) != 0)
        && (mi.State == MEM_COMMIT)
        && !(mi.Protect & (PAGE_NOACCESS | PAGE_GUARD))
        && (p + 8 <= (DWORD64)mi.BaseAddress + mi.RegionSize);
}

/* The stack of context @ctx, the frames after the first being return
 * addresses: a function without unwind data is a leaf, whose return address
 * is at the top of its stack. The walk stops where the stack cannot be read,
 * or at an address in no module and without unwind data, which is no code. */
static void put_stack(const CONTEXT *ctx)
{
    CONTEXT c = *ctx;
    PRUNTIME_FUNCTION f;
    DWORD64 base, frame;
    PVOID data;
    unsigned int i;
    bool named;

    put_registers(c.Rip, c.Rsp, c.EFlags);
    for (i = 0; (i < MAX_FRAMES) && (c.Rip != 0); i++) {
        named = put_frame(c.Rip);
        f = RtlLookupFunctionEntry(c.Rip, &base, NULL);
        if (!named && (f == NULL))
            break;
        if (!readable(c.Rsp)) {
            put("  stack unreadable at ");
            put_hex(c.Rsp);
            put("\n");
            break;
        }
        if (f == NULL) {
            c.Rip = *(DWORD64 *)c.Rsp;
            c.Rsp += 8;
        } else {
            RtlVirtualUnwind(UNW_FLAG_NHANDLER, base, c.Rip, f, &c, &data,
                             &frame, NULL);
        }
    }
}

/* Cygwin lays out a signal's machine context as Windows' CONTEXT: copied
 * into one, which has the alignment that its registers want. */
_Static_assert(sizeof(mcontext_t) >= sizeof(CONTEXT),
               "mcontext_t is not a CONTEXT");

static void put_signal_stack(void *uc)
{
    CONTEXT c;

    memcpy(&c, &((ucontext_t *)uc)->uc_mcontext, sizeof(c));
    put_stack(&c);
}

#else

static void put_signal_stack(void *uc)
{
    void *frames[MAX_FRAMES];
    unsigned int i;
    int n;
#ifdef __x86_64__
    const greg_t *r = ((ucontext_t *)uc)->uc_mcontext.gregs;

    put_registers(r[REG_RIP], r[REG_RSP], r[REG_EFL]);
#else
    (void)uc;
#endif
    n = backtrace(frames, MAX_FRAMES);
    for (i = 0; i < ARRAY_SIZE(report_fd); i++)
        if (report_fd[i] >= 0)
            backtrace_symbols_fd(frames, n, report_fd[i]);
}

#endif

void crash_init(int log_fd, void (*restore)(void))
{
    report_fd[1] = log_fd;
    restore_terminal = restore;
#ifndef __CYGWIN__
    /* The first backtrace() loads the unwinder, which allocates: done now,
     * not in a handler, which may run after the heap is corrupt. */
    {
        void *frame;
        backtrace(&frame, 1);
    }
#endif
}

void crash_name_thread(const char *name)
{
    stack_t ss;

    thread_name = name;
    /* A stack of its own for the report of a fault, which a stack overflow
     * leaves none of: the first time a thread names itself. */
    if ((sigaltstack(NULL, &ss) == 0) && (ss.ss_flags & SS_DISABLE)
        && ((ss.ss_sp = malloc(ALT_STACK_SIZE)) != NULL)) {
        ss.ss_size = ALT_STACK_SIZE;
        ss.ss_flags = 0;
        sigaltstack(&ss, NULL);
    }
}

void crash_report(int sig, const siginfo_t *si, void *uc)
{
    if (reporting)
        _exit(128 + sig);
    reporting = 1;
    if (restore_terminal != NULL)
        (*restore_terminal)();
    put("\nffemu: crashed with signal ");
    put_dec(sig);
    put(" ");
    put(signal_name(sig));
    if ((sig == SIGSEGV) || (sig == SIGBUS)) {
        put(" at ");
        put_hex((uintptr_t)si->si_addr);
    }
    put(" in the ");
    put((thread_name != NULL) ? thread_name : "unnamed");
    put(" thread; addr2line -f -e <program> <address> gives a frame's "
        "function\n");
    put_signal_stack(uc);
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

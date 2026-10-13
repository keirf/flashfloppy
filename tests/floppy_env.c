/*
 * floppy_env.c
 *
 * The board under the floppy code: register blocks in memory, a clock that
 * moves only when a test moves it, software timers, and an interrupt
 * controller that runs pending handlers when a test asks. On it, a host
 * that writes on the floppy interface while the floppy code's main loop
 * runs.
 *
 * Written & released by Mike Shevchenko <mike.shevchenko.kz@gmail.com>
 *
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

#include "harness.h"

struct gpio emu_gpio[7];
struct afio emu_afio;
struct exti emu_exti;
struct dma emu_dma[2];
struct tim emu_tim[7];

unsigned int ram_kb = 64;
uint8_t mcu_package = MCU_QFN32;
uint8_t has_kc30_header = 1;
uint32_t board_rotary_exti_mask;
unsigned int nr_speaker_pulses;

/*
 * Clock.
 */

static time_t now;
void (*clock_hook)(time_t now);

time_t time_now(void)
{
    return now;
}

static void clock_set(time_t t)
{
    now = t;
    if (clock_hook != NULL)
        (*clock_hook)(now);
}

void delay_from(time_t t, unsigned int ticks)
{
    if (time_diff(now, t + ticks) > 0)
        clock_set(t + ticks);
}

void delay_ticks(unsigned int ticks)
{
    clock_set(now + ticks);
}

uint32_t emu_stk_now(void)
{
    return -now & STK_MASK;
}

/*
 * Timers, in order of deadline.
 */

static struct timer *timers;

void timer_cancel(struct timer *timer)
{
    struct timer **pt;
    for (pt = &timers; *pt != NULL; pt = &(*pt)->next) {
        if (*pt == timer) {
            *pt = timer->next;
            break;
        }
    }
}

void timer_init(struct timer *timer, void (*cb_fn)(void *), void *cb_dat)
{
    timer_cancel(timer);
    timer->cb_fn = cb_fn;
    timer->cb_dat = cb_dat;
    timer->next = NULL;
}

void timer_set(struct timer *timer, time_t deadline)
{
    struct timer **pt;

    timer_cancel(timer);
    timer->deadline = deadline;
    for (pt = &timers; *pt != NULL; pt = &(*pt)->next)
        if (time_diff((*pt)->deadline, deadline) < 0)
            break;
    timer->next = *pt;
    *pt = timer;
}

static void timers_run(void)
{
    struct timer *t;
    while (((t = timers) != NULL) && (time_diff(t->deadline, now) >= 0)) {
        timers = t->next;
        t->next = NULL;
        (*t->cb_fn)(t->cb_dat);
    }
}

/*
 * Interrupts: enabled and pending bits; the handlers run in order of
 * number when a test runs them, never in the middle of other code.
 */

static uint64_t irq_enabled, irq_pending;

void IRQ_6(void);
void IRQ_7(void);
void IRQ_10(void);
void IRQ_12(void);
void IRQ_13(void);
void IRQ_23(void);
void IRQ_28(void);
void IRQ_40(void);
void IRQ_43(void);

void emu_irq_global(int enable)
{
}

unsigned int emu_irq_save(unsigned int newpri)
{
    return 0;
}

void emu_irq_restore(unsigned int oldpri)
{
}

void emu_irqx_enable(unsigned int x)
{
    irq_enabled |= 1ull << x;
}

void emu_irqx_disable(unsigned int x)
{
    irq_enabled &= ~(1ull << x);
}

void emu_irqx_set_pending(unsigned int x)
{
    irq_pending |= 1ull << x;
}

void emu_irqx_clear_pending(unsigned int x)
{
    irq_pending &= ~(1ull << x);
}

void emu_irqx_set_prio(unsigned int x, unsigned int pri)
{
}

void irq_raise(unsigned int x)
{
    emu_irqx_set_pending(x);
    irqs_run();
}

void irqs_run(void)
{
    uint64_t due;
    unsigned int x;

    while ((due = irq_pending & irq_enabled) != 0) {
        x = __builtin_ctzll(due);
        irq_pending &= ~(1ull << x);
        switch (x) {
        case 6: IRQ_6(); break;
        case 7: IRQ_7(); break;
        case 10: IRQ_10(); break;
        case 12: IRQ_12(); break;
        case 13: IRQ_13(); break;
        case 23: IRQ_23(); break;
        case 28: IRQ_28(); break;
        case 40: IRQ_40(); break;
        case 43: IRQ_43(); break;
        default:
            ft_abort(__FILE__, __LINE__, "IRQ %u has no handler", x);
        }
    }
}

void clock_run_ms(unsigned int ms)
{
    unsigned int i;
    for (i = 0; i < ms*10; i++) {
        clock_set(now + time_ms(1)/10);
        timers_run();
        irqs_run();
    }
}

void board_reset(void)
{
    struct timer *t;
    unsigned int i;

    /* Eject an image that an earlier test left inserted. */
    floppy_cancel();
    while ((t = timers) != NULL)
        timer_cancel(t);
    irq_enabled = irq_pending = 0;
    clock_hook = NULL;
    now = 0;
    memset(emu_gpio, 0, sizeof(emu_gpio));
    memset(&emu_afio, 0, sizeof(emu_afio));
    memset(&emu_exti, 0, sizeof(emu_exti));
    memset(emu_dma, 0, sizeof(emu_dma));
    memset(emu_tim, 0, sizeof(emu_tim));
    /* Inputs high, as pulled up, the floppy interface's being active low. */
    for (i = 0; i < ARRAY_SIZE(emu_gpio); i++)
        emu_gpio[i].idr = 0xffff;
    board_id = BRDREV_Gotek_standard;
    nr_speaker_pulses = 0;
}

void pin_set(unsigned int port, unsigned int pin, bool_t high)
{
    if (high)
        emu_gpio[port].idr |= 1u << pin;
    else
        emu_gpio[port].idr &= ~(1u << pin);
}

/* An edge on EXTI line @line: its pending flag set while its handler runs,
 * then cleared, as the handler's write of a 1 would clear it. */
static void exti_raise(unsigned int line, unsigned int irq)
{
    emu_exti.pr |= 1u << line;
    irq_raise(irq);
    emu_exti.pr &= ~(1u << line);
}

/*
 * The rest of the board.
 */

void gpio_configure_pin(GPIO gpio, unsigned int pin, unsigned int mode)
{
}

void _exti_route(unsigned int px, unsigned int pin)
{
}

void board_jc_set_mode(unsigned int mode)
{
}

bool_t board_jc_strapped(void)
{
    return FALSE;
}

void IRQ_rotary(void)
{
}

void speaker_pulse(void)
{
    nr_speaker_pulses++;
}

/* About the RAM that an STM32F105 of 64 KB leaves above the firmware's data
 * and stacks. */
static uint8_t arena[55*1024] aligned(32);
static uint32_t arena_used;

void arena_init(void)
{
    arena_used = 0;
}

void *arena_alloc(uint32_t sz)
{
    void *p = &arena[arena_used];
    sz = (sz + 31) & ~31u;
    if (sz > sizeof(arena) - arena_used)
        ft_abort(__FILE__, __LINE__, "arena of %u bytes full",
                 (unsigned int)sizeof(arena));
    arena_used += sz;
    return p;
}

uint32_t arena_avail(void)
{
    return sizeof(arena) - arena_used;
}

/*
 * A host on the floppy interface, and the floppy code's main loop.
 */

#define PA 0
#define PB 1
#define SEL_IRQ 6 /* EXTI0: SEL on PA0 */
#define SIDE_IRQ 10 /* EXTI4: SIDE on PB4 */
#define WGATE_PIN 1 /* PB1, the board's MCU being in QFN32 */
#define WGATE_IRQ 7 /* EXTI1 */
#define WDATA_RING 1024 /* samples in the floppy code's WDATA ring */
#define REV time_ms(200) /* a revolution at 300 rpm */
/* Ticks of the timer capturing WDATA to ticks of the clock. */
#define TIMER_TO_CLOCK(t) ((t) / (SAMPLECLK_MHZ / TIME_MHZ))

/* When the image was inserted, which the floppy code takes as the index. */
static time_t index_time;

void floppy_boot(void)
{
    board_reset();
    floppy_init();
    floppy_set_cyl(0, 0);
    pin_set(PA, 0, FALSE);
    irq_raise(SEL_IRQ);
}

static int insert_fn(void *slot)
{
    floppy_insert(0, slot);
    return 0;
}

void floppy_insert_image(const char *name)
{
    FRESULT fr;

    if ((fr = image_mount(name)) != FR_OK)
        ft_abort(__FILE__, __LINE__, "mounting %s gave %d", name, fr);
    if ((fr = F_call_cancellable(insert_fn, image_slot())) != FR_OK)
        ft_abort(__FILE__, __LINE__, "inserting %s gave %d", name, fr);
    index_time = now;
}

void floppy_head(unsigned int cyl, unsigned int head)
{
    floppy_set_cyl(0, cyl);
    pin_set(PB, 4, !head);
    irq_raise(SIDE_IRQ);
}

static int handle_fn(void *arg)
{
    return floppy_handle();
}

/* A pass of the main loop, then the clock 0.1 ms on. */
static void main_loop_pass(void)
{
    int rc = F_call_cancellable(handle_fn, NULL);
    if (rc != 0)
        ft_abort(__FILE__, __LINE__, "floppy_handle() gave %d", rc);
    clock_set(now + time_ms(1)/10);
    timers_run();
    irqs_run();
}

static void run_until(time_t t)
{
    while (time_diff(now, t) > 0)
        main_loop_pass();
}

static bool_t writing(void)
{
    struct track_info ti;
    floppy_get_track(&ti);
    return ti.writing;
}

/* The WDATA ring that the floppy code gave DMA1's channel 2. Its address
 * register holds the low 32 bits of a pointer into the arena. */
static uint16_t *wdata_ring(void)
{
    uintptr_t p = ((uintptr_t)arena & ~(uintptr_t)0xffffffffu)
        | emu_dma[0].ch[2-1].cmar;
    if ((p < (uintptr_t)arena)
        || (p + WDATA_RING*2 > (uintptr_t)arena + sizeof(arena)))
        ft_abort(__FILE__, __LINE__, "WDATA ring outside the arena");
    return (uint16_t *)p;
}

void host_write(uint32_t start, const uint8_t *cells, uint32_t nr_cells,
                unsigned int ticks, unsigned int jitter)
{
    volatile uint32_t *cndtr = &emu_dma[0].ch[2-1].cndtr;
    uint16_t *ring = wdata_ring();
    unsigned int prod = WDATA_RING - *cndtr, passes = 0;
    uint32_t i, t, seed = 1;
    int64_t wait;
    time_t t0;
    int j;

    /* Wait for the start to come round. */
    wait = (int64_t)TIMER_TO_CLOCK(start) - time_diff(index_time, now);
    wait = ((wait % REV) + REV) % REV;
    clock_set(now + wait);
    timers_run();
    irqs_run();

    t0 = now;
    pin_set(PB, WGATE_PIN, FALSE);
    exti_raise(WGATE_PIN, WGATE_IRQ);
    for (i = 0; i < nr_cells; i++) {
        if (!bit_get(cells, i))
            continue;
        seed = seed * 1103515245 + 12345;
        j = (int)((seed >> 16) % (2*jitter + 1)) - (int)jitter;
        t = (i + 1) * ticks + j;
        ring[prod++ % WDATA_RING] = t;
        if ((prod % 128) == 0) {
            *cndtr = WDATA_RING - (prod % WDATA_RING);
            irq_raise(DMA1_CH2_IRQ);
            run_until(t0 + TIMER_TO_CLOCK(t));
        }
    }
    *cndtr = WDATA_RING - (prod % WDATA_RING);
    irq_raise(DMA1_CH2_IRQ);
    run_until(t0 + TIMER_TO_CLOCK(nr_cells * ticks));
    pin_set(PB, WGATE_PIN, TRUE);
    exti_raise(WGATE_PIN, WGATE_IRQ);

    while (writing()) {
        if (++passes == 100000)
            ft_abort(__FILE__, __LINE__, "write never ended");
        main_loop_pass();
    }
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

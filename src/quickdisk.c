/*
 * quickdisk.c
 * 
 * Quick Disk interface control.
 * 
 * Written & released by Keir Fraser <keir.xen@gmail.com>
 * 
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

#define GPI_bus GPI_floating
#define GPO_bus GPO_pushpull(_2MHz, HIGH)
#define GPO_rdata GPO_pushpull(_2MHz, LOW)
#define AFO_rdata (AFO_pushpull(_2MHz) | (LOW<<4))

/* READY-window state machine and timer handling. */
static struct window {
    struct timer timer; /* Timer callback for state changes */
/* States describe action to take at *next* timer deadline. */
#define WIN_rdata_on  1 /* Activate RD */
#define WIN_ready_on  2 /* Assert /RY */
#define WIN_ready_off 3 /* Deassert /RY */
#define WIN_rdata_off 4 /* Mask RD */
    uint8_t state; /* WIN_* */
} window;

/* RD-active has a wider window than /RY-asserted. */
#define rd_before_ry time_ms(10) /* RD-active before /RY-asserted */
#define rd_after_ry  time_ms(10) /* RD-inactive after /RY-deasserted */

/* MOTOR state. */
static struct motor {
    bool_t on;   /* Is motor fully spun up? */
    struct timer timer; /* Spin-up timer */
} motor;

/* Timer functions. */
static void motor_timer(void *_drv);
static void window_timer(void *_drv);
static void index_assert(void *);

/* Track and modify states of output pins. */
static volatile struct {
    bool_t media;
    bool_t wrprot;
    bool_t ready;
} pins;
#define read_pin(pin) pins.pin
#define write_pin(pin, level) ({                \
    gpio_write_pin(gpio_out, pin_##pin, level); \
    pins.pin = level; })

#include "floppy_generic.c"

void floppy_cancel(void)
{
    struct drive *drv = &drive;

    /* Initialised? Bail if not. */
    if (!dma_rd)
        return;

    /* Immediately change outputs that we control entirely from the main loop. 
     * Asserting WRPROT prevents any further calls to wdata_start(). */
    write_pin(wrprot, HIGH);
    write_pin(media, HIGH);

    /* Deasserts /RY and turns off motor. */
    IRQx_set_pending(motor_irq);

    /* Stop DMA + timer work. */
    IRQx_disable(dma_rdata_irq);
    IRQx_disable(dma_wdata_irq);
    rdata_stop();
    wdata_stop();
    dma_rdata.ccr = 0;
    dma_wdata.ccr = 0;

    /* Clear soft state. */
    timer_cancel(&window.timer);
    timer_cancel(&index.timer);
    barrier(); /* cancel index.timer /then/ clear soft state */
    drv->index_suppressed = FALSE;
    drv->image = NULL;
    image = NULL;
    dma_rd = dma_wr = NULL;
    window.state = 0;
}

void floppy_set_fintf_mode(void)
{
    /* Quick Disk interface is static. */
}

void floppy_init(void)
{
    struct drive *drv = &drive;

    floppy_set_fintf_mode();

    printk("Interface: QuickDisk, JC=%s\n",
           !gpio_read_pin(gpiob, 1) ? "On (Roland)" : "Off");
    
    board_floppy_init();

    timer_init(&motor.timer, motor_timer, drv);
    timer_init(&window.timer, window_timer, drv);

    gpio_configure_pin(gpio_out, pin_02, GPO_bus);
    gpio_configure_pin(gpio_out, pin_08, GPO_bus);
    gpio_configure_pin(gpio_out, pin_26, GPO_bus);
    gpio_configure_pin(gpio_out, pin_28, GPO_bus);
    gpio_configure_pin(gpio_out, pin_34, GPO_bus);

    gpio_configure_pin(gpio_data, pin_wdata, GPI_bus);
    gpio_configure_pin(gpio_data, pin_rdata, GPO_rdata);

    write_pin(media,  HIGH);
    write_pin(wrprot, HIGH);
    write_pin(ready,  HIGH);

    floppy_init_irqs();

    timer_init(&index.timer, index_assert, NULL);
}

void floppy_insert(unsigned int unit, struct slot *slot)
{
    floppy_mount(slot);

    timer_dma_init();
    tim_rdata->ccr2 = sysclk_ns(1500); /* RD: 1.5us positive pulses */

    /* Drive is ready. Set output signals appropriately. */
    write_pin(media, LOW);
    if (!(slot->attributes & AM_RDO))
        write_pin(wrprot, LOW);

    /* Motor spins up, if enabled. */
    IRQx_set_pending(motor_irq);
}

static void floppy_sync_flux(void)
{
    const uint16_t buf_mask = ARRAY_SIZE(dma_rd->buf) - 1;
    struct drive *drv = &drive;
    uint16_t nr_to_wrap, nr_to_cons, nr;
    uint32_t oldpri;

    /* No DMA should occur until the timer is enabled. */
    ASSERT(dma_rd->cons == (ARRAY_SIZE(dma_rd->buf) - dma_rdata.cndtr));

    nr_to_wrap = ARRAY_SIZE(dma_rd->buf) - dma_rd->prod;
    nr_to_cons = (dma_rd->cons - dma_rd->prod - 1) & buf_mask;
    nr = min(nr_to_wrap, nr_to_cons);
    if (nr) {
        dma_rd->prod += image_rdata_flux(
            drv->image, &dma_rd->buf[dma_rd->prod], nr);
        dma_rd->prod &= buf_mask;
    }

    nr = (dma_rd->prod - dma_rd->cons) & buf_mask;
    if (nr < buf_mask)
        return;

    /* Must not currently be driving through the state machine. */
    if (window.state != 0)
        return;

    /* Motor must be spun up to start reading. */
    if (!motor.on)
        return;

    oldpri = IRQ_save(TIMER_IRQ_PRI);
    timer_cancel(&index.timer);
    rdata_start();
    index.timer.deadline = time_now();
    index_assert(NULL);
    IRQ_restore(oldpri);
}

static bool_t dma_rd_handle(struct drive *drv)
{
    switch (dma_rd->state) {

    case DMA_inactive: {
        time_t read_start_pos = 0;
        if (image_setup_track(drv->image, 0, &read_start_pos))
            return TRUE;
        /* Change state /then/ check for race against step or side change. */
        dma_rd->state = DMA_starting;
        barrier();
        if (dma_wr->state != DMA_inactive)
            dma_rd->state = DMA_stopping;
        break;
    }

    case DMA_starting:
        floppy_sync_flux();
        /* fall through */

    case DMA_active:
        floppy_read_data(drv);
        break;

    case DMA_stopping:
        dma_rd->state = DMA_inactive;
        /* Reinitialise the circular buffer to empty. */
        dma_rd->cons = dma_rd->prod =
            ARRAY_SIZE(dma_rd->buf) - dma_rdata.cndtr;
        /* Free-running index timer. */
        timer_cancel(&index.timer);
        timer_set(&index.timer, index.prev_time + drv->image->stk_per_rev);
        break;
    }

    return FALSE;
}

void floppy_get_track(struct track_info *ti)
{
    ti->cyl = ti->side = 0;
    ti->sel = TRUE;
    ti->writing = (dma_wr && dma_wr->state != DMA_inactive);
}

static void index_assert(void *dat)
{
    struct drive *drv = &drive;
    struct window *w = &window;
    time_t now = index.timer.deadline;

    index.prev_time = now;

    if (motor.on && (dma_rd->state == DMA_active)) {

        /* Reset the window state machine to start over. */
        w->state = WIN_rdata_on;
        timer_set(&w->timer, now + drv->image->qd.win_start - rd_before_ry);

    } else {

        /* Disable RDATA. */
        rdata_stop();

        /* Window state machine is idle. */
        w->state = 0;
        timer_cancel(&w->timer);

        /* Stop any ongoing write. */
        if (dma_wr->state != DMA_inactive)
            IRQx_set_pending(wgate_irq);

    }
}

static void motor_timer(void *_drv)
{
    /* Motor is now spun up. */
    motor.on = TRUE;
}

static void window_timer(void *_drv)
{
    struct drive *drv = _drv;
    struct window *w = &window;
    time_t now = w->timer.deadline;

    switch (w->state) {

    case WIN_rdata_on:
        if (dma_rd->state == DMA_active)
            gpio_configure_pin(gpio_data, pin_rdata, AFO_rdata);
        timer_set(&w->timer, now + rd_before_ry);
        break;

    case WIN_ready_on:
        if (motor.on)
            write_pin(ready, LOW);
        timer_set(&w->timer,
                  now + drv->image->qd.win_end - drv->image->qd.win_start);
        break;

    case WIN_ready_off:
        write_pin(ready, HIGH);
        timer_set(&w->timer, now + rd_after_ry);
        break;

    case WIN_rdata_off:
        gpio_configure_pin(gpio_data, pin_rdata, GPO_rdata);
        break;

    }

    w->state++;
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

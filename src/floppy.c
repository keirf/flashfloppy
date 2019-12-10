/*
 * floppy.c
 * 
 * Floppy interface control.
 * 
 * Written & released by Keir Fraser <keir.xen@gmail.com>
 * 
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

#define GPI_bus GPI_floating
#define GPO_bus GPO_pushpull(_2MHz,O_FALSE)
#define AFO_bus (AFO_pushpull(_2MHz) | (O_FALSE<<4))

#define GPO_rdata GPO_bus
#define AFO_rdata AFO_bus

/* A soft IRQ for handling lower priority work items. */
static void chgrst_timer(void *_drv);
static void drive_step_timer(void *_drv);
static void motor_spinup_timer(void *_drv);
void IRQ_43(void) __attribute__((alias("IRQ_soft")));
#define FLOPPY_SOFTIRQ 43

/* Index-pulse timer functions. */
static void index_assert(void *);   /* index.timer */
static void index_deassert(void *); /* index.timer_deassert */

static time_t sync_time, sync_pos;

static time_t prefetch_start_time;
static uint32_t max_prefetch_us;

struct drive;
static always_inline void drive_change_output(
    struct drive *drv, uint8_t outp, bool_t assert);

#include "floppy_generic.c"

static uint8_t pin02, pin02_inverted;
static uint8_t pin34, pin34_inverted;
static uint8_t fintf_mode;
const static struct fintf {
    uint8_t pin02:4;
    uint8_t pin34:4;
} fintfs[] = {
    [FINTF_SHUGART] = {
        .pin02 = outp_dskchg,
        .pin34 = outp_rdy },
    [FINTF_IBMPC] = {
        .pin02 = outp_unused,
        .pin34 = outp_dskchg },
    [FINTF_IBMPC_HDOUT] = {
        .pin02 = outp_hden,
        .pin34 = outp_dskchg },
    [FINTF_AKAI_S950] = {
        .pin02 = outp_hden,
        .pin34 = outp_rdy },
    [FINTF_AMIGA] = {
        .pin02 = outp_dskchg,
        .pin34 = outp_unused }
};

static always_inline void drive_change_pin(
    struct drive *drv, uint8_t pin, bool_t assert)
{
    uint16_t pin_mask = m(pin);

    /* Logically assert or deassert the pin. */
    if (assert)
        gpio_out_active |= pin_mask;
    else
        gpio_out_active &= ~pin_mask;

    /* Update the physical output pin, if the drive is selected. */
    if (drv->sel)
        gpio_write_pins(gpio_out, pin_mask, assert ? O_TRUE : O_FALSE);

    /* Caller expects us to re-enable interrupts. */
    IRQ_global_enable();
}

static void _drive_change_output(
    struct drive *drv, uint8_t outp, bool_t assert)
{
    IRQ_global_enable();

    if (pin02 == outp) {
        IRQ_global_disable();
        drive_change_pin(drv, pin_02, assert ^ pin02_inverted);
    }

    if (pin34 == outp) {
        IRQ_global_disable();
        drive_change_pin(drv, pin_34, assert ^ pin34_inverted);
    }
}

static void drive_change_output(
    struct drive *drv, uint8_t outp, bool_t assert)
{
    uint8_t outp_mask = m(outp);
    uint8_t pin;

    IRQ_global_disable();

    /* Logically assert or deassert the output line. */
    if (assert)
        drv->outp |= outp_mask;
    else
        drv->outp &= ~outp_mask;

    switch (outp) {
    case outp_index:  pin = pin_08; break;
    case outp_trk0:   pin = pin_26; break;
    case outp_wrprot: pin = pin_28; break;
    default:
        _drive_change_output(drv, outp, assert);
        return;
    }
    drive_change_pin(drv, pin, assert);
}

static void update_amiga_id(struct drive *drv, bool_t amiga_hd_id)
{
    /* Only for the Amiga interface, with hacked RDY (pin 34) signal. */
    if (fintf_mode != FINTF_AMIGA)
        return;

    drive_change_output(drv, outp_hden, amiga_hd_id);

    if (pin34 != outp_unused)
        return;

    IRQ_global_disable();

    /* If mounting an HD image then we signal to the host by toggling pin 34 
     * every time the drive is selected. */
    update_SELA_irq(amiga_hd_id);

    /* DD-ID: !!HACK!! We permanently assert pin 34, even when no disk is
     * inserted. Properly we should only do this when MTR is asserted. */
    /* HD ID: !!HACK!! Without knowledge of MTR signal we cannot synchronise
     * the HD-ID sequence 101010... with the host poll loop. It turns out that
     * starting with pin 34 asserted when the HD image is mounted seems to
     * generally work! */
    gpio_out_active |= m(pin_34);
    if (drive.sel)
        gpio_write_pins(gpio_out, m(pin_34), O_TRUE);

    IRQ_global_enable();
}

void floppy_cancel(void)
{
    struct drive *drv = &drive;

    /* Initialised? Bail if not. */
    if (!dma_rd)
        return;

    /* Immediately change outputs that we control entirely from the main loop. 
     * Asserting WRPROT prevents any further calls to wdata_start(). */
    drive_change_output(drv, outp_wrprot, TRUE);
    drive_change_output(drv, outp_hden, FALSE);
    update_amiga_id(drv, FALSE);

    /* Stop DMA + timer work. */
    IRQx_disable(dma_rdata_irq);
    IRQx_disable(dma_wdata_irq);
    rdata_stop();
    wdata_stop();
    dma_rdata.ccr = 0;
    dma_wdata.ccr = 0;

    /* Clear soft state. */
    timer_cancel(&drv->chgrst_timer);
    timer_cancel(&index.timer);
    barrier(); /* cancel index.timer /then/ clear soft state */
    drv->index_suppressed = FALSE;
    drv->image = NULL;
    drv->inserted = FALSE;
    image = NULL;
    dma_rd = dma_wr = NULL;
    index.fake_fired = FALSE;
    barrier(); /* clear soft state /then/ cancel index.timer_deassert */
    timer_cancel(&index.timer_deassert);
    motor_chgrst_eject(drv);

    /* Set outputs for empty drive. */
    barrier();
    drive_change_output(drv, outp_index, FALSE);
    drive_change_output(drv, outp_dskchg, TRUE);
}

void floppy_set_fintf_mode(void)
{
    static const char * const fintf_name[] = {
        [FINTF_SHUGART]     = "Shugart",
        [FINTF_IBMPC]       = "IBM PC",
        [FINTF_IBMPC_HDOUT] = "IBM PC + HD_OUT",
        [FINTF_AKAI_S950]   = "Akai S950",
        [FINTF_AMIGA]       = "Amiga"
    };
    static const char *const outp_name[] = {
        [outp_dskchg] = "chg",
        [outp_rdy] = "rdy",
        [outp_hden] = "dens",
        [outp_unused] = "high"
    };
    struct drive *drv = &drive;
    uint32_t old_active;
    uint8_t mode = ff_cfg.interface;

    if (mode == FINTF_JC) {
        /* Jumper JC selects default floppy interface configuration:
         *   - No Jumper: Shugart
         *   - Jumpered:  IBM PC */
        mode = gpio_read_pin(gpiob, 1) ? FINTF_SHUGART : FINTF_IBMPC;
    }

    ASSERT(mode < ARRAY_SIZE(fintfs));

    IRQ_global_disable();

    fintf_mode = mode;
    pin02 = ff_cfg.pin02 ? ff_cfg.pin02 - 1 : fintfs[mode].pin02;
    pin34 = ff_cfg.pin34 ? ff_cfg.pin34 - 1 : fintfs[mode].pin34;
    pin02_inverted = !!(pin02 & PIN_invert);
    pin34_inverted = !!(pin34 & PIN_invert);
    pin02 &= ~PIN_invert;
    pin34 &= ~PIN_invert;

    old_active = gpio_out_active;
    gpio_out_active &= ~(m(pin_02) | m(pin_34));
    if (((drv->outp >> pin02) ^ pin02_inverted) & 1)
        gpio_out_active |= m(pin_02);
    if (((drv->outp >> pin34) ^ pin34_inverted) & 1)
        gpio_out_active |= m(pin_34);

    /* Default handler for IRQ_SELA_changed */
    update_SELA_irq(FALSE);

    if (drv->sel) {
        gpio_write_pins(gpio_out, old_active & ~gpio_out_active, O_FALSE);
        gpio_write_pins(gpio_out, ~old_active & gpio_out_active, O_TRUE);
    }

    IRQ_global_enable();

    /* Default to Amiga-DD identity until HD image is mounted. */
    update_amiga_id(drv, FALSE);

    printk("Interface: %s (pin2=%s%s, pin34=%s%s)\n",
           fintf_name[mode],
           pin02_inverted ? "not-" : "", outp_name[pin02] ?: "?",
           pin34_inverted ? "not-" : "", outp_name[pin34] ?: "?");
}

void floppy_init(void)
{
    struct drive *drv = &drive;

    floppy_set_fintf_mode();

    board_floppy_init();

    timer_init(&drv->step.timer, drive_step_timer, drv);
    timer_init(&drv->motor.timer, motor_spinup_timer, drv);
    timer_init(&drv->chgrst_timer, chgrst_timer, drv);

    gpio_configure_pin(gpio_out, pin_02, GPO_bus);
    gpio_configure_pin(gpio_out, pin_08, GPO_bus);
    gpio_configure_pin(gpio_out, pin_26, GPO_bus);
    gpio_configure_pin(gpio_out, pin_28, GPO_bus);
    gpio_configure_pin(gpio_out, pin_34, GPO_bus);

    gpio_configure_pin(gpio_data, pin_wdata, GPI_bus);
    gpio_configure_pin(gpio_data, pin_rdata, GPO_bus);

    drive_change_output(drv, outp_dskchg, TRUE);
    drive_change_output(drv, outp_wrprot, TRUE);
    drive_change_output(drv, outp_trk0,   TRUE);

    floppy_init_irqs();

    IRQx_set_prio(FLOPPY_SOFTIRQ, FLOPPY_SOFTIRQ_PRI);
    IRQx_enable(FLOPPY_SOFTIRQ);

    timer_init(&index.timer, index_assert, NULL);
    timer_init(&index.timer_deassert, index_deassert, NULL);

    motor_chgrst_eject(drv);
}

void floppy_insert(unsigned int unit, struct slot *slot)
{
    struct image *im;
    struct drive *drv = &drive;

    /* Report only significant prefetch times (> 10ms). */
    max_prefetch_us = 10000;

    floppy_mount(slot);
    im = image;

    if (im->write_bc_ticks < sysclk_ns(1500))
        drive_change_output(drv, outp_hden, TRUE);

    timer_dma_init();

    /* Drive is ready. Set output signals appropriately. */
    update_amiga_id(drv, im->stk_per_rev > stk_ms(300));
    if (!(slot->attributes & AM_RDO))
        drive_change_output(drv, outp_wrprot, FALSE);
    barrier();
    drv->inserted = TRUE;
    motor_chgrst_insert(drv); /* update RDY + motor state */
    if (ff_cfg.chgrst <= CHGRST_delay(15))
        timer_set(&drv->chgrst_timer, time_now() + ff_cfg.chgrst*time_ms(500));
}

static void floppy_sync_flux(void)
{
    const uint16_t buf_mask = ARRAY_SIZE(dma_rd->buf) - 1;
    struct drive *drv = &drive;
    uint32_t prefetch_us;
    uint16_t nr_to_wrap, nr_to_cons, nr;
    int32_t ticks;

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

    /* Log maximum prefetch times. */
    prefetch_us = time_diff(prefetch_start_time, time_now()) / TIME_MHZ;
    if (prefetch_us > max_prefetch_us) {
        max_prefetch_us = prefetch_us;
        printk("[%uus]\n", max_prefetch_us);
    }

    if (!drv->index_suppressed) {
        ticks = time_diff(time_now(), sync_time) - time_us(1);
        if (ticks > time_ms(15)) {
            /* Too long to wait. Immediately re-sync index timing. */
            drv->index_suppressed = TRUE;
            printk("Trk %u: skip %ums\n",
                   drv->image->cur_track, (ticks+time_us(500))/time_ms(1));
        } else if (ticks > time_ms(5)) {
            /* A while to wait. Go do other work. */
            return;
        } else {
            if (ticks > 0)
                delay_ticks(ticks);
            /* If we're out of sync then forcibly re-sync index timing. */
            ticks = time_diff(time_now(), sync_time);
            if (ticks < -100) {
                drv->index_suppressed = TRUE;
                printk("Trk %u: late %uus\n",
                       drv->image->cur_track, -ticks/time_us(1));
            }
        }
    } else if (drv->step.state) {
        /* IDX is suppressed: Wait for heads to settle.
         * When IDX is not suppressed, settle time is already accounted for in
         * dma_rd_handle()'s call to image_setup_track(). */
        time_t step_settle = drv->step.start + time_ms(ff_cfg.head_settle_ms);
        int32_t delta = time_diff(time_now(), step_settle) - time_us(1);
        if (delta > time_ms(5))
            return; /* go do other work for a while */
        if (delta > 0)
            delay_ticks(delta);
    }

    if (drv->index_suppressed) {

        /* Re-enable index timing, snapped to the new read stream. 
         * Disable low-priority IRQs to keep timings tight. */
        uint32_t oldpri = IRQ_save(TIMER_IRQ_PRI);

        timer_cancel(&index.timer);

        /* If we crossed the index mark while filling the DMA buffer then we
         * need to set up the index pulse (usually done by IRQ_rdata_dma). */
        if (image_ticks_since_index(drv->image)
            < (sync_pos*(SYSCLK_MHZ/STK_MHZ))) {

            /* Sum all flux timings in the DMA buffer. */
            const uint16_t buf_mask = ARRAY_SIZE(dma_rd->buf) - 1;
            uint32_t i, ticks = 0;
            for (i = dma_rd->cons; i != dma_rd->prod; i = (i+1) & buf_mask)
                ticks += dma_rd->buf[i] + 1;

            /* Subtract current flux offset beyond the index. */
            ticks -= image_ticks_since_index(drv->image);

            /* Calculate deadline for index timer. */
            ticks /= SYSCLK_MHZ/TIME_MHZ;
            timer_set(&index.timer, time_now() + ticks);
        }

        IRQ_global_disable();
        IRQ_restore(oldpri);
        index.prev_time = time_now() - sync_pos;
        drv->index_suppressed = FALSE;
    }

    rdata_start();
}

static bool_t dma_rd_handle(struct drive *drv)
{
    switch (dma_rd->state) {

    case DMA_inactive: {
        time_t index_time, read_start_pos;
        unsigned int track;
        /* Allow 10ms from current rotational position to load new track */
        int32_t delay = time_ms(10);
        /* Allow extra time if heads are settling. */
        if (drv->step.state & STEP_settling) {
            time_t step_settle = drv->step.start
                + time_ms(ff_cfg.head_settle_ms);
            int32_t delta = time_diff(time_now(), step_settle);
            delay = max_t(int32_t, delta, delay);
        }
        /* No data fetch while stepping. */
        barrier(); /* check STEP_settling /then/ check STEP_active */
        if (drv->step.state & STEP_active)
            break;
        /* Work out where in new track to start reading data from. */
        index_time = index.prev_time;
        read_start_pos = drv->index_suppressed
            ? drive.restart_pos /* start read exactly where write ended */
            : time_since(index_time) + delay;
        read_start_pos %= drv->image->stk_per_rev;
        /* Seek to the new track. */
        track = drive_calc_track(drv);
        read_start_pos *= SYSCLK_MHZ/STK_MHZ;
        if ((track >= (DA_FIRST_CYL*2)) && (drv->outp & m(outp_wrprot))
            && !volume_readonly()) {
            /* Remove write-protect when driven into D-A mode. */
            drive_change_output(drv, outp_wrprot, FALSE);
        }
        if (image_setup_track(drv->image, track, &read_start_pos))
            return TRUE;
        prefetch_start_time = time_now();
        read_start_pos /= SYSCLK_MHZ/STK_MHZ;
        sync_pos = read_start_pos;
        if (!drv->index_suppressed) {
            /* Set the deadline to match existing index timing. */
            sync_time = index_time + read_start_pos;
            if (time_diff(time_now(), sync_time) < 0)
                sync_time += drv->image->stk_per_rev;
        }
        /* Change state /then/ check for race against step or side change. */
        dma_rd->state = DMA_starting;
        barrier();
        if ((drv->step.state & STEP_active)
            || (track != drive_calc_track(drv))
            || (dma_wr->state != DMA_inactive))
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

void floppy_set_cyl(uint8_t unit, uint8_t cyl)
{
    if (unit == 0) {
        struct drive *drv = &drive;
        drv->cyl = cyl;
        if (cyl == 0)
            drive_change_output(drv, outp_trk0, TRUE);
    }
}

void floppy_get_track(struct track_info *ti)
{
    ti->cyl = drive.cyl;
    ti->side = drive.head & (drive.nr_sides - 1);
    ti->sel = drive.sel;
    ti->writing = (dma_wr && dma_wr->state != DMA_inactive);
}

static void index_assert(void *dat)
{
    struct drive *drv = &drive;
    index.prev_time = index.timer.deadline;
    if (!drv->index_suppressed
        && !(drv->step.state && ff_cfg.index_suppression)
        && drv->motor.on) {
        drive_change_output(drv, outp_index, TRUE);
        timer_set(&index.timer_deassert, index.prev_time + time_ms(2));
    }
    if (dma_rd->state != DMA_active) /* timer set from input flux stream */
        timer_set(&index.timer, index.prev_time + drv->image->stk_per_rev);
}

static void index_deassert(void *dat)
{
    struct drive *drv = &drive;
    drive_change_output(drv, outp_index, FALSE);
}

static void chgrst_timer(void *_drv)
{
    struct drive *drv = _drv;
    drive_change_output(drv, outp_dskchg, FALSE);
}

static void drive_step_timer(void *_drv)
{
    struct drive *drv = _drv;

    switch (drv->step.state) {
    case STEP_started:
        /* nothing to do, IRQ_soft() needs to reset our deadline */
        break;
    case STEP_latched:
        speaker_pulse();
        if ((drv->cyl >= 84) && !drv->step.inward)
            drv->cyl = 84; /* Fast step back from D-A cyls */
        drv->cyl += drv->step.inward ? 1 : -1;
        timer_set(&drv->step.timer,
                  drv->step.start + time_ms(ff_cfg.head_settle_ms));
        if (drv->cyl == 0)
            drive_change_output(drv, outp_trk0, TRUE);
        /* New state last, as that lets hi-pri IRQ start another step. */
        barrier();
        drv->step.state = STEP_settling;
        break;
    case STEP_settling:
        /* Can race transition to STEP_started. */
        cmpxchg(&drv->step.state, STEP_settling, 0);
        break;
    }
}

static void motor_spinup_timer(void *_drv)
{
    struct drive *drv = _drv;

    drv->motor.on = TRUE;
    drive_change_output(drv, outp_rdy, TRUE);
}

static void IRQ_soft(void)
{
    struct drive *drv = &drive;

    if (drv->step.state == STEP_started) {
        timer_cancel(&drv->step.timer);
        drv->step.state = STEP_latched;
        timer_set(&drv->step.timer, drv->step.start + time_ms(1));
    }

    if (index.fake_fired) {
        index.fake_fired = FALSE;
        timer_set(&index.timer_deassert, time_now() + time_us(500));
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

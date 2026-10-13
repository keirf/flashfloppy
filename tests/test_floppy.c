/*
 * test_floppy.c
 *
 * The floppy interface: how the head steps for a Shugart host's STEP and
 * DIR.
 *
 * Written & released by Mike Shevchenko <mike.shevchenko.kz@gmail.com>
 *
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

#include "harness.h"

#define PA 0
#define PB 1
#define STEP_IRQ 28 /* TIM2, capturing STEP */

static unsigned int track(void)
{
    struct track_info ti;
    floppy_get_track(&ti);
    return ti.cyl;
}

/* A Shugart host steps by a pulse on STEP, inward while DIR is low. */
static void shugart_boot(void)
{
    drive_reset();
    board_reset();
    floppy_init();
    floppy_set_cyl(0, 0);
    pin_set(PA, 0, FALSE); /* selected */
}

static void steps(unsigned int nr, bool_t inward)
{
    pin_set(PB, 0, !inward);
    while (nr--) {
        irq_raise(STEP_IRQ);
        clock_run_ms(4);
    }
}

TEST(floppy_shugart_steps)
{
    shugart_boot();
    steps(5, TRUE);
    CHECK_EQ(track(), 5);
    steps(3, FALSE);
    CHECK_EQ(track(), 2);
    steps(4, FALSE);
    CHECK_EQ(track(), 0);
    CHECK_EQ(nr_speaker_pulses, 10);
    /* Deselected: no steps. */
    pin_set(PA, 0, TRUE);
    steps(2, TRUE);
    CHECK_EQ(track(), 0);
    /* No step beyond the last cylinder. */
    pin_set(PA, 0, FALSE);
    floppy_set_cyl(0, ff_cfg.max_cyl);
    steps(1, TRUE);
    CHECK_EQ(track(), ff_cfg.max_cyl);
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

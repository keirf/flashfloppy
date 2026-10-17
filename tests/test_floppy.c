/*
 * test_floppy.c
 *
 * The floppy interface: how the head steps for a Shugart host's STEP and
 * DIR, or for an Apple2 host's stepper phases.
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

/* The head's track: the apple2 target gives the cylinder in half tracks. */
static unsigned int track(void)
{
    struct track_info ti;
    floppy_get_track(&ti);
    return (TARGET == TARGET_apple2) ? ti.cyl / 2 : ti.cyl;
}

#if TARGET == TARGET_apple2

/* The stepper phases on, bits 0 to 3, on their pins: phase 0 on PA10, 1 on
 * PA9, 2 on PB0 and 3 on PA1; active high. */
static void set_phases(unsigned int pha)
{
    pin_set(PA, 10, pha & 1);
    pin_set(PA, 9, pha & 2);
    pin_set(PB, 0, pha & 4);
    pin_set(PA, 1, pha & 8);
}

/* An Apple2 host steps the head a half track at a time by turning on the
 * phase next to the current one, as DOS 3.3 does, through the pairs of
 * phases between them: the eight states of a seek, at most two phases on. */
static const uint8_t seek_states[8] = { 1, 3, 2, 6, 4, 12, 8, 9 };
static unsigned int seek_state; /* in seek_states[], even: one phase on */

static void seek(int halftracks)
{
    unsigned int i, n = (halftracks > 0) ? halftracks : -halftracks;

    for (i = 0; i < 2*n; i++) {
        seek_state = (seek_state + ((halftracks > 0) ? 1 : 7)) % 8;
        set_phases(seek_states[seek_state]);
        clock_run_ms(5);
    }
}

static void apple2_boot(void)
{
    drive_reset();
    board_reset();
    set_phases(0);
    floppy_init();
    floppy_set_cyl(0, 0);
    pin_set(PA, 0, FALSE); /* selected */
    /* Phase 0 on, the head at track 0, so that the steps start from it. */
    seek_state = 0;
    set_phases(seek_states[0]);
    clock_run_ms(5);
}

TEST(floppy_apple2_steps)
{
    apple2_boot();
    seek(6);
    CHECK_EQ(track(), 3);
    CHECK_EQ(nr_speaker_pulses, 6);
    seek(-4);
    CHECK_EQ(track(), 1);
    seek(-2);
    CHECK_EQ(track(), 0);
    /* No step out from track 0. */
    seek(-2);
    CHECK_EQ(track(), 0);
    CHECK_EQ(nr_speaker_pulses, 12);
}

/* The phases on, debounced, as the floppy code gives them to the display. */
TEST(floppy_apple2_phases_shown)
{
    apple2_boot();
    set_phases(6);
    clock_run_ms(3);
    CHECK_EQ(apple2_phases, 6);
    /* Deselected: no phases, no steps. */
    pin_set(PA, 0, TRUE);
    clock_run_ms(3);
    CHECK_EQ(apple2_phases, 0);
    seek(4);
    CHECK_EQ(track(), 0);
}

#else

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

#endif

/*
 * Local variables:
 * mode: C
 * c-file-style: "Linux"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */

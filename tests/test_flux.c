/*
 * test_flux.c
 *
 * The flux the floppy code is fed: the interval to each flux reversal of a
 * track, whose bit cells the handler made, a fraction of a tick carried from
 * one interval to the next.
 *
 * Written & released by Mike Shevchenko <mike.shevchenko.kz@gmail.com>
 *
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

#include "harness.h"

#define MAX_FLUX (64*1024)

static uint8_t file[1024*1024], cells[64*1024];
static uint16_t got[MAX_FLUX], want[MAX_FLUX];
static uint32_t lcg = 1;

static void fill(uint8_t *p, uint32_t len)
{
    while (len--)
        *p++ = (lcg = lcg * 1103515245 + 12345) >> 16;
}

/* Track @track's flux from the index, against its cells. */
static void check_flux(unsigned int track, bool_t lsb_first)
{
    unsigned int len, nr;

    len = image_stream(track, cells, sizeof(cells));
    nr = flux_of_cells(want, MAX_FLUX, cells, len * 8, lsb_first,
                       im->ticks_per_cell);
    REQUIRE((nr > 1000) && (nr < MAX_FLUX));
    image_flux(track, got, nr);
    if (!CHECK_MEM(got, want, nr * 2))
        ft_fail(__FILE__, __LINE__, "track %u, %u flux reversals",
                track, nr);
}

/* IBM PC 720K: 2us cells of MFM made of the sectors. */
TEST(flux_img)
{
    drive_reset();
    fill(file, 80*2*9*512);
    drive_add("PC720.IMG", file, 80*2*9*512);
    REQUIRE(image_mount("PC720.IMG") == FR_OK);
    check_flux(0, FALSE);
    check_flux(77*2 + 1, FALSE);
}

/* HFE: the cells as the file holds them, LSB first, at the header's rate. */
TEST(flux_hfe)
{
    const unsigned int trk = 12500, blks = (trk + 255) / 256;
    unsigned int i;

    drive_reset();
    memset(file, 0xff, 1024);
    memcpy(file, "HXCPICFE", 8);
    file[8] = 0; /* revision */
    file[9] = 1; /* tracks */
    file[10] = 1; /* sides */
    file[11] = 0; /* ISO MFM */
    file[12] = 250; /* kbit/s */
    file[13] = 0;
    file[14] = file[15] = 0; /* rpm */
    file[18] = 1; /* track list at block 1 */
    file[19] = 0;
    file[512] = 2; /* track 0 at block 2 */
    file[513] = 0;
    file[514] = (trk * 2) & 0xff; /* both sides */
    file[515] = (trk * 2) >> 8;
    for (i = 0; i < blks; i++) {
        fill(&file[1024 + i*512], 256);
        memset(&file[1024 + i*512 + 256], 0, 256);
    }
    drive_add("MFM.HFE", file, 1024 + blks*512);
    REQUIRE(image_mount("MFM.HFE") == FR_OK);
    check_flux(0, TRUE);
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

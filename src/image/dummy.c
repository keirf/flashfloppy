/*
 * dummy.c
 * 
 * Dummy handler for empty image slots.
 * 
 * Written & released by Keir Fraser <keir.xen@gmail.com>
 * 
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

static bool_t dummy_open(struct image *im)
{
    /* Check for dummy slot info (zero-sized, invalid start cluster). */
    im->nr_sides = 1;
    return (im->fp.obj.sclust == ~0u) && (f_size(&im->fp) == 0);
}

static void dummy_setup_track(
    struct image *im, uint16_t track, uint32_t *start_pos)
{
    im->cur_track = track;
    im->cur_ticks = (start_pos ? *start_pos : 0) * 16;
    im->tracklen_ticks = sampleclk_stk(im->stk_per_rev) * 16;
    im->ticks_since_flux = 0;
}

static bool_t dummy_read_track(struct image *im)
{
    return TRUE;
}

static uint16_t dummy_rdata_flux(struct image *im, uint16_t *tbuf, uint16_t nr)
{
    uint32_t todo = nr, ticks, cur_ticks = im->cur_ticks;

    while (todo--) {
        ticks = ((rand() >> 4) & 1023) + 100;
        cur_ticks += ticks << 4;
        *tbuf++ = ticks - 1;
    }

    im->cur_ticks = cur_ticks % im->tracklen_ticks;

    return nr;
}

static bool_t dummy_write_track(struct image *im)
{
    bool_t flush = (im->wr_cons != im->wr_bc);
    return flush;
}

static void dummy_sync(struct image *im)
{
}

const struct image_handler dummy_image_handler = {
    .open = dummy_open,
    .setup_track = dummy_setup_track,
    .read_track = dummy_read_track,
    .rdata_flux = dummy_rdata_flux,
    .write_track = dummy_write_track,
    .sync = dummy_sync,

    .async = TRUE,
};

/*
 * Local variables:
 * mode: C
 * c-file-style: "Linux"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */

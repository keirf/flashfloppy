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
    return (im->fp.obj.sclust == ~0u) && (f_size(&im->fp) == 0);
}

static void dummy_setup_track(
    struct image *im, uint16_t track, uint32_t *start_pos)
{
    struct image_buf *bc = &im->bufs.read_bc;
    uint32_t sys_ticks = start_pos ? *start_pos : 0;

    im->cur_track = track;

    im->ticks_per_cell = im->write_bc_ticks * 16;
    im->tracklen_bc = 100000;
    im->stk_per_rev = stk_sysclk(im->tracklen_bc * im->write_bc_ticks);

    im->cur_bc = (sys_ticks * 16) / im->ticks_per_cell;
    im->cur_bc &= ~15;
    if (im->cur_bc >= im->tracklen_bc)
        im->cur_bc = 0;
    im->cur_ticks = im->cur_bc * im->ticks_per_cell;
    im->ticks_since_flux = 0;

    bc->prod = bc->cons = 0;
}

static bool_t dummy_read_track(struct image *im)
{
    struct image_buf *bc = &im->bufs.read_bc;
    uint16_t *bc_b = bc->p;
    uint32_t bc_len, bc_mask, bc_space, bc_p, bc_c;

    /* Generate some MFM if there is space in the raw-bitcell ring buffer. */
    bc_p = bc->prod / 16; /* MFM words */
    bc_c = bc->cons / 16; /* MFM words */
    bc_len = bc->len / 2; /* MFM words */
    bc_mask = bc_len - 1;
    bc_space = bc_len - (uint16_t)(bc_p - bc_c);
    if (bc_space == 0)
        return FALSE;

    while (bc_space--)
        bc_b[bc_p++ & bc_mask] = 0xaaaa;

    bc->prod = bc_p * 16;

    return TRUE;
}

const struct image_handler dummy_image_handler = {
    .open = dummy_open,
    .setup_track = dummy_setup_track,
    .read_track = dummy_read_track,
    .rdata_flux = bc_rdata_flux,
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

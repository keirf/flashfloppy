/*
 * vgi.c
 * 
 * Micropolis/Vector Graphic Inc (VGI) files.
 * 
 * Written & released by Keir Fraser <keir.xen@gmail.com> and
 * Eric Anderson <ejona86@gmail.com>
 * 
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

#define SECTORS 16
#define SECTOR_SIZE 275
#define DD_TRACKLEN_BC 100000

#define MAX_WR_BATCH SECTORS

static bool_t vgi_open(struct image *im)
{
    FSIZE_t fsize = f_size(&im->fp);
    if (fsize == SECTORS*SECTOR_SIZE*45) {
        im->nr_cyls = 45; /* MOD I. 48 TPI */
        im->nr_sides = 1;
    } else if (fsize == SECTORS*SECTOR_SIZE*45*2) {
        im->nr_cyls = 45;
        im->nr_sides = 2;
    } else if (fsize == SECTORS*SECTOR_SIZE*77) {
        im->nr_cyls = 77; /* MOD II. 100 TPI */
        im->nr_sides = 1;
    } else if (fsize == SECTORS*SECTOR_SIZE*77*2) {
        im->nr_cyls = 77;
        im->nr_sides = 2;
    } else {
        return FALSE;
    }

    im->tracklen_bc = DD_TRACKLEN_BC;
    im->ticks_per_cell = (sysclk_stk(im->stk_per_rev) * 16u) / im->tracklen_bc;
    im->nr_hardsecs = 16;

    volume_cache_init(im->bufs.write_data.p + MAX_WR_BATCH * SECTOR_SIZE,
                      im->bufs.write_data.p + im->bufs.write_data.len);

    return TRUE;
}

static void vgi_setup_track(
    struct image *im, uint16_t track, uint32_t *start_pos)
{
    struct image_buf *rd = &im->bufs.read_data;
    struct image_buf *bc = &im->bufs.read_bc;
    uint32_t decode_off, sys_ticks = start_pos ? *start_pos : 0;

    im->vgi.trk_off = (track >> 1) * SECTORS * SECTOR_SIZE
                    + (track &  1) * SECTORS * SECTOR_SIZE * im->nr_cyls;
    im->cur_track = track;

    im->cur_bc = (sys_ticks * 16) / im->ticks_per_cell;
    if (im->cur_bc >= im->tracklen_bc)
        im->cur_bc = 0;
    im->cur_ticks = im->cur_bc * im->ticks_per_cell;
    im->ticks_since_flux = 0;

    im->vgi.sec_idx = im->cur_bc / (DD_TRACKLEN_BC/SECTORS);
    decode_off      = im->cur_bc % (DD_TRACKLEN_BC/SECTORS);
    if (im->vgi.sec_idx >= SECTORS)
        im->vgi.sec_idx = 0;

    rd->prod = rd->cons = 0;
    bc->prod = bc->cons = 0;
    im->vgi.err_cum_bc = 0;

    if (start_pos) {
        image_read_track(im);
        bc->cons = decode_off;
    }
}

static bool_t vgi_read_track(struct image *im)
{
    struct image_buf *rd = &im->bufs.read_data;
    struct image_buf *bc = &im->bufs.read_bc;
    uint8_t *buf = rd->p;
    uint16_t *bc_b = bc->p;
    uint32_t bc_len, bc_mask, bc_space, bc_p, bc_c;
    uint16_t pr;
    unsigned int i;

    if (rd->prod == rd->cons) {
        F_lseek(&im->fp, im->vgi.trk_off + im->vgi.sec_idx * SECTOR_SIZE);
        F_read(&im->fp, buf, SECTOR_SIZE, NULL);
        rd->prod++;
        im->vgi.sec_idx++;
        if (im->vgi.sec_idx >= SECTORS)
            im->vgi.sec_idx = 0;
    }

    /* Generate some MFM if there is space in the raw-bitcell ring buffer. */
    bc_p = bc->prod / 16; /* MFM words */
    bc_c = bc->cons / 16; /* MFM words */
    bc_len = bc->len / 2; /* MFM words */
    bc_mask = bc_len - 1;
    bc_space = bc_len - (uint16_t)(bc_p - bc_c);

    pr = be16toh(bc_b[(bc_p-1) & bc_mask]);
#define emit_raw(r) ({                                   \
    uint16_t _r = (r);                                   \
    bc_b[bc_p++ & bc_mask] = htobe16(_r & ~(pr << 15));  \
    pr = _r; })
#define emit_byte(b) emit_raw(mfmtab[(uint8_t)(b)])

    if (bc_space < 40 + SECTOR_SIZE + 40 + 35 + 1)
        return FALSE;

    /* sector preamble */
    for (i = 0; i < 40; i++)
        emit_byte(0);
    /* sync + sector header + sector data */
    for (i = 0; i < SECTOR_SIZE; i++)
        emit_byte(buf[i]);
    /* sector postamble */
    for (i = 0; i < 40; i++)
        emit_byte(0);
    /* filler */
    for (i = 0; i < 35; i++)
        emit_byte(0);
    /* Each sector needs 10 more bitcells. Just average it out to keep
     * convenient alignment in read_bc. */
    im->vgi.err_cum_bc -= 10;
    if (im->vgi.err_cum_bc < 0) {
        emit_byte(0);
        im->vgi.err_cum_bc += 16;
    }
    rd->cons++;

    bc->prod = bc_p * 16;

    return TRUE;
}

static bool_t vgi_write_track(struct image *im)
{
    bool_t flush = (im->wr_cons != im->wr_bc);
    return flush;
}

const struct image_handler vgi_image_handler = {
    .open = vgi_open,
    .setup_track = vgi_setup_track,
    .read_track = vgi_read_track,
    .rdata_flux = bc_rdata_flux,
    .write_track = vgi_write_track,
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

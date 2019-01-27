/*
 * image.c
 * 
 * Interface for accessing image files.
 * 
 * Written & released by Keir Fraser <keir.xen@gmail.com>
 * 
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

extern const struct image_handler adf_image_handler;
extern const struct image_handler hfe_image_handler;
extern const struct image_handler img_image_handler;
extern const struct image_handler st_image_handler;
extern const struct image_handler d81_image_handler;
extern const struct image_handler dsk_image_handler;
extern const struct image_handler da_image_handler;
extern const struct image_handler adfs_image_handler;
extern const struct image_handler mbd_image_handler;
extern const struct image_handler mgt_image_handler;
extern const struct image_handler pc98fdi_image_handler;
extern const struct image_handler pc98hdm_image_handler;
extern const struct image_handler trd_image_handler;
extern const struct image_handler opd_image_handler;
extern const struct image_handler ssd_image_handler;
extern const struct image_handler dsd_image_handler;
extern const struct image_handler sdu_image_handler;
extern const struct image_handler jvc_image_handler;
extern const struct image_handler vdk_image_handler;
extern const struct image_handler ti99_image_handler;
extern const struct image_handler dummy_image_handler;

const struct image_type image_type[] = {
    { "adf", &adf_image_handler },
    { "d81", &d81_image_handler },
    { "dsk", &dsk_image_handler },
    { "hdm", &pc98hdm_image_handler },
    { "hfe", &hfe_image_handler },
    { "img", &img_image_handler },
    { "ima", &img_image_handler },
    { "st",  &st_image_handler },
    { "adl", &adfs_image_handler },
    { "adm", &adfs_image_handler },
    { "mbd", &mbd_image_handler },
    { "mgt", &mgt_image_handler },
    { "fdi", &pc98fdi_image_handler },
    { "trd", &trd_image_handler },
    { "opd", &opd_image_handler },
    { "ssd", &ssd_image_handler },
    { "dsd", &dsd_image_handler },
    { "sdu", &sdu_image_handler },
    { "jvc", &jvc_image_handler },
    { "vdk", &vdk_image_handler },
    { "v9t9", &ti99_image_handler },
    { "", NULL }
};

bool_t image_valid(FILINFO *fp)
{
    char ext[8];

    /* Skip directories. */
    if (fp->fattrib & AM_DIR)
        return FALSE;

    /* Skip empty images. */
    if (fp->fsize == 0)
        return FALSE;

    /* Check valid extension. */
    filename_extension(fp->fname, ext, sizeof(ext));
    if (!strcmp(ext, "adf")) {
        return (ff_cfg.host == HOST_acorn) || !(fp->fsize % (2*11*512));
    } else {
        const struct image_type *type;
        for (type = &image_type[0]; type->handler != NULL; type++)
            if (!strcmp(ext, type->ext))
                return TRUE;
    }

    return FALSE;
}

static bool_t try_handler(struct image *im, const struct slot *slot,
                          const struct image_handler *handler)
{
    struct image_bufs bufs = im->bufs;
    BYTE mode;

    /* Reinitialise image structure, except for static buffers. */
    memset(im, 0, sizeof(*im));
    im->bufs = bufs;
    im->cur_track = ~0;
    im->slot = slot;

    /* Sensible defaults. */
    im->sync = SYNC_mfm;
    im->write_bc_ticks = sysclk_us(2);
    im->stk_per_rev = stk_ms(200);

    im->handler = handler;

    mode = FA_READ | FA_OPEN_EXISTING;
    if (handler->write_track != NULL)
        mode |= FA_WRITE;
    fatfs_from_slot(&im->fp, slot, mode);

    return handler->open(im);
}

void image_open(struct image *im, const struct slot *slot)
{
    static const struct image_handler * const image_handlers[] = {
        /* Special handler for dummy slots (empty HxC slot 0). */
        &dummy_image_handler,
        /* Formats with an identifying header. */
        &dsk_image_handler,
        &hfe_image_handler,
        /* Header-less formats in some semblance of priority order. */
        &adf_image_handler,
        &img_image_handler
    };

    char ext[sizeof(slot->type)+1];
    const struct image_handler *hint;
    const struct image_type *type;
    int i;

    /* Extract filename extension (if available). */
    memcpy(ext, slot->type, sizeof(slot->type));
    ext[sizeof(slot->type)] = '\0';

    /* Use the extension as a hint to the correct image handler. */
    for (type = &image_type[0]; type->handler != NULL; type++)
        if (!strcmp(ext, type->ext))
            break;
    hint = type->handler;

    /* Apply host-specific overrides to the hint. */
    switch (ff_cfg.host) {
    case HOST_acorn:
        if (hint == &adf_image_handler)
            hint = &adfs_image_handler;
        break;
    case HOST_tandy_coco:
        if (hint == &dsk_image_handler)
            hint = &jvc_image_handler;
        break;
    }

    if (hint) {
        if (try_handler(im, slot, hint))
            return;
        /* Hint failed. Try a secondary hint (allows DSK fallback to IMG). */
        hint = !strcmp(ext, "dsk") ? &img_image_handler : NULL;
        if (hint && try_handler(im, slot, hint))
            return;
    }

    /* Filename extension hinting failed: walk the handler list. */
    for (i = 0; i < ARRAY_SIZE(image_handlers); i++) {
        if (try_handler(im, slot, image_handlers[i]))
            return;
    }

    /* No handler found: bad image. */
    F_die(FR_BAD_IMAGE);
}

void image_extend(struct image *im)
{
    if (im->handler->extend && im->fp.dir_ptr && ff_cfg.extend_image)
        im->handler->extend(im);
}

bool_t image_setup_track(
    struct image *im, uint16_t track, uint32_t *start_pos)
{
    if (track < (DA_FIRST_CYL*2)) {
        /* If we are exiting D-A mode then need to re-read the config file. */
        if (im->handler == &da_image_handler)
            return TRUE;
    } else {
        im->handler = &da_image_handler;
    }

    im->handler->setup_track(im, track, start_pos);

    return FALSE;
}

bool_t image_read_track(struct image *im)
{
    return im->handler->read_track(im);
}

uint16_t bc_rdata_flux(struct image *im, uint16_t *tbuf, uint16_t nr)
{
    uint32_t ticks_per_cell = im->ticks_per_cell;
    uint32_t ticks = im->ticks_since_flux;
    uint32_t x, y = 32, todo = nr;
    struct image_buf *bc = &im->bufs.read_bc;
    uint32_t *bc_b = bc->p, bc_c = bc->cons, bc_p = bc->prod & ~31;
    unsigned int bc_mask = (bc->len / 4) - 1;

    /* Convert pre-generated bitcells into flux timings. */
    while (bc_c != bc_p) {
        y = bc_c % 32;
        x = be32toh(bc_b[(bc_c / 32) & bc_mask]) << y;
        bc_c += 32 - y;
        im->cur_bc += 32 - y;
        im->cur_ticks += (32 - y) * ticks_per_cell;
        while (y < 32) {
            y++;
            ticks += ticks_per_cell;
            if ((int32_t)x < 0) {
                *tbuf++ = (ticks >> 4) - 1;
                ticks &= 15;
                if (!--todo)
                    goto out;
            }
            x <<= 1;
        }
    }

    ASSERT(y == 32);

out:
    bc->cons = bc_c - (32 - y);
    im->cur_bc -= 32 - y;
    im->cur_ticks -= (32 - y) * ticks_per_cell;
    im->ticks_since_flux = ticks;

    if (im->cur_bc >= im->tracklen_bc) {
        im->cur_bc -= im->tracklen_bc;
        ASSERT(im->cur_bc < im->tracklen_bc);
        im->tracklen_ticks = im->cur_ticks - im->cur_bc * ticks_per_cell;
        im->cur_ticks -= im->tracklen_ticks;
    }

    return nr - todo;
}

uint16_t image_rdata_flux(struct image *im, uint16_t *tbuf, uint16_t nr)
{
    return im->handler->rdata_flux(im, tbuf, nr);
}

bool_t image_write_track(struct image *im)
{
    return im->handler->write_track(im);
}

uint32_t image_ticks_since_index(struct image *im)
{
    uint32_t ticks = im->cur_ticks - im->ticks_since_flux;
    if ((int32_t)ticks < 0)
        ticks += im->tracklen_ticks;
    return ticks >> 4;
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

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
extern const struct image_handler dsk_image_handler;
extern const struct image_handler da_image_handler;
extern const struct image_handler adl_image_handler;

bool_t image_valid(FILINFO *fp)
{
    char ext[8];

    /* Skip directories. */
    if (fp->fattrib & AM_DIR)
        return FALSE;

    /* Check valid extension. */
    filename_extension(fp->fname, ext, sizeof(ext));
    if (!strcmp(ext, "adf")) {
        return !(fp->fsize % (11*512));
    } else if (!strcmp(ext, "dsk")
               || !strcmp(ext, "hfe")
               || !strcmp(ext, "img")
               || !strcmp(ext, "ima")
               || !strcmp(ext, "st")
               || !strcmp(ext, "adl")
               || !strcmp(ext, "adm")) {
        return TRUE;
    }

    return FALSE;
}

static bool_t try_handler(struct image *im, const struct slot *slot,
                          const struct image_handler *handler)
{
    BYTE mode;

    im->handler = im->_handler = handler;

    mode = FA_READ | FA_OPEN_EXISTING;
    if (handler->write_track != NULL)
        mode |= FA_WRITE;
    fatfs_from_slot(&im->fp, slot, mode);
    
    return handler->open(im);
}

void image_open(struct image *im, const struct slot *slot)
{
    static const struct image_handler * const image_handlers[] = {
        /* Formats with an identifying header. */
        &dsk_image_handler,
        &hfe_image_handler,
        /* Header-less formats in some semblance of priority order. */
        &adf_image_handler,
        &img_image_handler
    };

    char ext[sizeof(slot->type)+1];
    struct image_bufs bufs = im->bufs;
    const struct image_handler *hint;
    int i;

    /* Reinitialise image structure, except for static buffers. */
    memset(im, 0, sizeof(*im));
    im->bufs = bufs;
    im->write_bc_ticks = sysclk_us(2);
    im->stk_per_rev = stk_ms(200);

    /* Extract filename extension (if available). */
    memcpy(ext, slot->type, sizeof(slot->type));
    ext[sizeof(slot->type)] = '\0';

    /* Use the extension as a hint to the correct image handler. */
    
    hint = (!strcmp(ext, "adf") ? &adf_image_handler
            : !strcmp(ext, "dsk") ? &dsk_image_handler
            : !strcmp(ext, "hfe") ? &hfe_image_handler
            : !strcmp(ext, "img") ? &img_image_handler
            : !strcmp(ext, "ima") ? &img_image_handler
            : !strcmp(ext, "st") ? &st_image_handler
            : !strcmp(ext, "adl") ? &adl_image_handler
            : !strcmp(ext, "adm") ? &adl_image_handler
            : NULL);
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

bool_t image_seek_track(
    struct image *im, uint16_t track, stk_time_t *start_pos)
{
    /* If we are exiting D-A mode then we need to re-read the config file. */
    if ((im->handler == &da_image_handler) && (track < 510))
        return TRUE;

    /* If we are already seeked to this track and we are not interested in 
     * a particular rotational position (ie. we are writing) then we have 
     * nothing to do. */
    if ((start_pos == NULL) && (track == im->cur_track))
        return FALSE;

    /* Are we in special direct-access mode, or not? */
    im->handler = (track >= 510)
        ? &da_image_handler
        : im->_handler;

    im->handler->seek_track(im, track, start_pos);

    return FALSE;
}

bool_t image_read_track(struct image *im)
{
    return im->handler->read_track(im);
}

uint16_t mfm_rdata_flux(struct image *im, uint16_t *tbuf, uint16_t nr,
                        uint32_t ticks_per_cell)
{
    uint32_t ticks = im->ticks_since_flux;
    uint32_t x, y = 32, todo = nr;
    struct image_buf *mfm = &im->bufs.read_mfm;
    uint32_t *mfmb = mfm->p, mfmc = mfm->cons, mfmp = mfm->prod & ~31;

    /* Convert pre-generated MFM into flux timings. */
    while (mfmc != mfmp) {
        y = mfmc % 32;
        x = be32toh(mfmb[(mfmc/32)%(mfm->len/4)]) << y;
        mfmc += 32 - y;
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
    mfm->cons = mfmc - (32 - y);
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

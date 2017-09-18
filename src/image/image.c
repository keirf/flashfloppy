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
extern const struct image_handler da_image_handler;

bool_t image_valid(FILINFO *fp)
{
    char ext[8];

    /* Skip directories. */
    if (fp->fattrib & AM_DIR)
        return FALSE;

    /* Check valid extension. */
    filename_extension(fp->fname, ext, sizeof(ext));
    if (!strcmp(ext, "adf")) {
        return fp->fsize == 901120;
    } else if (!strcmp(ext, "hfe")
               || !strcmp(ext, "img")
               || !strcmp(ext, "st")) {
        return TRUE;
    }

    return FALSE;
}

static bool_t try_handler(struct image *im, const struct v2_slot *slot,
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

bool_t image_open(struct image *im, const struct v2_slot *slot)
{
    static const struct image_handler * const image_handlers[] = {
        &hfe_image_handler, &adf_image_handler
    };

    char ext[4];
    struct image_bufs bufs = im->bufs;
    const struct image_handler *hint;
    int i;

    /* Reinitialise image structure, except for static buffers. */
    memset(im, 0, sizeof(*im));
    im->bufs = bufs;
    im->write_bc_ticks = sysclk_us(2);

    /* Extract filename extension (if available). */
    memcpy(ext, slot->type, 3);
    ext[3] = '\0';

    /* Use the extension as a hint to the correct image handler. */
    hint = (!strcmp(ext, "adf") ? &adf_image_handler
            : !strcmp(ext, "hfe") ? &hfe_image_handler
            : !strcmp(ext, "img") ? &img_image_handler
            : !strcmp(ext, "st") ? &st_image_handler
            : NULL);
    if (hint && try_handler(im, slot, hint))
        return TRUE;

    /* Filename extension hinting failed: walk the handler list. */
    for (i = 0; i < ARRAY_SIZE(image_handlers); i++) {
        if (try_handler(im, slot, image_handlers[i]))
            return TRUE;
    }

    return FALSE;
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

    return im->handler->seek_track(im, track, start_pos);
}

bool_t image_read_track(struct image *im)
{
    return im->handler->read_track(im);
}

uint16_t image_rdata_flux(struct image *im, uint16_t *tbuf, uint16_t nr)
{
    return im->handler->rdata_flux(im, tbuf, nr);
}

void image_write_track(struct image *im, bool_t flush)
{
    im->handler->write_track(im, flush);
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

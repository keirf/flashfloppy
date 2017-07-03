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
extern const struct image_handler scp_image_handler;

bool_t image_open(struct image *im, const char *name)
{
    char suffix[8];
    struct image_bufs bufs = im->bufs;
    BYTE mode;

    /* Reinitialise image structure, except for static buffers. */
    memset(im, 0, sizeof(*im));
    im->bufs = bufs;

    filename_extension(name, suffix, sizeof(suffix));
    if (!strcmp(suffix, "adf"))
        im->handler = &adf_image_handler;
    else if (!strcmp(suffix, "hfe"))
        im->handler = &hfe_image_handler;
    else if (!strcmp(suffix, "scp"))
        im->handler = &scp_image_handler;
    else
        return FALSE;

    mode = FA_READ | FA_OPEN_EXISTING;
    if (im->handler->write_track != NULL)
        mode |= FA_WRITE;
    F_open(&im->fp, name, mode);
    
    return im->handler->open(im);
}

bool_t image_seek_track(
    struct image *im, uint8_t track, stk_time_t *start_pos)
{
    /* If we are already seeked to this track and we are not interested in 
     * a particular rotational position (ie. we are writing) then we have 
     * nothing to do. */
    if ((start_pos == NULL) && (track == im->cur_track))
        return TRUE;

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

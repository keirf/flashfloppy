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

extern struct image_handler adf_image_handler;
extern struct image_handler hfe_image_handler;

bool_t image_open(struct image *im, const char *name)
{
    FRESULT fr;
    char suffix[8];

    filename_extension(name, suffix, sizeof(suffix));
    if (!strcmp(suffix, "adf"))
        im->handler = &adf_image_handler;
    else if (!strcmp(suffix, "hfe"))
        im->handler = &hfe_image_handler;
    else
        return FALSE;

    fr = f_open(&im->fp, name, FA_READ);
    if (fr)
        return FALSE;
    
    return im->handler->open(im);
}

bool_t image_seek_track(struct image *im, uint8_t track)
{
    return im->handler->seek_track(im, track);
}

void image_prefetch_data(struct image *im)
{
    return im->handler->prefetch_data(im);
}

uint16_t image_load_flux(struct image *im, uint16_t *tbuf, uint16_t nr)
{
    return im->handler->load_flux(im, tbuf, nr);
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

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

#if !defined(QUICKDISK)

extern const struct image_handler adf_image_handler;
extern const struct image_handler atr_image_handler;
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
extern const struct image_handler xdf_image_handler;
extern const struct image_handler dummy_image_handler;

const struct image_type image_type[] = {
    { "adf", &adf_image_handler },
    { "atr", &atr_image_handler },
    { "d81", &d81_image_handler },
    { "dsk", &dsk_image_handler },
    { "hdm", &pc98hdm_image_handler },
    { "hfe", &hfe_image_handler },
    { "img", &img_image_handler },
    { "ima", &img_image_handler },
    { "out", &img_image_handler },
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
    { "xdf", &xdf_image_handler },
    { "", NULL }
};

#else /* defined(QUICKDISK) */

extern const struct image_handler qd_image_handler;

const struct image_type image_type[] = {
    { "qd", &qd_image_handler },
    { "", NULL }
};

#endif /* QUICKDISK */


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
    if (!strcmp(ext, "adf") && !is_quickdisk) {
        return (ff_cfg.host == HOST_acorn) || !(fp->fsize % (2*11*512));
    } else {
        const struct image_type *type;
        for (type = &image_type[0]; type->handler != NULL; type++)
            if (!strcmp(ext, type->ext))
                return TRUE;
    }

    return FALSE;
}

static bool_t try_handler(struct image *im, struct slot *slot,
                          DWORD *cltbl,
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
    im->fp.cltbl = cltbl;

    return handler->open(im);
}

#if !defined(QUICKDISK)

void image_open(struct image *im, struct slot *slot, DWORD *cltbl)
{
    static const struct image_handler * const image_handlers[] = {
        /* Special handler for dummy slots (empty HxC slot 0). */
        &dummy_image_handler,
        /* Only put formats here that have a strong identifying header. */
        &dsk_image_handler,
        &hfe_image_handler
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

    while (hint != NULL) {
        if (try_handler(im, slot, cltbl, hint))
            return;
        /* Hint failed. Try a secondary hint. */
        if (hint == &img_image_handler)
            /* IMG,IMA,DSK,OUT -> XDF */
            hint = &xdf_image_handler;
        else if (!strcmp(ext, "dsk")) 
            /* DSK -> IMG */
            hint = &img_image_handler;
        else
            hint = NULL;
    }

    /* Filename extension hinting failed: walk the handler list. */
    for (i = 0; i < ARRAY_SIZE(image_handlers); i++) {
        if (try_handler(im, slot, cltbl, image_handlers[i]))
            return;
    }

    /* No handler found: bad image. */
    F_die(FR_BAD_IMAGE);
}

#else /* defined(QUICKDISK) */

void image_open(struct image *im, struct slot *slot, DWORD *cltbl)
{
    if (try_handler(im, slot, cltbl, &qd_image_handler))
        return;

    /* No handler found: bad image. */
    F_die(FR_BAD_IMAGE);
}

#endif

void image_extend(struct image *im)
{
    FSIZE_t new_sz;

    if (!(im->handler->extend && im->fp.dir_ptr && ff_cfg.extend_image))
        return;

    new_sz = im->handler->extend(im);
    if (f_size(&im->fp) >= new_sz)
        return;

    /* Disable fast-seek mode, as it disallows extending the file. */
    im->fp.cltbl = NULL;

    /* Attempt to extend the file. */
    F_lseek(&im->fp, new_sz);
    F_sync(&im->fp);
    if (f_tell(&im->fp) != new_sz)
        F_die(FR_DISK_FULL);

    /* Update the slot for the new file size. */
    im->slot->size = new_sz;
}

static void print_image_info(struct image *im)
{
    char msg[25];
    const static char *sync_s[] = { "Raw", "FM", "MFM" };
    const static char dens_c[] = { 'S', 'D', 'H', 'E' };
    int tlen, i;

    i = 0;
    for (tlen = 75000; tlen < im->tracklen_bc; tlen *= 2) {
        if (i == (sizeof(dens_c)-1))
            break;
        i++;
    }
    snprintf(msg, sizeof(msg), "%s %cS/%cD %uT",
             sync_s[im->sync],
             (im->nr_sides == 1) ? 'S' : 'D',
             dens_c[i], im->nr_cyls);
    lcd_write(0, 2, -1, msg);
}

bool_t image_setup_track(
    struct image *im, uint16_t track, uint32_t *start_pos)
{
#if !defined(QUICKDISK)
    if (track < (DA_FIRST_CYL*2)) {
        /* If we are exiting D-A mode then need to re-read the config file. */
        if (im->handler == &da_image_handler)
            return TRUE;
    } else {
        im->handler = &da_image_handler;
    }
#endif

    im->handler->setup_track(im, track, start_pos);

    print_image_info(im);

    return FALSE;
}

bool_t image_read_track(struct image *im)
{
    return im->handler->read_track(im);
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
    if (!is_quickdisk)
        ticks >>= 4;
    return ticks;
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

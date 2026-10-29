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

#if TARGET == TARGET_shugart

image_open_fn adf_open;
image_open_fn atr_open;
image_open_fn hfe_open;
image_open_fn img_open;
image_open_fn sf7_open;
image_open_fn st_open;
image_open_fn d81_open;
image_open_fn dsk_open;
image_open_fn adfs_open;
image_open_fn mbd_open;
image_open_fn mgt_open;
image_open_fn pc98fdi_open;
image_open_fn pc98hdm_open;
image_open_fn trd_open;
image_open_fn opd_open;
image_open_fn ssd_open;
image_open_fn dsd_open;
image_open_fn sdu_open;
image_open_fn jvc_open;
image_open_fn vdk_open;
image_open_fn ti99_open;
image_open_fn xdf_open;
image_open_fn dummy_open;
/* The track-specific handlers that image_setup_track() switches to. */
extern const struct image_handler da_image_handler;
extern const struct image_handler dummy_image_handler;

const struct image_type image_type[] = {
    { "adf", adf_open },
    { "atr", atr_open },
    { "d1m", d81_open }, /* CMD FD-2000 DD, 81 track */
    { "d2m", d81_open }, /* CMD FD-2000 HD, 81 track */
    { "d4m", d81_open }, /* CMD FD-4000 ED, 81 track */
    { "d81", d81_open },
    { "dsk", dsk_open },
    { "hdm", pc98hdm_open },
    { "hfe", hfe_open },
    { "img", img_open },
    { "ima", img_open },
    { "out", img_open },
    { "sf7", sf7_open },
    { "st",  st_open },
    { "adl", adfs_open },
    { "adm", adfs_open },
    { "mbd", mbd_open },
    { "mgt", mgt_open },
    { "fdi", pc98fdi_open },
    { "trd", trd_open },
    { "opd", opd_open },
    { "ssd", ssd_open },
    { "dsd", dsd_open },
    { "sdu", sdu_open },
    { "jvc", jvc_open },
    { "vdk", vdk_open },
    { "v9t9", ti99_open },
    { "xdf", xdf_open },
    { "", NULL }
};

#elif TARGET == TARGET_apple2

image_open_fn hfe_open;
image_open_fn nib_open;
image_open_fn nic_open;
image_open_fn a2dsk_open;

const struct image_type image_type[] = {
    { "hfe", hfe_open },
    { "nib", nib_open },
    { "nic", nic_open },
    { "dsk", a2dsk_open },
    { "do", a2dsk_open },
    { "po", a2dsk_open },
    { "", NULL }
};

#elif TARGET == TARGET_quickdisk

image_open_fn qd_open;

const struct image_type image_type[] = {
    { "qd", qd_open },
    { "", NULL }
};

#endif

#if TARGET == TARGET_apple2

static unsigned int image_nr_hidden;

void image_log_hidden(void)
{
    if (image_nr_hidden != 0)
        printk("%u image(s) hidden by size\n", image_nr_hidden);
    image_nr_hidden = 0;
}

static bool_t image_is_hidden(image_open_fn *open, uint32_t size)
{
    bool_t ok = TRUE;
    if (open == hfe_open) {
        /* An Apple2 disk has at most 40 tracks on one side, which an HFE holds
         * in well under this; a larger one is a disk of another kind. */
        ok = size <= 1280*1024;
    } else if (open == a2dsk_open) {
        /* Can this image hold the one side of 35 to 40 tracks an Apple2 disk
         * has? A sector image has 16 sectors of 256 bytes per track; one of
         * the Agat may end in a 128-byte trailer naming the disk. */
        ok = !((size % 4096) & ~128u)
            && (size / 4096 >= 35) && (size / 4096 <= 40);
    }
    if (!ok)
        image_nr_hidden++;
    return !ok;
}

#else

#define image_is_hidden(open, size) FALSE

#endif

bool_t image_type_is(const struct image_type *type, const char *ext)
{
    return !strncmp(ext, type->ext, sizeof(type->ext))
        && (strlen(ext) <= sizeof(type->ext));
}

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
    if ((TARGET == TARGET_shugart) && !strcmp(ext, "adf")) {
        return (ff_cfg.host == HOST_acorn) || !(fp->fsize % (2*11*512));
    } else {
        const struct image_type *type;
        for (type = &image_type[0]; type->open != NULL; type++)
            if (image_type_is(type, ext))
                return !image_is_hidden(type->open, fp->fsize);
    }

    return FALSE;
}

static bool_t try_handler(struct image *im, struct slot *slot,
                          DWORD *cltbl,
                          image_open_fn *open)
{
    struct image_bufs bufs = im->bufs;
    const struct image_handler *handler;

    /* Reinitialise image structure, except for static buffers. */
    memset(im, 0, sizeof(*im));
    im->bufs = bufs;
    im->cur_track = ~0;
    im->slot = slot;

    /* Sensible defaults. */
    im->sync = SYNC_mfm;
    im->write_bc_ticks = sampleclk_us(2);
    im->stk_per_rev = stk_ms(200);

    /* All handlers support write, so always open RW. */
    fatfs_from_slot(&im->fp, slot, FA_READ | FA_WRITE | FA_OPEN_EXISTING);
    im->fp.cltbl = cltbl;

    handler = open(im);
    im->disk_handler = im->track_handler = handler;
    return handler != NULL;
}

#if TARGET == TARGET_shugart

void image_open(struct image *im, struct slot *slot, DWORD *cltbl)
{
    static image_open_fn * const image_handlers[] = {
        /* Special handler for dummy slots (empty HxC slot 0). */
        dummy_open,
        /* Only put formats here that have a strong identifying header. */
        dsk_open,
        hfe_open
    };

    char ext[sizeof(slot->type)+1];
    image_open_fn *hint;
    const struct image_type *type;
    int i;

    /* Extract filename extension (if available). */
    memcpy(ext, slot->type, sizeof(slot->type));
    ext[sizeof(slot->type)] = '\0';

    /* Use the extension as a hint to the correct image handler. */
    for (type = &image_type[0]; type->open != NULL; type++)
        if (image_type_is(type, ext))
            break;
    hint = type->open;

    /* Apply host-specific overrides to the hint. */
    switch (ff_cfg.host) {
    case HOST_acorn:
        if (hint == adf_open)
            hint = adfs_open;
        break;
    case HOST_tandy_coco:
        if (hint == dsk_open)
            hint = jvc_open;
        break;
    }

    while (hint != NULL) {
        if (try_handler(im, slot, cltbl, hint))
            return;
        /* Hint failed. Try a secondary hint. */
        if (hint == img_open) {
            /* IMG,IMA,DSK,OUT -> XDF */
            hint = xdf_open;
        } else if (!strcmp(ext, "dsk")) {
            /* DSK -> IMG */
            hint = img_open;
            ext[0] = '\0';
        } else {
            hint = NULL;
        }
    }

    /* Filename extension hinting failed: walk the handler list. */
    for (i = 0; i < ARRAY_SIZE(image_handlers); i++) {
        if (try_handler(im, slot, cltbl, image_handlers[i]))
            return;
    }

    /* No handler found: bad image. */
    F_die(FR_BAD_IMAGE);
}

#elif TARGET == TARGET_apple2

void image_open(struct image *im, struct slot *slot, DWORD *cltbl)
{
    char ext[sizeof(slot->type)+1];
    const struct image_type *type;

    /* Extract filename extension (if available). */
    memcpy(ext, slot->type, sizeof(slot->type));
    ext[sizeof(slot->type)] = '\0';

    /* Apple2 image types are distinguished by extension alone. */
    for (type = &image_type[0]; type->open != NULL; type++)
        if (image_type_is(type, ext)
            && try_handler(im, slot, cltbl, type->open))
            return;

    /* No handler found: bad image. */
    F_die(FR_BAD_IMAGE);
}

#elif TARGET == TARGET_quickdisk

void image_open(struct image *im, struct slot *slot, DWORD *cltbl)
{
    if (try_handler(im, slot, cltbl, qd_open))
        return;

    /* No handler found: bad image. */
    F_die(FR_BAD_IMAGE);
}

#endif

void image_extend(struct image *im)
{
    FSIZE_t new_sz;

    if (!(im->disk_handler->extend && im->fp.dir_ptr && ff_cfg.extend_image))
        return;

    new_sz = im->disk_handler->extend(im);
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
    const char *sync = sync_s[im->sync];
    char sides = (im->nr_sides == 1) ? 'S' : 'D';
    int tlen, i;

    i = 0;
    for (tlen = 75000; tlen < im->tracklen_bc; tlen *= 2) {
        if (i == (sizeof(dens_c)-1))
            break;
        i++;
    }
    snprintf(msg, sizeof(msg), "%s %cS/%cD %uT",
             sync, sides, dens_c[i], im->nr_cyls);
    lcd_write(0, 2, -1, msg);

    /* In the row's layout of half width, the encoding, then the sides, the
     * density and the cylinders at the row's right. */
    snprintf(msg, sizeof(msg), "%s%*s%c%c%u", sync,
             max_t(int, lcd_columns/2 - strlen(sync) - 2
                   - ((im->nr_cyls > 99) ? 3 : (im->nr_cyls > 9) ? 2 : 1), 1),
             "", sides, dens_c[i], im->nr_cyls);
    lcd_write(0, 2 | LCD_WIDE, -1, msg);
}

bool_t image_setup_track(
    struct image *im, uint16_t track, uint32_t *start_pos)
{
    const struct image_handler *h = im->track_handler;

#if TARGET == TARGET_shugart
    if (!in_da_mode(im, track>>1)) {
        /* If we are exiting D-A mode then need to re-read the config file. */
        if (h == &da_image_handler)
            return TRUE;
        h = ((track>>1) >= im_nphys_cyls(im)) ? &dummy_image_handler
             : im->disk_handler;
    } else {
        h = &da_image_handler;
        im->nr_sides = 1;
    }
#endif

    im->track_handler = h;
    h->setup_track(im, track, start_pos);

    print_image_info(im);

    return FALSE;
}

bool_t image_read_track(struct image *im)
{
    return im->track_handler->read_track(im);
}

uint16_t image_rdata_flux(struct image *im, uint16_t *tbuf, uint16_t nr)
{
    return im->track_handler->rdata_flux(im, tbuf, nr);
}

bool_t image_write_track(struct image *im)
{
    return im->track_handler->write_track(im);
}

uint32_t image_ticks_since_index(struct image *im)
{
    uint32_t ticks = im->cur_ticks - im->ticks_since_flux;
    if ((int32_t)ticks < 0)
        ticks += im->tracklen_ticks;
    if (TARGET != TARGET_quickdisk)
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

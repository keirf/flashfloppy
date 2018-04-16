/*
 * img.c
 * 
 * Sector image files for IBM/ISO track formats:
 *  IMG/IMA: IBM/PC and others
 *  ST:      Atari ST
 *  ADL/ADM: Acorn 8-bit ADFS
 *  OPD:     Spectrum Opus Discovery
 *  TRD:     Spectrum TR-DOS
 * 
 * Written & released by Keir Fraser <keir.xen@gmail.com>
 * 
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

static void img_setup_track(
    struct image *im, uint16_t track, uint32_t *start_pos);
static bool_t img_read_track(struct image *im);
static bool_t img_write_track(struct image *im);
static bool_t mfm_open(struct image *im);
static bool_t mfm_read_track(struct image *im);
static bool_t mfm_write_track(struct image *im);
static bool_t fm_open(struct image *im);
static bool_t fm_read_track(struct image *im);
static bool_t fm_write_track(struct image *im);

static bool_t ti99_open(struct image *im);

#define LAYOUT_interleaved 0
#define LAYOUT_sequential_reverse_side1 1

#define sec_sz(im) (128u << (im)->img.sec_no)

#define _C(cyls) ((cyls) / 40)
const static struct img_type {
    uint8_t nr_secs:6;
    uint8_t nr_sides:2;
    uint8_t gap3;
    uint8_t interleave:3;
    uint8_t no:3;
    uint8_t base:2;
    uint8_t skew:4;
    uint8_t cyls:4;
} img_type[] = {
    {  9, 1, 84, 1, 2, 1, 0, _C(80) },
    { 10, 1, 30, 1, 2, 1, 0, _C(80) },
    { 11, 1,  3, 2, 2, 1, 0, _C(80) },
    {  8, 2, 84, 1, 2, 1, 0, _C(80) },
    {  9, 2, 84, 1, 2, 1, 0, _C(80) },
    { 10, 2, 30, 1, 2, 1, 0, _C(80) },
    { 11, 2,  3, 2, 2, 1, 0, _C(80) },
    { 18, 2, 84, 1, 2, 1, 0, _C(80) },
    { 19, 2, 70, 1, 2, 1, 0, _C(80) },
    { 21, 2, 18, 1, 2, 1, 0, _C(80) },
    { 20, 2, 40, 1, 2, 1, 0, _C(80) },
    { 36, 2, 84, 1, 2, 1, 0, _C(80) },
    { 0 }
}, adfs_type[] = {
    {  5, 2, 116, 1, 3, 0, 1, _C(80) }, /* ADFS D/E: 5 * 1kB, 800k */
    { 10, 2, 116, 1, 3, 0, 2, _C(80) }, /* ADFS F: 10 * 1kB, 1600k */
    { 16, 2,  57, 1, 1, 0, 0, _C(80) }, /* ADFS L 640k */
    { 16, 1,  57, 1, 1, 0, 0, _C(80) }, /* ADFS M 320k */
    { 16, 1,  57, 1, 1, 0, 0, _C(40) }, /* ADFS S 160k */
    { 0 }
}, akai_type[] = {
    {  5, 2, 116, 1, 3, 1, 0, _C(80) }, /* Akai DD:  5 * 1kB sectors */
    { 10, 2, 116, 1, 3, 1, 0, _C(80) }, /* Akai HD: 10 * 1kB sectors */
    { 0 }
}, ensoniq_type[] = {
    { 10, 2, 30, 1, 2, 0, 0, _C(80) },  /* Ensoniq 800kB */
    { 20, 2, 40, 1, 2, 0, 0, _C(80) },  /* Ensoniq 1.6MB */
    { 0 }
}, memotech_type[] = {
    { 16, 2, 57, 3, 1, 1, 0, _C(40) }, /* Type 03 */
    { 16, 2, 57, 3, 1, 1, 0, _C(80) }, /* Type 07 */
};

static bool_t _img_open(struct image *im, bool_t has_iam,
                        const struct img_type *type)
{
    if (type != NULL) {

        unsigned int nr_cyls, cyl_sz;

        /* Walk the layout/type hints looking for a match on file size. */
        for (; type->nr_secs != 0; type++) {
            unsigned int min_cyls, max_cyls;
            switch (type->cyls) {
            case _C(40):
                min_cyls = 38;
                max_cyls = 42;
                break;
            case _C(80):
            default:
                min_cyls = 77;
                max_cyls = 85;
                break;
            }
            cyl_sz = type->nr_secs * (128 << type->no) * type->nr_sides;
            for (nr_cyls = min_cyls; nr_cyls <= max_cyls; nr_cyls++)
                if ((nr_cyls * cyl_sz) == f_size(&im->fp))
                    goto found;
        }

        return FALSE;

    found:
        im->nr_cyls = nr_cyls;
        im->nr_sides = type->nr_sides;
        im->img.sec_no = type->no;
        im->img.interleave = type->interleave;
        im->img.skew = type->skew;
        im->img.sec_base = type->base;
        im->img.nr_sectors = type->nr_secs;
        im->img.gap3 = type->gap3;

    }

    im->img.has_iam = has_iam;

    return mfm_open(im);
}

static bool_t adfs_open(struct image *im)
{
    im->img.skew_cyls_only = TRUE;
    return _img_open(im, TRUE, adfs_type);
}

static bool_t img_open(struct image *im)
{
    const struct img_type *type;

    switch (ff_cfg.host) {
    case HOST_akai:
    case HOST_gem:
        type = akai_type;
        break;
    case HOST_ensoniq:
        type = ensoniq_type;
        break;
    case HOST_memotech:
        type = memotech_type;
        break;
    case HOST_ti99:
        return ti99_open(im);
    default:
        type = img_type;
        break;
    }

    /* Try specified host-specific geometries, falling back to default list. */
    return _img_open(im, TRUE, type) || _img_open(im, TRUE, img_type);
}

static bool_t st_open(struct image *im)
{
    bool_t ok = _img_open(im, FALSE, img_type);
    if (ok && im->img.nr_sectors == 9) {
        /* TOS formats 720kB disks with skew. */
        im->img.skew = 2;
    }
    return ok;
}

static bool_t trd_open(struct image *im)
{
    uint8_t geometry;

    /* Interrogate TR-DOS geometry identifier. */
    F_lseek(&im->fp, 0x8e3);
    F_read(&im->fp, &geometry, 1, NULL);
    switch (geometry) {
    case 0x16:
        im->nr_cyls = 80;
        im->nr_sides = 2;
        break;
    case 0x17:
        im->nr_cyls = 40;
        im->nr_sides = 2;
        break;
    case 0x18:
        im->nr_cyls = 80;
        im->nr_sides = 1;
        break;
    case 0x19:
        im->nr_cyls = 40;
        im->nr_sides = 1;
        break;
    default:
        /* Guess geometry */
        if (f_size(&im->fp) <= 40*16*256) {
            im->nr_cyls = 40;
            im->nr_sides = 1;
        } else if (f_size(&im->fp) < 40*2*16*256) {
            im->nr_cyls = 40;
            im->nr_sides = 1;
        } else {
            im->nr_cyls = 80;
            im->nr_sides = 2;
        }
    }

    im->img.sec_no = 1; /* 256-byte */
    im->img.interleave = 1;
    im->img.skew = 0;
    im->img.sec_base = 1;
    im->img.nr_sectors = 16;
    im->img.gap3 = 57;

    return _img_open(im, TRUE, NULL);
}

static bool_t opd_open(struct image *im)
{
    switch (f_size(&im->fp)) {
    case 184320:
        im->nr_cyls = 40;
        im->nr_sides = 1;
        break;
    case 737280:
        im->nr_cyls = 80;
        im->nr_sides = 2;
        break;
    default:
        return FALSE;
    }

    im->img.sec_no = 1; /* 256-byte */
    im->img.interleave = 13;
    im->img.skew = 13;
    im->img.skew_cyls_only = TRUE;
    im->img.sec_base = 0;
    im->img.nr_sectors = 18;
    im->img.gap3 = 12;

    return _img_open(im, TRUE, NULL);
}

static bool_t ssd_open(struct image *im)
{
    im->nr_cyls = 80;
    im->nr_sides = 1;
    im->img.interleave = 1;
    im->img.sec_no = 1; /* 256-byte */
    im->img.sec_base = 0;
    im->img.nr_sectors = 10;
    im->img.gap3 = 21;

    return fm_open(im);
}

static bool_t dsd_open(struct image *im)
{
    im->nr_cyls = 80;
    im->nr_sides = 2;
    im->img.interleave = 1;
    im->img.sec_no = 1; /* 256-byte */
    im->img.sec_base = 0;
    im->img.nr_sectors = 10;
    im->img.gap3 = 21;

    return fm_open(im);
}

static bool_t ti99_open(struct image *im)
{
    struct vib {
        uint8_t name[10];
        uint16_t tot_secs;
        uint8_t secs_per_track;
        char id[3];
        uint8_t protection;
        uint8_t tracks_per_side;
        uint8_t sides;
        uint8_t density;
    } vib;
    bool_t have_vib;
    unsigned int fsize = f_size(&im->fp);

    /* Must be a multiple of 256 sectors. */
    if ((fsize % 256) != 0)
        return FALSE;
    fsize /= 256;

    /* Check for 3-sector footer containing a bad sector map. We ignore it. */
    if ((fsize % 10) == 3)
        fsize -= 3;

    /* Main image must be non-zero size. */
    if (fsize == 0)
        return FALSE;

    /* Check for Volume Information Block in sector 0. */
    F_read(&im->fp, &vib, sizeof(vib), NULL);
    have_vib = !strncmp(vib.id, "DSK", 3);

    im->img.has_iam = FALSE;
    im->img.interleave = 4;
    im->img.skew = 3;
    im->img.skew_cyls_only = TRUE;
    im->img.sec_no = 1;
    im->img.sec_base = 0;
    im->img.layout = LAYOUT_sequential_reverse_side1;

    if ((fsize % (40*9)) == 0) {

        /* 9/18/36 sectors-per-track formats. */
        switch (fsize / (40*9)) {
        case 1: /* SSSD */
            im->nr_cyls = 40;
            im->nr_sides = 1;
            im->img.nr_sectors = 9;
            im->img.gap3 = 44;
            return fm_open(im);
        case 2: /* DSSD (or SSDD) */
            if (have_vib && (vib.sides == 1)) {
                /* Disambiguated: This is SSDD. */
                im->nr_cyls = 40;
                im->nr_sides = 1;
                im->img.nr_sectors = 18;
                im->img.interleave = 5;
                im->img.gap3 = 24;
                return mfm_open(im);
            }
            /* Assume DSSD. */
            im->nr_cyls = 40;
            im->nr_sides = 2;
            im->img.nr_sectors = 9;
            im->img.gap3 = 44;
            return fm_open(im);
        case 4: /* DSDD (or DSSD80) */
            if (have_vib && (vib.tracks_per_side == 80)) {
                /* Disambiguated: This is DSSD80. */
                im->nr_cyls = 80;
                im->nr_sides = 2;
                im->img.nr_sectors = 9;
                im->img.gap3 = 44;
                return fm_open(im);
            }
            /* Assume DSDD. */
            im->nr_cyls = 40;
            im->nr_sides = 2;
            im->img.nr_sectors = 18;
            im->img.interleave = 5;
            im->img.gap3 = 24;
            return mfm_open(im);
        case 8: /* DSDD80 */
            im->nr_cyls = 80;
            im->nr_sides = 2;
            im->img.nr_sectors = 18;
            im->img.interleave = 5;
            im->img.gap3 = 24;
            return mfm_open(im);
        case 16: /* DSHD80 */
            im->nr_cyls = 80;
            im->nr_sides = 2;
            im->img.nr_sectors = 36;
            im->img.interleave = 5;
            im->img.gap3 = 24;
            return mfm_open(im);
        }

    } else if ((fsize % (40*16)) == 0) {

        /* SSDD/DSDD, 16 sectors */
        im->nr_sides = fsize / (40*16);
        if (im->nr_sides <= 2) {
            im->nr_cyls = 40;
            im->img.nr_sectors = 16;
            im->img.interleave = 5;
            im->img.gap3 = 44;
            return mfm_open(im);
        }

    }

    return FALSE;
}

const struct image_handler img_image_handler = {
    .open = img_open,
    .setup_track = img_setup_track,
    .read_track = img_read_track,
    .rdata_flux = bc_rdata_flux,
    .write_track = img_write_track,
};

const struct image_handler st_image_handler = {
    .open = st_open,
    .setup_track = img_setup_track,
    .read_track = img_read_track,
    .rdata_flux = bc_rdata_flux,
    .write_track = img_write_track,
};

const struct image_handler adfs_image_handler = {
    .open = adfs_open,
    .setup_track = img_setup_track,
    .read_track = img_read_track,
    .rdata_flux = bc_rdata_flux,
    .write_track = img_write_track,
};

const struct image_handler trd_image_handler = {
    .open = trd_open,
    .setup_track = img_setup_track,
    .read_track = img_read_track,
    .rdata_flux = bc_rdata_flux,
    .write_track = img_write_track,
};

const struct image_handler opd_image_handler = {
    .open = opd_open,
    .setup_track = img_setup_track,
    .read_track = img_read_track,
    .rdata_flux = bc_rdata_flux,
    .write_track = img_write_track,
};

const struct image_handler ssd_image_handler = {
    .open = ssd_open,
    .setup_track = img_setup_track,
    .read_track = img_read_track,
    .rdata_flux = bc_rdata_flux,
    .write_track = img_write_track,
};

const struct image_handler dsd_image_handler = {
    .open = dsd_open,
    .setup_track = img_setup_track,
    .read_track = img_read_track,
    .rdata_flux = bc_rdata_flux,
    .write_track = img_write_track,
};

const struct image_handler ti99_image_handler = {
    .open = ti99_open,
    .setup_track = img_setup_track,
    .read_track = img_read_track,
    .rdata_flux = bc_rdata_flux,
    .write_track = img_write_track,
};


/*
 * Generic Handlers
 */

static void img_seek_track(
    struct image *im, uint16_t track, unsigned int cyl, unsigned int side)
{
    uint32_t trk_len;
    unsigned int i, pos, trk = cyl * im->nr_sides + side;

    /* Create logical sector map in rotational order. */
    memset(im->img.sec_map, 0xff, im->img.nr_sectors);
    pos = ((im->img.skew_cyls_only ? cyl : trk) * im->img.skew)
        % im->img.nr_sectors;
    for (i = 0; i < im->img.nr_sectors; i++) {
        while (im->img.sec_map[pos] != 0xff)
            pos = (pos + 1) % im->img.nr_sectors;
        im->img.sec_map[pos] = i + im->img.sec_base;
        pos = (pos + im->img.interleave) % im->img.nr_sectors;
    }

    trk_len = im->img.nr_sectors * sec_sz(im);
    switch (im->img.layout) {
    case LAYOUT_sequential_reverse_side1:
        im->img.trk_off = (side ? im->nr_cyls - cyl : cyl) * trk_len;
        break;
    default:
        im->img.trk_off = trk * trk_len;
        break;
    }

    im->cur_track = track;
}

static void img_setup_track(
    struct image *im, uint16_t track, uint32_t *start_pos)
{
    struct image_buf *rd = &im->bufs.read_data;
    struct image_buf *bc = &im->bufs.read_bc;
    uint32_t decode_off, sys_ticks = start_pos ? *start_pos : 0;
    uint8_t cyl = track/2, side = track&1;

    /* TODO: Fake out unformatted tracks. */
    cyl = min_t(uint8_t, cyl, im->nr_cyls-1);
    side = min_t(uint8_t, side, im->nr_sides-1);
    track = cyl*2 + side;

    if (track != im->cur_track)
        img_seek_track(im, track, cyl, side);

    im->img.trk_sec = 0;
    im->img.write_sector = -1;

    im->cur_bc = (sys_ticks * 16) / im->ticks_per_cell;
    im->cur_bc &= ~15;
    if (im->cur_bc >= im->tracklen_bc)
        im->cur_bc = 0;
    im->cur_ticks = im->cur_bc * im->ticks_per_cell;
    im->ticks_since_flux = 0;

    decode_off = im->cur_bc / 16;
    if (decode_off < im->img.idx_sz) {
        im->img.decode_pos = 0;
    } else {
        decode_off -= im->img.idx_sz;
        im->img.decode_pos = decode_off / (im->img.idam_sz + im->img.dam_sz);
        if (im->img.decode_pos < im->img.nr_sectors) {
            im->img.trk_sec = im->img.decode_pos;
            im->img.decode_pos = im->img.decode_pos * 2 + 1;
            decode_off %= im->img.idam_sz + im->img.dam_sz;
            if (decode_off >= im->img.idam_sz) {
                decode_off -= im->img.idam_sz;
                im->img.decode_pos++;
            }
        } else {
            im->img.decode_pos = im->img.nr_sectors * 2 + 1;
            decode_off -= im->img.nr_sectors
                * (im->img.idam_sz + im->img.dam_sz);
        }
    }

    rd->prod = rd->cons = 0;
    bc->prod = bc->cons = 0;

    if (start_pos) {
        image_read_track(im);
        bc->cons = decode_off * 16;
        *start_pos = sys_ticks;
    }
}

static bool_t img_read_track(struct image *im)
{
    return (im->sync == SYNC_fm) ? fm_read_track(im) : mfm_read_track(im);
}

static bool_t img_write_track(struct image *im)
{
    return (im->sync == SYNC_fm) ? fm_write_track(im) : mfm_write_track(im);
}


/*
 * MFM-Specific Handlers
 */

#define GAP_1    50 /* Post-IAM */
#define GAP_2    22 /* Post-IDAM */
#define GAP_4A   80 /* Post-Index */
#define GAP_SYNC 12

/* Shrink the IDAM pre-sync gap if sectors are close together. */
#define idam_gap_sync(im) min_t(uint8_t, (im)->img.gap3, GAP_SYNC)

static bool_t mfm_open(struct image *im)
{
    uint32_t tracklen;

    im->img.idx_sz = im->img.gap_4a = GAP_4A;
    if (im->img.has_iam)
        im->img.idx_sz += GAP_SYNC + 4 + GAP_1;
    im->img.idam_sz = idam_gap_sync(im) + 8 + 2 + GAP_2;
    im->img.dam_sz = GAP_SYNC + 4 + sec_sz(im) + 2 + im->img.gap3;

    /* Work out minimum track length (with no pre-index track gap). */
    tracklen = (im->img.idam_sz + im->img.dam_sz) * im->img.nr_sectors;
    tracklen += im->img.idx_sz;
    tracklen *= 16;

    /* Infer the data rate and hence the standard track length. */
    im->img.data_rate = (tracklen < 55000) ? 250 /* SD */
        : (tracklen < 105000) ? 500 /* DD */
        : (tracklen < 205000) ? 1000 /* HD */
        : 2000; /* ED */
    im->tracklen_bc = im->img.data_rate * 200;

    /* Does the track data fit within standard track length? */
    if (im->tracklen_bc < tracklen) {
        if ((tracklen - im->img.gap_4a*16) <= im->tracklen_bc) {
            /* Eliminate the post-index gap 4a if that suffices. */
            tracklen -= im->img.gap_4a*16;
            im->img.idx_sz -= im->img.gap_4a;
            im->img.gap_4a = 0;
        } else {
            /* Extend the track length ("long track"). */
            im->tracklen_bc = tracklen + 100;
        }
    }

    /* Round the track length up to a multiple of 32 bitcells. */
    im->tracklen_bc = (im->tracklen_bc + 31) & ~31;

    im->ticks_per_cell = ((sysclk_stk(im->stk_per_rev) * 16u)
                          / im->tracklen_bc);
    im->img.gap4 = (im->tracklen_bc - tracklen) / 16;

    im->write_bc_ticks = sysclk_ms(1) / im->img.data_rate;

    return TRUE;
}

static bool_t mfm_read_track(struct image *im)
{
    struct image_buf *rd = &im->bufs.read_data;
    struct image_buf *bc = &im->bufs.read_bc;
    uint8_t *buf = rd->p;
    uint16_t *bc_b = bc->p;
    uint32_t bc_len, bc_mask, bc_space, bc_p, bc_c;
    uint16_t pr = 0, crc;
    unsigned int i, buflen = rd->len & ~511;

    if (rd->prod == rd->cons) {
        uint8_t sec = im->img.sec_map[im->img.trk_sec] - im->img.sec_base;
        F_lseek(&im->fp, im->img.trk_off + sec * sec_sz(im));
        F_read(&im->fp, &buf[(rd->prod/8) % buflen], sec_sz(im), NULL);
        rd->prod += sec_sz(im) * 8;
        if (++im->img.trk_sec >= im->img.nr_sectors)
            im->img.trk_sec = 0;
    }

    /* Generate some MFM if there is space in the raw-bitcell ring buffer. */
    bc_p = bc->prod / 16; /* MFM words */
    bc_c = bc->cons / 16; /* MFM words */
    bc_len = bc->len / 2; /* MFM words */
    bc_mask = bc_len - 1;
    bc_space = bc_len - (uint16_t)(bc_p - bc_c);

#define emit_raw(r) ({                                   \
    uint16_t _r = (r);                                   \
    bc_b[bc_p++ & bc_mask] = htobe16(_r & ~(pr << 15));  \
    pr = _r; })
#define emit_byte(b) emit_raw(mfmtab[(uint8_t)(b)])

    if (im->img.decode_pos == 0) {
        /* Post-index track gap */
        if (bc_space < im->img.idx_sz)
            return FALSE;
        for (i = 0; i < im->img.gap_4a; i++)
            emit_byte(0x4e);
        if (im->img.has_iam) {
            /* IAM */
            for (i = 0; i < GAP_SYNC; i++)
                emit_byte(0x00);
            for (i = 0; i < 3; i++)
                emit_raw(0x5224);
            emit_byte(0xfc);
            for (i = 0; i < GAP_1; i++)
                emit_byte(0x4e);
        }
    } else if (im->img.decode_pos == (im->img.nr_sectors * 2 + 1)) {
        /* Pre-index track gap */
        if (bc_space < im->img.gap4)
            return FALSE;
        for (i = 0; i < im->img.gap4; i++)
            emit_byte(0x4e);
        im->img.decode_pos = (im->img.idx_sz != 0) ? -1 : 0;
    } else if (im->img.decode_pos & 1) {
        /* IDAM */
        uint8_t cyl = im->cur_track/2, hd = im->cur_track&1;
        uint8_t sec = im->img.sec_map[(im->img.decode_pos-1) >> 1];
        uint8_t idam[8] = { 0xa1, 0xa1, 0xa1, 0xfe, cyl, hd, sec,
                            im->img.sec_no };
        if (bc_space < im->img.idam_sz)
            return FALSE;
        for (i = 0; i < idam_gap_sync(im); i++)
            emit_byte(0x00);
        for (i = 0; i < 3; i++)
            emit_raw(0x4489);
        for (; i < 8; i++)
            emit_byte(idam[i]);
        crc = crc16_ccitt(idam, sizeof(idam), 0xffff);
        emit_byte(crc >> 8);
        emit_byte(crc);
        for (i = 0; i < GAP_2; i++)
            emit_byte(0x4e);
    } else {
        /* DAM */
        uint8_t *dat = &buf[(rd->cons/8)%buflen];
        uint8_t dam[4] = { 0xa1, 0xa1, 0xa1, 0xfb };
        if (bc_space < im->img.dam_sz)
            return FALSE;
        for (i = 0; i < GAP_SYNC; i++)
            emit_byte(0x00);
        for (i = 0; i < 3; i++)
            emit_raw(0x4489);
        emit_byte(dam[3]);
        for (i = 0; i < sec_sz(im); i++)
            emit_byte(dat[i]);
        crc = crc16_ccitt(dam, sizeof(dam), 0xffff);
        crc = crc16_ccitt(dat, sec_sz(im), crc);
        emit_byte(crc >> 8);
        emit_byte(crc);
        for (i = 0; i < im->img.gap3; i++)
            emit_byte(0x4e);
        rd->cons += sec_sz(im) * 8;
    }

#undef emit_raw
#undef emit_byte

    im->img.decode_pos++;
    bc->prod = bc_p * 16;

    return TRUE;
}

static bool_t mfm_write_track(struct image *im)
{
    const uint8_t header[] = { 0xa1, 0xa1, 0xa1, 0xfb };

    bool_t flush;
    struct write *write = get_write(im, im->wr_cons);
    struct image_buf *wr = &im->bufs.write_bc;
    uint16_t *buf = wr->p;
    unsigned int bufmask = (wr->len / 2) - 1;
    uint8_t *wrbuf = im->bufs.write_data.p;
    uint32_t c = wr->cons / 16, p = wr->prod / 16;
    uint32_t base = write->start / im->ticks_per_cell; /* in data bytes */
    unsigned int i;
    time_t t;
    uint16_t crc;
    uint8_t x;

    /* If we are processing final data then use the end index, rounded up. */
    barrier();
    flush = (im->wr_cons != im->wr_bc);
    if (flush)
        p = (write->bc_end + 15) / 16;

    if (im->img.write_sector == -1) {
        /* Convert write offset to sector number (in rotational order). */
        im->img.write_sector =
            (base - im->img.idx_sz - im->img.idam_sz
             + (im->img.idam_sz + im->img.dam_sz) / 2)
            / (im->img.idam_sz + im->img.dam_sz);
        if (im->img.write_sector >= im->img.nr_sectors) {
            printk("IMG Bad Sector Offset: %u -> %u\n",
                   base, im->img.write_sector);
            im->img.write_sector = -2;
        } else {
            /* Convert rotational order to logical order. */
            im->img.write_sector = im->img.sec_map[im->img.write_sector];
            im->img.write_sector -= im->img.sec_base;
        }
    }

    while ((int16_t)(p - c) >= (3 + sec_sz(im) + 2)) {

        /* Scan for sync words and IDAM. Because of the way we sync we expect
         * to see only 2*4489 and thus consume only 3 words for the header. */
        if (be16toh(buf[c++ & bufmask]) != 0x4489)
            continue;
        for (i = 0; i < 2; i++)
            if ((x = mfmtobin(buf[c++ & bufmask])) != 0xa1)
                break;

        switch (x) {

        case 0xfe: /* IDAM */
            for (i = 0; i < 3; i++)
                wrbuf[i] = 0xa1;
            wrbuf[i++] = x;
            for (; i < 10; i++)
                wrbuf[i] = mfmtobin(buf[c++ & bufmask]);
            crc = crc16_ccitt(wrbuf, i, 0xffff);
            if (crc != 0) {
                printk("IMG IDAM Bad CRC %04x, sector %u\n", crc, wrbuf[6]);
                break;
            }
            im->img.write_sector = wrbuf[6] - im->img.sec_base;
            if ((uint8_t)im->img.write_sector >= im->img.nr_sectors) {
                printk("IMG IDAM Bad Sector: %u\n", wrbuf[6]);
                im->img.write_sector = -2;
            }
            break;

        case 0xfb: /* DAM */
            for (i = 0; i < (sec_sz(im) + 2); i++)
                wrbuf[i] = mfmtobin(buf[c++ & bufmask]);

            crc = crc16_ccitt(wrbuf, sec_sz(im) + 2,
                              crc16_ccitt(header, 4, 0xffff));
            if (crc != 0) {
                printk("IMG Bad CRC %04x, sector %u[%u]\n",
                       crc, im->img.write_sector,
                       im->img.write_sector + im->img.sec_base);
                break;
            }

            if (im->img.write_sector < 0) {
                printk("IMG DAM for unknown sector (%d)\n",
                       im->img.write_sector);
                break;
            }

            /* All good: write out to mass storage. */
            printk("Write %u[%u]/%u... ", im->img.write_sector,
                   im->img.write_sector + im->img.sec_base,
                   im->img.nr_sectors);
            t = time_now();
            F_lseek(&im->fp,
                    im->img.trk_off + im->img.write_sector*sec_sz(im));
            F_write(&im->fp, wrbuf, sec_sz(im), NULL);
            printk("%u us\n", time_diff(t, time_now()) / TIME_MHZ);
            break;
        }
    }

    wr->cons = c * 16;

    return flush;
}


/*
 * FM-Specific Handlers
 */

/* 8271 mini-diskette values */
#define FM_GAP_1 26
#define FM_GAP_2 11
#define FM_GAP_4A 16
#define FM_GAP_SYNC 6
#define FM_SYNC_CLK 0xc7

static bool_t fm_open(struct image *im)
{
    uint32_t tracklen;

    im->img.idx_sz = im->img.gap_4a = FM_GAP_4A;
    im->img.idam_sz = FM_GAP_SYNC + 5 + 2 + FM_GAP_2;
    im->img.dam_sz = FM_GAP_SYNC + 1 + sec_sz(im) + 2 + im->img.gap3;

    /* Work out minimum track length (with no pre-index track gap). */
    tracklen = (im->img.idam_sz + im->img.dam_sz) * im->img.nr_sectors;
    tracklen += im->img.idx_sz;
    tracklen *= 16;

    /* Infer the data rate and hence the standard track length. */
    im->img.data_rate = 250; /* SD */
    im->tracklen_bc = im->img.data_rate * 200;

    ASSERT(im->tracklen_bc > tracklen);

    /* Round the track length up to a multiple of 32 bitcells. */
    im->tracklen_bc = (im->tracklen_bc + 31) & ~31;

    im->ticks_per_cell = ((sysclk_stk(im->stk_per_rev) * 16u)
                          / im->tracklen_bc);
    im->img.gap4 = (im->tracklen_bc - tracklen) / 16;

    im->write_bc_ticks = sysclk_ms(1) / im->img.data_rate;

    im->sync = SYNC_fm;

    return TRUE;
}

static uint16_t fm_sync(uint8_t dat, uint8_t clk)
{
    uint16_t _dat = mfmtab[dat] & 0x5555;
    uint16_t _clk = (mfmtab[clk] & 0x5555) << 1;
    return _clk | _dat;
}

static bool_t fm_read_track(struct image *im)
{
    struct image_buf *rd = &im->bufs.read_data;
    struct image_buf *bc = &im->bufs.read_bc;
    uint8_t *buf = rd->p;
    uint16_t crc, *bc_b = bc->p;
    uint32_t bc_len, bc_mask, bc_space, bc_p, bc_c;
    unsigned int i, buflen = rd->len & ~511;

    if (rd->prod == rd->cons) {
        uint8_t sec = im->img.sec_map[im->img.trk_sec] - im->img.sec_base;
        F_lseek(&im->fp, im->img.trk_off + sec * sec_sz(im));
        F_read(&im->fp, &buf[(rd->prod/8) % buflen], sec_sz(im), NULL);
        rd->prod += sec_sz(im) * 8;
        if (++im->img.trk_sec >= im->img.nr_sectors)
            im->img.trk_sec = 0;
    }

    /* Generate some FM if there is space in the raw-bitcell ring buffer. */
    bc_p = bc->prod / 16; /* FM words */
    bc_c = bc->cons / 16; /* FM words */
    bc_len = bc->len / 2; /* FM words */
    bc_mask = bc_len - 1;
    bc_space = bc_len - (uint16_t)(bc_p - bc_c);

#define emit_raw(r) ({                          \
    uint16_t _r = (r);                          \
    bc_b[bc_p++ & bc_mask] = htobe16(_r); })
#define emit_byte(b) emit_raw(mfmtab[(uint8_t)(b)] | 0xaaaa)

    if (im->img.decode_pos == 0) {
        /* Post-index track gap */
        if (bc_space < im->img.idx_sz)
            return FALSE;
        for (i = 0; i < im->img.gap_4a; i++)
            emit_byte(0xff);
        ASSERT(!im->img.has_iam);
    } else if (im->img.decode_pos == (im->img.nr_sectors * 2 + 1)) {
        /* Pre-index track gap */
        if (bc_space < im->img.gap4)
            return FALSE;
        for (i = 0; i < im->img.gap4; i++)
            emit_byte(0xff);
        im->img.decode_pos = (im->img.idx_sz != 0) ? -1 : 0;
    } else if (im->img.decode_pos & 1) {
        /* IDAM */
        uint8_t cyl = im->cur_track/2, hd = im->cur_track&1;
        uint8_t sec = im->img.sec_map[(im->img.decode_pos-1) >> 1];
        uint8_t idam[5] = { 0xfe, cyl, hd, sec, im->img.sec_no };
        if (bc_space < im->img.idam_sz)
            return FALSE;
        for (i = 0; i < FM_GAP_SYNC; i++)
            emit_byte(0x00);
        emit_raw(fm_sync(idam[0], FM_SYNC_CLK));
        for (i = 1; i < 5; i++)
            emit_byte(idam[i]);
        crc = crc16_ccitt(idam, sizeof(idam), 0xffff);
        emit_byte(crc >> 8);
        emit_byte(crc);
        for (i = 0; i < FM_GAP_2; i++)
            emit_byte(0xff);
    } else {
        /* DAM */
        uint8_t *dat = &buf[(rd->cons/8)%buflen];
        uint8_t dam[1] = { 0xfb };
        if (bc_space < im->img.dam_sz)
            return FALSE;
        for (i = 0; i < FM_GAP_SYNC; i++)
            emit_byte(0x00);
        emit_raw(fm_sync(dam[0], FM_SYNC_CLK));
        for (i = 0; i < sec_sz(im); i++)
            emit_byte(dat[i]);
        crc = crc16_ccitt(dam, sizeof(dam), 0xffff);
        crc = crc16_ccitt(dat, sec_sz(im), crc);
        emit_byte(crc >> 8);
        emit_byte(crc);
        for (i = 0; i < im->img.gap3; i++)
            emit_byte(0xff);
        rd->cons += sec_sz(im) * 8;
    }

#undef emit_raw
#undef emit_byte

    im->img.decode_pos++;
    bc->prod = bc_p * 16;

    return TRUE;
}

static bool_t fm_write_track(struct image *im)
{
    bool_t flush;
    struct write *write = get_write(im, im->wr_cons);
    struct image_buf *wr = &im->bufs.write_bc;
    uint16_t *buf = wr->p;
    unsigned int bufmask = (wr->len / 2) - 1;
    uint8_t *wrbuf = im->bufs.write_data.p;
    uint32_t c = wr->cons / 16, p = wr->prod / 16;
    uint32_t base = write->start / im->ticks_per_cell; /* in data bytes */
    unsigned int i;
    time_t t;
    uint16_t crc, sync;
    uint8_t x;

    /* If we are processing final data then use the end index, rounded up. */
    barrier();
    flush = (im->wr_cons != im->wr_bc);
    if (flush)
        p = (write->bc_end + 15) / 16;

    if (im->img.write_sector == -1) {
        /* Convert write offset to sector number (in rotational order). */
        im->img.write_sector =
            (base - im->img.idx_sz - im->img.idam_sz
             + (im->img.idam_sz + im->img.dam_sz) / 2)
            / (im->img.idam_sz + im->img.dam_sz);
        if (im->img.write_sector >= im->img.nr_sectors) {
            printk("IMG Bad Sector Offset: %u -> %u\n",
                   base, im->img.write_sector);
            im->img.write_sector = -2;
        } else {
            /* Convert rotational order to logical order. */
            im->img.write_sector = im->img.sec_map[im->img.write_sector];
            im->img.write_sector -= im->img.sec_base;
        }
    }

    while ((int16_t)(p - c) >= (2 + sec_sz(im) + 2)) {

        if (buf[c++ & bufmask] != 0xaaaa)
            continue;
        sync = buf[c & bufmask];
        if (mfmtobin(sync >> 1) != FM_SYNC_CLK)
            continue;
        x = mfmtobin(sync);
        c++;

        switch (x) {

        case 0xfe: /* IDAM */
            wrbuf[0] = x;
            for (i = 1; i < 7; i++)
                wrbuf[i] = mfmtobin(buf[c++ & bufmask]);
            crc = crc16_ccitt(wrbuf, i, 0xffff);
            if (crc != 0) {
                printk("IMG IDAM Bad CRC %04x, sector %u\n", crc, wrbuf[3]);
                break;
            }
            im->img.write_sector = wrbuf[3] - im->img.sec_base;
            if ((uint8_t)im->img.write_sector >= im->img.nr_sectors) {
                printk("IMG IDAM Bad Sector: %u\n", wrbuf[3]);
                im->img.write_sector = -2;
            }
            break;

        case 0xfb: /* DAM */
            for (i = 0; i < (sec_sz(im) + 2); i++)
                wrbuf[i] = mfmtobin(buf[c++ & bufmask]);

            crc = crc16_ccitt(wrbuf, sec_sz(im) + 2,
                              crc16_ccitt(&x, 1, 0xffff));
            if (crc != 0) {
                printk("IMG Bad CRC %04x, sector %u[%u]\n",
                       crc, im->img.write_sector,
                       im->img.write_sector + im->img.sec_base);
                break;
            }

            if (im->img.write_sector < 0) {
                printk("IMG DAM for unknown sector (%d)\n",
                       im->img.write_sector);
                break;
            }

            /* All good: write out to mass storage. */
            printk("Write %u[%u]/%u... ", im->img.write_sector,
                   im->img.write_sector + im->img.sec_base,
                   im->img.nr_sectors);
            t = time_now();
            F_lseek(&im->fp,
                    im->img.trk_off + im->img.write_sector*sec_sz(im));
            F_write(&im->fp, wrbuf, sec_sz(im), NULL);
            printk("%u us\n", time_diff(t, time_now()) / TIME_MHZ);
            break;
        }
    }

    wr->cons = c * 16;

    return flush;
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

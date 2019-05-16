/*
 * img.c
 * 
 * Sector image files for IBM/ISO track formats.
 * 
 * Written & released by Keir Fraser <keir.xen@gmail.com>
 * 
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

static FSIZE_t raw_extend(struct image *im);
static void raw_setup_track(
    struct image *im, uint16_t track, uint32_t *start_pos);
static bool_t raw_read_track(struct image *im);
static bool_t raw_write_track(struct image *im);
static bool_t raw_open(struct image *im);
static void mfm_prep_track(struct image *im);
static bool_t mfm_read_track(struct image *im);
static void fm_prep_track(struct image *im);
static bool_t fm_read_track(struct image *im);
static void *align_p(void *p);
static void check_p(void *p, struct image *im);

const static struct simple_layout {
    uint16_t nr_sectors;
    uint8_t is_fm, has_iam, no, gap3, gap4a, base[2];
} dfl_simple_layout = { 0, FALSE, TRUE, ~0, 0, 0, { 1, 1 } };
static void simple_layout(
    struct image *im, const struct simple_layout *layout);

static struct raw_trk *add_track_layout(
    struct image *im, unsigned int nr_sectors, unsigned int trk_idx);
static uint8_t *add_track_map(struct image *im);

static bool_t msx_open(struct image *im);
static bool_t pc_dos_open(struct image *im);
static bool_t ti99_open(struct image *im);
static bool_t uknc_open(struct image *im);

struct bpb;
static bool_t xdf_check(const struct bpb *bpb);

#define LAYOUT_sequential      (1u<<0)
#define LAYOUT_sides_swapped   (1u<<1)
#define LAYOUT_reverse_side(x) (1u<<(2+(x)))

#define sec_sz(n) (128u << (n))

#define _IAM 1 /* IAM */
#define _ITN 1 /* inter-track numbering */
#define _C(cyls) ((cyls) / 40 - 1)
#define _R(rpm) ((rpm) / 60 - 5)
#define _S(sides) ((sides) - 1)
const static struct raw_type {
    uint8_t nr_secs:6;
    uint8_t nr_sides:1;
    uint8_t has_iam:1;
    uint8_t gap3;
    uint8_t interleave:3;
    uint8_t no:3;
    uint8_t base:1;
    uint8_t inter_track_numbering:1;
    uint8_t cskew:4;
    uint8_t hskew:2;
    uint8_t cyls:1;
    uint8_t rpm:1;
} img_type[] = {
    {  8, _S(1), _IAM, 84, 1, 2, 1, 0, 0, 0, _C(40), _R(300) }, /* 160k */
    {  9, _S(1), _IAM, 84, 1, 2, 1, 0, 0, 0, _C(40), _R(300) }, /* 180k */
    { 10, _S(1), _IAM, 30, 1, 2, 1, 0, 0, 0, _C(40), _R(300) }, /* 200k */
    {  8, _S(2), _IAM, 84, 1, 2, 1, 0, 0, 0, _C(40), _R(300) }, /* 320k */
    {  9, _S(2), _IAM, 84, 1, 2, 1, 0, 0, 0, _C(40), _R(300) }, /* 360k (#1) */
    { 10, _S(2), _IAM, 30, 1, 2, 1, 0, 0, 0, _C(40), _R(300) }, /* 400k (#1) */
    { 15, _S(2), _IAM, 84, 1, 2, 1, 0, 0, 0, _C(80), _R(360) }, /* 1.2MB */
    {  9, _S(1), _IAM, 84, 1, 2, 1, 0, 0, 0, _C(80), _R(300) }, /* 360k (#2) */
    { 10, _S(1), _IAM, 30, 1, 2, 1, 0, 0, 0, _C(80), _R(300) }, /* 400k (#2) */
    { 11, _S(1), _IAM,  3, 2, 2, 1, 0, 0, 0, _C(80), _R(300) }, /* 440k */
    {  8, _S(2), _IAM, 84, 1, 2, 1, 0, 0, 0, _C(80), _R(300) }, /* 640k */
    {  9, _S(2), _IAM, 84, 1, 2, 1, 0, 0, 0, _C(80), _R(300) }, /* 720k */
    { 10, _S(2), _IAM, 30, 1, 2, 1, 0, 0, 0, _C(80), _R(300) }, /* 800k */
    { 11, _S(2), _IAM,  3, 2, 2, 1, 0, 0, 0, _C(80), _R(300) }, /* 880k */
    { 18, _S(2), _IAM, 84, 1, 2, 1, 0, 0, 0, _C(80), _R(300) }, /* 1.44M */
    { 19, _S(2), _IAM, 70, 1, 2, 1, 0, 0, 0, _C(80), _R(300) }, /* 1.52M */
    { 21, _S(2), _IAM, 18, 2, 2, 1, 0, 0, 0, _C(80), _R(300) }, /* 1.68M */
    { 20, _S(2), _IAM, 40, 1, 2, 1, 0, 0, 0, _C(80), _R(300) }, /* 1.6M */
    { 36, _S(2), _IAM, 84, 1, 2, 1, 0, 0, 0, _C(80), _R(300) }, /* 2.88M */
    { 0 }
}, adfs_type[] = {
    /* ADFS D/E: 5 * 1kB, 800k */
    {  5, _S(2), _IAM, 116, 1, 3, 0, 0, 1, 0, _C(80), _R(300) },
    /* ADFS F: 10 * 1kB, 1600k */
    { 10, _S(2), _IAM, 116, 1, 3, 0, 0, 2, 0, _C(80), _R(300) },
    /* ADFS L 640k */
    { 16, _S(2), _IAM,  57, 1, 1, 0, 0, 0, 0, _C(80), _R(300) },
    /* ADFS M 320k */
    { 16, _S(1), _IAM,  57, 1, 1, 0, 0, 0, 0, _C(80), _R(300) },
    /* ADFS S 160k */
    { 16, _S(1), _IAM,  57, 1, 1, 0, 0, 0, 0, _C(40), _R(300) },
    { 0 }
}, akai_type[] = {
    /* Akai DD:  5*1kB sectors */
    {  5, _S(2), _IAM, 116, 1, 3, 1, 0, 0, 0, _C(80), _R(300) },
    /* Akai HD: 10*1kB sectors */
    { 10, _S(2), _IAM, 116, 1, 3, 1, 0, 0, 0, _C(80), _R(300) },
    { 0 }
}, casio_type[] = {
    { 8, _S(2), _IAM, 116, 3, 3, 1, 0, 0, 0, _C(80), _R(360) }, /* 1280k */
    { 0 }
}, d81_type[] = {
    { 10, _S(2), _IAM, 30, 1, 2, 1, 0, 0, 0, _C(80), _R(300) },
    { 0 }
}, dec_type[] = {
    /* RX50 (400k) */
    { 10, _S(1), _IAM, 30, 1, 2, 1, 0, 0, 0, _C(80), _R(300) },
    /* RX33 (1.2MB) from default list */
    { 0 }
}, ensoniq_type[] = {
    { 10, _S(2), _IAM, 30, 1, 2, 0, 0, 0, 0, _C(80), _R(300) }, /* 800kB */
    { 20, _S(2), _IAM, 40, 1, 2, 0, 0, 0, 0, _C(80), _R(300) }, /* 1.6MB */
    { 0 }
}, fluke_type[] = {
    { 16, _S(2), _IAM, 57, 2, 1, 0, 0, 0, 0, _C(80), _R(300) },
    { 0 }
}, kaypro_type[] = {
    { 10, _S(1), _IAM, 30, 3, 2, 0, _ITN, 0, 0, _C(40), _R(300) }, /* 200k */
    { 10, _S(2), _IAM, 30, 3, 2, 0, _ITN, 0, 0, _C(40), _R(300) }, /* 400k */
    { 10, _S(2), _IAM, 30, 3, 2, 0, _ITN, 0, 0, _C(80), _R(300) }, /* 800k */
    { 0 }
}, mbd_type[] = {
    { 11, _S(2), _IAM,  30, 1, 3, 1, 0, 0, 0, _C(80), _R(300) },
    {  5, _S(2), _IAM, 116, 3, 1, 1, 0, 0, 0, _C(80), _R(300) },
    { 11, _S(2), _IAM,  30, 1, 3, 1, 0, 0, 0, _C(40), _R(300) },
    {  5, _S(2), _IAM, 116, 3, 1, 1, 0, 0, 0, _C(40), _R(300) },
    { 0 }
}, memotech_type[] = {
    { 16, _S(2), _IAM, 57, 3, 1, 1, 0, 0, 0, _C(40), _R(300) }, /* Type 03 */
    { 16, _S(2), _IAM, 57, 3, 1, 1, 0, 0, 0, _C(80), _R(300) }, /* Type 07 */
    { 0 }
}, msx_type[] = {
    { 8, _S(1), _IAM, 84, 1, 2, 1, 0, 0, 0, _C(80), _R(300) }, /* 320k */
    { 9, _S(1), _IAM, 84, 1, 2, 1, 0, 0, 0, _C(80), _R(300) }, /* 360k */
    { 0 } /* all other formats from default list */
}, nascom_type[] = {
    { 16, _S(1), _IAM, 57, 3, 1, 1, 0, 8, 0, _C(80), _R(300) }, /* 320k */
    { 16, _S(2), _IAM, 57, 3, 1, 1, 0, 8, 0, _C(80), _R(300) }, /* 360k */
    { 0 }
}, pc98_type[] = {
    { 8, _S(2), _IAM, 116, 1, 3, 1, 0, 0, 0, _C(80), _R(360) }, /* 1232k */
    { 8, _S(2), _IAM, 116, 1, 2, 1, 0, 0, 0, _C(80), _R(360) }, /* 640k */
    { 9, _S(2), _IAM, 116, 1, 2, 1, 0, 0, 0, _C(80), _R(360) }, /* 720k */
    { 0 }
}, uknc_type[] = {
    { 10, _S(2), 0, 38, 1, 2, 1, 0, 0, 0, _C(80), _R(300) },
    { 0 }
};

static FSIZE_t im_size(struct image *im)
{
    return (f_size(&im->fp) < im->img.base_off) ? 0
        : (f_size(&im->fp) - im->img.base_off);
}

static unsigned int enc_sec_sz(struct image *im, struct raw_sec *sec)
{
    return im->img.idam_sz + im->img.dam_sz_pre
        + sec_sz(sec->no) + im->img.dam_sz_post;
}

static void reset_all_params(struct image *im)
{
    memset(&im->img, 0, sizeof(im->img));
    im->nr_cyls = im->nr_sides = 0;
}

static bool_t raw_type_open(struct image *im, const struct raw_type *type)
{
    struct simple_layout layout;
    unsigned int nr_cyls, cyl_sz, nr_sides;

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
        nr_sides = type->nr_sides + 1;
        cyl_sz = type->nr_secs * (128 << type->no) * nr_sides;
        for (nr_cyls = min_cyls; nr_cyls <= max_cyls; nr_cyls++)
            if ((nr_cyls * cyl_sz) == im_size(im))
                goto found;
    }

    return FALSE;

found:
    im->nr_cyls = nr_cyls;
    im->nr_sides = nr_sides;
    im->img.interleave = type->interleave;
    im->img.hskew = type->hskew;
    im->img.cskew = type->cskew;
    im->img.rpm = (type->rpm + 5) * 60;

    layout.has_iam = type->has_iam;
    layout.nr_sectors = type->nr_secs;
    layout.no = type->no;
    layout.gap3 = type->gap3;
    layout.gap4a = 0;
    layout.is_fm = FALSE;
    layout.base[0] = layout.base[1] = type->base;
    if (type->inter_track_numbering == _ITN)
        layout.base[1] += type->nr_secs;

    simple_layout(im, &layout);

    return raw_open(im);
}

static bool_t tag_open(struct image *im, char *tag)
{
    enum {
        IMGCFG_cyls,
        IMGCFG_heads,
        IMGCFG_secs,
        IMGCFG_bps,
        IMGCFG_id,
        IMGCFG_mode,
        IMGCFG_interleave,
        IMGCFG_cskew,
        IMGCFG_hskew,
        IMGCFG_rpm,
        IMGCFG_gap3,
        IMGCFG_iam,
        IMGCFG_rate,
        IMGCFG_file_layout,
        IMGCFG_nr
    };

    const static struct opt img_cfg_opts[IMGCFG_nr+1] = {
        [IMGCFG_cyls] = { "cyls" },
        [IMGCFG_heads] = { "heads" },
        [IMGCFG_secs] = { "secs" },
        [IMGCFG_bps]  = { "bps" },
        [IMGCFG_id]   = { "id" },
        [IMGCFG_mode] = { "mode" },
        [IMGCFG_interleave] = { "interleave" },
        [IMGCFG_cskew] = { "cskew" },
        [IMGCFG_hskew] = { "hskew" },
        [IMGCFG_rpm]  = { "rpm" },
        [IMGCFG_gap3] = { "gap3" },
        [IMGCFG_iam]  = { "iam" },
        [IMGCFG_rate] = { "rate" },
        [IMGCFG_file_layout] = { "file-layout" },
    };

    int i, option;
    bool_t matched, active;
    uint16_t data_rate;
    struct simple_layout layout;
    struct {
        FIL file;
        struct slot slot;
        char buf[512];
    } *heap = (void *)im->bufs.read_data.p;
    struct opts opts = {
        .file = &heap->file,
        .opts = img_cfg_opts,
        .arg = heap->buf,
        .argmax = sizeof(heap->buf)-1
    };

    if (!get_img_cfg(&heap->slot))
        return FALSE;

    fatfs_from_slot(&heap->file, &heap->slot, FA_READ);

    matched = active = FALSE;

    while ((option = get_next_opt(&opts)) != OPT_eof) {

        if (option == OPT_section) {
            if (active) {
                simple_layout(im, &layout);
                for (i = 0; i < im->nr_sides; i++)
                    im->img.trk_info[i].data_rate = data_rate;
            }
            /* We process this section if we get a tag match, or if this 
             * is the default section and we have no other match so far. */
            active = (tag && !strcmp(opts.arg, tag))
                || (!matched && !strcmp(opts.arg, "default"));
            if (active) {
                matched = TRUE;
                reset_all_params(im);
                im->img.interleave = 1;
                data_rate = 0;
                layout = dfl_simple_layout;
            }
        }

        if (!active)
            continue;

        switch (option) {

        case IMGCFG_cyls:
            im->nr_cyls = strtol(opts.arg, NULL, 10);
            break;
        case IMGCFG_heads:
            im->nr_sides = strtol(opts.arg, NULL, 10);
            break;
        case IMGCFG_secs:
            layout.nr_sectors = strtol(opts.arg, NULL, 10);
            break;
        case IMGCFG_bps: {
            int no, sz = strtol(opts.arg, NULL, 10);
            for (no = 0; no < 8; no++)
                if ((128u<<no) == sz)
                    break;
            layout.no = no;
            break;
        }
        case IMGCFG_id: {
            char *p = opts.arg;
            layout.base[0] = strtol(p, &p, 0);
            layout.base[1] = (*p == ':') ? strtol(p+1, &p, 0) : layout.base[0];
            break;
        }
        case IMGCFG_mode:
            layout.is_fm = !strcmp(opts.arg, "fm");
            break;
        case IMGCFG_interleave:
            im->img.interleave = strtol(opts.arg, NULL, 10);
            break;
        case IMGCFG_cskew:
            im->img.cskew = strtol(opts.arg, NULL, 10);
            break;
        case IMGCFG_hskew:
            im->img.hskew = strtol(opts.arg, NULL, 10);
            break;
        case IMGCFG_rpm:
            im->img.rpm = strtol(opts.arg, NULL, 10);
            break;
        case IMGCFG_gap3:
            layout.gap3 = strtol(opts.arg, NULL, 10);
            break;
        case IMGCFG_iam:
            layout.has_iam = !strcmp(opts.arg, "yes");
            break;
        case IMGCFG_rate:
            data_rate = strtol(opts.arg, NULL, 10);
            break;
        case IMGCFG_file_layout: {
            char *p, *q;
            for (p = opts.arg; *p != '\0'; p = q) {
                for (q = p; *q && *q != ','; q++)
                    continue;
                if (*q == ',')
                    *q++ = '\0';
                if (!strncmp(p, "reverse-side", 12)) {
                    uint8_t side = !!strtol(p+12, NULL, 10);
                    im->img.layout |= LAYOUT_reverse_side(side);
                } else if (!strcmp(p, "sequential")) {
                    im->img.layout |= LAYOUT_sequential;
                } else if (!strcmp(p, "sides-swapped")) {
                    im->img.layout |= LAYOUT_sides_swapped;
                }
            }
            break;
        }

        }
    }

    if (active) {
        simple_layout(im, &layout);
        for (i = 0; i < im->nr_sides; i++)
            im->img.trk_info[i].data_rate = data_rate;
    }

    F_close(&heap->file);

    return matched ? raw_open(im) : FALSE;
}

static bool_t img_open(struct image *im)
{
    const struct raw_type *type;
    char *dot;

    dot = strrchr(im->slot->name, '.');
    if (tag_open(im, dot ? dot+1 : NULL))
        return TRUE;

    switch (ff_cfg.host) {
    case HOST_akai:
    case HOST_gem:
        type = akai_type;
        break;
    case HOST_casio:
        type = casio_type;
        break;
    case HOST_dec:
        type = dec_type;
        break;
    case HOST_ensoniq:
        type = ensoniq_type;
        break;
    case HOST_fluke:
        type = fluke_type;
        break;
    case HOST_kaypro:
        type = kaypro_type;
        break;
    case HOST_memotech:
        type = memotech_type;
        break;
    case HOST_msx:
        if (msx_open(im))
            return TRUE;
        goto fallback;
    case HOST_nascom:
        type = nascom_type;
        break;
    case HOST_pc98:
        type = pc98_type;
        break;
    case HOST_pc_dos:
        if (pc_dos_open(im))
            return TRUE;
        goto fallback;
    case HOST_ti99:
        return ti99_open(im);
    case HOST_uknc:
        return uknc_open(im);
    default:
        type = img_type;
        break;
    }

    /* Try specified host-specific geometries. */
    if (raw_type_open(im, type))
        return TRUE;

fallback:
    /* Fall back to default list. */
    reset_all_params(im);
    return raw_type_open(im, img_type);
}

static bool_t adfs_open(struct image *im)
{
    return raw_type_open(im, adfs_type);
}

static bool_t atr_open(struct image *im)
{

/* Original Atari drives (eg 1050) spin slightly slow (288rpm, -4%). 
 * Later interfaces use normal-speed drives (300rpm) with a faster-than-usual
 * bit rate (eg XF551 drives controller at 8.333MHz rather than 8MHz (+4%)). 
 * We emulate that faster bitrate here. 
 * Source: Atarimania FAQ, "How can I read/write Atari diskettes with
 * my other computer?" */
#define ATR_RATE(_r) ((_r) + (_r)/25)

/* Atari machines transfer floppy data via the slow SIO interface. This is 
 * capable of transferring only approx 2 sectors per disk revolution. Hence 
 * a significant sector interleave is required. 
 * Source: atariage.com/forums/topic/269694-improved-sector-layout-cx8111 */
#define ATR_INTERLEAVE(_secs) ((_secs)/2)

    struct {
        uint16_t sig, size_lo, size_sec, size_hi;
        uint8_t flags, unused[7];
    } header;
    bool_t is_fm;
    unsigned int i, j, sz, no, nr_sectors, rate;
    struct raw_sec *sec;
    struct raw_trk *trk;
    uint8_t *trk_map;

    F_read(&im->fp, &header, sizeof(header), NULL);
    if (le16toh(header.sig) != 0x0296)
        return FALSE;
    sz = (unsigned int)le16toh(header.size_lo) << 4;
    no = le16toh(header.size_sec) / 256; /* 128 or 256 -> 0 or 1 */

    /* 40-1-18, 256b/s, MFM */
    nr_sectors = 18;
    im->nr_cyls = 40;
    im->nr_sides = 1;
    is_fm = FALSE;
    rate = ATR_RATE(250);
    if (no == 0) {
        is_fm = (sz < (130*1024));
        if (is_fm) {
            /* 40-1-18, 128b/s, FM */
            rate = ATR_RATE(125);
        } else {
            /* 40-1-26, 128b/s, MFM */
            nr_sectors = 26;
        }
    } else if (sz >= (360*1024-3*128)) {
        /* 40-2-18, 256b/s, MFM */
        im->nr_sides = 2;
    }
    im->img.interleave = ATR_INTERLEAVE(nr_sectors);
    im->img.base_off = 16;

    /* Create two track layout: 0 -> Track 0; 1 -> All other tracks. */
    for (i = 0; i < 2; i++) {
        trk = add_track_layout(im, nr_sectors, i);
        trk->has_iam = TRUE;
        trk->is_fm = is_fm;
        trk->invert_data = TRUE;
        trk->data_rate = rate;
        sec = &im->img.sec_info_base[trk->sec_off];
        for (j = 0; j < nr_sectors; j++) {
            sec->id = j + 1;
            sec->no = no;
            sec++;
        }
    }

    /* Track 0 layout: First three sectors are always 128 bytes. */
    sec = &im->img.sec_info_base[im->img.trk_info[0].sec_off];
    for (i = 0; i < 3; i++) {
        sec->no = 0;
        sec++;
    }

    /* Track map: Special layout for first track only. */
    trk_map = add_track_map(im);
    *trk_map++ = 0;
    for (i = 1; i < im->nr_cyls * im->nr_sides; i++)
        *trk_map++ = 1;

    return raw_open(im);
}

static bool_t d81_open(struct image *im)
{
    im->img.layout = LAYOUT_sides_swapped;
    return raw_type_open(im, d81_type);
}

static bool_t st_open(struct image *im)
{
    const struct raw_type *in;
    struct raw_type *out, *st_type;

    st_type = out = im->bufs.read_data.p;

    for (in = img_type; in->nr_secs != 0; in++) {
        if (in->cyls != _C(80))
            continue;
        memcpy(out, in, sizeof(*out));
        out->has_iam = FALSE;
        if (out->nr_secs == 9) {
            /* TOS formats 720kB disks with skew. */
            if (out->nr_sides == _S(1)) {
                out->cskew = 2;
            } else { /* out->nr_sides == _S(2) */
                out->cskew = 4;
                out->hskew = 2;
            }
        }
        out++;
    }

    memset(out, 0, sizeof(*out));

    return raw_type_open(im, st_type);
}

static bool_t mbd_open(struct image *im)
{
    return raw_type_open(im, mbd_type);
}

static bool_t mgt_open(struct image *im)
{
    return raw_type_open(im, img_type);
}

static bool_t pc98fdi_open(struct image *im)
{
    struct {
        uint32_t zero;
        uint32_t density;
        uint32_t header_size;
        uint32_t image_body_size;
        uint32_t sector_size_bytes;
        uint32_t nr_secs;
        uint32_t nr_sides;
        uint32_t cyls;
    } header;
    struct simple_layout layout = dfl_simple_layout;
    F_read(&im->fp, &header, sizeof(header), NULL);
    if (le32toh(header.density) == 0x30) {
        im->img.rpm = 300;
        layout.gap3 = 84;
    } else {
        im->img.rpm = 360;
        layout.gap3 = 116;
    }
    layout.no = (le32toh(header.sector_size_bytes) == 512) ? 2 : 3;
    layout.nr_sectors = le32toh(header.nr_secs);
    im->nr_cyls = le32toh(header.cyls);
    im->nr_sides = le32toh(header.nr_sides);
    im->img.interleave = 1;
    /* Skip 4096-byte header. */
    im->img.base_off = le32toh(header.header_size);
    simple_layout(im, &layout);
    return raw_open(im);
}

static bool_t pc98hdm_open(struct image *im)
{
    return raw_type_open(im, pc98_type);
}

struct bpb {
    uint16_t sig;
    uint16_t bytes_per_sec;
    uint16_t sec_per_track;
    uint16_t num_heads;
    uint16_t tot_sec;
    uint16_t rootdir_ents;
    uint16_t fat_secs;
};

static void bpb_read(struct image *im, struct bpb *bpb)
{
    unsigned int i;
    uint16_t *x = (uint16_t *)bpb;
    const static uint16_t offs[] = {
        510, 11, 24, 26, 19, 17, 22 };

    for (i = 0; i < ARRAY_SIZE(offs); i++) {
        F_lseek(&im->fp, offs[i]);
        F_read(&im->fp, x, 2, NULL);
        *x = le16toh(*x);
        x++;
    }
}

static bool_t msx_open(struct image *im)
{
    struct bpb bpb;
    struct simple_layout layout = dfl_simple_layout;

    /* Try to disambiguate overloaded image sizes via the boot sector. */
    switch (im_size(im)) {
    case 320*1024: /* 80/1/8 or 40/2/8? */
    case 360*1024: /* 80/1/9 or 40/2/9? */
        bpb_read(im, &bpb);
        /* BS_55AA (bpb.sig) is not valid in MSXDOS so don't check it. */
        if ((bpb.bytes_per_sec == 512)
            && ((bpb.num_heads == 1) || (bpb.num_heads == 2))
            && (bpb.tot_sec == (im_size(im) / bpb.bytes_per_sec))
            && ((bpb.sec_per_track == 8) || (bpb.sec_per_track == 9))) {
            layout.no = 2;
            layout.nr_sectors = bpb.sec_per_track;
            im->nr_sides = bpb.num_heads;
            im->nr_cyls = (im->nr_sides == 1) ? 80 : 40;
            im->img.interleave = 1;
            simple_layout(im, &layout);
            if (raw_open(im))
                return TRUE;
        }
        break;
    }

    /* Use the MSX-specific list. */
    reset_all_params(im);
    if (raw_type_open(im, msx_type))
        return TRUE;

    /* Caller falls back to the generic list. */
    return FALSE;
}

static bool_t pc_dos_open(struct image *im)
{
    struct bpb bpb;
    struct simple_layout layout = dfl_simple_layout;
    unsigned int no;

    bpb_read(im, &bpb);

    if (bpb.sig != 0xaa55)
        goto fail;

    for (no = 0; no <= 6; no++)
        if (sec_sz(no) == bpb.bytes_per_sec)
            break;
    layout.no = no;

    if ((bpb.sec_per_track == 0) || (bpb.sec_per_track > 256))
        goto fail;
    layout.nr_sectors = bpb.sec_per_track;

    /* Yuk! A simple check for 3.5-inch HD XDF. Bail if we get a match:
     * Our caller will fall back to the XDF handler. */
    if ((bpb.sec_per_track == 23) && xdf_check(&bpb))
        goto fail;

    if ((bpb.num_heads != 1) && (bpb.num_heads != 2))
        goto fail;
    im->nr_sides = bpb.num_heads;

    im->nr_cyls = (bpb.tot_sec + layout.nr_sectors*im->nr_sides - 1)
        / (layout.nr_sectors * im->nr_sides);
    if (im->nr_cyls == 0)
        goto fail;

    im->img.interleave = 1;

    simple_layout(im, &layout);
    return raw_open(im);

fail:
    return FALSE;
}

static bool_t trd_open(struct image *im)
{
    const struct simple_layout layout = {
        .nr_sectors = 16,
        .is_fm = FALSE,
        .has_iam = TRUE,
        .no = 1, /* 256-byte */
        .gap3 = 57,
        .base = { 1, 1 }
    };
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
        if (im_size(im) <= 40*16*256) {
            im->nr_cyls = 40;
            im->nr_sides = 1;
        } else if (im_size(im) < 40*2*16*256) {
            im->nr_cyls = 40;
            im->nr_sides = 1;
        } else {
            im->nr_cyls = 80;
            im->nr_sides = 2;
        }
    }

    im->img.interleave = 1;

    simple_layout(im, &layout);
    return raw_open(im);
}

static bool_t opd_open(struct image *im)
{
    const struct simple_layout layout = {
        .nr_sectors = 18,
        .is_fm = FALSE,
        .has_iam = TRUE,
        .no = 1, /* 256-byte */
        .gap3 = 12,
        .base = { 0, 0 }
    };

    switch (im_size(im)) {
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

    im->img.interleave = 13;
    im->img.cskew = 13;

    simple_layout(im, &layout);
    return raw_open(im);
}

static bool_t dfs_open(struct image *im)
{
    const struct simple_layout layout = {
        .nr_sectors = 10,
        .is_fm = TRUE,
        .no = 1, /* 256-byte */
        .gap3 = 21,
        .base = { 0, 0 }
    };

    im->nr_cyls = 80;
    im->img.interleave = 1;
    im->img.cskew = 3;

    simple_layout(im, &layout);
    return raw_open(im);
}

static bool_t ssd_open(struct image *im)
{
    im->nr_sides = 1;
    return dfs_open(im);
}

static bool_t dsd_open(struct image *im)
{
    im->nr_sides = 2;
    return dfs_open(im);
}

static bool_t sdu_open(struct image *im)
{
    struct {
        uint8_t app[21], ver[5];
        uint16_t flags;
        uint16_t type;
        struct { uint16_t c, h, s; } max, used;
        uint16_t sec_size, trk_size;
    } header;
    struct simple_layout layout = dfl_simple_layout;

    /* Read basic (cyls, heads, spt) geometry from the image header. */
    F_read(&im->fp, &header, sizeof(header), NULL);
    im->nr_cyls = le16toh(header.max.c);
    im->nr_sides = le16toh(header.max.h);
    layout.nr_sectors = le16toh(header.max.s);

    /* Check the geometry. Accept 180k/360k/720k/1.44M/2.88M PC sizes. */
    if (((im->nr_cyls != 40) && (im->nr_cyls != 80))
        || ((im->nr_sides != 1) && (im->nr_sides != 2))
        || ((layout.nr_sectors != 9)
            && (layout.nr_sectors != 18)
            && (layout.nr_sectors != 36)))
        return FALSE;

    /* Fill in the rest of the geometry. */
    layout.no = 2; /* 512-byte sectors */
    layout.gap3 = 84; /* standard gap3 */
    im->img.interleave = 1; /* no interleave */

    /* Skip 46-byte SABDU header. */
    im->img.base_off = 46;

    simple_layout(im, &layout);
    return raw_open(im);
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
    unsigned int fsize = im_size(im);
    struct simple_layout layout = dfl_simple_layout;

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

    im->img.interleave = 4;
    im->img.cskew = 3;
    layout.no = 1;
    layout.base[0] = layout.base[1] = 0;
    im->img.layout = LAYOUT_sequential | LAYOUT_reverse_side(1);

    if ((fsize % (40*9)) == 0) {

        /* 9/18/36 sectors-per-track formats. */
        switch (fsize / (40*9)) {
        case 1: /* SSSD */
            im->nr_cyls = 40;
            im->nr_sides = 1;
            layout.nr_sectors = 9;
            layout.gap3 = 44;
            goto fm;
        case 2: /* DSSD (or SSDD) */
            if (have_vib && (vib.sides == 1)) {
                /* Disambiguated: This is SSDD. */
                im->nr_cyls = 40;
                im->nr_sides = 1;
                im->img.interleave = 5;
                layout.nr_sectors = 18;
                layout.gap3 = 24;
                goto mfm;
            }
            /* Assume DSSD. */
            im->nr_cyls = 40;
            im->nr_sides = 2;
            layout.nr_sectors = 9;
            layout.gap3 = 44;
            goto fm;
        case 4: /* DSDD (or DSSD80) */
            if (have_vib && (vib.tracks_per_side == 80)) {
                /* Disambiguated: This is DSSD80. */
                im->nr_cyls = 80;
                im->nr_sides = 2;
                layout.nr_sectors = 9;
                layout.gap3 = 44;
                goto fm;
            }
            /* Assume DSDD. */
            im->nr_cyls = 40;
            im->nr_sides = 2;
            im->img.interleave = 5;
            layout.nr_sectors = 18;
            layout.gap3 = 24;
            goto mfm;
        case 8: /* DSDD80 */
            im->nr_cyls = 80;
            im->nr_sides = 2;
            im->img.interleave = 5;
            layout.nr_sectors = 18;
            layout.gap3 = 24;
            goto mfm;
        case 16: /* DSHD80 */
            im->nr_cyls = 80;
            im->nr_sides = 2;
            im->img.interleave = 5;
            layout.nr_sectors = 36;
            layout.gap3 = 24;
            goto mfm;
        }

    } else if ((fsize % (40*16)) == 0) {

        /* SSDD/DSDD, 16 sectors */
        im->nr_sides = fsize / (40*16);
        if (im->nr_sides <= 2) {
            im->nr_cyls = 40;
            im->img.interleave = 5;
            layout.nr_sectors = 16;
            layout.gap3 = 44;
            goto mfm;
        }

    }

    return FALSE;

fm:
    layout.is_fm = TRUE;
mfm:
    simple_layout(im, &layout);
    return raw_open(im);
}

static bool_t uknc_open(struct image *im)
{
    struct raw_trk *trk;
    unsigned int i;
    bool_t ok;

    /* All tracks have special extra sync marks. */
    im->img.post_crc_syncs = 1;

    ok = raw_type_open(im, uknc_type);

    if (ok) {
        trk = im->img.trk_info;
        for (i = 0; i < im->nr_sides; i++) {
            /* All tracks have custom GAP2 and GAP4A. */
            trk->gap_2 = 24;
            trk->gap_4a = 27;
            trk++;
        }
    }

    return ok;
}

static bool_t jvc_open(struct image *im)
{
    struct jvc {
        uint8_t spt, sides, ssize_code, sec_id, attr;
    } jvc = {
        18, 1, 1, 1, 0
    };
    unsigned int bps, bpc;
    struct simple_layout layout = dfl_simple_layout;

    im->img.base_off = f_size(&im->fp) & 255;

    /* Check the image header. */
    F_read(&im->fp, &jvc,
           min_t(unsigned, im->img.base_off, sizeof(jvc)), NULL);
    if (jvc.attr || ((jvc.sides != 1) && (jvc.sides != 2)) || (jvc.spt == 0))
        return FALSE;

    im->nr_sides = jvc.sides;
    im->img.interleave = 3; /* RSDOS likes a 3:1 interleave (ref. xroar) */

    layout.no = jvc.ssize_code & 3;
    layout.base[0] = layout.base[1] = jvc.sec_id;
    layout.nr_sectors = jvc.spt;
    layout.gap3 = 20;
    layout.gap4a = 54;

    /* Calculate number of cylinders. */
    bps = 128 << layout.no;
    bpc = bps * layout.nr_sectors * im->nr_sides;
    im->nr_cyls = im_size(im) / bpc;
    if ((im->nr_cyls >= 88) && (im->nr_sides == 1)) {
        im->nr_sides++;
        im->nr_cyls /= 2;
        bpc *= 2;
    }
    if ((im_size(im) % bpc) >= bps)
        im->nr_cyls++;

    simple_layout(im, &layout);
    return raw_open(im);
}

static bool_t vdk_open(struct image *im)
{
    struct vdk {
        char id[2];
        uint16_t hlen;
        uint8_t misc[4];
        uint8_t cyls, heads;
        uint8_t flags, compression;
    } vdk;
    const struct simple_layout layout = {
        .nr_sectors = 18,
        .is_fm = FALSE,
        .has_iam = TRUE,
        .no = 1, /* 256-byte sectors */
        .gap3 = 20,
        .gap4a = 54,
        .base = { 1, 1 }
    };

    /* Check the image header. */
    F_read(&im->fp, &vdk, sizeof(vdk), NULL);
    if (strncmp(vdk.id, "dk", 2) || le16toh(vdk.hlen < 12))
        return FALSE;

    /* Read (cyls, heads) geometry from the image header. */
    im->nr_cyls = vdk.cyls;
    im->nr_sides = vdk.heads;

    /* Check the geometry. */
    if ((im->nr_sides != 1) && (im->nr_sides != 2))
        return FALSE;

    /* Fill in the rest of the geometry. */
    im->img.interleave = 2; /* DDOS likes a 2:1 interleave (ref. xroar) */

    im->img.base_off = le16toh(vdk.hlen);

    simple_layout(im, &layout);
    return raw_open(im);
}

/*
 * XDF:
 * The handling here is informed by xdfcopy.c in the fdutils distribution.
 */
struct xdf_format {
    unsigned int logical_sec_per_track; /* as reported by the Fat */
    unsigned int sec_per_track0, sec_per_trackN; /* physical sectors */
    unsigned int head1_shift_bc; /* effectively a head skew */
    struct {
        uint8_t no;   /* sector size code */
        uint8_t offs; /* offset (in 512-byte blocks) into image cyl data */
    } cylN_sec[2][4]; /* per-head, per-sector */
};

struct xdf_info {
    uint32_t *file_sec_offsets[4]; /* C0H0 C0H1 CnH0 CnH1 */
    const struct xdf_format *fmt;
    uint32_t cyl_bytes;
};

static bool_t xdf_check(const struct bpb *bpb)
{
    return (bpb->sig == 0xaa55)
        && (bpb->bytes_per_sec == 512)
        && (bpb->num_heads == 2)
        && (bpb->tot_sec == (2*80*bpb->sec_per_track));
}

static bool_t xdf_open(struct image *im)
{
    const static struct xdf_format formats[] = {
        { /* 3.5 HD */
            /* Cyl 0, head 0:
             * 1-8,129-139 (secs=19, interleave=2)
             * Sectors 1-8 (Aux FAT): Offsets 0x1800-0x2600 
             * Sectors 129-139 (Main FAT, Pt.1): Offsets 0x0000-0x1400 */
            /* Cyl 0, head 1:
             * 129-147 (secs=19, interleave=2)
             * Sector 129 (Main FAT, Pt.2): Offset 0x1600
             * Sectors 130-143 (RootDir): Offsets 0x2e00-0x4800 
             * Sectors 144-147 (Data): Offsets 0x5400-0x5a00 */
            /* Cyl N, head 0: 
             * 131(1k), 130(.5k), 132(2k), 134(8k)
             * Cyl N, head 1: Track slip of ~10k bitcells relative to head 0
             * 132(2k), 130(.5k), 131(1k), 134(8k)
             * Ordering of sectors in image (ID-Head):
             * 131-0, 132-0, 134-1, 130-0, 130-1, 134-0, 132-1, 131-1 */
            .logical_sec_per_track = 23,
            .sec_per_track0 = 19,
            .sec_per_trackN = 4,
            .head1_shift_bc = 10000,
            .cylN_sec = { 
                { {3, 0x00}, {2, 0x2c}, {4, 0x04}, {6, 0x30} }, /* Head 0 */
                { {4, 0x50}, {2, 0x2e}, {3, 0x58}, {6, 0x0c} }  /* Head 1 */
            }
        }
    };

    unsigned int i, j, rootdir_secs, fat_secs, img_curs, remain;
    struct xdf_info *xdf_info;
    const struct xdf_format *fmt;
    struct raw_sec *sec;
    struct raw_trk *trk;
    uint8_t *trk_map;
    struct bpb bpb;
    uint32_t *offs, *off;

    bpb_read(im, &bpb);
    if (!xdf_check(&bpb))
        return FALSE;

    fmt = formats;
    for (i = 0; i < ARRAY_SIZE(formats); i++)
        if (bpb.sec_per_track == fmt->logical_sec_per_track)
            goto found;
    return FALSE;

found:
    rootdir_secs = bpb.rootdir_ents/16;
    fat_secs = bpb.fat_secs;
    if (/* Rootdir must fill whole number of logical sectors */
        ((bpb.rootdir_ents & 15) != 0)
        /* Fat and Rootdir must fit in cylinder 0. */
        || (8 + 1 + fat_secs + rootdir_secs) > (2 * fmt->sec_per_track0))
        return FALSE;

    im->nr_sides = 2;
    im->nr_cyls = 80;

    /* Create four track layouts: C0H0 C0H1 CnH0 CnH1. */
    for (i = 0; i < 2; i++) {
        unsigned int aux_id = 1, main_id = 129;
        trk = add_track_layout(im, fmt->sec_per_track0, i);
        sec = &im->img.sec_info_base[trk->sec_off];
        for (j = 0; j < fmt->sec_per_track0; j++) {
            sec->id = (i == 0) && (j < 8) ? aux_id++ : main_id++;
            sec->no = 2;
            sec++;
        }
    }
    for (; i < 4; i++) {
        trk = add_track_layout(im, fmt->sec_per_trackN, i);
        sec = &im->img.sec_info_base[trk->sec_off];
        for (j = 0; j < fmt->sec_per_trackN; j++) {
            uint8_t n = fmt->cylN_sec[i-2][j].no;
            sec->id = n + 128;
            sec->no = n;
            sec++;
        }
    }

    /* Track map. */
    trk_map = add_track_map(im);
    *trk_map++ = 0;
    *trk_map++ = 1;
    for (i = 2; i < im->nr_cyls * im->nr_sides; i++)
        *trk_map++ = 2+(i&1);

    /* File sector offsets. */
    im->img.file_sec_offsets = (uint32_t *)align_p(im->img.sec_map)
        - 2*im->img.trk_info[0].nr_sectors;

    xdf_info = (struct xdf_info *)align_p(im->img.sec_map) - 1;
    trk = &im->img.trk_info[0];
    offs = off = (uint32_t *)xdf_info
        - 2*fmt->sec_per_track0 - 2*fmt->sec_per_trackN;
    check_p(offs, im);

    xdf_info->fmt = fmt;
    xdf_info->cyl_bytes = fmt->logical_sec_per_track * 2 * 512;

    /* Cyl 0 Image Layout (Thanks to fdutils/xdfcopy!):
     *   FS   Desc.    #secs-in-image  #secs-on-disk
     *   MAIN Boot     1               1
     *   MAIN Fat      fat_secs        fat_secs
     *   AUX  Fat      fat_secs        8
     *   MAIN RootDir  rootdir_secs    rootdir_secs
     *   AUX  Fat      5               0
     *   MAIN Data     *               * Notes:
     *  1. MAIN means sectors 129+ on head 0, followed by head 1.
     *  2. AUX means the dummy FAT on sectors 1-8 of head 0.
     *  3. Order on disk is AUX then MAIN. */
    xdf_info->file_sec_offsets[0] = off;
    xdf_info->file_sec_offsets[1] = off + fmt->sec_per_track0;
    /* 1. AUX Fat (limited to 8 sectors on disk). */
    img_curs = 1 + fat_secs; /* skip MAIN Boot+Fat */
    for (i = 0; i < 8; i++)
        *off++ = (img_curs + i) << 9;
    /* 2. MAIN Boot+Fat. */
    for (i = 0; i < (1 + fat_secs); i++)
        *off++ = i << 9;
    /* 3. MAIN RootDir. */
    img_curs += fat_secs; /* skip Aux FAT */
    for (i = 0; i < rootdir_secs; i++)
        *off++ = img_curs++ << 9;
    /* 4. MAIN Data. */
    img_curs += 5; /* skip AUX Fat duplicate */
    remain = 2*trk->nr_sectors - (off - offs);
    while (remain--)
        *off++ = img_curs++ << 9;

    /* Cyl N Image Layout: 
     *   Sectors are Interleaved on disk and in the image file. 
     *   This is described in a per-format offsets array. */
    xdf_info->file_sec_offsets[2] = off;
    xdf_info->file_sec_offsets[3] = off + fmt->sec_per_trackN;
    for (i = 0; i < 2; i++)
        for (j = 0; j < fmt->sec_per_trackN; j++)
        *off++ = (uint32_t)fmt->cylN_sec[i][j].offs << 8;

    return raw_open(im);
}

/* Sets up track delay and file sector-offsets table before calling 
 * generic routine. */
static void xdf_setup_track(
    struct image *im, uint16_t track, uint32_t *start_pos)
{
    struct xdf_info *xdf_info;
    unsigned int offs_sel;

    xdf_info = (struct xdf_info *)align_p(im->img.sec_map) - 1;

    im->img.track_delay_bc = 0;
    offs_sel = track & 1;

    if ((track>>1) == 0) {
        /* Cyl 0. */
        im->img.interleave = 2;
    } else {
        /* Cyl N. */
        im->img.interleave = 1;
        offs_sel += 2;
        if (track & 1)
            im->img.track_delay_bc = xdf_info->fmt->head1_shift_bc;
    }

    im->img.trk_off = (track>>1) * xdf_info->cyl_bytes;
    im->img.file_sec_offsets = xdf_info->file_sec_offsets[offs_sel];

    raw_setup_track(im, track, start_pos);
}

const struct image_handler img_image_handler = {
    .open = img_open,
    .setup_track = raw_setup_track,
    .read_track = raw_read_track,
    .rdata_flux = bc_rdata_flux,
    .write_track = raw_write_track,
};

const struct image_handler d81_image_handler = {
    .open = d81_open,
    .setup_track = raw_setup_track,
    .read_track = raw_read_track,
    .rdata_flux = bc_rdata_flux,
    .write_track = raw_write_track,
};

const struct image_handler st_image_handler = {
    .open = st_open,
    .setup_track = raw_setup_track,
    .read_track = raw_read_track,
    .rdata_flux = bc_rdata_flux,
    .write_track = raw_write_track,
};

const struct image_handler adfs_image_handler = {
    .open = adfs_open,
    .setup_track = raw_setup_track,
    .read_track = raw_read_track,
    .rdata_flux = bc_rdata_flux,
    .write_track = raw_write_track,
};

const struct image_handler atr_image_handler = {
    .open = atr_open,
    .setup_track = raw_setup_track,
    .read_track = raw_read_track,
    .rdata_flux = bc_rdata_flux,
    .write_track = raw_write_track,
};

const struct image_handler mbd_image_handler = {
    .open = mbd_open,
    .setup_track = raw_setup_track,
    .read_track = raw_read_track,
    .rdata_flux = bc_rdata_flux,
    .write_track = raw_write_track,
};

const struct image_handler mgt_image_handler = {
    .open = mgt_open,
    .setup_track = raw_setup_track,
    .read_track = raw_read_track,
    .rdata_flux = bc_rdata_flux,
    .write_track = raw_write_track,
};

const struct image_handler pc98fdi_image_handler = {
    .open = pc98fdi_open,
    .setup_track = raw_setup_track,
    .read_track = raw_read_track,
    .rdata_flux = bc_rdata_flux,
    .write_track = raw_write_track,
};

const struct image_handler pc98hdm_image_handler = {
    .open = pc98hdm_open,
    .setup_track = raw_setup_track,
    .read_track = raw_read_track,
    .rdata_flux = bc_rdata_flux,
    .write_track = raw_write_track,
};

const struct image_handler trd_image_handler = {
    .open = trd_open,
    .extend = raw_extend,
    .setup_track = raw_setup_track,
    .read_track = raw_read_track,
    .rdata_flux = bc_rdata_flux,
    .write_track = raw_write_track,
};

const struct image_handler opd_image_handler = {
    .open = opd_open,
    .setup_track = raw_setup_track,
    .read_track = raw_read_track,
    .rdata_flux = bc_rdata_flux,
    .write_track = raw_write_track,
};

const struct image_handler ssd_image_handler = {
    .open = ssd_open,
    .extend = raw_extend,
    .setup_track = raw_setup_track,
    .read_track = raw_read_track,
    .rdata_flux = bc_rdata_flux,
    .write_track = raw_write_track,
};

const struct image_handler dsd_image_handler = {
    .open = dsd_open,
    .extend = raw_extend,
    .setup_track = raw_setup_track,
    .read_track = raw_read_track,
    .rdata_flux = bc_rdata_flux,
    .write_track = raw_write_track,
};

const struct image_handler sdu_image_handler = {
    .open = sdu_open,
    .setup_track = raw_setup_track,
    .read_track = raw_read_track,
    .rdata_flux = bc_rdata_flux,
    .write_track = raw_write_track,
};

const struct image_handler jvc_image_handler = {
    .open = jvc_open,
    .setup_track = raw_setup_track,
    .read_track = raw_read_track,
    .rdata_flux = bc_rdata_flux,
    .write_track = raw_write_track,
};

const struct image_handler vdk_image_handler = {
    .open = vdk_open,
    .setup_track = raw_setup_track,
    .read_track = raw_read_track,
    .rdata_flux = bc_rdata_flux,
    .write_track = raw_write_track,
};

const struct image_handler ti99_image_handler = {
    .open = ti99_open,
    .setup_track = raw_setup_track,
    .read_track = raw_read_track,
    .rdata_flux = bc_rdata_flux,
    .write_track = raw_write_track,
};

const struct image_handler xdf_image_handler = {
    .open = xdf_open,
    .setup_track = xdf_setup_track,
    .read_track = raw_read_track,
    .rdata_flux = bc_rdata_flux,
    .write_track = raw_write_track,
};


/*
 * Generic Handlers
 */

static bool_t raw_open(struct image *im)
{
    im->img.rpm = im->img.rpm ?: 300;
    im->stk_per_rev = (stk_ms(200) * 300) / im->img.rpm;
    volume_cache_init(im->bufs.write_data.p + 1024,
                      im->img.heap_bottom);
    return TRUE;
}

static FSIZE_t raw_extend(struct image *im)
{
    unsigned int i, j, sz = im->img.base_off;
    struct raw_trk *trk;
    struct raw_sec *sec;

    for (i = 0; i < im->nr_cyls * im->nr_sides; i++) {
        trk = &im->img.trk_info[im->img.trk_map[i]];
        sec = &im->img.sec_info_base[trk->sec_off];
        for (j = 0; j < trk->nr_sectors; j++) {
            sz += sec_sz(sec->no);
            sec++;
        }
    }

    return sz;
}

static unsigned int file_idx(
    struct image *im, unsigned int cyl, unsigned int side)
{
    unsigned int _c, _s;
    _c = (im->img.layout & LAYOUT_reverse_side(side))
        ? im->nr_cyls - cyl - 1
        : cyl;
    _s = (im->img.layout & LAYOUT_sides_swapped)
        ? side ^ (im->nr_sides - 1)
        : side;
    return (im->img.layout & LAYOUT_sequential)
        ? (_s * im->nr_cyls) + _c
        : (_c * im->nr_sides) + _s;
}

static void raw_seek_track(
    struct image *im, uint16_t track, unsigned int cyl, unsigned int side)
{
    unsigned int i, j, k, pos, idx, off;
    struct raw_trk *trk;
    struct raw_sec *sec;

    im->cur_track = track;

    /* Update image structure with info for this track. */
    idx = cyl*im->nr_sides + side;
    trk = &im->img.trk_info[im->img.trk_map[cyl*im->nr_sides + side]];
    im->img.trk = trk;
    im->img.sec_info = &im->img.sec_info_base[trk->sec_off];

    /* Create logical sector map in rotational order. */
    memset(im->img.sec_map, 0xff, trk->nr_sectors);
    pos = ((cyl*im->img.cskew) + (side*im->img.hskew)) % trk->nr_sectors;
    for (i = 0; i < trk->nr_sectors; i++) {
        while (im->img.sec_map[pos] != 0xff)
            pos = (pos + 1) % trk->nr_sectors;
        im->img.sec_map[pos] = i;
        pos = (pos + im->img.interleave) % trk->nr_sectors;
    }

    /* Sort out all other logical layout issues. */
    if (trk->is_fm) {
        fm_prep_track(im);
    } else {
        mfm_prep_track(im);
    }

    if (im->img.file_sec_offsets == NULL) {
        /* Find offset of track data in the image file. */
        idx = file_idx(im, cyl, side);
        off = im->img.base_off;
        for (i = 0; i < im->nr_cyls; i++) {
            for (j = 0; j < im->nr_sides; j++) {
                if (file_idx(im, i, j) >= idx)
                    continue;
                trk = &im->img.trk_info[im->img.trk_map[i*im->nr_sides + j]];
                sec = &im->img.sec_info_base[trk->sec_off];
                for (k = 0; k < trk->nr_sectors; k++) {
                    off += sec_sz(sec->no);
                    sec++;
                }
            }
        }
        im->img.trk_off = off;
    }
}

static uint32_t calc_start_pos(struct image *im)
{
    uint32_t decode_off;
    int32_t bc;

    bc = im->cur_bc - im->img.track_delay_bc;
    if (bc < 0)
        bc += im->tracklen_bc;

    im->img.crc = 0xffff;
    im->img.trk_sec = im->img.rd_sec_pos = im->img.decode_data_pos = 0;
    decode_off = bc / 16;
    if (decode_off < im->img.idx_sz) {
        /* Post-index track gap */
        im->img.decode_pos = 0;
    } else {
        struct raw_trk *trk = im->img.trk;
        uint8_t *sec_map = im->img.sec_map;
        unsigned int i, ess;
        struct raw_sec *sec;
        decode_off -= im->img.idx_sz;
        for (i = 0; i < trk->nr_sectors; i++) {
            sec = &im->img.sec_info[*sec_map++];
            ess = enc_sec_sz(im, sec);
            if (decode_off < ess)
                break;
            decode_off -= ess;
        }
        if (i < trk->nr_sectors) {
            /* IDAM */
            im->img.trk_sec = i;
            im->img.decode_pos = i * 4 + 1;
            if (decode_off >= im->img.idam_sz) {
                /* DAM */
                decode_off -= im->img.idam_sz;
                im->img.decode_pos++;
                if (decode_off >= im->img.dam_sz_pre) {
                    /* Data or Post Data */
                    decode_off -= im->img.dam_sz_pre;
                    im->img.decode_pos++;
                    if (decode_off < sec_sz(sec->no)) {
                        /* Data */
                        im->img.rd_sec_pos = decode_off / 1024;
                        im->img.decode_data_pos = im->img.rd_sec_pos;
                        decode_off %= 1024;
                    } else {
                        /* Post Data */
                        decode_off -= sec_sz(sec->no);
                        im->img.decode_pos++;
                        /* Start fetch at next sector. */
                        im->img.trk_sec = (i + 1) % trk->nr_sectors;
                    }
                }
            }
        } else {
            /* Pre-index track gap */
            im->img.decode_pos = trk->nr_sectors * 4 + 1;
            im->img.decode_data_pos = decode_off / 1024;
            decode_off %= 1024;
        }
    }

    return decode_off;
}

static void raw_setup_track(
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
        raw_seek_track(im, track, cyl, side);

    im->img.write_sector = -1;

    im->cur_bc = (sys_ticks * 16) / im->ticks_per_cell;
    im->cur_bc &= ~15;
    if (im->cur_bc >= im->tracklen_bc)
        im->cur_bc = 0;
    im->cur_ticks = im->cur_bc * im->ticks_per_cell;
    im->ticks_since_flux = 0;

    decode_off = calc_start_pos(im);

    rd->prod = rd->cons = 0;
    bc->prod = bc->cons = 0;

    if (start_pos) {
        image_read_track(im);
        bc->cons = decode_off * 16;
        *start_pos = sys_ticks;
    }
}

void process_data(struct image *im, void *p, unsigned int len)
{
    /* Pointer and size should be 4-byte aligned. */
    ASSERT(!((len|(uint32_t)p)&3));

    if (im->img.trk->invert_data) {
        uint32_t *_p = p, *_q = _p + len/4;
        while (_p != _q) {
            *_p = ~*_p;
            _p++;
        }
    }
}

static bool_t raw_read_track(struct image *im)
{
    return (im->sync == SYNC_fm) ? fm_read_track(im) : mfm_read_track(im);
}

static bool_t raw_write_track(struct image *im)
{
    const uint8_t mfm_dam_header[] = { 0xa1, 0xa1, 0xa1, 0xfb };

    bool_t flush;
    struct raw_trk *trk = im->img.trk;
    struct write *write = get_write(im, im->wr_cons);
    struct image_buf *wr = &im->bufs.write_bc;
    uint16_t *buf = wr->p;
    unsigned int bufmask = (wr->len / 2) - 1;
    uint8_t *wrbuf = im->bufs.write_data.p;
    uint32_t c = wr->cons / 16, p = wr->prod / 16;
    struct raw_sec *sec;
    unsigned int i, off;
    time_t t;
    uint16_t crc, sec_sz;
    uint8_t id, x, *sec_map;
    int32_t base;

    /* If we are processing final data then use the end index, rounded up. */
    barrier();
    flush = (im->wr_cons != im->wr_bc);
    if (flush)
        p = (write->bc_end + 15) / 16;

    if (im->img.write_sector == -1) {
        base = write->start / im->ticks_per_cell; /* in data bytes */
        sec_map = im->img.sec_map;

        base -= im->img.track_delay_bc;
        if (base < 0)
            base += im->tracklen_bc;

        /* Convert write offset to sector number (in rotational order). */
        base -= im->img.idx_sz + im->img.idam_sz;
        for (i = 0; i < trk->nr_sectors; i++) {
            /* Within small range of expected data start? */
            if ((base >= -64) && (base <= 64))
                break;
            base -= enc_sec_sz(im, &im->img.sec_info[*sec_map++]);
        }

        /* Convert rotational order to logical order. */
        if (i >= trk->nr_sectors) {
            printk("IMG Bad Sector Offset: %u -> %u\n", base, i);
            im->img.write_sector = -2;
        } else {
            im->img.write_sector = *sec_map;
        }
    }

    for (;;) {

        sec_sz = (im->img.write_sector >= 0)
            ? sec_sz(im->img.sec_info[im->img.write_sector].no) : 128;

        if (im->sync == SYNC_fm) {

            uint16_t sync;
            if ((int16_t)(p - c) < (2 + sec_sz + 2))
                break;
            if (buf[c++ & bufmask] != 0xaaaa)
                continue;
            sync = buf[c & bufmask];
            if (mfmtobin(sync >> 1) != FM_SYNC_CLK)
                continue;
            x = mfmtobin(sync);
            c++;

        } else { /* MFM */

            if ((int16_t)(p - c) < (3 + sec_sz + 2))
                break;
            /* Scan for sync words and IDAM. Because of the way we sync we
             * expect to see only 2*4489 and thus consume only 3 words for the
             * header. */
            if (be16toh(buf[c++ & bufmask]) != 0x4489)
                continue;
            for (i = 0; i < 2; i++)
                if ((x = mfmtobin(buf[c++ & bufmask])) != 0xa1)
                    break;

        }

        switch (x) {

        case 0xfe: /* IDAM */
            if (im->sync == SYNC_fm) {
                wrbuf[0] = x;
                for (i = 1; i < 7; i++)
                    wrbuf[i] = mfmtobin(buf[c++ & bufmask]);
                id = wrbuf[3];
            } else { /* MFM */
                for (i = 0; i < 3; i++)
                    wrbuf[i] = 0xa1;
                wrbuf[i++] = x;
                for (; i < 10; i++)
                    wrbuf[i] = mfmtobin(buf[c++ & bufmask]);
                id = wrbuf[6];
            }
            crc = crc16_ccitt(wrbuf, i, 0xffff);
            if (crc != 0) {
                printk("IMG IDAM Bad CRC %04x, sector %u\n", crc, id);
                break;
            }
            /* Search by sector id for this sector's logical order. */
            for (i = 0, sec = im->img.sec_info;
                 (i < trk->nr_sectors) && (sec->id != id);
                 i++, sec++)
                continue;
            im->img.write_sector = i;
            if (i >= trk->nr_sectors) {
                printk("IMG IDAM Bad Sector: %02x\n", id);
                im->img.write_sector = -2;
            }
            break;

        case 0xfb: /* DAM */ {
            unsigned int nr, todo;

            if (im->img.write_sector < 0) {
                printk("IMG DAM for unknown sector (%d)\n",
                       im->img.write_sector);
                c += sec_sz + 2;
                break;
            }

            crc = (im->sync == SYNC_fm)
                ? crc16_ccitt(&x, 1, 0xffff)
                : crc16_ccitt(mfm_dam_header, 4, 0xffff);

            sec = &im->img.sec_info[im->img.write_sector];
            printk("Write %u[%02x]/%u... ", im->img.write_sector,
                   sec->id, trk->nr_sectors);
            t = time_now();

            sec = im->img.sec_info;
            if (im->img.file_sec_offsets) {
                off = im->img.file_sec_offsets[im->img.write_sector];
            } else {
                for (i = off = 0; i < im->img.write_sector; i++)
                    off += sec_sz(sec++->no);
            }
            F_lseek(&im->fp, im->img.trk_off + off);

            for (todo = sec_sz; todo != 0; todo -= nr) {
                nr = min_t(unsigned int, todo, 1024);
                mfm_ring_to_bin(buf, bufmask, c, wrbuf, nr);
                c += nr;
                crc = crc16_ccitt(wrbuf, nr, crc);
                process_data(im, wrbuf, nr);
                F_write(&im->fp, wrbuf, nr, NULL);
            }

            printk("%u us\n", time_diff(t, time_now()) / TIME_MHZ);

            mfm_ring_to_bin(buf, bufmask, c, wrbuf, 2);
            c += 2;
            crc = crc16_ccitt(wrbuf, 2, crc);
            if (crc != 0) {
                printk("IMG Bad CRC %04x, sector %u[%02x]\n",
                       crc, im->img.write_sector, sec->id);
            }

            break;
        }

        }
    }

    wr->cons = c * 16;

    return flush;
}

static void raw_dump_info(struct image *im)
{
    struct raw_trk *trk = im->img.trk;
    unsigned int i;

    if (!verbose_image_log)
        return;

    printk("C%u S%u:: %s %u-%u-%u:\n",
           im->cur_track/2, im->cur_track&1,
           (im->sync == SYNC_fm) ? "FM" : "MFM",
           im->nr_cyls, im->nr_sides, trk->nr_sectors);
    printk(" rpm: %u, tracklen: %u, datarate: %u\n",
           im->img.rpm, im->tracklen_bc, trk->data_rate);
    printk(" gap2: %u, gap3: %u, gap4a: %u, gap4: %u\n",
           trk->gap_2, trk->gap_3, trk->gap_4a, im->img.gap_4);
    printk(" ticks_per_cell: %u, write_bc_ticks: %u, has_iam: %u\n",
           im->ticks_per_cell, im->write_bc_ticks, trk->has_iam);
    printk(" interleave: %u, cskew %u, hskew %u\n ",
           im->img.interleave, im->img.cskew, im->img.hskew);
    printk(" file-layout: %x\n", im->img.layout);
    for (i = 0; i < trk->nr_sectors; i++) {
        struct raw_sec *sec = &im->img.sec_info[im->img.sec_map[i]];
        printk("{%u,%u} ", sec->id, sec->no);
    }
    printk("\n");
}

static void img_fetch_data(struct image *im)
{
    struct image_buf *rd = &im->bufs.read_data;
    uint8_t *buf = rd->p;
    struct raw_sec *sec, *s;
    uint8_t sec_i;
    uint16_t off, len;

    if (rd->prod != rd->cons)
        return;

    sec_i = im->img.sec_map[im->img.trk_sec];
    sec = &im->img.sec_info[sec_i];

    if (im->img.file_sec_offsets) {
        off = im->img.file_sec_offsets[sec_i];
    } else {
        off = 0;
        for (s = im->img.sec_info; s != sec; s++)
            off += sec_sz(s->no);
    }

    len = sec_sz(sec->no);

    off += im->img.rd_sec_pos * 1024;
    len -= im->img.rd_sec_pos * 1024;

    if (len > 1024) {
        len = 1024;
        im->img.rd_sec_pos++;
    } else {
        im->img.rd_sec_pos = 0;
        if (++im->img.trk_sec >= im->img.trk->nr_sectors)
            im->img.trk_sec = 0;
    }

    F_lseek(&im->fp, im->img.trk_off + off);
    F_read(&im->fp, buf, len, NULL);
    process_data(im, buf, len);

    rd->prod++;
}

static void *align_p(void *p)
{
    return (void *)((uint32_t)p&~3);
}

static void check_p(void *p, struct image *im)
{
    uint8_t *a = p, *b = (uint8_t *)im->bufs.read_data.p;
    if ((int32_t)(a-b) < 1024)
        F_die(FR_BAD_IMAGE);
    im->img.heap_bottom = p;
}

static struct raw_trk *add_track_layout(
    struct image *im, unsigned int nr_sectors, unsigned int trk_idx)
{
    struct raw_sec *sec;
    struct raw_trk *trk;
    void *p;
    unsigned int i;

    if ((im->nr_sides < 1) || (im->nr_sides > 2)
        || (im->nr_cyls < 1) || (im->nr_cyls > 254)
        || (nr_sectors < 1) || (nr_sectors > 256))
        F_die(FR_BAD_IMAGE);

    if (!im->img.trk_info) {
        p = (uint8_t *)im->bufs.read_data.p + im->bufs.read_data.len;
        p = align_p(p);
        im->img.sec_info_base = p;
        im->img.trk_info = p;
    }

    sec = im->img.sec_info_base - nr_sectors;
    trk = (struct raw_trk *)align_p(sec) - trk_idx - 1;
    check_p(trk, im);

    memcpy(trk, im->img.trk_info, trk_idx * sizeof(*trk));
    for (i = 0; i < trk_idx; i++)
        trk[i].sec_off += nr_sectors;
    memset(&trk[i], 0, sizeof(*trk));
    trk[i].nr_sectors = nr_sectors;

    im->img.sec_info_base = sec;
    im->img.trk_info = trk;

    return &trk[i];
}

/* Create track map. This maps track# to a track-info structure. */
static uint8_t *add_track_map(struct image *im)
{
    uint8_t *trk_map, *sec_map;
    struct raw_sec *top, *sec;

    top = align_p((uint8_t *)im->bufs.read_data.p + im->bufs.read_data.len);
    sec = im->img.sec_info_base;
    while (sec < top) {
        if (sec->no > 6)
            F_die(FR_BAD_IMAGE);
        sec++;
    }

    trk_map = (uint8_t *)im->img.trk_info - im->nr_cyls * im->nr_sides;
    sec_map = trk_map - 256;
    check_p(sec_map, im);

    im->img.trk_map = trk_map;
    im->img.sec_map = sec_map;

    return trk_map;
}

static void simple_layout(struct image *im, const struct simple_layout *layout)
{
    struct raw_sec *sec;
    struct raw_trk *trk;
    uint8_t *trk_map;
    unsigned int i, j;

    /* Create a track layout per side. */
    for (i = 0; i < im->nr_sides; i++) {
        trk = add_track_layout(im, layout->nr_sectors, i);
        trk->is_fm = layout->is_fm;
        trk->has_iam = layout->has_iam;
        trk->gap_3 = layout->gap3;
        trk->gap_4a = layout->gap4a;
        sec = &im->img.sec_info_base[trk->sec_off];
        for (j = 0; j < layout->nr_sectors; j++) {
            sec->id = j + layout->base[i];
            sec->no = layout->no;
            sec++;
        }
    }

    /* Create track map, mapping each side to its respective layout. */
    trk_map = add_track_map(im);
    for (i = 0; i < im->nr_cyls; i++)
        for (j = 0; j < im->nr_sides; j++)
            *trk_map++ = j;
}


/*
 * MFM-Specific Handlers
 */

#define GAP_1    50 /* Post-IAM */
#define GAP_2    22 /* Post-IDAM */
#define GAP_4A   80 /* Post-Index */
#define GAP_SYNC 12

/* Shrink the IDAM pre-sync gap if sectors are close together. */
#define idam_gap_sync(im) min_t(uint8_t, (im)->img.trk->gap_3, GAP_SYNC)

static void mfm_prep_track(struct image *im)
{
    const uint8_t GAP_3[] = { 32, 54, 84, 116, 255, 255, 255, 255 };
    struct raw_trk *trk = im->img.trk;
    uint32_t tracklen;
    uint8_t gap_3 = trk->gap_3;
    unsigned int i;

    trk->gap_2 = trk->gap_2 ?: GAP_2;
    /* GAP_SYNC is a suitable small initial guess for auto GAP3. */
    trk->gap_3 = trk->gap_3 ?: GAP_SYNC;
    trk->gap_4a = trk->gap_4a ?: GAP_4A;

    im->img.idx_sz = trk->gap_4a;
    if (trk->has_iam)
        im->img.idx_sz += GAP_SYNC + 4 + GAP_1;
    im->img.idam_sz = idam_gap_sync(im) + 8 + 2 + trk->gap_2;
    im->img.dam_sz_pre = GAP_SYNC + 4;
    im->img.dam_sz_post = 2 + trk->gap_3;

    im->img.idam_sz += im->img.post_crc_syncs;
    im->img.dam_sz_post += im->img.post_crc_syncs;

    /* Work out minimum track length (with no pre-index track gap). */
    tracklen = im->img.idx_sz;
    for (i = 0; i < trk->nr_sectors; i++)
        tracklen += enc_sec_sz(im, &im->img.sec_info[i]);
    tracklen *= 16;

    if (trk->data_rate == 0) {
        /* Infer the data rate. */
        for (i = 1; i < 3; i++) { /* DD=1, HD=2, ED=3 */
            uint32_t maxlen = (((50000u * 300) / im->img.rpm) << i) + 5000;
            if (tracklen < maxlen)
                break;
        }
        trk->data_rate = 125u << i; /* DD=250, HD=500, ED=1000 */
    }

    /* Calculate standard track length from data rate and RPM. */
    im->tracklen_bc = (trk->data_rate * 400 * 300) / im->img.rpm;

    /* Calculate a suitable GAP3 if not specified. */
    if (gap_3 == 0) {
        int space;
        uint8_t no = trk->nr_sectors ? im->img.sec_info[0].no : 2;
        im->img.dam_sz_post -= trk->gap_3;
        tracklen -= 16 * trk->nr_sectors * trk->gap_3;
        space = max_t(int, 0, im->tracklen_bc - tracklen);
        trk->gap_3 = min_t(int, space/(16*trk->nr_sectors), GAP_3[no]);
        im->img.dam_sz_post += trk->gap_3;
        tracklen += 16 * trk->nr_sectors * trk->gap_3;
    }

    /* Does the track data fit within standard track length? */
    if (im->tracklen_bc < tracklen) {
        if ((tracklen - trk->gap_4a*16) <= im->tracklen_bc) {
            /* Eliminate the post-index gap 4a if that suffices. */
            tracklen -= trk->gap_4a*16;
            im->img.idx_sz -= trk->gap_4a;
            trk->gap_4a = 0;
        } else {
            /* Extend the track length ("long track"). */
            im->tracklen_bc = tracklen + 100;
        }
    }

    /* Round the track length up to a multiple of 32 bitcells. */
    im->tracklen_bc = (im->tracklen_bc + 31) & ~31;

    im->ticks_per_cell = ((sysclk_stk(im->stk_per_rev) * 16u)
                          / im->tracklen_bc);
    im->img.gap_4 = (im->tracklen_bc - tracklen) / 16;

    im->write_bc_ticks = sysclk_us(500) / trk->data_rate;

    im->sync = SYNC_mfm;

    raw_dump_info(im);
}

static bool_t mfm_read_track(struct image *im)
{
    struct image_buf *rd = &im->bufs.read_data;
    struct image_buf *bc = &im->bufs.read_bc;
    struct raw_trk *trk = im->img.trk;
    uint8_t *buf = rd->p;
    uint16_t *bc_b = bc->p;
    uint32_t bc_len, bc_mask, bc_space, bc_p, bc_c;
    uint16_t pr, crc;
    unsigned int i;

    img_fetch_data(im);

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

    if (im->img.decode_pos == 0) {
        /* Post-index track gap */
        if (bc_space < im->img.idx_sz)
            return FALSE;
        for (i = 0; i < trk->gap_4a; i++)
            emit_byte(0x4e);
        if (trk->has_iam) {
            /* IAM */
            for (i = 0; i < GAP_SYNC; i++)
                emit_byte(0x00);
            for (i = 0; i < 3; i++)
                emit_raw(0x5224);
            emit_byte(0xfc);
            for (i = 0; i < GAP_1; i++)
                emit_byte(0x4e);
        }
    } else if (im->img.decode_pos == (trk->nr_sectors * 4 + 1)) {
        /* Pre-index track gap */
        uint16_t sz = im->img.gap_4 - im->img.decode_data_pos * 1024;
        if (bc_space < min_t(unsigned int, sz, 1024))
            return FALSE;
        if (sz > 1024) {
            sz = 1024;
            im->img.decode_data_pos++;
            im->img.decode_pos--;
        } else {
            im->img.decode_data_pos = 0;
            im->img.decode_pos = (im->img.idx_sz != 0) ? -1 : 0;
        }
        for (i = 0; i < sz; i++)
            emit_byte(0x4e);
    } else {
        struct raw_sec *sec = &im->img.sec_info[im->img.sec_map[(
                    im->img.decode_pos-1)>>2]];
        switch ((im->img.decode_pos - 1) & 3) {
        case 0: /* IDAM */ {
            uint8_t cyl = im->cur_track/2, hd = im->cur_track&1;
            uint8_t idam[8] = { 0xa1, 0xa1, 0xa1, 0xfe, cyl, hd,
                                sec->id, sec->no };
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
            for (i = 0; i < im->img.post_crc_syncs; i++)
                emit_raw(0x4489);
            for (i = 0; i < trk->gap_2; i++)
                emit_byte(0x4e);
            break;
        }
        case 1: /* DAM */ {
            uint8_t dam[4] = { 0xa1, 0xa1, 0xa1, 0xfb };
            if (bc_space < im->img.dam_sz_pre)
                return FALSE;
            for (i = 0; i < GAP_SYNC; i++)
                emit_byte(0x00);
            for (i = 0; i < 3; i++)
                emit_raw(0x4489);
            emit_byte(dam[3]);
            im->img.crc = crc16_ccitt(dam, sizeof(dam), 0xffff);
            break;
        }
        case 2: /* Data */ {
            uint16_t sec_sz = sec_sz(sec->no);
            sec_sz -= im->img.decode_data_pos * 1024;
            if (bc_space < min_t(unsigned int, sec_sz, 1024))
                return FALSE;
            if (sec_sz > 1024) {
                sec_sz = 1024;
                im->img.decode_data_pos++;
                im->img.decode_pos--;
            } else {
                im->img.decode_data_pos = 0;
            }
            for (i = 0; i < sec_sz; i++)
                emit_byte(buf[i]);
            im->img.crc = crc16_ccitt(buf, sec_sz, im->img.crc);
            rd->cons++;
            break;
        }
        case 3: /* Post Data */ {
            if (bc_space < im->img.dam_sz_post)
                return FALSE;
            crc = im->img.crc;
            emit_byte(crc >> 8);
            emit_byte(crc);
            for (i = 0; i < im->img.post_crc_syncs; i++)
                emit_raw(0x4489);
            for (i = 0; i < trk->gap_3; i++)
                emit_byte(0x4e);
            break;
        }
        }
    }

#undef emit_raw
#undef emit_byte

    im->img.decode_pos++;
    bc->prod = bc_p * 16;

    return TRUE;
}


/*
 * FM-Specific Handlers
 */

#define FM_GAP_1 26 /* Post-IAM */
#define FM_GAP_2 11 /* Post-IDAM */
#define FM_GAP_SYNC 6

static void fm_prep_track(struct image *im)
{
    const uint8_t GAP_3[] = { 27, 42, 58, 138, 255, 255, 255, 255 };
    struct raw_trk *trk = im->img.trk;
    uint32_t tracklen;
    unsigned int i;

    trk->gap_2 = trk->gap_2 ?: FM_GAP_2;
    /* Default post-index gap size depends on whether the track format includes 
     * IAM or not (see uPD765A/7265 Datasheet). */
    trk->gap_4a = trk->gap_4a ?: trk->has_iam ? 40 : 16;

    im->img.idx_sz = trk->gap_4a;
    if (trk->has_iam)
        im->img.idx_sz += FM_GAP_SYNC + 1 + FM_GAP_1;
    im->img.idam_sz = FM_GAP_SYNC + 5 + 2 + trk->gap_2;
    im->img.dam_sz_pre = FM_GAP_SYNC + 1;
    im->img.dam_sz_post = 2 + trk->gap_3;

    /* Work out minimum track length (with no pre-index track gap). */
    tracklen = im->img.idx_sz;
    for (i = 0; i < trk->nr_sectors; i++)
        tracklen += enc_sec_sz(im, &im->img.sec_info[i]);
    tracklen *= 16;

    /* Calculate data rate and track length. */
    trk->data_rate = trk->data_rate ?: 125; /* SD */
    im->tracklen_bc = (trk->data_rate * 400 * 300) / im->img.rpm;

    /* Calculate a suitable GAP3 if not specified. */
    if (trk->gap_3 == 0) {
        int space = max_t(int, 0, im->tracklen_bc - tracklen);
        uint8_t no = trk->nr_sectors ? im->img.sec_info[0].no : 2;
        trk->gap_3 = min_t(int, space/(16*trk->nr_sectors), GAP_3[no]);
        im->img.dam_sz_post += trk->gap_3;
        tracklen += 16 * trk->nr_sectors * trk->gap_3;
    }

    /* Round the track length up to fit the data and be a multiple of 32. */
    im->tracklen_bc = max_t(uint32_t, im->tracklen_bc, tracklen);
    im->tracklen_bc = (im->tracklen_bc + 31) & ~31;

    im->ticks_per_cell = ((sysclk_stk(im->stk_per_rev) * 16u)
                          / im->tracklen_bc);
    im->img.gap_4 = (im->tracklen_bc - tracklen) / 16;

    im->write_bc_ticks = sysclk_us(500) / trk->data_rate;

    im->sync = SYNC_fm;

    raw_dump_info(im);
}

uint16_t fm_sync(uint8_t dat, uint8_t clk)
{
    uint16_t _dat = mfmtab[dat] & 0x5555;
    uint16_t _clk = (mfmtab[clk] & 0x5555) << 1;
    return _clk | _dat;
}

static bool_t fm_read_track(struct image *im)
{
    struct image_buf *rd = &im->bufs.read_data;
    struct image_buf *bc = &im->bufs.read_bc;
    struct raw_trk *trk = im->img.trk;
    uint8_t *buf = rd->p;
    uint16_t crc, *bc_b = bc->p;
    uint32_t bc_len, bc_mask, bc_space, bc_p, bc_c;
    unsigned int i;

    img_fetch_data(im);

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
        for (i = 0; i < trk->gap_4a; i++)
            emit_byte(0xff);
        if (trk->has_iam) {
            /* IAM */
            for (i = 0; i < FM_GAP_SYNC; i++)
                emit_byte(0x00);
            emit_raw(fm_sync(0xfc, 0xd7));
            for (i = 0; i < FM_GAP_1; i++)
                emit_byte(0xff);
        }
    } else if (im->img.decode_pos == (trk->nr_sectors * 4 + 1)) {
        /* Pre-index track gap */
        uint16_t sz = im->img.gap_4 - im->img.decode_data_pos * 1024;
        if (bc_space < min_t(unsigned int, sz, 1024))
            return FALSE;
        if (sz > 1024) {
            sz = 1024;
            im->img.decode_data_pos++;
            im->img.decode_pos--;
        } else {
            im->img.decode_data_pos = 0;
            im->img.decode_pos = (im->img.idx_sz != 0) ? -1 : 0;
        }
        for (i = 0; i < sz; i++)
            emit_byte(0xff);
    } else {
        struct raw_sec *sec = &im->img.sec_info[im->img.sec_map[(
                    im->img.decode_pos-1)>>2]];
        switch ((im->img.decode_pos - 1) & 3) {
        case 0: /* IDAM */ {
            uint8_t cyl = im->cur_track/2, hd = im->cur_track&1;
            uint8_t idam[5] = { 0xfe, cyl, hd, sec->id, sec->no };
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
            for (i = 0; i < trk->gap_2; i++)
                emit_byte(0xff);
            break;
        }
        case 1: /* DAM */ {
            uint8_t dam[1] = { 0xfb };
            if (bc_space < im->img.dam_sz_pre)
                return FALSE;
            for (i = 0; i < FM_GAP_SYNC; i++)
                emit_byte(0x00);
            emit_raw(fm_sync(dam[0], FM_SYNC_CLK));
            im->img.crc = crc16_ccitt(dam, sizeof(dam), 0xffff);
            break;
        }
        case 2: /* Data */ {
            uint16_t sec_sz = sec_sz(sec->no);
            sec_sz -= im->img.decode_data_pos * 1024;
            if (bc_space < min_t(unsigned int, sec_sz, 1024))
                return FALSE;
            if (sec_sz > 1024) {
                sec_sz = 1024;
                im->img.decode_data_pos++;
                im->img.decode_pos--;
            } else {
                im->img.decode_data_pos = 0;
            }
            for (i = 0; i < sec_sz; i++)
                emit_byte(buf[i]);
            im->img.crc = crc16_ccitt(buf, sec_sz, im->img.crc);
            rd->cons++;
            break;
        }
        case 3: /* Post Data */ {
            if (bc_space < im->img.dam_sz_post)
                return FALSE;
            crc = im->img.crc;
            emit_byte(crc >> 8);
            emit_byte(crc);
            for (i = 0; i < trk->gap_3; i++)
                emit_byte(0xff);
            break;
        }
        }
    }

#undef emit_raw
#undef emit_byte

    im->img.decode_pos++;
    bc->prod = bc_p * 16;

    return TRUE;
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

/*
 * scp.c
 * 
 * SuperCard Pro (SCP) flux files.
 * 
 * Written & released by Keir Fraser <keir.xen@gmail.com>
 * 
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

struct scp_header {
    uint8_t sig[3]; /* 'SCP' */
    uint8_t version;
    uint8_t disk_type;
    uint8_t nr_revs;
    uint8_t start_trk;
    uint8_t end_trk;
    uint8_t flags;
    uint8_t bc_enc;
    uint8_t heads;
    uint8_t rsvd;
    uint32_t csum;
};

struct trk_header {
    uint8_t sig[3]; /* 'TRK' */
    uint8_t track;
    struct {
        uint32_t duration;
        uint32_t nr_flux;
        uint32_t dat_off;
    } rev[5];
};

#define FF_MHZ (SYSCLK_MHZ*16u)
#define SCP_MHZ 40u

static bool_t scp_open(struct image *im)
{
    struct scp_header header;
    UINT nr;

    im->fr = f_read(&im->fp, &header, sizeof(header), &nr);
    if (im->fr || strncmp((char *)header.sig, "SCP", 3)) {
        printk("Not a SCP file\n");
        return FALSE;
    }

    if (header.nr_revs == 0) {
        printk("Invalid revolution count (%u)\n", header.nr_revs);
        return FALSE;
    }

    if (header.bc_enc != 0 && header.bc_enc != 16) {
        printk("Unsupported bit cell time width (%u)\n", header.bc_enc);
        return FALSE;
    }

    im->scp.nr_revs = header.nr_revs;
    im->nr_tracks = header.end_trk - header.start_trk + 1;

    return TRUE;
}

static bool_t scp_seek_track(struct image *im, uint8_t track)
{
    struct trk_header header;
    uint32_t hdr_offset, i, j;
    UINT nr;

    /* TODO: Fake out unformatted tracks. */
    track = min_t(uint8_t, track, im->nr_tracks-1);

    hdr_offset = 0x10 + track*4;
    if ((im->fr = f_lseek(&im->fp, hdr_offset))
        || (im->fr = f_read(&im->fp, &hdr_offset, 4, &nr)))
        return FALSE;

    hdr_offset = le32toh(hdr_offset);
    if ((im->fr = f_lseek(&im->fp, hdr_offset))
        || (im->fr = f_read(&im->fp, &header, sizeof(header), &nr)))
        return FALSE;

    if (strncmp((char *)header.sig, "TRK", 3) || header.track != track)
        return FALSE;

    for (i = 0; i < ARRAY_SIZE(im->scp.rev); i++) {
        j = i % im->scp.nr_revs;
        im->scp.rev[i].dat_off = hdr_offset + le32toh(header.rev[j].dat_off);
        im->scp.rev[i].nr_dat = le32toh(header.rev[j].nr_flux);
    }

    im->scp.pf_rev = im->scp.ld_rev = 0;
    im->scp.pf_pos = im->scp.ld_pos = 0;
    im->cons = im->prod = 0;
    im->ticks_since_flux = 0;
    im->cur_ticks = 0;
    im->cur_track = track;

    return TRUE;
}

static void scp_prefetch_data(struct image *im)
{
    UINT _nr, nr, nr_flux = im->scp.rev[im->scp.pf_rev].nr_dat;
    uint16_t *buf = (uint16_t *)im->buf;

    if ((uint32_t)(im->prod - im->cons) > (sizeof(im->buf)-512)/2)
        return;

    f_lseek(&im->fp, im->scp.rev[im->scp.pf_rev].dat_off + im->scp.pf_pos*2);
    nr = min_t(UINT, 512, (nr_flux - im->scp.pf_pos) * 2);
    nr = min_t(UINT, nr, sizeof(im->buf) - ((im->prod*2) % sizeof(im->buf)));
    f_read(&im->fp, &buf[im->prod % (sizeof(im->buf)/2)], nr, &_nr);
    ASSERT(nr == _nr);
    im->prod += nr/2;
    im->scp.pf_pos += nr/2;
    if (im->scp.pf_pos >= nr_flux) {
        ASSERT(im->scp.pf_pos == nr_flux);
        im->scp.pf_pos = 0;
        im->scp.pf_rev = (im->scp.pf_rev + 1) % ARRAY_SIZE(im->scp.rev);
    }
}

static uint16_t scp_load_flux(struct image *im, uint16_t *tbuf, uint16_t nr)
{
    uint32_t x, ticks = im->ticks_since_flux, todo = nr;
    uint32_t nr_flux = im->scp.rev[im->scp.ld_rev].nr_dat;
    uint16_t *buf = (uint16_t *)im->buf;

    while (im->cons != im->prod) {
        if (im->scp.ld_pos == nr_flux) {
            im->tracklen_ticks = im->cur_ticks;
            im->cur_ticks = 0;
            im->scp.ld_pos = 0;
            im->scp.ld_rev = (im->scp.ld_rev + 1) % ARRAY_SIZE(im->scp.rev);
            nr_flux = im->scp.rev[im->scp.ld_rev].nr_dat;
        }
        im->scp.ld_pos++;
        x = be16toh(buf[im->cons++ % (sizeof(im->buf)/2)]) ?: 0x10000;
        x *= (FF_MHZ << 8) / SCP_MHZ;
        x >>= 8;
        if (x >= 0x10000)
            x = 0xffff; /* clamp */
        im->cur_ticks += x;
        ticks += x;
        *tbuf++ = (ticks >> 4) - 1;
        ticks &= 15;
        if (!--todo)
            goto out;
    }

out:
    im->ticks_since_flux = ticks;
    return nr - todo;
}

struct image_handler scp_image_handler = {
    .open = scp_open,
    .seek_track = scp_seek_track,
    .prefetch_data = scp_prefetch_data,
    .load_flux = scp_load_flux
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

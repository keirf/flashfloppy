/*
 * floppy.h
 * 
 * Floppy interface control and image management.
 * 
 * Written & released by Keir Fraser <keir.xen@gmail.com>
 * 
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

#define DRIVE_RPM 300u
#define DRIVE_MS_PER_REV (60000u/DRIVE_RPM)
#define DRIVE_SETTLE_MS 12

struct drive {
    const char *filename;
    uint8_t cyl, head;
    bool_t sel;
    struct {
        bool_t active;
        bool_t settling;
        bool_t inward;
        stk_time_t start;
    } step;
    struct image *image;
};

struct adf_image {
    uint32_t trk_off;
    uint16_t trk_pos, trk_len;
    uint32_t mfm[16], mfm_cons;
};

struct hfe_image {
    uint16_t tlut_base;
    uint16_t trk_off;
    uint16_t trk_pos, trk_len;
    uint32_t ticks_per_cell;
};

struct scp_image {
    uint8_t nr_revs;        /* # revolutions per track in image file */
    uint8_t pf_rev, ld_rev; /* Current prefetch/load revolution */
    uint32_t ld_pos; /* Current load position */
    uint32_t pf_pos; /* Prefetch position */
    struct {
        uint32_t dat_off;
        uint32_t nr_dat;
    } rev[5];
};

struct image {
    struct image_handler *handler;

    /* FatFS. */
    FIL fp;

    /* Info about image as a whole. */
    uint16_t nr_tracks;

    /* Track buffer. */
    uint32_t buf[4096/4];
    uint32_t prod, cons;

    /* Info about current track. */
    uint16_t cur_track;
    uint32_t tracklen_bc, cur_bc; /* Track length and cursor, in bitcells */
    uint32_t tracklen_ticks; /* Timing of previous revolution, in 'ticks' */
    uint32_t cur_ticks; /* Offset from index, in 'ticks' */
    uint32_t ticks_since_flux; /* Ticks since last flux sample/reversal */

    union {
        struct adf_image adf;
        struct hfe_image hfe;
        struct scp_image scp;
    };
};

#define TRACKNR_INVALID ((uint16_t)-1)

struct image_handler {
    bool_t (*open)(struct image *im);
    bool_t (*seek_track)(struct image *im, uint8_t track,
                         stk_time_t *ptime_after_index);
    void (*prefetch_data)(struct image *im);
    uint16_t (*load_flux)(struct image *im, uint16_t *tbuf, uint16_t nr);
};

bool_t image_open(struct image *im, const char *name);
bool_t image_seek_track(struct image *im, uint8_t track,
                        stk_time_t *ptime_after_index);
void image_prefetch_data(struct image *im);
uint16_t image_load_flux(struct image *im, uint16_t *tbuf, uint16_t nr);
uint32_t image_ticks_since_index(struct image *im);

void floppy_init(const char *disk0_name);
void floppy_cancel(void);
int floppy_handle(void);

/*
 * Local variables:
 * mode: C
 * c-file-style: "Linux"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */

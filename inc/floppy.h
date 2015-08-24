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
    uint32_t ticks;
    uint32_t mfm[16], mfm_cons;
};

struct hfe_image {
    uint16_t tlut_base;
    uint16_t trk_off;
    uint16_t trk_pos, trk_len;
    uint32_t ticks, ticks_per_cell;
};

struct image {
    struct drive *drive;

    struct image_handler *handler;

    /* FatFS. */
    FIL fp;
    FRESULT fr;

    /* Info about image as a whole. */
    uint16_t nr_tracks;

    /* Track buffer. */
    uint32_t buf[1024/4];
    uint32_t prod, cons;

    /* Info about current track. */
    uint16_t cur_track;
    uint32_t tracklen_bc, cur_bc; /* Track length and cursor, in bitcells */
    uint32_t cur_ticks; /* Offset from index, in 'ticks' */

    union {
        struct adf_image adf;
        struct hfe_image hfe;
    };
};

#define TRACKNR_INVALID ((uint16_t)-1)

struct image_handler {
    bool_t (*open)(struct image *im);
    bool_t (*seek_track)(struct image *im, uint8_t track);
    void (*prefetch_data)(struct image *im);
    uint16_t (*load_flux)(struct image *im, uint16_t *tbuf, uint16_t nr);
};

bool_t image_open(struct image *im, const char *name);
bool_t image_seek_track(struct image *im, uint8_t track);
void image_prefetch_data(struct image *im);
uint16_t image_load_flux(struct image *im, uint16_t *tbuf, uint16_t nr);

void floppy_init(const char *disk0_name, const char *disk1_name);
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

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
    struct {
        bool_t active;
        stk_time_t time;
    } index;
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

    union {
        struct adf_image adf;
        struct hfe_image hfe;
    };
};

bool_t adf_open(struct image *im);
bool_t adf_seek_track(struct image *im, uint8_t track);
void adf_prefetch_data(struct image *im);
uint16_t adf_load_mfm(struct image *im, uint16_t *tbuf, uint16_t nr);

bool_t hfe_open(struct image *im);
bool_t hfe_seek_track(struct image *im, uint8_t track);
void hfe_prefetch_data(struct image *im);
uint16_t hfe_load_mfm(struct image *im, uint16_t *tbuf, uint16_t nr);

/*
 * Local variables:
 * mode: C
 * c-file-style: "Linux"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */

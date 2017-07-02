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
#define WRITE_TRACK_BITCELLS 100000

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

struct image_buf {
    void *p;
    uint32_t len;
    uint32_t prod, cons;
};

struct image_bufs {
    /* Buffer space for a whole track of raw MFM. */
    struct image_buf write_mfm;
    /* Staging area for writeout to mass storage. */
    struct image_buf write_data;
    /* Read buffer for track data to be used for generating flux pattern. */
    struct image_buf read_data;
};

struct image {
    const struct image_handler *handler;

    /* FatFS. */
    FIL fp;

    /* Info about image as a whole. */
    uint16_t nr_tracks;

    /* Data buffers. */
    struct image_bufs bufs;

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

struct image_handler {
    bool_t (*open)(struct image *im);
    bool_t (*seek_track)(
        struct image *im, uint8_t track, stk_time_t *start_pos);
    bool_t (*read_track)(struct image *im);
    uint16_t (*rdata_flux)(struct image *im, uint16_t *tbuf, uint16_t nr);
    void (*write_track)(struct image *im);
    uint32_t syncword;
};

/* Open specified image file on mass storage device. */
bool_t image_open(struct image *im, const char *name);

/* Seek to given track and start reading track data at specified rotational
 * position (specified as number of SYSCLK ticks past the index mark).
 * 
 * If start_pos is NULL then the caller is in write mode and thus is not
 * interested in fetching data from a particular rotational position.
 * 
 * Returns TRUE if track successfully loaded, else FALSE. */
bool_t image_seek_track(
    struct image *im, uint8_t track, stk_time_t *start_pos);

/* Read track data into memory. Returns TRUE if any new data was read. */
bool_t image_read_track(struct image *im);

/* Generate flux timings for the RDATA timer and output pin. */
uint16_t image_rdata_flux(struct image *im, uint16_t *tbuf, uint16_t nr);

/* Write track data from memory to mass storage. */
void image_write_track(struct image *im);

/* Rotational position of last-generated flux (SYSCLK ticks past index). */
uint32_t image_ticks_since_index(struct image *im);

void floppy_init(void);
void floppy_insert(unsigned int unit, const char *image_name);
void floppy_cancel(void);
void floppy_handle(void);

/*
 * Local variables:
 * mode: C
 * c-file-style: "Linux"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */

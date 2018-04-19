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

#define DRIVE_SETTLE_MS 12

#define FINTF_SHUGART     0
#define FINTF_IBMPC       1
#define FINTF_IBMPC_HDOUT 2
#define FINTF_AKAI_S950   3

struct adf_image {
    uint32_t trk_off;
    uint16_t trk_pos, trk_len;
    int32_t decode_pos;
    uint32_t pre_idx_gap_bc;
    uint32_t nr_secs;
};

struct hfe_image {
    uint16_t tlut_base;
    uint16_t trk_off;
    uint16_t trk_pos, trk_len;
    bool_t is_v3;
    uint8_t batch_secs;
    struct {
        uint16_t off, len;
        bool_t dirty;
    } write_batch;
};

struct img_image {
    uint32_t trk_off, base_off;
    uint16_t trk_sec;
    int32_t decode_pos;
    uint8_t layout; /* LAYOUT_* */
    bool_t has_iam;
    uint8_t gap3, gap_4a;
    int8_t write_sector;
    uint8_t sec_base, sec_map[64];
    uint8_t nr_sectors, sec_no;
    uint8_t interleave:4, skew:4;
    bool_t skew_cyls_only;
    uint16_t data_rate, gap4;
    uint32_t idx_sz, idam_sz, dam_sz;
};

struct dsk_image {
    uint32_t trk_off;
    uint16_t trk_pos;
    int32_t decode_pos;
    bool_t extended;
    int8_t write_sector;
    uint16_t gap4;
    uint32_t idx_sz, idam_sz, dam_sz;
};

struct directaccess {
    struct da_status_sector dass;
    int32_t decode_pos;
    uint16_t trk_sec;
};

struct image_buf {
    void *p;
    uint32_t len;
    uint32_t prod, cons;
};

struct image_bufs {
    /* Buffering for bitcells being written to disk. */
    struct image_buf write_bc;
    /* Buffering for bitcells we generate from read_data. */
    struct image_buf read_bc;
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
    uint8_t nr_cyls, nr_sides;

    /* Data buffers. */
    struct image_bufs bufs;

    struct write {
        uint32_t start; /* Ticks past index when current write started */
        uint32_t bc_end; /* Final bitcell buffer index */
        uint16_t dma_end; /* Final DMA buffer index */
        uint16_t track; /* Track written to */
    } write[8];
    uint16_t wr_cons, wr_bc, wr_prod;

    /* Info about current track. */
    uint16_t cur_track;
    uint16_t write_bc_ticks; /* Nr SYSCLK ticks per bitcell in write stream */
    uint32_t ticks_per_cell; /* Nr 'ticks' per bitcell in read stream. */
    uint32_t tracklen_bc, cur_bc; /* Track length and cursor, in bitcells */
    uint32_t tracklen_ticks; /* Timing of previous revolution, in 'ticks' */
    uint32_t cur_ticks; /* Offset from index, in 'ticks' */
    uint32_t ticks_since_flux; /* Ticks since last flux sample/reversal */
    uint32_t write_bc_window; /* Sliding window at head of bitcell stream */
    uint32_t stk_per_rev; /* Nr STK ticks per revolution. */
    enum { SYNC_none=0, SYNC_fm, SYNC_mfm } sync;

    union {
        struct adf_image adf;
        struct hfe_image hfe;
        struct img_image img;
        struct dsk_image dsk;
        struct directaccess da;
    };
};

static inline struct write *get_write(struct image *im, uint16_t idx)
{
    return &im->write[idx & (ARRAY_SIZE(im->write) - 1)];
}

struct image_handler {
    bool_t (*open)(struct image *im);
    void (*setup_track)(
        struct image *im, uint16_t track, uint32_t *start_pos);
    bool_t (*read_track)(struct image *im);
    uint16_t (*rdata_flux)(struct image *im, uint16_t *tbuf, uint16_t nr);
    bool_t (*write_track)(struct image *im);
};

/* Is given file valid to open as an image? */
bool_t image_valid(FILINFO *fp);

/* Open specified image file on mass storage device. */
void image_open(struct image *im, const struct slot *slot);

/* Seek to given track and start reading track data at specified rotational
 * position (specified as number of SYSCLK ticks past the index mark).
 * 
 * If start_pos is NULL then the caller is in write mode and thus is not
 * interested in fetching data from a particular rotational position.
 * 
 * Returns TRUE if the config file needs to be re-read (exiting D-A mode). */
bool_t image_setup_track(
    struct image *im, uint16_t track, uint32_t *start_pos);

/* Read track data into memory. Returns TRUE if any new data was read. */
bool_t image_read_track(struct image *im);

/* Generate flux timings for the RDATA timer and output pin. */
uint16_t image_rdata_flux(struct image *im, uint16_t *tbuf, uint16_t nr);
uint16_t bc_rdata_flux(struct image *im, uint16_t *tbuf, uint16_t nr);

/* Write track data from memory to mass storage. Returns TRUE if processing
 * was completed for the write at the tail of the pipeline. */
bool_t image_write_track(struct image *im);

/* Rotational position of last-generated flux (SYSCLK ticks past index). */
uint32_t image_ticks_since_index(struct image *im);

/* MFM conversion. */
extern const uint16_t mfmtab[];
static inline uint16_t bintomfm(uint8_t x) { return mfmtab[x]; }
uint8_t mfmtobin(uint16_t x);

/* External API. */
void floppy_init(uint8_t fintf_mode);
void floppy_insert(unsigned int unit, struct slot *slot);
void floppy_cancel(void);
bool_t floppy_handle(void); /* TRUE -> re-read config file */
void floppy_set_cyl(uint8_t unit, uint8_t cyl);
void floppy_get_track(uint8_t *p_cyl, uint8_t *p_side, uint8_t *p_sel,
                      uint8_t *p_writing);
void floppy_set_fintf_mode(uint8_t fintf_mode);

/*
 * Local variables:
 * mode: C
 * c-file-style: "Linux"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */

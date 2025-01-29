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

#define SAMPLECLK_MHZ 72
#define sampleclk_ns(x) (((x) * SAMPLECLK_MHZ) / 1000)
#define sampleclk_us(x) ((x) * SAMPLECLK_MHZ)
#define sampleclk_ms(x) ((x) * SAMPLECLK_MHZ * 1000)
#define sampleclk_stk(x) ((x) * (SAMPLECLK_MHZ / STK_MHZ))
#define stk_sampleclk(x) ((x) / (SAMPLECLK_MHZ / STK_MHZ))

#if TARGET == TARGET_quickdisk
#define is_quickdisk TRUE
#else
#define is_quickdisk FALSE
#endif

#if TARGET == TARGET_apple2
#define WDATA_TOGGLE TRUE
#else
#define WDATA_TOGGLE FALSE
#endif

#define FINTF_SHUGART     0
#define FINTF_IBMPC       1
#define FINTF_IBMPC_HDOUT 2
#define FINTF_JPPC_HDOUT  3
#define FINTF_AMIGA       4
#define FINTF_JPPC        5

#define outp_dskchg 0
#define outp_index  1
#define outp_trk0   2
#define outp_wrprot 3
#define outp_rdy    4
#define outp_hden   5
#define outp_nr     6
#define outp_unused outp_nr

#define verbose_image_log FALSE

struct adf_image {
    uint32_t trk_off;
    uint32_t sec_idx;
    int32_t decode_pos;
    uint32_t pre_idx_gap_bc;
    uint32_t nr_secs;
    uint32_t written_secs;
    uint8_t sec_map[2][22];
};

struct hfe_image {
    uint16_t tlut_base;
    uint16_t trk_off;
    uint16_t trk_pos, trk_len;
    bool_t is_v3, double_step;
    uint8_t batch_secs;
    struct {
        uint16_t start;
        bool_t wrapped;
    } write;
    struct {
        uint16_t off, len;
        bool_t dirty;
    } write_batch;
};

struct qd_image {
    uint16_t tb;
    uint32_t trk_off;
    uint32_t trk_pos, trk_len;
    uint32_t win_start, win_end;
    struct {
        uint32_t start;
        bool_t wrapped;
    } write;
    struct {
        uint32_t off, len;
        bool_t dirty;
    } write_batch;
};

struct raw_sec {
    uint8_t r;
    uint8_t n; /* 3 bits */
};

struct raw_trk {
    uint16_t nr_sectors;
    uint16_t sec_off;
    uint16_t data_rate;
    uint16_t rpm;
    uint16_t img_bps; /* could squeeze this field into uint8_t or bitfield */
    int16_t gap_2, gap_3, gap_4a;
    uint8_t interleave, cskew, hskew;
    uint8_t has_iam:1, is_fm:1, invert_data:1;
#define RAW_TRK_HEAD(h) ((h)+1)
    uint8_t head:2;
};

struct img_image {
    uint32_t trk_off, base_off;
    uint16_t trk_sec, rd_sec_pos;
    int32_t decode_pos;
    uint16_t decode_data_pos, crc;
    uint8_t layout; /* LAYOUT_* */
    uint8_t post_crc_syncs;
    int16_t write_sector;
    uint8_t *sec_map, *trk_map;
    struct raw_trk *trk, *trk_info;
    struct raw_sec *sec_info, *sec_info_base;
    /* If not NULL, replaces the default method for finding sector data. 
     * Sector data is at trk_off + file_sec_offsets[i]. */
    uint32_t *file_sec_offsets;
    /* Delay start of track this many bitcells past index. */
    uint32_t track_delay_bc;
    uint16_t gap_4;
    uint32_t idx_sz, idam_sz;
    uint16_t dam_sz_pre, dam_sz_post;
    void *heap_bottom;
};

struct dsk_image {
    uint32_t trk_off;
    uint16_t trk_pos;
    uint16_t rd_sec_pos;
    int32_t decode_pos;
    uint16_t decode_data_pos, crc;
    bool_t extended;
    int8_t write_sector;
    uint16_t gap4;
    uint32_t idx_sz, idam_sz;
    uint16_t dam_sz_pre, dam_sz_post;
    uint8_t rev;
};

struct directaccess {
    struct da_status_sector dass;
    int32_t decode_pos;
    uint16_t trk_sec;
    uint16_t idx_sz, idam_sz, dam_sz;
    bool_t lba_set;
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
    /* Handler for currently-selected type of disk image. */
    const struct image_handler *disk_handler;

    /* Handler for current track. May differ from the primary disk handler. */
    const struct image_handler *track_handler;

    /* FatFS. */
    FIL fp;

    /* Info about image as a whole. */
    uint8_t nr_cyls, nr_sides;
    uint8_t step, hswap;

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
    uint16_t write_bc_ticks; /* SAMPLECLK ticks per bitcell in write stream */
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
        struct qd_image qd;
        struct img_image img;
        struct dsk_image dsk;
        struct directaccess da;
    };

    struct slot *slot;
};

static inline struct write *get_write(struct image *im, uint16_t idx)
{
    return &im->write[idx & (ARRAY_SIZE(im->write) - 1)];
}

struct image_handler {
    bool_t (*open)(struct image *im);
    FSIZE_t (*extend)(struct image *im);
    void (*setup_track)(
        struct image *im, uint16_t track, uint32_t *start_pos);
    bool_t (*read_track)(struct image *im);
    uint16_t (*rdata_flux)(struct image *im, uint16_t *tbuf, uint16_t nr);
    bool_t (*write_track)(struct image *im);
};

/* List of supported image types. */
extern const struct image_type {
    char ext[8];
    const struct image_handler *handler;
} image_type[];

/* Is given file valid to open as an image? */
bool_t image_valid(FILINFO *fp);

/* Open specified image file on mass storage device. */
void image_open(struct image *im, struct slot *slot, DWORD *cltbl);

/* Extend a trunated image file. */
void image_extend(struct image *im);

/* Seek to given track and start reading track data at specified rotational
 * position (specified as number of SAMPLECLK ticks past the index mark).
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

/* Rotational position of last-generated flux (SAMPLECLK ticks past index). */
uint32_t image_ticks_since_index(struct image *im);

/* MFM conversion. */
extern const uint16_t mfmtab[];
static inline uint16_t bintomfm(uint8_t x) { return mfmtab[x]; }
uint8_t mfmtobin(uint16_t x);
void mfm_to_bin(const void *in, void *out, unsigned int nr);
void mfm_ring_to_bin(const uint16_t *ring, unsigned int mask,
                     unsigned int idx, void *out, unsigned int nr);
#define MFM_DAM_CRC  0xe295 /* 0xa1, 0xa1, 0xa1, 0xfb */
#define FM_DAM_CRC   0xbf84 /* 0xfb */

/* FM conversion. */
#define FM_SYNC_CLK 0xc7
uint16_t fm_sync(uint8_t dat, uint8_t clk);

/* External API. */
void floppy_init(void);
bool_t floppy_ribbon_is_reversed(void);
void floppy_insert(unsigned int unit, struct slot *slot);
void floppy_cancel(void);
bool_t floppy_handle(void); /* TRUE -> re-read config file */
void floppy_set_cyl(uint8_t unit, uint8_t cyl);
struct track_info {
    uint8_t cyl, side:1, sel:1, writing:1, in_da_mode:1;
};
void floppy_get_track(struct track_info *ti);
void floppy_set_fintf_mode(void);
void floppy_set_max_cyl(void);
static inline unsigned int im_nphys_cyls(struct image *im)
{
    return min_t(unsigned int, im->nr_cyls * (im->step?:1), 255);
}
static inline bool_t in_da_mode(struct image *im, unsigned int cyl)
{
#if TARGET == TARGET_shugart
    return cyl >= max_t(unsigned int, DA_FIRST_CYL, im_nphys_cyls(im));
#else
    return FALSE;
#endif
}

extern uint32_t motor_chgrst_exti_mask;
void motor_chgrst_setup_exti(void);

/*
 * Local variables:
 * mode: C
 * c-file-style: "Linux"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */

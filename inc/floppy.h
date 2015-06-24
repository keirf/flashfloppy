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

struct drive {
    const char *filename;
    uint8_t cyl;
    bool_t sel;
    struct {
        bool_t active;
        bool_t inward;
        stk_time_t start;
    } step;
    struct image *image;
};

struct hfe_image {
    uint16_t tlut_base;
};

struct image {
    struct drive *drive;
    FIL fp;
    FRESULT fr;
    uint16_t nr_tracks;
    uint32_t buf[2048/4];
    union {
        struct hfe_image hfe;
    };
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

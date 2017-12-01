/*
 * config.h
 * 
 * Configuration file parsing.
 * 
 * Written & released by Keir Fraser <keir.xen@gmail.com>
 * 
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

struct opt {
    const char *name;
};

struct opts {
    FIL *file;
    const struct opt *opts;
    char *arg;
    int argmax;
};

int get_next_opt(struct opts *opts);

/* FF.CFG options structure. */
struct __packed ff_cfg {
    /* Bump version for every incompatible change to structure layout. 
     * No need to bump for new fields appended to this structure. */
#define FFCFG_VERSION 2
    uint8_t version;
    /* Size of this structure. This allows simple backward compatibility 
     * by merging old and new structures of different sizes. */
    uint8_t size;
    /* interface: FINTF_* interface mode */
#define FINTF_JC 255 /* mode specified by jumper JC */
    uint8_t interface; /* FINTF_* interface mode */
    char da_report_version[16]; 
    uint8_t autoselect_file_secs;
    uint8_t autoselect_folder_secs;
    bool_t nav_loop; /* Wrap slot number at 0 and max? */
    uint8_t display_off_secs;
    bool_t display_on_activity; /* Display on when there is drive activity? */
    uint16_t display_scroll_rate;
#define FONT_7x16 7
#define FONT_8x16 8
    uint8_t oled_font; /* FONT_* oled font specifier */
    uint8_t step_volume;
    uint8_t side_select_glitch_filter;
    bool_t ejected_on_startup;
#define IMGS_last   0
#define IMGS_static 1
#define IMGS_init   2
    uint8_t image_on_startup;
    uint16_t display_probe_ms;
#define TWOBUTTON_zero   0
#define TWOBUTTON_eject  1
#define TWOBUTTON_rotary 2
    uint8_t twobutton_action;
#define NAVMODE_default 0
#define NAVMODE_indexed 1
#define NAVMODE_native  2
    uint8_t nav_mode;
};

extern struct ff_cfg ff_cfg;
extern const struct ff_cfg dfl_ff_cfg;

void flash_ff_cfg_update(void);
void flash_ff_cfg_erase(void);
void flash_ff_cfg_read(void);

/*
 * Local variables:
 * mode: C
 * c-file-style: "Linux"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */

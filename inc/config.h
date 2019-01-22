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
#define FONT_6x13 7
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
#define TWOBUTTON_zero        0
#define TWOBUTTON_eject       1
#define TWOBUTTON_rotary      2
#define TWOBUTTON_rotary_fast 3
#define TWOBUTTON_mask        3
#define TWOBUTTON_reverse     (1u<<7)
    uint8_t twobutton_action;
#define NAVMODE_default 0
#define NAVMODE_indexed 1
#define NAVMODE_native  2
    uint8_t nav_mode;
#define TRKCHG_instant  0
#define TRKCHG_realtime 1
    uint8_t track_change;
#define HOST_unspecified 0
#define HOST_akai        1
#define HOST_gem         2
#define HOST_ensoniq     3
#define HOST_acorn       4
#define HOST_ti99        5
#define HOST_memotech    6
#define HOST_uknc        7
#define HOST_pc98        8
#define HOST_pc_dos      9
#define HOST_msx        10
#define HOST_dec        11
#define HOST_tandy_coco 12
#define HOST_fluke      13
#define HOST_kaypro     14
#define HOST_nascom     15
#define HOST_casio      16
    uint8_t host;
    /* Bitfields within display_type field. */
#define DISPLAY_auto     0
#define DISPLAY_lcd      (1<<0)
#define DISPLAY_oled     (1<<1)
#define DISPLAY_narrower (1<<0) /* only if DISPLAY_oled */
#define DISPLAY_rotate   (1<<2) /* only if DISPLAY_oled */
#define DISPLAY_narrow   (1<<3) /* only if DISPLAY_oled */
#define DISPLAY_sh1106   (1<<4) /* only if DISPLAY_oled */
#define DISPLAY_oled_64  (1<<5) /* only if DISPLAY_oled */
#define _DISPLAY_lcd_columns 5 /* only if DISPLAY_lcd */
#define DISPLAY_lcd_columns(x) ((x)<<_DISPLAY_lcd_columns)
    uint16_t display_type;
#define ROT_none    0
#define ROT_full    1
#define ROT_half    3
#define ROT_quarter 2
#define ROT_reverse (1u<<7)
    uint8_t rotary;
    bool_t write_protect;
    uint16_t nav_scroll_rate;
    uint16_t nav_scroll_pause;
    uint16_t display_scroll_pause;
    bool_t index_suppression;
    bool_t extend_image;
#define PIN_auto   0
#define PIN_nc     PIN_high
#define PIN_high   (outp_nr + 1)
#define PIN_rdy    (outp_rdy + 1)
#define PIN_dens   (outp_hden + 1)
#define PIN_chg    (outp_dskchg + 1)
#define PIN_invert 0x80
#define PIN_low    (PIN_high | PIN_invert)
#define PIN_nrdy   (PIN_rdy | PIN_invert)
#define PIN_ndens  (PIN_dens | PIN_invert)
#define PIN_nchg   (PIN_chg | PIN_invert)
    uint8_t pin02, pin34;
    uint8_t head_settle_ms;
    uint8_t oled_contrast;
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

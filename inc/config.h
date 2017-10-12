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
extern struct ff_cfg {
    /* interface: FINTF_* interface mode */
#define FINTF_JC 255 /* mode specified by jumper JC */
    uint8_t interface; /* FINTF_* interface mode */
    bool_t ejected_on_startup;
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
} ff_cfg;

/*
 * Local variables:
 * mode: C
 * c-file-style: "Linux"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */

/*
 * main.c
 * 
 * System initialisation and navigation main loop.
 * 
 * Written & released by Keir Fraser <keir.xen@gmail.com>
 * 
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

int EXC_reset(void) __attribute__((alias("main")));

static FATFS fatfs;
static struct {
    FIL file;
    DIR dp;
    FILINFO fp;
    char buf[512];
} *fs;

static struct {
    uint16_t slot_nr, max_slot_nr;
    uint8_t slot_map[1000/8];
    struct short_slot autoboot, hxcsdfe;
    struct slot slot;
    uint32_t cfg_cdir, cur_cdir;
    struct {
        uint32_t cdir;
        uint16_t slot;
    } stack[20];
    uint8_t depth;
    bool_t usb_power_fault;
    uint8_t hxc_mode:1;
    uint8_t ejected:1;
    uint8_t ima_ej_flag:1; /* "\\EJ" flag in IMAGE_A.CFG? */
    /* FF.CFG values which override HXCSDFE.CFG. */
    uint8_t ffcfg_has_step_volume:1;
    uint8_t ffcfg_has_display_off_secs:1;
    uint8_t ffcfg_has_display_scroll_rate:1;
} cfg;

/* If TRUE, reset to start of filename when selecting a new image. 
 * If FALSE, try to maintain scroll offset when browsing through images. */
#define cfg_scroll_reset TRUE

uint8_t board_id;

static uint32_t backlight_ticks;
static uint8_t backlight_state;
#define BACKLIGHT_OFF          0
#define BACKLIGHT_SWITCHING_ON 1
#define BACKLIGHT_ON           2

static bool_t slot_valid(unsigned int i)
{
    if (i > cfg.max_slot_nr)
        return FALSE;
    if (!cfg.hxc_mode)
        return TRUE;
    if (i >= (sizeof(cfg.slot_map)*8))
        return FALSE;
    return !!(cfg.slot_map[i/8] & (0x80>>(i&7)));
}

/* Turn the LCD backlight on, reset the switch-off handler and ticker. */
static void lcd_on(void)
{
    if (display_mode != DM_LCD_1602)
        return;
    backlight_ticks = 0;
    barrier();
    backlight_state = BACKLIGHT_ON;
    barrier();
    lcd_backlight(ff_cfg.display_off_secs != 0);
}

static bool_t slot_type(const char *str)
{
    char ext[8];
    filename_extension(cfg.slot.name, ext, sizeof(ext));
    if (!strcmp(ext, str))
        return TRUE;
    return !strcmp(cfg.slot.type, str);
}

#define wp_column ((lcd_columns > 16) ? 8 : 7)
static void display_wp_status(void)
{
    if (display_mode != DM_LCD_1602)
        return;
    lcd_write(wp_column, 1, 1, (cfg.slot.attributes & AM_RDO) ? "*" : "");
}

/* Scroll long filename. */
static struct {
    uint16_t off, end, pause, rate;
    int32_t ticks;
} lcd_scroll;
static void lcd_scroll_init(uint16_t pause, uint16_t rate)
{
    int diff = lcd_scroll.off - lcd_scroll.end;
    lcd_scroll.pause = pause;
    lcd_scroll.rate = rate;
    lcd_scroll.end = max_t(
        int, strnlen(cfg.slot.name, sizeof(cfg.slot.name)) - lcd_columns, 0);
    if (lcd_scroll.end && !lcd_scroll.pause)
        lcd_scroll.end += lcd_columns;
    if (lcd_scroll.off > lcd_scroll.end)
        lcd_scroll.off = lcd_scroll.pause || !lcd_scroll.end
            ? 0 : lcd_scroll.end + diff;
}
static void lcd_scroll_name(void)
{
    char msg[25];
    if ((lcd_scroll.ticks > 0) || (lcd_scroll.end == 0))
        return;
    lcd_scroll.ticks = time_ms(lcd_scroll.rate);
    if (lcd_scroll.pause != 0) {
        if (++lcd_scroll.off > lcd_scroll.end)
            lcd_scroll.off = 0;
        snprintf(msg, sizeof(msg), "%s", cfg.slot.name + lcd_scroll.off);
        if ((lcd_scroll.off == 0)
            || (lcd_scroll.off == lcd_scroll.end))
            lcd_scroll.ticks = time_ms(lcd_scroll.pause);
    } else {
        const unsigned int scroll_gap = 4;
        lcd_scroll.off++;
        if (lcd_scroll.off <= lcd_scroll.end) {
            snprintf(msg, sizeof(msg), "%s%*s%s",
                     cfg.slot.name + lcd_scroll.off,
                     scroll_gap, "", cfg.slot.name);
        } else {
            snprintf(msg, sizeof(msg), "%*s%s",
                     scroll_gap - (lcd_scroll.off - lcd_scroll.end), "",
                     cfg.slot.name);
            if ((lcd_scroll.off - lcd_scroll.end) == scroll_gap)
                lcd_scroll.off = 0;
        }
    }
    lcd_write(0, 0, -1, msg);
}

/* Write slot info to display. */
static void display_write_slot(bool_t nav_mode)
{
    char msg[25], *type;
    if (display_mode != DM_LCD_1602) {
        if (display_mode == DM_LED_7SEG)
            led_7seg_write_decimal(cfg.slot_nr);
        return;
    }
    if (nav_mode && !cfg_scroll_reset) {
        lcd_scroll_init(0, ff_cfg.nav_scroll_rate);
        if (lcd_scroll.end == 0) {
            snprintf(msg, sizeof(msg), "%s", cfg.slot.name);
            lcd_write(0, 0, -1, msg);
        } else {
            lcd_scroll.off--;
            lcd_scroll.ticks = 0;
            lcd_scroll_name();
        }
    } else {
        snprintf(msg, sizeof(msg), "%s", cfg.slot.name);
        lcd_write(0, 0, -1, msg);
    }
    type = (cfg.slot.attributes & AM_DIR) ? "DIR"
        : slot_type("adf") ? "ADF"
        : slot_type("d81") ? "D81"
        : slot_type("dsk") ? "DSK"
        : slot_type("hfe") ? "HFE"
        : slot_type("img") ? "IMG"
        : slot_type("ima") ? "IMA"
        : slot_type("st") ? "ST "
        : slot_type("adl") ? "ADL"
        : slot_type("adm") ? "ADM"
        : slot_type("mbd") ? "MBD"
        : slot_type("mgt") ? "MGT"
        : slot_type("fdi") ? "FDI"
        : slot_type("trd") ? "TRD"
        : slot_type("opd") ? "OPD"
        : slot_type("ssd") ? "SSD"
        : slot_type("dsd") ? "DSD"
        : slot_type("sdu") ? "SDU"
        : slot_type("jvc") ? "JVC"
        : slot_type("vdk") ? "VDK"
        : slot_type("v9t9") ? "T99"
        : "UNK";
    snprintf(msg, sizeof(msg), "%03u/%03u%*s%s D:%u",
             cfg.slot_nr, cfg.max_slot_nr,
             (lcd_columns > 16) ? 3 : 1, "",
             type, cfg.depth);
    if (cfg.hxc_mode) {
        /* HxC mode: Exclude depth from the info message. */
        char *p = strrchr(msg, 'D');
        if (p)
            *p = '\0';
    }
    lcd_write(0, 1, -1, msg);
    lcd_on();
}

/* Write track number to LCD. */
static void lcd_write_track_info(bool_t force)
{
    static uint8_t lcd_cyl, lcd_side, lcd_writing;
    uint8_t cyl, side, sel, writing;
    char msg[17];

    if (display_mode != DM_LCD_1602)
        return;

    floppy_get_track(&cyl, &side, &sel, &writing);
    cyl = min_t(uint8_t, cyl, 99);
    side = min_t(uint8_t, side, 1);

    if (force || (cyl != lcd_cyl) || ((side != lcd_side) && sel)
        || (writing != lcd_writing)) {
        snprintf(msg, sizeof(msg), "%c T:%02u.%u",
                 (cfg.slot.attributes & AM_RDO) ? '*' : writing ? 'W' : ' ',
                 cyl, side);
        lcd_write(wp_column, 1, -1, msg);
        if (ff_cfg.display_on_activity)
            lcd_on();
        lcd_cyl = cyl;
        lcd_side = side;
        lcd_writing = writing;
    }
}

/* Handle switching the LCD backlight. */
static uint8_t lcd_handle_backlight(uint8_t b)
{
    if ((display_mode != DM_LCD_1602)
        || (ff_cfg.display_off_secs == 0)
        || (ff_cfg.display_off_secs == 0xff))
        return b;

    switch (backlight_state) {
    case BACKLIGHT_OFF:
        if (!b)
            break;
        /* First button press is to turn on the backlight. Nothing more. */
        b = 0;
        backlight_state = BACKLIGHT_SWITCHING_ON;
        lcd_backlight(TRUE);
        break;
    case BACKLIGHT_SWITCHING_ON:
        /* We sit in this state until the button is released. */
        if (!b)
            backlight_state = BACKLIGHT_ON;
        b = 0;
        backlight_ticks = 0;
        break;
    case BACKLIGHT_ON:
        /* After a period with no button activity we turn the backlight off. */
        if (b)
            backlight_ticks = 0;
        if (backlight_ticks++ >= 200*ff_cfg.display_off_secs) {
            lcd_backlight(FALSE);
            backlight_state = BACKLIGHT_OFF;
        }
        break;
    }

    return b;
}

static struct timer button_timer;
static volatile uint8_t buttons;
static uint8_t rotary;
#define B_LEFT 1
#define B_RIGHT 2
#define B_SELECT 4
static void button_timer_fn(void *unused)
{
    /* Rotary encoder outputs a Gray code, counting clockwise: 00-01-11-10. */
    const uint32_t rotary_transitions[] = {
        [ROT_none]    = 0x00000000, /* No encoder */
        [ROT_full]    = 0x20000100, /* 4 transitions (full cycle) per detent */
        [ROT_half]    = 0x24000018, /* 2 transitions (half cycle) per detent */
        [ROT_quarter] = 0x24428118  /* 1 transition (quarter cyc) per detent */
    };

    static uint16_t bl, br, bs;
    uint8_t b = 0;

    /* Check PA5 (USBFLT, active low). */
    if ((board_id == BRDREV_Gotek_enhanced) && !gpio_read_pin(gpioa, 5)) {
        /* Latch the error and disable USBENA. */
        cfg.usb_power_fault = TRUE;
        gpio_write_pin(gpioa, 4, HIGH);
    }

    /* We debounce the switches by waiting for them to be pressed continuously 
     * for 16 consecutive sample periods (16 * 5ms == 80ms) */

    bl <<= 1;
    bl |= gpio_read_pin(gpioc, 8);
    if (bl == 0)
        b |= (ff_cfg.twobutton_action == TWOBUTTON_rotary)
            ? B_LEFT|B_RIGHT : B_LEFT;

    br <<= 1;
    br |= gpio_read_pin(gpioc, 7);
    if (br == 0)
        b |= (ff_cfg.twobutton_action == TWOBUTTON_rotary)
            ? B_SELECT : B_RIGHT;

    bs <<= 1;
    bs |= gpio_read_pin(gpioc, 6);
    if (bs == 0)
        b |= B_SELECT;

    rotary = ((rotary << 2) | ((gpioc->idr >> 10) & 3)) & 15;
    b |= (rotary_transitions[ff_cfg.rotary & 3] >> (rotary << 1)) & 3;

    b = lcd_handle_backlight(b);

    /* Latch final button state and reset the timer. */
    buttons = b;
    timer_set(&button_timer, button_timer.deadline + time_ms(5));
}

static void canary_init(void)
{
    _irq_stackbottom[0] = _thread_stackbottom[0] = 0xdeadbeef;
}

static void canary_check(void)
{
    ASSERT(_irq_stackbottom[0] == 0xdeadbeef);
    ASSERT(_thread_stackbottom[0] == 0xdeadbeef);
}

static void slot_from_short_slot(
    struct slot *slot, const struct short_slot *short_slot)
{
    memcpy(slot->name, short_slot->name, sizeof(short_slot->name));
    slot->name[sizeof(short_slot->name)] = '\0';
    memcpy(slot->type, short_slot->type, sizeof(short_slot->type));
    slot->type[sizeof(short_slot->type)] = '\0';
    slot->attributes = short_slot->attributes;
    slot->firstCluster = short_slot->firstCluster;
    slot->size = short_slot->size;
    slot->dir_sect = slot->dir_ptr = 0;
}

static void fatfs_to_short_slot(
    struct short_slot *slot, FIL *file, const char *name)
{
    char *dot;
    unsigned int i;

    slot->attributes = file->obj.attr;
    slot->firstCluster = file->obj.sclust;
    slot->size = file->obj.objsize;
    snprintf(slot->name, sizeof(slot->name), "%s", name);
    if ((dot = strrchr(slot->name, '.')) != NULL) {
        memcpy(slot->type, dot+1, sizeof(slot->type));
        for (i = 0; i < sizeof(slot->type); i++)
            slot->type[i] = tolower(slot->type[i]);
        *dot = '\0';
    } else {
        memset(slot->type, 0, sizeof(slot->type));
    }
}

void fatfs_from_slot(FIL *file, const struct slot *slot, BYTE mode)
{
    memset(file, 0, sizeof(*file));
    file->obj.fs = &fatfs;
    file->obj.id = fatfs.id;
    file->obj.attr = slot->attributes;
    file->obj.sclust = slot->firstCluster;
    file->obj.objsize = slot->size;
    file->flag = mode;
    file->dir_sect = slot->dir_sect;
    file->dir_ptr = (void *)slot->dir_ptr;
}

static void fatfs_to_slot(struct slot *slot, FIL *file, const char *name)
{
    char *dot;
    unsigned int i;

    slot->attributes = file->obj.attr;
    slot->firstCluster = file->obj.sclust;
    slot->size = file->obj.objsize;
    slot->dir_sect = file->dir_sect;
    slot->dir_ptr = (uint32_t)file->dir_ptr;
    snprintf(slot->name, sizeof(slot->name), "%s", name);
    if ((dot = strrchr(slot->name, '.')) != NULL) {
        snprintf(slot->type, sizeof(slot->type), "%s", dot+1);
        for (i = 0; i < sizeof(slot->type); i++)
            slot->type[i] = tolower(slot->type[i]);
        *dot = '\0';
    } else {
        memset(slot->type, 0, sizeof(slot->type));
    }
}

static void dump_file(void)
{
    F_lseek(&fs->file, 0);
#ifndef NDEBUG
    printk("[");
    do {
        F_read(&fs->file, fs->buf, sizeof(fs->buf), NULL);
        printk("%s", fs->buf);
    } while (!f_eof(&fs->file));
    printk("]\n");
    F_lseek(&fs->file, 0);
#endif
}

static bool_t native_dir_next(void)
{
    for (;;) {
        F_readdir(&fs->dp, &fs->fp);
        if (fs->fp.fname[0] == '\0')
            return FALSE;
        /* Skip dot files. */
        if (fs->fp.fname[0] == '.')
            continue;
        /* Skip hidden files/folders. */
        if (fs->fp.fattrib & AM_HID)
            continue;
        /* Allow folder navigation when LCD/OLED display is attached. */
        if ((fs->fp.fattrib & AM_DIR) && (display_mode == DM_LCD_1602)
            /* Skip FF/ in root folder */
            && ((cfg.depth != 0) || strcmp(fs->fp.fname, "FF"))
            /* Skip __MACOSX/ zip-file resource-fork folder */
            && strcmp(fs->fp.fname, "__MACOSX"))
            break;
        /* Allow valid image files. */
        if (image_valid(&fs->fp))
            break;
    }
    return TRUE;
}

/* Parse pinNN= config value. */
static uint8_t parse_pin_str(const char *s)
{
    uint8_t pin = 0;
    if (*s == 'n') {
        pin = PIN_invert;
        s++;
    }
    pin ^= !strcmp(s, "low") ? PIN_low
        : !strcmp(s, "high") ? PIN_high
        : !strcmp(s, "c") ? (PIN_invert | PIN_nc)
        : !strcmp(s, "rdy") ? PIN_rdy
        : !strcmp(s, "dens") ? PIN_dens
        : !strcmp(s, "chg") ? PIN_chg
        : PIN_auto;
    return pin;
}

static void read_ff_cfg(void)
{
    enum {
#define x(n,o,v) FFCFG_##o,
#include "ff_cfg_defaults.h"
#undef x
        FFCFG_nr
    };

    const static struct opt ff_cfg_opts[FFCFG_nr+1] = {
#define x(n,o,v) [FFCFG_##o] = { #n },
#include "ff_cfg_defaults.h"
#undef x
    };

    FRESULT fr;
    int option;
    struct opts opts = {
        .file = &fs->file,
        .opts = ff_cfg_opts,
        .arg = fs->buf,
        .argmax = sizeof(fs->buf)-1
    };

    fatfs.cdir = cfg.cfg_cdir;
    fr = F_try_open(&fs->file, "FF.CFG", FA_READ);
    if (fr)
        return;

    while ((option = get_next_opt(&opts)) != -1) {

        switch (option) {

            /* DRIVE EMULATION */

        case FFCFG_interface:
            ff_cfg.interface =
                !strcmp(opts.arg, "shugart") ? FINTF_SHUGART
                : !strcmp(opts.arg, "ibmpc") ? FINTF_IBMPC
                : !strcmp(opts.arg, "ibmpc-hdout") ? FINTF_IBMPC_HDOUT
                : !strcmp(opts.arg, "akai-s950") ? FINTF_AKAI_S950
                : !strcmp(opts.arg, "amiga") ? FINTF_AMIGA
                : FINTF_JC;
            break;

        case FFCFG_host:
            ff_cfg.host =
                !strcmp(opts.arg, "acorn") ? HOST_acorn
                : !strcmp(opts.arg, "akai") ? HOST_akai
                : !strcmp(opts.arg, "dec") ? HOST_dec
                : !strcmp(opts.arg, "ensoniq") ? HOST_ensoniq
                : !strcmp(opts.arg, "gem") ? HOST_gem
                : !strcmp(opts.arg, "memotech") ? HOST_memotech
                : !strcmp(opts.arg, "msx") ? HOST_msx
                : !strcmp(opts.arg, "pc98") ? HOST_pc98
                : !strcmp(opts.arg, "pc-dos") ? HOST_pc_dos
                : !strcmp(opts.arg, "tandy-coco") ? HOST_tandy_coco
                : !strcmp(opts.arg, "ti99") ? HOST_ti99
                : !strcmp(opts.arg, "uknc") ? HOST_uknc
                : HOST_unspecified;
            break;

        case FFCFG_pin02:
            ff_cfg.pin02 = parse_pin_str(opts.arg);
            break;

        case FFCFG_pin34:
            ff_cfg.pin34 = parse_pin_str(opts.arg);
            break;

        case FFCFG_write_protect:
            ff_cfg.write_protect = !strcmp(opts.arg, "yes");
            break;

        case FFCFG_side_select_glitch_filter:
            ff_cfg.side_select_glitch_filter = strtol(opts.arg, NULL, 10);
            break;

        case FFCFG_track_change:
            ff_cfg.track_change =
                !strcmp(opts.arg, "realtime") ? TRKCHG_realtime
                : TRKCHG_instant;
            break;

        case FFCFG_index_suppression:
            ff_cfg.index_suppression = !strcmp(opts.arg, "yes");
            break;

            /* STARTUP / INITIALISATION */

        case FFCFG_ejected_on_startup:
            ff_cfg.ejected_on_startup = !strcmp(opts.arg, "yes");
            break;

        case FFCFG_image_on_startup:
            ff_cfg.image_on_startup =
                !strcmp(opts.arg, "static") ? IMGS_static
                : !strcmp(opts.arg, "last") ? IMGS_last : IMGS_init;
            break;

        case FFCFG_display_probe_ms:
            ff_cfg.display_probe_ms = strtol(opts.arg, NULL, 10);
            break;

            /* IMAGE NAVIGATION */

        case FFCFG_autoselect_file_secs:
            ff_cfg.autoselect_file_secs = strtol(opts.arg, NULL, 10);
            break;

        case FFCFG_autoselect_folder_secs:
            ff_cfg.autoselect_folder_secs = strtol(opts.arg, NULL, 10);
            break;

        case FFCFG_nav_mode:
            ff_cfg.nav_mode =
                !strcmp(opts.arg, "native") ? NAVMODE_native
                : !strcmp(opts.arg, "indexed") ? NAVMODE_indexed
                : NAVMODE_default;
            break;

        case FFCFG_nav_loop:
            ff_cfg.nav_loop = !strcmp(opts.arg, "yes");
            break;

        case FFCFG_twobutton_action:
            ff_cfg.twobutton_action =
                !strcmp(opts.arg, "rotary") ? TWOBUTTON_rotary
                : !strcmp(opts.arg, "rotary-fast") ? TWOBUTTON_rotary_fast
                : !strcmp(opts.arg, "eject") ? TWOBUTTON_eject
                : TWOBUTTON_zero;
            break;

        case FFCFG_rotary:
            ff_cfg.rotary =
                !strcmp(opts.arg, "gray") ? ROT_quarter /* obsolete name */
                : !strcmp(opts.arg, "quarter") ? ROT_quarter
                : !strcmp(opts.arg, "half") ? ROT_half
                : !strcmp(opts.arg, "none") ? ROT_none
                : ROT_full;
            break;

            /* DISPLAY */

        case FFCFG_display_type: {
            char *p, *q, *r;
            ff_cfg.display_type = DISPLAY_auto;
            for (p = opts.arg; *p != '\0'; p = q) {
                for (q = p; *q && *q != '-'; q++)
                    continue;
                if (*q == '-')
                    *q++ = '\0';
                if (!strcmp(p, "lcd")) {
                    ff_cfg.display_type = DISPLAY_lcd;
                } else if (!strcmp(p, "oled")) {
                    ff_cfg.display_type = DISPLAY_oled;
                } else if (!strcmp(p, "rotate")) {
                    ff_cfg.display_type |= DISPLAY_rotate;
                } else if (!strcmp(p, "narrow")) {
                    ff_cfg.display_type |= DISPLAY_narrow;
                } else if (!strcmp(p, "sh1106")) {
                    ff_cfg.display_type |= DISPLAY_sh1106;
                } else if ((r = strchr(p, 'x')) != NULL) {
                    unsigned int w, h;
                    *r++ = '\0';
                    w = strtol(p, NULL, 10);
                    h = strtol(r, NULL, 10);
                    if ((ff_cfg.display_type & DISPLAY_oled) && (h == 64)) {
                        ff_cfg.display_type |= DISPLAY_oled_64;
                    } else if (ff_cfg.display_type & DISPLAY_lcd) {
                        ff_cfg.display_type |= DISPLAY_lcd_columns(w);
                    }
                }
            }
            break;
        }

        case FFCFG_oled_font:
            ff_cfg.oled_font =
                !strcmp(opts.arg, "6x13") ? FONT_6x13
                : FONT_8x16;
            break;

        case FFCFG_display_off_secs:
            ff_cfg.display_off_secs = strtol(opts.arg, NULL, 10);
            cfg.ffcfg_has_display_off_secs = TRUE;
            break;

        case FFCFG_display_on_activity:
            ff_cfg.display_on_activity = !strcmp(opts.arg, "yes");
            break;

        case FFCFG_display_scroll_rate:
            ff_cfg.display_scroll_rate = strtol(opts.arg, NULL, 10);
            if (ff_cfg.display_scroll_rate < 100)
                ff_cfg.display_scroll_rate = 100;
            cfg.ffcfg_has_display_scroll_rate = TRUE;
            break;

        case FFCFG_display_scroll_pause:
            ff_cfg.display_scroll_pause = strtol(opts.arg, NULL, 10);
            break;

        case FFCFG_nav_scroll_rate:
            ff_cfg.nav_scroll_rate = strtol(opts.arg, NULL, 10);
            break;

        case FFCFG_nav_scroll_pause:
            ff_cfg.nav_scroll_pause = strtol(opts.arg, NULL, 10);
            break;

            /* MISCELLANEOUS */

        case FFCFG_step_volume: {
            int volume = strtol(opts.arg, NULL, 10);
            if (volume <= 0) volume = 0;
            if (volume >= 20) volume = 20;
            ff_cfg.step_volume = volume;
            cfg.ffcfg_has_step_volume = TRUE;
            break;
        }

        case FFCFG_da_report_version:
            memset(ff_cfg.da_report_version, 0,
                   sizeof(ff_cfg.da_report_version));
            snprintf(ff_cfg.da_report_version,
                     sizeof(ff_cfg.da_report_version),
                     "%s", opts.arg);
            break;

        case FFCFG_extend_image:
            ff_cfg.extend_image = !strcmp(opts.arg, "yes");
            break;

        }
    }

    F_close(&fs->file);

    flash_ff_cfg_update();
}

static void process_ff_cfg_opts(const struct ff_cfg *old)
{
    /* interface, pin02, pin34: Inform the floppy subsystem. */
    if ((ff_cfg.interface != old->interface)
        || (ff_cfg.pin02 != old->pin02)
        || (ff_cfg.pin34 != old->pin34))
        floppy_set_fintf_mode();

    /* ejected-on-startup: Set the ejected state appropriately. */
    if (ff_cfg.ejected_on_startup)
        cfg.ejected = TRUE;

    /* oled-font, display-type: Reinitialise the display subsystem. */
    if ((ff_cfg.oled_font != old->oled_font)
        || (ff_cfg.display_type != old->display_type))
        system_reset(); /* hit it with a hammer */
}

static void cfg_init(void)
{
    struct ff_cfg old_ff_cfg = ff_cfg;
    struct hxcsdfe_cfg hxc_cfg;
    unsigned int sofar;
    char *p;
    BYTE mode;
    FRESULT fr;

    cfg.hxc_mode = FALSE;
    cfg.ima_ej_flag = FALSE;
    cfg.slot_nr = cfg.depth = 0;
    cfg.cur_cdir = fatfs.cdir;

    fr = f_chdir("FF");
    cfg.cfg_cdir = fatfs.cdir;

    read_ff_cfg();
    process_ff_cfg_opts(&old_ff_cfg);

    switch (ff_cfg.nav_mode) {
    case NAVMODE_native:
        goto native_mode;
    case NAVMODE_indexed:
        cfg.hxc_mode = TRUE;
        goto out;
    default:
        break;
    }

    /* Probe for HxC compatibility mode. */
    fatfs.cdir = cfg.cur_cdir;
    fr = F_try_open(&fs->file, "HXCSDFE.CFG", FA_READ|FA_WRITE);
    if (fr)
        goto native_mode;
    fatfs_to_short_slot(&cfg.hxcsdfe, &fs->file, "HXCSDFE.CFG");
    F_read(&fs->file, &hxc_cfg, sizeof(hxc_cfg), NULL);
    if (hxc_cfg.startup_mode & HXCSTARTUP_slot0) {
        /* Startup mode: slot 0. */
        hxc_cfg.slot_index = hxc_cfg.cur_slot_number = 0;
        F_lseek(&fs->file, 0);
        F_write(&fs->file, &hxc_cfg, sizeof(hxc_cfg), NULL);
    }
    if (hxc_cfg.startup_mode & HXCSTARTUP_ejected) {
        /* Startup mode: eject. */
        cfg.ejected = TRUE;
    }
        
    F_close(&fs->file);

    /* Indexed mode (DSKAxxxx.HFE) does not need AUTOBOOT.HFE. */
    if (!strncmp("HXCFECFGV", hxc_cfg.signature, 9) && hxc_cfg.index_mode) {
        memset(&cfg.autoboot, 0, sizeof(cfg.autoboot));
        cfg.hxc_mode = TRUE;
        goto out;
    }

    fr = F_try_open(&fs->file, "AUTOBOOT.HFE", FA_READ);
    if (fr)
        goto native_mode;
    fatfs_to_short_slot(&cfg.autoboot, &fs->file, "AUTOBOOT.HFE");
    cfg.autoboot.attributes |= AM_RDO; /* default read-only */
    F_close(&fs->file);

    cfg.hxc_mode = TRUE;
    goto out;

native_mode:
    /* Native mode (direct navigation). */
    if (ff_cfg.image_on_startup == IMGS_init)
        goto out;

    fatfs.cdir = cfg.cfg_cdir;
    mode = FA_READ;
    if (ff_cfg.image_on_startup == IMGS_last)
        mode |= FA_WRITE | FA_OPEN_ALWAYS;
    fr = F_try_open(&fs->file, "IMAGE_A.CFG", mode);
    if (fr)
        goto out;

    /* Process IMAGE_A.CFG file. */
    sofar = 0; /* bytes consumed so far */
    fatfs.cdir = cfg.cur_cdir;
    for (;;) {
        unsigned int nr = cfg.depth ? 1 : 0;
        bool_t ok;
        /* Read next pathname section, search for its terminating slash. */
        F_read(&fs->file, fs->buf, sizeof(fs->buf), NULL);
        fs->buf[sizeof(fs->buf)-1] = '\0';
        for (p = fs->buf; *p && (*p != '/'); p++)
            continue;
        /* No terminating slash: we're done. */
        if ((p == fs->buf) || !*p)
            break;
        /* Terminate the name section, push curdir onto stack, then chdir. */
        *p++ = '\0';
        printk("%u:D: '%s'\n", cfg.depth, fs->buf);
        if (cfg.depth == ARRAY_SIZE(cfg.stack))
            F_die(FR_PATH_TOO_DEEP);
        /* Find slot nr, and stack it */
        F_opendir(&fs->dp, "");
        while ((ok = native_dir_next()) && strcmp(fs->fp.fname, fs->buf))
            nr++;
        F_closedir(&fs->dp);
        if (!ok)
            goto clear_image_a;
        cfg.stack[cfg.depth].slot = nr;
        cfg.stack[cfg.depth++].cdir = fatfs.cdir;
        fr = f_chdir(fs->buf);
        if (fr)
            goto clear_image_a;
        /* Seek on to next pathname section. */
        sofar += p - fs->buf;
        F_lseek(&fs->file, sofar);
    }
    if (cfg.depth != 0) {
        /* No subfolder support on LED display. */
        if (display_mode != DM_LCD_1602)
            goto clear_image_a; /* no subfolder support on LED display */
        /* Skip '..' entry. */
        cfg.slot_nr = 1;
    }
    while ((p != fs->buf) && isspace(p[-1]))
        *--p = '\0'; /* Strip trailing whitespace */
    if (((p - fs->buf) >= 3) && !strcmp(p-3, "\\EJ")) {
        /* Eject flag "\\EJ" is found. Act on it and then skip over it. */
        cfg.ejected = TRUE;
        cfg.ima_ej_flag = TRUE;
        p -= 3;
        *p = '\0';
    }
    if (p != fs->buf) {
        /* If there was a non-empty non-terminated pathname section, it 
         * must be the name of the currently-selected image file. */
        bool_t ok;
        printk("%u:F: '%s' %s\n", cfg.depth, fs->buf,
               cfg.ima_ej_flag ? "(EJ)" : "");
        F_opendir(&fs->dp, "");
        while ((ok = native_dir_next()) && strcmp(fs->fp.fname, fs->buf))
            cfg.slot_nr++;
        F_closedir(&fs->dp);
        if (!ok)
            goto clear_image_a;
    }
    F_close(&fs->file);
    cfg.cur_cdir = fatfs.cdir;

out:
    printk("Mode: %s\n", cfg.hxc_mode ? "HxC" : "Native");
    fatfs.cdir = cfg.cur_cdir;
    return;

clear_image_a:
    /* Error! Clear the IMAGE_A.CFG file. */
    printk("IMAGE_A.CFG is bad: %sring it\n",
           (ff_cfg.image_on_startup == IMGS_last) ? "clea" : "igno");
    F_lseek(&fs->file, 0);
    if (ff_cfg.image_on_startup == IMGS_last)
        F_truncate(&fs->file);
    F_close(&fs->file);
    cfg.slot_nr = cfg.depth = 0;
    cfg.ima_ej_flag = FALSE;
    goto out;
}

#define CFG_KEEP_SLOT_NR  0 /* Do not re-read slot number from config */
#define CFG_READ_SLOT_NR  1 /* Read slot number afresh from config */
#define CFG_WRITE_SLOT_NR 2 /* Write new slot number to config */

static void native_update(uint8_t slot_mode)
{
    int i;

    if (slot_mode == CFG_READ_SLOT_NR) {
        /* Populate slot_map[]. */
        memset(&cfg.slot_map, 0xff, sizeof(cfg.slot_map));
        cfg.max_slot_nr = cfg.depth ? 1 : 0;
        F_opendir(&fs->dp, "");
        while (native_dir_next())
            cfg.max_slot_nr++;
        /* Adjust max_slot_nr. Must be at least one 'slot'. */
        if (!cfg.max_slot_nr)
            F_die(FR_NO_DIRENTS);
        cfg.max_slot_nr--;
        F_closedir(&fs->dp);
        /* Select last disk_index if not greater than available slots. */
        cfg.slot_nr = (cfg.slot_nr <= cfg.max_slot_nr) ? cfg.slot_nr : 0;
    }

    if ((ff_cfg.image_on_startup == IMGS_last)
        && (slot_mode == CFG_WRITE_SLOT_NR)) {
        char *p, *q;
        fatfs.cdir = cfg.cfg_cdir;
        F_open(&fs->file, "IMAGE_A.CFG", FA_READ|FA_WRITE);
        printk("Before: "); dump_file();
        /* Read final section of the file. */
        if (f_size(&fs->file) > sizeof(fs->buf))
            F_lseek(&fs->file, f_size(&fs->file) - sizeof(fs->buf));
        F_read(&fs->file, fs->buf, sizeof(fs->buf), NULL);
        F_lseek(&fs->file, (f_size(&fs->file) > sizeof(fs->buf)
                            ? f_size(&fs->file) - sizeof(fs->buf) : 0));
        /* Find end of last subfolder name, if any. */
        if ((p = strrchr(fs->buf, '/')) != NULL) {
            /* Found: seek to after the trailing '/'. */
            F_lseek(&fs->file, f_tell(&fs->file) + (p+1 - fs->buf));
        } else {
            /* No subfolder: we overwrite the entire file. */
            F_lseek(&fs->file, 0);
        }
        if (cfg.slot.attributes & AM_DIR) {
            if (!strcmp(fs->fp.fname, "..")) {
                /* Strip back to next '/' */
                if (!p) F_die(FR_BAD_IMAGECFG); /* must exist */
                *p = '\0';
                if ((q = strrchr(fs->buf, '/')) != NULL) {
                    F_lseek(&fs->file, f_tell(&fs->file) - (p-q));
                } else {
                    F_lseek(&fs->file, 0);
                }
            } else {
                /* Add name plus '/' */
                F_write(&fs->file, fs->fp.fname,
                        strnlen(fs->fp.fname, sizeof(fs->fp.fname)), NULL);
                F_write(&fs->file, "/", 1, NULL);
            }
        } else {
            /* Add name */
            F_write(&fs->file, fs->fp.fname,
                    strnlen(fs->fp.fname, sizeof(fs->fp.fname)), NULL);
        }
        F_truncate(&fs->file);
        printk("After: "); dump_file();
        F_close(&fs->file);
        fatfs.cdir = cfg.cur_cdir;
        cfg.ima_ej_flag = FALSE;
    }
    
    /* Populate current slot. */
    i = cfg.depth ? 1 : 0;
    F_opendir(&fs->dp, "");
    while (native_dir_next()) {
        if (i >= cfg.slot_nr)
            break;
        i++;
    }
    F_closedir(&fs->dp);
    if (i > cfg.slot_nr) {
        /* Must be the ".." folder. */
        snprintf(fs->fp.fname, sizeof(fs->fp.fname), "..");
        fs->fp.fattrib = AM_DIR;
    }
    if (fs->fp.fattrib & AM_DIR) {
        /* Leave the full pathname cached in fs->fp. */
        cfg.slot.attributes = fs->fp.fattrib;
        snprintf(cfg.slot.name, sizeof(cfg.slot.name), "%s", fs->fp.fname);
    } else {
        F_open(&fs->file, fs->fp.fname, FA_READ);
        fs->file.obj.attr = fs->fp.fattrib;
        fatfs_to_slot(&cfg.slot, &fs->file, fs->fp.fname);
        F_close(&fs->file);
    }
}

static void ima_mark_ejected(bool_t ej)
{
    if (cfg.hxc_mode || (ff_cfg.image_on_startup != IMGS_last)
        || (cfg.ima_ej_flag == ej))
        return;

    fatfs.cdir = cfg.cfg_cdir;
    F_open(&fs->file, "IMAGE_A.CFG", FA_READ|FA_WRITE);
    printk("Before: "); dump_file();
    if (ej) {
        F_lseek(&fs->file, f_size(&fs->file));
        F_write(&fs->file, "\\EJ", 3, NULL);
    } else {
        F_lseek(&fs->file, max_t(int, f_size(&fs->file)-3, 0));
        F_truncate(&fs->file);
    }
    printk("After: "); dump_file();
    F_close(&fs->file);
    fatfs.cdir = cfg.cur_cdir;
    cfg.ima_ej_flag = ej;
}

static void hxc_cfg_update(uint8_t slot_mode)
{
    struct hxcsdfe_cfg hxc_cfg;
    struct v1_slot v1_slot;
    struct v2_slot v2_slot;
    BYTE mode = FA_READ;
    int i;

    if (slot_mode == CFG_WRITE_SLOT_NR)
        mode |= FA_WRITE;

    if (ff_cfg.nav_mode == NAVMODE_indexed) {
        FRESULT fr;
        char slot[10];
        hxc_cfg.index_mode = TRUE;
        fatfs.cdir = cfg.cfg_cdir;
        switch (slot_mode) {
        case CFG_READ_SLOT_NR:
            cfg.slot_nr = 0;
            if (ff_cfg.image_on_startup == IMGS_init)
                break;
            if ((fr = F_try_open(&fs->file, "IMAGE_A.CFG", FA_READ)) != FR_OK)
                break;
            F_read(&fs->file, slot, sizeof(slot), NULL);
            F_close(&fs->file);
            slot[sizeof(slot)-1] = '\0';
            cfg.slot_nr = strtol(slot, NULL, 10);
            break;
        case CFG_WRITE_SLOT_NR:
            if (ff_cfg.image_on_startup != IMGS_last)
                break;
            snprintf(slot, sizeof(slot), "%u", cfg.slot_nr);
            F_open(&fs->file, "IMAGE_A.CFG", FA_WRITE | FA_OPEN_ALWAYS);
            F_write(&fs->file, slot, strnlen(slot, sizeof(slot)), NULL);
            F_truncate(&fs->file);
            F_close(&fs->file);
            break;
        }
        fatfs.cdir = cfg.cur_cdir;
        goto indexed_mode;
    }

    slot_from_short_slot(&cfg.slot, &cfg.hxcsdfe);
    fatfs_from_slot(&fs->file, &cfg.slot, mode);
    F_read(&fs->file, &hxc_cfg, sizeof(hxc_cfg), NULL);
    if (strncmp("HXCFECFGV", hxc_cfg.signature, 9))
        goto bad_signature;

    if (slot_mode == CFG_READ_SLOT_NR) {
        /* buzzer_step_duration seems to range 0xFF-0xD8. */
        if (!cfg.ffcfg_has_step_volume)
            ff_cfg.step_volume = hxc_cfg.step_sound
                ? (0x100 - hxc_cfg.buzzer_step_duration) / 2 : 0;
        if (!cfg.ffcfg_has_display_off_secs)
            ff_cfg.display_off_secs = hxc_cfg.back_light_tmr;
        /* Interpret HxC scroll speed as updates per minute. */
        if (!cfg.ffcfg_has_display_scroll_rate && hxc_cfg.lcd_scroll_speed)
            ff_cfg.display_scroll_rate = 60000u / hxc_cfg.lcd_scroll_speed;
    }

    switch (hxc_cfg.signature[9]-'0') {

    case 1: {
        if (slot_mode != CFG_READ_SLOT_NR) {
            /* Keep the already-configured slot number. */
            hxc_cfg.slot_index = cfg.slot_nr;
            if (slot_mode == CFG_WRITE_SLOT_NR) {
                /* Update the config file with new slot number. */
                F_lseek(&fs->file, 0);
                F_write(&fs->file, &hxc_cfg, sizeof(hxc_cfg), NULL);
            }
        }
        cfg.slot_nr = hxc_cfg.slot_index;
        if (hxc_cfg.index_mode)
            break;
        /* Slot mode: initialise slot map and current slot. */
        if (slot_mode == CFG_READ_SLOT_NR) {
            cfg.max_slot_nr = hxc_cfg.number_of_slot - 1;
            memset(&cfg.slot_map, 0xff, sizeof(cfg.slot_map));
        }
        /* Slot mode: read current slot file info. */
        if (cfg.slot_nr == 0) {
            slot_from_short_slot(&cfg.slot, &cfg.autoboot);
        } else {
            F_lseek(&fs->file, 1024 + cfg.slot_nr*128);
            F_read(&fs->file, &v1_slot, sizeof(v1_slot), NULL);
            memcpy(&v2_slot.type, &v1_slot.name[8], 3);
            memcpy(&v2_slot.attributes, &v1_slot.attributes, 1+4+4+17);
            v2_slot.name[17] = '\0';
            slot_from_short_slot(&cfg.slot, &v2_slot);
        }
        break;
    }

    case 2:
        if (slot_mode != CFG_READ_SLOT_NR) {
            hxc_cfg.cur_slot_number = cfg.slot_nr;
            if (slot_mode == CFG_WRITE_SLOT_NR) {
                F_lseek(&fs->file, 0);
                F_write(&fs->file, &hxc_cfg, sizeof(hxc_cfg), NULL);
            }
        }
        cfg.slot_nr = hxc_cfg.cur_slot_number;
        if (hxc_cfg.index_mode)
            break;
        /* Slot mode: initialise slot map and current slot. */
        if (slot_mode == CFG_READ_SLOT_NR) {
            cfg.max_slot_nr = hxc_cfg.max_slot_number - 1;
            F_lseek(&fs->file, hxc_cfg.slots_map_position*512);
            F_read(&fs->file, &cfg.slot_map, sizeof(cfg.slot_map), NULL);
            cfg.slot_map[0] |= 0x80; /* slot 0 always available */
            /* Find true max_slot_nr: */
            while (!slot_valid(cfg.max_slot_nr))
                cfg.max_slot_nr--;
        }
        /* Slot mode: read current slot file info. */
        if (cfg.slot_nr == 0) {
            slot_from_short_slot(&cfg.slot, &cfg.autoboot);
        } else {
            F_lseek(&fs->file, hxc_cfg.slots_position*512
                    + cfg.slot_nr*64*hxc_cfg.number_of_drive_per_slot);
            F_read(&fs->file, &v2_slot, sizeof(v2_slot), NULL);
            slot_from_short_slot(&cfg.slot, &v2_slot);
        }
        break;

    default:
    bad_signature:
        hxc_cfg.signature[15] = '\0';
        printk("Bad signature '%s'\n", hxc_cfg.signature);
        F_die(FR_BAD_HXCSDFE);

    }

    F_close(&fs->file);

indexed_mode:
    if (hxc_cfg.index_mode) {

        char name[16];

        /* Index mode: populate slot_map[]. */
        if (slot_mode == CFG_READ_SLOT_NR) {
            memset(&cfg.slot_map, 0, sizeof(cfg.slot_map));
            cfg.max_slot_nr = 0;
            for (F_findfirst(&fs->dp, &fs->fp, "", "DSKA*.*");
                 fs->fp.fname[0] != '\0';
                 F_findnext(&fs->dp, &fs->fp)) {
                const char *p = fs->fp.fname + 4; /* skip "DSKA" */
                unsigned int idx = 0;
                /* Skip directories. */
                if (fs->fp.fattrib & AM_DIR)
                    continue;
                /* Parse 4-digit index number. */
                for (i = 0; i < 4; i++) {
                    if ((*p < '0') || (*p > '9'))
                        break;
                    idx *= 10;
                    idx += *p++ - '0';
                }
                /* Expect a 4-digit number range 0-999 followed by a period. */
                if ((i != 4) || (*p++ != '.') || (idx > 999))
                    continue;
                /* Expect 3-char extension followed by nul. */
                for (i = 0; (i < 3) && *p; i++, p++)
                    continue;
                if ((i != 3) || (*p != '\0'))
                    continue;
                /* A file type we support? */
                if (!image_valid(&fs->fp))
                    continue;
                /* All is fine, populate the 'slot'. */
                cfg.slot_map[idx/8] |= 0x80 >> (idx&7);
                cfg.max_slot_nr = max_t(
                    uint16_t, cfg.max_slot_nr, idx);
            }
            F_closedir(&fs->dp);
        }

        /* Index mode: populate current slot. */
        for (;;) {
            snprintf(name, sizeof(name), "DSKA%04u.*", cfg.slot_nr);
            F_findfirst(&fs->dp, &fs->fp, "", name);
            F_closedir(&fs->dp);
            /* Found a valid image? */
            if (fs->fp.fname[0])
                break;
            /* Fall back to slot 0. If already there, bail with error. */
            if (cfg.slot_nr == 0)
                F_die(FR_BAD_IMAGE);
            cfg.slot_nr = 0;
        }
        F_open(&fs->file, fs->fp.fname, FA_READ);
        fs->file.obj.attr = fs->fp.fattrib;
        fatfs_to_slot(&cfg.slot, &fs->file, fs->fp.fname);
        F_close(&fs->file);
    }

    for (i = 0; i < sizeof(cfg.slot.type); i++)
        cfg.slot.type[i] = tolower(cfg.slot.type[i]);
}

static void cfg_update(uint8_t slot_mode)
{
    if (cfg.hxc_mode)
        hxc_cfg_update(slot_mode);
    else
        native_update(slot_mode);
    if (!(cfg.slot.attributes & AM_DIR)
        && (ff_cfg.write_protect || usbh_msc_readonly()))
        cfg.slot.attributes |= AM_RDO;
}

/* Based on button presses, change which floppy image is selected. */
static bool_t choose_new_image(uint8_t init_b)
{
    uint8_t b, prev_b;
    time_t last_change = 0;
    int old_slot = cfg.slot_nr, i, changes = 0;

    for (prev_b = 0, b = init_b;
         (b &= (B_LEFT|B_RIGHT)) != 0;
         prev_b = b, b = buttons) {

        if (prev_b == b) {
            /* Decaying delay between image steps while button pressed. */
            time_t delay = time_ms(1000) / (changes + 1);
            if (delay < time_ms(50))
                delay = time_ms(50);
            if (ff_cfg.twobutton_action == TWOBUTTON_rotary_fast)
                delay = time_ms(40);
            if (time_diff(last_change, time_now()) < delay)
                continue;
            changes++;
        } else {
            /* Different button pressed. Takes immediate effect, resets 
             * the continuous-press decaying delay. */
            changes = 0;
        }
        last_change = time_now();

        i = cfg.slot_nr;
        if (!(b ^ (B_LEFT|B_RIGHT))) {
            if (ff_cfg.twobutton_action == TWOBUTTON_eject) {
                cfg.slot_nr = old_slot;
                cfg.ejected = TRUE;
                cfg_update(CFG_KEEP_SLOT_NR);
                break;
            }
            i = cfg.slot_nr = 0;
            cfg_update(CFG_KEEP_SLOT_NR);
            if ((ff_cfg.twobutton_action == TWOBUTTON_rotary)
                || (ff_cfg.twobutton_action == TWOBUTTON_rotary_fast)) {
                /* Wait for button release, then update display, or
                 * immediately enter parent-dir (if we're in a subfolder). */
                while (buttons)
                    continue;
                if (cfg.depth == 0)
                    display_write_slot(TRUE);
                return (cfg.depth != 0);
            }
            display_write_slot(TRUE);
            /* Ignore changes while user is releasing the buttons. */
            while ((time_diff(last_change, time_now()) < time_ms(1000))
                   && buttons)
                continue;
        } else if (b & B_LEFT) {
        b_left:
            do {
                if (i-- == 0) {
                    if (!ff_cfg.nav_loop)
                        goto b_right;
                    i = cfg.max_slot_nr;
                }
            } while (!slot_valid(i));
        } else { /* b & B_RIGHT */
        b_right:
            do {
                if (i++ >= cfg.max_slot_nr) {
                    if (!ff_cfg.nav_loop)
                        goto b_left;
                    i = 0;
                }
            } while (!slot_valid(i));
        }

        cfg.slot_nr = i;
        cfg_update(CFG_KEEP_SLOT_NR);
        display_write_slot(TRUE);
    }

    return FALSE;
}

static void assert_usbh_msc_connected(void)
{
    if (!usbh_msc_connected() || cfg.usb_power_fault)
        F_die(FR_DISK_ERR);
}

static int run_floppy(void *_b)
{
    volatile uint8_t *pb = _b;
    time_t t_now, t_prev, t_diff;
    int32_t lcd_update_ticks;

    floppy_insert(0, &cfg.slot);

    lcd_update_ticks = time_ms(20);
    t_prev = time_now();
    while (((*pb = buttons) == 0) && !floppy_handle()) {
        t_now = time_now();
        t_diff = time_diff(t_prev, t_now);
        if (display_mode == DM_LCD_1602) {
            lcd_update_ticks -= t_diff;
            if (lcd_update_ticks <= 0) {
                lcd_write_track_info(FALSE);
                lcd_update_ticks = time_ms(20);
            }
            lcd_scroll.ticks -= t_diff;
            lcd_scroll_name();
        }
        canary_check();
        assert_usbh_msc_connected();
        t_prev = t_now;
    }

    return 0;
}

static int floppy_main(void *unused)
{
    FRESULT fres;
    char msg[17];
    uint8_t b;
    uint32_t i;

    /* If any buttons are pressed when USB drive is mounted then we start 
     * in ejected state. */
    if (buttons)
        cfg.ejected = TRUE;

    arena_init();
    fs = arena_alloc(sizeof(*fs));
    
    cfg_init();
    cfg_update(CFG_READ_SLOT_NR);

    /* If we start on a folder, go directly into the image selector. */
    if (cfg.slot.attributes & AM_DIR) {
        display_write_slot(FALSE);
        b = buttons;
        goto select;
    }

    for (;;) {

        lcd_scroll.ticks = time_ms(ff_cfg.display_scroll_pause)
            ?: time_ms(ff_cfg.display_scroll_rate);
        lcd_scroll.off = lcd_scroll.end = 0;
        lcd_scroll_init(ff_cfg.display_scroll_pause,
                        ff_cfg.display_scroll_rate);

        /* Make sure slot index is on a valid slot. Find next valid slot if 
         * not (and update config). */
        i = cfg.slot_nr;
        if (!slot_valid(i)) {
            while (!slot_valid(i))
                if (i++ >= cfg.max_slot_nr)
                    i = 0;
            printk("Updated slot %u -> %u\n", cfg.slot_nr, i);
            cfg.slot_nr = i;
            cfg_update(CFG_WRITE_SLOT_NR);
        }

        if (cfg.slot.attributes & AM_DIR) {
            if (!strcmp(fs->fp.fname, "..")) {
                if (cfg.depth == 0)
                    F_die(FR_BAD_IMAGECFG);
                fatfs.cdir = cfg.cur_cdir = cfg.stack[--cfg.depth].cdir;
                cfg.slot_nr = cfg.stack[cfg.depth].slot;
            } else {
                if (cfg.depth == ARRAY_SIZE(cfg.stack))
                    F_die(FR_PATH_TOO_DEEP);
                cfg.stack[cfg.depth].slot = cfg.slot_nr;
                cfg.stack[cfg.depth++].cdir = cfg.cur_cdir;
                F_chdir(fs->fp.fname);
                cfg.cur_cdir = fatfs.cdir;
                cfg.slot_nr = 1;
            }
            cfg_update(CFG_READ_SLOT_NR);
            display_write_slot(FALSE);
            b = buttons;
            goto select;
        }

        fs = NULL;

        display_write_slot(FALSE);
        if (display_mode == DM_LCD_1602)
            lcd_write_track_info(TRUE);

        printk("Current slot: %u/%u\n", cfg.slot_nr, cfg.max_slot_nr);
        printk("Name: '%s' Type: %s\n", cfg.slot.name, cfg.slot.type);
        printk("Attr: %02x Clus: %08x Size: %u\n",
               cfg.slot.attributes, cfg.slot.firstCluster, cfg.slot.size);

        if (cfg.ejected) {
            cfg.ejected = FALSE;
            b = B_SELECT;
        } else {
            fres = F_call_cancellable(run_floppy, &b);
            floppy_cancel();
            assert_usbh_msc_connected();
        }

        arena_init();
        fs = arena_alloc(sizeof(*fs));

        /* When an image is loaded, select button means eject. */
        if (fres || (b & B_SELECT)) {
            /* ** EJECT STATE ** */
            unsigned int wait;
            snprintf(msg, sizeof(msg), "EJECTED");
            switch (display_mode) {
            case DM_LED_7SEG:
                if (fres)
                    snprintf(msg, sizeof(msg), "%c%02u",
                             (fres >= 30) ? 'E' : 'F', fres);
                led_7seg_write_string(msg);
                break;
            case DM_LCD_1602:
                if (fres)
                    snprintf(msg, sizeof(msg), "*%s*%02u*",
                             (fres >= 30) ? "ERR" : "FAT", fres);
                display_wp_status();
                lcd_write(wp_column+1, 1, -1, "");
                lcd_write((lcd_columns > 16) ? 10 : 8, 1, 0, msg);
                lcd_on();
                break;
            }
            if (fres == FR_OK)
                ima_mark_ejected(TRUE);
            fres = FR_OK;
            /* Wait for buttons to be released. */
            wait = 0;
            while (buttons != 0) {
                delay_ms(1);
                if (wait++ >= 2000) {
                toggle_wp:
                    wait = 0;
                    cfg.slot.attributes ^= AM_RDO;
                    if (usbh_msc_readonly()) {
                        /* Read-only filesystem: force AM_RDO always. */
                        cfg.slot.attributes |= AM_RDO;
                    }
                    display_wp_status();
                    if (display_mode == DM_LED_7SEG)
                        led_7seg_write_string((cfg.slot.attributes & AM_RDO)
                                              ? "RDO" : "RIT");
                }
            }
            /* Wait for any button to be pressed. */
            wait = 0;
            while ((b = buttons) == 0) {
                /* Bail if USB disconnects. */
                assert_usbh_msc_connected();
                /* Update the display. */
                delay_ms(1);
                switch (display_mode) {
                case DM_LED_7SEG:
                    /* Alternate the 7-segment display. */
                    if ((++wait % 1000) == 0) {
                        switch (wait / 1000) {
                        case 1:
                            led_7seg_write_decimal(cfg.slot_nr);
                            break;
                        default:
                            led_7seg_write_string(msg);
                            wait = 0;
                            break;
                        }
                    }
                    break;
                case DM_LCD_1602:
                    /* Continue to scroll long filename. */
                    lcd_scroll.ticks -= time_ms(1);
                    lcd_scroll_name();
                    break;
                }
            }
            if (ff_cfg.twobutton_action == TWOBUTTON_eject) {
                /* Wait 50ms for 2-button press. */
                for (wait = 0; wait < 50; wait++) {
                    b = buttons;
                    if ((b & (B_LEFT|B_RIGHT)) == (B_LEFT|B_RIGHT))
                        b = B_SELECT;
                    if (b & B_SELECT)
                        break;
                    delay_ms(1);
                }
            }
            /* Reload same image immediately if eject pressed again. */
            if (b & B_SELECT) {
                /* Wait for eject button to be released. */
                wait = 0;
                while (b & B_SELECT) {
                    b = buttons;
                    if ((ff_cfg.twobutton_action == TWOBUTTON_eject) && b) {
                        /* Wait for 2-button release. */
                        b = B_SELECT;
                    }
                    delay_ms(1);
                    if (wait++ >= 2000)
                        goto toggle_wp;
                }
                ima_mark_ejected(FALSE);
                continue;
            }
        }

        /* No buttons pressed: re-read config and carry on. */
        if (b == 0) {
            cfg_update(CFG_READ_SLOT_NR);
            continue;
        }

    select:
        lcd_scroll.off = lcd_scroll.end = 0;
        do {
            unsigned int wait_ms;

            /* While buttons are pressed we poll them and update current image
             * accordingly. */
            cfg.ejected = FALSE;
            if (choose_new_image(b) || cfg.ejected)
                break;

            /* Wait a few seconds for further button presses before acting on 
             * the new image selection. */
            if (cfg_scroll_reset)
                lcd_scroll.off = lcd_scroll.end = 0;
            lcd_scroll_init(0, ff_cfg.nav_scroll_rate);
            lcd_scroll.ticks = time_ms(ff_cfg.nav_scroll_pause);
            wait_ms = (cfg.slot.attributes & AM_DIR) ?
                ff_cfg.autoselect_folder_secs : ff_cfg.autoselect_file_secs;
            wait_ms *= 1000;
            if (wait_ms && (display_mode == DM_LCD_1602)) {
                /* Allow time for full name to scroll through. */
                unsigned int scroll_ms = ff_cfg.nav_scroll_pause;
                scroll_ms += lcd_scroll.end * ff_cfg.nav_scroll_rate;
                wait_ms = max(wait_ms, scroll_ms);
            }
            for (i = 0; (wait_ms == 0) || (i < wait_ms); i++) {
                b = buttons;
                if (b != 0)
                    break;
                assert_usbh_msc_connected();
                delay_ms(1);
                lcd_scroll.ticks -= time_ms(1);
                lcd_scroll_name();
            }

            /* Flash the LED display to indicate loading the new image. */
            if (!(b & (B_LEFT|B_RIGHT)) && (display_mode == DM_LED_7SEG)) {
                led_7seg_display_setting(FALSE);
                delay_ms(200);
                led_7seg_display_setting(TRUE);
                b = buttons;
            }

            /* Wait for select button to be released. */
            while ((b = buttons) & B_SELECT)
                continue;

        } while (b != 0);

        /* Write the slot number resulting from the latest round of button 
         * presses back to the config file. */
        cfg_update(CFG_WRITE_SLOT_NR);
    }

    ASSERT(0);
    return 0;
}

static void cfg_maybe_factory_reset(void)
{
    unsigned int i;
    uint8_t b = buttons;

    /* Need both LEFT and RIGHT pressed, or SELECT alone. */
    if ((b != (B_LEFT|B_RIGHT)) && (b != B_SELECT))
        return;

    /* Buttons must be continuously pressed for three seconds. */
    for (i = 0; (i < 3000) && (buttons == b); i++)
        delay_ms(1);
    if (buttons != b)
        return;

    /* Inform user that factory reset is about to occur. */
    switch (display_mode) {
    case DM_LED_7SEG:
        led_7seg_write_string("RST");
        break;
    case DM_LCD_1602:
        lcd_clear();
        lcd_write(0, 0, 0, "Reset Flash");
        lcd_write(0, 1, 0, "Configuration");
        lcd_on();
        break;
    }

    /* Wait for buttons to be released... */
    while (buttons)
        continue;

    /* ...and then do the Flash erase. */
    flash_ff_cfg_erase();

    /* Linger so user sees it is done. */
    delay_ms(2000);

    /* Reset so that changes take effect. */
    system_reset();
}

static void banner(void)
{
    switch (display_mode) {
    case DM_LED_7SEG:
        led_7seg_write_string((led_7seg_nr_digits() == 3) ? "F-F" : "FF");
        break;
    case DM_LCD_1602:
        lcd_clear();
        lcd_write(0, 0, 0, "FlashFloppy");
        lcd_write(0, 1, 0, "v");
        lcd_write(1, 1, 0, FW_VER);
        lcd_on();
        break;
    }
}

static void maybe_show_version(void)
{
    uint8_t b, nb;
    char *p, *np, msg[3];
    int len;

    /* LCD/OLED already displays version info in idle state. */
    if (display_mode != DM_LED_7SEG)
        return;

    /* Check if right button is pressed and released. */
    if ((b = buttons) != B_RIGHT)
        return;
    while ((nb = buttons) == b)
        continue;
    if (nb)
        return;

    /* Iterate through the dotted sections of the version number. */
    for (p = FW_VER; p != NULL; p = np ? np+1 : NULL) {
        np = strchr(p, '.');
        memset(msg, ' ', sizeof(msg));
        len = min_t(int, np ? np - p : strnlen(p, sizeof(msg)), sizeof(msg));
        memcpy(&msg[sizeof(msg) - len], p, len);
        led_7seg_write_string(msg);
        delay_ms(1000);
    }

    banner();
}

static void handle_errors(FRESULT fres)
{
    char msg[17];
    bool_t pwr = cfg.usb_power_fault;

    if (pwr) {
        printk("USB Power Fault detected!\n");
        snprintf(msg, sizeof(msg), "USB Power Fault");
    } else if (usbh_msc_connected() && (fres != FR_OK)) {
        printk("FATFS RETURN CODE: %u\n", fres);
        snprintf(msg, sizeof(msg),
                 (display_mode == DM_LED_7SEG)
                 ? ((fres >= 30) ? "E%02u" : "F%02u")
                 : ((fres >= 30) ? "*ERROR* %02u" : "*FATFS* %02u"),
                 fres);
    } else {
        /* No error. Do nothing. */
        return;
    }

    switch (display_mode) {
    case DM_LED_7SEG:
        led_7seg_write_string(msg);
        break;
    case DM_LCD_1602:
        lcd_write(0, 0, -1, "***************");
        lcd_write(0, 1, -1, msg);
        lcd_on();
        break;
    }

    /* Wait for buttons to be released, pressed and released again. */
    while (buttons)
        continue;
    while (!buttons && (pwr || usbh_msc_connected()))
        continue;
    while (buttons)
        continue;

    /* On USB power fault we simply reset. */
    if (pwr)
        system_reset();
}

int main(void)
{
    FRESULT fres;

    /* Relocate DATA. Initialise BSS. */
    if (_sdat != _ldat)
        memcpy(_sdat, _ldat, _edat-_sdat);
    memset(_sbss, 0, _ebss-_sbss);

    canary_init();
    stm32_init();
    time_init();
    console_init();
    console_crash_on_input();
    board_init();
    delay_ms(200); /* 5v settle */

    printk("\n** FlashFloppy v%s for Gotek\n", FW_VER);
    printk("** Keir Fraser <keir.xen@gmail.com>\n");
    printk("** https://github.com/keirf/FlashFloppy\n\n");

    printk("Board: %s\n",
           (board_id == BRDREV_Gotek_standard) ? "Standard" : "Enhanced");

    speaker_init();

    flash_ff_cfg_read();

    floppy_init();

    display_init();

    usbh_msc_init();

    rotary = (gpioc->idr >> 10) & 3;
    timer_init(&button_timer, button_timer_fn, NULL);
    timer_set(&button_timer, time_now());

    for (;;) {

        banner();

        arena_init();
        usbh_msc_buffer_set(arena_alloc(512));
        while ((f_mount(&fatfs, "", 1) != FR_OK) && !cfg.usb_power_fault) {
            maybe_show_version();
            cfg_maybe_factory_reset();
            usbh_msc_process();
        }
        usbh_msc_buffer_set((void *)0xdeadbeef);

        arena_init();
        fres = F_call_cancellable(floppy_main, NULL);
        floppy_cancel();

        handle_errors(fres);
    }

    return 0;
}

/*
 * Local variables:
 * mode: C
 * c-file-style: "Linux"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */

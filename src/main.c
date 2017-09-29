/*
 * main.c
 * 
 * Bootstrap the STM32F103C8T6 and get things moving.
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
    uint8_t backlight_on_secs;
    uint16_t lcd_scroll_msec;
    struct v2_slot autoboot, hxcsdfe, slot;
    uint32_t cfg_cdir, cur_cdir;
    uint16_t cdir_stack[20];
    uint8_t depth;
    bool_t lastdisk_committed;
} cfg;

static bool_t first_startup = TRUE;
static bool_t ejected;

/* Wrap slot number at 0 and max? */
#define config_nav_loop TRUE

static uint8_t cfg_mode;
#define CFG_none      0 /* Iterate through all images in root. */
#define CFG_hxc       1 /* Operation based on HXCSDFE.CFG. */
#define CFG_lastidx   2 /* remember last image but dont store any config. */

uint8_t board_id;

#define IMAGE_SELECT_WAIT_SECS 2
#define BACKLIGHT_ON_SECS      20
#define LCD_SCROLL_MSEC        400
#define LCD_SCROLL_PAUSE_MSEC  2000

static uint32_t backlight_ticks;
static uint8_t backlight_state;
#define BACKLIGHT_OFF          0
#define BACKLIGHT_SWITCHING_ON 1
#define BACKLIGHT_ON           2

/* Turn the LCD backlight on, reset the switch-off handler and ticker. */
static void lcd_on(void)
{
    if (display_mode != DM_LCD_1602)
        return;
    backlight_ticks = 0;
    barrier();
    backlight_state = BACKLIGHT_ON;
    barrier();
    lcd_backlight(cfg.backlight_on_secs != 0);
}

static bool_t slot_type(const char *str)
{
    char ext[4];
    filename_extension(cfg.slot.name, ext, sizeof(ext));
    if (!strcmp(ext, str))
        return TRUE;
    memcpy(ext, cfg.slot.type, 3);
    ext[3] = '\0';
    return !strcmp(ext, str);
}

/* Write slot info to LCD. */
static void lcd_write_slot(void)
{
    char msg[17], *type;
    if (display_mode != DM_LCD_1602)
        return;
    snprintf(msg, sizeof(msg), "%s", cfg.slot.name);
    lcd_write(0, 0, 16, msg);
    type = (cfg.slot.attributes & AM_DIR) ? "DIR"
        : slot_type("adf") ? "ADF"
        : slot_type("hfe") ? "HFE"
        : slot_type("img") ? "IMG"
        : slot_type("ima") ? "IMA"
        : slot_type("st") ? "ST "
        : "UNK";
    snprintf(msg, sizeof(msg), "%03u/%03u %s D:%u",
             cfg.slot_nr, cfg.max_slot_nr, type, cfg.depth);
    lcd_write(0, 1, 16, msg);
    lcd_on();
}

/* Write track number to LCD. */
static uint8_t lcd_cyl, lcd_side;
static int32_t lcd_update_ticks;
static void lcd_write_track_info(bool_t force)
{
    uint8_t cyl, side, sel;
    char msg[17];
    if (display_mode != DM_LCD_1602)
        return;
    floppy_get_track(&cyl, &side, &sel);
    cyl = min_t(uint8_t, cyl, 99);
    side = min_t(uint8_t, side, 1);
    if (force || (cyl != lcd_cyl) || ((side != lcd_side) && sel)) {
        snprintf(msg, sizeof(msg), "T:%02u S:%u", cyl, side);
        lcd_write(8, 1, 0, msg);
        lcd_cyl = cyl;
        lcd_side = side;
    }
}

/* Scroll long filename within 16-character window. */
static uint8_t lcd_scroll_off, lcd_scroll_end;
static int32_t lcd_scroll_ticks;
static void lcd_scroll_name(void)
{
    char msg[17];
    if (lcd_scroll_ticks > 0)
        return;
    if (++lcd_scroll_off > lcd_scroll_end)
        lcd_scroll_off = 0;
    snprintf(msg, sizeof(msg), "%s", cfg.slot.name + lcd_scroll_off);
    lcd_write(0, 0, 16, msg);
    lcd_scroll_ticks =
        ((lcd_scroll_off == 0)
         || (lcd_scroll_off == lcd_scroll_end))
        ? stk_ms(LCD_SCROLL_PAUSE_MSEC) : stk_ms(cfg.lcd_scroll_msec);
}

/* Handle switching the LCD backlight. */
static uint8_t lcd_handle_backlight(uint8_t b)
{
    if ((display_mode != DM_LCD_1602)
        || (cfg.backlight_on_secs == 0)
        || (cfg.backlight_on_secs == 0xff))
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
        if (backlight_ticks++ >= 200*cfg.backlight_on_secs) {
            lcd_backlight(FALSE);
            backlight_state = BACKLIGHT_OFF;
        }
        break;
    }

    return b;
}

static struct timer button_timer;
static volatile uint8_t buttons;
#define B_LEFT 1
#define B_RIGHT 2
#define B_SELECT 4
static void button_timer_fn(void *unused)
{
    static uint16_t bl, br, bs;
    static uint8_t rotary;
    uint8_t b = 0;

    /* We debounce the switches by waiting for them to be pressed continuously 
     * for 16 consecutive sample periods (16 * 5ms == 80ms) */

    bl <<= 1;
    bl |= gpio_read_pin(gpioc, 8);
    if (bl == 0)
        b |= B_LEFT;

    br <<= 1;
    br |= gpio_read_pin(gpioc, 7);
    if (br == 0)
        b |= B_RIGHT;

    bs <<= 1;
    bs |= gpio_read_pin(gpioc, 6);
    if (bs == 0)
        b |= B_SELECT;

    /* Rotary encoder: we look for a 1->0 edge (falling edge) on pin A. 
     * Pin B then tells us the direction (left or right). */
    rotary <<= 1;
    rotary |= gpio_read_pin(gpioc, 10);
    if ((rotary & 0x03) == 0x02)
        b |= (gpio_read_pin(gpioc, 11) == 0) ? B_LEFT : B_RIGHT;

    b = lcd_handle_backlight(b);

    /* Latch final button state and reset the timer. */
    buttons = b;
    timer_set(&button_timer, stk_add(button_timer.deadline, stk_ms(5)));
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

void fatfs_from_slot(FIL *file, const struct v2_slot *slot, BYTE mode)
{
    memset(file, 0, sizeof(*file));
    file->obj.fs = &fatfs;
    file->obj.id = fatfs.id;
    file->obj.attr = slot->attributes;
    file->obj.sclust = slot->firstCluster;
    file->obj.objsize = slot->size;
    file->flag = mode;
    /* WARNING: dir_ptr, dir_sect are unknown. */
}

static void fatfs_to_slot(struct v2_slot *slot, FIL *file, const char *name)
{
    const char *dot = strrchr(name, '.');
    unsigned int i;

    slot->attributes = file->obj.attr;
    slot->firstCluster = file->obj.sclust;
    slot->size = file->obj.objsize;
    snprintf(slot->name, sizeof(slot->name), "%s", name);
    memcpy(slot->type, dot+1, 3);
    for (i = 0; i < 3; i++)
        slot->type[i] = tolower(slot->type[i]);
}

static void dump_file(void)
{
    F_lseek(&fs->file, 0);
    printk("[");
    do {
        F_read(&fs->file, fs->buf, sizeof(fs->buf), NULL);
        printk("%s", fs->buf);
    } while (!f_eof(&fs->file));
    printk("]\n");
}

static bool_t no_cfg_dir_next(void)
{
    do {
        F_readdir(&fs->dp, &fs->fp);
        if (fs->fp.fname[0] == '\0')
            return FALSE;
        if ((fs->fp.fattrib & AM_DIR) && (display_mode == DM_LCD_1602)
            && ((cfg.depth != 0) || strcmp(fs->fp.fname, "FF")))
            break;
    } while (!image_valid(&fs->fp));
    return TRUE;
}

static uint8_t cfg_init(void)
{
    struct hxcsdfe_cfg hxc_cfg;
    unsigned int sofar;
    char *p;
    uint8_t type;
    FRESULT fr;

    cfg.slot_nr = cfg.depth = 0;
    cfg.lastdisk_committed = FALSE;
    cfg.cur_cdir = fatfs.cdir;

    fr = f_chdir("FF");
    cfg.cfg_cdir = fatfs.cdir;

    fr = F_try_open(&fs->file, "HXCSDFE.CFG", FA_READ|FA_WRITE);
    if (fr)
        goto no_config;
    fatfs_to_slot(&cfg.hxcsdfe, &fs->file, "HXCSDFE.CFG");
    F_read(&fs->file, &hxc_cfg, sizeof(hxc_cfg), NULL);
    if (first_startup && (hxc_cfg.startup_mode & HXCSTARTUP_slot0)) {
        /* Startup mode: slot 0. */
        hxc_cfg.slot_index = hxc_cfg.cur_slot_number = 0;
        F_lseek(&fs->file, 0);
        F_write(&fs->file, &hxc_cfg, sizeof(hxc_cfg), NULL);
    }
    if (first_startup && (hxc_cfg.startup_mode & HXCSTARTUP_ejected)) {
        /* Startup mode: eject. */
        ejected = TRUE;
    }
        
    F_close(&fs->file);

    /* Indexed mode (DSKAxxxx.HFE) does not need AUTOBOOT.HFE. */
    if (!strncmp("HXCFECFGV", hxc_cfg.signature, 9) && hxc_cfg.index_mode) {
        memset(&cfg.autoboot, 0, sizeof(cfg.autoboot));
        type = CFG_hxc;
        goto out;
    }

    fr = F_try_open(&fs->file, "AUTOBOOT.HFE", FA_READ);
    if (fr)
        goto no_config;
    fatfs_to_slot(&cfg.autoboot, &fs->file, "AUTOBOOT.HFE");
    F_close(&fs->file);

    type = CFG_hxc;
    goto out;

no_config:
    fr = F_try_open(&fs->file, "LASTDISK.IDX", FA_READ|FA_WRITE);
    if (fr) {
        type = CFG_none;
        goto out;
    }

    /* Process LASTDISK.IDX file. */
    sofar = 0; /* bytes consumed so far */
    fatfs.cdir = cfg.cur_cdir;
    for (;;) {
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
        cfg.cdir_stack[cfg.depth++] = fatfs.cdir;
        fr = f_chdir(fs->buf);
        if (fr) {
            /* Error! Clear the LASTDISK.IDX file. */
            printk("LASTDISK.IDX is bad: clearing it\n");
            F_lseek(&fs->file, 0);
            F_truncate(&fs->file);
            F_close(&fs->file);
            fatfs.cdir = cfg.cur_cdir;
            cfg.depth = 0;
            type = CFG_lastidx;
            goto out;
        }
        /* Seek on to next pathname section. */
        sofar += p - fs->buf;
        F_lseek(&fs->file, sofar);
    }
    F_close(&fs->file);
    cfg.cur_cdir = fatfs.cdir;
    type = CFG_lastidx;
    if (p != fs->buf) {
        /* If there was a non-empty non-terminated pathname section, it 
         * must be the name of the currently-selected image file. */
        printk("%u:F: '%s'\n", cfg.depth, fs->buf);
        F_opendir(&fs->dp, "");
        cfg.slot_nr = cfg.depth ? 1 : 0;
        while (no_cfg_dir_next()) {
            if (!strcmp(fs->fp.fname, fs->buf)) {
                /* Yes, last disk image was committed. Tell our caller to load 
                 * it immediately rather than jumping to the selector. */
                cfg.lastdisk_committed = TRUE;
                break;
            }
            cfg.slot_nr++;
        }
        F_closedir(&fs->dp);
    }
    /* Sensible default slot to hover over in the selector. */
    if (!cfg.lastdisk_committed)
        cfg.slot_nr = cfg.depth ? 1 : 0;

out:
    fatfs.cdir = cfg.cur_cdir;
    return type;
}

#define CFG_KEEP_SLOT_NR  0 /* Do not re-read slot number from config */
#define CFG_READ_SLOT_NR  1 /* Read slot number afresh from config */
#define CFG_WRITE_SLOT_NR 2 /* Write new slot number to config */

static void no_cfg_update(uint8_t slot_mode)
{
    int i;

    if (slot_mode == CFG_READ_SLOT_NR) {

        /* Default settings. */
        speaker_volume(10);
        cfg.backlight_on_secs = BACKLIGHT_ON_SECS;
        cfg.lcd_scroll_msec = LCD_SCROLL_MSEC;

        /* Populate slot_map[]. */
        memset(&cfg.slot_map, 0xff, sizeof(cfg.slot_map));
        cfg.max_slot_nr = cfg.depth ? 1 : 0;
        F_opendir(&fs->dp, "");
        while (no_cfg_dir_next())
            cfg.max_slot_nr++;
        /* Adjust max_slot_nr. Must be at least one 'slot'. */
        if (!cfg.max_slot_nr)
            F_die();
        cfg.max_slot_nr--;
        F_closedir(&fs->dp);
        /* Select last disk_index if not greater than available slots. */
        printk("READ: %u %u/%u\n", cfg.depth, cfg.slot_nr, cfg.max_slot_nr);
        cfg.slot_nr = (cfg.slot_nr <= cfg.max_slot_nr) ? cfg.slot_nr : 0;
    }

    if ((cfg_mode == CFG_lastidx) && (slot_mode == CFG_WRITE_SLOT_NR)) {
        char slot[10], *p, *q;
        snprintf(slot, sizeof(slot), "%u", cfg.slot_nr);
        fatfs.cdir = cfg.cfg_cdir;
        F_open(&fs->file, "LASTDISK.IDX", FA_READ|FA_WRITE);
        printk("Before: "); dump_file(); F_lseek(&fs->file, 0);
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
                if (!p) F_die(); /* must exist */
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
        printk("After: "); dump_file(); F_lseek(&fs->file, 0);
        F_close(&fs->file);
        fatfs.cdir = cfg.cur_cdir;
    }
    
    /* Populate current slot. */
    i = cfg.depth ? 1 : 0;
    F_opendir(&fs->dp, "");
    while (no_cfg_dir_next()) {
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

static void hxc_cfg_update(uint8_t slot_mode)
{
    struct hxcsdfe_cfg hxc_cfg;
    BYTE mode = FA_READ;
    int i;

    if (slot_mode == CFG_WRITE_SLOT_NR)
        mode |= FA_WRITE;

    fatfs_from_slot(&fs->file, &cfg.hxcsdfe, mode);
    F_read(&fs->file, &hxc_cfg, sizeof(hxc_cfg), NULL);
    if (strncmp("HXCFECFGV", hxc_cfg.signature, 9))
        goto bad_signature;

    if (slot_mode == CFG_READ_SLOT_NR) {
        /* buzzer_step_duration seems to range 0xFF-0xD8. */
        speaker_volume(hxc_cfg.step_sound
                       ? (0x100 - hxc_cfg.buzzer_step_duration) / 2 : 0);
        cfg.backlight_on_secs = hxc_cfg.back_light_tmr;
        cfg.lcd_scroll_msec = LCD_SCROLL_MSEC;
        /* Interpret HxC scroll speed as updates per minute. */
        if (hxc_cfg.lcd_scroll_speed)
            cfg.lcd_scroll_msec = 60000u / hxc_cfg.lcd_scroll_speed;
    }

    switch (hxc_cfg.signature[9]-'0') {

    case 1: {
        struct v1_slot v1_slot;
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
            memcpy(&cfg.slot, &cfg.autoboot, sizeof(cfg.slot));
        } else {
            F_lseek(&fs->file, 1024 + cfg.slot_nr*128);
            F_read(&fs->file, &v1_slot, sizeof(v1_slot), NULL);
            memcpy(&cfg.slot.type, &v1_slot.name[8], 3);
            memcpy(&cfg.slot.attributes, &v1_slot.attributes, 1+4+4+17);
            cfg.slot.name[17] = '\0';
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
            while (!(cfg.slot_map[cfg.max_slot_nr/8]
                     & (0x80>>(cfg.max_slot_nr&7))))
                cfg.max_slot_nr--;
        }
        /* Slot mode: read current slot file info. */
        if (cfg.slot_nr == 0) {
            memcpy(&cfg.slot, &cfg.autoboot, sizeof(cfg.slot));
        } else {
            F_lseek(&fs->file, hxc_cfg.slots_position*512
                    + cfg.slot_nr*64*hxc_cfg.number_of_drive_per_slot);
            F_read(&fs->file, &cfg.slot, sizeof(cfg.slot), NULL);
        }
        break;

    default:
    bad_signature:
        hxc_cfg.signature[15] = '\0';
        printk("Bad signature '%s'\n", hxc_cfg.signature);
        F_die();

    }

    F_close(&fs->file);

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
        snprintf(name, sizeof(name), "DSKA%04u.*", cfg.slot_nr);
        F_findfirst(&fs->dp, &fs->fp, "", name);
        F_closedir(&fs->dp);
        if (fs->fp.fname[0]) {
            F_open(&fs->file, fs->fp.fname, FA_READ);
            fs->file.obj.attr = fs->fp.fattrib;
            fatfs_to_slot(&cfg.slot, &fs->file, fs->fp.fname);
            F_close(&fs->file);
        }
    }

    for (i = 0; i < 3; i++)
        cfg.slot.type[i] = tolower(cfg.slot.type[i]);
}

static void cfg_update(uint8_t slot_mode)
{
    switch (cfg_mode) {
    case CFG_none:
    case CFG_lastidx:
        no_cfg_update(slot_mode);
        break;
    case CFG_hxc:
        hxc_cfg_update(slot_mode);
        break;
    }
}

/* Based on button presses, change which floppy image is selected. */
static void choose_new_image(uint8_t init_b)
{
    uint8_t b, prev_b;
    stk_time_t last_change = 0;
    int i, changes = 0;

    for (prev_b = 0, b = init_b;
         (b &= (B_LEFT|B_RIGHT)) != 0;
         prev_b = b, b = buttons) {

        if (prev_b == b) {
            /* Decaying delay between image steps while button pressed. */
            stk_time_t delay = stk_ms(1000) / (changes + 1);
            if (delay < stk_ms(50))
                delay = stk_ms(50);
            if (stk_diff(last_change, stk_now()) < delay)
                continue;
            changes++;
        } else {
            /* Different button pressed. Takes immediate effect, resets 
             * the continuous-press decaying delay. */
            changes = 0;
        }
        last_change = stk_now();

        i = cfg.slot_nr;
        if (!(b ^ (B_LEFT|B_RIGHT))) {
            i = cfg.slot_nr = 0;
            switch (display_mode) {
            case DM_LED_7SEG:
                led_7seg_write_decimal(0);
                break;
            case DM_LCD_1602:
                cfg_update(CFG_KEEP_SLOT_NR);
                lcd_write_slot();
                break;
            }
            /* Ignore changes while user is releasing the buttons. */
            while ((stk_diff(last_change, stk_now()) < stk_ms(1000))
                   && buttons)
                continue;
        } else if (b & B_LEFT) {
        b_left:
            do {
                if (i-- == 0) {
                    if (!config_nav_loop)
                        goto b_right;
                    i = cfg.max_slot_nr;
                }
            } while (!(cfg.slot_map[i/8] & (0x80>>(i&7))));
        } else { /* b & B_RIGHT */
        b_right:
            do {
                if (i++ >= cfg.max_slot_nr) {
                    if (!config_nav_loop)
                        goto b_left;
                    i = 0;
                }
            } while (!(cfg.slot_map[i/8] & (0x80>>(i&7))));
        }

        cfg.slot_nr = i;
        switch (display_mode) {
        case DM_LED_7SEG:
            led_7seg_write_decimal(cfg.slot_nr);
            break;
        case DM_LCD_1602:
            cfg_update(CFG_KEEP_SLOT_NR);
            lcd_write_slot();
            break;
        }

    }
}

int floppy_main(void)
{
    stk_time_t t_now, t_prev, t_diff;
    char msg[4];
    uint8_t b;
    uint32_t i;

    arena_init();
    fs = arena_alloc(sizeof(*fs));
    
    cfg_mode = cfg_init();
    cfg_update(CFG_READ_SLOT_NR);
    first_startup = FALSE;

    /* In lastdisk mode, go straight into selector if nothing selected. */
    if ((cfg_mode == CFG_lastidx) && !cfg.lastdisk_committed) {
        lcd_write_slot();
        b = buttons;
        goto select;
    }

    for (;;) {

        /* Make sure slot index is on a valid slot. Find next valid slot if 
         * not (and update config). */
        i = cfg.slot_nr;
        if (!(cfg.slot_map[i/8] & (0x80>>(i&7)))) {
            while (!(cfg.slot_map[i/8] & (0x80>>(i&7))))
                if (i++ >= cfg.max_slot_nr)
                    i = 0;
            printk("Updated slot %u -> %u\n", cfg.slot_nr, i);
            cfg.slot_nr = i;
            cfg_update(CFG_WRITE_SLOT_NR);
        }

        if (cfg.slot.attributes & AM_DIR) {
            if (!strcmp(fs->fp.fname, "..")) {
                fatfs.cdir = cfg.cur_cdir = cfg.cdir_stack[--cfg.depth];
                cfg.slot_nr = 0;
            } else {
                cfg.cdir_stack[cfg.depth++] = cfg.cur_cdir;
                F_chdir(fs->fp.fname);
                cfg.cur_cdir = fatfs.cdir;
                cfg.slot_nr = 1;
            }
            cfg_update(CFG_READ_SLOT_NR);
            lcd_write_slot();
            b = buttons;
            goto select;
        }

        fs = NULL;

        switch (display_mode) {
        case DM_LED_7SEG:
            led_7seg_write_decimal(cfg.slot_nr);
            break;
        case DM_LCD_1602:
            lcd_write_slot();
            lcd_write_track_info(TRUE);
            break;
        }

        printk("Current slot: %u/%u\n", cfg.slot_nr, cfg.max_slot_nr);
        memcpy(msg, cfg.slot.type, 3);
        msg[3] = '\0';
        printk("Name: '%s' Type: %s\n", cfg.slot.name, msg);
        printk("Attr: %02x Clus: %08x Size: %u\n",
               cfg.slot.attributes, cfg.slot.firstCluster, cfg.slot.size);

        if (ejected) {

            ejected = FALSE;
            b = B_SELECT;

        } else {

            floppy_insert(0, &cfg.slot);

            lcd_update_ticks = stk_ms(20);
            lcd_scroll_ticks = stk_ms(LCD_SCROLL_PAUSE_MSEC);
            lcd_scroll_off = 0;
            lcd_scroll_end = max_t(
                int, strnlen(cfg.slot.name, sizeof(cfg.slot.name)) - 16, 0);
            t_prev = stk_now();
            while (((b = buttons) == 0) && !floppy_handle()) {
                t_now = stk_now();
                t_diff = stk_diff(t_prev, t_now);
                if (display_mode == DM_LCD_1602) {
                    lcd_update_ticks -= t_diff;
                    if (lcd_update_ticks <= 0) {
                        lcd_write_track_info(FALSE);
                        lcd_update_ticks = stk_ms(20);
                    }
                    lcd_scroll_ticks -= t_diff;
                    lcd_scroll_name();
                }
                canary_check();
                if (!usbh_msc_connected())
                    F_die();
                t_prev = t_now;
            }

            floppy_cancel();

        }

        arena_init();
        fs = arena_alloc(sizeof(*fs));

        /* When an image is loaded, select button means eject. */
        if (b & B_SELECT) {
            /* ** EJECT STATE ** */
            switch (display_mode) {
            case DM_LED_7SEG:
                led_7seg_write_string("EJECT");
                break;
            case DM_LCD_1602:
                lcd_write(0, 1, 8, "EJECT");
                break;
            }
            /* Wait for eject button to be released. */
            while (buttons & B_SELECT)
                continue;
            /* Wait for any button to be pressed. */
            while ((b = buttons) == 0)
                continue;
            /* Reload same image immediately if eject pressed again. */
            if (b & B_SELECT) {
                while (buttons & B_SELECT)
                    continue;
                continue;
            }
        }

        /* No buttons pressed: re-read config and carry on. */
        if (b == 0) {
            cfg_update(CFG_READ_SLOT_NR);
            continue;
        }

    select:
        do {
            /* While buttons are pressed we poll them and update current image
             * accordingly. */
            choose_new_image(b);

            /* Wait a few seconds for further button presses before acting on 
             * the new image selection. */
            for (i = 0; i < IMAGE_SELECT_WAIT_SECS*1000; i++) {
                b = buttons;
                if (b != 0)
                    break;
                delay_ms(1);
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

int main(void)
{
    FRESULT fres;
    uint8_t fintf_mode;

    /* Relocate DATA. Initialise BSS. */
    if (_sdat != _ldat)
        memcpy(_sdat, _ldat, _edat-_sdat);
    memset(_sbss, 0, _ebss-_sbss);

    canary_init();

    stm32_init();
    timers_init();

    console_init();
    console_crash_on_input();

    board_init();

    /* Wait for 5v power to stabilise before initing external peripherals. */
    delay_ms(200);

    printk("\n** FlashFloppy v%s for Gotek\n", FW_VER);
    printk("** Keir Fraser <keir.xen@gmail.com>\n");
    printk("** https://github.com/keirf/FlashFloppy\n\n");

    speaker_init();

    /* Jumper JC selects default floppy interface configuration:
     *   - No Jumper: Shugart
     *   - Jumpered:  PC */
    fintf_mode = gpio_read_pin(gpiob, 1) ? FINTF_SHUGART : FINTF_PC;
    floppy_init(fintf_mode);

    display_init();

    usbh_msc_init();

    cfg.backlight_on_secs = BACKLIGHT_ON_SECS;
    timer_init(&button_timer, button_timer_fn, NULL);
    timer_set(&button_timer, stk_now());

    for (;;) {

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

        while (f_mount(&fatfs, "", 1) != FR_OK)
            usbh_msc_process();

        arena_init();
        fres = F_call_cancellable(floppy_main);
        floppy_cancel();
        printk("FATFS RETURN CODE: %u\n", fres);
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

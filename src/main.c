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

static const char image_a[] = "IMAGE_A.CFG";
static const char init_image_a[] = "INIT_A.CFG";

static FATFS fatfs;
static struct {
    FIL file;
    DIR dp;
    FILINFO fp;
    char buf[512];
} *fs;

struct native_dirent {
    uint32_t dir_sect;
    uint16_t dir_off;
    uint8_t attr;
    char name[0];
};

static struct {
    uint16_t slot_nr, max_slot_nr;
    uint8_t slot_map[1000/8];
    struct short_slot autoboot;
    struct short_slot hxcsdfe;
    struct short_slot imgcfg;
    struct slot slot, clipboard;
    uint32_t cfg_cdir, cur_cdir;
    struct native_dirent **sorted;
    struct {
        uint32_t cdir;
        uint16_t slot;
    } stack[20];
    uint8_t depth;
    bool_t usb_power_fault;
    uint8_t dirty_slot_nr:1;
    uint8_t dirty_slot_name:1;
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

#define BUTTON_SCAN_HZ 500
#define BUTTON_SCAN_MS (1000/BUTTON_SCAN_HZ)
static uint32_t display_ticks;
static uint8_t display_state;
enum { BACKLIGHT_OFF, BACKLIGHT_SWITCHING_ON, BACKLIGHT_ON };
enum { LED_NORMAL, LED_TRACK, LED_TRACK_QUIESCENT,
       LED_BUTTON_HELD, LED_BUTTON_RELEASED };

static void native_get_slot_map(bool_t sorted_only);

/* Hack inside the guts of FatFS. */
void flashfloppy_fill_fileinfo(FIL *fp);

#ifdef LOGFILE
/* Logfile must be written to config dir. */
#define logfile_flush(_file) do {               \
    fatfs.cdir = cfg.cfg_cdir;                  \
    logfile_flush(_file);                       \
    fatfs.cdir = cfg.cur_cdir;                  \
} while(0)
#endif

bool_t lba_within_fat_volume(uint32_t lba)
{
    /* Also disallows access to the boot/bpb sector of the mounted volume. */
    return (lba > fatfs.volbase) && (lba <= fatfs.volend);
}

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

uint16_t get_slot_nr(void)
{
    return cfg.slot_nr;
}

bool_t set_slot_nr(uint16_t slot_nr)
{
    if (!slot_valid(slot_nr))
        return FALSE;
    cfg.slot_nr = slot_nr;
    cfg.dirty_slot_nr = TRUE;
    return TRUE;
}

void set_slot_name(const char *name)
{
    snprintf(cfg.slot.name, sizeof(cfg.slot.name), "%s", name);
    cfg.dirty_slot_name = TRUE;
}

/* Turn the LCD backlight on, reset the switch-off handler and ticker. */
static void lcd_on(void)
{
    if (display_type != DT_LCD_OLED)
        return;
    display_ticks = 0;
    barrier();
    display_state = BACKLIGHT_ON;
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
    static struct track_info ti;
    char msg[lcd_columns+1];

    if ((lcd_scroll.ticks > 0) || (lcd_scroll.end == 0))
        return;

    floppy_get_track(&ti);
    if (ti.in_da_mode) {
        /* Display controlled by src/image/da.c */
        return;
    }

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
    const struct image_type *type;
    char msg[lcd_columns+1], typename[4] = "";
    unsigned int i;

    if (display_type != DT_LCD_OLED) {
        if (display_type == DT_LED_7SEG)
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

    if (slot_type("v9t9")) {
        snprintf(typename, sizeof(typename), "T99");
    } else if (!(cfg.slot.attributes & AM_DIR)) {
        for (type = &image_type[0]; type->handler != NULL; type++)
            if (slot_type(type->ext))
                break;
        if (type->handler != NULL) {
            snprintf(typename, sizeof(typename), "%s", type->ext);
            for (i = 0; i < sizeof(typename); i++)
                typename[i] = toupper(typename[i]);
        }
    }

    snprintf(msg, sizeof(msg), "%03u/%03u%*s%3s D:%u",
             cfg.slot_nr, cfg.max_slot_nr,
             (lcd_columns > 16) ? 3 : 1, "",
             typename, cfg.depth);

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
    static struct track_info lcd_ti;
    struct track_info ti;
    char msg[17];

    if (display_type != DT_LCD_OLED)
        return;

    floppy_get_track(&ti);

    if (lcd_columns <= 16)
        ti.cyl = min_t(uint8_t, ti.cyl, 99);
    ASSERT(ti.side <= 1);

    if (force || (ti.cyl != lcd_ti.cyl)
        || ((ti.side != lcd_ti.side) && ti.sel)
        || (ti.writing != lcd_ti.writing)) {
        snprintf(msg, sizeof(msg), "%c T:%02u.%u",
                 (cfg.slot.attributes & AM_RDO) ? '*' : ti.writing ? 'W' : ' ',
                 ti.cyl, ti.side);
        lcd_write(wp_column, 1, -1, msg);
        if (ff_cfg.display_on_activity != DISPON_no)
            lcd_on();
        lcd_ti = ti;
    } else if ((ff_cfg.display_on_activity == DISPON_sel) && ti.sel) {
        lcd_on();
    }
}

static void led_7seg_update_track(bool_t force)
{
    static struct track_info led_ti;
    static bool_t showing_track;
    static uint8_t active_countdown;

    bool_t changed;
    struct track_info ti;
    char msg[4];

    if (display_type != DT_LED_7SEG)
        return;

    floppy_get_track(&ti);
    changed = (ti.cyl != led_ti.cyl) || ((ti.side != led_ti.side) && ti.sel)
        || (ti.writing != led_ti.writing);

    if (force) {
        /* First call afer mounting new image: forcibly show track nr. */
        display_state = LED_TRACK;
        showing_track = FALSE;
        changed = TRUE;
    }

    if (ti.in_da_mode) {
        /* Display controlled by src/image/da.c */
        display_state = LED_NORMAL;
    }

    if (changed) {
        /* We will show new track nr unless overridden by a button press. */
        if (display_state == LED_TRACK_QUIESCENT)
            display_state = LED_TRACK;
        active_countdown = 50*4;
        led_ti = ti;
    } else if (active_countdown != 0) {
        /* Count down towards reverting to showing image nr. */
        active_countdown--;
    }

    if ((display_state != LED_TRACK) || (active_countdown == 0)) {
        if (showing_track)
            display_write_slot(FALSE);
        showing_track = FALSE;
        active_countdown = 0;
        if (display_state == LED_TRACK)
            display_state = LED_TRACK_QUIESCENT;
        return;
    }

    if (!showing_track || changed) {
        const static char status[] = { 'k', 'm', 'v', 'w' };
        snprintf(msg, sizeof(msg), "%2u%c", ti.cyl,
                 status[ti.side|(ti.writing<<1)]);
        led_7seg_write_string(msg);
        showing_track = TRUE;
    }
}

/* Handle switching the LCD backlight. */
static uint8_t lcd_handle_backlight(uint8_t b)
{
    if ((ff_cfg.display_off_secs == 0)
        || (ff_cfg.display_off_secs == 0xff))
        return b;

    switch (display_state) {
    case BACKLIGHT_OFF:
        if (!b)
            break;
        /* First button press turns on the backlight. Nothing more. */
        b = 0;
        display_state = BACKLIGHT_SWITCHING_ON;
        lcd_backlight(TRUE);
        break;
    case BACKLIGHT_SWITCHING_ON:
        /* We sit in this state until the button is released. */
        if (!b)
            display_state = BACKLIGHT_ON;
        b = 0;
        display_ticks = 0;
        break;
    case BACKLIGHT_ON:
        /* After a period with no button activity we turn the backlight off. */
        if (b)
            display_ticks = 0;
        if (display_ticks++ >= BUTTON_SCAN_HZ*ff_cfg.display_off_secs) {
            lcd_backlight(FALSE);
            display_state = BACKLIGHT_OFF;
        }
        break;
    }

    return b;
}

static uint8_t led_handle_display(uint8_t b)
{
    switch (display_state) {
    case LED_TRACK:
        if (!b)
            break;
        /* First button press switches to image number. Nothing more. */
        b = 0;
        display_state = LED_BUTTON_HELD;
        break;
    case LED_BUTTON_HELD:
        /* We sit in this state until the button is released. */
        if (!b)
            display_state = LED_BUTTON_RELEASED;
        b = 0;
        display_ticks = 0;
        break;
    case LED_BUTTON_RELEASED:
        /* After a period with no button activity we return to track number. */
        if (display_ticks++ >= BUTTON_SCAN_HZ*3)
            display_state = LED_TRACK;
        break;
    }

    return b;
}

static uint8_t v2_read_rotary(uint8_t rotary)
{
    /* Rotary encoder outputs a Gray code, counting clockwise: 00-01-11-10. */
    const uint32_t rotary_transitions[] = {
        [ROT_none]    = 0x00000000, /* No encoder */
        [ROT_full]    = 0x20000100, /* 4 transitions (full cycle) per detent */
        [ROT_half]    = 0x24000018, /* 2 transitions (half cycle) per detent */
        [ROT_quarter] = 0x24428118  /* 1 transition (quarter cyc) per detent */
    };
    return (rotary_transitions[ff_cfg.rotary & 3] >> (rotary << 1)) & 3;
}

static uint8_t read_rotary(uint8_t rotary)
{
    /* Rotary encoder outputs a Gray code, counting clockwise: 00-01-11-10. */
    const uint32_t rotary_transitions = 0x24428118;

    /* Number of back-to-back transitions we see per detent on various 
     * types of rotary encoder. */
    const uint8_t rotary_transitions_per_detent[] = {
        [ROT_full] = 4, [ROT_half] = 2, [ROT_quarter] = 1
    };

    /* p_t(x) returns the previous valid transition in same direction. 
     * eg. p_t(0b0111) == 0b0001 */
#define p_t(x) (((x)>>2)|((((x)^3)&3)<<2))

    /* Bitmask of which valid 4-bit transition codes we have seen in each 
     * direction (CW and CCW). */
    static uint16_t t_seen[2];

    uint16_t ts;
    uint8_t rb;

    /* Check if we have seen a valid CW or CCW state transition. */
    rb = (rotary_transitions >> (rotary << 1)) & 3;
    if (likely(!rb))
        return 0; /* Nope */

    /* Have we seen the /previous/ transition in this direction? If not, any 
     * previously-recorded transitions are not in a contiguous step-wise
     * sequence, and should be discarded as switch bounce. */
    ts = t_seen[rb-1];
    if (!(ts & (1<<p_t(rotary))))
        ts = 0; /* Clear any existing bounce transitions. */

    /* Record this transition and check if we have seen enough to get 
     * us from one detent to another. */
    ts |= (1<<rotary);
    if ((popcount(ts) < rotary_transitions_per_detent[ff_cfg.rotary & 3])
        || (((ff_cfg.rotary & 3) == ROT_full) && ((rotary & 3) != 3))) {
        /* Not enough transitions yet: Remember where we are for next time. */
        t_seen[rb-1] = ts;
        return 0;
    }

    /* This is a valid movement between detents. Clear transition state 
     * and return the movement to the caller. */
    t_seen[0] = t_seen[1] = 0;
    return rb;
#undef p_t
}

static struct timer button_timer;
static volatile uint8_t buttons, velocity;
static uint8_t rotary, rb;

void IRQ_rotary(void)
{
    if ((ff_cfg.rotary & ROT_typemask) != ROT_full)
        return;
    rotary = ((rotary << 2) | board_get_rotary()) & 15;
    rb = read_rotary(rotary) ?: rb;
}

static void set_rotary_exti(void)
{
    exti->imr &= ~board_rotary_exti_mask;
    board_rotary_exti_mask = 0;
    if ((ff_cfg.rotary & ROT_typemask) == ROT_full)
        board_setup_rotary_exti();
}

static void button_timer_fn(void *unused)
{
    const uint8_t rotary_reverse[4] = {
        [B_LEFT] = B_RIGHT, [B_RIGHT] = B_LEFT
    };

    static uint16_t cur_time, prev_time;
    static uint32_t _b[3]; /* 0 = left, 1 = right, 2 = select */
    uint8_t x, b = osd_buttons_rx;
    bool_t twobutton_rotary =
        (ff_cfg.twobutton_action & TWOBUTTON_mask) == TWOBUTTON_rotary;
    int i, twobutton_reverse = !!(ff_cfg.twobutton_action & TWOBUTTON_reverse);

    cur_time++;
    if ((uint16_t)(cur_time - prev_time) > 0x7fff)
        prev_time = cur_time - 0x7fff;
    velocity = 0;

    /* Check PA5 (USBFLT, active low). */
    if (gotek_enhanced() && !gpio_read_pin(gpioa, 5)) {
        /* Latch the error and disable USBENA. */
        cfg.usb_power_fault = TRUE;
        gpio_write_pin(gpioa, 4, HIGH);
    }

    /* We debounce the switches by waiting for them to be pressed continuously 
     * for 32 consecutive sample periods (32 * 2ms == 64ms) */
    x = ~board_get_buttons();
    for (i = 0; i < 3; i++) {
        _b[i] <<= 1;
        _b[i] |= x & 1;
        x >>= 1;
    }

    if (_b[twobutton_reverse] == 0)
        b |= twobutton_rotary ? B_LEFT|B_RIGHT : B_LEFT;

    if (_b[!twobutton_reverse] == 0)
        b |= twobutton_rotary ? B_SELECT : B_RIGHT;

    if (_b[2] == 0)
        b |= B_SELECT;

    rotary = ((rotary << 2) | board_get_rotary()) & 15;
    switch (ff_cfg.rotary & ROT_typemask) {

    case ROT_trackball: {
        static uint16_t count, thresh, dir;
        rb = rotary_reverse[(rotary ^ (rotary >> 2)) & 3];
        if (rb == 0) {
            /* Idle: Increase threshold, decay the counter. */
            thresh = min_t(int, thresh + BUTTON_SCAN_MS, 360);
            count = max_t(int, count - BUTTON_SCAN_MS, 0);
        } else if (rb != dir) {
            /* Change of direction: Put the brakes on. */
            dir = rb;
            count = rb = 0;
            thresh = 360;
        } else {
            /* Step in same direction: Increase count, decay the threshold. */
            count += 160;
            thresh = max_t(int, 0, thresh - 40);
            if (count >= thresh) {
                /* Count exceeds threshold: register a press. */
                count = 0;
            } else {
                /* Don't register a press yet. */
                rb = 0;
            }
        }
        break;
    }

    case ROT_buttons:
        rb = rotary_reverse[rotary & 3];
        break;

    case ROT_none:
        break;

    default: /* rotary encoder */ {
        rb = (ff_cfg.rotary & ROT_v2) ? v2_read_rotary(rotary)
            : (read_rotary(rotary) ?: rb);
        if (rb) {
            uint16_t delta = cur_time - prev_time;
            velocity = (BUTTON_SCAN_HZ/10)/(delta?:1);
            velocity = range_t(int, velocity, 0, 20);
            prev_time = cur_time;
        }
        break;
    }

    }
    if (ff_cfg.rotary & ROT_reverse)
        rb = rotary_reverse[rb];
    b |= rb;
    rb = 0;

    switch (display_type) {
    case DT_LCD_OLED:
        b = lcd_handle_backlight(b);
        break;
    case DT_LED_7SEG:
        b = led_handle_display(b);
        break;
    }

    /* Latch final button state and reset the timer. */
    buttons = b;
    timer_set(&button_timer, button_timer.deadline + time_ms(BUTTON_SCAN_MS));
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

static void fix_hxc_short_slot(struct short_slot *short_slot)
{
    char *dot;
    /* Get rid of trailing file extension. */
    short_slot->name[51] = '\0';
    if (((dot = strrchr(short_slot->name, '.')) != NULL)
        && !strcmp(dot+1, short_slot->type))
        *dot = '\0';
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

bool_t get_img_cfg(struct slot *slot)
{
    if (!cfg.imgcfg.size)
        return FALSE;
    slot_from_short_slot(slot, &cfg.imgcfg);
    return TRUE;
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
        if ((fs->fp.fattrib & AM_DIR) && (display_type == DT_LCD_OLED)
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

static inline int __tolower(int c)
{
    if ((c >= 'A') && (c <= 'Z'))
        c += 'a' - 'A';
    return c;
}

int strcmp_lower(const char *s1, const char *s2)
{
    for (;;) {
        int diff = __tolower(*s1) - __tolower(*s2);
        if (diff || !*s1)
            return diff;
        s1++; s2++;
    }
    return 0;
}

static int native_dir_cmp(const void *a, const void *b)
{
    const struct native_dirent *da = a;
    const struct native_dirent *db = b;
    if ((da->attr ^ db->attr) & AM_DIR) {
        switch (ff_cfg.sort_priority) {
        case SORTPRI_folders:
            return (da->attr & AM_DIR) ? -1 : 1;
        case SORTPRI_files:
            return (da->attr & AM_DIR) ? 1 : -1;
        }
    }
    return strcmp_lower(da->name, db->name);
}

/* Returns -1 if not read & sorted. */
static int native_read_and_sort_dir(void)
{
    struct native_dirent **p_ent;
    struct native_dirent *ent = arena_alloc(0);
    char *start = arena_alloc(0);
    char *end = start + arena_avail();
    int nr;

    if (ff_cfg.folder_sort == SORT_never)
        return -1;

    volume_cache_destroy();

    F_opendir(&fs->dp, "");
    p_ent = (struct native_dirent **)end;
    while (((char *)p_ent - (char *)ent)
           > (FF_MAX_LFN + 1 + sizeof(*ent) + sizeof(ent))) {
        if (!native_dir_next())
            goto complete;
        *--p_ent = ent;
        ASSERT((unsigned int)(fs->fp.dir_ptr - fatfs.win) < 512u);
        ent->dir_sect = fs->fp.dir_sect;
        ent->dir_off = fs->fp.dir_ptr - fatfs.win;
        ent->attr = fs->fp.fattrib;
        strcpy(ent->name, fs->fp.fname);
        ent = (struct native_dirent *)(
            ((uint32_t)ent + sizeof(*ent) + strlen(ent->name) + 1 + 3) & ~3);
    }

    if (ff_cfg.folder_sort == SORT_always)
        goto complete;

    volume_cache_init(start, end);
    cfg.sorted = NULL;
    return -1;

complete:
    nr = (struct native_dirent **)end - p_ent;

    qsort_p(p_ent, nr, native_dir_cmp);

    F_closedir(&fs->dp);

    volume_cache_init(ent, p_ent);
    cfg.sorted = p_ent;
    return nr;
}

static void update_slot_by_name(void)
{
    const char *name = cfg.slot.name;
    int len = strnlen(name, 256);
    int nr = ~0, max;

    if (cfg.sorted) {

        max = cfg.max_slot_nr;
        if (cfg.depth)
            max--;
        for (nr = 0; nr <= max; nr++)
            if (!strncmp(cfg.sorted[nr]->name, name, len))
                break;
        if (cfg.depth)
            nr++;

    }

    else if (!cfg.hxc_mode) {

        nr = cfg.depth ? 1 : 0;
        F_opendir(&fs->dp, "");
        while (native_dir_next() && strncmp(fs->fp.fname, name, len))
            nr++;
        F_closedir(&fs->dp);

    } else if (ff_cfg.nav_mode != NAVMODE_indexed) {

        struct _hxc{
            struct hxcsdfe_cfg cfg;
            struct v1_slot v1_slot;
            struct v2_slot v2_slot;
        } *hxc = (struct _hxc *)fs->buf;
        struct slot *slot = (struct slot *)hxc;
        
        slot_from_short_slot(slot, &cfg.hxcsdfe);
        fatfs_from_slot(&fs->file, slot, FA_READ);
        F_read(&fs->file, &hxc->cfg, sizeof(hxc->cfg), NULL);
        if (hxc->cfg.index_mode)
            goto out;
        for (nr = 1; nr <= cfg.max_slot_nr; nr++) {
            if (!slot_valid(nr))
                continue;
            switch (hxc->cfg.signature[9]-'0') {
            case 1:
                F_lseek(&fs->file, 1024 + nr*128);
                F_read(&fs->file, &hxc->v1_slot, sizeof(hxc->v1_slot), NULL);
                memcpy(&hxc->v2_slot.type, &hxc->v1_slot.name[8], 3);
                memcpy(&hxc->v2_slot.attributes, &hxc->v1_slot.attributes,
                       1+4+4+17);
                hxc->v2_slot.name[17] = '\0';
                break;
            case 2:
                F_lseek(&fs->file, hxc->cfg.slots_position*512
                    + nr*64*hxc->cfg.number_of_drive_per_slot);
                F_read(&fs->file, &hxc->v2_slot, sizeof(hxc->v2_slot), NULL);
                break;
            }
            fix_hxc_short_slot(&hxc->v2_slot);
            if (!strncmp(hxc->v2_slot.name, name,
                         min_t(int, len, sizeof(hxc->v2_slot.name))))
                break;
        }

    }

    set_slot_nr(nr);
out:
    cfg.dirty_slot_nr = TRUE;
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

static uint16_t parse_display_order(const char *p)
{
    int sh = 0;
    uint16_t order = 0;

    if (!strcmp(p, "default"))
        return DORD_default;

    while (p != NULL) {
        order |= ((p[0]-'0')&7) << sh;
        if (p[1] == 'd')
            order |= DORD_double << sh;
        sh += DORD_shift;
        if ((p = strchr(p, ',')) == NULL)
            break;
        p++;
    }

    if (sh < 16)
        order |= 0x7777 << sh;

    return order;
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
                : !strcmp(opts.arg, "jppc") ? FINTF_JPPC
                : !strcmp(opts.arg, "jppc-hdout") ? FINTF_JPPC_HDOUT
                : !strcmp(opts.arg, "akai-s950") ? FINTF_JPPC_HDOUT
                : !strcmp(opts.arg, "amiga") ? FINTF_AMIGA
                : FINTF_JC;
            break;

        case FFCFG_host:
            ff_cfg.host =
                !strcmp(opts.arg, "acorn") ? HOST_acorn
                : !strcmp(opts.arg, "akai") ? HOST_akai
                : !strcmp(opts.arg, "casio") ? HOST_casio
                : !strcmp(opts.arg, "dec") ? HOST_dec
                : !strcmp(opts.arg, "ensoniq") ? HOST_ensoniq
                : !strcmp(opts.arg, "fluke") ? HOST_fluke
                : !strcmp(opts.arg, "gem") ? HOST_gem
                : !strcmp(opts.arg, "ibm-3174") ? HOST_ibm_3174
                : !strcmp(opts.arg, "memotech") ? HOST_memotech
                : !strcmp(opts.arg, "msx") ? HOST_msx
                : !strcmp(opts.arg, "nascom") ? HOST_nascom
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

        case FFCFG_max_cyl:
            ff_cfg.max_cyl = strtol(opts.arg, NULL, 10);
            break;

        case FFCFG_side_select_glitch_filter:
            ff_cfg.side_select_glitch_filter = strtol(opts.arg, NULL, 10);
            break;

        case FFCFG_track_change:
            ff_cfg.track_change =
                !strcmp(opts.arg, "realtime") ? TRKCHG_realtime
                : TRKCHG_instant;
            break;

        case FFCFG_write_drain:
            ff_cfg.write_drain =
                !strcmp(opts.arg, "realtime") ? WDRAIN_realtime
                : !strcmp(opts.arg, "eot") ? WDRAIN_eot
                : WDRAIN_instant;
            break;

        case FFCFG_index_suppression:
            ff_cfg.index_suppression = !strcmp(opts.arg, "yes");
            break;

        case FFCFG_head_settle_ms:
            ff_cfg.head_settle_ms = strtol(opts.arg, NULL, 10);
            break;

        case FFCFG_motor_delay:
            ff_cfg.motor_delay =
                !strcmp(opts.arg, "ignore") ? MOTOR_ignore
                : (strtol(opts.arg, NULL, 10) + 9) / 10;
            break;

        case FFCFG_chgrst:
            ff_cfg.chgrst =
                !strncmp(opts.arg, "delay-", 6)
                  ? CHGRST_delay(strtol(opts.arg+6, NULL, 10))
                : !strcmp(opts.arg, "pa14") ? CHGRST_pa14
                : CHGRST_step;
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

        case FFCFG_folder_sort:
            ff_cfg.folder_sort =
                !strcmp(opts.arg, "never") ? SORT_never
                : !strcmp(opts.arg, "small") ? SORT_small
                : SORT_always;
            break;

        case FFCFG_sort_priority:
            ff_cfg.sort_priority =
                !strcmp(opts.arg, "none") ? SORTPRI_none
                : !strcmp(opts.arg, "files") ? SORTPRI_files
                : SORTPRI_folders;
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

        case FFCFG_twobutton_action: {
            char *p, *q;
            ff_cfg.twobutton_action = TWOBUTTON_zero;
            for (p = opts.arg; *p != '\0'; p = q) {
                for (q = p; *q && *q != ','; q++)
                    continue;
                if (*q == ',')
                    *q++ = '\0';
                if (!strcmp(p, "reverse")) {
                    ff_cfg.twobutton_action |= TWOBUTTON_reverse;
                } else {
                    ff_cfg.twobutton_action &= TWOBUTTON_reverse;
                    ff_cfg.twobutton_action |=
                        !strcmp(p, "rotary") ? TWOBUTTON_rotary
                        : !strcmp(p, "rotary-fast") ? TWOBUTTON_rotary_fast
                        : !strcmp(p, "eject") ? TWOBUTTON_eject
                        : !strcmp(p, "htu") ? TWOBUTTON_htu
                        : TWOBUTTON_zero;
                }
            }
            break;
        }

        case FFCFG_rotary: {
            char *p, *q;
            ff_cfg.rotary = ROT_full;
            for (p = opts.arg; *p != '\0'; p = q) {
                for (q = p; *q && *q != ','; q++)
                    continue;
                if (*q == ',')
                    *q++ = '\0';
                if (!strcmp(p, "reverse")) {
                    ff_cfg.rotary |= ROT_reverse;
                } else if (!strcmp(p, "v2")) {
                    ff_cfg.rotary |= ROT_v2;
                } else {
                    ff_cfg.rotary &= ~ROT_typemask;
                    ff_cfg.rotary |=
                        !strcmp(p, "quarter") ? ROT_quarter
                        : !strcmp(p, "half") ? ROT_half
                        : !strcmp(p, "trackball") ? ROT_trackball
                        : !strcmp(p, "buttons") ? ROT_buttons
                        : !strcmp(p, "none") ? ROT_none
                        : ROT_full;
                }
            }
            break;
        }

        case FFCFG_indexed_prefix:
            memset(ff_cfg.indexed_prefix, 0,
                   sizeof(ff_cfg.indexed_prefix));
            snprintf(ff_cfg.indexed_prefix,
                     sizeof(ff_cfg.indexed_prefix),
                     "%s", opts.arg);
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
                } else if ((r = strchr(p, 'x')) != NULL) {
                    unsigned int w, h;
                    *r++ = '\0';
                    w = strtol(p, NULL, 10);
                    h = strtol(r, NULL, 10);
                    if (ff_cfg.display_type & DISPLAY_oled) {
                        if (h == 64)
                            ff_cfg.display_type |= DISPLAY_oled_64;
                    } else if (ff_cfg.display_type & DISPLAY_lcd) {
                        ff_cfg.display_type |= DISPLAY_lcd_columns(w);
                        ff_cfg.display_type |= DISPLAY_lcd_rows(h);
                    }
                } else if (ff_cfg.display_type & DISPLAY_oled) {
                    if (!strcmp(p, "rotate")) {
                        ff_cfg.display_type |= DISPLAY_rotate;
                    } else if (!strncmp(p, "narrow", 6)) {
                        ff_cfg.display_type |=
                            (p[6] == 'e') ? DISPLAY_narrower : DISPLAY_narrow;
                    } else if (!strcmp(p, "inverse")) {
                        ff_cfg.display_type |= DISPLAY_inverse;
                    } else if (!strcmp(p, "ztech")) {
                        ff_cfg.display_type |= DISPLAY_ztech;
                    } else if (!strcmp(p, "slow")) {
                        ff_cfg.display_type |= DISPLAY_slow;
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

        case FFCFG_oled_contrast:
            ff_cfg.oled_contrast = strtol(opts.arg, NULL, 10);
            break;

        case FFCFG_display_order:
            ff_cfg.display_order = parse_display_order(opts.arg);
            break;

        case FFCFG_osd_display_order:
            ff_cfg.osd_display_order = parse_display_order(opts.arg);
            break;

        case FFCFG_display_off_secs:
            ff_cfg.display_off_secs = strtol(opts.arg, NULL, 10);
            cfg.ffcfg_has_display_off_secs = TRUE;
            break;

        case FFCFG_display_on_activity:
            ff_cfg.display_on_activity =
                !strcmp(opts.arg, "no") ? DISPON_no
                : !strcmp(opts.arg, "sel") ? DISPON_sel
                : DISPON_yes;
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

    flash_ff_cfg_update(fs->buf);
}

static void process_ff_cfg_opts(const struct ff_cfg *old)
{
    /* rotary, chgrst, motor-delay: Inform the rotary-encoder subsystem. 
     * It is harmless to notify unconditionally. */
    set_rotary_exti();

    /* interface, pin02, pin34: Inform the floppy subsystem. */
    if ((ff_cfg.interface != old->interface)
        || (ff_cfg.pin02 != old->pin02)
        || (ff_cfg.pin34 != old->pin34))
        floppy_set_fintf_mode();

    /* max-cyl: Inform the floppy subsystem. */
    if (ff_cfg.max_cyl != old->max_cyl)
        floppy_set_max_cyl();

    /* ejected-on-startup: Set the ejected state appropriately. */
    if (ff_cfg.ejected_on_startup)
        cfg.ejected = TRUE;

    /* oled-font, display-type: Reinitialise the display subsystem. */
    if ((ff_cfg.oled_font != old->oled_font)
        || (ff_cfg.oled_contrast != old->oled_contrast)
        || (ff_cfg.display_type != old->display_type))
        system_reset(); /* hit it with a hammer */
}

static void cfg_init(void)
{
    struct ff_cfg old_ff_cfg = ff_cfg;
    struct hxcsdfe_cfg *hxc_cfg;
    unsigned int sofar;
    char *p;
    FRESULT fr;

    memset(&cfg.clipboard, 0, sizeof(cfg.clipboard));
    cfg.dirty_slot_nr = FALSE;
    cfg.dirty_slot_name = FALSE;
    cfg.hxc_mode = FALSE;
    cfg.ima_ej_flag = FALSE;
    cfg.slot_nr = cfg.depth = 0;
    cfg.cur_cdir = fatfs.cdir;

    fr = f_chdir("FF");
    cfg.cfg_cdir = fatfs.cdir;

    memset(&cfg.imgcfg, 0, sizeof(cfg.imgcfg));
    fr = F_try_open(&fs->file, "IMG.CFG", FA_READ);
    if (!fr) {
        fatfs_to_short_slot(&cfg.imgcfg, &fs->file, "IMG.CFG");
        F_close(&fs->file);
    }

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
    hxc_cfg = (struct hxcsdfe_cfg *)fs->buf;
    F_read(&fs->file, hxc_cfg, sizeof(*hxc_cfg), NULL);
    if (hxc_cfg->startup_mode & HXCSTARTUP_slot0) {
        /* Startup mode: slot 0. */
        hxc_cfg->slot_index = hxc_cfg->cur_slot_number = 0;
        F_lseek(&fs->file, 0);
        F_write(&fs->file, hxc_cfg, sizeof(*hxc_cfg), NULL);
    }
    if (hxc_cfg->startup_mode & HXCSTARTUP_ejected) {
        /* Startup mode: eject. */
        cfg.ejected = TRUE;
    }
    hxc_cfg = NULL;
    F_close(&fs->file);

    /* Slot 0 is a dummy image unless AUTOBOOT.HFE exists. */
    memset(&cfg.autoboot, 0, sizeof(cfg.autoboot));
    snprintf(cfg.autoboot.name, sizeof(cfg.autoboot.name), "(Empty)");
    cfg.autoboot.firstCluster = ~0u; /* flag to dummy_open() */

    fr = F_try_open(&fs->file, "AUTOBOOT.HFE", FA_READ);
    if (!fr) {
        fatfs_to_short_slot(&cfg.autoboot, &fs->file, "AUTOBOOT.HFE");
        cfg.autoboot.attributes |= AM_RDO; /* default read-only */
        F_close(&fs->file);
    }

    cfg.hxc_mode = TRUE;
    goto out;

native_mode:
    /* Native mode (direct navigation). */
    fatfs.cdir = cfg.cfg_cdir;

    sofar = 0;
    if (ff_cfg.image_on_startup == IMGS_static) {
        fr = F_try_open(&fs->file, init_image_a, FA_READ);
        if (!fr) {
            sofar = min_t(unsigned int, f_size(&fs->file), sizeof(fs->buf));
            F_read(&fs->file, fs->buf, sofar, NULL);
            F_close(&fs->file);
        }
    }

    F_open(&fs->file, image_a, FA_READ | FA_WRITE | FA_OPEN_ALWAYS);

    if (ff_cfg.image_on_startup != IMGS_last) {
        F_lseek(&fs->file, 0);
        F_truncate(&fs->file);
        F_write(&fs->file, fs->buf, sofar, NULL);
        F_lseek(&fs->file, 0);
    }

    /* Process IMAGE_A.CFG file. */
    sofar = 0; /* bytes consumed so far */
    fatfs.cdir = cfg.cur_cdir;
    lcd_write(0, 3, -1, "/");
    for (;;) {
        int nr;
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
        lcd_write(0, 3, -1, fs->buf);
        if (cfg.depth == ARRAY_SIZE(cfg.stack))
            F_die(FR_PATH_TOO_DEEP);
        /* Find slot nr, and stack it */
        if ((nr = native_read_and_sort_dir()) != -1) {
            while (--nr >= 0)
                if (!strcmp(cfg.sorted[nr]->name, fs->buf))
                    break;
            ok = (nr >= 0);
            cfg.sorted = NULL;
        } else {
            F_opendir(&fs->dp, "");
            while ((ok = native_dir_next()) && strcmp(fs->fp.fname, fs->buf))
                nr++;
            F_closedir(&fs->dp);
        }
        if (!ok)
            goto clear_image_a;
        nr += cfg.depth ? 1 : 0;
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
        if (display_type != DT_LCD_OLED)
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
        int i, nr;
        printk("%u:F: '%s' %s\n", cfg.depth, fs->buf,
               cfg.ima_ej_flag ? "(EJ)" : "");
        native_get_slot_map(TRUE);
        cfg.slot_nr = cfg.depth ? 1 : 0;
        if (cfg.sorted) {
            nr = cfg.max_slot_nr + 1 - cfg.slot_nr;
            for (i = 0; i < nr; i++)
                if (!strcmp(cfg.sorted[i]->name, fs->buf))
                    break;
            ok = (i < nr);
            cfg.slot_nr += i;
        } else {
            F_opendir(&fs->dp, "");
            while ((ok = native_dir_next()) && strcmp(fs->fp.fname, fs->buf))
                cfg.slot_nr++;
            F_closedir(&fs->dp);
        }
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
    printk("IMAGE_A.CFG is bad: clearing it\n");
    F_lseek(&fs->file, 0);
    lcd_write(0, 3, -1, "/");
    F_truncate(&fs->file);
    F_close(&fs->file);
    cfg.slot_nr = cfg.depth = 0;
    cfg.ima_ej_flag = FALSE;
    cfg.sorted = NULL;
    goto out;
}

static void native_get_slot_map(bool_t sorted_only)
{
    int i;

    /* Populate slot_map[]. */
    memset(&cfg.slot_map, 0xff, sizeof(cfg.slot_map));
    cfg.max_slot_nr = cfg.depth ? 1 : 0;

    if ((i = native_read_and_sort_dir()) != -1) {
        cfg.max_slot_nr += i;
    } else {
        if (sorted_only)
            return;
        F_opendir(&fs->dp, "");
        while (native_dir_next())
            cfg.max_slot_nr++;
        F_closedir(&fs->dp);
    }

    /* Adjust max_slot_nr. Must be at least one 'slot'. */
    if (!cfg.max_slot_nr)
        F_die(FR_NO_DIRENTS);
    cfg.max_slot_nr--;

    /* Select last disk_index if not greater than available slots. */
    cfg.slot_nr = (cfg.slot_nr <= cfg.max_slot_nr) ? cfg.slot_nr : 0;
}

#define CFG_KEEP_SLOT_NR  0 /* Do not re-read slot number from config */
#define CFG_READ_SLOT_NR  1 /* Read slot number afresh from config */
#define CFG_WRITE_SLOT_NR 2 /* Write new slot number to config */

static void native_update(uint8_t slot_mode)
{
    int i;

    if ((slot_mode == CFG_READ_SLOT_NR) && !cfg.sorted)
        native_get_slot_map(FALSE);

    if (slot_mode == CFG_WRITE_SLOT_NR) {
        char *p, *q;
        fatfs.cdir = cfg.cfg_cdir;
        F_open(&fs->file, image_a, FA_READ|FA_WRITE);
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
                    /* We need to find the next preceding '/' so we can update 
                     * multi-row LCD/OLED display with subfolder info. */
                    long sz, pos = f_tell(&fs->file) - (p-q);
                    /* pos-1 = offset of end of subfolder name. */
                    sz = min_t(long, pos-1, sizeof(fs->buf));
                    F_lseek(&fs->file, pos-1-sz);
                    F_read(&fs->file, fs->buf, sz, NULL);
                    fs->buf[sz] = '\0';
                    /* q = pointer to '/' preceding the subfolder name. */
                    q = strrchr(fs->buf, '/');
                    lcd_write(0, 3, -1, q ? q+1 : fs->buf);
                    F_lseek(&fs->file, pos);
                } else {
                    F_lseek(&fs->file, 0);
                    lcd_write(0, 3, -1, "/");
                }
            } else {
                /* Add name plus '/' */
                F_write(&fs->file, fs->fp.fname,
                        strnlen(fs->fp.fname, sizeof(fs->fp.fname)), NULL);
                F_write(&fs->file, "/", 1, NULL);
                lcd_write(0, 3, -1, fs->fp.fname);
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
    if ((i = cfg.depth ? 1 : 0) > cfg.slot_nr) {
        /* Must be the ".." folder. */
        snprintf(fs->fp.fname, sizeof(fs->fp.fname), "..");
        fs->fp.fattrib = AM_DIR;
        goto is_dir;
    }

    if (cfg.sorted) {

        struct native_dirent *ent = cfg.sorted[cfg.slot_nr-i];
        snprintf(fs->fp.fname, sizeof(fs->fp.fname), ent->name);
        fs->file.obj.fs = &fatfs;
        fs->file.dir_sect = ent->dir_sect;
        fs->file.dir_ptr = fatfs.win + ent->dir_off;
        flashfloppy_fill_fileinfo(&fs->file);
        fs->fp.fattrib = fs->file.obj.attr;
        if (fs->file.obj.attr & AM_DIR)
            goto is_dir;
        fatfs_to_slot(&cfg.slot, &fs->file, fs->fp.fname);

    } else {

        F_opendir(&fs->dp, "");
        while (native_dir_next()) {
            if (i >= cfg.slot_nr)
                break;
            i++;
        }
        F_closedir(&fs->dp);
        if (fs->fp.fattrib & AM_DIR) {
        is_dir:
            /* Leave the full pathname cached in fs->fp. */
            cfg.slot.attributes = fs->fp.fattrib;
            snprintf(cfg.slot.name, sizeof(cfg.slot.name),
                     "[%s]", fs->fp.fname);
        } else {
            F_open(&fs->file, fs->fp.fname, FA_READ);
            fs->file.obj.attr = fs->fp.fattrib;
            fatfs_to_slot(&cfg.slot, &fs->file, fs->fp.fname);
            F_close(&fs->file);
        }

    }
}

static void ima_mark_ejected(bool_t ej)
{
    if (cfg.hxc_mode || (cfg.ima_ej_flag == ej))
        return;

    fatfs.cdir = cfg.cfg_cdir;
    F_open(&fs->file, image_a, FA_READ|FA_WRITE);
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
    struct _hxc {
        struct hxcsdfe_cfg cfg;
        struct v1_slot v1_slot;
        struct v2_slot v2_slot;
    } *hxc = (struct _hxc *)fs->buf;
    BYTE mode = FA_READ;
    int i;

    if (slot_mode == CFG_WRITE_SLOT_NR)
        mode |= FA_WRITE;

    if (ff_cfg.nav_mode == NAVMODE_indexed) {
        FRESULT fr;
        char slot[10];
        hxc->cfg.index_mode = TRUE;
        fatfs.cdir = cfg.cfg_cdir;
        switch (slot_mode) {
        case CFG_READ_SLOT_NR:
            cfg.slot_nr = 0;
            if (ff_cfg.image_on_startup == IMGS_init)
                break;
            if ((fr = F_try_open(&fs->file, image_a, FA_READ)) != FR_OK)
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
            F_open(&fs->file, image_a, FA_WRITE | FA_OPEN_ALWAYS);
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
    F_read(&fs->file, &hxc->cfg, sizeof(hxc->cfg), NULL);
    if (strncmp("HXCFECFGV", hxc->cfg.signature, 9))
        goto bad_signature;

    if (slot_mode == CFG_READ_SLOT_NR) {
        /* buzzer_step_duration seems to range 0xFF-0xD8. */
        if (!cfg.ffcfg_has_step_volume)
            ff_cfg.step_volume = hxc->cfg.step_sound
                ? (0x100 - hxc->cfg.buzzer_step_duration) / 2 : 0;
        if (!cfg.ffcfg_has_display_off_secs)
            ff_cfg.display_off_secs = hxc->cfg.back_light_tmr;
        /* Interpret HxC scroll speed as updates per minute. */
        if (!cfg.ffcfg_has_display_scroll_rate && hxc->cfg.lcd_scroll_speed)
            ff_cfg.display_scroll_rate = 60000u / hxc->cfg.lcd_scroll_speed;
    }

    switch (hxc->cfg.signature[9]-'0') {

    case 1: {
        if (slot_mode != CFG_READ_SLOT_NR) {
            /* Keep the already-configured slot number. */
            hxc->cfg.slot_index = cfg.slot_nr;
            if (slot_mode == CFG_WRITE_SLOT_NR) {
                /* Update the config file with new slot number. */
                F_lseek(&fs->file, 0);
                F_write(&fs->file, &hxc->cfg, sizeof(hxc->cfg), NULL);
            }
        }
        cfg.slot_nr = hxc->cfg.slot_index;
        if (hxc->cfg.index_mode)
            break;
        /* Slot mode: initialise slot map and current slot. */
        if (slot_mode == CFG_READ_SLOT_NR) {
            cfg.max_slot_nr = hxc->cfg.number_of_slot - 1;
            memset(&cfg.slot_map, 0xff, sizeof(cfg.slot_map));
        }
        /* Slot mode: read current slot file info. */
        if (cfg.slot_nr == 0) {
            slot_from_short_slot(&cfg.slot, &cfg.autoboot);
        } else {
            F_lseek(&fs->file, 1024 + cfg.slot_nr*128);
            F_read(&fs->file, &hxc->v1_slot, sizeof(hxc->v1_slot), NULL);
            memcpy(&hxc->v2_slot.type, &hxc->v1_slot.name[8], 3);
            memcpy(&hxc->v2_slot.attributes, &hxc->v1_slot.attributes,
                   1+4+4+17);
            hxc->v2_slot.name[17] = '\0';
            fix_hxc_short_slot(&hxc->v2_slot);
            slot_from_short_slot(&cfg.slot, &hxc->v2_slot);
        }
        break;
    }

    case 2:
        if (slot_mode != CFG_READ_SLOT_NR) {
            hxc->cfg.cur_slot_number = cfg.slot_nr;
            if (slot_mode == CFG_WRITE_SLOT_NR) {
                F_lseek(&fs->file, 0);
                F_write(&fs->file, &hxc->cfg, sizeof(hxc->cfg), NULL);
            }
        }
        cfg.slot_nr = hxc->cfg.cur_slot_number;
        if (hxc->cfg.index_mode)
            break;
        /* Slot mode: initialise slot map and current slot. */
        if (slot_mode == CFG_READ_SLOT_NR) {
            cfg.max_slot_nr = hxc->cfg.max_slot_number - 1;
            F_lseek(&fs->file, hxc->cfg.slots_map_position*512);
            F_read(&fs->file, &cfg.slot_map, sizeof(cfg.slot_map), NULL);
            cfg.slot_map[0] |= 0x80; /* slot 0 always available */
            /* Find true max_slot_nr: */
            while (!slot_valid(cfg.max_slot_nr))
                cfg.max_slot_nr--;
        }
        /* Slot mode: read current slot file info. */
        if (cfg.slot_nr == 0) {
            slot_from_short_slot(&cfg.slot, &cfg.autoboot);
        } else if (slot_valid(cfg.slot_nr)) {
            F_lseek(&fs->file, hxc->cfg.slots_position*512
                    + cfg.slot_nr*64*hxc->cfg.number_of_drive_per_slot);
            F_read(&fs->file, &hxc->v2_slot, sizeof(hxc->v2_slot), NULL);
            fix_hxc_short_slot(&hxc->v2_slot);
            slot_from_short_slot(&cfg.slot, &hxc->v2_slot);
        } else {
            memset(&cfg.slot, 0, sizeof(cfg.slot));
        }
        break;

    default:
    bad_signature:
        hxc->cfg.signature[15] = '\0';
        printk("Bad signature '%s'\n", hxc->cfg.signature);
        F_die(FR_BAD_HXCSDFE);

    }

    F_close(&fs->file);

indexed_mode:
    if (hxc->cfg.index_mode) {

        char name[16];

        /* Index mode: populate slot_map[]. */
        if (slot_mode == CFG_READ_SLOT_NR) {
            memset(&cfg.slot_map, 0, sizeof(cfg.slot_map));
            cfg.max_slot_nr = 0;
            snprintf(name, sizeof(name), "%s*.*", ff_cfg.indexed_prefix);
            for (F_findfirst(&fs->dp, &fs->fp, "", name);
                 fs->fp.fname[0] != '\0';
                 F_findnext(&fs->dp, &fs->fp)) {
                const char *p = fs->fp.fname
                    + strnlen(ff_cfg.indexed_prefix,
                            sizeof(ff_cfg.indexed_prefix));
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
                /* Expect a 4-digit number range 0-999. */
                if ((i != 4) || (idx > 999))
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
            if (!slot_valid(cfg.max_slot_nr))
                F_die(FR_NO_DIRENTS);
        }

        /* Index mode: populate current slot. */
        snprintf(name, sizeof(name), "%s%04u*.*",
                 ff_cfg.indexed_prefix, cfg.slot_nr);
        printk("[%s]\n", name);
        F_findfirst(&fs->dp, &fs->fp, "", name);
        F_closedir(&fs->dp);
        if (fs->fp.fname[0]) {
            /* Found a valid image. */
            F_open(&fs->file, fs->fp.fname, FA_READ);
            fs->file.obj.attr = fs->fp.fattrib;
            fatfs_to_slot(&cfg.slot, &fs->file, fs->fp.fname);
            F_close(&fs->file);
        } else {
            memset(&cfg.slot, 0, sizeof(cfg.slot));
        }
    }

    for (i = 0; i < sizeof(cfg.slot.type); i++)
        cfg.slot.type[i] = tolower(cfg.slot.type[i]);
}

/* Always updates cfg.slot info for current slot_nr. Additionally:
 * Native (Direct Navigation):
 *  READ_SLOT:  Update slot_map/max_slot_nr for current directory.
 *  WRITE_SLOT: Update IMAGE_A.CFG based on cached fs->fp.fname.
 * NAVMODE_indexed (FF-native indexed mode):
 *  READ_SLOT:  Update slot_nr from IMAGE_A.CFG. Update slot_map/max_slot_nr.
 *  WRITE_SLOT: Update IMAGE_A.CFG from slot_nr.
 * HxC Indexed Mode:
 *  READ_SLOT:  Update slot_nr from HXCSDFE.CFG. Update slot_map/max_slot_nr.
 *  WRITE_SLOT: Update HXCSDFE.CFG from slot_nr.
 * HxC Selector Mode:
 *  READ_SLOT:  Update slot_nr/slot_map/max_slot_nr from HXCSDFE.CFG.
 *  WRITE_SLOT: Update HXCSDFE.CFG from slot_nr. */
static void cfg_update(uint8_t slot_mode)
{
    if (cfg.hxc_mode)
        hxc_cfg_update(slot_mode);
    else
        native_update(slot_mode);
    if (!(cfg.slot.attributes & AM_DIR)
        && (ff_cfg.write_protect || volume_readonly()))
        cfg.slot.attributes |= AM_RDO;
}

static void htu_choose_new_image(uint8_t b)
{
    int digits[3], pos, i;
    time_t t = time_now();

    /* Isolate the digits of the current slot number. */
    i = cfg.slot_nr;
    for (pos = 0; pos < 3; pos++) {
        digits[pos] = i % 10;
        i /= 10;
    }

    /* Tens or Units depending on which button is pressed. */
    pos = !(b & B_RIGHT);

    do {
        if (!(b ^ (B_LEFT|B_RIGHT))) {
            /* Both Buttons: Hundreds. */
            pos = 2;
            if (time_since(t) > time_ms(1000)) {
                /* After 1sec with both buttons held, immediately update the
                 * display to show the reset to 000. */
                i = 0;
                goto out;
            }
        }
    } while ((b = buttons&(B_LEFT|B_RIGHT)) != 0);

    /* Increment the selected digit. */
    digits[pos] = (digits[pos] + 1) % 10;

    /* Reconstitute the slot number. Reset the selected digit on overflow. */
    i = digits[0] + digits[1]*10 + digits[2]*100;
    if (i > cfg.max_slot_nr) {
        digits[pos] = 0;
        i = digits[0] + digits[1]*10 + digits[2]*100;
    }

out:
    cfg.slot_nr = i;
    cfg_update(CFG_KEEP_SLOT_NR);
    display_write_slot(TRUE);
    while (b && buttons)
        continue;
}

/* Based on button presses, change which floppy image is selected. */
static bool_t choose_new_image(uint8_t init_b)
{
    uint8_t b, prev_b;
    time_t last_change = 0;
    int old_slot = cfg.slot_nr, i, changes = 0;
    uint8_t twobutton_action = ff_cfg.twobutton_action & TWOBUTTON_mask;

    for (prev_b = 0, b = init_b;
         (b &= (B_LEFT|B_RIGHT)) != 0;
         prev_b = b, b = buttons) {

        if (prev_b == b) {
            /* Decaying delay between image steps while button pressed. */
            time_t delay = time_ms(1000) / (changes + 1);
            if (delay < time_ms(50))
                delay = time_ms(50);
            if (twobutton_action == TWOBUTTON_rotary_fast)
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

        if (twobutton_action == TWOBUTTON_htu) {
            htu_choose_new_image(b);
            return FALSE;
        }

        i = cfg.slot_nr;
        if (!(b ^ (B_LEFT|B_RIGHT))) {
            if (twobutton_action == TWOBUTTON_eject) {
                cfg.slot_nr = old_slot;
                cfg.ejected = TRUE;
                cfg_update(CFG_KEEP_SLOT_NR);
                break;
            }
            i = cfg.slot_nr = 0;
            cfg_update(CFG_KEEP_SLOT_NR);
            if ((twobutton_action == TWOBUTTON_rotary)
                || (twobutton_action == TWOBUTTON_rotary_fast)) {
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
            i -= velocity ?: 1;
            if ((i < 0) && !ff_cfg.nav_loop) {
                i = 0;
                goto b_right;
            }
            while (i < 0)
                i += cfg.max_slot_nr + 1;
        b_left:
            while (!slot_valid(i)) {
                if (i-- == 0) {
                    if (!ff_cfg.nav_loop)
                        goto b_right;
                    i = cfg.max_slot_nr;
                }
            } while (!slot_valid(i));
        } else { /* b & B_RIGHT */
            i += velocity ?: 1;
            if ((i > cfg.max_slot_nr) && !ff_cfg.nav_loop) {
                i = cfg.max_slot_nr;
                goto b_left;
            }
            i = i % (cfg.max_slot_nr + 1);
        b_right:
            while (!slot_valid(i)) {
                if (i++ >= cfg.max_slot_nr) {
                    if (!ff_cfg.nav_loop)
                        goto b_left;
                    i = 0;
                }
            }
        }

        cfg.slot_nr = i;
        cfg_update(CFG_KEEP_SLOT_NR);
        display_write_slot(TRUE);
    }

    return FALSE;
}

static void assert_volume_connected(void)
{
    if (!volume_connected() || cfg.usb_power_fault)
        F_die(FR_DISK_ERR);
}

static int run_floppy(void *_b)
{
    volatile uint8_t *pb = _b;
    time_t t_now, t_prev, t_diff;
    int32_t update_ticks;

    floppy_insert(0, &cfg.slot);

    led_7seg_update_track(TRUE);

    update_ticks = time_ms(20);
    t_prev = time_now();
    while (((*pb = buttons) == 0) && !floppy_handle()) {
        t_now = time_now();
        t_diff = time_diff(t_prev, t_now);
        if ((update_ticks -= t_diff) <= 0) {
            led_7seg_update_track(FALSE);
            lcd_write_track_info(FALSE);
            update_ticks = time_ms(20);
        }
        if (display_type == DT_LCD_OLED) {
            lcd_scroll.ticks -= t_diff;
            lcd_scroll_name();
        }
        canary_check();
        assert_volume_connected();
        t_prev = t_now;
    }

    if (display_type == DT_LED_7SEG)
        display_state = LED_NORMAL;

    return 0;
}

static void floppy_arena_setup(void)
{
    arena_init();

    fs = arena_alloc(sizeof(*fs));

    if (cfg.sorted) {
        native_get_slot_map(FALSE);
    } else {
        unsigned int cache_len = arena_avail();
        uint8_t *cache_start = arena_alloc(0);
        volume_cache_init(cache_start, cache_start + cache_len);
    }
}

static void floppy_arena_teardown(void)
{
    fs = NULL;
    volume_cache_destroy();
}

static void noinline volume_space(void)
{
    char msg[25];
    unsigned int free = (fatfs.free_clst*fatfs.csize+1953/2)/1953 + 100/2;
    unsigned int total = (fatfs.n_fatent*fatfs.csize+1953/2)/1953 + 100/2;
    if (fatfs.free_clst < fatfs.n_fatent-2) {
        snprintf(msg, sizeof(msg), "Free:%u.%u/%u.%uG",
                 free/1000, (free%1000)/100,
                 total/1000, (total%1000)/100);
    } else {
        snprintf(msg, sizeof(msg), "Volume: %u.%uG",
                 total/1000, (total%1000)/100);
    }
    lcd_write(0, 2, -1, msg);

}

/* Wait 50ms for 2-button press. */
static uint8_t wait_twobutton_press(uint8_t b)
{
    unsigned int wait;

    for (wait = 0; wait < 50; wait++) {
        if ((buttons & (B_LEFT|B_RIGHT)) == (B_LEFT|B_RIGHT))
            b = B_SELECT;
        if (b & B_SELECT)
            break;
        delay_ms(1);
    }

    return b;
}

static uint8_t menu_wait_button(bool_t twobutton_eject, const char *led_msg)
{
    unsigned int wait = 0;
    uint8_t b;

    /* Wait for any button to be pressed. */
    while ((b = buttons) == 0) {
        /* Bail if USB disconnects. */
        assert_volume_connected();
        /* Update the display. */
        delay_ms(1);
        switch (display_type) {
        case DT_LED_7SEG:
            /* Alternate the 7-segment display. */
            if ((++wait % 1000) == 0) {
                switch (wait / 1000) {
                case 1:
                    led_7seg_write_decimal(cfg.slot_nr);
                    break;
                default:
                    led_7seg_write_string(led_msg);
                    wait = 0;
                    break;
                }
            }
            break;
        case DT_LCD_OLED:
            /* Continue to scroll long filename. */
            lcd_scroll.ticks -= time_ms(1);
            lcd_scroll_name();
            break;
        }
    }

    return twobutton_eject ? wait_twobutton_press(b) : b;
}

static uint8_t noinline display_error(FRESULT fres, uint8_t b)
{
    bool_t twobutton_eject =
        (ff_cfg.twobutton_action & TWOBUTTON_mask) == TWOBUTTON_eject;
    char msg[17];

    display_mode = DM_menu;

    switch (display_type) {
    case DT_LED_7SEG:
        snprintf(msg, sizeof(msg), "%c%02u",
                 (fres >= 30) ? 'E' : 'F', fres);
        led_7seg_write_string(msg);
        break;
    case DT_LCD_OLED:
        snprintf(msg, sizeof(msg), "*%s*%02u*",
                 (fres >= 30) ? "ERR" : "FAT", fres);
        lcd_write(wp_column+1, 1, -1, "");
        lcd_write((lcd_columns > 16) ? 10 : 8, 1, 0, msg);
        lcd_on();
        break;
    }

    /* Wait for buttons to be released. */
    while (buttons != 0)
        continue;

    /* Wait for any button to be pressed. */
    b = menu_wait_button(twobutton_eject, msg);

    while (b & B_SELECT) {
        b = buttons;
        if (twobutton_eject && b) {
            /* Wait for 2-button release. */
            b = B_SELECT;
        }
    }

    display_mode = DM_normal;
    return b;
}

static bool_t confirm(const char *op)
{
    char msg[17];
    uint8_t b;

    snprintf(msg, sizeof(msg), "Confirm %s?", op);
    lcd_write(0, 1, -1, msg);

    while (buttons)
        continue;

    while ((b = menu_wait_button(TRUE, "")) == 0)
        continue;

    return (b == B_SELECT);
}

static void image_copy(void)
{
    cfg.clipboard = cfg.slot;
}

static void image_paste(const char *subfolder)
{
    time_t t;
    uint32_t cur_cdir = fatfs.cdir;
    int i, baselen, todo, idx, max_idx = -1;
    char *p, *q;
    FIL *nfil;
    bool_t use_basename = FALSE;
    const struct slot *slot = &cfg.clipboard;

    if (!slot->size || !confirm("Paste"))
        return;

    if (subfolder)
        F_chdir(subfolder);

    /* Is the source filename of the form '*_000'? */
    strcpy(fs->buf, slot->name);
    if ((p = q = strrchr(fs->buf, '_')) != NULL) {
        for (i = 0; i < 3; i++) {
            char c = *++p;
            if ((c < '0') || (c > '9'))
                break;
        }
        p = ((i == 3) && (p[1] == '\0')) ? q : NULL;
    }

    if (p == NULL) {
        /* Source filename is not of the form '*_000'. Does it exist at the 
         * destinaton? */
        FRESULT fres;
        p = fs->buf + strlen(fs->buf);
        snprintf(p, sizeof(fs->buf)-(p-fs->buf), ".%s", slot->type);
        fres = f_stat(fs->buf, &fs->fp);
        /* If it doesn't exist, we use the same exact name for the clone. */
        use_basename = (fres == FR_NO_FILE);
    }

    if (use_basename) {

        /* Clone the original source filename. */
        t = time_now();
        lcd_write(0, 1, -1, "Pasting...");

    } else {

        /* Search for next available three-digit clone identifier. */
        baselen = p - fs->buf;
        snprintf(p, sizeof(fs->buf) - baselen, "_*.%s", slot->type);

        for (F_findfirst(&fs->dp, &fs->fp, "", fs->buf);
             fs->fp.fname[0] != '\0';
             F_findnext(&fs->dp, &fs->fp)) {
            p = fs->fp.fname + baselen + 1;
            /* Parse 3-digit index number. */
            idx = 0;
            for (i = 0; i < 3; i++) {
                if ((*p < '0') || (*p > '9'))
                    break;
                idx *= 10;
                idx += *p++ - '0';
            }
            /* Expect a 3-digit number. */
            if ((i != 3) || (*p != '.'))
                continue;
            max_idx = max_t(int, max_idx, idx);
        }
        F_closedir(&fs->dp);

        /* Is there already a '*_999' file? */
        t = time_now();
        if (max_idx >= 999) {
            lcd_write(0, 1, -1, "No spare slots!");
            goto out;
        }

        snprintf(fs->buf, sizeof(fs->buf), "Pasting (%03u)...", max_idx+1);
        lcd_write(0, 1, -1, fs->buf);

        strcpy(fs->buf, slot->name);
        snprintf(fs->buf + baselen, sizeof(fs->buf) - baselen, "_%03u.%s",
                 max_idx+1, slot->type);

    }

    volume_cache_destroy();
    fatfs_from_slot(&fs->file, slot, FA_READ);
    nfil = arena_alloc(sizeof(*nfil));
    F_open(nfil, fs->buf, FA_CREATE_NEW|FA_WRITE);
    todo = f_size(&fs->file); 
    while (todo != 0) {
        int nr = min_t(int, todo, arena_avail());
        void *p = arena_alloc(0);
        F_read(&fs->file, p, nr, NULL);
        F_write(nfil, p, nr, NULL);
        todo -= nr;
    }
    F_close(nfil);
    fatfs.cdir = cfg.cur_cdir;
    floppy_arena_setup();
    if (!cfg.sorted)
        cfg_update(CFG_READ_SLOT_NR);

out:
    fatfs.cdir = cur_cdir;
    delay_from(t, time_ms(2000));
}

static bool_t image_delete(void)
{
    FRESULT fres;
    time_t t = time_now();
    bool_t ok = FALSE;

    if (!confirm("Delete"))
        return FALSE;

    lcd_write(0, 1, -1, "Deleting...");

    /* Kill the clipboard if we're deleting the copy source. */
    if (cfg.slot.firstCluster == cfg.clipboard.firstCluster)
        memset(&cfg.clipboard, 0, sizeof(cfg.clipboard));

    snprintf(fs->buf, sizeof(fs->buf), "%s.%s",
             cfg.slot.name, cfg.slot.type);
    fres = f_unlink(fs->buf);
    if (fres != FR_OK) {
        snprintf(fs->buf, sizeof(fs->buf), "Failed (%d)", fres);
        t = time_now();
    }

    cfg.slot_nr = min_t(uint16_t, cfg.slot_nr, cfg.max_slot_nr-1);
    cfg.sorted = NULL;
    cfg_update(CFG_READ_SLOT_NR);
    ok = TRUE;

    delay_from(t, time_ms(2000));
    return ok;
}

enum {
    EJM_header = 0,
    EJM_wrprot,
    EJM_copy,
    EJM_paste,
    EJM_delete,
    EJM_exit_to_selector,
    EJM_exit_reinsert,
    EJM_nr
};

static int construct_eject_menu(uint8_t *menu)
{
    int i, j;
    for (i = j = 0; i < EJM_nr; i++) {
        if ((i == EJM_paste) && !cfg.clipboard.size)
            continue;
        if ((i >= EJM_copy) && (i <= EJM_delete) && cfg.hxc_mode)
            continue;
        menu[j++] = i;
    }
    return j-1;
}

static uint8_t noinline eject_menu(uint8_t b)
{
    const static char *menu_s[] = {
        [EJM_header] = "**Eject Menu**",
        [EJM_copy]   = "Copy",
        [EJM_paste]  = "Paste",
        [EJM_delete] = "Delete",
        [EJM_exit_to_selector] = "Exit to Selector",
        [EJM_exit_reinsert] = "Exit & Re-Insert",
    };

    char msg[17];
    uint8_t menu[EJM_nr];
    unsigned int wait;
    int sel = 0, sel_max;
    bool_t twobutton_eject =
        ((ff_cfg.twobutton_action & TWOBUTTON_mask) == TWOBUTTON_eject)
        || (display_type == DT_LCD_OLED); /* or two buttons can't exit menu */

    display_mode = DM_menu;

    ima_mark_ejected(TRUE);

    sel_max = construct_eject_menu(menu);

    for (;;) {

        switch (display_type) {
        case DT_LED_7SEG:
            led_7seg_write_string("EJE");
            break;
        case DT_LCD_OLED:
            switch (menu[sel]) {
            case EJM_wrprot:
                snprintf(msg, sizeof(msg), "Write Prot.: O%s",
                         (cfg.slot.attributes & AM_RDO) ? "N" : "FF");
                lcd_write(0, 1, -1, msg);
                break;
            default:
                lcd_write(0, 1, -1, menu_s[menu[sel]]);
                break;
            }
            lcd_on();
            break;
        }

        /* Wait for buttons to be released. */
        wait = 0;
        while (buttons != 0) {
            delay_ms(1);
            if ((display_type != DT_LCD_OLED) && (wait++ >= 2000)) {
            toggle_wp:
                wait = 0;
                cfg.slot.attributes ^= AM_RDO;
                if (volume_readonly()) {
                    /* Read-only filesystem: force AM_RDO always. */
                    cfg.slot.attributes |= AM_RDO;
                }
                if (display_type == DT_LED_7SEG)
                    led_7seg_write_string((cfg.slot.attributes & AM_RDO)
                                          ? "RDO" : "RIT");
            }
        }

        /* Wait for any button to be pressed. */
        b = menu_wait_button(twobutton_eject, "EJE");

        if (b & B_LEFT)
            sel--;
        if (b & B_RIGHT)
            sel++;
        if ((sel != 0) && (display_type != DT_LCD_OLED))
            goto out;

        if (sel < 0)
            sel = sel_max;
        if (sel > sel_max)
            sel = 0;

        if (b & B_SELECT) {
            /* Wait for eject button to be released. */
            wait = 0;
            while (b & B_SELECT) {
                b = buttons;
                if (twobutton_eject && b) {
                    /* Wait for 2-button release. */
                    b = B_SELECT;
                }
                delay_ms(1);
                if ((display_type != DT_LCD_OLED) && (wait++ >= 2000))
                    goto toggle_wp;
            }
            /* LED display: No menu, we exit straight out. */
            if (display_type != DT_LCD_OLED)
                goto out;
            switch (menu[sel]) {
            case EJM_wrprot: /* Toggle W.Protect */
                cfg.slot.attributes ^= AM_RDO;
                if (volume_readonly()) {
                    /* Read-only filesystem: force AM_RDO always. */
                    cfg.slot.attributes |= AM_RDO;
                }
                break;
            case EJM_copy:
                image_copy();
                sel_max = construct_eject_menu(menu);
                break;
            case EJM_paste:
                image_paste(NULL);
                break;
            case EJM_delete:
                if (!image_delete())
                    break;
                b = 0xff; /* selector */
                goto out;
            case EJM_exit_to_selector:
                display_write_slot(TRUE);
                b = 0xff; /* selector */
                goto out;
            case EJM_header:
            case EJM_exit_reinsert:
                b = 0;
                goto out;
            }
        }
 
    }

out:
    ima_mark_ejected(FALSE);
    display_mode = DM_normal;
    return b;
}

static void folder_menu(void)
{
    if (display_type != DT_LCD_OLED)
        return;

    /* HACK: image_paste() clobbers fs->fp.fname so we make use of
     * cfg.slot.name instead. */
    snprintf(cfg.slot.name, sizeof(cfg.slot.name),
             "%s", fs->fp.fname);
    image_paste(cfg.slot.name);

    /* Now we must refresh the display */
    cfg_update(CFG_KEEP_SLOT_NR);
    display_write_slot(TRUE);
}

static int floppy_main(void *unused)
{
    FRESULT fres;
    uint8_t b;
    uint32_t i;

    /* If any buttons are pressed when USB drive is mounted then we start 
     * in ejected state. */
    cfg.ejected = (buttons != 0);

    cfg.sorted = NULL;
    floppy_arena_setup();

    lcd_clear();
    display_mode = DM_normal;

    cfg_init();
    cfg_update(CFG_READ_SLOT_NR);

    /* If we start on a folder, go directly into the image selector. */
    if (cfg.slot.attributes & AM_DIR) {
        volume_space();
        display_write_slot(FALSE);
        b = buttons;
        goto select;
    }

    for (;;) {

        volume_space();

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
            if (cfg.hxc_mode) {
                /* No directory support in HxC selector/indexed modes. */
                F_die(FR_BAD_IMAGE);
            }
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
            cfg.sorted = NULL;
            cfg_update(CFG_READ_SLOT_NR);
            display_write_slot(FALSE);
            b = buttons;
            goto select;
        }

        display_write_slot(FALSE);
        if (display_type == DT_LCD_OLED)
            lcd_write_track_info(TRUE);

        printk("Current slot: %u/%u\n", cfg.slot_nr, cfg.max_slot_nr);
        printk("Name: '%s' Type: %s\n", cfg.slot.name, cfg.slot.type);
        printk("Attr: %02x Clus: %08x Size: %u\n",
               cfg.slot.attributes, cfg.slot.firstCluster, cfg.slot.size);

        logfile_flush(&fs->file);

        if (cfg.ejected) {
            cfg.ejected = FALSE;
            b = B_SELECT;
        } else {
            floppy_arena_teardown();
            fres = F_call_cancellable(run_floppy, &b);
            floppy_cancel();
            assert_volume_connected();
            if ((b != 0) && (display_type == DT_LCD_OLED)) {
                /* Immediate visual indication of button press. */
                lcd_write(wp_column, 1, 1, "-");
                lcd_on();
            }
            floppy_arena_setup();
            logfile_flush(&fs->file);
            volume_space();
        }

        if (cfg.dirty_slot_name) {
            cfg.dirty_slot_name = FALSE;
            update_slot_by_name();
        }

        if (cfg.dirty_slot_nr) {
            cfg.dirty_slot_nr = FALSE;
            if (!cfg.hxc_mode)
                cfg_update(CFG_KEEP_SLOT_NR); /* get correct fs->fp */
            cfg_update(CFG_WRITE_SLOT_NR);
        }

        if (fres) {
            b = display_error(fres, b);
            fres = FR_OK;
            if (b == 0)
                continue;
        }

        if (b & B_SELECT) {
            b = eject_menu(b);
            if (b == 0)
                continue;
            if (b == 0xff) {
                display_write_slot(FALSE);
                b = 0;
                goto select;
            }
        }

        /* No buttons pressed: we probably just exited D-A mode. */
        if (b == 0) {
            /* If using HXCSDFE.CFG then re-read as it may have changed. */
            if (cfg.hxc_mode && (ff_cfg.nav_mode != NAVMODE_indexed))
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
            if (wait_ms && (display_type == DT_LCD_OLED)) {
                /* Allow time for full name to scroll through. */
                unsigned int scroll_ms = ff_cfg.nav_scroll_pause;
                scroll_ms += lcd_scroll.end * ff_cfg.nav_scroll_rate;
                wait_ms = max(wait_ms, scroll_ms);
            }
            for (i = 0; (wait_ms == 0) || (i < wait_ms); i++) {
                b = buttons;
                if (b != 0)
                    break;
                assert_volume_connected();
                delay_ms(1);
                lcd_scroll.ticks -= time_ms(1);
                lcd_scroll_name();
            }

            /* Wait for select button to be released. */
            for (i = 0; !cfg.ejected && ((b = buttons) & B_SELECT); i++) {
                delay_ms(1);
                if (i >= 1500) { /* 1.5 seconds */
                    if (cfg.slot.attributes & AM_DIR) {
                        /* Display Folder Menu. */
                        folder_menu();
                        /* We go back into the selector, and to do this we 
                         * need to wait for all buttons released. */
                        while (buttons)
                            continue;
                        goto select;
                    } else {
                        cfg.ejected = TRUE;
                    }
                }
            }

        } while (!cfg.ejected && (b != 0));

        /* Write the slot number resulting from the latest round of button 
         * presses back to the config file. */
        cfg_update(CFG_WRITE_SLOT_NR);
    }

    ASSERT(0);
    return 0;
}

static bool_t main_menu_confirm(const char *op)
{
    char msg[17];
    uint8_t b;

    snprintf(msg, sizeof(msg), "Confirm %s?", op);
    lcd_write(0, 1, -1, msg);

    while ((b = buttons) == 0)
        continue;

    return wait_twobutton_press(b) == B_SELECT;
}

static void factory_reset(void)
{
    /* Inform user that factory reset is about to occur. */
    switch (display_type) {
    case DT_LED_7SEG:
        led_7seg_write_string("RST");
        break;
    case DT_LCD_OLED:
        lcd_clear();
        display_mode = DM_banner; /* double height row 0 */
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

static void update_firmware(void)
{
#if MCU == STM32F105

    /* Power up the backup-register interface and allow writes. */
    rcc->apb1enr |= RCC_APB1ENR_PWREN | RCC_APB1ENR_BKPEN;
    pwr->cr |= PWR_CR_DBP;

    /* Indicate to bootloader that we want to perform firmware update. */
    bkp->dr1[0] = 0xdead;
    bkp->dr1[1] = 0xbeef;

#elif MCU == AT32F435

    _reset_flag = RESET_FLAG_BOOTLOADER;

#endif

    system_reset();
}

static void ff_osd_configure(void)
{
    if (!has_osd || !main_menu_confirm("OSD Cnf"))
        return;

    lcd_write(0, 1, -1, "Exit: Power Off");

    for (;;) {
        time_t t = time_now();
        uint8_t b = buttons;
        if (b == (B_LEFT|B_RIGHT)) {
            osd_buttons_tx = B_SELECT;
            /* Wait for two-button release. */
            while ((time_diff(t, time_now()) < time_ms(1000)) && buttons)
                continue;
        } else if (b & (B_LEFT|B_RIGHT)) {
            /* Wait 50ms for a two-button press. */
            while ((b == buttons) && (time_diff(t, time_now()) < time_ms(50)))
                continue;
            osd_buttons_tx = (buttons == (B_LEFT|B_RIGHT)) ? B_SELECT : b;
        } else {
            osd_buttons_tx = b;
        }
        /* Hold button-held state for a while to make sure it gets 
         * transferred to the OSD. */
        if (osd_buttons_tx)
            delay_ms(100);
    }
}

static void main_menu(void)
{
    const static char *menu[] = {
        "**Main Menu**",
        "Factory Reset",
        "Update Firmware",
        "Configure FF OSD",
        "Exit",
    };

    int sel = 0;
    uint8_t b;

    /* Not available to 7-segment display. */
    if (display_type != DT_LCD_OLED)
        return;

    display_mode = DM_menu;

    for (;;) {

        if (sel < 0)
            sel += ARRAY_SIZE(menu);
        if (sel >= ARRAY_SIZE(menu))
            sel -= ARRAY_SIZE(menu);

        lcd_write(0, 1, -1, menu[sel]);
        lcd_on();

        /* Wait for buttons to be released. */
        while (buttons != 0)
            continue;

        /* Wait for any button to be pressed. */
        while ((b = buttons) == 0)
            continue;
        b = wait_twobutton_press(b);

        if (b & B_LEFT)
            sel--;
        if (b & B_RIGHT)
            sel++;

        if (b & B_SELECT) {
            /* Wait for buttons to be released. */
            while (buttons)
                continue;
            switch (sel) {
            case 1: /* Factory Reset */
                if (main_menu_confirm("Reset"))
                    factory_reset();
                break;
            case 2: /* Update Firmware */
                if (main_menu_confirm("Update"))
                    update_firmware();
                break;
            case 3: /* Configure FF OSD */
                ff_osd_configure();
                break;
            case 0: case 4: /* Exit */
                goto out;
            }
        }
 
    }

out:
    display_mode = DM_normal;
}

static void noinline banner(void)
{
    char msg[2][25];

    switch (display_type) {

    case DT_LED_7SEG:
#if MCU == STM32F105
#define sep_ch "-"
#elif MCU == AT32F435
#define sep_ch "z"
#endif
        led_7seg_write_string(
#if defined(LOGFILE)
            "LOG"
#elif defined(QUICKDISK)
            (led_7seg_nr_digits() == 3) ? "Q"sep_ch"D" : "QD"
#else
            (led_7seg_nr_digits() == 3) ? "F"sep_ch"F" : "FF"
#endif
            );
#undef sep_ch
        break;

    case DT_LCD_OLED:
        lcd_clear();
        display_mode = DM_banner; /* double height row 0 */
#if MCU == STM32F105
        lcd_write(0, 0, 0, "FlashFloppy");
#elif MCU == AT32F435
        lcd_write(0, 0, 0, "FlashFloppy+");
#endif
        snprintf(msg[0], sizeof(msg[0]), "%s%s", fw_ver,
#if defined(LOGFILE)
                 " Log"
#elif defined(QUICKDISK)
                 " QD"
#else
                 ""
#endif
            );
        snprintf(msg[1], sizeof(msg[1]), "%9s %dkB", msg[0], ram_kb);
        lcd_write(0, 1, 0, msg[1]);
        lcd_on();
        break;

    }
}

static void check_buttons(void)
{
    unsigned int i;
    uint8_t b = buttons;

    /* Need both LEFT and RIGHT pressed, or SELECT alone. */
    if ((b != (B_LEFT|B_RIGHT)) && (b != B_SELECT))
        return;

    /* Buttons must be continuously pressed for three seconds. */
    for (i = 0; (i < 3000) && (buttons == b); i++)
        delay_ms(1);

    if (buttons == b)
        factory_reset();
    else
        main_menu();

    banner();
}

static void maybe_show_version(void)
{
    uint8_t b, nb;
    const char *p, *np;
    char msg[3];
    int len;

    /* LCD/OLED already displays version info in idle state. */
    if (display_type != DT_LED_7SEG)
        return;

    /* Check if right button is pressed and released. */
    if ((b = buttons) != B_RIGHT)
        return;
    while ((nb = buttons) == b)
        continue;
    if (nb)
        return;

    led_7seg_write_string("RAN");
    delay_ms(1000);

    led_7seg_write_decimal(ram_kb);
    delay_ms(1000);

    led_7seg_write_string("UER");
    delay_ms(1000);

    /* Iterate through the dotted sections of the version number. */
    for (p = fw_ver; p != NULL; p = np ? np+1 : NULL) {
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
    } else if (volume_connected() && (fres != FR_OK)) {
        printk("**Error %u\n", fres);
        snprintf(msg, sizeof(msg),
                 (display_type == DT_LED_7SEG)
                 ? ((fres >= 30) ? "E%02u" : "F%02u")
                 : ((fres >= 30) ? "*ERROR* %02u" : "*FATFS* %02u"),
                 fres);
    } else {
        /* No error. Do nothing. */
        return;
    }

    switch (display_type) {
    case DT_LED_7SEG:
        led_7seg_write_string(msg);
        break;
    case DT_LCD_OLED:
        display_mode = DM_menu; /* double-height row 1 */
        lcd_write(0, 0, -1, "***************");
        lcd_write(0, 1, -1, msg);
        lcd_on();
        break;
    }

    /* Wait for buttons to be released, pressed and released again. */
    while (buttons)
        continue;
    while (!buttons && (pwr || volume_connected()))
        continue;
    while (buttons)
        continue;

    /* On USB power fault we simply reset. */
    if (pwr)
        system_reset();
}

int main(void)
{
    static const char * const board_name[] = {
        [BRDREV_Gotek_standard] = "Standard",
        [BRDREV_Gotek_enhanced] = "Enhanced",
        [BRDREV_Gotek_sd_card]  = "Enhanced + SD"
    };

    FRESULT fres;

    /* Relocate DATA. Initialise BSS. */
    if (&_sdat[0] != &_ldat[0])
        memcpy(_sdat, _ldat, _edat-_sdat);
    memset(_sbss, 0, _ebss-_sbss);

    canary_init();
    stm32_init();
    time_init();
    console_init();
    board_init();
    console_crash_on_input();
    delay_ms(200); /* 5v settle */

    printk("\n** FlashFloppy %s\n", fw_ver);
    printk("** Keir Fraser <keir.xen@gmail.com>\n");
    printk("** github:keirf/flashfloppy\n\n");

    printk("Build: %s %s\n", build_date, build_time);
    printk("Board: %s\n", board_name[board_id]);

    speaker_init();

    flash_ff_cfg_read();

    floppy_init();

    display_init();

    while (floppy_ribbon_is_reversed()) {
        printk("** Error: Ribbon cable upside down?\n");
        switch (display_type) {
        case DT_LED_7SEG:
            led_7seg_write_string("RIB");
            break;
        case DT_LCD_OLED:
            lcd_write(0, 0, -1, "Ribbon Cable May");
            lcd_write(0, 1, -1, "Be Upside Down? ");
            lcd_on();
            break;
        }
    }

    usbh_msc_init();

    rotary = board_get_rotary();
    set_rotary_exti();
    timer_init(&button_timer, button_timer_fn, NULL);
    timer_set(&button_timer, time_now());

    for (;;) {

        banner();

        arena_init();
        usbh_msc_buffer_set(arena_alloc(512));
        while ((f_mount(&fatfs, "", 1) != FR_OK) && !cfg.usb_power_fault) {
            maybe_show_version();
            check_buttons();
            usbh_msc_process();
        }
        usbh_msc_buffer_set((void *)0xdeadbeef);

        fres = F_call_cancellable(floppy_main, NULL);
        osd_buttons_tx = 0;
        floppy_cancel();
        floppy_arena_teardown();

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

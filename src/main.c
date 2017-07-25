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
} *fs;

static struct {
    uint16_t slot_nr, max_slot_nr;
    uint8_t slot_map[1000/8];
    struct v2_slot autoboot, hxcsdfe, slot;
} cfg;

uint8_t board_id;

static uint8_t display_mode;
#define DM_LCD_1602 1
#define DM_LED_7SEG 2

#define IMAGE_SELECT_WAIT_SECS  4
#define BACKLIGHT_ON_SECS      20

static uint32_t backlight_ticks;
static uint8_t backlight_state;
#define BACKLIGHT_OFF          0
#define BACKLIGHT_SWITCHING_ON 1
#define BACKLIGHT_ON           2

static void lcd_on(void)
{
    backlight_ticks = 0;
    barrier();
    backlight_state = BACKLIGHT_ON;
    barrier();
    lcd_backlight(TRUE);
}

static void lcd_write_slot(void)
{
    char msg[17];
    snprintf(msg, sizeof(msg), "%s", cfg.slot.name);
    lcd_write(0, 0, 16, msg);
    snprintf(msg, sizeof(msg), "%03u/%03u", cfg.slot_nr, cfg.max_slot_nr);
    lcd_write(0, 1, 0, msg);
    lcd_on();
}

static struct timer button_timer;
static volatile uint8_t buttons;
#define B_LEFT 1
#define B_RIGHT 2
static void button_timer_fn(void *unused)
{
    static uint16_t bl, br;
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

    /* LCD backlight handling. */
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
        if (backlight_ticks++ == 200*BACKLIGHT_ON_SECS) {
            lcd_backlight(FALSE);
            backlight_state = BACKLIGHT_OFF;
        }
        break;
    }

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
    slot->attributes = file->obj.attr;
    slot->firstCluster = file->obj.sclust;
    slot->size = file->obj.objsize;
    strcpy(slot->name, name);
    memcpy(slot->type, dot+1, 3);
}

static void init_cfg(void)
{
    F_open(&fs->file, "HXCSDFE.CFG", FA_READ);
    fatfs_to_slot(&cfg.hxcsdfe, &fs->file, "HXCSDFE.CFG");
    F_close(&fs->file);

    F_open(&fs->file, "AUTOBOOT.HFE", FA_READ);
    fatfs_to_slot(&cfg.autoboot, &fs->file, "AUTOBOOT.HFE");
    F_close(&fs->file);
}

#define CFG_KEEP_SLOT_NR  0 /* Do not re-read slot number from config */
#define CFG_READ_SLOT_NR  1 /* Read slot number afresh from config */
#define CFG_WRITE_SLOT_NR 2 /* Write new slot number to config */
static void read_cfg(uint8_t slot_mode)
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
    switch (hxc_cfg.signature[9]-'0') {
    case 1: {
        struct v1_slot v1_slot;
        if (slot_mode != CFG_READ_SLOT_NR) {
            hxc_cfg.slot_index = cfg.slot_nr;
            if (slot_mode == CFG_WRITE_SLOT_NR) {
                F_lseek(&fs->file, 0);
                F_write(&fs->file, &hxc_cfg, sizeof(hxc_cfg), NULL);
            }
        }
        cfg.slot_nr = hxc_cfg.slot_index;
        cfg.max_slot_nr = hxc_cfg.number_of_slot - 1;
        memset(&cfg.slot_map, 0xff, sizeof(cfg.slot_map));
        if (cfg.slot_nr == 0)
            break;
        F_lseek(&fs->file, 1024 + cfg.slot_nr*128);
        F_read(&fs->file, &v1_slot, sizeof(v1_slot), NULL);
        memcpy(&cfg.slot.type, &v1_slot.name[8], 3);
        memcpy(&cfg.slot.attributes, &v1_slot.attributes, 1+4+4+17);
        cfg.slot.name[17] = '\0';
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
        cfg.max_slot_nr = hxc_cfg.max_slot_number - 1;
        F_lseek(&fs->file, hxc_cfg.slots_map_position*512);
        F_read(&fs->file, &cfg.slot_map, sizeof(cfg.slot_map), NULL);
        cfg.slot_map[0] |= 0x80; /* slot 0 always available */
        if (cfg.slot_nr == 0)
            break;
        F_lseek(&fs->file, hxc_cfg.slots_position*512
                + cfg.slot_nr*64*hxc_cfg.number_of_drive_per_slot);
        F_read(&fs->file, &cfg.slot, sizeof(cfg.slot), NULL);
        break;
    default:
    bad_signature:
        hxc_cfg.signature[15] = '\0';
        printk("Bad signature '%s'\n", hxc_cfg.signature);
        F_die();
    }
    F_close(&fs->file);

    if (cfg.slot_nr == 0)
        memcpy(&cfg.slot, &cfg.autoboot, sizeof(cfg.slot));

    for (i = 0; i < 3; i++)
        cfg.slot.type[i] = tolower(cfg.slot.type[i]);
}

/* Based on button presses, change which floppy image is selected. */
static void choose_new_image(uint8_t init_b)
{
    char msg[4];
    uint8_t b, prev_b;
    stk_time_t last_change = 0;
    uint32_t i, changes = 0;

    for (prev_b = 0, b = init_b; b != 0; prev_b = b, b = buttons) {
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
                led_7seg_write("000");
                break;
            case DM_LCD_1602:
                read_cfg(CFG_KEEP_SLOT_NR);
                lcd_write_slot();
                break;
            }
            /* Ignore changes while user is releasing the buttons. */
            while ((stk_diff(last_change, stk_now()) < stk_ms(1000))
                   && buttons)
                continue;
        } else if (b & B_LEFT) {
            do {
                if (i-- == 0)
                    i = cfg.max_slot_nr;
            } while (!(cfg.slot_map[i/8] & (0x80>>(i&7))));
        } else { /* b & B_RIGHT */
            do {
                if (i++ >= cfg.max_slot_nr)
                    i = 0;
            } while (!(cfg.slot_map[i/8] & (0x80>>(i&7))));
        }
        cfg.slot_nr = i;
        switch (display_mode) {
        case DM_LED_7SEG:
            snprintf(msg, sizeof(msg), "%03u", cfg.slot_nr);
            led_7seg_write(msg);
            break;
        case DM_LCD_1602:
            read_cfg(CFG_KEEP_SLOT_NR);
            lcd_write_slot();
            break;
        }
    }
}

int floppy_main(void)
{
    char msg[4];
    uint8_t b;
    uint32_t i;

    lcd_clear();
    arena_init();
    fs = arena_alloc(sizeof(*fs));
    init_cfg();
    read_cfg(CFG_READ_SLOT_NR);

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
            read_cfg(CFG_WRITE_SLOT_NR);
        }

        fs = NULL;

        switch (display_mode) {
        case DM_LED_7SEG:
            snprintf(msg, sizeof(msg), "%03u", cfg.slot_nr);
            led_7seg_write(msg);
            break;
        case DM_LCD_1602:
            lcd_write_slot();
            break;
        }

        printk("Current slot: %u/%u\n", cfg.slot_nr, cfg.max_slot_nr);
        memcpy(msg, cfg.slot.type, 3);
        printk("Name: '%s' Type: %s\n", cfg.slot.name, msg);
        printk("Attr: %02x Clus: %08x Size: %u\n",
               cfg.slot.attributes, cfg.slot.firstCluster, cfg.slot.size);

        floppy_insert(0, &cfg.slot);

        while (((b = buttons) == 0) && !floppy_handle()) {
            canary_check();
            if (!usbh_msc_connected())
                F_die();
        }

        floppy_cancel();
        arena_init();
        fs = arena_alloc(sizeof(*fs));

        /* No buttons pressed: re-read config and carry on. */
        if (b == 0) {
            read_cfg(CFG_READ_SLOT_NR);
            continue;
        }

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
        } while (b != 0);

        /* Write the slot number resulting from the latest round of button 
         * presses back to the config file. */
        read_cfg(CFG_WRITE_SLOT_NR);
    }

    ASSERT(0);
    return 0;
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
    timers_init();

    console_init();
    console_crash_on_input();

    board_init();

    speaker_init();

    backlight_init();
    tft_init();
    backlight_set(8);
    touch_init();

    if (lcd_init()) {
        display_mode = DM_LCD_1602;
    } else {
        led_7seg_init();
        display_mode = DM_LED_7SEG;
    }

    usbh_msc_init();

    floppy_init();

    timer_init(&button_timer, button_timer_fn, NULL);
    timer_set(&button_timer, stk_now());

    for (;;) {

        switch (display_mode) {
        case DM_LED_7SEG:
            led_7seg_write("F-F");
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

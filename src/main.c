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

void speed_tests(void) __attribute__((weak, alias("dummy_fn")));
void speed_tests_cancel(void) __attribute__((weak, alias("dummy_fn")));
static void dummy_fn(void) {}

static struct timer button_timer;
static volatile uint8_t buttons;
#define B_LEFT 1
#define B_RIGHT 2
static void button_timer_fn(void *unused)
{
    static uint16_t bl, br;
    uint8_t b = 0;

    bl <<= 1;
    bl |= gpio_read_pin(gpioc, 8);
    if (bl == 0)
        b |= B_LEFT;

    br <<= 1;
    br |= gpio_read_pin(gpioc, 7);
    if (br == 0)
        b |= B_RIGHT;

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

static void read_cfg(bool_t writeback_slot_nr)
{
    struct hxcsdfe_cfg hxc_cfg;
    BYTE mode = FA_READ;
    int i;

    if (writeback_slot_nr)
        mode |= FA_WRITE;

    fatfs_from_slot(&fs->file, &cfg.hxcsdfe, mode);
    F_read(&fs->file, &hxc_cfg, sizeof(hxc_cfg), NULL);
    if (strncmp("HXCFECFGV", hxc_cfg.signature, 9))
        goto bad_signature;
    switch (hxc_cfg.signature[9]-'0') {
    case 1: {
        struct v1_slot v1_slot;
        if (writeback_slot_nr) {
            hxc_cfg.slot_index = cfg.slot_nr;
            F_lseek(&fs->file, 0);
            F_write(&fs->file, &hxc_cfg, sizeof(hxc_cfg), NULL);
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
        if (writeback_slot_nr) {
            hxc_cfg.cur_slot_number = cfg.slot_nr;
            F_lseek(&fs->file, 0);
            F_write(&fs->file, &hxc_cfg, sizeof(hxc_cfg), NULL);
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

int floppy_main(void)
{
    char msg[4];
    uint8_t b, prev_b;
    stk_time_t last_change = 0;
    uint32_t i, changes = 0;

    speed_tests();

    arena_init();
    fs = arena_alloc(sizeof(*fs));
    init_cfg();
    read_cfg(FALSE);

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
            read_cfg(TRUE);
        }

        fs = NULL;

        snprintf(msg, sizeof(msg), "%03u", cfg.slot_nr);
        led_7seg_write(msg);
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
            read_cfg(FALSE);
            continue;
        }

        /* While buttons are pressed we poll them and update current slot 
         * accordingly. */
        for (prev_b = 0; b != 0; prev_b = b, b = buttons) {
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
                i = 0;
                led_7seg_write("000");
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
            snprintf(msg, sizeof(msg), "%03u", cfg.slot_nr);
            led_7seg_write(msg);
        }

        read_cfg(TRUE);
    }

    ASSERT(0);
    return 0;
}

int main(void)
{
    unsigned int i;
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

    led_7seg_init();

    usbh_msc_init();

    floppy_init();

    timer_init(&button_timer, button_timer_fn, NULL);
    timer_set(&button_timer, stk_now());

    for (i = 0; ; i++) {

        bool_t mount_err = 0;

        led_7seg_write("F-F");

        while (f_mount(&fatfs, "", 1) != FR_OK) {
            usbh_msc_process();
            if (!mount_err) {
                mount_err = 1;
                draw_string_8x16(2, 7, "* Please Insert Valid SD Card *");
            }
        }
        if (mount_err)
            clear_screen();

        arena_init();
        fres = F_call_cancellable(floppy_main);
        floppy_cancel();
        speed_tests_cancel();
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

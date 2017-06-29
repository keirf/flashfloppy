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
    char lfn[_MAX_LFN+1];
} *fs;

uint8_t board_id;

void speed_tests(void) __attribute__((weak, alias("dummy_fn")));
void speed_tests_cancel(void) __attribute__((weak, alias("dummy_fn")));
static void dummy_fn(void) {}

static void do_tft(void)
{
    uint16_t x, y;
    int sx, sy;
    if (!touch_get_xy(&x, &y))
        return;
    /* x=0x160-0xe20; y=0x190-0xe60 */
    sx = (x - 0x160) * 320 / (0xe20-0x160);
    sy = (y - 0x190) * 240 / (0xe60-0x190);
    if (sx < 0) sx = 0;
    if (sx >= 320) sx = 319;
    if (sy < 0) sy = 0;
    if (sy >= 240) sy=239;
    fill_rect(sx, sy, 2, 2, 0xf800);
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

static void list_dir(const char *dir)
{
    char *name;
    unsigned int i;

    F_opendir(&fs->dp, dir);

    draw_string_8x16(0, 0, dir);

    for (i = 1; i < TFT_8x16_ROWS; ) {
        F_readdir(&fs->dp, &fs->fp);
        if (fs->fp.fname[0] == '\0')
            break;
        name = *fs->fp.lfname ? fs->fp.lfname : fs->fp.fname;
        if (*name == '.')
            continue;
        draw_string_8x16(0, i++, name);
    }
    F_closedir(&fs->dp);
}

int floppy_main(void)
{
    char buf[32];
    UINT i, nr;

    arena_init();
    fs = arena_alloc(sizeof(*fs));
    fs->fp.lfname = fs->lfn;
    fs->fp.lfsize = sizeof(fs->lfn);

    list_dir("/");

    F_open(&fs->file, "small", FA_READ);
    for (;;) {
        F_read(&fs->file, buf, sizeof(buf), &nr);
        if (nr == 0) {
            printk("\nEOF\n");
            break;
        }
        for (i = 0; i < nr; i++)
            printk("%c", buf[i]);
    }
    F_close(&fs->file);

    fs = NULL;

    speed_tests();

    floppy_insert(0, "nzs_crack.adf");

    for (;;) {
        do_tft();
        floppy_handle();
        canary_check();
    }

    ASSERT(0);
    return 0;
}

int main(void)
{
    /* Relocate DATA. Initialise BSS. */
    if (_sdat != _ldat)
        memcpy(_sdat, _ldat, _edat-_sdat);
    memset(_sbss, 0, _ebss-_sbss);

    canary_init();

    stm32_init();
    timers_init();

    console_init();
    console_crash_on_input();
    delay_ms(250); /* XXX printk debug delay */

    board_init();

    speaker_init();

    backlight_init();
    tft_init();
    backlight_set(8);
    touch_init();

    led_7seg_init();
    led_7seg_write_hex(0xdea);

    usbh_msc_init();

    floppy_init();

    for (;;) {

        bool_t mount_err = 0;
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
        F_call_cancellable(floppy_main);
        floppy_cancel();
        speed_tests_cancel();
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

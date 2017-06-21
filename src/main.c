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

FATFS fatfs;
FIL file;

uint8_t board_id;

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
    DIR dp;
    FILINFO fp;
    static char lfn[_MAX_LFN+1];
    char *name;
    unsigned int i;

    fp.lfname = lfn;
    fp.lfsize = sizeof(lfn);

    F_opendir(&dp, dir);

    draw_string_8x16(0, 0, dir);

    for (i = 1; i < TFT_8x16_ROWS; ) {
        F_readdir(&dp, &fp);
        if (fp.fname[0] == '\0')
            break;
        name = *fp.lfname ? fp.lfname : fp.fname;
        if (*name == '.')
            continue;
        draw_string_8x16(0, i++, name);
    }
    F_closedir(&dp);
}

int floppy_main(void)
{
    char buf[32];
    UINT i, nr;

    floppy_init("nzs_crack.adf");

    list_dir("/");
    
    F_open(&file, "small", FA_READ);
    for (;;) {
        F_read(&file, buf, sizeof(buf), &nr);
        if (nr == 0) {
            printk("\nEOF\n");
            break;
        }
        for (i = 0; i < nr; i++)
            printk("%c", buf[i]);
    }

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

        F_call_cancellable(floppy_main);
        floppy_deinit();
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

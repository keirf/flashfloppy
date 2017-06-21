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

#ifdef BUILD_TOUCH

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

static void board_init(void)
{
    uint16_t id;

    /* Test whether PC13-15 are externally pulled low. We pull each line up to
     * 3.3v via the internal weak pullup (<50k resistance). Load on each line
     * is conservatively <50pF, allowing for LSE crystal load caps. Need to
     * wait time T for input to reach 1.71v to read as HIGH.
     * T = -RCln(1-Vthresh/Vcc) = -50k*50p*ln(1-1.71/3.3) ~= 1.9us. */
    gpioc->odr = 0xffffu;
    gpioc->crh = 0x88888888u; /* PC8-15: Input with pull-up */
    delay_us(5); /* 1.9us is a tiny delay so fine to pad it some more */
    id = (gpioc->idr >> 13) & 7; /* ID should now be stable at PC[15:13] */

    /* Analog Input: disables Schmitt Trigger Inputs hence zero load for any 
     * voltage at the input pin (and voltage build-up is clamped by protection 
     * diodes even if the pin floats). 
     * NB. STMF4xx spec states that Analog Input is not safe for 5v operation. 
     * It's unclear whether this might apply to STMF1xx devices too, so for 
     * safety's sake set Analog Input only on pins not driven to 5v. */
    gpioc->crh = gpioc->crl = 0; /* PC0-15: Analog Input */

    /* Selective external pulldowns define a board identifier.
     * Check if it's one we recognise and pull down any floating pins. */
    switch (board_id = id) {
    case BRDREV_LC150: /* LC Tech */
        /* PB8/9: unused, floating. */
        gpio_configure_pin(gpiob, 8, GPI_pull_down);
        gpio_configure_pin(gpiob, 9, GPI_pull_down);
        /* PB2 = BOOT1: externally tied. */
       break;
    case BRDREV_MM150: /* Maple Mini */
        /* PB1: LED connected to GND. */
        gpio_configure_pin(gpiob, 1, GPI_pull_down);
        /* PB8 = Button, PB9 = USB DISConnect: both externally tied. */
        break;
    case BRDREV_TB160: /* "Taobao" / Blue Pill / etc. */
        /* PA13/14: SW-debug, floating. */
        gpio_configure_pin(gpioa, 13, GPI_pull_down);
        gpio_configure_pin(gpioa, 14, GPI_pull_down);
        /* PB2 = BOOT1: externally tied. */
        break;
    default:
        printk("Unknown board ID %x\n", id);
        ASSERT(0);
    }
}

#elif BUILD_GOTEK

static inline void do_tft(void) {}

static void gpio_pull_up_pins(GPIO gpio, uint16_t mask)
{
    unsigned int i;
    for (i = 0; i < 16; i++) {
        if (mask & 1)
            gpio_configure_pin(gpio, i, GPI_pull_up);
        mask >>= 1;
    }
}

static void board_init(void)
{
    board_id = BRDREV_Gotek;

    /* Pull up all currently unused and possibly-floating pins. */
    /* Skip PA0-1,8 (floppy inputs), PA9-10 (serial console). */
    gpio_pull_up_pins(gpioa, ~0x0703);
    /* Skip PB0,4,9 (floppy inputs). */
    gpio_pull_up_pins(gpiob, ~0x0211);
    /* Don't skip any PCx pins. */
    gpio_pull_up_pins(gpioc, ~0x0000);
}

#else /* !BUILD_TOUCH && !BUILD_GOTEK */

#error "Must define BUILD_GOTEK or BUILD_TOUCH"

#endif

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

    i = usart1->dr; /* clear UART_SR_RXNE */    
    for (i = 0; !(usart1->sr & USART_SR_RXNE); i++) {
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

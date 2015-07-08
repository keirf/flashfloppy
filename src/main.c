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
    uint16_t id, i;

    /* Test if PC13-15 are externally pulled low. We pull each line up to 3.3v
     * via the internal weak pullup: maximum 50k resistance. Load on each line
     * could be (conservatively) 50pF, allowing for LSE crystal load caps. Need
     * to wait time T for input to reach 1.71v to read as HIGH.
     * T = -RCln(1-Vthresh/Vcc) = -50k*50p*ln(1-1.71/3.3) ~= 1.9us. 
     * It's a negligible delay so pad it some more... */
    delay_us(5);

    /* Selective external pullups plus the weaker internal pullups define 
     * a board identifier. Check it's one we recognise. */
    switch (id = (gpioc->idr >> 13) & 7) {
    case 7: /* Rev LC150 */
        break;
    default:
        printk("Unknown board ID %x\n", id);
        ASSERT(0);
    }

    /* PB2/BOOT1 is externally pulled up or down on every board. */
    gpio_configure_pin(gpiob, 2, GPI_floating);

    /* External pulldowns don't need the internal pullup. */
    for (i = 0; i < 3; i++)
        if (!(id & (1<<i)))
            gpio_configure_pin(gpioc, i+13, GPI_floating);
}

int main(void)
{
    FRESULT fr;
    int i;

    /* Relocate DATA. Initialise BSS. */
    if (_sdat != _ldat)
        memcpy(_sdat, _ldat, _edat-_sdat);
    memset(_sbss, 0, _ebss-_sbss);

    stm32_init();

    console_init();
    delay_ms(250); /* XXX printk debug delay */

    board_init();

    backlight_init();
    tft_init();
    backlight_set(8);
    touch_init();

    floppy_init(NULL, NULL);

    f_mount(&fatfs, "", 1);
    fr = f_open(&file, "small", FA_READ);
    printk("File open %d\n", fr);
    if (fr == FR_OK) {
        char buf[32];
        UINT i, nr;
        while (f_read(&file, buf, sizeof(buf), &nr) == FR_OK) {
            if (nr == 0) {
                printk("\nEOF\n");
                break;
            }
            for (i = 0; i < nr; i++)
                printk("%c", buf[i]);
        }
    }

    i = usart1->dr; /* clear UART_SR_RXNE */    
    for (i = 0; !(usart1->sr & USART_SR_RXNE); i++) {
        do_tft();
        floppy_handle();
    }

    ASSERT(0);

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

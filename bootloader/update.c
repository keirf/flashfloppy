/*
 * update.c
 * 
 * USB-flash update bootloader for main firmware.
 * 
 * Procedure:
 *  - Press both Gotek buttons to start the update process.
 *  - Requires a USB flash drive containing exactly one update file
 *    named "FF_Gotek*.upd" (* = wildcard).
 * 
 * Status messages:
 *  uPd -> Waiting for buttons to release
 *  uSb -> Waiting for USB stack
 *   rd -> Reading the update file
 *  CrC -> CRC-checking the file
 *  CLr -> Erasing flash
 *  Prg -> Programming flash
 * 
 * Error messages:
 *  E01 -> No update file found
 *  E02 -> More than one update file found
 *  E03 -> Update file is invalid (bad signature or size)
 *  E04 -> Update file is corrupt (bad CRC)
 *  E05 -> Flash error (bad CRC on verify)
 *  Fxx -> FatFS error (probably bad filesystem)
 * 
 * Press both Gotek buttons to dismiss an error and retry the update.
 * 
 * Written & released by Keir Fraser <keir.xen@gmail.com>
 * 
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

#define FIRMWARE_START 0x08008000
#define FIRMWARE_END   0x08020000

#if BUILD_GOTEK
#define FLASH_PAGE_SIZE 2048
#elif BUILD_TOUCH
#define FLASH_PAGE_SIZE 1024
#endif

/* FPEC */
void fpec_init(void);
void fpec_page_erase(uint32_t flash_address);
void fpec_write(const void *data, unsigned int size, uint32_t flash_address);

int EXC_reset(void) __attribute__((alias("main")));

/* FatFS */
static FATFS fatfs;

/* Shared state. regarding update progress/failure. */
static bool_t old_firmware_erased;
static enum {
    FC_no_file = 1, /* no update file */
    FC_multiple_files, /* multiple update files */
    FC_bad_file, /* bad signature or size */
    FC_bad_crc, /* bad file crc */
    FC_bad_prg
} fail_code;

uint8_t board_id;

static void canary_init(void)
{
    _irq_stackbottom[0] = _thread_stackbottom[0] = 0xdeadbeef;
}

static void canary_check(void)
{
    ASSERT(_irq_stackbottom[0] == 0xdeadbeef);
    ASSERT(_thread_stackbottom[0] == 0xdeadbeef);
}

static void erase_old_firmware(void)
{
    uint32_t p;
    for (p = FIRMWARE_START; p < FIRMWARE_END; p += FLASH_PAGE_SIZE)
        fpec_page_erase(p);
}

static void msg_display(const char *p)
{
    printk("[%s]\n", p);
    switch (display_mode) {
    case DM_LED_3DIG:
        led_3dig_write(p);
        break;
    case DM_LCD_1602:
        lcd_write(6, 1, 0, p);
        lcd_sync();
        break;
    }
}

int update(void)
{
    /* FatFS state, local to this function, but off stack. */
    static FIL file;
    static DIR dp;
    static FILINFO fno;
    static char update_fname[FF_MAX_LFN+1];

    /* Our file buffer. Again, off stack. */
    static uint8_t buf[2048];

    uint32_t p;
    uint16_t footer[2], crc;
    UINT i, nr;
    FIL *fp = &file;

    /* Find the update file, confirming that it exists and there is no 
     * ambiguity (ie. we don't allow multiple update files). */
    F_findfirst(&dp, &fno, "", "ff_gotek*.upd");
    if (*fno.fname == '\0') {
        fail_code = FC_no_file;
        goto fail;
    }
    strcpy(update_fname, fno.fname);
    printk("Found update \"%s\"\n", update_fname);
    F_findnext(&dp, &fno);
    if (*fno.fname != '\0') {
        printk("** Error: found another file \"%s\"\n", fno.fname);
        fail_code = FC_multiple_files;
        goto fail;
    }
    F_closedir(&dp);

    /* Open and sanity-check the file. */
    msg_display(" RD");
    F_open(fp, update_fname, FA_READ);
    /* Check size. */
    fail_code = ((f_size(fp) < 1024)
                 || (f_size(fp) > (FIRMWARE_END-FIRMWARE_START))
                 || (f_size(fp) & 3))
        ? FC_bad_file : 0;
    printk("%u bytes: %s\n", f_size(fp), fail_code ? "BAD" : "OK");
    if (fail_code)
        goto fail;
    /* Check signature in footer. */
    F_lseek(fp, f_size(fp) - sizeof(footer));
    F_read(fp, footer, sizeof(footer), NULL);
    if (be16toh(footer[0]) != 0x4659/* "FY" */) {
        fail_code = FC_bad_file;
        goto fail;
    }

    /* Check the CRC-CCITT. */
    msg_display("CRC");
    crc = 0xffff;
    F_lseek(fp, 0);
    for (i = 0; !f_eof(fp); i++) {
        nr = min_t(UINT, sizeof(buf), f_size(fp) - f_tell(fp));
        F_read(&file, buf, nr, NULL);
        crc = crc16_ccitt(buf, nr, crc);
    }
    if (crc != 0) {
        fail_code = FC_bad_crc;
        goto fail;
    }

    /* Erase the old firmware. */
    msg_display("CLR");
    fpec_init();
    erase_old_firmware();
    old_firmware_erased = TRUE;

    /* Program the new firmware. */
    msg_display("PRG");
    crc = 0xffff;
    F_lseek(fp, 0);
    p = FIRMWARE_START;
    for (i = 0; !f_eof(fp); i++) {
        nr = min_t(UINT, sizeof(buf), f_size(fp) - f_tell(fp));
        F_read(&file, buf, nr, NULL);
        fpec_write(buf, nr, p);
        if (memcmp((void *)p, buf, nr) != 0) {
            /* Byte-by-byte verify failed. */
            fail_code = FC_bad_prg;
            goto fail;
        }
        p += nr;
    }

    /* Verify the new firmware (CRC-CCITT). */
    p = FIRMWARE_START;
    crc = crc16_ccitt((void *)p, f_size(fp), 0xffff);
    if (crc) {
        /* CRC verify failed. */
        fail_code = FC_bad_prg;
        goto fail;
    }

    /* All done! */
    fail_code = 0;

fail:
    canary_check();
    return 0;
}

static void display_setting(bool_t on)
{
    switch (display_mode) {
    case DM_LED_3DIG:
        led_3dig_display_setting(on);
        break;
    case DM_LCD_1602:
        lcd_backlight(on);
        lcd_sync();
        break;
    }
}

/* Wait for both buttons to be pressed (LOW) or not pressed (HIGH). Perform 
 * debouncing by sampling the buttons every 5ms and checking for same state 
 * over 16 consecutive samples. */
static void wait_buttons(uint8_t level)
{
    uint16_t x = 0;

    do {
        delay_ms(5);
        x <<= 1;
        x |= ((gpio_read_pin(gpioc, 8) == level) &&
              (gpio_read_pin(gpioc, 7) == level));
    } while (x != 0xffff);
}

int main(void)
{
    char msg[4];
    FRESULT fres;

    /* Relocate DATA. Initialise BSS. */
    if (_sdat != _ldat)
        memcpy(_sdat, _ldat, _edat-_sdat);
    memset(_sbss, 0, _ebss-_sbss);

    /* Enable GPIOC, set all pins as input with weak pull-up. */
    rcc->apb2enr = RCC_APB2ENR_IOPCEN;
    gpioc->odr = 0xffffu;
    gpioc->crh = 0x88888888u;
    gpioc->crl = 0x88888888u;

    /* Check the two Gotek buttons. Only when both are pressed do we enter
     * update mode. */
    if (gpio_read_pin(gpioc, 8) || gpio_read_pin(gpioc, 7)) {
        /* Nope, so jump straight at the main firmware. */
        uint32_t sp = *(uint32_t *)FIRMWARE_START;
        uint32_t pc = *(uint32_t *)(FIRMWARE_START + 4);
        if (sp != ~0u) { /* only if firmware is apparently not erased */
            asm volatile (
                "mov sp,%0 ; blx %1"
                :: "r" (sp), "r" (pc));
        }
    }

    /*
     * UPDATE MODE
     */

    /* Initialise the world. */
    canary_init();
    stm32_init();
    timers_init();
    console_init();
    delay_ms(200); /* 5v settle */
    board_init();

    printk("\n** FF Update Bootloader v%s for Gotek\n", FW_VER);
    printk("** Keir Fraser <keir.xen@gmail.com>\n");
    printk("** https://github.com/keirf/FlashFloppy\n\n");

    display_init();
    switch (display_mode) {
    case DM_LED_3DIG:
        msg_display("UPD");
        break;
    case DM_LCD_1602:
        lcd_write(0, 0, 0, "FF Update Flash");
        lcd_write(5, 1, 0, "[---]");
        lcd_sync();
        break;
    }

    usbh_msc_init();

    /* Wait for buttons to be pressed. */
    wait_buttons(LOW);

    /* Wait for buttons to be released. */
    wait_buttons(HIGH);

    /* Wait for a filesystem. */
    msg_display("USB");
    while (f_mount(&fatfs, "", 1) != FR_OK) {
        usbh_msc_process();
        canary_check();
    }

    /* Do the update. */
    fres = F_call_cancellable(update);

    if (fres || fail_code) {

        /* An error occurred. Report it on the display. */
        if (fres)
            snprintf(msg, sizeof(msg), "F%02u", fres);
        else
            snprintf(msg, sizeof(msg), "E%02u", fail_code);
        msg_display(msg);

        /* If we had modified flash, fully erase the main firmware area. */
        if (old_firmware_erased)
            erase_old_firmware();

        /* Wait for buttons to be pressed, so user sees error message. */
        wait_buttons(LOW);

    } else {

        /* No errors. */
        printk("Success!\n");

    }

    /* Clear the display. */
    if (display_mode == DM_LCD_1602)
        lcd_clear();
    display_setting(FALSE);

    /* All done. Reset. */
    system_reset();

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

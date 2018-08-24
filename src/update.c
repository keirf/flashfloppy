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

#ifndef RELOADER
/* Main bootloader: flashes the main firmware (last 96kB or 224kB of Flash). */
#define FIRMWARE_START 0x08008000
#define FIRMWARE_END   (0x08000000 +FLASH_MEM_SIZE -FLASH_PAGE_SIZE)
#define FILE_PATTERN   "ff_gotek*.upd"
#define is_reloader    FALSE
#else
/* "Reloader": reflashes the main bootloader (first 32kB). */
#define FIRMWARE_START 0x08000000
#define FIRMWARE_END   0x08008000
#define FILE_PATTERN   "ff_gotek*.rld"
#define is_reloader    TRUE
#endif

int EXC_reset(void) __attribute__((alias("main")));

static uint8_t USBH_Cfg_Rx_Buffer[512];

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
    case DM_LED_7SEG:
        led_7seg_write_string(p);
        break;
    case DM_LCD_1602:
        lcd_write(6, 1, 0, p);
        lcd_sync();
        break;
    }
}

int update(void *unused)
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
    F_findfirst(&dp, &fno, "", FILE_PATTERN);
    if (*fno.fname == '\0') {
        fail_code = FC_no_file;
        goto fail;
    }
    snprintf(update_fname, sizeof(update_fname), "%s", fno.fname);
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
    case DM_LED_7SEG:
        led_7seg_display_setting(on);
        break;
    case DM_LCD_1602:
        lcd_backlight(on);
        lcd_sync();
        break;
    }
}

static bool_t buttons_pressed(void)
{
    /* Check for both LEFT and RIGHT buttons pressed. */
    return ((!gpio_read_pin(gpioc, 8) && !gpio_read_pin(gpioc, 7))
            /* Also respond to third (SELECT) button on its own. */
            || !gpio_read_pin(gpioc, 6));
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
        if (level) {
            /* All buttons must be released. */
            x |= gpio_read_pin(gpioc, 8)
                && gpio_read_pin(gpioc, 7)
                && gpio_read_pin(gpioc, 6);
        } else {
            x |= buttons_pressed();
        }
    } while (x != 0xffff);
}

int main(void)
{
    char msg[20];
    FRESULT fres;

    /* Relocate DATA. Initialise BSS. */
    if (_sdat != _ldat)
        memcpy(_sdat, _ldat, _edat-_sdat);
    memset(_sbss, 0, _ebss-_sbss);

#ifndef RELOADER
    /* Enable GPIOC, set all pins as input with weak pull-up. */
    rcc->apb2enr = RCC_APB2ENR_IOPCEN;
    gpioc->odr = 0xffffu;
    gpioc->crh = 0x88888888u;
    gpioc->crl = 0x88888888u;

    /* Enter update mode only if buttons are pressed. */
    if (!buttons_pressed()) {
        /* Nope, so jump straight at the main firmware. */
        uint32_t sp = *(uint32_t *)FIRMWARE_START;
        uint32_t pc = *(uint32_t *)(FIRMWARE_START + 4);
        if (sp != ~0u) { /* only if firmware is apparently not erased */
            asm volatile (
                "mov sp,%0 ; blx %1"
                :: "r" (sp), "r" (pc));
        }
    }
#endif

    /*
     * UPDATE MODE
     */

    /* Initialise the world. */
    canary_init();
    stm32_init();
    time_init();
    console_init();
    board_init();
    delay_ms(200); /* 5v settle */

    printk("\n** FF %s v%s for Gotek\n",
           is_reloader ? "Reloader" : "Update Bootloader",
           FW_VER);
    printk("** Keir Fraser <keir.xen@gmail.com>\n");
    printk("** https://github.com/keirf/FlashFloppy\n\n");

    flash_ff_cfg_read();

    display_init();
    switch (display_mode) {
    case DM_LED_7SEG:
        msg_display(is_reloader ? "RLD" : "UPD");
        break;
    case DM_LCD_1602:
        snprintf(msg, sizeof(msg), "FF %s",
                 is_reloader ? "Reloader" : "Update Flash");
        lcd_write(0, 0, 0, msg);
        lcd_write(0, 1, 0, "v");
        lcd_write(1, 1, 0, FW_VER);
        lcd_sync();
        break;
    }

    display_setting(TRUE);

    usbh_msc_init();
    usbh_msc_buffer_set(USBH_Cfg_Rx_Buffer);

    /* Wait for buttons to be pressed. */
    wait_buttons(LOW);

    /* Wait for buttons to be released. */
    wait_buttons(HIGH);

    if (display_mode == DM_LCD_1602)
        lcd_write(0, 1, -1, "     [   ]");

    /* Wait for a filesystem. */
    msg_display("USB");
    while (f_mount(&fatfs, "", 1) != FR_OK) {
        usbh_msc_process();
        canary_check();
    }

    /* Do the update. */
    fres = F_call_cancellable(update, NULL);

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

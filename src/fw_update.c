/*
 * fw_update.c
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

/* Main bootloader: flashes the main firmware. */
#if MCU == STM32F105
#define FIRMWARE_START 0x08008000
#define FIRMWARE_END   (0x08020000 - FLASH_PAGE_SIZE)
#elif MCU == AT32F435
#define FIRMWARE_START 0x0800c000
#define FIRMWARE_END   (0x08040000 - FLASH_PAGE_SIZE)
#endif
#define FILE_PATTERN   "ff_gotek*.upd"

int EXC_reset(void) __attribute__((alias("main")));

static uint8_t USBH_Cfg_Rx_Buffer[512];

/* File buffer. */
static uint8_t buf[2048];

/* FatFS */
static FATFS fatfs;

/* Shared state. regarding update progress/failure. */
static bool_t old_firmware_erased;
static enum fail_code {
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

#define MAIN_FW_KEY 0x39b5ba2c
static void reset_to_main_fw(void) __attribute__((noreturn));
static void reset_to_main_fw(void)
{
    *(volatile uint32_t *)_ebss = MAIN_FW_KEY;
    cpu_sync();
    system_reset();
}

static bool_t main_fw_requested(void)
{
    bool_t req = (*(volatile uint32_t *)_ebss == MAIN_FW_KEY);
    *(volatile uint32_t *)_ebss = 0;
    return req;
}

static bool_t fw_update_requested(void)
{
    bool_t requested;

#if MCU == STM32F105

    /* Power up the backup-register interface and allow writes. */
    rcc->apb1enr |= RCC_APB1ENR_PWREN | RCC_APB1ENR_BKPEN;
    pwr->cr |= PWR_CR_DBP;

    /* Has bootloader been requested via magic numbers in the backup regs? */
    requested = ((bkp->dr1[0] == 0xdead) && (bkp->dr1[1] == 0xbeef));

    /* Clean up backup registers and peripheral clocks. */
    bkp->dr1[0] = bkp->dr1[1] = 0;
    rcc->apb1enr = 0;

#elif MCU == AT32F435

    /* Check-and-clear a magic value poked into SRAM1 by the main firmware. */
    requested = (_reset_flag == RESET_FLAG_BOOTLOADER);
    _reset_flag = 0;

#endif

    return requested;
}

static void erase_old_firmware(void)
{
    uint32_t p;
    for (p = FIRMWARE_START; p < FIRMWARE_END; p += flash_page_size)
        fpec_page_erase(p);
}

static void msg_display(const char *p)
{
    printk("[%s]\n", p);
    switch (display_type) {
    case DT_LED_7SEG:
        led_7seg_write_string(p);
        break;
    case DT_LCD_OLED:
        lcd_write(6, 1, 0, p);
        lcd_sync();
        break;
    }
}

static enum fail_code find_update_file(
    char *file_name, size_t file_name_size, const char *file_pattern)
{
    static DIR dp;
    static FILINFO fno;
    /* Find the update file, confirming that it exists and there is no 
     * ambiguity (ie. we don't allow multiple update files). */
    F_findfirst(&dp, &fno, "", file_pattern);
    if (*fno.fname == '\0') {
        return FC_no_file;
    }
    snprintf(file_name, file_name_size, "%s", fno.fname);
    printk("Found update \"%s\"\n", file_name);
    F_findnext(&dp, &fno);
    if (*fno.fname != '\0') {
        printk("** Error: found another file \"%s\"\n", fno.fname);
        return FC_multiple_files;
    }
    F_closedir(&dp);
    return 0;
}

static uint16_t file_crc(FIL *fp, UINT off, UINT sz)
{
    uint16_t crc;
    UINT todo, nr;

    crc = 0xffff;
    F_lseek(fp, off);
    for (todo = sz; todo != 0; todo -= nr) {
        nr = min_t(UINT, sizeof(buf), todo);
        F_read(fp, buf, nr, NULL);
        crc = crc16_ccitt(buf, nr, crc);
    }

    return crc;
}

static enum fail_code erase_and_program(FIL *fp, UINT off, UINT sz)
{
    uint32_t p;
    uint16_t footer[2];
    UINT todo, nr;

    /* Check size. */
    fail_code = ((sz < 1024)
                 || (sz > (FIRMWARE_END-FIRMWARE_START))
                 || (sz & 3))
        ? FC_bad_file : 0;
    printk("%u bytes: %s\n", sz, fail_code ? "BAD" : "OK");
    if (fail_code)
        return fail_code;

    /* Check signature in footer. */
    F_lseek(fp, off + sz - sizeof(footer));
    F_read(fp, footer, sizeof(footer), NULL);
    if (be16toh(footer[0]) != 0x4659/* "FY" */)
        return FC_bad_file;

    /* Check the CRC-CCITT. */
    msg_display("CRC");
    if (file_crc(fp, off, sz) != 0)
        return FC_bad_crc;

    /* Erase the old firmware. */
    msg_display("CLR");
    fpec_init();
    erase_old_firmware();
    old_firmware_erased = TRUE;

    /* Program the new firmware. */
    msg_display("PRG");
    F_lseek(fp, off);
    p = FIRMWARE_START;
    for (todo = sz; todo != 0; todo -= nr) {
        nr = min_t(UINT, sizeof(buf), todo);
        F_read(fp, buf, nr, NULL);
        fpec_write(buf, nr, p);
        if (memcmp((void *)p, buf, nr) != 0) {
            /* Byte-by-byte verify failed. */
            return FC_bad_prg;
        }
        p += nr;
    }

    /* Verify the new firmware (CRC-CCITT). */
    if (crc16_ccitt((void *)FIRMWARE_START, sz, 0xffff) != 0) {
        /* CRC verify failed. */
        return FC_bad_prg;
    }

    return 0;
}

static enum fail_code find_new_update_entry(FIL *fp, UINT *p_off, UINT *p_sz)
{
    struct {
        char sig[4];
        uint32_t off;
        uint32_t nr;
    } header;

    struct {
        uint8_t model;
        uint8_t pad[3];
        uint32_t off;
        uint32_t len;
    } entry;

    uint32_t i;

    F_lseek(fp, 0);
    F_read(fp, &header, sizeof(header), NULL);
    if (strncmp(header.sig, "FFUP", 4))
        return FC_bad_file;

    if (file_crc(fp, 0, header.off + header.nr*12 + 4) != 0)
        return FC_bad_crc;

    F_lseek(fp, header.off);
    for (i = 0; i < header.nr; i++) {
        F_read(fp, &entry, sizeof(entry), NULL);
        if (entry.model == MCU) {
            *p_off = entry.off;
            *p_sz = entry.len;
            return 0;
        }
    }

    return FC_bad_file;
}

static enum fail_code find_update_entry(FIL *fp, UINT *p_off, UINT *p_sz)
{
    uint16_t footer[2];

    *p_off = *p_sz = 0;

    F_lseek(fp, f_size(fp) - sizeof(footer));
    F_read(fp, footer, sizeof(footer), NULL);
    switch (be16toh(footer[0])) {
#if MCU == STM32F105
    case 0x4659/* "FY" */:
        *p_off = 0;
        *p_sz = f_size(fp);
        return 0;
#endif
    case 0x4646/* "FF" */:
        return find_new_update_entry(fp, p_off, p_sz);
    }

    return FC_bad_file;
}

int update(void *unused)
{
    /* FatFS state, local to this function, but off stack. */
    static FIL file;
    static char update_fname[FF_MAX_LFN+1];

    FIL *fp = &file;
    UINT off, sz;

    fail_code = find_update_file(update_fname, sizeof(update_fname),
                                 "flashfloppy-*.upd");
#if MCU == STM32F105
    if (fail_code == FC_no_file)
        fail_code = find_update_file(update_fname, sizeof(update_fname),
                                     "ff_gotek*.upd");
#endif
    if (fail_code)
        goto fail;

    /* Open and sanity-check the file. */
    msg_display(" RD");
    F_open(fp, update_fname, FA_READ);

    fail_code = find_update_entry(fp, &off, &sz);
    if (fail_code)
        goto fail;

    fail_code = erase_and_program(fp, off, sz);

fail:
    canary_check();
    return 0;
}

static void display_setting(bool_t on)
{
    switch (display_type) {
    case DT_LED_7SEG:
        led_7seg_display_setting(on);
        break;
    case DT_LCD_OLED:
        lcd_backlight(on);
        lcd_sync();
        break;
    }
}

static bool_t buttons_pressed(void)
{
    unsigned int b = board_get_buttons() | osd_buttons_rx;
    return (
        /* Check for both LEFT and RIGHT buttons pressed. */
        ((b & (B_LEFT|B_RIGHT)) == (B_LEFT|B_RIGHT))
        /* Also respond to third (SELECT) button on its own. */
        || (b & B_SELECT));
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
            unsigned int b = board_get_buttons() | osd_buttons_rx;
            x |= !b;
        } else {
            x |= buttons_pressed();
        }
    } while (x != 0xffff);
}

int main(void)
{
    char msg[20];
    FRESULT fres;
    bool_t update_requested;

    /* Relocate DATA. Initialise BSS. */
    if (_sdat != _ldat)
        memcpy(_sdat, _ldat, _edat-_sdat);
    memset(_sbss, 0, _ebss-_sbss);

    update_requested = fw_update_requested();

    if (main_fw_requested() && !update_requested) {
        /* Check for, and jump to, the main firmware. */
        uint32_t sp = *(uint32_t *)FIRMWARE_START;
        uint32_t pc = *(uint32_t *)(FIRMWARE_START + 4);
        if (sp != ~0u) { /* only if firmware is apparently not erased */
            asm volatile (
                "mov sp,%0 ; blx %1"
                :: "r" (sp), "r" (pc));
        }
        update_requested = TRUE;
    }

    /*
     * UPDATE MODE
     */

    /* Initialise the world. */
    canary_init();
    stm32_init();
    time_init();
    console_init();
    board_init();

    printk("\n** FF Update Bootloader %s\n", fw_ver);
    printk("** Keir Fraser <keir.xen@gmail.com>\n");
    printk("** github:keirf/flashfloppy\n\n");

    if (!update_requested && !buttons_pressed())
        reset_to_main_fw();

    delay_ms(200); /* 5v settle */

    flash_ff_cfg_read();

    display_init();
    switch (display_type) {
    case DT_LED_7SEG:
        msg_display("UPD");
        break;
    case DT_LCD_OLED:
        snprintf(msg, sizeof(msg), "FF Update Flash");
        lcd_write(0, 0, 0, msg);
        lcd_write(0, 1, 0, fw_ver);
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

    if (display_type == DT_LCD_OLED)
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

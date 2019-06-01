/*
 * bl_update.c
 * 
 * Main firmware containing a payload of an updated bootloader.
 * 
 * Procedure:
 *  - Place this *.UPD file on your USB stick and follow usual update process.
 * 
 * Status messages:
 *  CLr -> Erasing flash
 *  Prg -> Programming flash
 * 
 * Error messages:
 *  E05 -> Flash error (bad CRC on verify)
 * 
 * Written & released by Keir Fraser <keir.xen@gmail.com>
 * 
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

/* Reflash the main bootloader (first 32kB). */
#define FIRMWARE_START 0x08000000
#define FIRMWARE_END   0x08008000

/* The update payload. */
extern char update_start[], update_end[];
asm (
"    .section .rodata\n"
"    .align 4\n"
"    .global update_start, update_end\n"
"update_start:\n"
"    .incbin \"../bootloader/Bootloader.bin\"\n"
"update_end:\n"
"    .previous\n"
    );

/* Only the vector table in low 2kB, as we erase first page of firmware, 
 * and we mustn't erase the code we're executing. */
asm (
"    .section .vector_table.padding\n"
"    .balign "STR(FLASH_PAGE_SIZE)"\n"
"    .previous\n"
    );

int EXC_reset(void) __attribute__((alias("main")));

uint8_t board_id;

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
    case DM_LCD_OLED:
        lcd_write(6, 1, 0, p);
        lcd_sync();
        break;
    }
}

static void display_setting(bool_t on)
{
    switch (display_mode) {
    case DM_LED_7SEG:
        led_7seg_display_setting(on);
        break;
    case DM_LCD_OLED:
        lcd_backlight(on);
        lcd_sync();
        break;
    }
}

int main(void)
{
    void *buf = update_start;
    unsigned int nr = update_end - update_start;
    int retries = 0;

    /* Relocate DATA. Initialise BSS. */
    if (_sdat != _ldat)
        memcpy(_sdat, _ldat, _edat-_sdat);
    memset(_sbss, 0, _ebss-_sbss);

    /* Initialise the world. */
    stm32_init();
    time_init();
    console_init();
    board_init();
    delay_ms(200); /* 5v settle */

    printk("\n** FF Update Firmware for Gotek\n", fw_ver);
    printk("** Keir Fraser <keir.xen@gmail.com>\n");
    printk("** https://github.com/keirf/FlashFloppy\n\n");

    flash_ff_cfg_read();

    display_init();
    switch (display_mode) {
    case DM_LED_7SEG:
        msg_display("BLD");
        break;
    case DM_LCD_OLED:
        lcd_write(0, 0, 0, "New Bootloader..");
        lcd_write(0, 1, 0, "     [   ]");
        lcd_sync();
        break;
    }

    display_setting(TRUE);

    for (retries = 0; retries < 5; retries++) {

        /* Erase the old firmware. */
        msg_display("CLR");
        fpec_init();
        erase_old_firmware();

        /* Program the new firmware. */
        msg_display("PRG");
        fpec_write(buf, nr, FIRMWARE_START);

        if (!memcmp((void *)FIRMWARE_START, buf, nr))
            goto success;

    }

    /* An error occurred. Report it on the display. */
    msg_display("ERR");

    /* Erase the bootloader. It's now damaged. */
    erase_old_firmware();

    /* Spin forever. We're toast. */
    for (;;)
        continue;

success:
    /* No errors. */
    printk("Success!\n");

    /* Clear the display. */
    display_setting(FALSE);

    /* All done. Erase ourself and reset. */
    IRQ_global_disable();
    fpec_page_erase((uint32_t)_stext);
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

/*
 * update.c
 * 
 * USB-flash update bootloader for main firmware.
 * 
 * Status messages:
 *  uPd -> Waiting for buttons to release
 *  uSb -> Waiting for USB stack
 *   Rd -> Reading the update file
 *  CrC -> CRC-checking the file
 *  ErA -> Erasing flash
 *  Prg -> Programming flash
 * 
 * Error messages:
 *  E01 -> No update file found
 *  E02 -> Update file is invalid (bad signature or size)
 *  E03 -> Update file is corrupt (bad CRC)
 *  E04 -> Flash error (bad CRC on verify)
 *  Fxx -> FatFS error (probably bad filesystem)
 * 
 * Written & released by Keir Fraser <keir.xen@gmail.com>
 * 
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

#define FIRMWARE_START 0x08008000
#define FIRMWARE_END   0x08040000

#if BUILD_GOTEK
#define FLASH_PAGE_SIZE 2048
#elif BUILD_TOUCH
#define FLASH_PAGE_SIZE 1024
#endif

/* CRC-CCITT */
void crc16_gentable(void);
uint16_t crc16_ccitt(const void *buf, size_t len, uint16_t crc);

/* FPEC */
void fpec_init(void);
void fpec_page_erase(uint32_t flash_address);
void fpec_write(const void *data, unsigned int size, uint32_t flash_address);

int EXC_reset(void) __attribute__((alias("main")));

/* FatFS */
static FATFS fatfs;
static FIL file;
static DIR dp;
static FILINFO fno;
static char lfn[_MAX_LFN+1];

/* Update state and buffers. */
static uint8_t buf[2048];
static bool_t old_firmware_erased;
static enum { FC_no_file = 1, FC_bad_file, FC_bad_crc, FC_bad_prg } fail_code;

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

int update(void)
{
    uint32_t p;
    uint16_t footer[2], crc;
    UINT i, nr;
    char *name;
    FIL *fp = &file;

    fno.lfname = lfn;
    fno.lfsize = sizeof(lfn);

    fail_code = FC_no_file;
    F_findfirst(&dp, &fno, "", "ff_gotek*.upd");
    name = *fno.lfname ? fno.lfname : fno.fname;
    if (!*name)
        goto fail;
    F_closedir(&dp);

    /* Open and sanity-check the file. */
    fail_code = FC_bad_file;
    led_7seg_write(" RD");
    F_open(fp, name, FA_READ);
    /* Check size. */
    if ((f_size(fp) < 1024) /* too small */
        || (f_size(fp) > (FIRMWARE_END-FIRMWARE_START)) /* too large */
        || (f_size(fp) & 1)) /* odd size */
        goto fail;
    F_lseek(fp, f_size(fp) - sizeof(footer));
    F_read(fp, footer, sizeof(footer), NULL);
    /* Check signature mark. */
    if (be16toh(footer[0]) != 0x4659) /* "FY" */
        goto fail;

    /* Check the CRC-CCITT. */
    fail_code = FC_bad_crc;
    led_7seg_write("CRC");
    crc = 0xffff;
    F_lseek(fp, 0);
    for (i = 0; !f_eof(fp); i++) {
        nr = min_t(UINT, sizeof(buf), f_size(fp) - f_tell(fp));
        F_read(&file, buf, nr, NULL);
        crc = crc16_ccitt(buf, nr, crc);
    }
    if (crc != 0)
        goto fail;

    /* Erase the old firmware. */
    led_7seg_write("ERA");
    fpec_init();
    erase_old_firmware();
    old_firmware_erased = TRUE;

    /* Program the new firmware. */
    led_7seg_write("PRG");
    crc = 0xffff;
    F_lseek(fp, 0);
    p = FIRMWARE_START;
    for (i = 0; !f_eof(fp); i++) {
        nr = min_t(UINT, sizeof(buf), f_size(fp) - f_tell(fp));
        F_read(&file, buf, nr, NULL);
        fpec_write(buf, nr, p);
        p += nr;
    }

    /* Verify the new firmware (CRC-CCITT). */
    fail_code = FC_bad_prg;
    p = FIRMWARE_START;
    crc = crc16_ccitt((void *)p, f_size(fp), 0xffff);
    if (crc)
        goto fail;

    /* All done! */
    fail_code = 0;

fail:
    canary_check();
    return 0;
}

/* Wait for both buttons to be pressed (LOW) or not pressed (HIGH). Perform 
 * debouncing by sampling the buttons every 10ms and checking for same state 
 * over 16 consecutive samples. */
static void wait_buttons(uint8_t level)
{
    uint16_t x = 0;

    do {
        delay_ms(10);
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
    board_init();
    backlight_init();
    tft_init();
    backlight_set(8);
    touch_init();
    led_7seg_init();
    usbh_msc_init();
    crc16_gentable();

    led_7seg_write("UPD");

    /* Wait for buttons to be released. */
    wait_buttons(HIGH);

    /* Wait for a filesystem. */
    led_7seg_write("USB");
    while (f_mount(&fatfs, "", 1) != FR_OK) {
        usbh_msc_process();
        canary_check();
    }

    /* Do the update. */
    fres = F_call_cancellable(update);

    /* Check for errors and report them on display. */
    if (fres) {
        snprintf(msg, sizeof(msg), "F%02u", fres);
        led_7seg_write(msg);
    } else if (fail_code) {
        snprintf(msg, sizeof(msg), "E%02u", fail_code);
        led_7seg_write(msg);
    } else {
        /* All done. Reset. */
        ASSERT(!fail_code);
        led_7seg_write("   ");
        system_reset();
    }

    /* If we had modified flash, fully erase the main firmware area. */
    if (old_firmware_erased)
        erase_old_firmware();

    /* Wait for buttons to be pressed, so user sees error message. */
    wait_buttons(LOW);

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

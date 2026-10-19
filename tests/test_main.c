/*
 * test_main.c
 *
 * main.c on the drive in RAM, as when the drive is inserted: the options
 * that it reads from FF.CFG.
 *
 * Written & released by Mike Shevchenko <mike.shevchenko.kz@gmail.com>
 *
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

#include "harness.h"

/* main.c itself, for its state and its static functions. */
#include "../src/main.c"

/* What main.c needs of the MCU, the board, the display and the USB stack,
 * which no test here reaches. */
volatile uint32_t _reset_flag;
uint32_t _thread_stackbottom[1], _irq_stackbottom[1];
char _sdat[1], _edat[1], _ldat[1], _sbss[1], _ebss[1];
const char build_date[] = "", build_time[] = "";
bool_t is_artery_mcu;
struct rcc emu_rcc;
struct bkp emu_bkp;
struct pwr emu_pwr;
uint8_t display_mode, lcd_columns;
bool_t has_osd;
uint8_t osd_buttons_tx, osd_buttons_rx;
void stm32_init(void) {}
void time_init(void) {}
void console_init(void) {}
void console_crash_on_input(void) {}
void board_init(void) {}
void board_setup_rotary_exti(void) {}
unsigned int board_get_buttons(void) { return 0; }
unsigned int board_get_rotary(void) { return 0; }
void delay_ms(unsigned int ms) {}
void display_init(void) {}
void lcd_clear(void) {}
void lcd_backlight(bool_t on) {}
void led_7seg_write_decimal(unsigned int val) {}
void speaker_init(void) {}
void speaker_notify_insert(unsigned int slotnr) {}
void speaker_notify_eject(void) {}
void usbh_msc_init(void) {}
void usbh_msc_buffer_set(uint8_t *buf) {}
void usbh_msc_process(void) {}
const void *flash_ff_cfg_area(void) { return NULL; }
void flash_ff_cfg_read(void) {}
void flash_ff_cfg_update(void *scratch) {}
void flash_ff_cfg_erase(void) {}
void system_reset(void)
{
    ft_abort(__FILE__, __LINE__, "system_reset()");
}

/* The folders of the drive, made in this order: with no image on the drive,
 * the slots that main.c needs. */
static const char * const dirs[] = { "GAMES", "GAMES/SUB", "TOOLS" };

/* As floppy_main() starts, in the cancellable call that the F_*() functions
 * need. */
static int insert_fn(void *unused)
{
    floppy_arena_setup();
    cfg_init();
    cfg_update(CFG_READ_SLOT_NR);
    return 0;
}

/* Inserts the drive with FF.CFG of @ff_cfg_text and the folders: main.c's
 * volume mounted, its configuration read and its first slot taken, as at an
 * insertion with an LCD or OLED display. */
static void insert(const char *ff_cfg_text)
{
    FRESULT fr;
    unsigned int i;

    drive_reset();
    drive_add("FF.CFG", ff_cfg_text, strlen(ff_cfg_text));
    REQUIRE(f_mount(&fatfs, "", 1) == FR_OK);
    for (i = 0; i < ARRAY_SIZE(dirs); i++)
        REQUIRE(f_mkdir(dirs[i]) == FR_OK);
    display_type = DT_LCD_OLED;
    cfg.sorted = NULL;
    fr = F_call_cancellable(insert_fn, NULL);
    if (fr != FR_OK)
        ft_abort(__FILE__, __LINE__, "insertion gave FatFS error %d", fr);
}

TEST(ff_cfg_yes_no)
{
    /* Each the other way from its default; only "yes" is yes. */
    insert("write-protect = yes\r\n"
           "index-suppression = no\r\n"
           "ejected-on-startup = yes\r\n"
           "nav-loop = on\r\n"
           "extend-image = no\r\n"
           "show-filename-ext = yes\r\n");
    CHECK_EQ(ff_cfg.write_protect, TRUE);
    CHECK_EQ(ff_cfg.index_suppression, FALSE);
    CHECK_EQ(ff_cfg.ejected_on_startup, TRUE);
    CHECK_EQ(ff_cfg.nav_loop, FALSE);
    CHECK_EQ(ff_cfg.extend_image, FALSE);
    CHECK_EQ(ff_cfg.show_filename_ext, SHOWEXT_yes);

    insert("show-filename-ext = auto\r\n");
    CHECK_EQ(ff_cfg.show_filename_ext, SHOWEXT_auto);
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

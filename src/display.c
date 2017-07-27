/*
 * display.c
 * 
 * Autodetect and set up the display peripheral.
 * 
 * Written & released by Keir Fraser <keir.xen@gmail.com>
 * 
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

uint8_t display_mode;

static const char * const dm_name[] = {
    "None",
    "1602 LCD",
    "3-digit LED"
};

void display_init(void)
{
    display_mode = (lcd_init() ? DM_LCD_1602
                    : led_3dig_init() ? DM_LED_3DIG
                    : DM_NONE);
    printk("** Display: %s\n\n", dm_name[display_mode]);
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

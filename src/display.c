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

void display_init(void)
{
    char name[20];

    if (lcd_init()) {
        display_mode = DM_LCD_1602;
        snprintf(name, sizeof(name), "1602 LCD");
    } else {
        led_7seg_init();
        display_mode = DM_LED_7SEG;
        snprintf(name, sizeof(name), "%u-Digit 7-Seg LED");
    }
    printk("** Display: %s\n\n", name);
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

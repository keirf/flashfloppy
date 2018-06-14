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
    int probe_ms = ff_cfg.display_probe_ms;

    display_mode = DM_NONE;
    snprintf(name, sizeof(name), "None");

    for (;;) {

        stk_time_t t = stk_now();

        if (lcd_init()) {
            display_mode = DM_LCD_1602;
            snprintf(name, sizeof(name), "1602 LCD");
            break; /* positive identification */
        }

        if (ff_cfg.display_type == DISPLAY_auto) {
            led_7seg_init();
            display_mode = DM_LED_7SEG;
            snprintf(name, sizeof(name), "%u-Digit 7-Seg LED",
                     led_7seg_nr_digits());
            if (led_7seg_nr_digits() == 3)
                break; /* positive identification */
        }

        if (probe_ms <= 0)
            break; /* probe timeout */

        /* Wait 100ms between probes. */
        delay_ms(100);
        probe_ms -= stk_diff(t, stk_now()) / stk_ms(1);

    }

    printk("Display: %s\n\n", name);
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

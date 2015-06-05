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
void ili9341_init(void);

int main(void)
{
    FRESULT fr;
    int i;

    /* Relocate DATA. Initialise BSS. */
    if (_sdat != _ldat)
        memcpy(_sdat, _ldat, _edat-_sdat);
    memset(_sbss, 0, _ebss-_sbss);

    /* STM core bringup. */
    exception_init();
    clock_init();
    console_init();
    /*leds_init();*/
    /*usb_init();*/

    delay_ms(500); /* XXX */
    printk("Pre\n");
    ili9341_init();
    printk("Post\n");
    f_mount(&fatfs, "", 0);
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

    gpio_configure_pin(gpioa, 0, GPO_opendrain(_2MHz,LOW));

    i = usart1->dr; /* clear UART_SR_RXNE */    
    for (i = 0; !(usart1->sr & USART_SR_RXNE); i++) {
        /*leds_write_hex(i);*/
        printk("%04x ", i);
        if ((i & 7) == 7) printk("\n");
        gpio_write_pin(gpioa, 0, i&1);
        delay_ms(80);
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

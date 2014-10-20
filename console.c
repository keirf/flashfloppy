/*
 * console.c
 * 
 * printf-style interface to USART1.
 * 
 * Written & released by Keir Fraser <keir.xen@gmail.com>
 * 
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

#include "stm32f10x.h"
#include "intrinsics.h"
#include "util.h"

int vprintk(const char *format, va_list ap)
{
    static char str[128];
    char *p;
    int n;

    irq_disable();

    n = vsnprintf(str, sizeof(str), format, ap);

    for (p = str; *p; p++) {
        while (!(usart1->sr & (1u<<7)/* txe */))
            cpu_relax();
        usart1->dr = *p;
    }

    irq_enable();

    return n;
}

int printk(const char *format, ...)
{
    va_list ap;
    int n;

    va_start(ap, format);
    n = vprintk(format, ap);
    va_end(ap);

    return n;
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

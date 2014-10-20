
#include <stdarg.h>
#include <stdint.h>
#include <stddef.h>

#define cpu_relax() asm volatile("nop" ::: "memory")
#define irq_disable() asm volatile("cpsid i" ::: "memory")
#define irq_enable() asm volatile("cpsie i" ::: "memory")

/* Reset and clock control */
struct rcc {
    uint32_t cr;       /* Clock control */
    uint32_t cfgr;     /* Clock configuration */
    uint32_t cir;      /* Clock interrupt */
    uint32_t apb2rstr; /* APB2 peripheral reset */
    uint32_t apb1rstr; /* APB1 peripheral reset */
    uint32_t ahbenr;   /* AHB periphernal clock enable */
    uint32_t apb2enr;  /* APB2 peripheral clock enable */
    uint32_t apb1enr;  /* APB1 peripheral clock enable */
    uint32_t bdcr;     /* Backup domain control */
    uint32_t csr;      /* Control/status */
    uint32_t ahbstr;   /* AHB peripheral clock reset */
    uint32_t cfgr2;    /* Clock configuration 2 */
};

#define RCC_BASE 0x40021000

volatile struct rcc * const rcc = (struct rcc *)RCC_BASE;

/* General-purpose I/O */
struct gpio {
    uint32_t crl;  /* Port configuration low */
    uint32_t crh;  /* Port configuration high */
    uint32_t idr;  /* Port input data */
    uint32_t odr;  /* Port output data */
    uint32_t bsrr; /* Port bit set/reset */
    uint32_t brr;  /* Port bit reset */
    uint32_t lckr; /* Port configuration lock */
};

#define GPIOA_BASE 0x40010800
#define GPIOB_BASE 0x40010c00
#define GPIOC_BASE 0x40011000
#define GPIOD_BASE 0x40011400
#define GPIOE_BASE 0x40011800
#define GPIOF_BASE 0x40011c00
#define GPIOG_BASE 0x40012000

volatile struct gpio * const gpioa = (struct gpio *)GPIOA_BASE;

/* USART */
struct usart {
    uint32_t sr;   /* Status */
    uint32_t dr;   /* Data */
    uint32_t brr;  /* Baud rate */
    uint32_t cr1;  /* Control 1 */
    uint32_t cr2;  /* Control 2 */
    uint32_t cr3;  /* Control 3 */
    uint32_t gtpr; /* Guard time and prescaler */
};

#define USART1_BASE 0x40013800

volatile struct usart * const usart1 = (struct usart *)USART1_BASE;

void ms_delay(int ms)
{
   while (ms-- > 0) {
      volatile int x=5971;
      while (x-- > 0)
         __asm("nop");
   }
}

static void do_putch(char **p, char *end, char c)
{
    if (*p < end)
        **p = c;
    (*p)++;
}

int vsnprintf(char *str, size_t size, const char *format, va_list ap)
    __attribute__ ((format (printf, 3, 0)));
int vsnprintf(char *str, size_t size, const char *format, va_list ap)
{
    unsigned int x, flags;
    int width;
    char c, *p = str, *end = p + size - 1, tmp[12], *q;

    while ((c = *format++) != '\0') {
        if (c != '%') {
            do_putch(&p, end, c);
            continue;
        }

        flags = width = 0;
#define BASE      (31u <<  0)
#define UPPER     ( 1u <<  8)
#define SIGN      ( 1u <<  9)
#define ALTERNATE ( 1u << 10)
#define ZEROPAD   ( 1u << 11)
#define CHAR      ( 1u << 12)
#define SHORT     ( 1u << 13)

    more:
        switch (c = *format++) {
        case '#':
            flags |= ALTERNATE;
            goto more;
        case '0':
            flags |= ZEROPAD;
            goto more;
        case '1'...'9':
            width = c-'0';
            while (((c = *format) >= '0') && (c <= '9')) {
                width = width*10 + c-'0';
                format++;
            }
            goto more;
        case 'h':
            if ((c = *format) == 'h') {
                flags |= CHAR;
                format++;
            } else {
                flags |= SHORT;
            }
            goto more;
        case 'o':
            flags |= 8;
            break;
        case 'd':
        case 'i':
            flags |= SIGN;
        case 'u':
            flags |= 10;
            break;
        case 'X':
            flags |= UPPER;
        case 'x':
        case 'p':
            flags |= 16;
            break;
        case 'c':
            c = va_arg(ap, unsigned int);
        default:
            do_putch(&p, end, c);
            continue;
        }

        x = va_arg(ap, unsigned int);

        if (flags & CHAR) {
            if (flags & SIGN)
                x = (char)x;
            else
                x = (unsigned char)x;
        } else if (flags & SHORT) {
            if (flags & SIGN)
                x = (short)x;
            else
                x = (unsigned short)x;
        }

        if ((flags & SIGN) && ((int)x < 0)) {
            if (flags & ZEROPAD) {
                do_putch(&p, end, '-');
                flags &= ~SIGN;
            }
            width--;
            x = -x;
        }

        if (flags & ALTERNATE) {
            if (((flags & BASE) == 8) || ((flags & BASE) == 16)) {
                do_putch(&p, end, '0');
                width--;
            }
            if ((flags & BASE) == 16) {
                do_putch(&p, end, 'x');
                width--;
            }
        }

        if (x == 0) {
            q = tmp;
            *q++ = '0';
        } else {
            for (q = tmp; x; q++, x /= (flags&BASE))
                *q = ((flags & UPPER)
                      ? "0123456789ABCDEF"
                      : "0123456789abcdef") [x % (flags&BASE)];
        }
        while (width-- > (q-tmp))
            do_putch(&p, end, (flags & ZEROPAD) ? '0' : ' ');
        if (flags & SIGN)
            do_putch(&p, end, '-');
        while (q != tmp)
            do_putch(&p, end, *--q);
    };

    if (p <= end)
        *p = '\0';

    return p - str;
}

int vprintk(const char *format, va_list ap)
    __attribute__ ((format (printf, 1, 0)));
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
    __attribute__ ((format (printf, 1, 2)));
int printk(const char *format, ...)
{
    va_list ap;
    int n;

    va_start(ap, format);
    n = vprintk(format, ap);
    va_end(ap);

    return n;
}

int main(void)
{
    uint32_t x = 1u<<16;

    rcc->apb2enr |= (1u<<14)/* usart1 */ | (1u<<2)/* gpioa */ | (1u<<0)/* afioen */;

    gpioa->crl = 0x44444446u;
    gpioa->crh = 0x444444a4u;

    usart1->cr1 = (1u<<13);
    usart1->cr2 = 0;
    usart1->cr3 = 0;
    usart1->gtpr = 0;
    usart1->brr = (1u<<4) | 1u; /* 460800 baud @ 8MHz */
    usart1->cr1 = (1u<<13) | (1u<<3) | (1u<<2);

    for (;;) {
        printk("Hello world! printf test: '%5d' '%05d' '%#014hhx' '%p' '%%'\n",
               -2, -987, 0x65383, gpioa);
        gpioa->bsrr = x ^= (1u<<16)|(1u<<0);
        ms_delay(100);
    }

    return 0;
}

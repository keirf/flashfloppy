/*
 * util.h
 * 
 * Utility definitions.
 * 
 * Written & released by Keir Fraser <keir.xen@gmail.com>
 * 
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

#define ASSERT(p) if (!(p)) illegal();

typedef char bool_t;
#define TRUE 1
#define FALSE 0

#ifndef offsetof
#define offsetof(a,b) __builtin_offsetof(a,b)
#endif
#define container_of(ptr, type, member) ({                      \
        typeof( ((type *)0)->member ) *__mptr = (ptr);          \
        (type *)( (char *)__mptr - offsetof(type,member) );})
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

#define min(x,y) ({                             \
    const typeof(x) _x = (x);                   \
    const typeof(y) _y = (y);                   \
    (void) (&_x == &_y);                        \
    _x < _y ? _x : _y; })

#define max(x,y) ({                             \
    const typeof(x) _x = (x);                   \
    const typeof(y) _y = (y);                   \
    (void) (&_x == &_y);                        \
    _x > _y ? _x : _y; })

#define min_t(type,x,y) \
    ({ type __x = (x); type __y = (y); __x < __y ? __x: __y; })
#define max_t(type,x,y) \
    ({ type __x = (x); type __y = (y); __x > __y ? __x: __y; })

void filename_extension(const char *filename, char *extension, size_t size);

void *memset(void *s, int c, size_t n);
void *memcpy(void *dest, const void *src, size_t n);
void *memmove(void *dest, const void *src, size_t n);

int strcmp(const char *s1, const char *s2);
int strncmp(const char *s1, const char *s2, size_t n);
char *strrchr(const char *s, int c);
int tolower(int c);

int vsnprintf(char *str, size_t size, const char *format, va_list ap)
    __attribute__ ((format (printf, 3, 0)));

int vprintk(const char *format, va_list ap)
    __attribute__ ((format (printf, 1, 0)));

int printk(const char *format, ...)
    __attribute__ ((format (printf, 1, 2)));

#define le16toh(x) (x)
#define le32toh(x) (x)
#define htole16(x) (x)
#define htole32(x) (x)
#define be16toh(x) _rev16(x)
#define be32toh(x) _rev32(x)
#define htobe16(x) _rev16(x)
#define htobe32(x) _rev32(x)

/* Arena-based memory allocation */
void *arena_alloc(uint32_t sz);
uint32_t arena_total(void);
uint32_t arena_avail(void);
void arena_init(void);

/* Board-specific callouts */
void board_init(void);

/* Performance tests */
void speed_tests(void);
void speed_tests_cancel(void);

/* Serial console control */
void console_init(void);
void console_sync(void);
void console_crash_on_input(void);


#ifdef BUILD_GOTEK

/* Gotek: DMA1 Ch2 is overloaded, IRQ needs switching. */
extern void (*_IRQ_dma1_ch2)(void);

/* Gotek: 3-digit 7-segment display */
void led_7seg_init(void);
void led_7seg_suspend(void);
void led_7seg_resume(void);
void led_7seg_write_hex(unsigned int x);

/* Gotek: USB stack processing */
void usbh_msc_init(void);
void usbh_msc_process(void);

#else /* !BUILD_GOTEK */

static inline void led_7seg_init(void) {}
static inline void led_7seg_suspend(void) {}
static inline void led_7seg_resume(void) {}
static inline void led_7seg_write_hex(unsigned int x) {}

static inline void usbh_msc_init(void) {}
static inline void usbh_msc_process(void) {}

#endif /* !BUILD_GOTEK */

extern uint8_t board_id;
#define BRDREV_MM150 0
#define BRDREV_TB160 1
#define BRDREV_LC150 7
#define BRDREV_Gotek 8

/* Text/data/BSS address ranges. */
extern char _stext[], _etext[];
extern char _sdat[], _edat[], _ldat[];
extern char _sbss[], _ebss[];

/* Stacks. */
extern uint32_t _thread_stacktop[], _thread_stackbottom[];
extern uint32_t _irq_stacktop[], _irq_stackbottom[];

/* Default exception handler. */
void EXC_unused(void);

/* IRQ priorities, 0 (highest) to 15 (lowest). */
#define FLOPPY_IRQ_HI_PRI     1
#define TIMER_IRQ_PRI         4
#define WDATA_IRQ_PRI         7
#define RDATA_IRQ_PRI         8
#define FLOPPY_IRQ_LO_PRI     9
#define USB_IRQ_PRI          10
#define LED_7SEG_PRI         14
#define CONSOLE_IRQ_PRI      15

/*
 * Local variables:
 * mode: C
 * c-file-style: "Linux"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */

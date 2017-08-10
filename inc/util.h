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

#define FW_VER "0.4.0a"

#ifndef NDEBUG
#define ASSERT(p) do { if (!(p)) illegal(); } while (0)
#else
#define ASSERT(p) do { if (0 && (p)) {} } while (0)
#endif

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

void fatfs_from_slot(FIL *file, const struct v2_slot *slot, BYTE mode);

void filename_extension(const char *filename, char *extension, size_t size);

void *memset(void *s, int c, size_t n);
void *memcpy(void *dest, const void *src, size_t n);
void *memmove(void *dest, const void *src, size_t n);
int memcmp(const void *s1, const void *s2, size_t n);

size_t strnlen(const char *s, size_t maxlen);
int strcmp(const char *s1, const char *s2);
int strncmp(const char *s1, const char *s2, size_t n);
char *strrchr(const char *s, int c);
int tolower(int c);

int vsnprintf(char *str, size_t size, const char *format, va_list ap)
    __attribute__ ((format (printf, 3, 0)));

int snprintf(char *str, size_t size, const char *format, ...)
    __attribute__ ((format (printf, 3, 4)));

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

#ifndef NDEBUG

/* Serial console control */
void console_init(void);
void console_sync(void);
void console_crash_on_input(void);

/* Serial console output */
int vprintk(const char *format, va_list ap)
    __attribute__ ((format (printf, 1, 0)));
int printk(const char *format, ...)
    __attribute__ ((format (printf, 1, 2)));

#else /* NDEBUG */

#define console_init() ((void)0)
#define console_sync() IRQ_global_disable()
#define console_crash_on_input() ((void)0)
static inline int vprintk(const char *format, va_list ap) { return 0; }
static inline int printk(const char *format, ...) { return 0; }

#endif

/* CRC-CCITT */
uint16_t crc16_ccitt(const void *buf, size_t len, uint16_t crc);

/* Display setup and identification. */
void display_init(void);
extern uint8_t display_mode;
#define DM_NONE     0
#define DM_LCD_1602 1
#define DM_LED_3DIG 2

#ifdef BUILD_GOTEK

/* Gotek: 3-digit 7-segment display */
bool_t led_3dig_init(void);
void led_3dig_write(const char *p);
void led_3dig_display_setting(bool_t enable);

/* Gotek: I2C 16x2 LCD */
bool_t lcd_init(void);
void lcd_clear(void);
void lcd_write(int col, int row, int min, const char *str);
bool_t lcd_has_backlight(void);
void lcd_backlight(bool_t on);
void lcd_sync(void);

/* Gotek: USB stack processing */
void usbh_msc_init(void);
void usbh_msc_process(void);
bool_t usbh_msc_connected(void);

#else /* !BUILD_GOTEK */

static inline bool_t led_3dig_init(void) { return FALSE; }
static inline void led_3dig_write(const char *p) {}
static inline void led_3dig_display_setting(bool_t enable) {}

static inline bool_t lcd_init(void) { return FALSE; }
static inline void lcd_clear(void) {}
static inline void lcd_write(int col, int row, int min, const char *str) {}
static inline bool_t lcd_has_backlight(void) { return FALSE; }
static inline void lcd_backlight(bool_t on) {};
static inline void lcd_sync(void) {}

static inline void usbh_msc_init(void) {}
static inline void usbh_msc_process(void) {}
static inline bool_t usbh_msc_connected(void) { return FALSE; }

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
#define RESET_IRQ_PRI         0
#define FLOPPY_IRQ_SEL_PRI    1
#define FLOPPY_IRQ_WGATE_PRI  2
#define FLOPPY_IRQ_STEP_PRI   3
#define FLOPPY_IRQ_SIDE_PRI   4
#define FLOPPY_IRQ_HI_PRI     3
#define TIMER_IRQ_PRI         4
#define WDATA_IRQ_PRI         7
#define RDATA_IRQ_PRI         8
#define FLOPPY_IRQ_LO_PRI     9
#define USB_IRQ_PRI          10
#define I2C_IRQ_PRI          14

/*
 * Local variables:
 * mode: C
 * c-file-style: "Linux"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */

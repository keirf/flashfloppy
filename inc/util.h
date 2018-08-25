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

#if FLASH_MEM_SIZE==0x40000
#define FW_VER "0.9.27a:256KB"
#else
#define FW_VER "0.9.27a:128KB"
#endif

#ifndef NDEBUG
#define ASSERT(p) do { if (!(p)) illegal(); } while (0)
#else
#define ASSERT(p) do { if (0 && (p)) {} } while (0)
#endif

typedef char bool_t;
#define TRUE 1
#define FALSE 0

#define LONG_MAX ((long int)((~0UL)>>1))
#define LONG_MIN ((long int)~LONG_MAX)

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

struct slot {
    char name[FF_MAX_LFN+1];
    char type[7];
    uint8_t attributes;
    uint32_t firstCluster;
    uint32_t size;
    uint32_t dir_sect, dir_ptr;
};
void fatfs_from_slot(FIL *file, const struct slot *slot, BYTE mode);

void filename_extension(const char *filename, char *extension, size_t size);

/* Fast memset/memcpy: Pointers must be word-aligned, count must be a non-zero 
 * multiple of 32 bytes. */
void memset_fast(void *s, int c, size_t n);
void memcpy_fast(void *dest, const void *src, size_t n);

void *memset(void *s, int c, size_t n);
void *memcpy(void *dest, const void *src, size_t n);
void *memmove(void *dest, const void *src, size_t n);
int memcmp(const void *s1, const void *s2, size_t n);

size_t strnlen(const char *s, size_t maxlen);
int strcmp(const char *s1, const char *s2);
int strncmp(const char *s1, const char *s2, size_t n);
char *strchr(const char *s, int c);
char *strrchr(const char *s, int c);
int tolower(int c);
int isspace(int c);

long int strtol(const char *nptr, char **endptr, int base);

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
#define DM_LED_7SEG 2

/* Speaker. */
void speaker_init(void);
void speaker_pulse(void);

/* Display: 3-digit 7-segment display */
void led_7seg_init(void);
void led_7seg_write_string(const char *p);
void led_7seg_write_decimal(unsigned int val);
void led_7seg_display_setting(bool_t enable);
int led_7seg_nr_digits(void);

/* Display: I2C 16x2 LCD */
bool_t lcd_init(void);
void lcd_clear(void);
void lcd_write(int col, int row, int min, const char *str);
void lcd_backlight(bool_t on);
void lcd_sync(void);
extern uint8_t lcd_columns, lcd_rows;

/* USB stack processing */
void usbh_msc_init(void);
void usbh_msc_buffer_set(uint8_t *buf);
void usbh_msc_process(void);
bool_t usbh_msc_connected(void);
bool_t usbh_msc_readonly(void);

extern uint8_t board_id;

/* Gotek board revisions */
#define BRDREV_Gotek_standard 0xf
#define BRDREV_Gotek_enhanced 0x0

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
#define FLOPPY_SOFTIRQ_PRI    9
#define I2C_IRQ_PRI          13
#define USB_IRQ_PRI          14

/*
 * Local variables:
 * mode: C
 * c-file-style: "Linux"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */

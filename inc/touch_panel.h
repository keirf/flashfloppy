/*
 * touch_panel.h
 * 
 * Interfaces to ILI9341 display driver, PWM-controlled backlight, and
 * XPT2046 touch screen controller.
 * 
 * Written & released by Keir Fraser <keir.xen@gmail.com>
 * 
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

/* TFT display dimensions with 8x16 characters */
#define TFT_8x16_COLS 40
#define TFT_8x16_ROWS 15

/* TFT display dimensions with 4x8 characters */
#define TFT_4x8_COLS 80
#define TFT_4x8_ROWS 24

#ifdef BUILD_TOUCH

/* TFT LCD display */
void tft_init(void);
void fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t c);
void clear_screen(void);
void draw_string_8x16(uint16_t x, uint16_t y, const char *str);
void draw_string_4x8(uint16_t x, uint16_t y, const char *str);

/* PWM backlight */
void backlight_init(void);
void backlight_set(uint8_t level);

/* Touch screen control */
void touch_init(void);
bool_t touch_get_xy(uint16_t *px, uint16_t *py);

#else

static inline void tft_init(void)
{}
static inline void fill_rect(
    uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t c)
{}
static inline void clear_screen(void)
{}
static inline void draw_string_8x16(uint16_t x, uint16_t y, const char *str)
{
    printk("%s\n", str);
}
static inline void draw_string_4x8(uint16_t x, uint16_t y, const char *str)
{
    printk("%s\n", str);
}

static inline void backlight_init(void)
{}
static inline void backlight_set(uint8_t level)
{}

static inline void touch_init(void)
{}
static inline bool_t touch_get_xy(uint16_t *px, uint16_t *py)
{
    return FALSE;
}

#endif

/*
 * Local variables:
 * mode: C
 * c-file-style: "Linux"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */

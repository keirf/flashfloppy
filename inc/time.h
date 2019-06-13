/*
 * time.h
 * 
 * System-time abstraction over STM32 STK timer.
 * 
 * Written & released by Keir Fraser <keir.xen@gmail.com>
 * 
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

typedef uint32_t time_t;

#define TIME_MHZ STK_MHZ
#define time_us(x) stk_us(x)
#define time_ms(x) stk_ms(x)
#define time_sysclk(x) stk_sysclk(x)
#define sysclk_time(x) sysclk_stk(x)

void delay_from(time_t t, unsigned int ticks);
time_t time_now(void);

#define time_diff(x,y) ((int32_t)((y)-(x))) /* d = y - x */
#define time_add(x,d)  ((time_t)((x)+(d)))  /* y = x + d */
#define time_sub(x,d)  ((time_t)((x)-(d)))  /* y = x - d */
#define time_since(x)  time_diff(x, time_now())

void time_init(void);

/*
 * Local variables:
 * mode: C
 * c-file-style: "Linux"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */

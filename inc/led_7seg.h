/* 7-segment display bits by physical position. */
#ifndef LED_7SEG_H
#define LED_7SEG_H

#include <stdint.h>

#define SEG_T (1u << 0)
#define SEG_TR (1u << 1)
#define SEG_BR (1u << 2)
#define SEG_B (1u << 3)
#define SEG_BL (1u << 4)
#define SEG_TL (1u << 5)
#define SEG_M (1u << 6)

extern const uint8_t led_7seg_digits[10];
extern const uint8_t led_7seg_letters[26];

#endif

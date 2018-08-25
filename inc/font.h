/*-------------------------------------------*/
/* Integer type definitions for FatFs module */
/*-------------------------------------------*/

#ifndef FONT_H
#define FONT_H

bool_t is_sjis_1st(uint8_t code1);

uint8_t *font_get(uint16_t kcode);
uint8_t *font_get_nl(uint16_t kcode);

void font_cache(const char *str);
void font_close(void);
void font_init(const char *font_fname);

#endif

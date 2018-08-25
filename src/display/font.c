/*
 * font.c
 * 
 * 1. SHIFT-JIS KANJI-font driver
 *
 * 
 * Written by Tatsuyuki Sato <tatsu.sato@gmail.com>
 * 
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

/****************************************************************************
  configration
****************************************************************************/
#define NUM_FONT_SIZE  (16*2)   /* FONT PATTERN SIZE  */
#define NUM_CACHE_FONT (128)     /* mnum of FONT CACHE */

/****************************************************************************
static work area
****************************************************************************/

// font file
static FIL font_file_fp;		// FatFs file object
static int font_file_ready = 0; // file opened flag

/* font cache object */
typedef struct font_buffer
{
	// link control
	struct font_buffer *prev;
	struct font_buffer *next;
	// buffer
	uint16_t code;
	uint8_t  pattern[NUM_FONT_SIZE];
}FONT;

/* font cache top entry */
static FONT *font_top;
/* font cache buffer */
static FONT font_list[NUM_CACHE_FONT];

/* font working buffer */
static uint8_t pat_buf[NUM_FONT_SIZE];

/****************************************************************************
 JIS to KANJI-ROM code conversion
****************************************************************************/
// uint16_t jis2mbrom(uint16_t jis);
#include "knj83256.c"

/****************************************************************************
  SJIS 1st byte check
****************************************************************************/
bool_t is_sjis_1st(uint8_t code1)
{
    return (code1>=0x80 && code1<=0x9f) || (code1>=0xE0 && code1<=0xEB);
}

/****************************************************************************
  SJIS to JIS code conversion
****************************************************************************/
static uint16_t sjis2jis(uint16_t sjis)
{
	uint8_t hb,lb;
	uint16_t jis;

	hb = ((uint8_t)(sjis>>8) -0x81)*2 +0x21;
	lb = (uint8_t)sjis;
	if(lb<0x7f)
	{
		lb-=0x1f;
	}else if(lb<0x9f)
	{
		lb-=0x20;
	}else
	{
		lb-=0x7e;
		hb++;
	}	
	jis = (((uint16_t)hb)<<8) | lb;

//printk("jis %04X\n",jis);
	return jis;
}

/****************************************************************************
update targe font to newest list
****************************************************************************/
static FONT *font_update(FONT *font)
{
	// check top of list
	if(font->prev==0) return font;
	
	// remove link from list
	font->prev->next = font->next;
	if(font->next)
		font->next->prev = font->prev;
	// insert to top
	font->prev     = 0; // NULL 
	font->next     = font_top;
	font_top->prev = font;
	font_top       = font;
	return font;
}

/****************************************************************************
load and regist font with horizonal to vertical conversion

input
	pattern[0x00] : H=0..7 ,V=0
	pattern[0x01] : H=8..15,V=0
	pattern[0x02] : H=0..7 ,V=1
                  :
output
	pattern[0x00] : H=0,V=7..0
	pattern[0x01] : H=1,V=7..0
                  :
	pattern[0x10] : H=0,V=15..8
	pattern[0x11] : H=1,V=15..8
                  :
 
****************************************************************************/
static FRESULT font_load_convert(uint8_t *dst)
{
	uint8_t *src;
	UINT readed;
	FRESULT res;
	uint16_t vd16,xmask;
	uint8_t ymask;
	UINT y,offs;
	
	// clear buffer
	memset(dst,0,16*2);
	
	// read work buffer from file
	res = f_read(&font_file_fp,pat_buf,NUM_FONT_SIZE,&readed);
	
	// re-build hrizonal font to vertical font
	src   = pat_buf;
	ymask = 0x01<<(0%8);
	offs  = (0/8);
	for(y=0;y<16;y++)
	{
		vd16  = ((uint16_t)src[0])<<8 | src[1]; // LT,RT
		for(xmask=0x8000;xmask;xmask>>=1)
		{
			if(vd16 & xmask) dst[offs] |= ymask;
			offs++;
		}
		src+=2;
		ymask <<= 1; // y++
		if(ymask)
			offs -= 16;   // x=0
		else
			ymask = 0x01; // next row
	}
	return res;
}

/****************************************************************************
	get font pattern with auto-load
****************************************************************************/
uint8_t *font_get(uint16_t kcode)
{
	FONT *font;
	FRESULT res;
	FSIZE_t ofs;

//printk("font_get(%04X)",kcode);
	res = FR_NOT_READY;
	// search cache buffer
	for(font=font_top;;font=font->next)
	{
//printk("[%04X]",font->code);
		if(kcode==font->code)
		{
//printk("cache_hit\n");
			// hit in cache
			font_update(font);
			return font->pattern;
		}
		// end of link ?
		if(font->next==0) break;
	}
	if(font_file_ready)
	{
		// cache miss ,erase & overwrite bottom of list
	
		// calcrate pattern location in font file
		ofs = jis2mbrom(sjis2jis(kcode)) *NUM_FONT_SIZE;
//printk("font_pos=%08X\n",(int)ofs);
		res = f_lseek(&font_file_fp,ofs);
//printk("res %d\n",res);
		if(res==FR_OK)
		{
//			UINT readed;
//printk("FONT_SEEK OK\n");
			//res = f_read(&font_file_fp,font->pattern,NUM_FONT_SIZE,&readed);
			res = font_load_convert(font->pattern);
			
			if(res==FR_OK)
			{
//printk("FONT_READ OK\n");
				// read ok , regist font code
				font->code = kcode;
				// move to top of cache list
				font_update(font);
				// return font pattern
				return font->pattern;
			}
		}
//printk("FONT_READ ERR\n");
		// cause read error , close font file
		font_close();
	}
	// regist blank data
	font->code = 0xffff;
	{
		int i;
		for(i=0;i<NUM_FONT_SIZE;i++)
			font->pattern[i]=0xff;
	}
	return font->pattern;
}

/****************************************************************************
get font without auto-load
****************************************************************************/
uint8_t *font_get_nl(uint16_t kcode)
{
	FONT *font;
	
	// search cache buffer
	for(font=font_top;;font=font->next)
	{
		if(kcode==font->code)
		{
			// hit in cache
			font_update(font);
			return font->pattern;
		}
		// end of link ?
		if(font->next==0) break;
	}
	// non cached code , build & return the dummy pattern
	memset(pat_buf,0x55,sizeof(NUM_FONT_SIZE));
	return pat_buf;		// font_top->pattern;
}

/****************************************************************************
	cache font sjis string
****************************************************************************/
void font_cache(const char *str)
{
	const char *p;
	uint16_t c16;
	uint8_t c8;
	
//printk("font_cache()");
    // pre-cache all font
	c16 = 0;
    for (p=str;*p;)
    {
		c8 = *p++;
		if(c16)
		{
			// DBCS 2nd
            font_get(c16 | c8);
			c16 = 0;
		} else if(  is_sjis_1st(c8) ) {
			// DBCS 1st
			c16 = ((uint16_t)c8)<<8;
		}
    }
}

/****************************************************************************
	close font file
****************************************************************************/
void font_close(void)
{
printk("font_close\n");
	if(font_file_ready)
	{
		f_close(&font_file_fp);
		font_file_ready = 0;
	}
}

/****************************************************************************
	initilize & open font file
****************************************************************************/
void font_init(const char *font_fname)
{
	FONT *font;
	
	// close font file
	font_close();
	// open font file
printk("font_open(%s)\n",font_fname);
	if(f_open(&font_file_fp,font_fname,FA_READ)==FR_OK)
	{
		font_file_ready = 1;
	}

	// init cache buffer
	for(font=font_list;font<(font_list+NUM_CACHE_FONT);font++)
	{
		font->prev = font-1;
		font->next = font+1;
		font->code = 0xffff;
	}
	(font-1) -> next = 0; // btm next == null
	font_list-> prev = 0; // top prev == null
	font_top = font_list;
}

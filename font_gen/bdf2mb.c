#include <stdio.h>
#include <string.h>
/****************************************************************************
	shinonome bdf font to MB832001-042 binary font data conversion
	
	1.東雲16フォントから、漢字ROM MB831000-042/044 互換データを生成する。
	2.水平漢字ROMデータから垂直配置に変換する

	2018.8.17 T.Sato
****************************************************************************/

//#define SRC_FNAME8  "shnm8x16a.bdf"
#define SRC_FNAME16 "shnmk16.bdf"
#define DST_FNAME16 "font1616"

#define SRC_FNAME12 "shnmk12.bdf"
#define DST_FNAME12 "font1212"

/****************************************************************************
****************************************************************************/
typedef unsigned char  uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int   UINT;

/****************************************************************************
  work buffer
****************************************************************************/
// KNAJI font data buffer
// 0x00000-0x1FFFF : MB831000-042 JIS std.1
// 0x20000-0x3FFFF : MB831000-044 JIS std.2
static uint8_t font_buf[2*16*0x2000];
static uint8_t vfont_buf[2*16*0x2000];

/****************************************************************************
  JIS to ROM address generator
****************************************************************************/
uint16_t jis2mbrom(uint16_t jis)
{
	static const uint8_t mb_ktable[3*6] = // conversion lockup table
{// T[12:8]         // I[4:0]
	0x00,0x02,0x01,	// 09,0A,0B
	0x04,0x06,0x08,	// 0D,0E,0F
	0x0A,0x0C,0x0E,	// 11,12,13
	0x10,0x12,0x14,	// 15,16,17
	0x16,0x18,0x1A,	// 19,1A,1B
	0x1C,0x1E,0x1D	// 1D,1E,1F
};
	uint8_t  index;
	uint16_t romcode;
	// fixed: K[8:4] = J[11:8]  , K[4:0] = J[4:0]
	romcode  =  ((jis & 0xf00)>>3) | (jis & 0x1f);
	// index: ((J[14:12])-2)*3  +((J[6:5])-1)    -(total_orign)
	index  = (((jis>>12)&7)*3) +((jis>> 5)&3)   -(2*3+1);
	// mixing fixed field & table field
	return romcode ^= (uint16_t)(mb_ktable[index])<<8;
}

/****************************************************************************
  Horizonal Pattern to Vertical Pattern

  input
  +0x00:H=0-7  ,V=0
  +0x01:H=8-157,V=0
  +0x02:H=0-7  ,V=1
  +0x03:H=8-157,V=1
       :

  output
  +0x00:V=0-7  ,H=0
  +0x01:V=0-7  ,H=1
       :
  +0x10:V=8-15 ,H=0
  +0x11:V=8-15 ,H=1
       :
****************************************************************************/
int font_rotate1616(uint8_t *src,uint8_t *dst)
{
	uint16_t vd16,xmask;
	uint8_t ymask;
	UINT offs;
	UINT y;
	
	// clear buffer
	memset(dst,0,16*2);
	
	// re-build hrizonal font to vertical font
	for(y=0;y<16;y++)
	{
		vd16  = ((uint16_t)src[y*2])<<8 | src[y*2+1]; // LT+RT
		offs  = (y/8)*16;
		ymask = 0x80>>(y&7);
		for(xmask=0x8000;xmask;xmask>>=1)
		{
			if(vd16 & xmask) dst[offs] |= ymask;
			offs++;
		}
	}
	return 0;
}

/****************************************************************************
  BDF to BIN loader with address conversion

  output format : 16x16only
****************************************************************************/
int font_load(const char *fname)
{
	int c,d;
	char s[1024];
	int pos = -1;
	int maxPos= 0;
	FILE *fp;
	
	fp=fopen(fname,"rt");
	if(fp==NULL)
	{
		printf("Can't open %s\n",fname);
		return -1;
	}
	
	while(fgets(s,1024,fp)!=NULL)
	{
		if(strncmp(s,"STARTCHAR ",9)==0)
		{
			c = -1;
			pos = -1;
			sscanf(s,"STARTCHAR %x",&c);
			if(c>=0)
			{
				if(c>=256)
				{
					// DBCS : JIS to ROM code
					pos = jis2mbrom(c); // jis2sjis(c);
					// 8x16 or 16x16 char size
					pos *= 16*2;
				}
				else
				{
					// SBCS : ANK 8x16
					pos = c * (16*1);
				}
			}
		}
		if(strncmp(s,"BITMAP",6)==0) {
			while(fgets(s,1024,fp)!=NULL)
			{
				if(strncmp(s,"ENDCHAR",7)==0) break;
				sscanf(s,"%x",&d);
				if(c>=256)
				{
					// kanji
					font_buf[pos++] = d>>8;    // Left
					font_buf[pos++] = d&0xff;  // Right
				}
				else if(c>=0)
				{
					// ank
					font_buf[pos++] = d;
				}
			}
		}
		// maximum data size
		if(maxPos < pos)
			maxPos = pos;
	}
	fclose(fp);
	return maxPos;
}

/****************************************************************************
  output
****************************************************************************/
int file_write(const char *fileName,const void *data,int dataSize)
{
	FILE *fp;
	int numWrited;
	
	fp=fopen(fileName,"wb+");
	if(fp==NULL) return -1;
	numWrited = fwrite(data,1,dataSize,fp);
	fclose(fp);
	return numWrited==dataSize ? 0 : -2;
}

/****************************************************************************
conversion
****************************************************************************/
int font_conv(const char *fileNameIn,const char *fileNameOut,int dataSize)
{
	int font_size;
	int pos;
	char fn_buf[256];
	
	printf("load font %s\n",fileNameIn);
	memset(font_buf,0x00,sizeof(font_buf));
	font_size = font_load(fileNameIn);
	if(font_size<0)
	{
		printf("Failed\n");
		return -1;
	}
	printf("Generated %dbytes\n",font_size);
	// write file
	sprintf(fn_buf,"%s.bin",fileNameOut);
	if(file_write(fn_buf,font_buf,sizeof(font_buf))==0)
	{
		printf("Writed\n");
	}
	// rotate font
	memset(vfont_buf,0x00,sizeof(font_buf));
	for(pos=0;pos<font_size;pos+=16*2)
	{
		font_rotate1616(&font_buf[pos],&vfont_buf[pos]);
	}
	// write file
	sprintf(fn_buf,"%sv.bin",fileNameOut);
	if(file_write(fn_buf,vfont_buf,sizeof(vfont_buf))==0)
	{
		printf("Writed\n");
	}
	return 0;
}

/****************************************************************************
  main
****************************************************************************/
int main(int argc,char **argv) {
	printf("BDF to KANJI-ROM data converter\n");
	
	// clear font buffer
	memset(font_buf,0x00,sizeof(font_buf));

	// SBCS ANK
	
	// DBCS CP932 KANJI
	font_conv(SRC_FNAME12,DST_FNAME12,sizeof(font_buf));
	font_conv(SRC_FNAME16,DST_FNAME16,sizeof(font_buf));
	
	return 0;
}

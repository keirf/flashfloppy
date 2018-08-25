/****************************************************************************
  JIS to KANJI-ROM code converter
  
  MB823xx series ROM code
  +--------+-----------+-----------+
  |ROM     |  std. 1   |  std. 2   |
  +--------+-----------+-----------+
  |MB83256 |19/20/21/22|23/24/25/26|
  |MB831000|    042    |    044    |
  |MB832001|          042          |
  +--------+-----------------------+

JIS code bit assign
+---------------------------------------------------------------------+
|bit|B15|B14|B13|B12|B11 |B10|B9 |B8 ||B7 |B6 |B5 |B4 |B3 |B2 |B1 |B0 |
|JIS|---|b17|b16|b15|b14 |b13|b12|b11||---|b27|b26|b25|b24|b23|b22|b21|
+---------------------------------------------------------------------+
|ROM|---|I4 |I3 |I2 |(K8)|K7 |K6 |K5 ||---|I1 |I0 |K4 |K3 |K2 |K1 |K0 |
+---------------------------------------------------------------------+
Kn : KANJI-ROM code
In : conversion table INDEX

KANJI-ROM address bit assign
+------------------------------------------------------------------+
|CODE| 0 | 0 | 0 |K12|K11|K10|K9 |K8    ||K7 -K5 |K4-K0||R3-R0|-L/R|
+------------------------------------------------------------------+
|JIS |---|---|---|T12|T11|T10|T9 |T8^J11||J10-J8 |J4-J0||-----|--- |
+------------------------------------------------------------------+
|256K|---|---|---|CE2|CE1|A14|A13|A12   ||A11-A9 |A8-A4||A3-A0|CE0 |
|1M  |---|---|---|CE2|A16|A15|A14|A13   ||A12-A10|A9-A5||A4-A1|A0  |
|2M  |---|---|---|A??|A??|A??|A??|A??   ||A??-A??|A?-A?||A?-A?|A?  |
+------------------------------------------------------------------+
LR : left=? / right=?
Rn : raster number
K  : kanji-code number
Jn : JIS code
Tn : conversion table VALUE from index

conversion table
+---+---+---+---+---+ +---+---+---+---+---+
|            std. 1 (T12=0)               |
+---+---+---+---+---+ +---+---+---+---+---+
|    JIS index      | |   ROM Code        |  
|I4 |I3 |I2 |I1 |I0 | |T12|T11|T10|T9 |T8 |
+---+---+---+---+---+ +---+---+---+---+---+
| 0 | 1 | 0 | 0 | 1 | | 0 | 0 | 0 | 0 | 0 |
| 0 | 1 | 0 | 1 | 0 | | 0 | 0 | 0 | 1 | 0 |
| 0 | 1 | 0 | 1 | 1 | | 0 | 0 | 0 | 0 | 1 |
| 0 | 1 | 1 | 0 | 1 | | 0 | 0 | 1 | 0 | 0 |
| 0 | 1 | 1 | 1 | 0 | | 0 | 0 | 1 | 1 | 0 |
+---+---+---+---+---+ +---+---+---+---+---+
| 0 | 1 | 1 | 1 | 1 | | 0 | 1 | 0 | 0 | 0 |
| 1 | 0 | 0 | 0 | 1 | | 0 | 1 | 0 | 1 | 0 |
| 1 | 0 | 0 | 1 | 0 | | 0 | 1 | 1 | 0 | 0 |
| 1 | 0 | 0 | 1 | 1 | | 0 | 1 | 1 | 1 | 0 |
+---+---+---+---+---+ +---+---+---+---+---+
|            std. 2 (T12=1)               |
+---+---+---+---+---+ +---+---+---+---+---+
|    JIS index      | |   ROM Code        |  
+---+---+---+---+---+ +---+---+---+---+---+
|I4 |I3 |I2 |I1 |I0 | |T12|T11|T10|T9 |T8 |
| 1 | 0 | 1 | 0 | 1 | | 1 | 0 | 0 | 0 | 0 |
| 1 | 0 | 1 | 1 | 0 | | 1 | 0 | 0 | 1 | 0 |
| 1 | 0 | 1 | 1 | 1 | | 1 | 0 | 1 | 0 | 0 |
| 1 | 1 | 0 | 0 | 1 | | 1 | 0 | 1 | 1 | 0 |
+---+---+---+---+---+ +---+---+---+---+---+
| 1 | 1 | 0 | 1 | 0 | | 1 | 1 | 0 | 0 | 0 |
| 1 | 1 | 0 | 1 | 1 | | 1 | 1 | 0 | 1 | 0 |
| 1 | 1 | 1 | 0 | 1 | | 1 | 1 | 1 | 0 | 0 |
| 1 | 1 | 1 | 1 | 0 | | 1 | 1 | 1 | 1 | 0 |
| 1 | 1 | 1 | 1 | 1 | | 1 | 1 | 0 | 1 | 1 |
+---+---+---+---+---+ +---+---+---+---+---+
*/

/****************************************************************************
	JIS to type MB83256-19 KANJI-ROM address conversion
result:
	romcode
	RES[12]   : ROM Code[12]   (0=JIS std.1 / 1= JIS std.2)
	RES[11]   : ROM Code[11]   (chip select for less than 2M bit ROM)
	RES[10:0] : ROM Code[10:0]
note:
	rom_address = romcode * (2*16);
	
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
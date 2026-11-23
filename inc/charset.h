/*
 * charset.h
 *
 * The character set of every name that the firmware holds, a byte for each
 * character: ASCII; the generic currency sign at 0x01; Central European
 * letters at 0x02-0x1f; the Latin letters of code pages 437 and 850 at
 * 0x80-0xbf, with the pound at 0x9f; and the Russian alphabet in KOI8-R's
 * order, small letters at 0xc0-0xdf and capitals at 0xe0-0xff. A character
 * of a long name that the set lacks reads as '?', which no FAT name holds,
 * and one outside the Basic Multilingual Plane as two.
 *
 * Central European capitals occupy 0x02-0x10, their small letters 15 cells
 * below. Latin capitals occupy 0x80-0x9d, with their small letters 32 cells
 * below. Base letters are alphabetical, with accents in the same order for
 * each. Unpaired letters are at the right: dotless I at 0x9e, small y
 * diaeresis at 0xbe and sharp S at 0xbf. Yo and yo share Ye's cells, as
 * Russian commonly permits, and either Unicode spelling matches.
 *
 * The Russian letters take KOI8-R's order rather than 1251's alphabetical one
 * because a KOI8-R letter with bit 7 cleared is the Latin letter that sounds
 * like it: a display without Cyrillic glyphs, such as FF OSD or an LCD with a
 * Latin ROM, shows a name readably by a subtraction. The set is the firmware's
 * own and ffemu's, never written to a drive, so a byte may move as long as
 * every table that maps it moves too: charset.c's, scripts/mk_font.py's and
 * ffemu's.
 *
 * Written & released by Mike Shevchenko <mike.shevchenko@gmail.com>
 *
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

/* Short name @name, the bytes of an 8.3 entry, into the set in place: as
 * UTF-8 if its bytes are that, else in the code page of short names. */
void charset_sfn_decode(char *name);

/* Whether 8.3 entry @dir holds a byte over 0x7f. */
bool_t charset_sfn_extended(const uint8_t *dir);

/* Whether Unicode characters @a and @b match, letter case and shared cells
 * aside. */
bool_t charset_uni_match(DWORD a, DWORD b);

/* Whether 8.3 entry @dir reads as @name, in UTF-16, letter case aside. */
bool_t charset_sfn_match(const uint8_t *dir, const uint16_t *name);

/* The key that sorts character @c, letter case aside: Latin letters by base
 * letter among ASCII, then the Russian letters in their alphabet's order. */
unsigned int charset_key(uint8_t c);

/* FF OSD's font, which ends at 0x7f, beside the LCD ROMs of LCDCS_*. */
#define CHARSET_osd 3

/* The code that shows character @c in character ROM @rom, LCDCS_* or
 * CHARSET_osd: the ROM's own glyph of it, else a stand-in, else a mark of
 * a character that the ROM cannot show. */
uint8_t charset_to_rom(uint8_t c, unsigned int rom);

/*
 * Local variables:
 * mode: C
 * c-file-style: "Linux"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */

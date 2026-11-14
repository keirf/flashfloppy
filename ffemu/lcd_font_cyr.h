/*
 * lcd_font_cyr.h
 *
 * cyr-melt, the character ROM of MELT's Cyrillic LCD modules, Latin and
 * Cyrillic, upper and lower case: page 0 of their character generator, as
 * the MT-16S2H datasheet draws it in its table 5. The page is
 * chosen by bit 1 of the function set command, which the firmware leaves
 * clear. The Cyrillic letters that Latin ones do not stand for are at
 * 0xa0 to 0xc7 and 0xe0 to 0xe6, and the capital soft sign at 0x08;
 * Ukrainian and Belarusian ones at 0x0a and 0x0c to 0x0f. Unlike ASCII,
 * 0x5c is a cent sign, 0x7b to 0x7d are a small 10, 12 and 15, 0x7e is a
 * return arrow and 0x7f a pencil; a backslash is at 0x15, the braces at
 * 0x8b and 0x8c, a tilde at 0xe9. Codes 0x00 to 0x07 are the user-defined
 * characters.
 *
 * The same codes, at 0x20 to 0x7f and 0xa0 to 0xff, some glyphs drawn
 * otherwise, but nothing below 0x20 and blanks at 0x80 to 0x9f, are in
 * Sitronix's ST7066U ROM 0T, "English/Russian", which Winstar sells as
 * font code CT and Display Elektronik as CYR22, and in the generic
 * "LCD1602 with Cyrillic" modules; Winstar's WS0010 character OLEDs have
 * it as font table 10, with accented Latin at 0x80 to 0x9f. Russian text
 * libraries for such modules, LiquidCrystalRus and Marlin's Cyrillic
 * character set among them, map to these codes. Sitronix's ROM is
 * cyr-sitronix, lcd_font_cyr_sitronix.h.
 *
 * The form: the glyphs from code 0x08 on, eight to a band, each band under
 * a line that labels its glyphs: an ASCII character as itself, a blank
 * glyph as "empty", a Cyrillic letter by its short name ("zhe", "i kr."
 * for the short i, "m.zn." for the soft sign, and so on, capitalized for
 * a capital), another character as + and its Unicode code point, and a
 * glyph that no single character matches as "non-U". Each glyph is five
 * dots wide and eight tall, the last row the cursor's, 'X' a dot and '.'
 * none, the glyphs of a row apart by '|'. Then the Unicode code point that
 * each code stands for: a space for a blank glyph, 0 for a glyph that no
 * single character of the Basic Multilingual Plane matches.
 *
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

#define LCD_FONT_CYR_FIRST 0x08

static const char * const lcd_font_cyr[] = {
    /* M.zn. +00B5 u kr. non-U   Yi    yi    Ie    ie  */
      "X....|.....|.X.X.|X..X.|.X.X.|.....|.XXX.|.....",
      "X....|X..X.|..X..|X.X.X|.....|.X.X.|X...X|.....",
      "X....|X..X.|X...X|X.X.X|..X..|.....|X....|.XXX.",
      "XXX..|X..X.|X...X|X.X.X|..X..|..X..|XXX..|X...X",
      "X..X.|X..X.|.XXXX|X.X.X|..X..|..X..|X....|XXX..",
      "X..X.|XXX.X|....X|X.X.X|..X..|..X..|X...X|X...X",
      "XXX..|X....|.XXX.|X..X.|..X..|..X..|.XXX.|.XXX.",
      ".....|X....|.....|.....|.....|.....|.....|.....",

    /* +2026 +203C +00F7 +2192 +2190   \   non-U +2116 */
      ".....|.X.X.|.....|.....|.....|.....|.....|.....",
      ".....|.X.X.|..X..|..X..|..X..|X....|.....|X..XX",
      ".....|.X.X.|.....|...X.|.X...|.X...|.XXXX|X..X.",
      ".....|.X.X.|XXXXX|XXXXX|XXXXX|..X..|.X..X|XX.XX",
      ".....|.X.X.|.....|...X.|.X...|...X.|.X...|X.XXX",
      ".....|.....|..X..|..X..|..X..|....X|.X...|X..X.",
      "X.X.X|.X.X.|.....|.....|.....|.....|.X..X|X..X.",
      ".....|.....|.....|.....|.....|.....|.....|.....",

    /* +00AE +00A9 +2264 +2265 +221A +00A5 +2260 +2248 */
      ".XXX.|.XXX.|...X.|.X...|..XXX|X...X|.....|.....",
      "X..XX|XX.XX|..X..|..X..|..X..|.X.X.|...X.|.X.X.",
      "X.X.X|X.X.X|.X...|...X.|..X..|XXXXX|XXXXX|X.X..",
      "X..XX|X.XXX|..X..|..X..|..X..|..X..|..X..|.....",
      "X.X.X|X.X.X|...X.|.X...|X.X..|XXXXX|XXXXX|.X.X.",
      "X.X.X|XX.XX|.....|.....|.XX..|..X..|.X...|X.X..",
      ".XXX.|.XXX.|.XXX.|.XXX.|..X..|..X..|.....|.....",
      ".....|.....|.....|.....|.....|.....|.....|.....",

    /* empty   !     "     #     $     %     &     '   */
      ".....|..X..|.X.X.|.X.X.|..X..|XX...|.XX..|.XX..",
      ".....|..X..|.X.X.|.X.X.|.XXXX|XX..X|X..X.|..X..",
      ".....|..X..|.X.X.|XXXXX|X.X..|...X.|X.X..|.X...",
      ".....|..X..|.....|.X.X.|.XXX.|..X..|.X...|.....",
      ".....|..X..|.....|XXXXX|..X.X|.X...|X.X.X|.....",
      ".....|.....|.....|.X.X.|XXXX.|X..XX|X..X.|.....",
      ".....|..X..|.....|.X.X.|..X..|...XX|.XX.X|.....",
      ".....|.....|.....|.....|.....|.....|.....|.....",

    /*   (     )     *     +     ,     -     .     /   */
      "...X.|.X...|.....|.....|.....|.....|.....|.....",
      "..X..|..X..|..X..|..X..|.....|.....|.....|....X",
      ".X...|...X.|X.X.X|..X..|.....|.....|.....|...X.",
      ".X...|...X.|.XXX.|XXXXX|.....|XXXXX|.....|..X..",
      ".X...|...X.|X.X.X|..X..|.XX..|.....|.....|.X...",
      "..X..|..X..|..X..|..X..|..X..|.....|.XX..|X....",
      "...X.|.X...|.....|.....|.X...|.....|.XX..|.....",
      ".....|.....|.....|.....|.....|.....|.....|.....",

    /*   0     1     2     3     4     5     6     7   */
      ".XXX.|..X..|.XXX.|XXXXX|...X.|XXXXX|..XX.|XXXXX",
      "X...X|.XX..|X...X|...X.|..XX.|X....|.X...|....X",
      "X..XX|..X..|....X|..X..|.X.X.|XXXX.|X....|...X.",
      "X.X.X|..X..|...X.|...X.|X..X.|....X|XXXX.|..X..",
      "XX..X|..X..|..X..|....X|XXXXX|....X|X...X|.X...",
      "X...X|..X..|.X...|X...X|...X.|X...X|X...X|.X...",
      ".XXX.|.XXX.|XXXXX|.XXX.|...X.|.XXX.|.XXX.|.X...",
      ".....|.....|.....|.....|.....|.....|.....|.....",

    /*   8     9     :     ;     <     =     >     ?   */
      ".XXX.|.XXX.|.....|.....|...X.|.....|.X...|.XXX.",
      "X...X|X...X|.XX..|.XX..|..X..|.....|..X..|X...X",
      "X...X|X...X|.XX..|.XX..|.X...|XXXXX|...X.|....X",
      ".XXX.|.XXXX|.....|.....|X....|.....|....X|...X.",
      "X...X|....X|.XX..|.XX..|.X...|XXXXX|...X.|..X..",
      "X...X|...X.|.XX..|..X..|..X..|.....|..X..|.....",
      ".XXX.|.XX..|.....|.X...|...X.|.....|.X...|..X..",
      ".....|.....|.....|.....|.....|.....|.....|.....",

    /*   @     A     B     C     D     E     F     G   */
      ".XXX.|.XXX.|XXXX.|.XXX.|XXX..|XXXXX|XXXXX|.XXX.",
      "X...X|X...X|X...X|X...X|X..X.|X....|X....|X...X",
      "X.XXX|X...X|X...X|X....|X...X|X....|X....|X....",
      "X.X.X|X...X|XXXX.|X....|X...X|XXXX.|XXXX.|X.XXX",
      "X.XXX|XXXXX|X...X|X....|X...X|X....|X....|X...X",
      "X....|X...X|X...X|X...X|X..X.|X....|X....|X...X",
      ".XXX.|X...X|XXXX.|.XXX.|XXX..|XXXXX|X....|.XXXX",
      ".....|.....|.....|.....|.....|.....|.....|.....",

    /*   H     I     J     K     L     M     N     O   */
      "X...X|.XXX.|..XXX|X...X|X....|X...X|X...X|.XXX.",
      "X...X|..X..|...X.|X..X.|X....|XX.XX|X...X|X...X",
      "X...X|..X..|...X.|X.X..|X....|X.X.X|XX..X|X...X",
      "XXXXX|..X..|...X.|XX...|X....|X.X.X|X.X.X|X...X",
      "X...X|..X..|...X.|X.X..|X....|X...X|X..XX|X...X",
      "X...X|..X..|X..X.|X..X.|X....|X...X|X...X|X...X",
      "X...X|.XXX.|.XX..|X...X|XXXXX|X...X|X...X|.XXX.",
      ".....|.....|.....|.....|.....|.....|.....|.....",

    /*   P     Q     R     S     T     U     V     W   */
      "XXXX.|.XXX.|XXXX.|.XXX.|XXXXX|X...X|X...X|X...X",
      "X...X|X...X|X...X|X...X|..X..|X...X|X...X|X...X",
      "X...X|X...X|X...X|X....|..X..|X...X|X...X|X...X",
      "XXXX.|X...X|XXXX.|.XXX.|..X..|X...X|X...X|X.X.X",
      "X....|X.X.X|X.X..|....X|..X..|X...X|X...X|X.X.X",
      "X....|X..X.|X..X.|X...X|..X..|X...X|.X.X.|X.X.X",
      "X....|.XX.X|X...X|.XXX.|..X..|.XXX.|..X..|.X.X.",
      ".....|.....|.....|.....|.....|.....|.....|.....",

    /*   X     Y     Z     [   +00A2   ]     ^     _   */
      "X...X|X...X|XXXXX|.XXX.|.....|.XXX.|..X..|.....",
      "X...X|X...X|....X|.X...|..X..|...X.|.X.X.|.....",
      ".X.X.|X...X|...X.|.X...|.XXX.|...X.|X...X|.....",
      "..X..|.X.X.|..X..|.X...|X.X..|...X.|.....|.....",
      ".X.X.|..X..|.X...|.X...|X.X.X|...X.|.....|.....",
      "X...X|..X..|X....|.X...|.XXX.|...X.|.....|.....",
      "X...X|..X..|XXXXX|.XXX.|..X..|.XXX.|.....|XXXXX",
      ".....|.....|.....|.....|.....|.....|.....|.....",

    /*   `     a     b     c     d     e     f     g   */
      ".X...|.....|X....|.....|....X|.....|..XX.|.....",
      "..X..|.....|X....|.....|....X|.....|.X..X|.....",
      ".....|.XXX.|X.XX.|.XXX.|.XX.X|.XXX.|.X...|.XXXX",
      ".....|....X|XX..X|X....|X..XX|X...X|XXX..|X...X",
      ".....|.XXXX|X...X|X....|X...X|XXXXX|.X...|.XXXX",
      ".....|X...X|X...X|X...X|X...X|X....|.X...|....X",
      ".....|.XXXX|XXXX.|.XXX.|.XXXX|.XXX.|.X...|.XXX.",
      ".....|.....|.....|.....|.....|.....|.....|.....",

    /*   h     i     j     k     l     m     n     o   */
      "X....|..X..|...X.|X....|.XX..|.....|.....|.....",
      "X....|.....|.....|X....|..X..|.....|.....|.....",
      "X.XX.|.XX..|..XX.|X..X.|..X..|XX.X.|X.XX.|.XXX.",
      "XX..X|..X..|...X.|X.X..|..X..|X.X.X|XX..X|X...X",
      "X...X|..X..|...X.|XX...|..X..|X.X.X|X...X|X...X",
      "X...X|..X..|X..X.|X.X..|..X..|X...X|X...X|X...X",
      "X...X|.XXX.|.XX..|X..X.|.XXX.|X...X|X...X|.XXX.",
      ".....|.....|.....|.....|.....|.....|.....|.....",

    /*   p     q     r     s     t     u     v     w   */
      ".....|.....|.....|.....|.X...|.....|.....|.....",
      ".....|.....|.....|.....|.X...|.....|.....|.....",
      "XXXX.|.XXXX|X.XX.|.XXX.|XXX..|X...X|X...X|X...X",
      "X...X|X...X|XX..X|X....|.X...|X...X|X...X|X...X",
      "XXXX.|.XXXX|X....|.XXX.|.X...|X...X|X...X|X.X.X",
      "X....|....X|X....|....X|.X..X|X..XX|.X.X.|X.X.X",
      "X....|....X|X....|XXXX.|..XX.|.XX.X|..X..|.X.X.",
      ".....|.....|.....|.....|.....|.....|.....|.....",

    /*   x     y     z   non-U non-U non-U +21B5 non-U */
      ".....|.....|.....|X.XXX|X.XXX|X.XXX|....X|.....",
      ".....|.....|.....|X.X.X|X...X|X.X..|..X.X|..XXX",
      "X...X|X...X|XXXXX|X.X.X|X.XXX|X.XXX|.X..X|.XXX.",
      ".X.X.|X...X|...X.|X.X.X|X.X..|X...X|XXXXX|XXX..",
      "..X..|.XXXX|..X..|X.XXX|X.XXX|X.XXX|.X...|X...X",
      ".X.X.|....X|.X...|.....|.....|.....|..X..|XXXX.",
      "X...X|.XXX.|XXXXX|.....|.....|.....|.....|.....",
      ".....|.....|.....|.....|.....|.....|.....|.....",

    /* +22EF non-U non-U non-U +25BA +25C4 +25BC +25B2 */
      ".....|.....|.....|.....|.....|.....|.....|.....",
      ".....|X....|X.X..|X.X.X|.X...|...X.|.....|.....",
      ".....|X....|X.X..|X.X.X|.XX..|..XX.|XXXXX|..X..",
      "X.X.X|X.X.X|X.X.X|X.X.X|.XXX.|.XXX.|.XXX.|.XXX.",
      ".....|X....|X.X..|X.X.X|.XX..|..XX.|..X..|XXXXX",
      ".....|X....|X.X..|X.X.X|.X...|...X.|.....|.....",
      ".....|.....|.....|.....|.....|.....|.....|.....",
      ".....|.....|.....|.....|.....|.....|.....|.....",

    /* +20BD non-U   -     {     }   non-U non-U non-U */
      "XXXX.|.....|.....|...X.|.X...|.XXX.|....X|XXXXX",
      "X...X|.....|.....|..X..|..X..|X...X|...XX|X...X",
      "X...X|XXXXX|.....|..X..|..X..|.XXX.|.XX.X|X...X",
      "XXXX.|X.X.X|.XXX.|.X...|...X.|..X..|.XX.X|X...X",
      "X....|..X..|.....|..X..|..X..|..X..|.XX.X|X.XXX",
      "X....|..X..|.....|..X..|..X..|.XX..|...XX|X.XX.",
      "X...X|..X.X|.....|...X.|.X...|.XX..|....X|XXX..",
      ".....|.....|.....|.....|.....|.....|.....|.....",

    /* +00B1 +2261 +25C6 +25C7 +2713 non-U +00B9 +00B2 */
      "..X..|.....|.....|.....|.....|..XX.|.X...|XXX..",
      "..X..|XXXXX|..X..|..X..|....X|.....|XX...|..X..",
      "XXXXX|.....|.XXX.|.X.X.|...X.|.XXX.|.X...|XXX..",
      "..X..|XXXXX|XXXXX|X...X|X.X..|..XX.|.X...|X....",
      "..X..|.....|.XXX.|.X.X.|.X...|..XX.|XXX..|XXX..",
      ".....|XXXXX|..X..|..X..|.....|..XX.|.....|.....",
      "XXXXX|.....|.....|.....|.....|.XXXX|.....|.....",
      ".....|.....|.....|.....|.....|.....|.....|.....",

    /* +00B3 +00B0 +20AC non-U non-U non-U non-U non-U */
      "XXX..|.XX..|..XX.|..XX.|..XX.|..XX.|..XX.|..XX.",
      "..X..|X..X.|.X..X|.XXXX|.XXXX|.XXXX|.XXXX|.XXXX",
      ".X...|X..X.|XXX..|.XXXX|.X..X|.X..X|.X..X|.X..X",
      "..X..|.XX..|.X...|.XXXX|.XXXX|.X..X|.X..X|.X..X",
      "XXX..|.....|XXX..|.XXXX|.XXXX|.XXXX|.X..X|.X..X",
      ".....|.....|.X..X|.XXXX|.XXXX|.XXXX|.XXXX|.X..X",
      ".....|.....|..XX.|.XXXX|.XXXX|.XXXX|.XXXX|.XXXX",
      ".....|.....|.....|.....|.....|.....|.....|.....",

    /*   Be    Ge    Yo   Zhe    Ze    I   I kr.   El  */
      "XXXXX|XXXXX|.X.X.|X.X.X|XXXX.|X...X|..X..|...XX",
      "X....|X...X|.....|X.X.X|....X|X...X|X.X.X|..X.X",
      "XXXX.|X....|XXXXX|X.X.X|....X|X..XX|X...X|.X..X",
      "X...X|X....|X....|.XXX.|.XXX.|X.X.X|X..XX|.X..X",
      "X...X|X....|XXXX.|X.X.X|....X|XX..X|X.X.X|.X..X",
      "X...X|X....|X....|X.X.X|....X|X...X|XX..X|.X..X",
      "XXXX.|X....|XXXXX|X.X.X|XXXX.|X...X|X...X|X...X",
      ".....|.....|.....|.....|.....|.....|.....|.....",

    /*   Pe    U     Ef   Che   Sha  T.zn.   Y   E ob. */
      "XXXXX|X...X|..X..|X...X|X.X.X|XX...|X...X|.XXX.",
      "X...X|X...X|.XXX.|X...X|X.X.X|.X...|X...X|X...X",
      "X...X|X...X|X.X.X|X...X|X.X.X|.X...|X...X|....X",
      "X...X|.XXXX|X.X.X|X...X|X.X.X|.XXX.|XX..X|..XXX",
      "X...X|....X|X.X.X|.XXXX|X.X.X|.X..X|X.X.X|....X",
      "X...X|....X|.XXX.|....X|X.X.X|.X..X|X.X.X|X...X",
      "X...X|.XXX.|..X..|....X|XXXXX|.XXX.|XX..X|.XXX.",
      ".....|.....|.....|.....|.....|.....|.....|.....",

    /*   Yu    Ya    be    ve    ge    yo   zhe    ze  */
      "X..X.|.XXXX|...X.|.....|.....|.X.X.|.....|.....",
      "X.X.X|X...X|.XX..|.....|.....|.....|.....|.....",
      "X.X.X|X...X|X....|XXX..|XXXX.|.XXX.|X.X.X|XXX..",
      "XXX.X|.XXXX|XXXX.|X..X.|X..X.|X...X|X.X.X|...X.",
      "X.X.X|..X.X|X...X|XXX..|X....|XXXXX|.XXX.|.XX..",
      "X.X.X|.X..X|X...X|X..X.|X....|X....|X.X.X|...X.",
      "X..X.|X...X|.XXX.|XXX..|X....|.XXX.|X.X.X|XXX..",
      ".....|.....|.....|.....|.....|.....|.....|.....",

    /*   i   i kr.   ka    el    em    en    pe    te  */
      ".....|.X.X.|.....|.....|.....|.....|.....|.....",
      ".....|..X..|.....|.....|.....|.....|.....|.....",
      "X...X|X...X|X..X.|..XX.|X...X|X...X|XXXXX|XXXXX",
      "X..XX|X..XX|X.X..|.X.X.|XX.XX|X...X|X...X|..X..",
      "X.X.X|X.X.X|XX...|X..X.|X.X.X|XXXXX|X...X|..X..",
      "XX..X|XX..X|X.X..|X..X.|X...X|X...X|X...X|..X..",
      "X...X|X...X|X..X.|X..X.|X...X|X...X|X...X|..X..",
      ".....|.....|.....|.....|.....|.....|.....|.....",

    /*  che   sha  t.zn.   y   m.zn. e ob.   yu    ya  */
      ".....|.....|.....|.....|.....|.....|.....|.....",
      ".....|.....|.....|.....|.....|.....|.....|.....",
      "X...X|X.X.X|XX...|X...X|X....|.XXX.|X..X.|.XXXX",
      "X...X|X.X.X|.X...|X...X|X....|X...X|X.X.X|X...X",
      ".XXXX|X.X.X|.XXX.|XX..X|XXX..|..XXX|XXX.X|.XXXX",
      "....X|X.X.X|.X..X|X.X.X|X..X.|X...X|X.X.X|..X.X",
      "....X|XXXXX|.XXX.|XX..X|XXX..|.XXX.|X..X.|.X..X",
      ".....|.....|.....|.....|.....|.....|.....|.....",

    /* +00AB +00BB +201E +201C +2116 +00BF +0192 +00A3 */
      ".....|.....|.....|XX.XX|X..X.|..X..|...XX|..XX.",
      ".....|.....|.....|.X..X|XX.X.|.....|..X..|.X..X",
      "..X..|..X..|.....|X..X.|X.XX.|..X..|..X..|.X...",
      ".X..X|X..X.|.....|.....|X..X.|.X...|.XXX.|XXXX.",
      "X..X.|.X..X|.X..X|.....|..XXX|X....|..X..|.X...",
      ".X..X|X..X.|X..X.|.....|..X.X|X...X|..X..|.X...",
      "..X..|..X..|XX.XX|.....|..XXX|.XXX.|XX...|XXXXX",
      ".....|.....|.....|.....|.....|.....|.....|.....",

    /* non-U non-U non-U non-U non-U non-U non-U +2160 */
      ".....|.....|.....|.....|X.X..|.....|X.X..|.XXX.",
      ".....|.....|.....|.....|.....|XX.XX|.X...|..X..",
      ".....|.....|.X.X.|.X.X.|XXX.X|.XXX.|X.X.X|..X..",
      ".....|..X..|.X.X.|.X.X.|...X.|..X..|...X.|..X..",
      ".....|..X..|.X.X.|.X.X.|..X..|.XXX.|..X..|..X..",
      "..X..|..X..|.....|.....|.X...|XX.XX|.X...|.XXX.",
      "..X..|..X..|.XXX.|.X.X.|X....|.....|X....|.....",
      ".....|.....|.....|.....|.....|.....|.....|.....",

    /* +2161 +2191 +2193 non-U non-U non-U non-U +00B7 */
      "XXXXX|..X..|..X..|X....|....X|X....|X....|.....",
      ".X.X.|.XXX.|..X..|X..X.|.X..X|X...X|XXX..|.....",
      ".X.X.|X.X.X|..X..|X.XX.|.XX.X|X.X.X|X...X|..XX.",
      ".X.X.|..X..|..X..|XXXXX|XXXXX|XXXXX|...X.|..XX.",
      ".X.X.|..X..|X.X.X|X.XX.|.XX.X|X.X.X|..X..|.....",
      "XXXXX|..X..|.XXX.|X..X.|.X..X|X...X|.X...|.....",
      ".....|..X..|..X..|X....|....X|X....|X....|.....",
      ".....|.....|.....|.....|.....|.....|.....|.....",

    /*   De   Tse  Shcha   de    ef   tse  shcha +00B4 */
      "..XX.|X..X.|X.X.X|.....|.....|.....|.....|...X.",
      ".X.X.|X..X.|X.X.X|.....|.....|.....|.....|..X..",
      ".X.X.|X..X.|X.X.X|..XX.|..X..|X..X.|X.X.X|.....",
      ".X.X.|X..X.|X.X.X|.X.X.|.XXX.|X..X.|X.X.X|.....",
      ".X.X.|X..X.|X.X.X|.X.X.|X.X.X|X..X.|X.X.X|.....",
      ".X.X.|X..X.|X.X.X|.X.X.|X.X.X|X..X.|X.X.X|.....",
      "XXXXX|XXXXX|XXXXX|XXXXX|.XXX.|XXXXX|XXXXX|.....",
      "X...X|....X|....X|X...X|..X..|....X|....X|.....",

    /* +00A8   ~   +00E9 +00E7 +0133 non-U non-U +25CB */
      ".X.X.|.X..X|...X.|.....|.X.X.|..X..|.....|.....",
      ".....|X.XX.|..X..|.....|.....|.XXX.|.X.X.|.XXX.",
      ".....|.....|.XXX.|.XXX.|.X.X.|.XXX.|.....|X...X",
      ".....|.....|X...X|X....|.X.X.|.XXX.|X...X|X...X",
      ".....|.....|XXXX.|X...X|.X.X.|XXXXX|.....|X...X",
      ".....|.....|X....|.XXX.|...X.|..X..|.X.X.|.XXX.",
      ".....|.....|.XXX.|..X..|.XX..|.....|.....|.....",
      ".....|.....|.....|.XX..|.....|.....|.....|.....",

    /* +00BC +2153 +00BD +00BE non-U non-U non-U non-U */
      "X...X|X...X|X...X|XX...|X...X|XX...|.X...|.X...",
      "X..X.|X..X.|X..X.|.X...|XXXXX|.....|X.X..|.XXX.",
      "X.X.X|X.XXX|X.XXX|XX...|X...X|...XX|XXX..|.X...",
      ".X.XX|.X..X|.X..X|.X..X|XXXXX|.....|X.X.X|.XXXX",
      "X.X.X|X..XX|X..XX|XX.XX|X...X|.XX..|....X|.X...",
      "..XXX|....X|..X..|..X.X|XXXXX|.....|..X.X|.XXX.",
      "....X|..XXX|..XXX|..XXX|X...X|.....|.XXXX|.X...",
      ".....|.....|.....|....X|.....|.....|..X..|XXX..",

    /* non-U non-U non-U non-U non-U +00A7 +00B6 +2588 */
      "..X..|.....|...X.|..X..|...X.|..XX.|.XXXX|XXXXX",
      ".XXX.|.XX.X|.XXX.|.XXX.|X..X.|.X..X|X.X.X|XXXXX",
      "..X..|.....|...X.|..X..|.X.X.|..X..|X.X.X|XXXXX",
      "XXXXX|XXX.X|XXXX.|XXXXX|..XX.|.X.X.|.XXXX|XXXXX",
      "..X..|.....|...X.|..X..|.X.X.|.X.X.|..X.X|XXXXX",
      ".XXX.|.XX.X|.XXX.|.XXX.|X..X.|..X..|..X.X|XXXXX",
      "..X..|...X.|...X.|..X..|...X.|X..X.|..X.X|XXXXX",
      ".XXX.|..XXX|..XXX|.....|..XXX|.XX..|.....|XXXXX",
};

static const unsigned int lcd_font_cyr_unicode[] = {
    /* 0x08 */ 0x042c, 0x00b5, 0x045e, 0x0000, 0x0407, 0x0457, 0x0404, 0x0454,
    /* 0x10 */ 0x2026, 0x203c, 0x00f7, 0x2192, 0x2190, 0x005c, 0x0000, 0x2116,
    /* 0x18 */ 0x00ae, 0x00a9, 0x2264, 0x2265, 0x221a, 0x00a5, 0x2260, 0x2248,
    /* 0x20 */ 0x0020, 0x0021, 0x0022, 0x0023, 0x0024, 0x0025, 0x0026, 0x0027,
    /* 0x28 */ 0x0028, 0x0029, 0x002a, 0x002b, 0x002c, 0x002d, 0x002e, 0x002f,
    /* 0x30 */ 0x0030, 0x0031, 0x0032, 0x0033, 0x0034, 0x0035, 0x0036, 0x0037,
    /* 0x38 */ 0x0038, 0x0039, 0x003a, 0x003b, 0x003c, 0x003d, 0x003e, 0x003f,
    /* 0x40 */ 0x0040, 0x0041, 0x0042, 0x0043, 0x0044, 0x0045, 0x0046, 0x0047,
    /* 0x48 */ 0x0048, 0x0049, 0x004a, 0x004b, 0x004c, 0x004d, 0x004e, 0x004f,
    /* 0x50 */ 0x0050, 0x0051, 0x0052, 0x0053, 0x0054, 0x0055, 0x0056, 0x0057,
    /* 0x58 */ 0x0058, 0x0059, 0x005a, 0x005b, 0x00a2, 0x005d, 0x005e, 0x005f,
    /* 0x60 */ 0x0060, 0x0061, 0x0062, 0x0063, 0x0064, 0x0065, 0x0066, 0x0067,
    /* 0x68 */ 0x0068, 0x0069, 0x006a, 0x006b, 0x006c, 0x006d, 0x006e, 0x006f,
    /* 0x70 */ 0x0070, 0x0071, 0x0072, 0x0073, 0x0074, 0x0075, 0x0076, 0x0077,
    /* 0x78 */ 0x0078, 0x0079, 0x007a, 0x0000, 0x0000, 0x0000, 0x21b5, 0x0000,
    /* 0x80 */ 0x22ef, 0x0000, 0x0000, 0x0000, 0x25ba, 0x25c4, 0x25bc, 0x25b2,
    /* 0x88 */ 0x20bd, 0x0000, 0x002d, 0x007b, 0x007d, 0x0000, 0x0000, 0x0000,
    /* 0x90 */ 0x00b1, 0x2261, 0x25c6, 0x25c7, 0x2713, 0x0000, 0x00b9, 0x00b2,
    /* 0x98 */ 0x00b3, 0x00b0, 0x20ac, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    /* 0xa0 */ 0x0411, 0x0413, 0x0401, 0x0416, 0x0417, 0x0418, 0x0419, 0x041b,
    /* 0xa8 */ 0x041f, 0x0423, 0x0424, 0x0427, 0x0428, 0x042a, 0x042b, 0x042d,
    /* 0xb0 */ 0x042e, 0x042f, 0x0431, 0x0432, 0x0433, 0x0451, 0x0436, 0x0437,
    /* 0xb8 */ 0x0438, 0x0439, 0x043a, 0x043b, 0x043c, 0x043d, 0x043f, 0x0442,
    /* 0xc0 */ 0x0447, 0x0448, 0x044a, 0x044b, 0x044c, 0x044d, 0x044e, 0x044f,
    /* 0xc8 */ 0x00ab, 0x00bb, 0x201e, 0x201c, 0x2116, 0x00bf, 0x0192, 0x00a3,
    /* 0xd0 */ 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x2160,
    /* 0xd8 */ 0x2161, 0x2191, 0x2193, 0x0000, 0x0000, 0x0000, 0x0000, 0x00b7,
    /* 0xe0 */ 0x0414, 0x0426, 0x0429, 0x0434, 0x0444, 0x0446, 0x0449, 0x00b4,
    /* 0xe8 */ 0x00a8, 0x007e, 0x00e9, 0x00e7, 0x0133, 0x0000, 0x0000, 0x25cb,
    /* 0xf0 */ 0x00bc, 0x2153, 0x00bd, 0x00be, 0x0000, 0x0000, 0x0000, 0x0000,
    /* 0xf8 */ 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x00a7, 0x00b6, 0x2588,
};

/*
 * Local variables:
 * mode: C
 * c-file-style: "Linux"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */

/*
 * sfn_cp.h
 *
 * How each character is written in the short names of a drive made from a
 * directory, by the code page and the system that would have copied the
 * files, and what a short name's bytes read as: the tables of sfn_cp.c.
 *
 * Written & released by Mike Shevchenko <mike.shevchenko@gmail.com>
 *
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

/* Flags of a character in a short name. */
#define SFN_INEXACT 1  /* the name takes a numeric tail for it */
#define SFN_LOSSY 2  /* written as another character, or not at all */
#define SFN_REFUSED 4  /* the system cannot copy a name that holds it */

/* A character other than ASCII as one table writes it: its byte, 0 if it is
 * dropped, and its SFN_* flags. */
struct sfn_map {
    uint16_t u;
    uint8_t b, flags;
};

/* The characters of one code page and system, by code point. One that the
 * table lacks is dropped if @drop, else written as '_', and the name takes
 * a numeric tail either way. */
struct sfn_table {
    const struct sfn_map *map;
    unsigned int nr;
    bool drop;
};

extern const struct sfn_table sfn_437_windows, sfn_850_windows, sfn_852_windows;
extern const struct sfn_table sfn_866_windows;
extern const struct sfn_table sfn_437_linux, sfn_850_linux, sfn_852_linux;
extern const struct sfn_table sfn_866_linux, sfn_mik;

/* The characters that Windows writes in UTF-8 short names as another, its
 * capital, by code point. */
extern const uint16_t sfn_utf8_upper[][2];
extern const unsigned int sfn_utf8_upper_nr;

/* What each byte from 0x80 of a short name reads as in code pages 437, 850,
 * 852, 866 and MIK, by code point. */
extern const uint16_t sfn_437_uni[128], sfn_850_uni[128], sfn_852_uni[128];
extern const uint16_t sfn_866_uni[128], sfn_mik_uni[128];

/*
 * Local variables:
 * mode: C
 * c-file-style: "Linux"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */

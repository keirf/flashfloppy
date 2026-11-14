/*
 * test_names.c
 *
 * Names of files and folders as FatFS gives them to the firmware, in the
 * character set of inc/charset.h, opened again by those names, as the
 * firmware opens an image or enters a folder; and the set's conversions and
 * order.
 *
 * Written & released by Mike Shevchenko <mike.shevchenko@gmail.com>
 *
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

#include "harness.h"

/* AGAT in Cyrillic capitals in the set, and as the short name that a
 * Russian MS-DOS or Windows writes, in code page 866, and that a Windows
 * writes whose code page is UTF-8. 866's capitals are MIK's. */
#define AGAT "\xe1\xe7\xe1\xf4"
#define AGAT_866 "\x80\x83\x80\x92"
#define AGAT_UTF8 "\xd0\x90\xd0\x93\xd0\x90\xd0\xa2"

/* Lists the root folder, which must hold names @want of @nr in that order,
 * then opens or enters each by its name. */
static void check_listed(const char * const *want, unsigned int nr)
{
    DIR dp;
    FILINFO fno;
    FIL f;
    unsigned int i;

    drive_mount();
    REQUIRE(f_opendir(&dp, "") == FR_OK);
    for (i = 0; i < nr; i++) {
        REQUIRE((f_readdir(&dp, &fno) == FR_OK) && (fno.fname[0] != '\0'));
        CHECK_STR(fno.fname, want[i]);
        if (fno.fattrib & AM_DIR) {
            CHECK_EQ(f_chdir(want[i]), FR_OK);
            CHECK_EQ(f_chdir("/"), FR_OK);
        } else {
            CHECK_EQ(f_open(&f, want[i], FA_READ), FR_OK);
            f_close(&f);
        }
    }
    CHECK((f_readdir(&dp, &fno) == FR_OK) && (fno.fname[0] == '\0'));
    f_closedir(&dp);
}

TEST(names_short)
{
    static const struct {
        uint8_t codepage;
        bool_t folder;
        const char *raw, *want;
    } t[] = {
        { CODEPAGE_866, FALSE, AGAT_866 ".DSK", AGAT ".DSK" },
        { CODEPAGE_866, TRUE, AGAT_866, AGAT },
        { CODEPAGE_mik, FALSE, AGAT_866 ".DSK", AGAT ".DSK" },
        /* Capital Yo: YOLKA, with Yo read as Ye. */
        { CODEPAGE_866, FALSE, "\xf0\x8b\x8a\x80", "\xe5\xec\xeb\xe1" },
        /* A capital, then MIK's small r, which 866 lacks there. */
        { CODEPAGE_866, FALSE, "\x80\xb0", "\xe1?" },
        { CODEPAGE_mik, FALSE, "\x80\xb0", "\xe1\xd2" },
        /* KASE with A umlaut, as a German MS-DOS writes it. */
        { CODEPAGE_437, FALSE, "K\x8eSE.IMG", "K\x82SE.IMG" },
        { CODEPAGE_866, FALSE, "K\x8eSE.IMG", "K\xefSE.IMG" },
        /* 437's sharp S; 437 and 850 share the pound cell. */
        { CODEPAGE_437, FALSE, "\x9c\xe1.IMG", "\x9f\xbf.IMG" },
        { CODEPAGE_850, FALSE, "\x9c.IMG", "\x9f.IMG" },
        /* CP852's dedicated letters, currency and shared glyphs. */
        { CODEPAGE_852, FALSE,
          "\xc6\xa4\x8f\xa8\x95\x9d\xe3\xe8.IMG",
          "\x02\x03\x04\x05\x06\x07\x08\x09.IMG" },
        { CODEPAGE_852, FALSE,
          "\xfc\x97\xb8\xdd\xde\x8d\xbd\xcf.IMG",
          "\x0a\x0b\x0c\x0d\x0e\x0f\x10\x01.IMG" },
        { CODEPAGE_852, FALSE,
          "\xc7\xa5\x86\xa9\x96\x88\xe4\xea.IMG",
          "\x11\x12\x13\x14\x15\x16\x17\x18.IMG" },
        { CODEPAGE_852, FALSE,
          "\xac\x9f\xe6\xe7\xa6\xa7\xd2\xd4.IMG",
          "\x04\x13\x0b\x1a\x10\x1f" "Dd.IMG" },
        { CODEPAGE_852, FALSE,
          "\x9b\x9c\xd5\xe5\xb7\xd8\x91\x92.IMG",
          "TtNnEeLl.IMG" },
        { CODEPAGE_852, FALSE,
          "\x8a\x8b\xeb\xfb\xd1\xd0\xf3\xfe.IMG",
          "\x94\xb4\x9b\xbb\x88\xa8??.IMG" },
        /* Every Latin letter added from 850, in 850's byte order. */
        { CODEPAGE_850, FALSE,
          "\x9b\xc6\x9d\xc7\xd0\xd1\xd2\xd3.DSK",
          "\xb6\xa5\x96\x85\xa8\x88\x8a\x8b.DSK" },
        { CODEPAGE_850, FALSE,
          "\xd4\xd5\xd6\xd7\xd8\xde\xe0\xe1.DSK",
          "\x8c\x9e\x8d\x8e\x8f\x90\x92\xbf.DSK" },
        { CODEPAGE_850, FALSE,
          "\xe2\xe3\xe4\xe5\xb5\xb6\xb7\xe7.DSK",
          "\x93\x95\xb7\x97\x80\x81\x83\xb8.DSK" },
        { CODEPAGE_850, FALSE,
          "\xe8\xe9\xea\xeb\xec\xed.DSK", "\x98\x99\x9a\x9c\xbd\x9d.DSK" },
        /* UTF-8, whatever the code page. */
        { CODEPAGE_866, FALSE, AGAT_UTF8 ".DSK", AGAT ".DSK" },
        { CODEPAGE_437, TRUE, AGAT_UTF8, AGAT },
        { CODEPAGE_mik, FALSE, "CAF\xc3\x89", "CAF\x89" },
        /* Not well-formed UTF-8, overlong and a surrogate: 866's. */
        { CODEPAGE_866, FALSE, "\xe0\x80\x80", "\xd2\xe1\xe1" },
        { CODEPAGE_866, FALSE, "\xed\xa0\x80", "\xdc\xc1\xe1" },
        /* Capital Yo and three capitals in 866, YORAA, which as UTF-8
         * would be a character outside the Basic Multilingual Plane. */
        { CODEPAGE_866, FALSE, "\xf0\x90\x80\x80", "\xe5\xf2\xe1\xe1" },
        /* 437's STRASSE with A and O umlauts after the sharp s: it passes
         * for UTF-8, a character outside the set. */
        { CODEPAGE_437, FALSE, "STRA\xe1\x8e\x99", "STRA?" },
    }, off[] = {
        /* With detection off, UTF-8 in 866: box drawing and letters. */
        { CODEPAGE_866, FALSE, AGAT_UTF8 ".DSK", "?\xf2?\xf5?\xf2?\xd7.DSK" },
        { CODEPAGE_437, FALSE, "STRA\xe1\x8e\x99", "STRA\xbf\x82\x94" },
    };
    FIL f;
    unsigned int i;

    for (i = 0; i < ARRAY_SIZE(t); i++) {
        drive_reset();
        ff_cfg.short_name_codepage = t[i].codepage;
        if (t[i].folder)
            drive_add_folder(t[i].raw, NULL);
        else
            drive_add(t[i].raw, "x", 1);
        check_listed(&t[i].want, 1);
    }
    for (i = 0; i < ARRAY_SIZE(off); i++) {
        drive_reset();
        ff_cfg.short_name_codepage = off[i].codepage;
        ff_cfg.detect_utf8_in_short_names = FALSE;
        drive_add(off[i].raw, "x", 1);
        check_listed(&off[i].want, 1);
    }

    /* A short name, as it reads, in the other letter case. */
    drive_reset();
    drive_add(AGAT_866 ".DSK", "x", 1);
    drive_mount();
    CHECK_EQ(f_open(&f, "\xc1\xc7\xc1\xd4.dsk", FA_READ), FR_OK);
    f_close(&f);

    drive_reset();
}

TEST(names_long)
{
    static const char * const want[] = {
        "\xe9\xc7\xd2\xc1.dsk", "\xe1\xc7\xc1\xd4", "K\xa2se.img",
        "\x07\xb2" "d\x1e.dsk", "\xe5\xcc\xcb\xc1.dsk", "??.dsk"
    };
    FIL f;
    char c;
    UINT nr;

    drive_reset();
    /* Igra, Agat, Kase with a umlaut, Lodz with L stroke, o acute and z
     * acute, Yolka, and an emoji outside the Basic Multilingual
     * Plane, which reads as two '?' and opens by them. */
    drive_add_long("IGRA~1.DSK", "\xd0\x98\xd0\xb3\xd1\x80\xd0\xb0.dsk",
                   "x", 1);
    drive_add_folder("AGAT~1", "\xd0\x90\xd0\xb3\xd0\xb0\xd1\x82");
    drive_add_long("KASE~1.IMG", "K\xc3\xa4se.img", "x", 1);
    drive_add_long("LODZ~1.DSK", "\xc5\x81\xc3\xb3" "d\xc5\xba.dsk", "x", 1);
    drive_add_long("ELKA~1.DSK", "\xd0\x81\xd0\xbb\xd0\xba\xd0\xb0.dsk",
                   "x", 1);
    drive_add_long("SMILE~1.DSK", "\xf0\x9f\x98\x80.dsk", "x", 1);
    check_listed(want, ARRAY_SIZE(want));

    /* In the other letter case: IGRA.DSK, KASE.IMG, YOLKA.DSK. */
    CHECK_EQ(f_open(&f, "\xe9\xe7\xf2\xe1.DSK", FA_READ), FR_OK);
    f_close(&f);
    CHECK_EQ(f_open(&f, "K\x82SE.IMG", FA_READ), FR_OK);
    f_close(&f);
    CHECK_EQ(f_open(&f, "\xe5\xec\xeb\xe1.DSK", FA_READ), FR_OK);
    f_close(&f);
    /* '?' is a name's character, matching none that is not there. */
    CHECK_EQ(f_open(&f, "NO?.DSK", FA_READ), FR_NO_FILE);

    /* La and Sa, with L stroke and S acute, now have cells of their own. */
    drive_reset();
    drive_add_long("LA~1.DSK", "\xc5\x81" "a.dsk", "1", 1);
    drive_add_long("SA~1.DSK", "\xc5\x9a" "a.dsk", "2", 1);
    drive_mount();
    REQUIRE(f_open(&f, "\x07" "a.dsk", FA_READ) == FR_OK);
    CHECK((f_read(&f, &c, 1, &nr) == FR_OK) && (nr == 1) && (c == '1'));
    f_close(&f);
    REQUIRE(f_open(&f, "\x0b" "a.dsk", FA_READ) == FR_OK);
    CHECK((f_read(&f, &c, 1, &nr) == FR_OK) && (nr == 1) && (c == '2'));
    f_close(&f);

    drive_reset();
}

TEST(names_conversions)
{
    static const uint16_t central[] = {
        0x0102, 0x0104, 0x0106, 0x0118, 0x013d,
        0x0141, 0x0143, 0x0154, 0x0158, 0x015a,
        0x015e, 0x0162, 0x016e, 0x0179, 0x017b,
        0x0103, 0x0105, 0x0107, 0x0119, 0x013e,
        0x0142, 0x0144, 0x0155, 0x0159, 0x015b,
        0x015f, 0x0163, 0x016f, 0x017a, 0x017c
    };
    static const uint16_t aliases[][2] = {
        { 0x010c, 0x0106 }, { 0x010d, 0x0107 },
        { 0x0160, 0x015a }, { 0x0161, 0x015b },
        { 0x017d, 0x017b }, { 0x017e, 0x017c },
        { 0x010e, 'D' }, { 0x010f, 'd' },
        { 0x0164, 'T' }, { 0x0165, 't' },
        { 0x0147, 'N' }, { 0x0148, 'n' },
        { 0x011a, 'E' }, { 0x011b, 'e' },
        { 0x0139, 'L' }, { 0x013a, 'l' },
        { 0x0110, 0x00d0 }, { 0x0111, 0x00f0 },
        { 0x0150, 0x00d6 }, { 0x0151, 0x00f6 },
        { 0x0170, 0x00dc }, { 0x0171, 0x00fc },
        { 0x0218, 0x015e }, { 0x0219, 0x015f },
        { 0x021a, 0x0162 }, { 0x021b, 0x0163 }
    };
    static const uint16_t removed[] = {
        0x00a2, 0x00a5, 0x20a7, 0x0192, 0x00aa, 0x00ba,
        0x00bf, 0x2310, 0x00ac, 0x00bd, 0x00bc, 0x00a1, 0x00ab, 0x00bb,
        0x02c7, 0x25a0
    };
    static const uint8_t yo_dir[11] = {
        0xf0, ' ', ' ', ' ', ' ', ' ', ' ', ' ', 'D', 'S', 'K'
    };
    static const uint16_t yo_russian[] = { 0x401, '.', 'D', 'S', 'K', 0 };
    static const uint16_t ye_russian[] = { 0x415, '.', 'D', 'S', 'K', 0 };
    unsigned int c, i;

    /* Every assigned character of the set to Unicode and back. */
    for (c = 1; c < 0x100; c++)
        CHECK_EQ(ff_uni2oem(ff_oem2uni(c, 0), 0), c);
    for (i = 0; i < ARRAY_SIZE(central); i++) {
        CHECK_EQ(ff_oem2uni(0x02 + i, 0), central[i]);
        CHECK_EQ(ff_uni2oem(central[i], 0), 0x02 + i);
        CHECK_EQ(ff_uni2oem(ff_wtoupper(central[i]), 0), 0x02 + i % 15);
    }
    for (i = 0; i < ARRAY_SIZE(aliases); i++) {
        CHECK_EQ(ff_uni2oem(aliases[i][0], 0),
                 ff_uni2oem(aliases[i][1], 0));
        CHECK(charset_uni_match(aliases[i][0], aliases[i][1]));
    }
    for (i = 0; i < ARRAY_SIZE(removed); i++)
        CHECK_EQ(ff_uni2oem(removed[i], 0), '?');
    /* The Russian alphabet and back, and its letter case; Yo shares Ye. */
    for (i = 0x410; i < 0x450; i++) {
        CHECK_EQ(ff_oem2uni(ff_uni2oem(i, 0), 0), i);
        CHECK_EQ(ff_wtoupper(i), (i < 0x430) ? i : i - 0x20);
    }
    CHECK_EQ(ff_uni2oem(0x401, 0), 0xe5);
    CHECK_EQ(ff_oem2uni(0xe5, 0), 0x415);
    CHECK_EQ(ff_uni2oem(0x451, 0), 0xc5);
    CHECK_EQ(ff_oem2uni(0xc5, 0), 0x435);
    CHECK_EQ(ff_wtoupper(0x451), 0x401);
    CHECK_EQ(ff_wtoupper(0xe4), 0xc4);
    CHECK_EQ(ff_wtoupper(0x131), 'I');
    CHECK_EQ(ff_uni2oem(0xa4, 0), 0x01);
    CHECK_EQ(ff_oem2uni(0x01, 0), 0xa4);
    CHECK_EQ(charset_to_rom(0x9f, LCDCS_jap), 0x9c);
    CHECK_EQ(charset_to_rom(0x9f, LCDCS_eur), 0xa3);
    CHECK_EQ(charset_to_rom(0x9f, LCDCS_cyr), 0xcf);
    CHECK_EQ(charset_to_rom(0x9f, CHARSET_osd), '?');
    CHECK_EQ(ff_uni2oem(0x141, 0), 0x07);
    ff_cfg.short_name_codepage = CODEPAGE_866;
    CHECK(charset_sfn_match(yo_dir, yo_russian));
    CHECK(charset_sfn_match(yo_dir, ye_russian));
    /* A surrogate pair reads as two '?'. */
    CHECK_EQ(ff_uni2oem(0xd83dde00, 0), 0x3f3f);
}

TEST(names_key)
{
    unsigned int i, cap;

    CHECK_EQ(charset_key('A'), charset_key('a'));
    CHECK_EQ(charset_key(0x01), 0x01);
    for (i = 0; i < 15; i++) {
        cap = 0x02 + i;
        CHECK((charset_key(cap) >= 'a') && (charset_key(cap) <= 'z'));
        CHECK_EQ(charset_key(cap), charset_key(cap + 15));
    }
    CHECK_EQ(charset_key(0x02), 'a');
    CHECK_EQ(charset_key(0x07), 'l');
    CHECK_EQ(charset_key(0x0b), 's');
    CHECK_EQ(charset_key(0x10), 'z');
    /* Every Latin letter sorts by its base, small and capital alike. */
    for (i = 0x80; i <= 0xbf; i++) {
        if (i == 0x9f) {
            CHECK_EQ(charset_key(i), i);
            continue;
        }
        cap = ff_uni2oem(ff_wtoupper(ff_oem2uni(i, 0)), 0);
        CHECK((charset_key(i) >= 'a') && (charset_key(i) <= 'z'));
        if (cap != '?')
            CHECK_EQ(charset_key(i), charset_key(cap));
    }
    CHECK_EQ(charset_key(0x86), 'a');
    CHECK_EQ(charset_key(0x96), 'o');
    CHECK_EQ(charset_key(0x88), 'd');
    CHECK_EQ(charset_key(0x9e), 'i');
    CHECK_EQ(charset_key(0xbf), 's');
    CHECK_EQ(charset_key(0x98), 'p');
    CHECK_EQ(charset_key(0xb8), 'p');
    for (i = 0; i < 32; i++) {
        cap = ff_uni2oem(0x410 + i, 0);
        CHECK_EQ(charset_key(cap), charset_key(ff_uni2oem(0x430 + i, 0)));
        if (i > 0)
            CHECK(charset_key(ff_uni2oem(0x410 + i - 1, 0))
                  < charset_key(cap));
    }
    /* Yo sorts as Cyrillic Ye, whose cell it shares. */
    CHECK_EQ(charset_key(ff_uni2oem(0x401, 0)),
             charset_key(ff_uni2oem(0x415, 0)));
    CHECK(charset_key('z') < charset_key(ff_uni2oem(0x410, 0)));
}

/*
 * Local variables:
 * mode: C
 * c-file-style: "Linux"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */

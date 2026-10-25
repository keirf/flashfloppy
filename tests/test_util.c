/*
 * test_util.c
 *
 * The firmware's own C library and helpers.
 *
 * Written & released by Mike Shevchenko <mike.shevchenko@gmail.com>
 *
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

#include "harness.h"

TEST(util_crc16_ccitt)
{
    /* The check value of CRC-16/CCITT-FALSE. */
    CHECK_EQ(crc16_ccitt("123456789", 9, 0xffff), 0x29b1);
}

/* Every code that can occur has a hint that fits beside it on 16 columns. */
TEST(util_fresult_hint)
{
    FRESULT fr;
    bool_t occurs;

    for (fr = FR_OK; fr <= FR_PATH_TOO_DEEP; fr++) {
        occurs = ((fr >= FR_DISK_ERR) && (fr <= FR_NO_FILESYSTEM))
            || (fr == FR_NOT_ENOUGH_CORE) || (fr == FR_INVALID_PARAMETER)
            || (fr >= FR_DISK_FULL);
        CHECK_EQ(strlen(fresult_hint(fr)) > 0, occurs);
        CHECK(strlen(fresult_hint(fr)) <= 12);
    }
    CHECK_STR(fresult_hint(FR_BAD_IMAGE), "Bad image");
    CHECK_STR(fresult_hint(FR_NO_FILESYSTEM), "Not FAT disk");
    CHECK_STR(fresult_hint(FR_PATH_TOO_DEEP + 1), "");
}

TEST(util_snprintf)
{
    char s[32];

    snprintf(s, sizeof(s), "%03u/%03u", 5, 123);
    CHECK_STR(s, "005/123");
    /* A string is padded on its right, a number on its left. */
    snprintf(s, sizeof(s), "[%3s]", "A");
    CHECK_STR(s, "[A  ]");
    snprintf(s, sizeof(s), "[%*s]", 4, "");
    CHECK_STR(s, "[    ]");
    snprintf(s, sizeof(s), "%x %02x %d", 0xbeef, 7, -12);
    CHECK_STR(s, "beef 07 -12");
    /* A % in an argument is no conversion. */
    snprintf(s, sizeof(s), "%s", "A%B 100%");
    CHECK_STR(s, "A%B 100%");
    /* Cut at the buffer's end, terminated. */
    snprintf(s, 6, "%s", "123456789");
    CHECK_STR(s, "12345");
}

TEST(util_filename_extension)
{
    char ext[8];

    filename_extension("Game.Disk.DSK", ext, sizeof(ext));
    CHECK_STR(ext, "dsk");
    filename_extension("README", ext, sizeof(ext));
    CHECK_STR(ext, "");
    filename_extension("a.VeryLongExt", ext, 4);
    CHECK_STR(ext, "ver");
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

/*
 * test_fatimg.c
 *
 * ffemu's USB drive made from a directory, FAT32 or FAT16, as the
 * firmware's FatFS reads it.
 *
 * Written & released by Mike Shevchenko <mike.shevchenko@gmail.com>
 *
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

#include "harness.h"
#include "dir_drive.h"

static uint8_t pattern[20000];
static uint8_t buf[sizeof(pattern)];

static void make_pattern(void)
{
    unsigned int i;

    for (i = 0; i < sizeof(pattern); i++)
        pattern[i] = i * 7 + i / 251;
}

/* Reads file @name of the mounted drive, which must hold @len bytes of
 * @data. */
static void check_file(const char *name, const void *data, uint32_t len)
{
    FIL f;
    UINT nr = 0;
    FRESULT fr;

    if ((fr = f_open(&f, name, FA_READ)) != FR_OK) {
        ft_fail(__FILE__, __LINE__, "f_open(%s) gave %d", name, fr);
        return;
    }
    fr = f_read(&f, buf, sizeof(buf), &nr);
    f_close(&f);
    CHECK_EQ(fr, FR_OK);
    CHECK_EQ(nr, len);
    if (nr == len)
        CHECK_MEM(buf, data, len);
}

/* The entries of the root folder of the mounted drive. */
static unsigned int root_entries(void)
{
    DIR dp;
    FILINFO fno;
    unsigned int nr = 0;

    if (f_opendir(&dp, "") != FR_OK)
        return 0;
    while ((f_readdir(&dp, &fno) == FR_OK) && (fno.fname[0] != '\0'))
        nr++;
    f_closedir(&dp);
    return nr;
}

TEST(fatimg_fat16_files)
{
    FATFS *fs;

    make_pattern();
    dir_drive_new();
    dir_drive_file("readme.txt", "hello\n", 6);
    dir_drive_file("Long File Name.dsk", pattern, sizeof(pattern));
    dir_drive_file("SUB/inner.img", pattern + 1, 5000);
    REQUIRE(dir_drive_insert(1) == 0);

    fs = drive_mount_from(dir_drive_read);
    REQUIRE(fs != NULL);
    CHECK_EQ(fs->fs_type, FS_FAT16);
    /* 4 KB clusters, as format makes them for this size. */
    CHECK_EQ(fs->csize, 8);
    CHECK_EQ(fs->n_rootdir, 512);
    check_file("readme.txt", "hello\n", 6);
    check_file("Long File Name.dsk", pattern, sizeof(pattern));
    check_file("SUB/inner.img", pattern + 1, 5000);
    CHECK_EQ(root_entries(), 3);

    drive_reset();
    dir_drive_remove();
}

TEST(fatimg_fat16_root_full)
{
    FATFS *fs;
    FILINFO fno;
    const char *first;
    char name[16];
    unsigned int i;

    /* An entry each: 511 of them fit beside the volume label. */
    dir_drive_new();
    for (i = 0; i < 600; i++) {
        snprintf(name, sizeof(name), "F%03u.TXT", i);
        dir_drive_file(name, name, 1);
    }
    REQUIRE(dir_drive_insert(1) == 0);
    CHECK_EQ(dir_drive_left_out(1, &first), 89);
    CHECK_STR(first, "F511.TXT");
    CHECK_EQ(dir_drive_left_out(0, &first), 0);

    fs = drive_mount_from(dir_drive_read);
    REQUIRE(fs != NULL);
    CHECK_EQ(fs->fs_type, FS_FAT16);
    CHECK_EQ(fs->n_rootdir, 512);
    CHECK_EQ(root_entries(), 511);
    check_file("F510.TXT", "F", 1);
    CHECK_EQ(f_stat("F511.TXT", &fno), FR_NO_FILE);

    drive_reset();
    dir_drive_remove();
}

TEST(fatimg_fat16_root_full_goes_on)
{
    FATFS *fs;
    FILINFO fno;
    const char *first;
    char name[16];
    unsigned int i;

    /* With 2 entries left, a long name, which takes 3, is left out, and
     * the copying goes on with the shorter ones after it. */
    dir_drive_new();
    for (i = 0; i < 509; i++) {
        snprintf(name, sizeof(name), "F%03u.TXT", i);
        dir_drive_file(name, name, 1);
    }
    dir_drive_file("G long name.txt", "G", 1);
    dir_drive_file("H.TXT", "H", 1);
    dir_drive_file("SUB/inner.txt", "I", 1);
    REQUIRE(dir_drive_insert(1) == 0);
    CHECK_EQ(dir_drive_left_out(1, &first), 1);
    CHECK_STR(first, "G long name.txt");

    fs = drive_mount_from(dir_drive_read);
    REQUIRE(fs != NULL);
    CHECK_EQ(root_entries(), 511);
    CHECK_EQ(f_stat("G long name.txt", &fno), FR_NO_FILE);
    check_file("H.TXT", "H", 1);
    check_file("SUB/inner.txt", "I", 1);

    drive_reset();
    dir_drive_remove();
}

TEST(fatimg_fat16_cluster_size)
{
    static const uint8_t zeros[512];
    FATFS *fs;
    FIL f;
    UINT nr = 0;

    /* 300 MB, too much for 65524 clusters of 4 KB. */
    dir_drive_new();
    dir_drive_file("big.img", NULL, 300ull << 20);
    dir_drive_file("small.txt", "small", 5);
    REQUIRE(dir_drive_insert(1) == 0);

    fs = drive_mount_from(dir_drive_read);
    REQUIRE(fs != NULL);
    CHECK_EQ(fs->fs_type, FS_FAT16);
    CHECK_EQ(fs->csize, 16);
    check_file("small.txt", "small", 5);
    /* The end of the big file, past the whole chain of its clusters. */
    REQUIRE(f_open(&f, "big.img", FA_READ) == FR_OK);
    CHECK_EQ(f_lseek(&f, (300ul << 20) - 512), FR_OK);
    CHECK_EQ(f_read(&f, buf, 512, &nr), FR_OK);
    f_close(&f);
    CHECK_EQ(nr, 512);
    CHECK_MEM(buf, zeros, 512);

    drive_reset();
    dir_drive_remove();
}

TEST(fatimg_fat16_drive_full)
{
    FATFS *fs;
    FILINFO fno;
    const char *first;

    /* 2500 MB, more than 65524 clusters of 32 KB: the second file finds
     * too little room, the third, after it, enough. */
    dir_drive_new();
    dir_drive_file("A/one.img", NULL, 1500ull << 20);
    dir_drive_file("A/two.img", NULL, 1000ull << 20);
    dir_drive_file("B.TXT", "B", 1);
    REQUIRE(dir_drive_insert(1) == 0);
    CHECK_EQ(dir_drive_left_out(0, &first), 1);
    CHECK_STR(first, "A/two.img");
    CHECK_EQ(dir_drive_left_out(1, &first), 0);

    fs = drive_mount_from(dir_drive_read);
    REQUIRE(fs != NULL);
    CHECK_EQ(fs->fs_type, FS_FAT16);
    CHECK_EQ(fs->csize, 64);
    /* The largest drive, as the files were copied to. */
    CHECK_EQ(fs->n_fatent - 2, 65524);
    CHECK_EQ(f_stat("A/one.img", &fno), FR_OK);
    CHECK_EQ(fno.fsize, 1500ul << 20);
    CHECK_EQ(f_stat("A/two.img", &fno), FR_NO_FILE);
    check_file("B.TXT", "B", 1);

    drive_reset();
    dir_drive_remove();
}

TEST(fatimg_fat32_default)
{
    FATFS *fs;

    make_pattern();
    dir_drive_new();
    dir_drive_file("Long File Name.dsk", pattern, sizeof(pattern));
    REQUIRE(dir_drive_insert(0) == 0);

    fs = drive_mount_from(dir_drive_read);
    REQUIRE(fs != NULL);
    CHECK_EQ(fs->fs_type, FS_FAT32);
    check_file("Long File Name.dsk", pattern, sizeof(pattern));

    drive_reset();
    dir_drive_remove();
}

/* The first file of the root folder of the drive: its short name into @sfn
 * and its case byte into @nt; 1 if a long name comes before it, 0 if not,
 * -1 if there is no such file. */
static int root_sfn(uint8_t *sfn, uint8_t *nt)
{
    static uint8_t sec[512];
    unsigned int rsvd, spc, i;
    uint32_t fat_secs, root;
    int lfn = 0;

    if (dir_drive_read(sec, 0, 1) != 0)
        return -1;
    spc = sec[13];
    rsvd = sec[14] | (sec[15] << 8);
    fat_secs = sec[36] | (sec[37] << 8) | (sec[38] << 16)
        | ((uint32_t)sec[39] << 24);
    root = sec[44] | (sec[45] << 8) | (sec[46] << 16)
        | ((uint32_t)sec[47] << 24);
    if (dir_drive_read(sec, rsvd + sec[16] * fat_secs + (root - 2) * spc,
                       1) != 0)
        return -1;
    for (i = 0; (i < sizeof(sec)) && (sec[i] != 0); i += 32) {
        if (sec[i] == 0xe5)
            continue;
        if (sec[i + 11] == 0x0f) {
            lfn = 1;
            continue;
        }
        if (sec[i + 11] & 0x08) /* the volume label */
            continue;
        memcpy(sfn, &sec[i], 11);
        *nt = sec[i + 12];
        return lfn;
    }
    return -1;
}

/* Short names in each code page as each system writes them, from what
 * Windows and Linux wrote when measured, and without long names. */
TEST(fatimg_short_names)
{
    static const struct {
        int sfn, by, lfn;
        const char *name, *sfn83;
        int has_lfn, nt;
    } c[] = {
        /* Windows at 866 writes an umlauted a as A, Linux '_' with a tail;
         * 437 has the letter; MIK lacks it; UTF-8 has its capital. */
        { DD_SFN_866, DD_BY_windows, 1, "K\xc3\xa4se.img", "KASE    IMG",
          1, 0 },
        { DD_SFN_866, DD_BY_linux, 1, "K\xc3\xa4se.img", "K_SE~1  IMG",
          1, 0 },
        { DD_SFN_437, DD_BY_windows, 1, "K\xc3\xa4se.img", "K\x8eSE    IMG",
          1, 0 },
        { DD_SFN_437, DD_BY_linux, 1, "K\xc3\xa4se.img", "K\x8eSE    IMG",
          1, 0 },
        /* 850's small letters take their capitals, in either system's rules. */
        { DD_SFN_850, DD_BY_linux, 1,
          "\xc3\xb8\xc3\xa3\xc3\x98\xc3\x83\xc3\xb0\xc3\x90\xc3\x8a\xc3\x8b.img",
          "\x9d\xc7\x9d\xc7\xd1\xd1\xd2\xd3" "IMG", 1, 0 },
        { DD_SFN_850, DD_BY_windows, 1,
          "\xc3\xb8\xc3\xa3\xc3\x98\xc3\x83\xc3\xb0\xc3\x90\xc3\x8a\xc3\x8b.img",
          "\x9d\xc7\x9d\xc7\xd1\xd1\xd2\xd3" "IMG", 1, 0 },
        /* 852's small letters take their capitals, in either system's rules. */
        { DD_SFN_852, DD_BY_linux, 1,
          "\xc4\x82\xc4\x85\xc4\x86\xc4\x99\xc4\xbd\xc5\x81\xc5\x83\xc5\x94.img",
          "\xc6\xa4\x8f\xa8\x95\x9d\xe3\xe8" "IMG", 1, 0 },
        { DD_SFN_852, DD_BY_windows, 1,
          "\xc4\x82\xc4\x85\xc4\x86\xc4\x99\xc4\xbd\xc5\x81\xc5\x83\xc5\x94.img",
          "\xc6\xa4\x8f\xa8\x95\x9d\xe3\xe8" "IMG", 1, 0 },
        { DD_SFN_mik, DD_BY_linux, 1, "K\xc3\xa4se.img", "K_SE~1  IMG",
          1, 0 },
        { DD_SFN_utf8, DD_BY_linux, 1, "K\xc3\xa4se.img",
          "K\xc3\x84SE   IMG", 1, 0 },
        /* Yo and Russian letters, in their capitals; MIK lacks Yo. */
        { DD_SFN_866, DD_BY_linux, 1, "\xd0\x81\xd0\xbb\xd0\xba\xd0\xb0.dsk",
          "\xf0\x8b\x8a\x80    DSK", 1, 0 },
        { DD_SFN_866, DD_BY_windows, 1,
          "\xd0\x81\xd0\xbb\xd0\xba\xd0\xb0.dsk",
          "\xf0\x8b\x8a\x80    DSK", 1, 0 },
        { DD_SFN_mik, DD_BY_windows, 1,
          "\xd0\x81\xd0\xbb\xd0\xba\xd0\xb0.dsk",
          "_\x8b\x8a\x80~1  DSK", 1, 0 },
        { DD_SFN_utf8, DD_BY_windows, 1,
          "\xd0\x81\xd0\xbb\xd0\xba\xd0\xb0.dsk",
          "\xd0\x81\xd0\x9b\xd0\x9a\xd0\x90" "DSK", 1, 0 },
        /* vfat writes one '_' for a character beyond the BMP. */
        { DD_SFN_437, DD_BY_linux, 1, "a\xf0\x9f\x98\x80.img",
          "A_~1    IMG", 1, 0 },
        /* Without long names, as MS-DOS leaves a drive: no case byte. */
        { DD_SFN_866, DD_BY_windows, 1, "game.img", "GAME    IMG", 0, 0x18 },
        { DD_SFN_866, DD_BY_windows, 0, "game.img", "GAME    IMG", 0, 0 },
        { DD_SFN_866, DD_BY_linux, 1, "game.img", "GAME    IMG", 1, 0 },
        { DD_SFN_866, DD_BY_linux, 0, "game.img", "GAME    IMG", 0, 0 },
        { DD_SFN_437, DD_BY_linux, 0, "K\xc3\xa4se.img", "K\x8eSE    IMG",
          0, 0 },
    };
    uint8_t sfn[11], nt = 0;
    unsigned int i;
    int lfn;

    for (i = 0; i < sizeof(c) / sizeof(c[0]); i++) {
        dir_drive_new();
        dir_drive_names(c[i].sfn, c[i].lfn, c[i].by);
        dir_drive_file(c[i].name, "x", 1);
        REQUIRE(dir_drive_insert(0) == 0);
        lfn = root_sfn(sfn, &nt);
        if ((lfn != c[i].has_lfn) || (nt != c[i].nt)
            || memcmp(sfn, c[i].sfn83, 11))
            ft_fail(__FILE__, __LINE__, "case %u, %s: short name %.11s, "
                    "long name %d, case byte %02x; expected %.11s, %d, %02x",
                    i, c[i].name, sfn, lfn, nt, c[i].sfn83, c[i].has_lfn,
                    c[i].nt);
        dir_drive_remove();
    }
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

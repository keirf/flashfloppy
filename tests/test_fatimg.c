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

/*
 * Local variables:
 * mode: C
 * c-file-style: "Linux"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */

/*
 * fs_hint.c
 *
 * What an error code means, in a few words, for the display to show beside
 * the code.
 *
 * Written & released by Mike Shevchenko <mike.shevchenko@gmail.com>
 *
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

/* The hints of FatFS's codes 1 to 19, then of FlashFloppy's own, 30 to 35:
 * 12 characters at most, so that the code, a blank and the hint fit a row
 * of 16 columns; none for a code that cannot occur. */
static const char hints[] =
    "I/O error\0" "FAT damaged\0" "Not ready\0" "No such file\0"
    "No folder\0" "Bad name\0" "Can't create\0" "File exists\0"
    "Disk changed\0" "Write prot.\0" "Internal\0" "Internal\0"
    "Not FAT disk\0" "\0" "\0" "\0" "No memory\0" "\0" "Internal\0"
    "Disk full\0" "Bad image\0" "Bad HXCSDFE\0" "Bad IMAGE_A\0"
    "No images\0" "Too nested";

const char *fresult_hint(FRESULT fr)
{
    const char *p = hints;
    int n = (fr < FR_DISK_FULL) ? fr - 1 : fr - FR_DISK_FULL + 19;

    if ((fr == FR_OK) || ((fr > FR_INVALID_PARAMETER) && (fr < FR_DISK_FULL))
        || (fr > FR_PATH_TOO_DEEP))
        return "";
    while (n--)
        p += strlen(p) + 1;
    return p;
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

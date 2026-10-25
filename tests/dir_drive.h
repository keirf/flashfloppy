/*
 * dir_drive.h
 *
 * ffemu's USB drive made from a directory, as the tests drive it: a folder
 * of files made for it, the drive built from them, and its sectors.
 *
 * Both halves include this header, the firmware's and the host's, so it
 * uses the types of neither.
 *
 * Written & released by Mike Shevchenko <mike.shevchenko@gmail.com>
 *
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

/* Makes a new empty folder, in the temporary one, for the files below. */
void dir_drive_new(void);
/* Adds file @name, its folders made as needed: @len bytes of @data, or of
 * zeros, sparse where the system can, if @data is NULL. */
void dir_drive_file(const char *name, const void *data,
                    unsigned long long len);
/* Inserts a drive made from the folder, FAT16 if @fat16: 0, or -1 if it
 * cannot be made. */
int dir_drive_insert(int fat16);
/* How many entries the drive left out for lack of room on FAT16: of its
 * full root folder if @root, else files of the full drive; the first of
 * them as a path into @first. */
unsigned int dir_drive_left_out(int root, const char **first);
/* @count sectors of the drive from @sec on, into @buf: 0, or -1. */
int dir_drive_read(void *buf, unsigned long sec, unsigned int count);
/* Removes the drive and the folder. */
void dir_drive_remove(void);

/*
 * Local variables:
 * mode: C
 * c-file-style: "Linux"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */

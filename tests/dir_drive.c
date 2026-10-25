/*
 * dir_drive.c
 *
 * The host's half of the tests of ffemu's USB drive made from a directory:
 * what fatimg.c needs of the rest of ffemu, and the folders of files that
 * the tests have it built from.
 *
 * Written & released by Mike Shevchenko <mike.shevchenko@gmail.com>
 *
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

#include <errno.h>
#include <fcntl.h>
#include <ftw.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#ifdef __CYGWIN__
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winioctl.h>
#include <io.h>
#endif

#include "host.h"
#include "dir_drive.h"

#ifndef O_BINARY
#define O_BINARY 0
#endif

struct config config;
const char *usb_path, *usb_path_name;

void cpu_quit(int code, const char *msg)
{
    if (msg != NULL)
        fprintf(stderr, "%s\n", msg);
    exit(code);
}

/* The firmware's writes kept as files need these, which no test reaches:
 * the tests set no state folder, so no pass is made. */
void make_dirs(char *dir)
{
    (void)dir;
}

void own_file(const char *path)
{
    (void)path;
}

void host_log(const char *fmt, ...)
{
    (void)fmt;
}

const char *user_path(const char *path, char *buf, size_t size)
{
    (void)buf;
    (void)size;
    return path;
}

void crash_name_thread(const char *name)
{
    (void)name;
}

static char dir[512];

static void fail(const char *what)
{
    fprintf(stderr, "dir_drive: %s: %s\n", what, strerror(errno));
    exit(2);
}

void dir_drive_new(void)
{
    const char *tmp = getenv("TMPDIR");

    if ((tmp == NULL) || (tmp[0] == '\0'))
        tmp = "/tmp";
    snprintf(dir, sizeof(dir), "%s/fftest-XXXXXX", tmp);
    if (mkdtemp(dir) == NULL)
        fail(dir);
}

void dir_drive_file(const char *name, const void *data,
                    unsigned long long len)
{
    char path[1024], *p;
    bool ok;
    int fd;

    snprintf(path, sizeof(path), "%s/%s", dir, name);
    for (p = path + strlen(dir) + 1; (p = strchr(p, '/')) != NULL; p++) {
        *p = '\0';
        if ((mkdir(path, 0755) != 0) && (errno != EEXIST))
            fail(path);
        *p = '/';
    }
    fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_BINARY, 0644);
    if (fd < 0)
        fail(path);
#ifdef __CYGWIN__
    /* NTFS gives a file grown by ftruncate() disk space unless sparse. */
    if (data == NULL) {
        DWORD n;
        DeviceIoControl((HANDLE)get_osfhandle(fd), FSCTL_SET_SPARSE, NULL, 0,
                        NULL, 0, &n, NULL);
    }
#endif
    ok = (data != NULL) ? (write(fd, data, len) == (ssize_t)len)
        : (ftruncate(fd, len) == 0);
    if (!ok || (close(fd) != 0))
        fail(path);
}

int dir_drive_insert(int fat16)
{
    config.usb_fat16 = fat16;
    return usb_insert(dir, false) ? 0 : -1;
}

unsigned int dir_drive_left_out(int root, const char **first)
{
    static struct usb_info info;

    usb_get_info(&info);
    *first = root ? info.root_full : info.drive_full;
    return root ? info.nr_root_full : info.nr_drive_full;
}

int dir_drive_read(void *buf, unsigned long sec, unsigned int count)
{
    return emu_usb_read(buf, sec, count);
}

static int remove_entry(const char *path, const struct stat *st, int flag,
                        struct FTW *ftw)
{
    return remove(path);
}

void dir_drive_remove(void)
{
    usb_remove();
    if (nftw(dir, remove_entry, 16, FTW_DEPTH | FTW_PHYS) != 0)
        fail(dir);
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

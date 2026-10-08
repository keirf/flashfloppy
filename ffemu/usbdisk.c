/*
 * usbdisk.c
 *
 * Finds the USB drives attached, the drive of a run without an argument.
 * The disks are those of /proc/partitions, which Cygwin provides too, with
 * the drive letters of their partitions; whether a disk is on USB, and what
 * it is, the system tells: Linux in /sys/block, Windows in the storage
 * descriptor of the physical drive, which it gives without administrator
 * rights. A drive is given as its first partition, the one that FatFS would
 * take: Windows lets anyone read a volume on removable media, but not the
 * physical drive.
 *
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef __CYGWIN__
#include <windows.h>
#include <winioctl.h>
#endif

#include "host.h"

#define MAX_DISKS 32

struct disk {
    char name[32];      /* as in /dev */
    char part[32];      /* its first partition, or "" if none */
    uint64_t bytes;
    char where[64];     /* drive letters or a mount point, or "" */
};

/* Copies @s of @n bytes into @buf, its blanks at both ends dropped. */
static void trim(char *buf, size_t size, const char *s, size_t n)
{
    while ((n != 0) && isspace((unsigned char)*s))
        s++, n--;
    while ((n != 0) && isspace((unsigned char)s[n-1]))
        n--;
    if (n >= size)
        n = size - 1;
    memcpy(buf, s, n);
    buf[n] = '\0';
}

/* Whether partition @part, as /proc/partitions names it, is on disk @disk. */
static bool on_disk(const char *part, const char *disk)
{
    size_t n = strlen(disk);
    return !strncmp(part, disk, n) && isdigit((unsigned char)part[n]);
}

/* The disks that are not empty, with their first partition, and the drive
 * letters of their partitions on Cygwin. */
static unsigned int list_disks(struct disk *d, unsigned int max)
{
    char line[256], name[32], mounts[128];
    unsigned long long kb;
    unsigned int nr = 0, i;
    FILE *f = fopen("/proc/partitions", "r");

    if (f == NULL)
        return 0;
    while (fgets(line, sizeof(line), f) != NULL) {
        mounts[0] = '\0';
        if (sscanf(line, "%*u %*u %llu %31s %127[^\n]", &kb, name,
                   mounts) < 2)
            continue;
        if (strncmp(name, "sd", 2) || isdigit((unsigned char)name[2]))
            continue;
        if (!isdigit((unsigned char)name[strlen(name) - 1])) {
            if ((kb == 0) || (nr == max))
                continue;
            memset(&d[nr], 0, sizeof(d[nr]));
            snprintf(d[nr].name, sizeof(d[nr].name), "%s", name);
            d[nr].bytes = (uint64_t)kb * 1024;
            nr++;
            continue;
        }
        /* A partition: Cygwin lists its drive letters, as "J:\". One of a
         * single block is an extended partition's entry, on Linux. */
        for (i = 0; i < nr; i++) {
            char *p = mounts, *where = d[i].where;
            if (!on_disk(name, d[i].name))
                continue;
            if (!d[i].part[0] && (kb > 1))
                snprintf(d[i].part, sizeof(d[i].part), "%s", name);
            while ((p = strchr(p, ':')) != NULL) {
                size_t n = strlen(where);
                if ((p > mounts) && (n + 4 < sizeof(d[i].where)))
                    snprintf(where + n, sizeof(d[i].where) - n, "%s%c:",
                             n ? " " : "", p[-1]);
                p++;
            }
        }
    }
    fclose(f);
    return nr;
}

#ifdef __CYGWIN__

/* Whether disk @d is on USB, and its vendor and product into @desc. Cygwin's
 * sda is \\.\PhysicalDrive0, sdb 1, and on to sdz and then sdaa. */
static bool usb_disk(const struct disk *d, char *desc, size_t size)
{
    STORAGE_PROPERTY_QUERY q = {
        .PropertyId = StorageDeviceProperty, .QueryType = PropertyStandardQuery
    };
    static uint8_t buf[1024];
    STORAGE_DEVICE_DESCRIPTOR *sd = (STORAGE_DEVICE_DESCRIPTOR *)buf;
    unsigned int nr = 0, i;
    char path[64], vendor[64] = "", product[64] = "";
    DWORD got;
    HANDLE h;
    bool usb;

    for (i = 2; d->name[i] != '\0'; i++)
        nr = nr * 26 + (d->name[i] - 'a') + ((i > 2) ? 26 : 0);
    snprintf(path, sizeof(path), "\\\\.\\PhysicalDrive%u", nr);
    h = CreateFileA(path, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                    OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return false;
    memset(buf, 0, sizeof(buf));
    usb = DeviceIoControl(h, IOCTL_STORAGE_QUERY_PROPERTY, &q, sizeof(q),
                          buf, sizeof(buf), &got, NULL)
        && (got >= sizeof(*sd)) && (sd->BusType == BusTypeUsb);
    CloseHandle(h);
    if (!usb)
        return false;

    if ((sd->VendorIdOffset != 0) && (sd->VendorIdOffset < got))
        trim(vendor, sizeof(vendor), (char *)buf + sd->VendorIdOffset,
             strnlen((char *)buf + sd->VendorIdOffset,
                     got - sd->VendorIdOffset));
    if ((sd->ProductIdOffset != 0) && (sd->ProductIdOffset < got))
        trim(product, sizeof(product), (char *)buf + sd->ProductIdOffset,
             strnlen((char *)buf + sd->ProductIdOffset,
                     got - sd->ProductIdOffset));
    snprintf(desc, size, "%s%s%s", vendor,
             (vendor[0] && product[0]) ? " " : "", product);
    return true;
}

static void where_mounted(struct disk *d)
{
    /* The drive letters come with /proc/partitions. */
    (void)d;
}

#else /* !__CYGWIN__ */

/* The first line of file @path, trimmed, into @buf; "" if none. */
static void read_line(const char *path, char *buf, size_t size)
{
    char line[128];
    FILE *f = fopen(path, "r");

    buf[0] = '\0';
    if (f == NULL)
        return;
    if (fgets(line, sizeof(line), f) != NULL)
        trim(buf, size, line, strlen(line));
    fclose(f);
}

/* Whether disk @d is on USB: its device in sysfs hangs off a USB device. Its
 * vendor and model into @desc. */
static bool usb_disk(const struct disk *d, char *desc, size_t size)
{
    char path[128], real[PATH_MAX], vendor[64], model[64];

    snprintf(path, sizeof(path), "/sys/block/%s", d->name);
    if ((realpath(path, real) == NULL) || (strstr(real, "/usb") == NULL))
        return false;
    snprintf(path, sizeof(path), "/sys/block/%s/device/vendor", d->name);
    read_line(path, vendor, sizeof(vendor));
    snprintf(path, sizeof(path), "/sys/block/%s/device/model", d->name);
    read_line(path, model, sizeof(model));
    snprintf(desc, size, "%s%s%s", vendor,
             (vendor[0] && model[0]) ? " " : "", model);
    return true;
}

/* Where a partition of disk @d is mounted, the first that is. */
static void where_mounted(struct disk *d)
{
    char line[512], dev[128], dir[256];
    FILE *f = fopen("/proc/mounts", "r");

    if (f == NULL)
        return;
    while (fgets(line, sizeof(line), f) != NULL) {
        if ((sscanf(line, "%127s %255s", dev, dir) == 2)
            && !strncmp(dev, "/dev/", 5) && on_disk(dev + 5, d->name)) {
            snprintf(d->where, sizeof(d->where), "%.63s", dir);
            break;
        }
    }
    fclose(f);
}

#endif /* !__CYGWIN__ */

int usb_disk_find(char *dev, size_t size, char *line, size_t line_size,
                  FILE *f)
{
    struct disk d[MAX_DISKS];
    unsigned int nr = list_disks(d, MAX_DISKS), i;
    char desc[128], bytes[32], text[320];
    int found = 0;

    for (i = 0; i < nr; i++) {
        if (!usb_disk(&d[i], desc, sizeof(desc)))
            continue;
        where_mounted(&d[i]);
        snprintf(text, sizeof(text), "%.127s, %.31s%s%.63s is /dev/%.31s",
                 desc[0] ? desc : "USB drive",
                 size_text(d[i].bytes, bytes, sizeof(bytes)),
                 d[i].where[0] ? ", " : "", d[i].where,
                 d[i].part[0] ? d[i].part : d[i].name);
        if (found++ == 0) {
            snprintf(dev, size, "/dev/%s",
                     d[i].part[0] ? d[i].part : d[i].name);
            snprintf(line, line_size, "%s", text);
        }
        if (f != NULL)
            fprintf(f, "%s\n", text);
    }
    return found;
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

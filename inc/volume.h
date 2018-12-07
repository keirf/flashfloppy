/*
 * volume.h
 * 
 * Volume abstraction for low-level storage drivers.
 * 
 * Written & released by Keir Fraser <keir.xen@gmail.com>
 * 
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

#include "../src/fatfs/diskio.h"

struct volume_ops {
    DSTATUS (*initialize)(BYTE);
    DSTATUS (*status)(BYTE);
    DRESULT (*read)(BYTE, BYTE *, DWORD, UINT);
    DRESULT (*write)(BYTE, const BYTE *, DWORD, UINT);
    DRESULT (*ioctl)(BYTE, BYTE, void *);
    bool_t (*connected)(void);
    bool_t (*readonly)(void);
};

bool_t volume_connected(void);
bool_t volume_readonly(void);

/*
 * Local variables:
 * mode: C
 * c-file-style: "Linux"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */

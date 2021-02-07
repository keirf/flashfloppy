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
    DRESULT (*read)(BYTE, BYTE *, LBA_t, UINT);
    DRESULT (*write)(BYTE, const BYTE *, LBA_t, UINT);
    DRESULT (*ioctl)(BYTE, BYTE, void *);
    bool_t (*connected)(void);
    bool_t (*readonly)(void);
};

bool_t volume_connected(void);
bool_t volume_readonly(void);
/* Returns TRUE if volume is in the middle of an operation that may
 * thread_yield(); FALSE if no I/O in progress. When TRUE, the thread will
 * yield as soon as calling this method would begin returning FALSE. */
bool_t volume_interrupt(void);

void volume_cache_init(void *start, void *end);
void volume_cache_destroy(void);
void volume_cache_metadata_only(FIL *fp);

/*
 * Local variables:
 * mode: C
 * c-file-style: "Linux"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */

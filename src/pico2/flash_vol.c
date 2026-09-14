/*
 * flash_vol.c
 *
 * Internal QSPI flash presented as a FatFS block device (RP2350 only).
 *
 * The firmware image and its FF.CFG sector live below 1MB (see
 * scripts/rp2350.ld.S and flash_cfg.c); everything above, up to the 4MB
 * fitted to every Pico 2 board, is a FAT volume holding the disk images.
 * volume.c selects this driver when no SD card mounts, and FatFS then
 * treats the internal flash exactly like a card: writable, so Direct
 * Access, IMAGE_A.CFG and the HxC selector all work as they do elsewhere.
 * scripts/mk_fat.py builds and flashes an image for it.
 *
 * Reads are plain memcpy from the XIP window: the firmware runs entirely
 * from SRAM, so flash is memory to us. A sector write is a read-modify-
 * write of the 4kB erase block containing it, via the fpec_* bootrom
 * helpers. Writing a sector it holds identical data for is skipped, which
 * spares the FAT and directory sectors FatFS rewrites on every f_sync().
 *
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

#define VOL_OFF  0x00100000
#define VOL_END  0x00400000
#define SECSZ    512
#define BLKSZ    FLASH_PAGE_SIZE /* 4kB QSPI erase sector */

/* Read-modify-write scratch for one erase block. */
static uint8_t blkbuf[BLKSZ];

static DSTATUS flash_disk_initialize(BYTE pdrv)
{
    static bool_t announced;

    if (pdrv)
        return STA_NOINIT;
    fpec_init();
    if (!announced) {
        /* Once only: with no filesystem in flash, mount retries forever. */
        announced = TRUE;
        printk("Volume: FAT in QSPI flash\n");
    }
    return 0;
}

static DSTATUS flash_disk_status(BYTE pdrv)
{
    return pdrv ? STA_NOINIT : 0;
}

static DRESULT flash_disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count)
{
    uint32_t off = VOL_OFF + sector * SECSZ;

    if (pdrv || ((off + count * SECSZ) > VOL_END))
        return RES_PARERR;

    memcpy(buff, (const void *)(XIP_BASE + off), count * SECSZ);
    return RES_OK;
}

static DRESULT flash_disk_write(
    BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count)
{
    uint32_t off = VOL_OFF + sector * SECSZ;

    if (pdrv || ((off + count * SECSZ) > VOL_END))
        return RES_PARERR;

    while (count != 0) {
        uint32_t blk = off & ~(BLKSZ-1);
        unsigned int blk_off = off & (BLKSZ-1);
        unsigned int nr = min_t(unsigned int,
                                count * SECSZ, BLKSZ - blk_off);
        const uint8_t *old = (const uint8_t *)(XIP_BASE + off);
        unsigned int i;
        bool_t erase = FALSE;

        for (i = 0; i < nr; i++) {
            /* Programming can only clear bits: any 0->1 transition needs
             * the whole block erased first. */
            if (old[i] & ~buff[i]) {
                erase = TRUE;
                break;
            }
        }

        if (erase) {
            memcpy(blkbuf, (const void *)(XIP_BASE + blk), BLKSZ);
            memcpy(&blkbuf[blk_off], buff, nr);
            fpec_page_erase(XIP_BASE + blk);
            fpec_write(blkbuf, BLKSZ, XIP_BASE + blk);
        } else if (memcmp(old, buff, nr)) {
            fpec_write(buff, nr, XIP_BASE + off);
        }

        buff += nr;
        off += nr;
        count -= nr / SECSZ;
    }

    return RES_OK;
}

static DRESULT flash_disk_ioctl(BYTE pdrv, BYTE ctrl, void *buff)
{
    if (pdrv)
        return RES_PARERR;

    switch (ctrl) {
    case CTRL_SYNC:
        return RES_OK;
    case GET_SECTOR_COUNT:
        *(LBA_t *)buff = (VOL_END - VOL_OFF) / SECSZ;
        return RES_OK;
    case GET_SECTOR_SIZE:
        *(WORD *)buff = SECSZ;
        return RES_OK;
    case GET_BLOCK_SIZE:
        *(DWORD *)buff = BLKSZ / SECSZ;
        return RES_OK;
    }

    return RES_PARERR;
}

static bool_t flash_connected(void)
{
    /* Soldered down: always present. */
    return TRUE;
}

static bool_t flash_readonly(void)
{
    return FALSE;
}

struct volume_ops flash_ops = {
    .initialize = flash_disk_initialize,
    .status = flash_disk_status,
    .read = flash_disk_read,
    .write = flash_disk_write,
    .ioctl = flash_disk_ioctl,
    .connected = flash_connected,
    .readonly = flash_readonly
};

/*
 * Local variables:
 * mode: C
 * c-file-style: "Linux"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */

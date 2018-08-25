/*
 * flash_cfg.c
 * 
 * Manage FF.CFG configuration values in Flash memory.
 * 
 * Written & released by Keir Fraser <keir.xen@gmail.com>
 * 
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

/* FF.CFG: Compiled default values. */
const struct ff_cfg dfl_ff_cfg = {
    .version = FFCFG_VERSION,
    .size = sizeof(struct ff_cfg),
#define x(n,o,v) .o = v,
#include "ff_cfg_defaults.h"
#undef x
};

/* FF.CFG: User-specified values, and defaults where not specified. */
struct ff_cfg ff_cfg;

#define SLOTW_NR   64           /* Number of 16-bit words per slot */
#define SLOTW_DEAD (SLOTW_NR-2) /* If != 0xffff: this slot is deleted */
#define SLOTW_CRC  (SLOTW_NR-1) /* CRC over entire config slot */
union cfg_slot {
    struct ff_cfg ff_cfg;
    uint16_t words[SLOTW_NR];
};

#define SLOT_BASE (union cfg_slot *)(0x8000000 +FLASH_MEM_SIZE -FLASH_PAGE_SIZE)
#define SLOT_NR   (FLASH_PAGE_SIZE / sizeof(union cfg_slot))

#define slot_is_blank(_slot) ((_slot)->words[0] == 0xffff)
#define slot_is_valid(_slot) (((_slot) != NULL) && !slot_is_blank(_slot))

static void erase_slot(union cfg_slot *slot)
{
    uint16_t zero = 0;
    fpec_init();
    fpec_write(&zero, 2, (uint32_t)&slot->words[SLOTW_DEAD]);
    printk("Config: Erased Slot %u\n", slot - SLOT_BASE);
}

/* Find first blank or valid config slot in Flash memory.
 * Returns NULL if none. */
static union cfg_slot *cfg_slot_find(void)
{
    unsigned int idx;
    union cfg_slot *slot;

    for (idx = 0; idx < SLOT_NR; idx++) {
        slot = SLOT_BASE + idx;
        if (slot->words[SLOTW_DEAD] != 0xffff)
            continue;
        if (slot_is_blank(slot))
            return slot;
        if ((slot->ff_cfg.version == dfl_ff_cfg.version)
            && !crc16_ccitt(slot, sizeof(*slot), 0xffff))
            return slot;
        /* Bad, non-blank config slot. Mark it dead. */
        erase_slot(slot);
    }

    return NULL;
}

void flash_ff_cfg_update(void)
{
    union cfg_slot new_slot, *slot = cfg_slot_find();
    uint16_t crc;

    /* Nothing to do if Flashed configuration is valid and up to date. */
    if (slot_is_valid(slot) && !memcmp(&slot->ff_cfg, &ff_cfg, sizeof(ff_cfg)))
        return;

    fpec_init();

    if ((slot != NULL) && slot_is_blank(slot)) {
        /* Slot is blank: no erase needed. */
    } else if ((slot != NULL)
               && ((slot - SLOT_BASE) < (SLOT_NR - 1))) {
        /* There's at least one blank slot available. Erase current slot. */
        erase_slot(slot);
        slot++;
    } else {
        /* No blank slots available. Erase whole page. */
        fpec_page_erase((uint32_t)SLOT_BASE);
        slot = SLOT_BASE;
        printk("Config: Erased Whole Page\n");
    }

    memset(&new_slot, 0, sizeof(new_slot));
    memcpy(&new_slot.ff_cfg, &ff_cfg, sizeof(ff_cfg));
    new_slot.words[SLOTW_DEAD] = 0xffff;
    crc = htobe16(crc16_ccitt(&new_slot, sizeof(new_slot)-2, 0xffff));
    /* Write up to but excluding SLOTW_DEAD. */
    fpec_write(&new_slot, sizeof(new_slot)-4, (uint32_t)slot);
    /* Write SLOTW_CRC. */
    fpec_write(&crc, 2, (uint32_t)&slot->words[SLOTW_CRC]);
    printk("Config: Written to Flash Slot %u\n", slot - SLOT_BASE);
}

void flash_ff_cfg_erase(void)
{
    union cfg_slot *slot = cfg_slot_find();
    if (slot_is_valid(slot))
        erase_slot(slot);
}

void flash_ff_cfg_read(void)
{
    union cfg_slot *slot = cfg_slot_find();
    bool_t found = slot_is_valid(slot);

    ff_cfg = dfl_ff_cfg;
    printk("Config: ");
    if (found) {
        unsigned int sz = min_t(unsigned int, slot->ff_cfg.size, ff_cfg.size);
        printk("Flash Slot %u (ver %u, size %u)\n",
               slot - SLOT_BASE, slot->ff_cfg.version, sz);
        /* Copy over all options that are present in Flash. */
        if (sz > offsetof(struct ff_cfg, interface))
            memcpy(&ff_cfg.interface, &slot->ff_cfg.interface,
                   sz - offsetof(struct ff_cfg, interface));
    } else {
        printk("Factory Defaults\n");
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

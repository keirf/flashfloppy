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
    .qd_ready_layout = 0xff,
#define x(n,o,v) .o = v,
#include "ff_cfg_defaults.h"
#undef x
};

/* FF.CFG: User-specified values, and defaults where not specified. */
struct ff_cfg ff_cfg;

/* Seven byte-sized runtime overrides. Base values alone may reach Flash. */
static const uint8_t setting_offsets[SET_nr] = {
    offsetof(struct ff_cfg, step_volume), offsetof(struct ff_cfg, qd_motor_volume),
    offsetof(struct ff_cfg, notify_volume), offsetof(struct ff_cfg, oled_contrast),
    offsetof(struct ff_cfg, display_off_secs), offsetof(struct ff_cfg, interface),
    offsetof(struct ff_cfg, qd_ready)
};
static uint8_t setting_base[SET_nr], setting_value[SET_nr], setting_mask;

uint8_t *runtime_setting(unsigned int item)
{
    return (uint8_t *)&ff_cfg + setting_offsets[item];
}

bool_t runtime_setting_active(unsigned int item)
{
    return !!(setting_mask & (1u << item));
}

void runtime_setting_set(unsigned int item, uint8_t value)
{
    if (!runtime_setting_active(item))
        setting_base[item] = *runtime_setting(item);
    setting_value[item] = *runtime_setting(item) = value;
    setting_mask |= 1u << item;
}

void runtime_settings_base(void)
{
    unsigned int i;
    for (i = 0; i < SET_nr; i++)
        if (runtime_setting_active(i))
            *runtime_setting(i) = setting_base[i];
}

void runtime_settings_apply(void)
{
    unsigned int i;
    for (i = 0; i < SET_nr; i++) {
        if (!runtime_setting_active(i))
            continue;
        setting_base[i] = *runtime_setting(i);
        *runtime_setting(i) = setting_value[i];
    }
}

#define SLOTW_NR   64           /* Number of 16-bit words per slot */
#define SLOTW_DEAD (SLOTW_NR-2) /* If != 0xffff: this slot is deleted */
#define SLOTW_CRC  (SLOTW_NR-1) /* CRC over entire config slot */
union cfg_slot {
    struct ff_cfg ff_cfg;
    uint16_t words[SLOTW_NR];
};

/* DEAD and CRC occupy the last four bytes of a configuration slot. */
_Static_assert(sizeof(struct ff_cfg) <= sizeof(union cfg_slot) - 4,
               "FF.CFG overlaps the slot metadata");

#if MCU == MCU_stm32f105
#define SLOT_BASE (union cfg_slot *)(0x8020000 - FLASH_PAGE_SIZE)
#elif MCU == MCU_at32f435
#define SLOT_BASE (union cfg_slot *)(0x8040000 - FLASH_PAGE_SIZE)
#endif
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

void flash_ff_cfg_update(void *scratch)
{
    union cfg_slot *new_slot = scratch, *slot = cfg_slot_find();
    uint16_t crc;
    unsigned int i;

    memset(new_slot, 0, sizeof(*new_slot));
    memcpy(&new_slot->ff_cfg, &ff_cfg, sizeof(ff_cfg));
    for (i = 0; i < SET_nr; i++)
        if (runtime_setting_active(i))
            *((uint8_t *)&new_slot->ff_cfg + setting_offsets[i]) = setting_base[i];

    /* Nothing to do if Flashed configuration is valid and up to date. */
    if (slot_is_valid(slot) && !memcmp(&slot->ff_cfg, &new_slot->ff_cfg, sizeof(ff_cfg)))
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
        if (flash_page_size < FLASH_PAGE_SIZE)
            fpec_page_erase((uint32_t)SLOT_BASE + flash_page_size);
        slot = SLOT_BASE;
        printk("Config: Erased Whole Page\n");
    }

    new_slot->words[SLOTW_DEAD] = 0xffff;
    crc = htobe16(crc16_ccitt(new_slot, sizeof(*new_slot)-2, 0xffff));
    /* Write up to but excluding SLOTW_DEAD. */
    fpec_write(new_slot, sizeof(*new_slot)-4, (uint32_t)slot);
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

    BUILD_BUG_ON(sizeof(*slot) != sizeof(slot->words));

    setting_mask = 0;
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
    /* Old layouts have no new READY setting; do not reinterpret their bytes. */
    if (ff_cfg.qd_ready_layout != 0xff || ff_cfg.qd_ready > QD_READY_JC)
        ff_cfg.qd_ready = dfl_ff_cfg.qd_ready;
    ff_cfg.qd_ready_layout = 0xff;
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

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

union flash_ff_cfg {
    struct ff_cfg ff_cfg;
    uint16_t words[64];
};

#define FLASH_FF_CFG_BASE (union flash_ff_cfg *)(0x8020000 - FLASH_PAGE_SIZE)
#define FLASH_FF_CFG_NR   (FLASH_PAGE_SIZE / sizeof(union flash_ff_cfg))

#define CFG_DEAD(_cfg)  ((_cfg)->words[0] == 0)
#define CFG_BLANK(_cfg) ((_cfg)->words[0] == 0xffff)
#define CFG_VALID(_cfg) (((_cfg) != NULL) && !CFG_BLANK(_cfg))

static void erase_slot(union flash_ff_cfg *cfg)
{
    uint16_t zero = 0;
    fpec_init();
    fpec_write(&zero, 2, (uint32_t)&cfg->words[0]);
    printk("Config: Erased Slot %u\n", cfg - FLASH_FF_CFG_BASE);
}

/* Find first blank or valid config slot in Flash memory.
 * Returns NULL if none. */
static union flash_ff_cfg *flash_ff_cfg_find(void)
{
    unsigned int idx;
    union flash_ff_cfg *cfg;

    for (idx = 0; idx < FLASH_FF_CFG_NR; idx++) {
        cfg = FLASH_FF_CFG_BASE + idx;
        if (CFG_DEAD(cfg))
            continue;
        if (CFG_BLANK(cfg))
            return cfg;
        if ((cfg->ff_cfg.ver == dfl_ff_cfg.ver)
            && !crc16_ccitt(cfg, sizeof(*cfg), 0xffff))
            return cfg;
        /* Bad, non-blank config slot. Mark it dead. */
        erase_slot(cfg);
    }

    return NULL;
}

void flash_ff_cfg_update(void)
{
    union flash_ff_cfg new_cfg, *cfg = flash_ff_cfg_find();

    /* Nothing to do if Flashed configuration is valid and up to date. */
    if (CFG_VALID(cfg) && !memcmp(&cfg->ff_cfg, &ff_cfg, sizeof(ff_cfg)))
        return;

    fpec_init();

    if ((cfg != NULL) && CFG_BLANK(cfg)) {
        /* Slot is blank: no erase needed. */
    } else if ((cfg != NULL)
               && ((cfg - FLASH_FF_CFG_BASE) < (FLASH_FF_CFG_NR - 1))) {
        /* There's at least one blank slot available. Erase current slot. */
        erase_slot(cfg);
        cfg++;
    } else {
        /* No blank slots available. Erase whole page. */
        fpec_page_erase((uint32_t)FLASH_FF_CFG_BASE);
        cfg = FLASH_FF_CFG_BASE;
        printk("Config: Erased Whole Page\n");
    }

    memset(&new_cfg, 0, sizeof(new_cfg));
    memcpy(&new_cfg.ff_cfg, &ff_cfg, sizeof(ff_cfg));
    new_cfg.words[63] = htobe16(
        crc16_ccitt(&new_cfg, sizeof(new_cfg)-2, 0xffff));
    fpec_write(&new_cfg, sizeof(new_cfg), (uint32_t)cfg);
    printk("Config: Written to Flash Slot %u\n", cfg - FLASH_FF_CFG_BASE);
}

void flash_ff_cfg_erase(void)
{
    union flash_ff_cfg *cfg = flash_ff_cfg_find();
    if (CFG_VALID(cfg))
        erase_slot(cfg);
}

void flash_ff_cfg_read(void)
{
    union flash_ff_cfg *cfg = flash_ff_cfg_find();
    bool_t found = CFG_VALID(cfg);

    ff_cfg = found ? cfg->ff_cfg : dfl_ff_cfg;
    printk("Config: ");
    if (found) {
        printk("Flash Slot %u\n", cfg - FLASH_FF_CFG_BASE);
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

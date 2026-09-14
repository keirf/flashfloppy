/*
 * fpec_rp2350.c
 *
 * RP2350 QSPI flash programming via bootrom routines. Presents the same
 * halfword-granular write interface as the STM32 FPEC: sub-page writes are
 * implemented as read-modify-write of 256-byte flash pages (only 1->0 bit
 * transitions, as guaranteed by the flash_cfg layer).
 *
 * All firmware code and data execute from SRAM, so XIP may be safely
 * suspended for the duration of a flash operation.
 *
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

#define FLASH_PROG_PAGE 256

typedef void (*rom_connect_internal_flash_fn)(void);
typedef void (*rom_flash_exit_xip_fn)(void);
typedef void (*rom_flash_range_erase_fn)(
    uint32_t addr, uint32_t count, uint32_t block_size, uint8_t block_cmd);
typedef void (*rom_flash_range_program_fn)(
    uint32_t addr, const uint8_t *data, uint32_t count);
typedef void (*rom_flash_flush_cache_fn)(void);
typedef void (*rom_flash_enter_cmd_xip_fn)(void);

static rom_connect_internal_flash_fn _connect_internal_flash;
static rom_flash_exit_xip_fn _flash_exit_xip;
static rom_flash_range_erase_fn _flash_range_erase;
static rom_flash_range_program_fn _flash_range_program;
static rom_flash_flush_cache_fn _flash_flush_cache;
static rom_flash_enter_cmd_xip_fn _flash_enter_cmd_xip;

void fpec_init(void)
{
    if (_connect_internal_flash != NULL)
        return;
    _connect_internal_flash = rp2350_rom_func(ROM_TABLE_CODE('I', 'F'));
    _flash_exit_xip = rp2350_rom_func(ROM_TABLE_CODE('E', 'X'));
    _flash_range_erase = rp2350_rom_func(ROM_TABLE_CODE('R', 'E'));
    _flash_range_program = rp2350_rom_func(ROM_TABLE_CODE('R', 'P'));
    _flash_flush_cache = rp2350_rom_func(ROM_TABLE_CODE('F', 'C'));
    _flash_enter_cmd_xip = rp2350_rom_func(ROM_TABLE_CODE('C', 'X'));
    ASSERT(_connect_internal_flash && _flash_exit_xip && _flash_range_erase
           && _flash_range_program && _flash_flush_cache
           && _flash_enter_cmd_xip);
}

static void flash_op_prepare(void)
{
    IRQ_global_disable();
    (*_connect_internal_flash)();
    (*_flash_exit_xip)();
}

static void flash_op_finish(void)
{
    (*_flash_flush_cache)();
    (*_flash_enter_cmd_xip)();
    IRQ_global_enable();
}

void fpec_page_erase(uint32_t flash_address)
{
    uint32_t off = flash_address - XIP_BASE;

    ASSERT(!(off & (FLASH_PAGE_SIZE-1)));

    flash_op_prepare();
    (*_flash_range_erase)(off, FLASH_PAGE_SIZE, FLASH_PAGE_SIZE, 0x20);
    flash_op_finish();
}

void fpec_write(const void *data, unsigned int size, uint32_t flash_address)
{
    const uint8_t *d = data;
    uint8_t page[FLASH_PROG_PAGE];

    while (size != 0) {
        uint32_t page_addr = flash_address & ~(FLASH_PROG_PAGE-1);
        unsigned int page_off = flash_address & (FLASH_PROG_PAGE-1);
        unsigned int nr = min_t(unsigned int, size, FLASH_PROG_PAGE-page_off);

        /* Read-modify-write of one flash page. */
        memcpy(page, (void *)page_addr, FLASH_PROG_PAGE);
        memcpy(&page[page_off], d, nr);

        flash_op_prepare();
        (*_flash_range_program)(page_addr - XIP_BASE, page, FLASH_PROG_PAGE);
        flash_op_finish();

        d += nr;
        size -= nr;
        flash_address += nr;
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

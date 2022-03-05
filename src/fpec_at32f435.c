/*
 * fpec_at324f435.c
 * 
 * AT32F435 Flash Memory Program/Erase Controller (FPEC).
 * 
 * Written & released by Keir Fraser <keir.xen@gmail.com>
 * 
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

static void fpec_wait_and_clear(FLASH_BANK bank)
{
    while (bank->sr & FLASH_SR_BSY)
        continue;
    bank->sr = FLASH_SR_EOP | FLASH_SR_WRPRTERR | FLASH_SR_PGERR;
    bank->cr = 0;
}

void fpec_init(void)
{
    /* Unlock the FPEC. */
    if (flash->bank1.cr & FLASH_CR_LOCK) {
        flash->unlock1 = 0x45670123;
        flash->unlock1 = 0xcdef89ab;
    }

    fpec_wait_and_clear(&flash->bank1);
}

void fpec_page_erase(uint32_t flash_address)
{
    FLASH_BANK bank = &flash->bank1;
    fpec_wait_and_clear(bank);
    bank->ar = flash_address;
    bank->cr |= FLASH_CR_PG_ER | FLASH_CR_ERASE_STRT;
    fpec_wait_and_clear(bank);
}

void fpec_write(const void *data, unsigned int size, uint32_t flash_address)
{
    FLASH_BANK bank = &flash->bank1;
    uint16_t *_f = (uint16_t *)flash_address;
    const uint16_t *_d = data;

    fpec_wait_and_clear(bank);
    for (; size != 0; size -= 2) {
        bank->cr |= FLASH_CR_PG;
        *_f++ = *_d++; 
        fpec_wait_and_clear(bank);
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

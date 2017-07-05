/*
 * fpec.c
 * 
 * STM32F10x Flash Memory Program/Erase Controller (FPEC).
 * 
 * Written & released by Keir Fraser <keir.xen@gmail.com>
 * 
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

static void fpec_wait_and_clear(void)
{
    while (flash->sr & FLASH_SR_BSY)
        continue;
    flash->sr = FLASH_SR_EOP | FLASH_SR_WRPRTERR | FLASH_SR_PGERR;
    flash->cr = 0;
}

void fpec_init(void)
{
    /* Erases and writes require the HSI oscillator. */
    rcc->cr |= RCC_CR_HSION;
    while (!(rcc->cr & RCC_CR_HSIRDY))
        cpu_relax();

    /* Unlock the FPEC. */
    if (flash->cr & FLASH_CR_LOCK) {
        flash->keyr = 0x45670123;
        flash->keyr = 0xcdef89ab;
    }

    fpec_wait_and_clear();
}

void fpec_page_erase(uint32_t flash_address)
{
    fpec_wait_and_clear();
    flash->cr |= FLASH_CR_PER;
    flash->ar = flash_address;
    flash->cr |= FLASH_CR_STRT;
    fpec_wait_and_clear();
}

void fpec_write(const void *data, unsigned int size, uint32_t flash_address)
{
    uint16_t *_f = (uint16_t *)flash_address;
    const uint16_t *_d = data;

    fpec_wait_and_clear();
    for (; size != 0; size -= 2) {
        flash->cr |= FLASH_CR_PG;
        *_f++ = *_d++; 
        fpec_wait_and_clear();
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

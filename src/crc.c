/*
 * crc.c
 * 
 * Table-based CRC16-CCITT.
 * 
 * Written & released by Keir Fraser <keir.xen@gmail.com>
 * 
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

/* CRC16-CCITT, polynomial 0x1021, a nibble at a time. */
static const uint16_t crc16tab[16] = {
    0x0000, 0x1021, 0x2042, 0x3063, 0x4084, 0x50a5, 0x60c6, 0x70e7,
    0x8108, 0x9129, 0xa14a, 0xb16b, 0xc18c, 0xd1ad, 0xe1ce, 0xf1ef
};

uint16_t crc16_ccitt(const void *buf, size_t len, uint16_t crc)
{
    unsigned int i;
    const uint8_t *b = buf;
    const uint16_t *tab = crc16tab;

    /* Keep the table's address in a register across the loop. The empty asm
     * hides the constant from GCC, which otherwise reloads it from the
     * literal pool on every byte when told not to move loop invariants. */
    asm ("" : "+r" (tab));

    for (i = 0; i < len; i++) {
        crc = tab[(crc>>12)^(*b>>4)] ^ (crc<<4);
        crc = tab[(crc>>12)^(*b++&15)] ^ (crc<<4);
    }
    return crc;
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

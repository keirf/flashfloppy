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

static uint16_t crc16tab[256];

void crc16_gentable(void)
{
    uint16_t crc, i, j;
    for (i = 0; i < 256; i++) {
        crc = i << 8;
        for (j = 0; j < 8; j++)
            crc = (crc & 0x8000) ? 0x1021 ^ (crc << 1) : (crc << 1);
        crc16tab[i] = crc;
    }
}

uint16_t crc16_ccitt(const void *buf, size_t len, uint16_t crc)
{
    unsigned int i;
    const uint8_t *b = buf;
    for (i = 0; i < len; i++)
        crc = crc16tab[(crc>>8)^*b++] ^ (crc<<8);
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

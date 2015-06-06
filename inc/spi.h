/*
 * spi.h
 * 
 * Helper functions for STM32F10x SPI interfaces.
 * 
 * Written & released by Keir Fraser <keir.xen@gmail.com>
 * 
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

void spi_quiesce(SPI spi);

void spi_16bit_frame(SPI spi);
void spi_8bit_frame(SPI spi);

void spi_xmit16(SPI spi, uint16_t out);
uint16_t spi_xchg16(SPI spi, uint16_t out);
#define spi_recv16(spi) spi_xchg16(spi, 0xffffu)

#define spi_xmit8(spi, x) spi_xmit16(spi, (uint8_t)(x))
#define spi_xchg8(spi, x) (uint8_t)spi_xchg16(spi, (uint8_t)(x))
#define spi_recv8(spi) spi_xchg8(spi, 0xffu)

/*
 * Local variables:
 * mode: C
 * c-file-style: "Linux"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */

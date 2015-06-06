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

/*
 * Local variables:
 * mode: C
 * c-file-style: "Linux"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */

/*
 * spi.c
 * 
 * Helper functions for STM32F10x SPI interfaces.
 * 
 * Written & released by Keir Fraser <keir.xen@gmail.com>
 * 
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

void spi_quiesce(SPI spi)
{
    while ((spi->sr & (SPI_SR_TXE|SPI_SR_BSY)) != SPI_SR_TXE)
        cpu_relax();
    (void)spi->dr; /* flush the rx buffer */
}

void spi_16bit_frame(SPI spi)
{
    spi_quiesce(spi);
    spi->cr1 |= SPI_CR1_DFF;
}

void spi_8bit_frame(SPI spi)
{
    spi_quiesce(spi);
    spi->cr1 &= ~SPI_CR1_DFF;
}

void spi_xmit16(SPI spi, uint16_t out)
{
    while (!(spi->sr & SPI_SR_TXE))
        cpu_relax();
    spi->dr = out;
}

uint16_t spi_xchg16(SPI spi, uint16_t out)
{
    while (!(spi->sr & SPI_SR_TXE))
        cpu_relax();
    spi->dr = out;
    while (!(spi->sr & SPI_SR_RXNE))
        continue;
    return spi->dr;
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

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

#if MCU == MCU_rp2350

/* PL022 back end. Only SPI0 is in use: the handle argument is ignored. */

void spi_quiesce(SPI spi)
{
    while ((rp_spi0->sr & (SPI_SR_TFE|SPI_SR_BSY)) != SPI_SR_TFE)
        cpu_relax();
    while (rp_spi0->sr & SPI_SR_RNE)
        (void)rp_spi0->dr; /* drain the rx fifo */
}

static void rp_spi_set_dss(unsigned int bits)
{
    rp_spi0->cr1 &= ~SPI_CR1_SSE;
    rp_spi0->cr0 = (rp_spi0->cr0 & ~0xfu) | SPI_CR0_DSS(bits);
    rp_spi0->cr1 |= SPI_CR1_SSE;
}

void spi_16bit_frame(SPI spi)
{
    spi_quiesce(spi);
    rp_spi_set_dss(16);
}

void spi_8bit_frame(SPI spi)
{
    spi_quiesce(spi);
    rp_spi_set_dss(8);
}

void spi_xmit16(SPI spi, uint16_t out)
{
    while (!(rp_spi0->sr & SPI_SR_TNF))
        cpu_relax();
    rp_spi0->dr = out;
}

uint16_t spi_xchg16(SPI spi, uint16_t out)
{
    while (!(rp_spi0->sr & SPI_SR_TNF))
        cpu_relax();
    rp_spi0->dr = out;
    while (!(rp_spi0->sr & SPI_SR_RNE))
        continue;
    return rp_spi0->dr;
}

#else

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

#endif

/*
 * Local variables:
 * mode: C
 * c-file-style: "Linux"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */

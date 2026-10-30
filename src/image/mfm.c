/*
 * mfm.c
 * 
 * MFM-conversion routines.
 * 
 * Written & released by Keir Fraser <keir.xen@gmail.com>
 * 
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

/* The MFM cells of data byte @x: its bits in the odd cells, from bit 15
 * down, and a clock in each even cell between two zero data bits. The
 * clock before the first data bit takes the bit before it as zero. */
uint16_t bintomfm(uint8_t x)
{
    uint32_t d = x;

    /* Spread the data bits to the odd cells. */
    d = (d | (d << 4)) & 0x0f0f;
    d = (d | (d << 2)) & 0x3333;
    d = (d | (d << 1)) & 0x5555;
    return d | (~((d << 1) | (d >> 1)) & 0xaaaa);
}

uint8_t always_inline mfmtobin(uint16_t x)
{
    uint8_t y;
    x = be16toh(x) << 1;
#if defined(__arm__)
    asm volatile (
        "lsrs %1,%1,#2 ; rrx %0,%0\n"
        "lsrs %1,%1,#2 ; rrx %0,%0\n"
        "lsrs %1,%1,#2 ; rrx %0,%0\n"
        "lsrs %1,%1,#2 ; rrx %0,%0\n"
        "lsrs %1,%1,#2 ; rrx %0,%0\n"
        "lsrs %1,%1,#2 ; rrx %0,%0\n"
        "lsrs %1,%1,#2 ; rrx %0,%0\n"
        "lsrs %1,%1,#2 ; rrx %0,%0\n"
        "rev %0,%0\n"
        : "=&r" (y) : "r" (x) );
#else
    {
        unsigned int i;
        for (y = i = 0; i < 8; i++)
            y |= ((x >> (2*i + 1)) & 1) << i;
    }
#endif
    return y;
}

void mfm_to_bin(const void *in, void *out, unsigned int nr)
{
    const uint16_t *_in = in;
    uint8_t *_out = out;
    while (nr--)
        *_out++ = mfmtobin(*_in++);
}

void mfm_ring_to_bin(const uint16_t *ring, unsigned int mask,
                     unsigned int idx, void *out, unsigned int nr)
{
    unsigned int head;
    idx &= mask;
    head = min_t(unsigned int, nr, mask+1-idx);
    mfm_to_bin(&ring[idx], out, head);
    if (head != nr) 
        mfm_to_bin(ring, (uint8_t *)out + head, nr - head);
}

uint16_t fm_sync(uint8_t dat, uint8_t clk)
{
    uint16_t _dat = bintomfm(dat) & 0x5555;
    uint16_t _clk = (bintomfm(clk) & 0x5555) << 1;
    return _clk | _dat;
}

uint16_t bc_rdata_flux(struct image *im, uint16_t *tbuf, uint16_t nr)
{
    uint32_t ticks_per_cell = im->ticks_per_cell;
    uint32_t ticks = im->ticks_since_flux;
    uint32_t x, y = 32, todo = nr;
    struct image_buf *bc = &im->bufs.read_bc;
    uint32_t *bc_b = bc->p, bc_c = bc->cons, bc_p = bc->prod & ~31;
    unsigned int bc_mask = (bc->len / 4) - 1;

    /* Convert pre-generated bitcells into flux timings. */
    while (bc_c != bc_p) {
        y = bc_c % 32;
        x = be32toh(bc_b[(bc_c / 32) & bc_mask]) << y;
        bc_c += 32 - y;
        im->cur_bc += 32 - y;
        im->cur_ticks += (32 - y) * ticks_per_cell;
        while (y < 32) {
            y++;
            ticks += ticks_per_cell;
            if ((int32_t)x < 0) {
                *tbuf++ = (ticks >> 4) - 1;
                ticks &= 15;
                if (!--todo)
                    goto out;
            }
            x <<= 1;
        }
    }

    ASSERT(y == 32);

out:
    bc->cons = bc_c - (32 - y);
    im->cur_bc -= 32 - y;
    im->cur_ticks -= (32 - y) * ticks_per_cell;
    im->ticks_since_flux = ticks;

    if (im->cur_bc >= im->tracklen_bc) {
        im->cur_bc -= im->tracklen_bc;
        ASSERT(im->cur_bc < im->tracklen_bc);
        im->tracklen_ticks = im->cur_ticks - im->cur_bc * ticks_per_cell;
        im->cur_ticks -= im->tracklen_ticks;
    }

    return nr - todo;
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

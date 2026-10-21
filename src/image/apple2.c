/*
 * apple2.c
 *
 * Apple2 nibble and sector images:
 *
 * .nib: 35 tracks of 6656 GCR bytes each, as the Disk II controller reads
 * them. Sync bytes have lost their two trailing zero bits in this format, so
 * the host's controller aligns itself on the data instead; it keeps the
 * alignment through the gaps, which are all ones.
 *
 * .nic, the format of the SDISK// emulator: 35 tracks of 16 blocks of 512
 * bytes, the first 416 bytes of each block one sector as a ready bit stream,
 * sync bits included, and the rest zero padding. A track streams as the 16
 * sectors' bit streams back to back: the same 6656 bytes as a .nib track.
 *
 * .dsk, .do and .po: 35 tracks of 16 sectors of 256 bytes, in DOS 3.3 order
 * (.do) or ProDOS order (.po); a .dsk is in DOS 3.3 order unless a ProDOS
 * volume directory starts where ProDOS order puts it. Each sector streams as
 * its .nic block would, with volume 254. An image of the Agat may end in a
 * 128-byte trailer naming the disk, which is left as it is.
 *
 * All of them stream bytes as bit cells, 4us each. A write is decoded back
 * into nibbles the way the controller does it, a nibble being eight bits from
 * a 1 bit on, and the nibbles go into the track once the write is complete:
 * into a .nib track where the write began; into the fixed places of a .nic
 * block, each address or data field found to the sector it belongs to; into
 * the sector of a sector image, each data field decoded.
 *
 * After the .nib handler of Oleg Odintsov's Gotek firmware 307 for the Agat,
 * the gotek-sa390 project of https://svn.code.sf.net/p/agat-hardware/code
 *
 * Written & released by Mike Shevchenko <mike.shevchenko.kz@gmail.com>
 *
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

#define LOG_PREFIX "Apple2"

#define TRK_LEN 6656 /* bytes streamed per track, in every format */
#define MAX_TRKS 40

#define DSK_TRK_LEN 4096
/* The volume in a sector image's address fields, which the image does not
 * keep: the one DOS 3.3 formats a disk with. */
#define DSK_VOLUME 254

#define NIC_BLK_LEN 512
#define NIC_BLK_DATA 416
#define NIC_BLKS_PER_TRK (TRK_LEN / NIC_BLK_DATA)
#define NIC_TRK_LEN (NIC_BLKS_PER_TRK * NIC_BLK_LEN)
/* Where the fields are in a block: D5 AA 96, the four 4-and-4 pairs and
 * DE AA EB; then D5 AA AD, 342 data nibbles, the checksum and DE AA EB. */
#define NIC_ADDR_OFF 34
#define NIC_ADDR_LEN 14
#define NIC_DATA_OFF 53
#define NIC_DATA_LEN 349

/* The staging buffer holds a block being modified, then the nibbles of the
 * write in progress, a track's worth: the volume cache has the rest. A read
 * of a sector image has its track there, then the blocks made of it. */
#define STAGE_BLK 0
#define STAGE_NIB NIC_BLK_LEN
#define STAGE_LEN (8*1024)
#define STAGE_NIB_MAX (STAGE_LEN - STAGE_NIB)

static void nib_seek_track(struct image *im, uint16_t track);

static const struct image_handler nib_image_handler;

const struct image_handler *nib_open(struct image *im)
{
    unsigned int trk_len = im->a2.dsk ? DSK_TRK_LEN
        : im->a2.nic ? NIC_TRK_LEN : TRK_LEN;
    unsigned int nr_trks = f_size(&im->fp) / trk_len;

    /* A sector image of the Agat may end in a 128-byte trailer naming the
     * disk. */
    if (((f_size(&im->fp) % trk_len) & ~(im->a2.dsk ? 128u : 0u))
        || (nr_trks < 35) || (nr_trks > MAX_TRKS))
        return NULL;

    im->a2.trk_len = trk_len;
    im->nr_cyls = nr_trks;
    im->step = 2; /* the host steps in half tracks */
    im->nr_sides = 1;
    im->write_bc_ticks = sampleclk_us(4);
    im->ticks_per_cell = im->write_bc_ticks * 16;
    im->sync = SYNC_none;
    im->tracklen_bc = TRK_LEN * 8;
    im->tracklen_ticks = im->tracklen_bc * im->ticks_per_cell;
    im->stk_per_rev = stk_sampleclk(im->tracklen_ticks / 16);

    ASSERT(STAGE_LEN <= im->bufs.read_data.len);
    volume_cache_init(im->bufs.read_data.p + STAGE_LEN,
                      im->bufs.read_data.p + im->bufs.read_data.len);
    if (im->bufs.read_data.len < (64*1024))
        volume_cache_metadata_only(&im->fp);

    nib_seek_track(im, 0);

    return &nib_image_handler;
}

static const struct image_handler nic_image_handler;

const struct image_handler *nic_open(struct image *im)
{
    im->a2.nic = TRUE;
    return nib_open(im) ? &nic_image_handler : NULL;
}

/* The sector of the file that each physical sector of a track holds, in DOS
 * 3.3 order and in ProDOS order. */
static const uint8_t dsk_file_sec[2][16] = {
    { 0, 7, 14, 6, 13, 5, 12, 4, 11, 3, 10, 2, 9, 1, 8, 15 },
    { 0, 8, 1, 9, 2, 10, 3, 11, 4, 12, 5, 13, 6, 14, 7, 15 }
};

static const struct image_handler a2dsk_image_handler;

const struct image_handler *a2dsk_open(struct image *im)
{
    unsigned int prodos = (im->slot->type[0] == 'p'); /* .po vs .do */
    uint32_t hdr[2];

    im->a2.nic = im->a2.dsk = TRUE;
    if (!nib_open(im))
        return NULL;

    /* .DSK: Auto-probe DOS 3.3 vs ProDOS sector order.  */
    if (im->slot->type[1] == 's') {
        /* The key block of a ProDOS volume directory, block 2: no previous
         * block, the next one 3, storage type F before the name's length. */
        F_lseek(&im->fp, 2*512);
        F_read(&im->fp, hdr, sizeof(hdr), NULL);
        prodos = (le32toh(hdr[0]) == 0x00030000)
            && ((le32toh(hdr[1]) & 0xf0) == 0xf0);
        if (prodos)
            printk(".DSK: ProDOS\n");
    }

    im->a2.file_sec = dsk_file_sec[prodos];

    return &a2dsk_image_handler;
}

static void nib_seek_track(struct image *im, uint16_t track)
{
    im->a2.trk_off = (uint32_t)track * im->a2.trk_len;
    im->cur_track = track;
}

static void nib_setup_track(
    struct image *im, uint16_t track, uint32_t *start_pos)
{
    struct image_buf *rd = &im->bufs.read_data;
    struct image_buf *bc = &im->bufs.read_bc;
    uint32_t start_ticks;
    unsigned int blk = im->a2.nic ? NIC_BLK_DATA : 256;

    /* Half-track steps, one side. */
    track >>= 2;
    if (track != im->cur_track)
        nib_seek_track(im, track);

    start_ticks = start_pos ? *start_pos : get_write(im, im->wr_cons)->start;

    im->cur_ticks = start_ticks * 16;
    im->cur_bc = udiv64((uint64_t)im->cur_ticks * im->tracklen_bc,
                        im->tracklen_ticks);
    if ((im->cur_ticks >= im->tracklen_ticks) ||
        (im->cur_bc >= im->tracklen_bc)) {
        im->cur_ticks = 0;
        im->cur_bc = 0;
    }
    im->ticks_since_flux = 0;

    rd->prod = rd->cons = 0;
    bc->prod = bc->cons = 0;

    if (start_pos) {
        /* Read mode: the ring starts at the block holding the position,
         * and reading the first blocks moves trk_pos past them. */
        uint32_t pos = ((im->cur_bc/8) / blk) * blk;
        im->a2.trk_pos = pos;
        image_read_track(im);
        bc->cons = im->cur_bc - pos * 8;
    } else {
        /* Write mode: the nibbles are collected first. */
        im->a2.write.start = im->cur_bc;
        im->a2.write.nr = 0;
        im->a2.write.acc = im->a2.write.nbits = 0;
        im->a2.write.lost = FALSE;
    }
}

/* Copies @n bytes into the bit-cell ring at byte @pos, around its end. */
static void bc_copy(struct image_buf *bc, uint32_t pos, const uint8_t *src,
                    unsigned int n)
{
    uint8_t *bc_b = bc->p;
    uint32_t mask = bc->len - 1, off = pos & mask;
    unsigned int first = min_t(unsigned int, n, bc->len - off);

    memcpy(&bc_b[off], src, first);
    memcpy(bc_b, src + first, n - first);
}

/* The 6-and-2 nibble of each 6-bit value. */
static const uint8_t gcr62[64] = {
    0x96, 0x97, 0x9a, 0x9b, 0x9d, 0x9e, 0x9f, 0xa6,
    0xa7, 0xab, 0xac, 0xad, 0xae, 0xaf, 0xb2, 0xb3,
    0xb4, 0xb5, 0xb6, 0xb7, 0xb9, 0xba, 0xbb, 0xbc,
    0xbd, 0xbe, 0xbf, 0xcb, 0xcd, 0xce, 0xcf, 0xd3,
    0xd6, 0xd7, 0xd9, 0xda, 0xdb, 0xdc, 0xdd, 0xde,
    0xdf, 0xe5, 0xe6, 0xe7, 0xe9, 0xea, 0xeb, 0xec,
    0xed, 0xee, 0xef, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6,
    0xf7, 0xf9, 0xfa, 0xfb, 0xfc, 0xfd, 0xfe, 0xff
};

/* Bytes 22 to 55 of a .nic block: the last self-sync bytes, 10 bits each,
 * ending at a byte boundary; the address field, its values left out; a gap;
 * the data field's prologue. */
#define DSK_FIXED_OFF (NIC_ADDR_OFF - 12)
static const uint8_t dsk_fixed[NIC_DATA_OFF + 3 - DSK_FIXED_OFF] = {
    0x03, 0xfc, 0xff, 0x3f, 0xcf, 0xf3, 0xfc, 0xff, 0x3f, 0xcf, 0xf3, 0xfc,
    0xd5, 0xaa, 0x96, 0, 0, 0, 0, 0, 0, 0, 0, 0xde, 0xaa, 0xeb,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xd5, 0xaa, 0xad
};
#define DSK_EPILOGUE (&dsk_fixed[NIC_ADDR_LEN - 3 + 12])

/* Bits 0 and 1 of @b swapped: 0, 5, 10 and 15 halved end in 0, 2, 1, 3. */
#define SWAP2(b) ((((b) & 3) * 5 >> 1) & 3)

/* Value @i of the 342 6-bit values that sector data @d is written as: first
 * the low two bits of each byte, swapped, those of three bytes 86 apart to a
 * value, then the high six bits of each byte. */
static unsigned int dsk_value(const uint8_t *d, unsigned int i)
{
    unsigned int j, v = 0;

    if (i >= 86)
        return d[i - 86] >> 2;
    for (j = i + 2*86; ; j -= 86) {
        if (j < 256)
            v = (v << 2) | SWAP2(d[j]);
        if (j == i)
            return v;
    }
}

/* Makes block @b, as a .nic image has it, of physical sector @sec of the
 * current track, whose data @trk holds. */
static void dsk_make_block(struct image *im, uint8_t *b, const uint8_t *trk,
                           unsigned int sec)
{
    const uint8_t *d = &trk[im->a2.file_sec[sec] * 256];
    uint32_t addr = DSK_VOLUME | (im->cur_track << 8) | (sec << 16);
    uint8_t *p;
    unsigned int i, v, x;

    memset(b, 0xff, NIC_BLK_DATA);
    memcpy(b + DSK_FIXED_OFF, dsk_fixed, sizeof(dsk_fixed));

    /* The address field's values, the checksum last, in 4-and-4: odd bits,
     * then even, the bits of the next value masked by 0xaa. */
    addr |= ((addr ^ (addr >> 8) ^ (addr >> 16)) & 0xff) << 24;
    p = b + NIC_ADDR_OFF + 3;
    for (i = 0; i < 4; i++, addr >>= 8) {
        *p++ = (addr >> 1) | 0xaa;
        *p++ = addr | 0xaa;
    }

    /* The data field: each value XORed with the one before, then the last
     * value as the checksum. */
    p = b + NIC_DATA_OFF + 3;
    for (i = x = 0; i < 342; i++) {
        v = dsk_value(d, i);
        *p++ = gcr62[v ^ x];
        x = v;
    }
    *p++ = gcr62[x];
    memcpy(p, DSK_EPILOGUE, 3);
}

static bool_t nib_read_track(struct image *im)
{
    struct image_buf *rd = &im->bufs.read_data;
    struct image_buf *bc = &im->bufs.read_bc;
    uint8_t *buf = rd->p;
    uint32_t bc_len, bc_space, bc_p, bc_c;
    unsigned int nr, i, blk_data, blk_file;

    if (im->a2.dsk) {
        /* The blocks are made after the track's sectors. */
        blk_data = blk_file = NIC_BLK_DATA;
        buf += DSK_TRK_LEN;
    } else if (im->a2.nic) {
        blk_data = NIC_BLK_DATA;
        blk_file = NIC_BLK_LEN;
    } else {
        blk_data = blk_file = 256;
    }

    if (rd->prod == rd->cons) {
        uint8_t *trk = rd->p;
        unsigned int sec = im->a2.trk_pos / blk_data;
        nr = min_t(unsigned int, 8, (TRK_LEN - im->a2.trk_pos) / blk_data);
        if (im->a2.dsk) {
            /* The whole track, of which the blocks are made. */
            F_lseek(&im->fp, im->a2.trk_off);
            F_read(&im->fp, trk, DSK_TRK_LEN, NULL);
            for (i = 0; i < nr; i++, sec++)
                dsk_make_block(im, &buf[i * NIC_BLK_DATA], trk, sec);
        } else {
            F_lseek(&im->fp, im->a2.trk_off + sec * blk_file);
            F_read(&im->fp, buf, nr * blk_file, NULL);
        }
        rd->cons = 0;
        rd->prod = nr;
        im->a2.trk_pos += nr * blk_data;
        if (im->a2.trk_pos >= TRK_LEN)
            im->a2.trk_pos = 0;
    }

    /* Fill the raw-bitcell ring buffer. */
    bc_p = bc->prod / 8;
    bc_c = bc->cons / 8;
    bc_len = bc->len;
    bc_space = bc_len - (uint16_t)(bc_p - bc_c);

    nr = min_t(unsigned int, rd->prod - rd->cons, bc_space / blk_data);
    if (nr == 0)
        return FALSE;

    while (nr--) {
        bc_copy(bc, bc_p, &buf[rd->cons * blk_file], blk_data);
        rd->cons++;
        bc_p += blk_data;
    }

    barrier();
    bc->prod = bc_p * 8;

    return TRUE;
}

/* Puts the @nr nibbles of the write that began at stream byte @pos into a
 * .nib track, around its end. */
static void nib_put_nibbles(struct image *im, const uint8_t *nib,
                            unsigned int nr, uint32_t pos)
{
    unsigned int first;

    if (nr > TRK_LEN)
        nr = TRK_LEN;

    first = min_t(unsigned int, nr, TRK_LEN - pos);
    F_lseek(&im->fp, im->a2.trk_off + pos);
    F_write(&im->fp, nib, first, NULL);
    if (first != nr) {
        F_lseek(&im->fp, im->a2.trk_off);
        F_write(&im->fp, nib + first, nr - first, NULL);
    }
}

/* Decodes data field @nib, its 343 nibbles after the prologue, into sector
 * @sec of the current track, with @buf for its work. */
static void dsk_put_sector(struct image *im, uint8_t *buf, const uint8_t *nib,
                           unsigned int sec)
{
    uint8_t *inv = buf + 256, *aux = inv + 128;
    unsigned int i, v, x = 0;

    /* The value of each nibble by its low seven bits, 64 for none. */
    memset(inv, 64, 128);
    for (i = 0; i < 64; i++)
        inv[gcr62[i] & 0x7f] = i;

    /* Each value XORed with the one before, the checksum last: the low bits
     * of the bytes into @aux, the high bits into place. */
    for (i = 0; i < 343; i++) {
        v = inv[nib[i] & 0x7f];
        if (v >= 64) {
            x = v;
            break;
        }
        x ^= v;
        if (i < 86)
            aux[i] = x;
        else if (i < 342)
            buf[i - 86] = x << 2;
    }
    if (x != 0) {
        /* The other sector images' format, one string in flash for all:
         * the checksum left over, then track[sector]. */
        log("Bad CRC: %04x, %u[%02x]\n", x, im->cur_track, sec);
        return;
    }
    for (i = 0; i < 256; i++)
        buf[i] |= SWAP2(aux[i % 86] >> (2 * (i / 86)));

    F_lseek(&im->fp, im->a2.trk_off + im->a2.file_sec[sec] * 256);
    F_write(&im->fp, buf, 256, NULL);
}

/* Puts the @nr nibbles of the write that began at stream byte @pos into the
 * .nic blocks they belong to, or each data field into its sector of a sector
 * image: an address field names its sector, and a data field follows its
 * address field, or is the one the write began in. */
static void nic_put_nibbles(struct image *im, const uint8_t *nib,
                            unsigned int nr, uint32_t pos)
{
    uint8_t *blk = (uint8_t *)im->bufs.write_data.p + STAGE_BLK;
    unsigned int cur = pos / NIC_BLK_DATA, loaded = ~0u, i = 0;
    unsigned int off, len;
    bool_t dirty = FALSE;

    while (i + 3 <= nr) {
        if ((nib[i] != 0xd5) || (nib[i+1] != 0xaa)) {
            i++;
            continue;
        }
        if ((nib[i+2] == 0x96) && (i + NIC_ADDR_LEN <= nr)) {
            /* The sector is the third pair of the address field. */
            cur = ((nib[i+7] << 1) | 1) & nib[i+8] & (NIC_BLKS_PER_TRK-1);
            off = NIC_ADDR_OFF;
            len = NIC_ADDR_LEN;
        } else if ((nib[i+2] == 0xad) && (i + NIC_DATA_LEN <= nr)) {
            off = NIC_DATA_OFF;
            len = NIC_DATA_LEN;
        } else {
            i++;
            continue;
        }
        if (im->a2.dsk) {
            if (off == NIC_DATA_OFF)
                dsk_put_sector(im, blk, &nib[i+3], cur);
        } else {
            if (loaded != cur) {
                if (dirty) {
                    F_lseek(&im->fp, im->a2.trk_off + loaded * NIC_BLK_LEN);
                    F_write(&im->fp, blk, NIC_BLK_LEN, NULL);
                }
                F_lseek(&im->fp, im->a2.trk_off + cur * NIC_BLK_LEN);
                F_read(&im->fp, blk, NIC_BLK_LEN, NULL);
                loaded = cur;
                dirty = FALSE;
            }
            memcpy(blk + off, nib + i, len);
            dirty = TRUE;
        }
        i += len;
    }

    if (dirty) {
        F_lseek(&im->fp, im->a2.trk_off + loaded * NIC_BLK_LEN);
        F_write(&im->fp, blk, NIC_BLK_LEN, NULL);
    }
}

static bool_t nib_write_track(struct image *im)
{
    struct write *write = get_write(im, im->wr_cons);
    struct image_buf *wr = &im->bufs.write_bc;
    uint8_t *buf = wr->p;
    uint8_t *nib = (uint8_t *)im->bufs.write_data.p + STAGE_NIB;
    unsigned int bufmask = wr->len - 1;
    uint32_t c = wr->cons, p = wr->prod & ~31;
    uint8_t acc = im->a2.write.acc, nbits = im->a2.write.nbits;
    bool_t flush;

    /* The bit cells up to the end of the write, once that is known. */
    barrier();
    flush = (im->wr_cons != im->wr_bc);
    if (flush)
        p = write->bc_end;

    /* Decode as the controller does: a nibble is eight bits from a 1 bit on,
     * and the zeros between nibbles are the sync bits. */
    for (; c != p; c++) {
        unsigned int bit = (buf[(c/8) & bufmask] >> (7 - (c&7))) & 1;
        if ((nbits == 0) && !bit)
            continue;
        acc = (acc << 1) | bit;
        if (++nbits < 8)
            continue;
        if (im->a2.write.nr < STAGE_NIB_MAX)
            nib[im->a2.write.nr++] = acc;
        else
            im->a2.write.lost = TRUE;
        acc = nbits = 0;
    }
    im->a2.write.acc = acc;
    im->a2.write.nbits = nbits;
    wr->cons = c;

    if (!flush)
        return FALSE;

    if (im->a2.write.lost)
        printk("*** Write overflow, %u kept\n", im->a2.write.nr);
    if (im->a2.nic)
        nic_put_nibbles(im, nib, im->a2.write.nr, im->a2.write.start / 8);
    else
        nib_put_nibbles(im, nib, im->a2.write.nr, im->a2.write.start / 8);

    return TRUE;
}

static const struct image_handler nib_image_handler = {
    .setup_track = nib_setup_track,
    .read_track = nib_read_track,
    .rdata_flux = bc_rdata_flux,
    .write_track = nib_write_track,
};

static const struct image_handler nic_image_handler = {
    .setup_track = nib_setup_track,
    .read_track = nib_read_track,
    .rdata_flux = bc_rdata_flux,
    .write_track = nib_write_track,
};

static const struct image_handler a2dsk_image_handler = {
    .setup_track = nib_setup_track,
    .read_track = nib_read_track,
    .rdata_flux = bc_rdata_flux,
    .write_track = nib_write_track,
};

/*
 * Local variables:
 * mode: C
 * c-file-style: "Linux"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */

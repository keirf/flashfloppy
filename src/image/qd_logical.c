/* Sharp byte images. Included by qd.c to share its flux pipeline.
 *
 * MZQ/legacy QD: 00 16 16, marker, count, "CRC"; then count records:
 * 00 16 16, marker, type, little-endian length, payload, "CRC".
 * QDF: 16-byte "-QD format-" header followed by the complete byte stream.
 * Compact emulator QDF uses four sync bytes and "CR", four syncs, 00 trailers.
 * CRC-16 is reflected polynomial 0xa001, initial zero, including the marker.
 * All integers in the on-disk byte images are little endian.
 */

#define QDL_TRACK_LEN 203417u /* Eight seconds, as in mk_qd.py. */
#define QDL_LEAD_IN   12713u  /* Half a second. */
#define QDL_WIN_END   177989u
#define QDL_MAX_BYTES ((QDL_TRACK_LEN-QDL_LEAD_IN-2542-512)/2)

/* Mount-time record index stays in the shared staging buffer. The first 1kB
 * is still the raw read batch; the next 512 bytes cache source-file reads.
 * The index uses at most 4kB and survives reads without additional allocation.
 */
struct qd_logical_block {
    uint32_t off, len, pos;
    uint16_t crc;
    uint16_t pad;
};

static struct qd_logical_block *qd_logical_blocks(struct image *im)
{
    return im->bufs.read_data.p + 2048;
}

static uint8_t qd_logical_file_byte(struct image *im, uint32_t pos)
{
    uint32_t off = pos & ~511u;
    uint8_t *buf = im->bufs.read_data.p + 1024;

    if (im->qd.logical_cache != off) {
        F_lseek(&im->fp, off);
        F_read(&im->fp, buf, min_t(uint32_t, 512, f_size(&im->fp)-off), NULL);
        im->qd.logical_cache = off;
    }
    return buf[pos & 511];
}

static uint16_t qd_logical_crc(uint16_t crc, uint8_t val)
{
    unsigned int i;
    crc ^= val;
    for (i = 0; i < 8; i++)
        crc = (crc >> 1) ^ ((crc & 1) ? 0xa001 : 0);
    return crc;
}

static bool_t qd_logical_open(struct image *im)
{
    struct qd_logical_block *blocks = qd_logical_blocks(im), *b;
    uint8_t hdr[16];
    uint32_t off = 0, pos = 4826, i, j, end, cache_off, prefix = 3, footer = 3;
    bool_t qdf;

    ASSERT(12*512 <= im->bufs.read_data.len);
    if (f_size(&im->fp) < 8)
        return FALSE;
    F_read(&im->fp, hdr, min_t(uint32_t, sizeof(hdr), f_size(&im->fp)), NULL);
    im->qd.logical_cache = ~0u;
    qdf = (f_size(&im->fp) >= sizeof(hdr))
        && !memcmp(hdr, "-QD format-", 11);
    if (qdf && (f_size(&im->fp) >= 29)) {
        F_read(&im->fp, hdr, 8, NULL);
        if (!memcmp(hdr, "\x16\x16\x16\x16", 4)
            && !memcmp(hdr+6, "CR", 2)) {
            off = 16;
            prefix = 4;
            footer = 7;
            im->qd.logical_format = 1;
            qdf = FALSE;
        }
    }
    if (qdf) {
        im->qd.logical_format = 2;
        /* Preserve QDF timing, gaps, sync bytes and stored CRCs verbatim. */
        im->qd.logical_len = f_size(&im->fp) - sizeof(hdr);
        if ((im->qd.logical_len == 0) || (im->qd.logical_len > QDL_MAX_BYTES))
            return FALSE;
        im->qd.trk_off = sizeof(hdr);
    } else {
        if (memcmp(hdr, (prefix == 3) ? "\0\x16\x16"
                   : "\x16\x16\x16\x16", prefix))
            return FALSE;
        im->qd.logical_blocks = hdr[prefix+1] + 1;
        for (i = 0; i < im->qd.logical_blocks; i++) {
            b = &blocks[i];
            if (off + 8 > f_size(&im->fp))
                return FALSE;
            F_lseek(&im->fp, off);
            F_read(&im->fp, hdr, 8, NULL);
            if (memcmp(hdr, (prefix == 3) ? "\0\x16\x16"
                       : "\x16\x16\x16\x16", prefix))
                return FALSE;
            b->off = off + prefix;
            b->len = (i == 0) ? 2
                : 4 + (hdr[prefix+2] | (hdr[prefix+3] << 8));
            b->pos = pos + 10; /* Ten sync characters before each marker. */
            end = b->off + b->len;
            if (end + footer > f_size(&im->fp))
                return FALSE;
            for (j = 0; j < footer; j++)
                if (qd_logical_file_byte(im, end+j)
                    != ((prefix == 3) ? "CRC" : "CR\x16\x16\x16\x16\0")[j])
                    return FALSE;
            b->crc = 0;
            for (j = 0; j < b->len; j++)
                b->crc = qd_logical_crc(
                    b->crc, qd_logical_file_byte(im, b->off+j));
            off = end + footer;
            /* Leave time for the ROM to process the preceding record.
             * The longer gap after the count matches a physical Sharp disk.
             */
            pos = b->pos + b->len + 2 + 7 + ((i == 0) ? 2794 : 256);
            if (pos > QDL_MAX_BYTES)
                return FALSE;
        }
        im->qd.logical_len = pos;
    }

    im->qd.tb = 0;
    im->nr_cyls = im->nr_sides = 1;
    im->write_bc_ticks = sampleclk_ns(4917);
    im->ticks_per_cell = im->write_bc_ticks;
    im->sync = SYNC_none;
    im->qd.trk_len = QDL_TRACK_LEN;
    im->qd.win_start = QDL_LEAD_IN;
    end = max_t(uint32_t, QDL_WIN_END,
                QDL_LEAD_IN + 2*im->qd.logical_len + 512);
    im->qd.win_end = end;
    im->qd.win_start *= im->write_bc_ticks * ((8*STK_MHZ)/SAMPLECLK_MHZ);
    im->qd.win_end *= im->write_bc_ticks * ((8*STK_MHZ)/SAMPLECLK_MHZ);
    cache_off = max_t(uint32_t, 8*512,
                      2048 + im->qd.logical_blocks*sizeof(*blocks));
    cache_off = (cache_off + 511) & ~511u;
    volume_cache_init(im->bufs.read_data.p + cache_off,
                      im->bufs.read_data.p + im->bufs.read_data.len);
    if (im->bufs.read_data.len < 64*1024)
        volume_cache_metadata_only(&im->fp);
    qd_seek_track(im, 0);
    return TRUE;
}

static uint8_t qd_logical_byte(struct image *im, uint32_t pos)
{
    struct qd_logical_block *blocks = qd_logical_blocks(im), *b;
    uint16_t i = im->qd.logical_block;
    int32_t off;

    if (im->qd.logical_format == 2)
        return qd_logical_file_byte(im, im->qd.trk_off + pos);
    while ((i+1 < im->qd.logical_blocks) && (pos >= blocks[i+1].pos-10))
        i++;
    while ((i != 0) && (pos < blocks[i].pos-10))
        i--;
    im->qd.logical_block = i;
    b = &blocks[i];
    off = (int32_t)pos - (int32_t)b->pos;
    if (off < -10)
        return 0;
    if (off < 0)
        return 0x16;
    if (off < b->len)
        return qd_logical_file_byte(im, b->off + off);
    off -= b->len;
    if (off < 2)
        return b->crc >> (off*8);
    return (off < 9) ? 0x16 : 0;
}

static void qd_logical_read(struct image *im, uint8_t *buf, unsigned int len)
{
    uint32_t pos = im->qd.trk_pos, byte;
    unsigned int shift, i, bit, prev;
    uint8_t val, raw;

    /* Encode LSB-first MFM in small batches. Random seeks derive the previous
     * data bit from the source, so byte/chunk/ring boundaries retain clocks.
     */
    while (len--) {
        if ((pos < QDL_LEAD_IN)
            || (pos-QDL_LEAD_IN >= 2*im->qd.logical_len)) {
            *buf++ = 0x11;
            pos++;
            continue;
        }
        byte = (pos-QDL_LEAD_IN)/2;
        shift = ((pos++ - QDL_LEAD_IN) & 1) * 4;
        val = qd_logical_byte(im, byte);
        prev = shift ? ((val >> 3) & 1)
            : byte ? (qd_logical_byte(im, byte-1) >> 7) : 0;
        val >>= shift;
        raw = 0;
        for (i = 0; i < 4; i++) {
            bit = val & 1;
            raw |= ((!prev && !bit) | (bit << 1)) << (2*i);
            prev = bit;
            val >>= 1;
        }
        *buf++ = raw;
    }
}

const struct image_handler qd_logical_image_handler = {
    .open = qd_logical_open,
    .setup_track = qd_setup_track,
    .read_track = qd_read_track,
    .rdata_flux = qd_rdata_flux,
    /* Logical Sharp containers are read-only. Native QD retains writes. */
};

/*
 * da.c
 * 
 * Direct-Access mode for host selector software.
 * 
 * Written & released by Keir Fraser <keir.xen@gmail.com>
 * 
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

#include "../fatfs/diskio.h"

static struct da_status_sector dass = {
    .sig = "HxCFEDA",
    .fw_ver = "FF-v0.1",
};

#define CMD_NOP          0
#define CMD_SET_LBA      1 /* p[0-3] = LBA (little endian) */
#define CMD_SET_CYL      2 /* p[0] = drive A cyl, p[1] = drive B cyl */
#define CMD_SET_RPM      3 /* p[0] = 0x00 -> default, 0xFF -> 300 RPM */
#define CMD_SELECT_IMAGE 4 /* p[0-1] = slot # (little endian) */

#define TRACKLEN_BC 100160 /* multiple of 32 */
#define TICKS_PER_CELL ((sysclk_ms(DRIVE_MS_PER_REV) * 16u) / TRACKLEN_BC)

static const uint16_t mfmtab[] = {
    0xaaaa, 0xaaa9, 0xaaa4, 0xaaa5, 0xaa92, 0xaa91, 0xaa94, 0xaa95, 
    0xaa4a, 0xaa49, 0xaa44, 0xaa45, 0xaa52, 0xaa51, 0xaa54, 0xaa55, 
    0xa92a, 0xa929, 0xa924, 0xa925, 0xa912, 0xa911, 0xa914, 0xa915, 
    0xa94a, 0xa949, 0xa944, 0xa945, 0xa952, 0xa951, 0xa954, 0xa955, 
    0xa4aa, 0xa4a9, 0xa4a4, 0xa4a5, 0xa492, 0xa491, 0xa494, 0xa495, 
    0xa44a, 0xa449, 0xa444, 0xa445, 0xa452, 0xa451, 0xa454, 0xa455, 
    0xa52a, 0xa529, 0xa524, 0xa525, 0xa512, 0xa511, 0xa514, 0xa515, 
    0xa54a, 0xa549, 0xa544, 0xa545, 0xa552, 0xa551, 0xa554, 0xa555, 
    0x92aa, 0x92a9, 0x92a4, 0x92a5, 0x9292, 0x9291, 0x9294, 0x9295, 
    0x924a, 0x9249, 0x9244, 0x9245, 0x9252, 0x9251, 0x9254, 0x9255, 
    0x912a, 0x9129, 0x9124, 0x9125, 0x9112, 0x9111, 0x9114, 0x9115, 
    0x914a, 0x9149, 0x9144, 0x9145, 0x9152, 0x9151, 0x9154, 0x9155, 
    0x94aa, 0x94a9, 0x94a4, 0x94a5, 0x9492, 0x9491, 0x9494, 0x9495, 
    0x944a, 0x9449, 0x9444, 0x9445, 0x9452, 0x9451, 0x9454, 0x9455, 
    0x952a, 0x9529, 0x9524, 0x9525, 0x9512, 0x9511, 0x9514, 0x9515, 
    0x954a, 0x9549, 0x9544, 0x9545, 0x9552, 0x9551, 0x9554, 0x9555, 
    0x4aaa, 0x4aa9, 0x4aa4, 0x4aa5, 0x4a92, 0x4a91, 0x4a94, 0x4a95, 
    0x4a4a, 0x4a49, 0x4a44, 0x4a45, 0x4a52, 0x4a51, 0x4a54, 0x4a55, 
    0x492a, 0x4929, 0x4924, 0x4925, 0x4912, 0x4911, 0x4914, 0x4915, 
    0x494a, 0x4949, 0x4944, 0x4945, 0x4952, 0x4951, 0x4954, 0x4955, 
    0x44aa, 0x44a9, 0x44a4, 0x44a5, 0x4492, 0x4491, 0x4494, 0x4495, 
    0x444a, 0x4449, 0x4444, 0x4445, 0x4452, 0x4451, 0x4454, 0x4455, 
    0x452a, 0x4529, 0x4524, 0x4525, 0x4512, 0x4511, 0x4514, 0x4515, 
    0x454a, 0x4549, 0x4544, 0x4545, 0x4552, 0x4551, 0x4554, 0x4555, 
    0x52aa, 0x52a9, 0x52a4, 0x52a5, 0x5292, 0x5291, 0x5294, 0x5295, 
    0x524a, 0x5249, 0x5244, 0x5245, 0x5252, 0x5251, 0x5254, 0x5255, 
    0x512a, 0x5129, 0x5124, 0x5125, 0x5112, 0x5111, 0x5114, 0x5115, 
    0x514a, 0x5149, 0x5144, 0x5145, 0x5152, 0x5151, 0x5154, 0x5155, 
    0x54aa, 0x54a9, 0x54a4, 0x54a5, 0x5492, 0x5491, 0x5494, 0x5495, 
    0x544a, 0x5449, 0x5444, 0x5445, 0x5452, 0x5451, 0x5454, 0x5455, 
    0x552a, 0x5529, 0x5524, 0x5525, 0x5512, 0x5511, 0x5514, 0x5515, 
    0x554a, 0x5549, 0x5544, 0x5545, 0x5552, 0x5551, 0x5554, 0x5555
};

static bool_t da_seek_track(
    struct image *im, uint16_t track, stk_time_t *start_pos)
{
    struct image_buf *rd = &im->bufs.read_data;
    struct image_buf *mfm = &im->bufs.read_mfm;
    struct da_status_sector *da = rd->p;

    im->tracklen_bc = TRACKLEN_BC;
    im->ticks_since_flux = 0;
    im->cur_track = 255*2;

    im->cur_bc = 0;
    im->cur_ticks = 0;

    rd->prod = rd->cons = 0;
    mfm->prod = mfm->cons = 0;

    if (start_pos) {
        memset(da, 0, 512);
        memcpy(da, &dass, sizeof(dass));
        image_read_track(im);
        *start_pos = 0;
    }

    return TRUE;
}

static bool_t da_read_track(struct image *im)
{
    struct image_buf *rd = &im->bufs.read_data;
    struct image_buf *mfm = &im->bufs.read_mfm;
    uint8_t *buf = rd->p;
    uint16_t *mfmb = mfm->p;
    unsigned int i, mfmlen, mfmp, mfmc;
    uint16_t pr = 0, crc;

    const unsigned int gap3 = 84;
    const unsigned int nr_sec = 9;
    const unsigned int sec_sz = 512;

    /* Read some sectors. */
    if (!rd->prod) {
        if (disk_read(0, buf + sec_sz, dass.lba_base, nr_sec-1) != RES_OK)
            F_die();
        rd->prod = nr_sec * sec_sz;
    }

    /* Generate some MFM if there is space in the MFM ring buffer. */
    mfmp = mfm->prod / 16; /* MFM words */
    mfmc = mfm->cons / 16; /* MFM words */
    mfmlen = mfm->len / 2; /* MFM words */
    if ((mfmlen - (mfmp - mfmc)) < (16 + sec_sz + 2 + gap3))
        return FALSE;

#define emit_raw(r) ({                                  \
    uint16_t _r = (r);                                  \
    mfmb[mfmp++ % mfmlen] = htobe16(_r & ~(pr << 15));  \
    pr = _r; })
#define emit_byte(b) emit_raw(mfmtab[(uint8_t)(b)])
    if (rd->cons == 0) {
        /* IAM */
        for (i = 0; i < 80; i++) /* Gap 4A */
            emit_byte(0x4e);
        for (i = 0; i < 12; i++)
            emit_byte(0x00);
        for (i = 0; i < 3; i++)
            emit_raw(0x5224);
        emit_byte(0xfc);
        for (i = 0; i < 50; i++) /* Gap 1 */
            emit_byte(0x4e);
    } else if (rd->cons == 19) {
        /* Track gap. TODO: Make this dynamically sized. */
        for (i = 0; i < 192; i++) /* Gap 4 */
            emit_byte(0x4e);
        rd->cons = -1;
    } else if (rd->cons & 1) {
        /* IDAM */
        uint8_t cyl = 255, hd = 0, sec = (rd->cons-1) >> 1, no = 2;
        uint8_t idam[8] = { 0xa1, 0xa1, 0xa1, 0xfe, cyl, hd, sec, no };
        for (i = 0; i < 12; i++) /* Pre-sync */
            emit_byte(0x00);
        for (i = 0; i < 3; i++)
            emit_raw(0x4489);
        for (; i < 8; i++)
            emit_byte(idam[i]);
        crc = crc16_ccitt(idam, sizeof(idam), 0xffff);
        emit_byte(crc >> 8);
        emit_byte(crc);
        for (i = 0; i < 22; i++) /* Gap 2 */
            emit_byte(0x4e);
    } else {
        /* DAM */
        uint8_t dam[4] = { 0xa1, 0xa1, 0xa1, 0xfb };
        unsigned int sec = (rd->cons-1) >> 1;
        for (i = 0; i < 12; i++) /* Pre-sync */
            emit_byte(0x00);
        for (i = 0; i < 3; i++)
            emit_raw(0x4489);
        emit_byte(dam[3]);
        for (i = 0; i < sec_sz; i++) /* Data */
            emit_byte(buf[sec*sec_sz+i]);
        crc = crc16_ccitt(dam, sizeof(dam), 0xffff);
        crc = crc16_ccitt(&buf[sec*sec_sz], sec_sz, crc);
        emit_byte(crc >> 8);
        emit_byte(crc);
        for (i = 0; i < gap3; i++) /* Gap 3 */
            emit_byte(0x4e);
    }

    rd->cons++;
    mfm->prod = mfmp * 16;

    return TRUE;
}

static uint16_t da_rdata_flux(struct image *im, uint16_t *tbuf, uint16_t nr)
{
    uint32_t ticks = im->ticks_since_flux, ticks_per_cell = TICKS_PER_CELL;
    uint32_t x, y = 32, todo = nr;
    struct image_buf *mfm = &im->bufs.read_mfm;
    uint32_t *mfmb = mfm->p;

    /* Convert pre-generated MFM into flux timings. */
    while (mfm->cons != mfm->prod) {
        y = mfm->cons % 32;
        x = be32toh(mfmb[(mfm->cons/32)%(mfm->len/4)]) << y;
        mfm->cons += 32 - y;
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
    if (im->cur_bc >= im->tracklen_bc) {
        im->cur_bc -= im->tracklen_bc;
        ASSERT(im->cur_bc < im->tracklen_bc);
        im->tracklen_ticks = im->cur_ticks - im->cur_bc * ticks_per_cell;
        im->cur_ticks -= im->tracklen_ticks;
    }

    mfm->cons -= 32 - y;
    im->cur_bc -= 32 - y;
    im->cur_ticks -= (32 - y) * ticks_per_cell;
    im->ticks_since_flux = ticks;
    return nr - todo;
}

static uint8_t mfmtobin(uint16_t x)
{
    unsigned int i;
    uint8_t y = 0;
    x  = be16toh(x) << 1;
    for (i = 0; i < 8; i++) {
        y <<= 1;
        if ((int16_t)x < 0)
            y |= 1;
        x <<= 2;
    }
    return y;
}

static void da_write_track(struct image *im, bool_t flush)
{
    const uint8_t header[] = { 0xa1, 0xa1, 0xa1, 0xfb };
    const unsigned int sec_sz = 512;

    struct image_buf *wr = &im->bufs.write_mfm;
    uint16_t *buf = wr->p;
    unsigned int buflen = wr->len / 2;
    uint8_t *wrbuf = im->bufs.write_data.p;
    uint32_t c = wr->cons / 16, p = wr->prod / 16;
    uint32_t base = im->write_start / (sysclk_us(2) * 16);
    unsigned int sect, i;
    stk_time_t t;
    uint16_t crc;
    uint8_t x;

    while ((p - c) >= (sec_sz + 6)) {

        /* Scan for sync word and IDAM mark. */
        if (be16toh(buf[c++ % buflen]) != 0x4489)
            continue;
        for (i = 0; i < 3; i++)
            if ((x = mfmtobin(buf[c++ % buflen])) != 0xa1)
                break;
        if (x != 0xfb)
            continue;

        sect = base / 658;
        if (sect > 8) {
            printk("D-A Bad Sector %u\n", sect);
            continue;
        }

        for (i = 0; i < (sec_sz + 2); i++)
            wrbuf[i] = mfmtobin(buf[c++ % buflen]);

        crc = crc16_ccitt(wrbuf, 514, crc16_ccitt(header, 4, 0xffff));
        if (crc != 0) {
            printk("D-A Bad CRC %04x, sector %u\n", crc, sect);
            continue;
        }

        if (sect == 0) {
            struct da_cmd_sector *dac = (struct da_cmd_sector *)wrbuf;
            if (strcmp(dass.sig, dac->sig))
                continue;
            switch (dac->cmd) {
            case CMD_NOP:
                break;
            case CMD_SET_LBA:
                for (i = 0; i < 4; i++) {
                    dass.lba_base <<= 8;
                    dass.lba_base |= dac->param[3-i];
                }
                printk("D-A LBA %08x\n", dass.lba_base);
                break;
            default:
                printk("Unexpected DA Cmd %02x\n", dac->cmd);
                break;
            }
        } else {
            /* All good: write out to mass storage. */
            printk("Write %08x+%u... ", dass.lba_base, sect-1);
            t = stk_now();
            if (disk_write(0, wrbuf, dass.lba_base+sect-1, 1) != RES_OK)
                F_die();
            printk("%u us\n", stk_diff(t, stk_now()) / STK_MHZ);
        }
    }

    wr->cons = c * 16;
}

const struct image_handler da_image_handler = {
    .seek_track = da_seek_track,
    .read_track = da_read_track,
    .rdata_flux = da_rdata_flux,
    .write_track = da_write_track,
    .syncword = 0x44894489
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

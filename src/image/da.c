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
    .fw_ver = "FF-v"FW_VER,
};

#define CMD_NOP          0
#define CMD_SET_LBA      1 /* p[0-3] = LBA (little endian) */
#define CMD_SET_CYL      2 /* p[0] = drive A cyl, p[1] = drive B cyl */
#define CMD_SET_RPM      3 /* p[0] = 0x00 -> default, 0xFF -> 300 RPM */
#define CMD_SELECT_IMAGE 4 /* p[0-1] = slot # (little endian) */

#define TRACKLEN_BC 100160 /* multiple of 32 */
#define TICKS_PER_CELL ((sysclk_ms(DRIVE_MS_PER_REV) * 16u) / TRACKLEN_BC)

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

    return FALSE;
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
#define emit_byte(b) emit_raw(bintomfm(b))
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

    /* Round up the producer index if we are processing final data. */
    if (flush && (wr->prod & 15))
        p++;

    while ((p - c) >= (3 + sec_sz + 2)) {

        /* Scan for sync words and IDAM. Because of the way we sync we expect
         * to see only 2*4489 and thus consume only 3 words for the header. */
        if (be16toh(buf[c++ % buflen]) != 0x4489)
            continue;
        for (i = 0; i < 2; i++)
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
            case CMD_SET_CYL:
                printk("D-A Cyl A=%u B=%u\n", dac->param[0], dac->param[1]);
                for (i = 0; i < 2; i++)
                    floppy_set_cyl(i, dac->param[i]);
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

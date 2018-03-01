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

#define DA_SIG    "HxCFEDA"
#define DA_FW_VER "FF-v"FW_VER

#define CMD_NOP          0
#define CMD_SET_LBA      1 /* p[0-3] = LBA (little endian) */
#define CMD_SET_CYL      2 /* p[0] = drive A cyl, p[1] = drive B cyl */
#define CMD_SET_RPM      3 /* p[0] = 0x00 -> default, 0xFF -> 300 RPM */
#define CMD_SELECT_IMAGE 4 /* p[0-1] = slot # (little endian) */

#define GAP3 84
#define SECSZ 512
#define IAM (80+12+4+50)
#define IDAM (12+8+2+22)
#define DAM (12+4+SECSZ+2+GAP3)
#define GAP4 192
#define TRACKLEN_BC 100160 /* multiple of 32 */

static void da_seek_track(struct image *im)
{
    struct da_status_sector *dass = &im->da.dass;

    im->cur_track = 255*2;

    memset(&im->da, 0, sizeof(im->da));

    snprintf(dass->sig, sizeof(dass->sig), "%s", DA_SIG);
    snprintf(dass->fw_ver, sizeof(dass->fw_ver), "%s",
             (ff_cfg.da_report_version[0] != '\0')
             ? ff_cfg.da_report_version : DA_FW_VER);
    dass->nr_sec = 8;

    im->write_bc_ticks = sysclk_us(2);
    im->ticks_per_cell = im->write_bc_ticks * 16;
}

static void da_setup_track(
    struct image *im, uint16_t track, stk_time_t *start_pos)
{
    struct image_buf *bc = &im->bufs.read_bc;
    unsigned int nsec;

    if (im->cur_track != 255*2)
        da_seek_track(im);

    nsec = im->da.dass.nr_sec + 1;
    im->tracklen_bc = 16 * (IAM + GAP4 + (IDAM + DAM) * nsec);
    im->stk_per_rev = stk_sysclk(im->tracklen_bc * im->write_bc_ticks);

    im->ticks_since_flux = 0;
    im->cur_bc = 0;
    im->cur_ticks = 0;

    im->da.decode_pos = 0;
    bc->prod = bc->cons = 0;

    if (start_pos) {
        image_read_track(im);
        *start_pos = 0;
    }
}

static bool_t da_read_track(struct image *im)
{
    struct da_status_sector *dass = &im->da.dass;
    struct image_buf *rd = &im->bufs.read_data;
    struct image_buf *bc = &im->bufs.read_bc;
    uint8_t *buf = rd->p;
    uint16_t *bc_b = bc->p;
    unsigned int i, bc_len, bc_p, bc_c;
    uint16_t pr = 0, crc;

    const unsigned int gap3 = 84;
    const unsigned int sec_sz = 512;

    /* Generate some MFM if there is space in the raw-bitcell ring buffer. */
    bc_p = bc->prod / 16; /* MFM words */
    bc_c = bc->cons / 16; /* MFM words */
    bc_len = bc->len / 2; /* MFM words */
    if ((bc_len - (bc_p - bc_c)) < (16 + sec_sz + 2 + gap3))
        return FALSE;

#define emit_raw(r) ({                                  \
    uint16_t _r = (r);                                  \
    bc_b[bc_p++ % bc_len] = htobe16(_r & ~(pr << 15));  \
    pr = _r; })
#define emit_byte(b) emit_raw(bintomfm(b))
    if (im->da.decode_pos == 0) {
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
    } else if (im->da.decode_pos == (1 + (dass->nr_sec + 1) * 2)) {
        /* Track gap. TODO: Make this dynamically sized. */
        for (i = 0; i < 192; i++) /* Gap 4 */
            emit_byte(0x4e);
        im->da.decode_pos = -1;
    } else if (im->da.decode_pos & 1) {
        /* IDAM */
        uint8_t cyl = 255, hd = 0, sec = (im->da.decode_pos-1) >> 1, no = 2;
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
        unsigned int sec = (im->da.decode_pos-1) >> 1;
        for (i = 0; i < 12; i++) /* Pre-sync */
            emit_byte(0x00);
        for (i = 0; i < 3; i++)
            emit_raw(0x4489);
        emit_byte(dam[3]);
        if (sec == 0) {
            struct da_status_sector *da = (struct da_status_sector *)buf;
            memset(da, 0, 512);
            memcpy(da, dass, sizeof(*dass));
        } else {
            if (disk_read(0, buf, dass->lba_base+sec-1, 1) != RES_OK)
                F_die(FR_DISK_ERR);
        }
        for (i = 0; i < sec_sz; i++) /* Data */
            emit_byte(buf[i]);
        crc = crc16_ccitt(dam, sizeof(dam), 0xffff);
        crc = crc16_ccitt(buf, sec_sz, crc);
        emit_byte(crc >> 8);
        emit_byte(crc);
        for (i = 0; i < gap3; i++) /* Gap 3 */
            emit_byte(0x4e);
    }

    im->da.decode_pos++;
    bc->prod = bc_p * 16;

    return TRUE;
}

static bool_t da_write_track(struct image *im)
{
    const uint8_t header[] = { 0xa1, 0xa1, 0xa1, 0xfb };
    const unsigned int sec_sz = 512;

    bool_t flush;
    struct da_status_sector *dass = &im->da.dass;
    struct write *write = get_write(im, im->wr_cons);
    struct image_buf *wr = &im->bufs.write_bc;
    uint16_t *buf = wr->p;
    unsigned int buflen = wr->len / 2;
    uint8_t *wrbuf = im->bufs.write_data.p;
    uint32_t c = wr->cons / 16, p = wr->prod / 16;
    uint32_t base = write->start / im->ticks_per_cell; /* in data bytes */
    unsigned int sect, i;
    stk_time_t t;
    uint16_t crc;
    uint8_t x;

    /* If we are processing final data then use the end index, rounded up. */
    barrier();
    flush = (im->wr_cons != im->wr_bc);
    if (flush)
        p = (write->bc_end + 15) / 16;

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
        if (sect > dass->nr_sec) {
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
            if (strcmp(dass->sig, dac->sig))
                continue;
            switch (dac->cmd) {
            case CMD_NOP:
                break;
            case CMD_SET_LBA:
                for (i = 0; i < 4; i++) {
                    dass->lba_base <<= 8;
                    dass->lba_base |= dac->param[3-i];
                }
                dass->nr_sec = (dac->param[5] <= 8) ? 8 : dac->param[5];
                printk("D-A LBA %08x, nr=%u\n", dass->lba_base, dass->nr_sec);
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
            printk("Write %08x+%u... ", dass->lba_base, sect-1);
            t = stk_now();
            if (disk_write(0, wrbuf, dass->lba_base+sect-1, 1) != RES_OK)
                F_die(FR_DISK_ERR);
            printk("%u us\n", stk_diff(t, stk_now()) / STK_MHZ);
        }
    }

    wr->cons = c * 16;

    return flush;
}

const struct image_handler da_image_handler = {
    .setup_track = da_setup_track,
    .read_track = da_read_track,
    .rdata_flux = bc_rdata_flux,
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

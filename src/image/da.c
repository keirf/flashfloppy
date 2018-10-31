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

#define DA_SIG "HxCFEDA"

#define CMD_NOP          0
#define CMD_SET_LBA      1 /* p[0-3] = LBA (little endian) */
#define CMD_SET_CYL      2 /* p[0] = drive A cyl, p[1] = drive B cyl */
#define CMD_SET_RPM      3 /* p[0] = 0x00 -> default, 0xFF -> 300 RPM */
#define CMD_SELECT_IMAGE 4 /* p[0-1] = slot # (little endian) */

#define GAP_SYNC 12 /* Pre-Sync */
#define GAP_1    50 /* Post-IAM */
#define GAP_2    22 /* Post-IDAM */
#define GAP_3    84 /* Post-DAM */
#define GAP_4   192 /* Pre-Index */
#define GAP_4A   80 /* Post-Index */
#define SEC_SZ  512

#define IAM  (GAP_4A + GAP_SYNC + 4 + GAP_1)
#define IDAM (GAP_SYNC + 8 + 2 + GAP_2)
#define DAM  (GAP_SYNC + 4 + SEC_SZ + 2 + GAP_3)

static void da_seek_track(struct image *im)
{
    struct da_status_sector *dass = &im->da.dass;
    bool_t version_override = (ff_cfg.da_report_version[0] != '\0');

    im->cur_track = 255*2;

    memset(&im->da, 0, sizeof(im->da));

    snprintf(dass->sig, sizeof(dass->sig), "%s", DA_SIG);
    snprintf(dass->fw_ver, sizeof(dass->fw_ver),
             version_override ? "%s" : "FF-v%s",
             version_override ? ff_cfg.da_report_version : fw_ver);
    dass->nr_sec = 8;

    im->sync = SYNC_mfm;
    im->write_bc_ticks = sysclk_us(2);
    im->ticks_per_cell = im->write_bc_ticks * 16;
}

static void da_setup_track(
    struct image *im, uint16_t track, uint32_t *start_pos)
{
    struct image_buf *rd = &im->bufs.read_data;
    struct image_buf *bc = &im->bufs.read_bc;
    uint32_t decode_off, sys_ticks = start_pos ? *start_pos : 0;
    unsigned int nsec;

    if (im->cur_track != 255*2)
        da_seek_track(im);

    nsec = im->da.dass.nr_sec + 1;
    im->tracklen_bc = 16 * (IAM + GAP_4 + (IDAM + DAM) * nsec);
    im->stk_per_rev = stk_sysclk(im->tracklen_bc * im->write_bc_ticks);

    im->da.trk_sec = 0;

    im->cur_bc = (sys_ticks * 16) / im->ticks_per_cell;
    im->cur_bc &= ~15;
    if (im->cur_bc >= im->tracklen_bc)
        im->cur_bc = 0;
    im->cur_ticks = im->cur_bc * im->ticks_per_cell;
    im->ticks_since_flux = 0;

    decode_off = im->cur_bc / 16;
    if (decode_off < IAM) {
        im->da.decode_pos = 0;
    } else {
        decode_off -= IAM;
        im->da.decode_pos = decode_off / (IDAM + DAM);
        if (im->da.decode_pos < nsec) {
            im->da.trk_sec = im->da.decode_pos;
            im->da.decode_pos = im->da.decode_pos * 2 + 1;
            decode_off %= IDAM + DAM;
            if (decode_off >= IDAM) {
                decode_off -= IDAM;
                im->da.decode_pos++;
            }
        } else {
            im->da.decode_pos = nsec * 2 + 1;
            decode_off -= nsec * (IDAM + DAM);
       }
    }

    rd->prod = rd->cons = 0;
    bc->prod = bc->cons = 0;

    if (start_pos) {
        image_read_track(im);
        bc->cons = decode_off * 16;
        *start_pos = sys_ticks;
    }
}

static bool_t da_read_track(struct image *im)
{
    struct da_status_sector *dass = &im->da.dass;
    struct image_buf *rd = &im->bufs.read_data;
    struct image_buf *bc = &im->bufs.read_bc;
    uint8_t *buf = rd->p;
    uint16_t *bc_b = bc->p;
    uint32_t bc_len, bc_mask, bc_space, bc_p, bc_c;
    uint16_t pr = 0, crc;
    unsigned int i;

    if (rd->prod == rd->cons) {
        uint8_t sec = im->da.trk_sec;
        if (sec == 0) {
            struct da_status_sector *da = (struct da_status_sector *)buf;
            memset(da, 0, SEC_SZ);
            memcpy(da, dass, sizeof(*dass));
        } else {
            if (disk_read(0, buf, dass->lba_base+sec-1, 1) != RES_OK)
                F_die(FR_DISK_ERR);
        }
        rd->prod++;
        if (++im->da.trk_sec >= (dass->nr_sec + 1))
            im->da.trk_sec = 0;
    }

    /* Generate some MFM if there is space in the raw-bitcell ring buffer. */
    bc_p = bc->prod / 16; /* MFM words */
    bc_c = bc->cons / 16; /* MFM words */
    bc_len = bc->len / 2; /* MFM words */
    bc_mask = bc_len - 1;
    bc_space = bc_len - (uint16_t)(bc_p - bc_c);
    if (bc_space < DAM)
        return FALSE;

#define emit_raw(r) ({                                   \
    uint16_t _r = (r);                                   \
    bc_b[bc_p++ & bc_mask] = htobe16(_r & ~(pr << 15));  \
    pr = _r; })
#define emit_byte(b) emit_raw(bintomfm(b))
    if (im->da.decode_pos == 0) {
        /* IAM */
        for (i = 0; i < GAP_4A; i++)
            emit_byte(0x4e);
        for (i = 0; i < GAP_SYNC; i++)
            emit_byte(0x00);
        for (i = 0; i < 3; i++)
            emit_raw(0x5224);
        emit_byte(0xfc);
        for (i = 0; i < GAP_1; i++)
            emit_byte(0x4e);
    } else if (im->da.decode_pos == (1 + (dass->nr_sec + 1) * 2)) {
        /* Track gap. */
        for (i = 0; i < GAP_4; i++)
            emit_byte(0x4e);
        im->da.decode_pos = -1;
    } else if (im->da.decode_pos & 1) {
        /* IDAM */
        uint8_t cyl = 255, hd = 0, sec = (im->da.decode_pos-1) >> 1, no = 2;
        uint8_t idam[8] = { 0xa1, 0xa1, 0xa1, 0xfe, cyl, hd, sec, no };
        for (i = 0; i < GAP_SYNC; i++)
            emit_byte(0x00);
        for (i = 0; i < 3; i++)
            emit_raw(0x4489);
        for (; i < 8; i++)
            emit_byte(idam[i]);
        crc = crc16_ccitt(idam, sizeof(idam), 0xffff);
        emit_byte(crc >> 8);
        emit_byte(crc);
        for (i = 0; i < GAP_2; i++)
            emit_byte(0x4e);
    } else {
        /* DAM */
        uint8_t dam[4] = { 0xa1, 0xa1, 0xa1, 0xfb };
        for (i = 0; i < GAP_SYNC; i++)
            emit_byte(0x00);
        for (i = 0; i < 3; i++)
            emit_raw(0x4489);
        emit_byte(dam[3]);
        for (i = 0; i < SEC_SZ; i++)
            emit_byte(buf[i]);
        crc = crc16_ccitt(dam, sizeof(dam), 0xffff);
        crc = crc16_ccitt(buf, SEC_SZ, crc);
        emit_byte(crc >> 8);
        emit_byte(crc);
        for (i = 0; i < GAP_3; i++)
            emit_byte(0x4e);
        rd->cons++;
    }

    im->da.decode_pos++;
    bc->prod = bc_p * 16;

    return TRUE;
}

static bool_t da_write_track(struct image *im)
{
    bool_t flush;
    struct da_status_sector *dass = &im->da.dass;
    struct write *write = get_write(im, im->wr_cons);
    struct image_buf *wr = &im->bufs.write_bc;
    uint16_t *buf = wr->p;
    unsigned int bufmask = (wr->len / 2) - 1;
    uint8_t *wrbuf = im->bufs.write_data.p;
    uint32_t c = wr->cons / 16, p = wr->prod / 16;
    uint32_t base = write->start / im->ticks_per_cell; /* in data bytes */
    unsigned int sect, i;
    time_t t;
    uint16_t crc;
    uint8_t x;

    /* If we are processing final data then use the end index, rounded up. */
    barrier();
    flush = (im->wr_cons != im->wr_bc);
    if (flush)
        p = (write->bc_end + 15) / 16;

    while ((int16_t)(p - c) >= (3 + SEC_SZ + 2)) {

        uint32_t _c = c;

        /* Scan for sync words and IDAM. Because of the way we sync we expect
         * to see only 2*4489 and thus consume only 3 words for the header. */
        if (be16toh(buf[c++ & bufmask]) != 0x4489)
            continue;
        for (i = 0; i < 2; i++)
            if ((x = mfmtobin(buf[c++ & bufmask])) != 0xa1)
                break;

        switch (x) {

        case 0x01: /* Named Sector */ {
            uint8_t header[5] = { 0xa1, 0xa1, 0xa1, 0x01, 0x00 };
            sect = header[4] = mfmtobin(buf[c++ & bufmask]);
            crc = crc16_ccitt(header, 5, 0xffff);
            break;
        }

        case 0xfb: /* Ordinary Sector */ {
            const uint8_t header[] = { 0xa1, 0xa1, 0xa1, 0xfb };
            sect = base / (IDAM + DAM);
            crc = crc16_ccitt(header, 4, 0xffff);
            break;
        }

        default: /* Unknown sector type */
            continue;

        }

        if (sect > dass->nr_sec) {
            printk("D-A Bad Sector %u\n", sect);
            continue;
        }

        if ((int16_t)(p - c) < (SEC_SZ + 2)) {
            c = _c;
            break;
        }

        for (i = 0; i < (SEC_SZ + 2); i++)
            wrbuf[i] = mfmtobin(buf[c++ & bufmask]);

        crc = crc16_ccitt(wrbuf, SEC_SZ + 2, crc);
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
                dass->nr_sec = dac->param[5] ?: 8;
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
            t = time_now();
            if (disk_write(0, wrbuf, dass->lba_base+sect-1, 1) != RES_OK)
                F_die(FR_DISK_ERR);
            printk("%u us\n", time_diff(t, time_now()) / TIME_MHZ);
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

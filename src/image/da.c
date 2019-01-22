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

#define SEC_SZ 512

#define CMD_NOP          0
#define CMD_SET_LBA      1 /* p[0-3] = LBA (little endian) */
#define CMD_SET_CYL      2 /* p[0] = drive A cyl, p[1] = drive B cyl */
#define CMD_SET_RPM      3 /* p[0] = 0x00 -> default, 0xFF -> 300 RPM */
#define CMD_SELECT_IMAGE 4 /* p[0-1] = slot # (little endian) */
#define CMD_SELECT_NAME 10 /* p[] = name (c string) */

#define FM_GAP_SYNC   6 /* Pre-Sync */
#define FM_GAP_2     11 /* Post-IDAM */
#define FM_GAP_3     58 /* Post-DAM */
#define FM_GAP_4     94 /* Pre-Index */
#define FM_GAP_4A    16 /* Post-Index */
static bool_t fm_read_track(struct image *im);
static bool_t fm_write_track(struct image *im);

#define MFM_GAP_SYNC 12 /* Pre-Sync */
#define MFM_GAP_1    50 /* Post-IAM */
#define MFM_GAP_2    22 /* Post-IDAM */
#define MFM_GAP_3    84 /* Post-DAM */
#define MFM_GAP_4   192 /* Pre-Index */
#define MFM_GAP_4A   80 /* Post-Index */
static bool_t mfm_read_track(struct image *im);
static bool_t mfm_write_track(struct image *im);

static void process_wdata(struct image *im, unsigned int sect, uint16_t crc);

static unsigned int enc_sec_sz(struct image *im)
{
    return im->da.idam_sz + im->da.dam_sz;
}

static void da_seek_track(struct image *im, uint16_t track)
{
    struct da_status_sector *dass = &im->da.dass;
    bool_t version_override = (ff_cfg.da_report_version[0] != '\0');

    if (im->cur_track == track)
        return;
    im->cur_track = track;

    switch (display_mode) {
    case DM_LED_7SEG:
        led_7seg_write_string((led_7seg_nr_digits() == 3) ? "D-A" : "DA");
        break;
    case DM_LCD_1602:
        lcd_clear();
        lcd_write((lcd_columns-11)/2, 0, 0, "Host Direct");
        lcd_write((lcd_columns- 6)/2, 1, 0, "Access");
        break;
    }

    memset(&im->da, 0, sizeof(im->da));

    snprintf(dass->sig, sizeof(dass->sig), "%s", DA_SIG);
    snprintf(dass->fw_ver, sizeof(dass->fw_ver),
             version_override ? "%s" : "FF-v%s",
             version_override ? ff_cfg.da_report_version : fw_ver);
    dass->current_index = get_slot_nr();

    switch (im->cur_track>>1) {
    case DA_SD_FM_CYL:
        dass->nr_sec = 4;
        im->sync = SYNC_fm;
        im->write_bc_ticks = sysclk_us(4);
        break;
    default:
        dass->nr_sec = 8;
        im->sync = SYNC_mfm;
        im->write_bc_ticks = sysclk_us(2);
        break;
    }

    im->ticks_per_cell = im->write_bc_ticks * 16;
}

static void da_setup_track(
    struct image *im, uint16_t track, uint32_t *start_pos)
{
    struct image_buf *rd = &im->bufs.read_data;
    struct image_buf *bc = &im->bufs.read_bc;
    uint32_t decode_off, sys_ticks = start_pos ? *start_pos : 0;
    unsigned int nsec;

    da_seek_track(im, track);

    nsec = im->da.dass.nr_sec + 1;
    switch (im->sync) {
    case SYNC_fm:
        im->da.idx_sz = FM_GAP_4A;
        im->da.idam_sz = FM_GAP_SYNC + 5 + 2 + FM_GAP_2;
        im->da.dam_sz = FM_GAP_SYNC + 1 + SEC_SZ + 2 + FM_GAP_3;
        im->tracklen_bc = FM_GAP_4;
        break;
    default:
        im->da.idx_sz = MFM_GAP_4A + MFM_GAP_SYNC + 4 + MFM_GAP_1;
        im->da.idam_sz = MFM_GAP_SYNC + 8 + 2 + MFM_GAP_2;
        im->da.dam_sz = MFM_GAP_SYNC + 4 + SEC_SZ + 2 + MFM_GAP_3;
        im->tracklen_bc = MFM_GAP_4;
        break;
    }

    im->tracklen_bc += enc_sec_sz(im) * nsec;
    im->tracklen_bc += im->da.idx_sz;
    im->tracklen_bc *= 16;

    im->stk_per_rev = stk_sysclk(im->tracklen_bc * im->write_bc_ticks);

    im->da.trk_sec = 0;

    im->cur_bc = (sys_ticks * 16) / im->ticks_per_cell;
    im->cur_bc &= ~15;
    if (im->cur_bc >= im->tracklen_bc)
        im->cur_bc = 0;
    im->cur_ticks = im->cur_bc * im->ticks_per_cell;
    im->ticks_since_flux = 0;

    decode_off = im->cur_bc / 16;
    if (decode_off < im->da.idx_sz) {
        im->da.decode_pos = 0;
    } else {
        decode_off -= im->da.idx_sz;
        im->da.decode_pos = decode_off / enc_sec_sz(im);
        if (im->da.decode_pos < nsec) {
            im->da.trk_sec = im->da.decode_pos;
            im->da.decode_pos = im->da.decode_pos * 2 + 1;
            decode_off %= enc_sec_sz(im);
            if (decode_off >= im->da.idam_sz) {
                decode_off -= im->da.idam_sz;
                im->da.decode_pos++;
            }
        } else {
            im->da.decode_pos = nsec * 2 + 1;
            decode_off -= nsec * enc_sec_sz(im);
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
    uint8_t *buf = rd->p;

    if (rd->prod == rd->cons) {
        uint8_t sec = im->da.trk_sec;
        if (sec == 0) {
            struct da_status_sector *da = (struct da_status_sector *)buf;
            memset(da, 0, SEC_SZ);
            memcpy(da, dass, sizeof(*dass));
            dass->read_cnt++;
        } else if (dass->lba_base == ~0u) {
            memset(buf, 0, SEC_SZ);
            if (sec == 1)
                strcpy((char *)buf, im->slot->name);
        } else {
            if (disk_read(0, buf, dass->lba_base+sec-1, 1) != RES_OK)
                F_die(FR_DISK_ERR);
        }
        rd->prod++;
        if (++im->da.trk_sec >= (dass->nr_sec + 1))
            im->da.trk_sec = 0;
    }

    return (im->sync == SYNC_fm) ? fm_read_track(im) : mfm_read_track(im);
}

static bool_t fm_read_track(struct image *im)
{
    struct da_status_sector *dass = &im->da.dass;
    struct image_buf *bc = &im->bufs.read_bc;
    struct image_buf *rd = &im->bufs.read_data;
    uint8_t *buf = rd->p;
    uint16_t *bc_b = bc->p;
    uint32_t bc_len, bc_mask, bc_space, bc_p, bc_c;
    uint16_t crc;
    unsigned int i;

    /* Generate some FM if there is space in the raw-bitcell ring buffer. */
    bc_p = bc->prod / 16; /* FM words */
    bc_c = bc->cons / 16; /* FM words */
    bc_len = bc->len / 2; /* FM words */
    bc_mask = bc_len - 1;
    bc_space = bc_len - (uint16_t)(bc_p - bc_c);
    if (bc_space < im->da.dam_sz)
        return FALSE;

#define emit_raw(r) ({                          \
    uint16_t _r = (r);                          \
    bc_b[bc_p++ & bc_mask] = htobe16(_r); })
#define emit_byte(b) emit_raw(bintomfm(b) | 0xaaaa)
    if (im->da.decode_pos == 0) {
        /* Post-index track gap */
        for (i = 0; i < FM_GAP_4A; i++)
            emit_byte(0xff);
    } else if (im->da.decode_pos == (1 + (dass->nr_sec + 1) * 2)) {
        /* Pre-index track gap */
        for (i = 0; i < FM_GAP_4; i++)
            emit_byte(0xff);
        im->da.decode_pos = -1;
    } else if (im->da.decode_pos & 1) {
        /* IDAM */
        uint8_t cyl = 254, hd = 0, sec = (im->da.decode_pos-1) >> 1, no = 2;
        uint8_t idam[5] = { 0xfe, cyl, hd, sec, no };
        for (i = 0; i < FM_GAP_SYNC; i++)
            emit_byte(0x00);
        emit_raw(fm_sync(idam[0], FM_SYNC_CLK));
        for (i = 1; i < 5; i++)
            emit_byte(idam[i]);
        crc = crc16_ccitt(idam, sizeof(idam), 0xffff);
        emit_byte(crc >> 8);
        emit_byte(crc);
        for (i = 0; i < FM_GAP_2; i++)
            emit_byte(0xff);
    } else {
        /* DAM */
        uint8_t dam[1] = { 0xfb };
        for (i = 0; i < FM_GAP_SYNC; i++)
            emit_byte(0x00);
        emit_raw(fm_sync(dam[0], FM_SYNC_CLK));
        for (i = 0; i < SEC_SZ; i++)
            emit_byte(buf[i]);
        crc = crc16_ccitt(dam, sizeof(dam), 0xffff);
        crc = crc16_ccitt(buf, SEC_SZ, crc);
        emit_byte(crc >> 8);
        emit_byte(crc);
        for (i = 0; i < FM_GAP_3; i++)
            emit_byte(0xff);
        rd->cons++;
    }
#undef emit_raw
#undef emit_byte

    im->da.decode_pos++;
    bc->prod = bc_p * 16;

    return TRUE;
}

static bool_t mfm_read_track(struct image *im)
{
    struct da_status_sector *dass = &im->da.dass;
    struct image_buf *bc = &im->bufs.read_bc;
    struct image_buf *rd = &im->bufs.read_data;
    uint8_t *buf = rd->p;
    uint16_t *bc_b = bc->p;
    uint32_t bc_len, bc_mask, bc_space, bc_p, bc_c;
    uint16_t pr = 0, crc;
    unsigned int i;

    /* Generate some MFM if there is space in the raw-bitcell ring buffer. */
    bc_p = bc->prod / 16; /* MFM words */
    bc_c = bc->cons / 16; /* MFM words */
    bc_len = bc->len / 2; /* MFM words */
    bc_mask = bc_len - 1;
    bc_space = bc_len - (uint16_t)(bc_p - bc_c);
    if (bc_space < im->da.dam_sz)
        return FALSE;

#define emit_raw(r) ({                                   \
    uint16_t _r = (r);                                   \
    bc_b[bc_p++ & bc_mask] = htobe16(_r & ~(pr << 15));  \
    pr = _r; })
#define emit_byte(b) emit_raw(bintomfm(b))
    if (im->da.decode_pos == 0) {
        /* IAM */
        for (i = 0; i < MFM_GAP_4A; i++)
            emit_byte(0x4e);
        for (i = 0; i < MFM_GAP_SYNC; i++)
            emit_byte(0x00);
        for (i = 0; i < 3; i++)
            emit_raw(0x5224);
        emit_byte(0xfc);
        for (i = 0; i < MFM_GAP_1; i++)
            emit_byte(0x4e);
    } else if (im->da.decode_pos == (1 + (dass->nr_sec + 1) * 2)) {
        /* Track gap. */
        for (i = 0; i < MFM_GAP_4; i++)
            emit_byte(0x4e);
        im->da.decode_pos = -1;
    } else if (im->da.decode_pos & 1) {
        /* IDAM */
        uint8_t cyl = 255, hd = 0, sec = (im->da.decode_pos-1) >> 1, no = 2;
        uint8_t idam[8] = { 0xa1, 0xa1, 0xa1, 0xfe, cyl, hd, sec, no };
        for (i = 0; i < MFM_GAP_SYNC; i++)
            emit_byte(0x00);
        for (i = 0; i < 3; i++)
            emit_raw(0x4489);
        for (; i < 8; i++)
            emit_byte(idam[i]);
        crc = crc16_ccitt(idam, sizeof(idam), 0xffff);
        emit_byte(crc >> 8);
        emit_byte(crc);
        for (i = 0; i < MFM_GAP_2; i++)
            emit_byte(0x4e);
    } else {
        /* DAM */
        uint8_t dam[4] = { 0xa1, 0xa1, 0xa1, 0xfb };
        for (i = 0; i < MFM_GAP_SYNC; i++)
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
        for (i = 0; i < MFM_GAP_3; i++)
            emit_byte(0x4e);
        rd->cons++;
    }
#undef emit_raw
#undef emit_byte

    im->da.decode_pos++;
    bc->prod = bc_p * 16;

    return TRUE;
}

static bool_t da_write_track(struct image *im)
{
    return (im->sync == SYNC_fm) ? fm_write_track(im) : mfm_write_track(im);
}

static bool_t fm_write_track(struct image *im)
{
    bool_t flush;
    struct write *write = get_write(im, im->wr_cons);
    struct image_buf *wr = &im->bufs.write_bc;
    uint16_t *buf = wr->p;
    unsigned int bufmask = (wr->len / 2) - 1;
    uint8_t *wrbuf = im->bufs.write_data.p;
    uint32_t c = wr->cons / 16, p = wr->prod / 16;
    uint32_t base = write->start / im->ticks_per_cell; /* in data bytes */
    unsigned int sect, i;
    uint16_t sync;
    uint8_t x;

    /* If we are processing final data then use the end index, rounded up. */
    barrier();
    flush = (im->wr_cons != im->wr_bc);
    if (flush)
        p = (write->bc_end + 15) / 16;

    while ((int16_t)(p - c) >= (2 + SEC_SZ + 2)) {

        if (buf[c++ & bufmask] != 0xaaaa)
            continue;
        sync = buf[c & bufmask];
        if (mfmtobin(sync >> 1) != FM_SYNC_CLK)
            continue;
        x = mfmtobin(sync);
        c++;

        if (x != 0xfb)
            continue;

        sect = (base - im->da.idx_sz - im->da.idam_sz + enc_sec_sz(im)/2)
            / enc_sec_sz(im);

        for (i = 0; i < (SEC_SZ + 2); i++)
            wrbuf[i] = mfmtobin(buf[c++ & bufmask]);

        process_wdata(im, sect, crc16_ccitt(&x, 1, 0xffff));
    }

    wr->cons = c * 16;

    return flush;
}

static bool_t mfm_write_track(struct image *im)
{
    bool_t flush;
    struct write *write = get_write(im, im->wr_cons);
    struct image_buf *wr = &im->bufs.write_bc;
    uint16_t *buf = wr->p;
    unsigned int bufmask = (wr->len / 2) - 1;
    uint8_t *wrbuf = im->bufs.write_data.p;
    uint32_t c = wr->cons / 16, p = wr->prod / 16;
    uint32_t base = write->start / im->ticks_per_cell; /* in data bytes */
    unsigned int sect, i;
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
            sect = (base - im->da.idx_sz - im->da.idam_sz + enc_sec_sz(im)/2)
                / enc_sec_sz(im);
            crc = crc16_ccitt(header, 4, 0xffff);
            break;
        }

        default: /* Unknown sector type */
            continue;

        }

        if ((int16_t)(p - c) < (SEC_SZ + 2)) {
            c = _c;
            break;
        }

        for (i = 0; i < (SEC_SZ + 2); i++)
            wrbuf[i] = mfmtobin(buf[c++ & bufmask]);

        process_wdata(im, sect, crc);
    }

    wr->cons = c * 16;

    return flush;
}

static void process_wdata(struct image *im, unsigned int sect, uint16_t crc)
{
    struct da_status_sector *dass = &im->da.dass;
    uint8_t *wrbuf = im->bufs.write_data.p;
    unsigned int i;
    time_t t;

    crc = crc16_ccitt(wrbuf, SEC_SZ + 2, crc);
    if ((crc != 0) || (sect > dass->nr_sec)) {
        printk("D-A Bad Sector: CRC %04x, ID %u\n", crc, sect);
        return;
    }

    if (sect == 0) {
        struct da_cmd_sector *dac = (struct da_cmd_sector *)wrbuf;
        dass->cmd_cnt++;
        dass->last_cmd_status = 1; /* error */
        if (strcmp(dass->sig, dac->sig)) {
            dac->sig[7] = '\0';
            printk("D-A Bad Sig: '%s'\n", dac->sig);
            return;
        }
        switch (dac->cmd) {
        case CMD_NOP:
            dass->last_cmd_status = 0; /* ok */
            break;
        case CMD_SET_LBA:
            for (i = 0; i < 4; i++) {
                dass->lba_base <<= 8;
                dass->lba_base |= dac->param[3-i];
            }
            dass->nr_sec = dac->param[5] ?: (im->sync == SYNC_fm) ? 4 : 8;
            printk("D-A LBA %08x, nr=%u\n", dass->lba_base, dass->nr_sec);
            dass->last_cmd_status = 0; /* ok */
            break;
        case CMD_SET_CYL:
            printk("D-A Cyl A=%u B=%u\n", dac->param[0], dac->param[1]);
            for (i = 0; i < 2; i++)
                floppy_set_cyl(i, dac->param[i]);
            dass->last_cmd_status = 0; /* ok */
            break;
        case CMD_SELECT_IMAGE: {
            uint16_t index = dac->param[0] | ((uint16_t)dac->param[1] << 8);
            bool_t ok = set_slot_nr(index);
            printk("D-A Img %u -> %u (%s)\n",
                   dass->current_index, index, ok ? "OK" : "Bad");
            if (ok) {
                dass->current_index = index;
                dass->last_cmd_status = 0;
            }
            break;
        }
        case CMD_SELECT_NAME: {
            int index;
            char *name = (char *)dac->param;
            name[FF_MAX_LFN] = '\0';
            index = set_slot_by_name(name, wrbuf + 512);
            printk("D-A Img By Name \"%s\" %u -> %d\n",
                   name, dass->current_index, index);
            if (index >= 0) {
                dass->current_index = index;
                dass->last_cmd_status = 0;
            }
            break;
        }
        default:
            printk("Unexpected DA Cmd %02x\n", dac->cmd);
            break;
        }
    } else if (dass->lba_base != ~0u) {
        /* All good: write out to mass storage. */
        dass->write_cnt++;
        printk("Write %08x+%u... ", dass->lba_base, sect-1);
        t = time_now();
        if (disk_write(0, wrbuf, dass->lba_base+sect-1, 1) != RES_OK)
            F_die(FR_DISK_ERR);
        printk("%u us\n", time_diff(t, time_now()) / TIME_MHZ);
    }
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

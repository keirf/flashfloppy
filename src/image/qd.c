/*
 * qd.c
 * 
 * Quick Disk image files.
 * 
 * Written & released by Keir Fraser <keir.xen@gmail.com>
 * 
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

struct disk_header {
    char sig[8];        /* ...QD... */
};

struct track_header {
    uint32_t offset;    /* Byte offset to track data */
    uint32_t len;       /* Byte length of track data */
    uint32_t win_start; /* Byte offset of read/write window start */
    uint32_t win_end;   /* Byte offset of read/write window end */
};

#define MAX_BC_SECS 4

static void qd_seek_track(struct image *im, uint16_t track);

static bool_t qd_open(struct image *im)
{
    struct disk_header dh;

    F_read(&im->fp, &dh, sizeof(dh), NULL);
    if (strncmp(&dh.sig[3], "QD", 2))
        return FALSE;

    im->qd.tb = 1;
    im->nr_cyls = 1;
    im->nr_sides = 1;
    im->write_bc_ticks = sysclk_us(4) + 66; /* 4.917us */
    im->ticks_per_cell = im->write_bc_ticks;
    im->sync = SYNC_none;

    /* There is only one track: Seek to it. */
    qd_seek_track(im, 0);

    return TRUE;
}

static void qd_seek_track(struct image *im, uint16_t track)
{
    struct track_header thdr;
    uint32_t trk_off;

    F_lseek(&im->fp, im->qd.tb*512 + (track/2)*16);
    F_read(&im->fp, &thdr, sizeof(thdr), NULL);

    /* Byte offset and length of track data. */
    trk_off = le32toh(thdr.offset);
    im->qd.trk_len = le32toh(thdr.len);

    /* Read/write window limits in STK ticks from data start. */
    im->qd.win_start = le32toh(thdr.win_start) * im->write_bc_ticks;
    im->qd.win_end = le32toh(thdr.win_end) * im->write_bc_ticks;

    im->tracklen_bc = im->qd.trk_len * 8;
    im->stk_per_rev = stk_sysclk(im->tracklen_bc * im->write_bc_ticks);

    ring_io_init(&im->qd.ring_io, &im->fp, &im->bufs.read_data,
            trk_off, ~0, (im->qd.trk_len+511) / 512);
    im->qd.ring_io.trailing_secs = MAX_BC_SECS;

    im->cur_track = track;
}

static void qd_setup_track(
    struct image *im, uint16_t track, uint32_t *start_pos)
{
    struct image_buf *bc = &im->bufs.read_bc;
    uint32_t sys_ticks;

    sys_ticks = start_pos ? *start_pos : get_write(im, im->wr_cons)->start;
    im->cur_bc = sys_ticks / im->ticks_per_cell;
    if (im->cur_bc >= im->tracklen_bc)
        im->cur_bc = 0;
    im->cur_ticks = im->cur_bc * im->ticks_per_cell;
    im->ticks_since_flux = 0;

    sys_ticks = im->cur_ticks;

    bc->prod = bc->cons = 0;

    if (start_pos) {
        /* Read mode. */
        im->qd.ring_io.batch_secs = 2;
        ring_io_seek(&im->qd.ring_io, (im->cur_bc/8) & ~511, FALSE, FALSE);
        /* Consumer may be ahead of producer, but only until the first read
         * completes. */
        bc->cons = im->cur_bc & 4095;
        *start_pos = sys_ticks;
    } else {
        /* Write mode. */
        im->qd.ring_io.batch_secs = 8;
        ring_io_seek(&im->qd.ring_io, im->cur_bc/8, TRUE, FALSE);
    }
}

static bool_t qd_read_track(struct image *im)
{
    struct image_buf *rd = &im->bufs.read_data;
    struct image_buf *bc = &im->bufs.read_bc;
    uint8_t *buf = rd->p;
    uint8_t *bc_b = bc->p;
    uint32_t bc_len, bc_mask, bc_space, bc_p, bc_c;
    unsigned int nr_sec;

    ring_io_progress(&im->qd.ring_io);
    if (rd->cons >= rd->prod)
        return FALSE;

    /* Fill the raw-bitcell ring buffer. */
    bc_p = bc->prod / 8;
    bc_c = bc->cons / 8;
    bc_len = bc->len;
    bc_mask = bc_len - 1;
    bc_space = min_t(uint32_t, bc_len, MAX_BC_SECS*512)
        - (uint16_t)(bc_p - bc_c);

    nr_sec = min_t(unsigned int, (rd->prod - rd->cons)/512, bc_space/512);
    if (nr_sec == 0)
        return FALSE;

    while (nr_sec--) {
        memcpy(&bc_b[bc_p & bc_mask],
               &buf[ring_io_idx(&im->qd.ring_io, rd->cons)],
               512);
        rd->cons += 512;
        bc_p += 512;
    }

    barrier();
    bc->prod = bc_p * 8;

    return TRUE;
}

static uint16_t qd_rdata_flux(struct image *im, uint16_t *tbuf, uint16_t nr)
{
    struct image_buf *bc = &im->bufs.read_bc;
    uint8_t *bc_b = bc->p;
    uint32_t bc_c = bc->cons, bc_p = bc->prod, bc_mask = bc->len - 1;
    uint32_t ticks = im->ticks_since_flux;
    uint32_t ticks_per_cell = im->ticks_per_cell;
    uint32_t y = 8, todo = nr;
    uint8_t x;

    while ((uint32_t)(bc_p - bc_c) >= 8) {
        ASSERT(y == 8);
        if (im->cur_bc >= im->tracklen_bc) {
            ASSERT(im->cur_bc == im->tracklen_bc);
            im->tracklen_ticks = im->cur_ticks;
            im->cur_bc = im->cur_ticks = 0;
            /* Skip tail of current 512-byte block. */
            bc_c = (bc_c + 512*8-1) & ~(512*8-1);
            continue;
        }
        y = bc_c % 8;
        x = bc_b[(bc_c/8) & bc_mask] >> y;
        bc_c += 8 - y;
        im->cur_bc += 8 - y;
        im->cur_ticks += (8 - y) * ticks_per_cell;
        while (y < 8) {
            y++;
            ticks += ticks_per_cell;
            if (x & 1) {
                *tbuf++ = ticks - 1;
                ticks = 0;
                if (!--todo)
                    goto out;
            }
            x >>= 1;
        }
    }

out:
    bc->cons = bc_c - (8 - y);
    im->cur_bc -= 8 - y;
    im->cur_ticks -= (8 - y) * ticks_per_cell;
    im->ticks_since_flux = ticks;
    return nr - todo;
}

static bool_t qd_write_track(struct image *im)
{
    bool_t flush;
    struct write *write = get_write(im, im->wr_cons);
    struct image_buf *wr = &im->bufs.write_bc;
    uint8_t *buf = wr->p;
    unsigned int bufmask = wr->len - 1;
    uint8_t *w;
    struct image_buf *rd = &im->bufs.read_data;
    uint32_t i, space, c = wr->cons / 8, p = wr->prod / 8;

    /* If we are processing final data then use the end index, rounded to
     * nearest. */
    barrier();
    flush = (im->wr_cons != im->wr_bc);
    if (flush)
        p = (write->bc_end + 4) / 8;

    for (;;) {

        uint32_t pos = ring_io_pos(&im->qd.ring_io, rd->cons);
        UINT nr;

        /* All bytes remaining in the raw-bitcell buffer. */
        nr = space = (p - c) & bufmask;
        /* Limit to end of current 512-byte QD block. */
        nr = min_t(UINT, nr, 512 - (pos & 511));
        /* Limit to end of QD track. */
        nr = min_t(UINT, nr, im->qd.trk_len - pos);

        /* Bail if no bytes to write. */
        if (nr == 0)
            break;

        /* It should be quite rare to wait on the read, as that'd be like a
         * buffer underrun during normal reading. */
        if (rd->cons + nr > rd->prod) {
            flush = FALSE;
            break;
        }

        /* Encode into the sector buffer for later write-out. */
        w = rd->p + ring_io_idx(&im->qd.ring_io, rd->cons);
        for (i = 0; i < nr; i++)
            *w++ = _rbit32(buf[c++ & bufmask]) >> 24;

        rd->cons += nr;
        if (pos + nr >= im->qd.trk_len) {
            ASSERT(pos + nr == im->qd.trk_len);
            ring_io_flush(&im->qd.ring_io);
            rd->cons += 512 - pos%512;
        }
    }

    if (flush)
        ring_io_flush(&im->qd.ring_io);
    else
        ring_io_progress(&im->qd.ring_io);

    wr->cons = c * 8;

    return flush;
}

static void qd_sync(struct image *im)
{
    ring_io_sync(&im->qd.ring_io);
    ring_io_shutdown(&im->qd.ring_io);
}

const struct image_handler qd_image_handler = {
    .open = qd_open,
    .setup_track = qd_setup_track,
    .read_track = qd_read_track,
    .rdata_flux = qd_rdata_flux,
    .write_track = qd_write_track,
    .sync = qd_sync,

    .async = TRUE,
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

/*
 * test_img.c
 *
 * Raw sector images, of IBM PC floppies and TR-DOS disks: the MFM tracks
 * they stream, decoded back into sectors and compared with the image, and
 * sectors written to them.
 *
 * Written & released by Mike Shevchenko <mike.shevchenko.kz@gmail.com>
 *
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

#include "harness.h"

#define MAX_SECS 32

struct sector {
    uint8_t c, h, r, n;
    bool_t data_ok;
    uint32_t data_cell; /* where the data starts in the track's cells */
    uint8_t data[1024];
};

/* An image's geometry: sectors numbered from 1, cylinder by cylinder. */
struct geom {
    unsigned int cyls, heads, secs, size;
    int id_head; /* the head in the sectors' IDs, -1 for the side */
};

static const struct geom pc720 = { 80, 2, 9, 512, -1 };
static const struct geom pc1440 = { 80, 2, 18, 512, -1 };

static uint8_t img[80*2*18*512];
static uint8_t cells[64*1024];
static struct sector secs[MAX_SECS];

static uint8_t *sector(const struct geom *g, unsigned int c, unsigned int h,
                       unsigned int r)
{
    return &img[((c*g->heads + h)*g->secs + r-1) * g->size];
}

/* The made-up contents of each sector: its cylinder, head and number, then
 * steps of 37. */
static void make_img(const struct geom *g)
{
    unsigned int c, h, r, i;
    uint8_t *p;

    for (c = 0; c < g->cyls; c++) {
        for (h = 0; h < g->heads; h++) {
            for (r = 1; r <= g->secs; r++) {
                p = sector(g, c, h, r);
                p[0] = c;
                p[1] = h;
                p[2] = r;
                for (i = 3; i < g->size; i++)
                    p[i] = c*7 + h*91 + r*13 + i*37;
            }
        }
    }
}

static uint32_t img_size(const struct geom *g)
{
    return g->cyls * g->heads * g->secs * g->size;
}

static unsigned int cell(uint32_t i)
{
    return (cells[i/8] >> (7 - (i & 7))) & 1;
}

/* The byte whose MFM cells start at cell @i: the data bits are the odd
 * cells. */
static uint8_t mfm_byte(uint32_t i)
{
    unsigned int k, b = 0;
    for (k = 0; k < 8; k++)
        b = (b << 1) | cell(i + 2*k + 1);
    return b;
}

static uint16_t mfm_word(uint32_t i)
{
    unsigned int k, w = 0;
    for (k = 0; k < 16; k++)
        w = (w << 1) | cell(i + k);
    return w;
}

/* The sectors of a track of @nr_cells cells: after each sync of three A1
 * with a missing clock, cells 4489, an ID or a data mark. */
static unsigned int mfm_decode(uint32_t nr_cells)
{
    struct sector *s = NULL;
    uint8_t buf[4 + 1024 + 2];
    unsigned int nr = 0, i, len;
    uint32_t pos;

    for (pos = 0; pos + 48 + 16 < nr_cells; pos++) {
        if ((mfm_word(pos) != 0x4489) || (mfm_word(pos + 16) != 0x4489)
            || (mfm_word(pos + 32) != 0x4489))
            continue;
        memset(buf, 0xa1, 3);
        buf[3] = mfm_byte(pos + 48);
        if ((buf[3] == 0xfe) && (nr < MAX_SECS)) {
            len = 4 + 4 + 2;
            for (i = 4; i < len; i++)
                buf[i] = mfm_byte(pos + 48 + (i - 3) * 16);
            if (crc16_ccitt(buf, len, 0xffff) != 0)
                continue;
            s = &secs[nr++];
            s->c = buf[4];
            s->h = buf[5];
            s->r = buf[6];
            s->n = buf[7];
            s->data_ok = FALSE;
        } else if ((buf[3] == 0xfb) && (s != NULL) && (s->n <= 3)) {
            len = 4 + (128 << s->n) + 2;
            for (i = 4; i < len; i++)
                buf[i] = mfm_byte(pos + 48 + (i - 3) * 16);
            s->data_ok = (crc16_ccitt(buf, len, 0xffff) == 0);
            s->data_cell = pos + 48 + 16;
            memcpy(s->data, &buf[4], 128 << s->n);
            s = NULL;
        } else {
            continue;
        }
        pos += 48 + (len - 3) * 16 - 1;
    }
    return nr;
}

/* Checks that track @cyl, @head streams each of its sectors once, with the
 * image's contents. */
static void check_track(const struct geom *g, unsigned int cyl,
                        unsigned int head)
{
    unsigned int len, nr, i, r, seen = 0;
    unsigned int id_head = (g->id_head < 0) ? head : g->id_head;

    len = image_stream(cyl*2 + head, cells, sizeof(cells));
    nr = mfm_decode(len * 8);
    if (nr != g->secs)
        ft_fail(__FILE__, __LINE__, "track %u.%u: %u sectors, not %u",
                cyl, head, nr, g->secs);
    for (i = 0; i < nr; i++) {
        struct sector *s = &secs[i];
        r = s->r;
        if ((s->c != cyl) || (s->h != id_head) || (r < 1) || (r > g->secs)
            || ((128u << s->n) != g->size) || (seen & (1u << r))) {
            ft_fail(__FILE__, __LINE__, "track %u.%u: unexpected sector"
                    " C%u H%u R%u N%u", cyl, head, s->c, s->h, r, s->n);
            continue;
        }
        seen |= 1u << r;
        CHECK(s->data_ok);
        if (!CHECK_MEM(s->data, sector(g, cyl, head, r), g->size))
            ft_fail(__FILE__, __LINE__, "track %u.%u, sector %u",
                    cyl, head, r);
    }
}

/* The 16 MFM cells of byte @b at cell *@pos: a clock 1 between two 0
 * data bits. */
static void put_mfm(uint8_t *w, uint32_t *pos, unsigned int b,
                    unsigned int *prev)
{
    unsigned int i, bit, cells = 0;
    for (i = 0; i < 8; i++) {
        bit = (b >> (7 - i)) & 1;
        cells = (cells << 2) | (!(*prev | bit) << 1) | bit;
        *prev = bit;
    }
    w[*pos/8] = cells >> 8;
    w[*pos/8 + 1] = cells;
    *pos += 16;
}

/* A data field as a controller writes it after a sector's ID: 12 bytes 00,
 * three A1 with a missing clock, the mark FB, the data, its CRC, wrong if
 * @bad_crc, then gap bytes. Returns its length in cells. */
static uint32_t make_dam_write(uint8_t *w, const uint8_t *data,
                               unsigned int len, bool_t bad_crc)
{
    static const uint8_t mark[4] = { 0xa1, 0xa1, 0xa1, 0xfb };
    uint32_t pos = 0;
    unsigned int i, prev = 0;
    uint16_t crc;

    for (i = 0; i < 12; i++)
        put_mfm(w, &pos, 0x00, &prev);
    for (i = 0; i < 3; i++) {
        w[pos/8] = 0x44;
        w[pos/8 + 1] = 0x89;
        pos += 16;
    }
    prev = 1;
    put_mfm(w, &pos, 0xfb, &prev);
    for (i = 0; i < len; i++)
        put_mfm(w, &pos, data[i], &prev);
    crc = crc16_ccitt(data, len, crc16_ccitt(mark, 4, 0xffff));
    crc ^= bad_crc;
    put_mfm(w, &pos, crc >> 8, &prev);
    put_mfm(w, &pos, crc & 0xff, &prev);
    for (i = 0; i < 150; i++)
        put_mfm(w, &pos, 0x4e, &prev);
    return pos;
}

/* New contents of sector @r of the track, put into img[] as expected, and
 * the data field that writes them into @w: returns its length in cells, and
 * in @start the cell where a controller starts it. */
static uint32_t new_sector(const struct geom *g, unsigned int cyl,
                           unsigned int head, unsigned int r, bool_t bad_crc,
                           uint8_t *w, uint32_t *start)
{
    uint8_t *p = sector(g, cyl, head, r);
    unsigned int i, len, nr;

    len = image_stream(cyl*2 + head, cells, sizeof(cells));
    nr = mfm_decode(len * 8);
    for (i = 0; (i < nr) && (secs[i].r != r); i++)
        continue;
    REQUIRE(i < nr);
    for (len = 0; len < g->size; len++)
        p[len] ^= 0xff;
    /* The data field's 12 bytes 00 and its mark come first. */
    *start = secs[i].data_cell - 16*16;
    return make_dam_write(w, p, g->size, bad_crc);
}

static void write_sector(const struct geom *g, unsigned int cyl,
                         unsigned int head, unsigned int r, bool_t bad_crc)
{
    static uint8_t w[2048];
    uint32_t start, nr_cells;

    nr_cells = new_sector(g, cyl, head, r, bad_crc, w, &start);
    image_write(cyl*2 + head, start, w, nr_cells);
}

#define DD_CELL 144 /* WDATA timer ticks of a 2 us bit cell */

/* The same, written by a host by flux on WDATA, its clock 0.7% slow and
 * each flux reversal up to a tenth of a cell early or late. */
static void host_write_sector(const struct geom *g, unsigned int cyl,
                              unsigned int head, unsigned int r)
{
    static uint8_t w[2048];
    uint32_t start, nr_cells;

    nr_cells = new_sector(g, cyl, head, r, FALSE, w, &start);
    host_write(start * DD_CELL, w, nr_cells, DD_CELL + 1, DD_CELL / 10);
}

static uint8_t file[80*2*9*512];

TEST(img_write_sectors)
{
    drive_reset();
    make_img(&pc720);
    drive_add("PC720.IMG", img, img_size(&pc720));
    REQUIRE(image_mount("PC720.IMG") == FR_OK);

    write_sector(&pc720, 10, 1, 5, FALSE);
    write_sector(&pc720, 10, 1, 1, FALSE);
    write_sector(&pc720, 79, 0, 9, FALSE);
    image_sync();
    CHECK(!log_has("Bad CRC"));
    CHECK_EQ(drive_get("PC720.IMG", file, sizeof(file)), sizeof(file));
    CHECK_MEM(file, img, sizeof(file));
    check_track(&pc720, 10, 1);
    check_track(&pc720, 79, 0);
}

/* A data field whose CRC is wrong is written all the same, and logged. */
TEST(img_write_bad_crc)
{
    drive_reset();
    make_img(&pc720);
    drive_add("PC720.IMG", img, img_size(&pc720));
    REQUIRE(image_mount("PC720.IMG") == FR_OK);

    write_sector(&pc720, 33, 0, 4, TRUE);
    image_sync();
    CHECK(log_has("Bad CRC"));
    CHECK_EQ(drive_get("PC720.IMG", file, sizeof(file)), sizeof(file));
    CHECK_MEM(file, img, sizeof(file));
}

/* Sectors that a Shugart host writes through the floppy code: WGATE, the
 * timer's captures of WDATA, and the main loop writing them to the image. */
TEST(img_write_flux)
{
    drive_reset();
    make_img(&pc720);
    drive_add("PC720.IMG", img, img_size(&pc720));
    floppy_boot();
    floppy_insert_image("PC720.IMG");

    floppy_head(10, 1);
    host_write_sector(&pc720, 10, 1, 5);
    host_write_sector(&pc720, 10, 1, 1);
    floppy_head(79, 0);
    host_write_sector(&pc720, 79, 0, 9);
    floppy_cancel();
    CHECK(!log_has("Bad CRC"));
    CHECK_EQ(drive_get("PC720.IMG", file, sizeof(file)), sizeof(file));
    CHECK_MEM(file, img, sizeof(file));
}

/*
 * HFE: bit cells written as they come, LSB first in the file.
 */

static uint8_t rbit8(uint8_t x)
{
    unsigned int i, y = 0;
    for (i = 0; i < 8; i++)
        y |= ((x >> i) & 1) << (7 - i);
    return y;
}

#define HFE_TRK 12500 /* bytes of cells of side 0 */

/* A single-sided MFM HFE of one track of @trk, as the file holds it. */
static uint32_t make_hfe(uint8_t *hfe, const uint8_t *trk)
{
    unsigned int i;

    memset(hfe, 0xff, 1024);
    memcpy(hfe, "HXCPICFE", 8);
    hfe[8] = 0; /* revision */
    hfe[9] = 1; /* tracks */
    hfe[10] = 1; /* sides */
    hfe[11] = 0; /* ISO MFM */
    hfe[12] = 250; /* kbit/s */
    hfe[13] = 0;
    hfe[14] = hfe[15] = 0; /* rpm */
    hfe[16] = 0xff; /* interface mode */
    hfe[18] = 1; /* track list at block 1 */
    hfe[19] = 0;
    hfe[512] = 2; /* track 0 at block 2 */
    hfe[513] = 0;
    hfe[514] = (HFE_TRK * 2) & 0xff; /* both sides */
    hfe[515] = (HFE_TRK * 2) >> 8;
    memset(&hfe[1024], 0, ((HFE_TRK + 255) / 256) * 512);
    for (i = 0; i < HFE_TRK; i++)
        hfe[1024 + (i / 256) * 512 + (i % 256)] = trk[i];
    return 1024 + ((HFE_TRK + 255) / 256) * 512;
}

TEST(hfe_write_cells)
{
    static const struct { uint32_t start, len; } writes[2] = {
        { 1000, 300 }, { 250, 600 } /* the second across two blocks */
    };
    static uint8_t trk[HFE_TRK], hfe[32*1024], back[32*1024], w[1024];
    unsigned int i, j;
    uint32_t len, x = 1;

    drive_reset();
    for (i = 0; i < HFE_TRK; i++)
        trk[i] = (x = x * 1103515245 + 12345) >> 16;
    len = make_hfe(hfe, trk);
    drive_add("MFM.HFE", hfe, len);
    REQUIRE(image_mount("MFM.HFE") == FR_OK);

    for (i = 0; i < 2; i++) {
        for (j = 0; j < writes[i].len; j++) {
            w[j] = (x = x * 1103515245 + 12345) >> 16;
            trk[writes[i].start + j] = rbit8(w[j]);
        }
        image_write(0, writes[i].start * 8, w, writes[i].len * 8);
    }
    image_sync();
    make_hfe(hfe, trk);
    CHECK_EQ(drive_get("MFM.HFE", back, sizeof(back)), len);
    CHECK_MEM(back, hfe, len);
    CHECK_EQ(image_stream(0, back, sizeof(back)), HFE_TRK);
    CHECK_MEM(back, trk, HFE_TRK);
}

/* An HFE whose header names Apple II GCR, which no Shugart host reads, is
 * refused; one whose header says none is served. */
TEST(hfe_encodings)
{
    static const uint8_t encodings[5] = { 0x00, 0x02, 0x06, 0x07, 0xff };
    static uint8_t trk[HFE_TRK], hfe[32*1024];
    unsigned int i;
    uint32_t len;

    memset(trk, 0x49, HFE_TRK); /* no four 0 cells in a row */
    for (i = 0; i < ARRAY_SIZE(encodings); i++) {
        bool_t ok = (encodings[i] != 0x06) && (encodings[i] != 0x07);
        FRESULT fr;
        drive_reset();
        ft_fw_log_clear();
        len = make_hfe(hfe, trk);
        hfe[11] = encodings[i];
        drive_add("A.HFE", hfe, len);
        fr = image_mount("A.HFE");
        if ((fr == FR_OK) != ok)
            ft_fail(__FILE__, __LINE__, "encoding %02x: mount gave %d",
                    encodings[i], fr);
        if (!ok)
            CHECK(log_has("HFE unfit: GCR"));
    }
}

TEST(img_pc_720k)
{
    drive_reset();
    make_img(&pc720);
    drive_add("PC720.IMG", img, img_size(&pc720));
    REQUIRE(image_mount("PC720.IMG") == FR_OK);
    CHECK_EQ(im->nr_cyls, 80);
    CHECK_EQ(im->nr_sides, 2);
    check_track(&pc720, 0, 0);
    check_track(&pc720, 41, 1);
    check_track(&pc720, 79, 1);
}

TEST(img_pc_1440k)
{
    drive_reset();
    make_img(&pc1440);
    drive_add("PC1440.IMG", img, img_size(&pc1440));
    REQUIRE(image_mount("PC1440.IMG") == FR_OK);
    CHECK_EQ(im->nr_cyls, 80);
    CHECK_EQ(im->nr_sides, 2);
    check_track(&pc1440, 0, 1);
    check_track(&pc1440, 79, 0);
}

/* A fragmented image, with the floppy code's fast-seek table and
 * without it. */
TEST(img_fragmented)
{
    unsigned int fast;

    for (fast = 0; fast < 2; fast++) {
        drive_reset();
        make_img(&pc720);
        CHECK_EQ(drive_add_fragmented("PC720.IMG", img, img_size(&pc720)),
                 360);
        REQUIRE((fast ? image_mount_fast_seek : image_mount)("PC720.IMG")
                == FR_OK);
        check_track(&pc720, 0, 0);
        check_track(&pc720, 41, 1);
        check_track(&pc720, 79, 1);
    }
}

/* A revolution from mid-track is the track from the index, turned; the
 * handler may start up to a byte's 16 cells early. A start within a
 * sector's data regenerates it whole, its CRC included; a start within the
 * CRC itself leaves it wrong in this first revolution, when no host can
 * read the sector, having missed its marks, so those 32 cells are not
 * compared. */
TEST(img_mid_track)
{
    static const uint32_t starts[] = {
        16, 1000, 31337, 31337 + 512*16, 99984
    };
    static const unsigned int tracks[2] = { 3*2 + 1, 60*2 };
    static uint8_t full[2][12500], got[12500], turned[12500];
    unsigned int i, t, k, nr, in_data = 0;
    uint32_t s, nr_bits, crc_cell;

    drive_reset();
    make_img(&pc720);
    drive_add("PC720.IMG", img, img_size(&pc720));
    REQUIRE(image_mount("PC720.IMG") == FR_OK);
    for (t = 0; t < 2; t++)
        REQUIRE(image_stream(tracks[t], full[t], sizeof(full[t]))
                == sizeof(full[t]));
    nr_bits = sizeof(full[0]) * 8;
    for (i = 0; i < ARRAY_SIZE(starts); i++) {
        for (t = 0; t < 2; t++) {
            s = image_stream_from(tracks[t], starts[i], got, nr_bits);
            CHECK((s <= starts[i]) && (starts[i] - s < 16));
            bits_rotate(turned, full[t], nr_bits, s);
            memcpy(cells, full[t], sizeof(full[t]));
            nr = mfm_decode(nr_bits);
            for (k = 0; k < nr; k++) {
                crc_cell = secs[k].data_cell + 512*16;
                in_data += (s >= secs[k].data_cell) && (s < crc_cell);
                if ((s < crc_cell) || (s >= crc_cell + 32))
                    continue;
                crc_cell = (crc_cell + nr_bits - s) % nr_bits;
                memcpy(&got[crc_cell/8], &turned[crc_cell/8], 4);
            }
            if (!CHECK_MEM(got, turned, sizeof(got)))
                ft_fail(__FILE__, __LINE__, "track %u from bit %u",
                        tracks[t], s);
        }
    }
    /* Some starts fall within a sector's data. */
    CHECK(in_data != 0);
}

/*
 * TR-DOS: 16 sectors of 256 bytes a track, the head 0 in the IDs of both
 * sides, and the geometry from the disk's info sector, track 0 sector 9,
 * or from the file's size when that holds more.
 */

static const struct geom trd_ds80 = { 80, 2, 16, 256, 0 };
static const struct geom trd_ss80 = { 80, 1, 16, 256, 0 };

/* The info sector's geometry: the first free sector and track, the disk
 * type, the number of free sectors, and the TR-DOS id. */
static void make_trd_info(unsigned int type, unsigned int tot_secs)
{
    uint8_t *p = &img[0x8e0];
    unsigned int nr_free = tot_secs - 16;

    p[1] = 0; /* first free sector */
    p[2] = 1; /* first free track */
    p[3] = type;
    p[4] = 0; /* files */
    p[5] = nr_free & 0xff;
    p[6] = nr_free >> 8;
    p[7] = 0x10;
}

TEST(trd_640k)
{
    drive_reset();
    make_img(&trd_ds80);
    make_trd_info(0x16, 80*2*16);
    drive_add("DISK.TRD", img, img_size(&trd_ds80));
    REQUIRE(image_mount("DISK.TRD") == FR_OK);
    CHECK_EQ(im->nr_cyls, 80);
    CHECK_EQ(im->nr_sides, 2);
    check_track(&trd_ds80, 0, 0);
    check_track(&trd_ds80, 0, 1);
    check_track(&trd_ds80, 40, 1);
    check_track(&trd_ds80, 79, 1);
}

TEST(trd_single_sided)
{
    drive_reset();
    make_img(&trd_ss80);
    make_trd_info(0x18, 80*16);
    drive_add("DISK.TRD", img, img_size(&trd_ss80));
    REQUIRE(image_mount("DISK.TRD") == FR_OK);
    CHECK_EQ(im->nr_cyls, 80);
    CHECK_EQ(im->nr_sides, 1);
    check_track(&trd_ss80, 0, 0);
    check_track(&trd_ss80, 79, 0);
}

/* A file shorter than its disk, as TR-DOS tools save it, takes the info
 * sector's geometry; the tracks it holds read as usual. */
TEST(trd_short_file)
{
    drive_reset();
    make_img(&trd_ds80);
    make_trd_info(0x16, 80*2*16);
    drive_add("SHORT.TRD", img, 20*16*256);
    REQUIRE(image_mount("SHORT.TRD") == FR_OK);
    CHECK_EQ(im->nr_cyls, 80);
    CHECK_EQ(im->nr_sides, 2);
    check_track(&trd_ds80, 0, 0);
    check_track(&trd_ds80, 9, 1);
}

/* An odd number of tracks leaves the last cylinder's second side empty. */
TEST(trd_odd_tracks)
{
    unsigned int len, nr;

    drive_reset();
    make_img(&trd_ds80);
    make_trd_info(0x16, 81*16);
    drive_add("ODD.TRD", img, 81*16*256);
    REQUIRE(image_mount("ODD.TRD") == FR_OK);
    CHECK_EQ(im->nr_cyls, 41);
    check_track(&trd_ds80, 40, 0);
    len = image_stream(40*2 + 1, cells, sizeof(cells));
    nr = mfm_decode(len * 8);
    CHECK_EQ(nr, 0);
}

TEST(trd_write_sectors)
{
    drive_reset();
    make_img(&trd_ds80);
    make_trd_info(0x16, 80*2*16);
    drive_add("DISK.TRD", img, img_size(&trd_ds80));
    REQUIRE(image_mount("DISK.TRD") == FR_OK);

    write_sector(&trd_ds80, 41, 1, 7, FALSE);
    write_sector(&trd_ds80, 0, 0, 16, FALSE);
    image_sync();
    CHECK(!log_has("Bad CRC"));
    CHECK_EQ(drive_get("DISK.TRD", file, sizeof(file)),
             img_size(&trd_ds80));
    CHECK_MEM(file, img, img_size(&trd_ds80));
    check_track(&trd_ds80, 41, 1);
    check_track(&trd_ds80, 0, 0);
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

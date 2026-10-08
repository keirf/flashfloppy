/*
 * test_apple2.c
 *
 * The Apple2 images: what .dsk, .do, .po, .nib and .nic stream, what a
 * write puts into them, and which images Apple2 mode lists and mounts.
 *
 * The disks are made up here, never taken from real disks. The expected
 * streams come from an encoder in this file, written from the format, and
 * their CRC32s from the same made-up disks converted by apple2-disk-convert
 * of github.com/mike-shevchenko/retro-tools, an independent implementation.
 *
 * Written & released by Mike Shevchenko <mike.shevchenko.kz@gmail.com>
 *
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

#include "harness.h"

#define TRK_LEN 6656 /* bytes a track streams */
#define REC_LEN 416 /* bytes of a sector's record in that stream */
#define NIC_REC 512 /* bytes of a sector's record in a .nic */
#define DSK_TRK 4096
#define TRAILER 128
#define TRACK(t) ((t) << 2) /* the floppy code's track: half-track steps */

/* CRC32s of the streams of the converter's made-up images. */
#define CRC_MADE_UP 0x93d29fbb /* made-up.nic, 35 tracks */
#define CRC_MADE_UP_40 0x55c6ae61 /* made-up-40.nic */
#define CRC_PRODOS_DSK 0xd68b08d4 /* prodos-header.nic */
#define CRC_PRODOS_DO 0x7fb71b88 /* prodos-header-as-do.nic */
#define CRC_SPARE_PAIRS 0x1a706780 /* spare-pairs.nic */

/* The file's sector that each physical sector holds. */
static const uint8_t dos_order[16] = {
    0, 7, 14, 6, 13, 5, 12, 4, 11, 3, 10, 2, 9, 1, 8, 15
};
static const uint8_t prodos_order[16] = {
    0, 8, 1, 9, 2, 10, 3, 11, 4, 12, 5, 13, 6, 14, 7, 15
};

static uint8_t disk[40*DSK_TRK + TRAILER], file[40*16*NIC_REC];
static uint8_t want[40*TRK_LEN], got[40*16*NIC_REC];
static uint8_t nib62[64];

/* The made-up disk in DOS 3.3 order: the sector of track t and DOS sector s
 * holds t, s, then steps of 37 from t*16+s. */
static void make_disk(uint8_t *d, unsigned int nr_trks)
{
    unsigned int t, s, i;
    for (t = 0; t < nr_trks; t++) {
        for (s = 0; s < 16; s++) {
            uint8_t *p = &d[t*DSK_TRK + s*256];
            p[0] = t;
            p[1] = s;
            for (i = 0; i < 254; i++)
                p[i+2] = t*16 + s + 37*i;
        }
    }
}

/* The same sectors in ProDOS order. */
static void to_prodos(uint8_t *po, const uint8_t *d, unsigned int nr_trks)
{
    unsigned int t, p;
    for (t = 0; t < nr_trks; t++)
        for (p = 0; p < 16; p++)
            memcpy(&po[t*DSK_TRK + prodos_order[p]*256],
                   &d[t*DSK_TRK + dos_order[p]*256], 256);
}

/* The 64 data nibbles, by their rule: the top bit set, no three 0 bits in a
 * row and at most one pair of them, two adjacent 1 bits below the top bit,
 * and the marks' AA and D5 left out. */
static void make_nib62(void)
{
    unsigned int b, i, n = 0, pairs00, run0, ones11;

    for (b = 0x80; b < 0x100; b++) {
        pairs00 = run0 = ones11 = 0;
        for (i = 0; i < 8; i++) {
            run0 = (b >> i) & 1 ? 0 : run0 + 1;
            if (run0 == 3)
                break;
            pairs00 += (run0 == 2);
            if ((i < 6) && (((b >> i) & 3) == 3))
                ones11 = 1;
        }
        if ((i == 8) && (pairs00 <= 1) && ones11
            && (b != 0xaa) && (b != 0xd5) && (n < 64))
            nib62[n++] = b;
    }
    REQUIRE(n == 64);
}

static void put_4and4(uint8_t *p, unsigned int v)
{
    p[0] = (v >> 1) | 0xaa;
    p[1] = v | 0xaa;
}

/* The record of physical sector @p of track @t, holding @d, as it streams
 * and as a .nic holds it. @spare fills the unused top bits of the last two
 * auxiliary values with the pairs of bytes 0 and 1, as DOS 3.3 does. */
static void make_record(uint8_t *r, unsigned int t, unsigned int p,
                        const uint8_t *d, int spare)
{
    static const uint8_t sync[12] = {
        0x03, 0xfc, 0xff, 0x3f, 0xcf, 0xf3, 0xfc, 0xff, 0x3f, 0xcf, 0xf3, 0xfc
    };
    uint8_t v[342];
    unsigned int i, j, prev;

    memset(r, 0xff, REC_LEN);
    memcpy(r + 22, sync, sizeof(sync));
    memcpy(r + 34, "\xd5\xaa\x96", 3);
    put_4and4(r + 37, 254);
    put_4and4(r + 39, t);
    put_4and4(r + 41, p);
    put_4and4(r + 43, 254 ^ t ^ p);
    memcpy(r + 45, "\xde\xaa\xeb", 3);

    /* The low pairs, swapped, three bytes 86 apart to a value; then the
     * high six bits of each byte. */
    memset(v, 0, 86);
    for (j = 0; j < (spare ? 258 : 256); j++) {
        unsigned int b = d[j & 255];
        v[j % 86] |= (((b & 1) << 1) | ((b >> 1) & 1)) << (2 * (j / 86));
    }
    for (i = 0; i < 256; i++)
        v[86 + i] = d[i] >> 2;
    memcpy(r + 53, "\xd5\xaa\xad", 3);
    for (i = prev = 0; i < 342; i++) {
        r[56 + i] = nib62[v[i] ^ prev];
        prev = v[i];
    }
    r[56 + 342] = nib62[prev];
    memcpy(r + 399, "\xde\xaa\xeb", 3);
}

/* What track @t of sector image @d in sector order @order streams. */
static void make_track(uint8_t *s, const uint8_t *d, unsigned int t,
                       const uint8_t *order, int spare)
{
    unsigned int p;
    for (p = 0; p < 16; p++)
        make_record(&s[p*REC_LEN], t, p, &d[t*DSK_TRK + order[p]*256],
                    spare);
}

/* Streams every track of the mounted image into got[]. */
static void stream_all(unsigned int nr_trks)
{
    unsigned int t;
    for (t = 0; t < nr_trks; t++)
        CHECK_EQ(image_stream(TRACK(t), &got[t*TRK_LEN], TRK_LEN), TRK_LEN);
}

/* A .nic of the streams in want[]. */
static void make_nic(uint8_t *nic, unsigned int nr_trks)
{
    unsigned int r;
    memset(nic, 0, nr_trks * 16 * NIC_REC);
    for (r = 0; r < nr_trks * 16; r++)
        memcpy(&nic[r*NIC_REC], &want[r*REC_LEN], REC_LEN);
}

TEST(apple2_nibble_rule)
{
    make_nib62();
    CHECK_EQ(nib62[0], 0x96);
    CHECK_EQ(nib62[1], 0x97);
    CHECK_EQ(nib62[27], 0xcb);
    CHECK_EQ(nib62[63], 0xff);
}

/* Vectors worked out by hand. */
TEST(apple2_fields_by_hand)
{
    static const uint8_t addr_t0s0[14] = {
        0xd5, 0xaa, 0x96, 0xff, 0xfe, 0xaa, 0xaa, 0xaa, 0xaa,
        0xff, 0xfe, 0xde, 0xaa, 0xeb
    };
    uint8_t zeros[343], ones[343];

    make_nib62();
    drive_reset();
    apple2_mode = TRUE;
    memset(disk, 0, 35*DSK_TRK);
    memset(&disk[dos_order[1]*256], 0x01, 256);
    drive_add("ZEROS.DSK", disk, 35*DSK_TRK);
    REQUIRE(image_mount("ZEROS.DSK") == FR_OK);
    image_stream(TRACK(0), got, TRK_LEN);

    CHECK_MEM(&got[34], addr_t0s0, 14);
    /* 256 zeros are 343 nibbles 96; 256 bytes 01 are E6, 83 times 96, D6
     * 96 AC, then 256 times 96. */
    memset(zeros, 0x96, 343);
    CHECK_MEM(&got[56], zeros, 343);
    memset(ones, 0x96, 343);
    ones[0] = 0xe6;
    ones[84] = 0xd6;
    ones[86] = 0xac;
    CHECK_MEM(&got[REC_LEN + 56], ones, 343);
}

TEST(apple2_dsk_stream)
{
    unsigned int t;

    make_nib62();
    drive_reset();
    apple2_mode = TRUE;
    make_disk(disk, 35);
    drive_add("MADEUP.DSK", disk, 35*DSK_TRK);
    REQUIRE(image_mount("MADEUP.DSK") == FR_OK);
    CHECK_EQ(im->nr_cyls, 35);
    CHECK_EQ(im->nr_sides, 1);
    stream_all(35);
    for (t = 0; t < 35; t++)
        make_track(&want[t*TRK_LEN], disk, t, dos_order, 0);
    CHECK_MEM(got, want, 35*TRK_LEN);
    CHECK_EQ(crc32(got, 35*TRK_LEN), CRC_MADE_UP);
    CHECK(!log_has("ProDOS order"));
}

/* .do and .po name their order: the same disk streams the same. */
TEST(apple2_do_po_stream)
{
    drive_reset();
    apple2_mode = TRUE;
    make_disk(disk, 35);
    drive_add("MADEUP.DO", disk, 35*DSK_TRK);
    to_prodos(file, disk, 35);
    drive_add("MADEUP.PO", file, 35*DSK_TRK);

    REQUIRE(image_mount("MADEUP.DO") == FR_OK);
    stream_all(35);
    CHECK_EQ(crc32(got, 35*TRK_LEN), CRC_MADE_UP);
    CHECK(!log_has("ProDOS order"));

    REQUIRE(image_mount("MADEUP.PO") == FR_OK);
    stream_all(35);
    CHECK_EQ(crc32(got, 35*TRK_LEN), CRC_MADE_UP);
    CHECK(log_has("ProDOS order"));
}

TEST(apple2_dsk_sizes_mount)
{
    drive_reset();
    apple2_mode = TRUE;
    make_disk(disk, 40);
    drive_add("TRACKS40.DSK", disk, 40*DSK_TRK);
    make_disk(disk, 35);
    memset(&disk[35*DSK_TRK], 'T', TRAILER);
    drive_add("LABEL.DSK", disk, 35*DSK_TRK + TRAILER);

    REQUIRE(image_mount("TRACKS40.DSK") == FR_OK);
    CHECK_EQ(im->nr_cyls, 40);
    stream_all(40);
    CHECK_EQ(crc32(got, 40*TRK_LEN), CRC_MADE_UP_40);

    REQUIRE(image_mount("LABEL.DSK") == FR_OK);
    CHECK_EQ(im->nr_cyls, 35);
    stream_all(35);
    CHECK_EQ(crc32(got, 35*TRK_LEN), CRC_MADE_UP);
}

/* A .dsk is read in ProDOS order when block 2 starts as the key block of a
 * ProDOS volume directory where ProDOS order puts it; a .do never is. */
TEST(apple2_prodos_detection)
{
    static const uint8_t key[9] = {
        0x00, 0x00, 0x03, 0x00, 0xf4, 'T', 'E', 'S', 'T'
    };

    drive_reset();
    apple2_mode = TRUE;
    make_disk(disk, 35);
    memcpy(&disk[1024], key, sizeof(key));
    disk[1059] = 0x27; /* entry length */
    disk[1060] = 0x0d; /* entries per block */
    disk[1063] = 0x06; /* bitmap at block 6 */
    disk[1064] = 0x00;
    disk[1065] = 0x18; /* 280 blocks */
    disk[1066] = 0x01;
    drive_add("PRODOS.DSK", disk, 35*DSK_TRK);
    drive_add("PRODOS.DO", disk, 35*DSK_TRK);

    REQUIRE(image_mount("PRODOS.DSK") == FR_OK);
    stream_all(35);
    CHECK_EQ(crc32(got, 35*TRK_LEN), CRC_PRODOS_DSK);
    CHECK(log_has("ProDOS order"));

    ft_fw_log_clear();
    REQUIRE(image_mount("PRODOS.DO") == FR_OK);
    stream_all(35);
    CHECK_EQ(crc32(got, 35*TRK_LEN), CRC_PRODOS_DO);
    CHECK(!log_has("ProDOS order"));
}

/* A .nic streams its records' first 416 bytes, a .nib its bytes. */
TEST(apple2_nic_nib_stream)
{
    unsigned int t;

    make_nib62();
    drive_reset();
    apple2_mode = TRUE;
    make_disk(disk, 35);
    for (t = 0; t < 35; t++)
        make_track(&want[t*TRK_LEN], disk, t, dos_order, 0);
    make_nic(file, 35);
    drive_add("MADEUP.NIC", file, 35*16*NIC_REC);
    drive_add("MADEUP.NIB", want, 35*TRK_LEN);

    REQUIRE(image_mount("MADEUP.NIC") == FR_OK);
    CHECK_EQ(im->nr_cyls, 35);
    stream_all(35);
    CHECK_MEM(got, want, 35*TRK_LEN);

    REQUIRE(image_mount("MADEUP.NIB") == FR_OK);
    CHECK_EQ(im->nr_cyls, 35);
    stream_all(35);
    CHECK_MEM(got, want, 35*TRK_LEN);
}

/* Whole tracks written to a blank sector image, as a format writes them. */
TEST(apple2_write_tracks)
{
    unsigned int t;

    make_nib62();
    drive_reset();
    apple2_mode = TRUE;
    make_disk(disk, 35);
    memset(file, 0, 35*DSK_TRK);
    drive_add("BLANK.DSK", file, 35*DSK_TRK);

    REQUIRE(image_mount("BLANK.DSK") == FR_OK);
    for (t = 0; t < 35; t++) {
        make_track(want, disk, t, dos_order, 0);
        image_write(TRACK(t), 0, want, TRK_LEN*8);
    }
    image_sync();
    CHECK_EQ(drive_get("BLANK.DSK", got, sizeof(got)), 35*DSK_TRK);
    CHECK_MEM(got, disk, 35*DSK_TRK);
}

/* Data fields written alone, each from the gap after its address field, as
 * DOS writes a sector, into .dsk and .po. */
TEST(apple2_write_fields)
{
    static const char *const names[2] = { "BLANK.DSK", "BLANK.PO" };
    unsigned int i, t, p;

    make_nib62();
    drive_reset();
    apple2_mode = TRUE;
    make_disk(disk, 35);
    memset(file, 0, 35*DSK_TRK);
    drive_add(names[0], file, 35*DSK_TRK);
    drive_add(names[1], file, 35*DSK_TRK);

    for (i = 0; i < 2; i++) {
        REQUIRE(image_mount(names[i]) == FR_OK);
        for (t = 0; t < 35; t++) {
            make_track(want, disk, t, dos_order, 0);
            for (p = 0; p < 16; p++)
                image_write(TRACK(t), (p*REC_LEN + 48) * 8,
                            &want[p*REC_LEN + 48], (REC_LEN - 48) * 8);
        }
        image_sync();
        CHECK_EQ(drive_get(names[i], got, sizeof(got)), 35*DSK_TRK);
        if (i == 0) {
            CHECK_MEM(got, disk, 35*DSK_TRK);
        } else {
            to_prodos(file, disk, 35);
            CHECK_MEM(got, file, 35*DSK_TRK);
        }
    }
}

/* A write leaves the trailer of an Agat image as it is. */
TEST(apple2_write_keeps_trailer)
{
    unsigned int t;

    make_nib62();
    drive_reset();
    apple2_mode = TRUE;
    memset(file, 0, 35*DSK_TRK);
    memcpy(&file[35*DSK_TRK], "AGAT-DISK", 9);
    memset(&file[35*DSK_TRK + 9], 'L', TRAILER - 9);
    drive_add("LABEL.DSK", file, 35*DSK_TRK + TRAILER);
    make_disk(disk, 35);
    memcpy(&disk[35*DSK_TRK], &file[35*DSK_TRK], TRAILER);

    REQUIRE(image_mount("LABEL.DSK") == FR_OK);
    for (t = 0; t < 35; t++) {
        make_track(want, disk, t, dos_order, 0);
        image_write(TRACK(t), 0, want, TRK_LEN*8);
    }
    image_sync();
    CHECK_EQ(drive_get("LABEL.DSK", got, sizeof(got)),
             35*DSK_TRK + TRAILER);
    CHECK_MEM(got, disk, 35*DSK_TRK + TRAILER);
}

/* A data field with a wrong checksum, or with a nibble that is no data
 * nibble, is dropped and logged; the sector stays as it was. */
TEST(apple2_write_bad_fields)
{
    unsigned int i;

    make_nib62();
    drive_reset();
    apple2_mode = TRUE;
    make_disk(disk, 35);
    drive_add("MADEUP.DSK", disk, 35*DSK_TRK);
    REQUIRE(image_mount("MADEUP.DSK") == FR_OK);

    for (i = 0; i < 2; i++) {
        uint8_t *field;
        make_track(want, disk, 5, dos_order, 0);
        field = &want[3*REC_LEN + 56];
        /* Another data nibble, then AA, which is none. */
        field[100] = i ? 0xaa : (field[100] == 0x96 ? 0x97 : 0x96);
        image_write(TRACK(5), (3*REC_LEN + 48) * 8, &want[3*REC_LEN + 48],
                    (REC_LEN - 48) * 8);
    }
    image_sync();
    CHECK_EQ(drive_get("MADEUP.DSK", got, sizeof(got)), 35*DSK_TRK);
    CHECK_MEM(got, disk, 35*DSK_TRK);
    CHECK(log_has("Bad CRC"));
}

/* The unused top bits of the last two auxiliary values, which DOS 3.3 fills,
 * do not change the sector a data field decodes to. */
TEST(apple2_write_spare_pairs)
{
    unsigned int t;

    make_nib62();
    drive_reset();
    apple2_mode = TRUE;
    make_disk(disk, 35);
    for (t = 0; t < 35; t++)
        make_track(&want[t*TRK_LEN], disk, t, dos_order, 1);
    CHECK_EQ(crc32(want, 35*TRK_LEN), CRC_SPARE_PAIRS);
    drive_add("MADEUP.DSK", disk, 35*DSK_TRK);

    REQUIRE(image_mount("MADEUP.DSK") == FR_OK);
    for (t = 0; t < 35; t++)
        image_write(TRACK(t), 0, &want[t*TRK_LEN], TRK_LEN*8);
    image_sync();
    CHECK_EQ(drive_get("MADEUP.DSK", got, sizeof(got)), 35*DSK_TRK);
    CHECK_MEM(got, disk, 35*DSK_TRK);
    CHECK(!log_has("Bad CRC"));
}

/* A .nic takes each field written into its place in the sector's record. */
TEST(apple2_write_nic)
{
    unsigned int t;

    make_nib62();
    drive_reset();
    apple2_mode = TRUE;
    make_disk(disk, 35);
    for (t = 0; t < 35; t++)
        make_track(&want[t*TRK_LEN], disk, t, dos_order, 0);
    make_nic(file, 35);
    memset(&file[(2*16 + 5)*NIC_REC + 53], 0, 349);
    drive_add("MADEUP.NIC", file, 35*16*NIC_REC);

    REQUIRE(image_mount("MADEUP.NIC") == FR_OK);
    image_write(TRACK(2), 0, &want[2*TRK_LEN], TRK_LEN*8);
    image_sync();
    make_nic(file, 35);
    CHECK_EQ(drive_get("MADEUP.NIC", got, sizeof(got)), 35*16*NIC_REC);
    CHECK_MEM(got, file, 35*16*NIC_REC);
}

/* A .nib takes the nibbles of a write where it began, as the controller
 * reads them: eight bits from a 1 bit on, the 0 bits between them
 * dropped. */
TEST(apple2_write_nib)
{
    unsigned int i, n = 0, acc = 0, nbits = 0;
    static uint8_t nibs[TRK_LEN];

    make_nib62();
    drive_reset();
    apple2_mode = TRUE;
    make_disk(disk, 35);
    make_track(want, disk, 3, dos_order, 0);
    for (i = 0; i < TRK_LEN*8; i++) {
        unsigned int bit = (want[i/8] >> (7 - (i & 7))) & 1;
        if (!nbits && !bit)
            continue;
        acc = (acc << 1) | bit;
        if (++nbits == 8) {
            nibs[n++] = acc;
            acc = nbits = 0;
        }
    }
    memset(file, 0, 35*TRK_LEN);
    drive_add("BLANK.NIB", file, 35*TRK_LEN);

    REQUIRE(image_mount("BLANK.NIB") == FR_OK);
    image_write(TRACK(3), 0, want, TRK_LEN*8);
    image_sync();
    memcpy(&file[3*TRK_LEN], nibs, n);
    CHECK_EQ(drive_get("BLANK.NIB", got, sizeof(got)), 35*TRK_LEN);
    CHECK_MEM(got, file, 35*TRK_LEN);
}

#define A2_CELL 288 /* WDATA timer ticks of a 4 us bit cell */

/* Data fields that an Apple2 host writes through the floppy code, each from
 * the gap after its address field as DOS writes a sector: WGATE, the timer's
 * captures of WDATA, and the main loop writing them to the image. The host's
 * clock is 0.7% slow, each flux reversal up to a tenth of a cell early or
 * late. */
TEST(apple2_write_flux)
{
    static const uint8_t phys[2] = { 3, 12 };
    static uint8_t rec[REC_LEN];
    unsigned int i, j;
    uint8_t *d;

    make_nib62();
    drive_reset();
    make_disk(disk, 35);
    drive_add("DOS.DSK", disk, 35*DSK_TRK);
    floppy_boot(TRUE);
    floppy_insert_image("DOS.DSK");

    floppy_head(2*17, 0);
    for (i = 0; i < 2; i++) {
        d = &disk[17*DSK_TRK + dos_order[phys[i]]*256];
        for (j = 0; j < 256; j++)
            d[j] ^= 0xff;
        make_record(rec, 17, phys[i], d, 0);
        host_write((phys[i]*REC_LEN + 48) * 8 * A2_CELL, &rec[48],
                   (REC_LEN - 48) * 8, A2_CELL + 2, A2_CELL / 10);
    }
    floppy_cancel();
    CHECK(!log_has("Bad CRC"));
    CHECK_EQ(drive_get("DOS.DSK", got, sizeof(got)), 35*DSK_TRK);
    CHECK_MEM(got, disk, 35*DSK_TRK);
}

/* Which files Apple2 mode lists, by size alone. */
TEST(apple2_listed_sizes)
{
    static const struct {
        const char *name;
        uint32_t size;
        bool_t listed;
    } files[] = {
        { "A.DSK", 34*DSK_TRK, FALSE },
        { "A.DSK", 35*DSK_TRK, TRUE },
        { "A.DSK", 35*DSK_TRK + 1, FALSE },
        { "A.DSK", 35*DSK_TRK + 127, FALSE },
        { "A.DSK", 35*DSK_TRK + TRAILER, TRUE },
        { "A.DSK", 35*DSK_TRK + 129, FALSE },
        { "A.DSK", 35*DSK_TRK + 256, FALSE },
        { "A.DO", 40*DSK_TRK, TRUE },
        { "A.PO", 40*DSK_TRK + TRAILER, TRUE },
        { "A.PO", 41*DSK_TRK, FALSE },
        { "AGAT840.DSK", 860160, FALSE },
        { "A.HFE", 932864, TRUE },
        { "A.HFE", 1405952, FALSE },
        { "A.NIB", 35*TRK_LEN, TRUE },
        { "A.NIC", 35*16*NIC_REC, TRUE },
        { "A.TRD", 655360, FALSE },
    };
    unsigned int i, nr_hidden = 0;
    FILINFO fno;

    drive_reset();
    apple2_mode = TRUE;
    image_nr_hidden = 0;
    for (i = 0; i < ARRAY_SIZE(files); i++) {
        memset(&fno, 0, sizeof(fno));
        snprintf(fno.fname, sizeof(fno.fname), "%s", files[i].name);
        fno.fsize = files[i].size;
        fno.fattrib = AM_ARC;
        if (image_valid(&fno) != files[i].listed)
            ft_fail(__FILE__, __LINE__, "%s of %u bytes is%s listed",
                    files[i].name, (unsigned int)files[i].size,
                    files[i].listed ? " not" : "");
        nr_hidden += !files[i].listed && strcmp(files[i].name, "A.TRD");
    }
    CHECK_EQ(image_nr_hidden, nr_hidden);
    image_log_hidden();
    CHECK(log_has("image(s) hidden by size"));
}

/*
 * HFE: an image whose header names an encoding that does not suit the mode
 * is refused; one whose header says none is served, and the bits of its
 * first track only noted in the log.
 */

static uint32_t make_hfe(uint8_t *hfe, uint8_t encoding, bool_t gcr)
{
    const unsigned int len = TRK_LEN * 2; /* cells of side 0, as bytes */
    unsigned int i, j;
    uint8_t *trk = &hfe[1024];

    memset(hfe, 0xff, 1024);
    memcpy(hfe, "HXCPICFE", 8);
    hfe[8] = 0; /* revision */
    hfe[9] = 1; /* tracks */
    hfe[10] = 1; /* sides */
    hfe[11] = encoding;
    hfe[12] = 250; /* kbit/s */
    hfe[13] = 0;
    hfe[14] = hfe[15] = 0; /* rpm */
    hfe[16] = 0xff; /* interface mode */
    hfe[17] = 1;
    hfe[18] = 1; /* track list at block 1 */
    hfe[19] = 0;
    hfe[21] = 0x00; /* double step */
    hfe[512] = 2; /* track 0 at block 2 */
    hfe[513] = 0;
    hfe[514] = (len * 2) & 0xff; /* both sides */
    hfe[515] = (len * 2) >> 8;

    /* Each 512-byte block holds 256 bytes of side 0, then 256 of side 1.
     * GCR: a bit is two cells, the transition in the second, LSB first.
     * Else bytes 49, whose 0 cells never run to four. */
    for (i = 0; i < len; i++) {
        uint8_t c = 0x49;
        if (gcr) {
            uint8_t b = want[i/2] >> ((i & 1) ? 0 : 4);
            for (c = j = 0; j < 4; j++)
                c |= ((b >> (3 - j)) & 1) << (2*j + 1);
        }
        trk[(i / 256) * 512 + (i % 256)] = c;
        trk[(i / 256) * 512 + 256 + (i % 256)] = 0;
    }
    return 1024 + ((len + 255) / 256) * 512;
}

TEST(apple2_hfe_encodings)
{
    static const uint8_t encodings[5] = { 0x00, 0x02, 0x06, 0x07, 0xff };
    unsigned int mode, i;
    uint32_t len;

    make_nib62();
    make_disk(disk, 35);
    make_track(want, disk, 0, dos_order, 0);
    for (mode = 0; mode < 2; mode++) {
        for (i = 0; i < ARRAY_SIZE(encodings); i++) {
            bool_t gcr_enc = (encodings[i] == 6) || (encodings[i] == 7);
            bool_t ok = (encodings[i] == 0xff) || (gcr_enc == mode);
            FRESULT fr;
            drive_reset();
            ft_fw_log_clear();
            apple2_mode = mode;
            len = make_hfe(file, encodings[i], TRUE);
            drive_add("GCR.HFE", file, len);
            fr = image_mount("GCR.HFE");
            if ((fr == FR_OK) != ok)
                ft_fail(__FILE__, __LINE__, "encoding %02x in %s mode: "
                        "mount gave %d", encodings[i],
                        mode ? "Apple2" : "Shugart", fr);
            if (!ok)
                CHECK(log_has(mode ? "HFE unfit: non-GCR"
                              : "HFE unfit: GCR"));
        }
    }

    /* Unknown encoding: served, the bits of track 0 noted when they look
     * like the other kind. */
    drive_reset();
    len = make_hfe(file, 0xff, TRUE);
    drive_add("GCR.HFE", file, len);
    REQUIRE(image_mount("GCR.HFE") == FR_OK);
    CHECK(log_has("HFE: looks GCR"));

    drive_reset();
    apple2_mode = TRUE;
    len = make_hfe(file, 0xff, FALSE);
    drive_add("MFM.HFE", file, len);
    REQUIRE(image_mount("MFM.HFE") == FR_OK);
    CHECK(log_has("HFE: looks non-GCR"));
}

/*
 * Positions: the floppy code starts a track where the disk has turned to,
 * after every seek and every write. A revolution from there must be the
 * track from the index, turned.
 */

TEST(apple2_mid_track)
{
    static const char *const names[3] = {
        "MADEUP.DSK", "MADEUP.NIC", "MADEUP.NIB"
    };
    static const uint32_t starts[] = {
        1, 7, 8, 3*REC_LEN*8 + 5, 26000, TRK_LEN*8 - 1
    };
    static uint8_t turned[TRK_LEN];
    unsigned int i, j, t;
    uint32_t s;

    make_nib62();
    drive_reset();
    apple2_mode = TRUE;
    make_disk(disk, 35);
    for (t = 0; t < 35; t++)
        make_track(&want[t*TRK_LEN], disk, t, dos_order, 0);
    drive_add(names[0], disk, 35*DSK_TRK);
    make_nic(file, 35);
    drive_add(names[1], file, 35*16*NIC_REC);
    drive_add(names[2], want, 35*TRK_LEN);

    for (i = 0; i < 3; i++) {
        REQUIRE(image_mount(names[i]) == FR_OK);
        for (j = 0; j < ARRAY_SIZE(starts); j++) {
            /* A seek between each two reads. */
            t = (j * 13 + i) % 35;
            s = image_stream_from(TRACK(t), starts[j], got, TRK_LEN*8);
            CHECK_EQ(s, starts[j]);
            bits_rotate(turned, &want[t*TRK_LEN], TRK_LEN*8, s);
            if (!CHECK_MEM(got, turned, TRK_LEN))
                ft_fail(__FILE__, __LINE__, "%s, track %u from bit %u",
                        names[i], t, s);
        }
    }
}

/* Right after a write, the track streams what was written, from wherever
 * it is read: no stale bytes of the ring. */
TEST(apple2_read_after_write)
{
    static const char *const names[3] = {
        "MADEUP.DSK", "MADEUP.NIC", "MADEUP.NIB"
    };
    static uint8_t turned[TRK_LEN];
    unsigned int i, t;
    uint32_t s;

    make_nib62();
    for (i = 0; i < 3; i++) {
        drive_reset();
        apple2_mode = TRUE;
        make_disk(disk, 35);
        for (t = 0; t < 35; t++)
            make_track(&want[t*TRK_LEN], disk, t, dos_order, 0);
        if (i == 0) {
            drive_add(names[i], disk, 35*DSK_TRK);
        } else if (i == 1) {
            make_nic(file, 35);
            drive_add(names[i], file, 35*16*NIC_REC);
        } else {
            drive_add(names[i], want, 35*TRK_LEN);
        }
        REQUIRE(image_mount(names[i]) == FR_OK);
        image_stream_from(TRACK(5), 30000, got, TRK_LEN*8);

        /* New contents for physical sector 2 of track 5, written as DOS
         * writes a sector. */
        memset(&disk[5*DSK_TRK + dos_order[2]*256], 0x5a, 256);
        make_track(&want[5*TRK_LEN], disk, 5, dos_order, 0);
        image_write(TRACK(5), (2*REC_LEN + 48) * 8,
                    &want[5*TRK_LEN + 2*REC_LEN + 48], (REC_LEN - 48) * 8);

        s = image_stream_from(TRACK(5), 2*REC_LEN*8 + 900, got, TRK_LEN*8);
        bits_rotate(turned, &want[5*TRK_LEN], TRK_LEN*8, s);
        if (!CHECK_MEM(got, turned, TRK_LEN))
            ft_fail(__FILE__, __LINE__, "%s from bit %u", names[i], s);
        image_stream(TRACK(5), got, TRK_LEN);
        if (!CHECK_MEM(got, &want[5*TRK_LEN], TRK_LEN))
            ft_fail(__FILE__, __LINE__, "%s from the index", names[i]);
    }
}

/* Fragmented images, as a USB drive may hold them, read and written with
 * the floppy code's fast-seek table and without it. */
TEST(apple2_fragmented)
{
    unsigned int fast, t;

    make_nib62();
    for (fast = 0; fast < 2; fast++) {
        FRESULT (*mount)(const char *) =
            fast ? image_mount_fast_seek : image_mount;
        drive_reset();
        apple2_mode = TRUE;
        make_disk(disk, 35);
        CHECK_EQ(drive_add_fragmented("MADEUP.DSK", disk, 35*DSK_TRK), 70);
        memset(file, 0, 35*DSK_TRK);
        drive_add_fragmented("BLANK.DSK", file, 35*DSK_TRK);

        REQUIRE((*mount)("MADEUP.DSK") == FR_OK);
        stream_all(35);
        if (crc32(got, 35*TRK_LEN) != CRC_MADE_UP)
            ft_fail(__FILE__, __LINE__, "read %s fast seek: wrong stream",
                    fast ? "with" : "without");

        REQUIRE((*mount)("BLANK.DSK") == FR_OK);
        for (t = 0; t < 35; t++) {
            make_track(want, disk, t, dos_order, 0);
            image_write(TRACK(t), 0, want, TRK_LEN*8);
        }
        image_sync();
        CHECK_EQ(drive_get("BLANK.DSK", got, sizeof(got)), 35*DSK_TRK);
        if (!CHECK_MEM(got, disk, 35*DSK_TRK))
            ft_fail(__FILE__, __LINE__, "written %s fast seek",
                    fast ? "with" : "without");
    }
}

/* An HFE from a byte of its track on. */
TEST(apple2_hfe_mid_track)
{
    static const uint32_t starts[] = { 8, 2048, 2048*8 + 8, 100000 };
    static uint8_t cells[TRK_LEN*2], turned[TRK_LEN*2];
    unsigned int i;
    uint32_t len, s;

    make_nib62();
    drive_reset();
    apple2_mode = TRUE;
    make_disk(disk, 35);
    make_track(want, disk, 0, dos_order, 0);
    len = make_hfe(file, 0xff, TRUE);
    drive_add("GCR.HFE", file, len);
    /* Side 0 of the track, as the file holds it. */
    for (i = 0; i < TRK_LEN*2; i++)
        cells[i] = file[1024 + (i / 256) * 512 + (i % 256)];

    REQUIRE(image_mount("GCR.HFE") == FR_OK);
    CHECK_EQ(image_stream(TRACK(0), got, sizeof(got)), TRK_LEN*2);
    CHECK_MEM(got, cells, TRK_LEN*2);
    for (i = 0; i < ARRAY_SIZE(starts); i++) {
        s = image_stream_from(TRACK(0), starts[i], got, TRK_LEN*2*8);
        CHECK_EQ(s, starts[i]);
        bits_rotate(turned, cells, TRK_LEN*2*8, s);
        if (!CHECK_MEM(got, turned, TRK_LEN*2))
            ft_fail(__FILE__, __LINE__, "from bit %u", s);
    }
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

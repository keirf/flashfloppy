/*
 * test_apple2.c
 *
 * The Apple2 images: which HFEs each mode mounts and Apple2 mode lists,
 * and an HFE's stream from a byte of its track on.
 *
 * The disks are made up here, never taken from real disks. The expected
 * streams come from an encoder in this file, written from the format.
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

/* The file's sector that each physical sector holds, in DOS 3.3 order. */
static const uint8_t dos_order[16] = {
    0, 7, 14, 6, 13, 5, 12, 4, 11, 3, 10, 2, 9, 1, 8, 15
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

TEST(apple2_nibble_rule)
{
    make_nib62();
    CHECK_EQ(nib62[0], 0x96);
    CHECK_EQ(nib62[1], 0x97);
    CHECK_EQ(nib62[27], 0xcb);
    CHECK_EQ(nib62[63], 0xff);
}

/* Which files Apple2 mode lists, by size alone. */
TEST(apple2_listed_sizes)
{
    static const struct {
        const char *name;
        uint32_t size;
        bool_t listed;
    } files[] = {
        { "A.HFE", 932864, TRUE },
        { "A.HFE", 1405952, FALSE },
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
    CHECK(log_has("HFE(s) >1.25MB hidden"));
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

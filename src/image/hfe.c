/*
 * hfe.c
 * 
 * HxC Floppy Emulator (HFE) image files.
 * 
 * Written & released by Keir Fraser <keir.xen@gmail.com>
 * 
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

/* NB. Fields are little endian. */
struct disk_header {
    char sig[8];
    uint8_t formatrevision;
    uint8_t nr_tracks, nr_sides;
    uint8_t track_encoding;
    uint16_t bitrate; /* kB/s, approx */
    uint16_t rpm; /* unused, can be zero */
    uint8_t interface_mode;
    uint8_t rsvd; /* set to 1? */
    uint16_t track_list_offset;
    /* from here can write 0xff to end of block... */
    uint8_t write_allowed;
    uint8_t single_step;
    uint8_t t0s0_altencoding, t0s0_encoding;
    uint8_t t0s1_altencoding, t0s1_encoding;
};

/* track_encoding */
enum {
    ENC_ISOIBM_MFM,
    ENC_Amiga_MFM,
    ENC_ISOIBM_FM,
    ENC_Emu_FM,
    ENC_Unknown = 0xff
};

/* interface_mode */
enum {
    IFM_IBMPC_DD,
    IFM_IBMPC_HD,
    IFM_AtariST_DD,
    IFM_AtariST_HD,
    IFM_Amiga_DD,
    IFM_Amiga_HD,
    IFM_CPC_DD,
    IFM_GenericShugart_DD,
    IFM_IBMPC_ED,
    IFM_MSX2_DD,
    IFM_C64_DD,
    IFM_EmuShugart_DD,
    IFM_S950_DD,
    IFM_S950_HD,
    IFM_Disable = 0xfe
};

struct track_header {
    uint16_t offset;
    uint16_t len;
};

bool_t hfe_open(struct image *im)
{
    struct disk_header dhdr;
    UINT nr;

    im->fr = f_read(&im->fp, &dhdr, sizeof(dhdr), &nr);
    if (im->fr
        || strncmp(dhdr.sig, "HXCPICFE", sizeof(dhdr.sig))
        || (dhdr.formatrevision != 0))
        return FALSE;

    im->hfe.tlut_base = le16toh(dhdr.track_list_offset);
    im->nr_tracks = dhdr.nr_tracks * 2;

    return TRUE;
}

bool_t hfe_seek_track(struct image *im, uint8_t track)
{
    struct track_header thdr;
    UINT nr;

    if ((im->fr = f_lseek(&im->fp, im->hfe.tlut_base*512 + (track/2)*4))
        || (im->fr = f_read(&im->fp, &thdr, sizeof(thdr), &nr)))
        return FALSE;

    thdr.offset = le16toh(thdr.offset);
    thdr.len = le16toh(thdr.len);

    return TRUE;
}

void hfe_load_mfm(struct image *im, uint16_t *tbuf, uint16_t nr)
{
    
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

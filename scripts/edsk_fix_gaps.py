# edsk_fix_gaps.py
# 
# Some images with GAPS protection incorrectly have the next sector's pre-sync
# bytes (12x00) included. This script detects such images and strips the
# extra bytes.
# 
# Written & released by Keir Fraser <keir.xen@gmail.com>
# 
# This is free and unencumbered software released into the public domain.
# See the file COPYING for more details, or visit <http://unlicense.org>.

import struct, sys

def main(argv):
    if len(argv) != 3:
        print("%s <input_file> <output_file>" % argv[0])
        return
    in_f = open(argv[1], "rb")
    in_dat = in_f.read()
    out_dat = bytearray(in_dat[:256])
    in_dat = in_dat[256:]
    fixed = 0
    nr_idam_gaps = 0
    trk = 0
    while True:
        if not in_dat:
            break
        x = struct.unpack("<10s", in_dat[:10])
        if x[0] != b'Track-Info':
            out_dat += in_dat[:256]
            in_dat = in_dat[256:]
            continue
        (track, side) = struct.unpack("BB", in_dat[16:18])
        (n, nr, gap, filler) = struct.unpack("BBBB", in_dat[20:24])
        out_dat += in_dat[:24]
        sinfo = bytearray(in_dat[24:256])
        in_dat = in_dat[256:]
        sec_dat = [None] * nr
        idam_gaps = [False] * nr
        in_dlen = 0
        for i in range(nr):
            (c,h,r,n,s1,s2,alen) = struct.unpack("<BBBBBBH", sinfo[:8])
            sec_dat[i] = in_dat[:alen]
            in_dat = in_dat[alen:]
            in_dlen += alen
            if alen % 0x80 != 0 and alen >= 13:
                remainder = bytearray(sec_dat[i][-13:])
                # Check for exactly 12x00 at end of data area
                if remainder[0] != 0 and all([v == 0 for v in remainder[1:]]):
                    # Strip the 12x00 bytes
                    alen -= 12
                    sec_dat[i] = sec_dat[i][:-12]
                    sinfo[6] = alen & 0xff
                    sinfo[7] = (alen >> 8) & 0xff
                    # Check if the next sector's IDAM is included
                    idam = bytearray(12+4)
                    idam[12] = idam[13] = idam[14] = 0xa1
                    idam[15] = 0xfe
                    if sec_dat[i][-100:].find(idam) != -1 and i < nr-1:
                        # IDAM is included. Mark this sector for later fixup
                        idam_gaps[i] = True
                        nr_idam_gaps += 1
                    fixed += 1
            out_dat += sinfo[:8]
            sinfo = sinfo[8:]
        if sinfo:
            out_dat += sinfo
        # Check for required late fixups
        for i in reversed(range(nr)):
            if idam_gaps[i]:
                # This sector includes next sector's IDAM, presumably because
                # of a post-IDAM gapo signature. We fix this by completely
                # de-interleaving the sector. We work back to front as
                # may need to concatenate multiple later sectors.
                sec_dat[i] += bytearray(12)          # Pre-sync
                sec_dat[i] += bytearray([0xa1]) * 3  # Sync
                sec_dat[i] += bytearray([0xfb])      # DAM
                sec_dat[i] += sec_dat[i+1]           # Data
                alen = len(sec_dat[i])
                out_dat[24+i*8+6-256] = alen & 0xff
                out_dat[24+i*8+7-256] = (alen >> 8) & 0xff
        # Emit sector data areas
        out_dlen = 256
        for x in sec_dat:
            out_dat += x
            out_dlen += len(x)
        # Fix up input alignment
        if in_dlen % 256 != 0:
            in_dat = in_dat[256-in_dlen%256:]
        # Fix up output alignment
        if out_dlen % 256 != 0:
            out_dat += bytearray(256-out_dlen%256)
            out_dlen += 256 - out_dlen%256
        # Find track in track-sizes header and adjust track size
        while out_dat[52+trk] == 0:
            trk += 1
        out_dat[52+trk] = out_dlen >> 8
        trk += 1
    # All done! Create the output file and print a user summary
    out_f = open(argv[2], "wb")
    out_f.write(out_dat)
    if fixed != 0:
        print("Fixed %u sectors (%u de-interleaved)" % (fixed, nr_idam_gaps))
    else:
        print("No fixups")
    
if __name__ == "__main__":
    main(sys.argv)

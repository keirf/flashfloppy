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
        sec_dat = b''
        tot_dlen = 0
        _fixed = 0
        while sinfo and nr != 0:
            (c,h,r,n,s1,s2,alen) = struct.unpack("<BBBBBBH", sinfo[:8])
            sec_dat += in_dat[:alen]
            in_dat = in_dat[alen:]
            tot_dlen += alen
            if alen % 0x80 != 0 and alen >= 13:
                remainder = bytearray(sec_dat[-13:])
                if remainder[0] != 0 and all([v == 0 for v in remainder[1:]]):
                    alen -= 12
                    sec_dat = sec_dat[:-12]
                    sinfo[6] = alen & 0xff
                    sinfo[7] = (alen >> 8) & 0xff
                    idam = bytearray(12+4)
                    idam[12] = idam[13] = idam[14] = 0xa1
                    idam[15] = 0xfe
                    if sec_dat[-100:].find(idam) != -1 and nr > 1:
                        sys.exit("Cannot handle interleaved sectors")
                    fixed += 1
                    _fixed += 1
            out_dat += sinfo[:8]
            sinfo = sinfo[8:]
            nr -= 1
        if sinfo:
            out_dat += sinfo
        out_dat += sec_dat
        out_dat += bytearray(_fixed*12)
        if tot_dlen % 256 != 0:
            in_dat = in_dat[256-tot_dlen%256:]
            out_dat += bytearray(256-tot_dlen%256)
    out_f = open(argv[2], "wb")
    out_f.write(out_dat)
    if fixed != 0:
        print("Fixed %u sectors" % fixed)
    else:
        print("No fixups")
    
if __name__ == "__main__":
    main(sys.argv)

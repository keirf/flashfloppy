# edsk.py
# 
# Dump interesting per-sector info about an EDSK file
# 
# Written & released by Keir Fraser <keir.xen@gmail.com>
# 
# This is free and unencumbered software released into the public domain.
# See the file COPYING for more details, or visit <http://unlicense.org>.

import struct, sys, binascii

def main(argv):
    in_f = open(argv[1], "rb")
    in_dat = in_f.read()
    in_len = len(in_dat)
    x = struct.unpack("<34s14sBBH", in_dat[:52])
    tracks = int(x[2])
    sides = int(x[3])
    tsz = in_dat[52:256]
    populated = 0
    ext = False
    if x[0].startswith("EXTENDED CPC DSK File\r\nDisk-Info\r\n"):
        print("Extended DSK")
        ext = True
    else:
        assert x[0].startswith("MV - CPCEMU")
        print("Standard DSK")
    while tsz:
        x = struct.unpack("B", tsz[:1])
        if int(x[0]) != 0:
            populated += 1
        tsz = tsz[1:]
    print("%u cylinders, %u sides, %u non-empty tracks"
          % (tracks, sides, populated))
    in_dat = in_dat[256:]
    while True:
        if not in_dat:
            break
        x = struct.unpack("<10s", in_dat[:10])
        if x[0] != b'Track-Info':
            in_dat = in_dat[256:]
            continue
        (track, side) = struct.unpack("BB", in_dat[16:18])
        (n, nr, gap, filler) = struct.unpack("BBBB", in_dat[20:24])
        print("T%u.%u: N=%u nr=%u gap=%u fill=%x"
              % (track, side, n, nr, gap, filler))
        sinfo = in_dat[24:256]
        in_dat = in_dat[256:]
        tot_dlen = 0
        while sinfo and nr != 0:
            (c,h,r,n,s1,s2,alen) = struct.unpack("<BBBBBBH", sinfo[:8])
            if not ext:
                alen = 128<<n
            sdat = in_dat[:alen]
            in_dat = in_dat[alen:]
            tot_dlen += alen
            special = []
            _s1 = s1
            _s2 = s2
            _n = n
            if _n > 8:
                _n = 8
            nsz = 128 << _n
            if s2 & 0x01:
                special += ['DAM Missing']
            elif s1 & 0x01:
                special += ['IDAM Missing']
            s1 &= ~0x01
            s2 &= ~0x01
            if s1 & 0x20:
                if s2 & 0x20:
                    special += ['Data CRC']
                else:
                    special += ['ID CRC']
                s1 &= ~0x20
                s2 &= ~0x20
            if s2 & 0x40:
                special += ['DDAM']
                s2 &= ~0x40
            if s1 or s2:
                special += ['XXXX-UNKNOWN']
            if alen < nsz:
                if alen % 0x80 == 0:
                    special += ['Incomplete']
                else:
                    special += ['Small GAPS']
            elif alen > nsz:
                if alen % nsz == 0:
                    special += ['Weak (%u)' % (alen / nsz)]
                else:
                    special += ['Large GAPS']
            if alen % 0x80 != 0 and alen >= 13:
                remainder = bytearray(sdat[-13:])
                if remainder[0] != 0 and all([v == 0 for v in remainder[1:]]):
                    special += ["Pre-Sync??"]
            print("  %u.%u id=%u n=%u(%u) stat=%02x:%02x %u crc=%08x\t"
                  % (c,h,r,n,nsz,_s1,_s2,alen,binascii.crc32(sdat)&0xffffffff)
                  + str(special))
#            print(binascii.hexlify(sdat))
            sinfo = sinfo[8:]
            nr -= 1
        if tot_dlen % 256 != 0:
            in_dat = in_dat[256 - (tot_dlen % 256) : ]
    
if __name__ == "__main__":
    main(sys.argv)

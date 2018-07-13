# edsk_fix_speedlock.py
# 
# Speedlock-protected EDSK images in TOSEC are often missing the weak-sector
# information, and so fail to load with FlashFloppy. This script detects
# such images and adds the necessary info to make them work.
#
# Based on idea and original C implementation by Tom Dalby.
# 
# Written & released by Keir Fraser <keir.xen@gmail.com>
# 
# This is free and unencumbered software released into the public domain.
# See the file COPYING for more details, or visit <http://unlicense.org>.

import struct, sys, random

def main(argv):
    if len(argv) != 3:
        print("%s <input_file> <output_file>" % argv[0])
        return
    in_f = open(argv[1], "rb")
    in_dat = in_f.read()
    # Check image size
    if len(in_dat) < 2048:
        print("Not a valid EDSK image - Too short")
        return
    # Check image header
    x = struct.unpack("<34s14sBBH", in_dat[:52])
    if x[0] != b'EXTENDED CPC DSK File\r\nDisk-Info\r\n':
        print("Not a valid EDSK image - No image signature")
        return
    # Check track header
    trk_dat = in_dat[256:]
    x = struct.unpack("<10s", trk_dat[:10])
    (track, side) = struct.unpack("BB", trk_dat[16:18])
    (n, nr, gap, filler) = struct.unpack("BBBB", trk_dat[20:24])
    if x[0] != b'Track-Info' or track != 0 or side != 0 or n != 2 or nr != 9:
        print("Not a Speedlock image - Track header")
        return
    # Check sector headers for Speedlock-iness
    sinfo = trk_dat[24:256]
    for i in range(nr):
        (c,h,r,n,s1,s2,alen) = struct.unpack("<BBBBBBH", sinfo[:8])
        sinfo = sinfo[8:]
        if alen == 1536 and i == 1:
            print("Speedlock image is already fixed up!")
            return
        if c != 0 or h != 0 or r != i+1 or n != 2 or alen != 512:
            print("Not a Speedlock image - Sector %u header" % i)
            return
        if i == 1 and (s1 != s2 or s1 != 0x20):
            print("Not a Speedlock image - Bad CRC expected at weak sector")
            return
    # Modify the track-offset table to reflect longer track 0 data
    in_dat = bytearray(in_dat[:256])
    in_dat[52] = in_dat[52] + 4 # extra 4*256 bytes
    # Modify the track-0 info to reflect modified Gap3 and weak sector 2
    trk0_dat = bytearray(trk_dat[:256])
    trk0_dat[22] = 82    # Gap3 = 82
    trk0_dat[24+8+7] = 6 # Sector 2 actual length == 3*512
    # We are satisfied. Add the necessary info to the output file.
    out_f = open(argv[2], "wb")
    out_f.write(in_dat)   # Image Header
    out_f.write(trk0_dat) # Trk 0 header
    out_f.write(trk_dat[256:256+512]) # Sector 1 data
    out_f.write(bytearray(random.getrandbits(8) for _ in range(3*512))) # Sec 2
    out_f.write(trk_dat[256+512+512:]) # All the rest
    
if __name__ == "__main__":
    main(sys.argv)

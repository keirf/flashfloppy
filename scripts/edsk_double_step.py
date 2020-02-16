# edsk_double_step.py
#
# Create a double-step EDSK image by doubling up cylinders.
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
    out = bytearray(in_dat[:256])
    # Check image size
    if len(in_dat) < 2048:
        print("Not a valid EDSK image - Too short")
        return
    # Check image header
    sig, _, tracks, sides, tsz = struct.unpack("<34s14sBBH", in_dat[:52])
    out[48] = tracks * 2 # double up on number of cyls
    tszs = in_dat[52:256]
    in_dat = in_dat[256:]
    if sig.startswith(b"MV - CPCEMU"):
        for i in range(tracks):
            out += in_dat[:tsz*sides]
            for j in range(sides):
                out[16-tsz*(j+1)] = i*2 # fix cyl#
            out += in_dat[:tsz*sides]
            for j in range(sides):
                out[16-tsz*(j+1)] = i*2+1 # fix cyl#
            in_dat = in_dat[tsz*sides:]
    elif sig.startswith(b"EXTENDED CPC DSK File\r\nDisk-Info\r\n"):
        for i in range(tracks):
            for j in range(2):
                off = 0
                for k in range(sides):
                    tsz = tszs[k]*256
                    out += in_dat[off:off+tsz]
                    out[16-tsz] = i*2+j # fix cyl#
                    out[52+(i*2+j)*sides+k] = tszs[k] # fix track size
                    off += tsz
            tszs = tszs[sides:]
            in_dat = in_dat[off:]
    else:
        print("Not a valid EDSK image")
        return
    with open(argv[2], "wb") as f:
        f.write(out)
    
if __name__ == "__main__":
    main(sys.argv)

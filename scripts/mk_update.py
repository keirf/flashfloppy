# mk_update.py
#
# Convert a raw firmware binary into an update file for our bootloader:
#  N bytes: <raw binary data>
#  2 bytes: 'FY'
#  2 bytes: CRC16-CCITT, seed 0xFFFF, stored big endian
# 
# Written & released by Keir Fraser <keir.xen@gmail.com>
# 
# This is free and unencumbered software released into the public domain.
# See the file COPYING for more details, or visit <http://unlicense.org>.

import crcmod.predefined
import struct, sys

def main(argv):
    in_f = open(argv[1], "rb")
    out_f = open(argv[2], "wb")
    in_dat = in_f.read()
    in_len = len(in_dat)
    assert (in_len & 3) == 0, "input is not longword padded"
    crc16 = crcmod.predefined.Crc('crc-ccitt-false')
    out_f.write(in_dat)
    crc16.update(in_dat)
    in_dat = struct.pack("cc", 'F', 'Y')
    out_f.write(in_dat)
    crc16.update(in_dat)
    in_dat = struct.pack(">H", crc16.crcValue)
    out_f.write(in_dat)

if __name__ == "__main__":
    main(sys.argv)

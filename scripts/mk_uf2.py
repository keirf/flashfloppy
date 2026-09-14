# mk_uf2.py <input.bin> <output.uf2>
#
# Convert a raw binary (linked at the XIP flash base) into a UF2 file
# suitable for the RP2350 BOOTSEL mask-ROM loader.
#
# This is free and unencumbered software released into the public domain.
# See the file COPYING for more details, or visit <http://unlicense.org>.

import struct, sys

UF2_MAGIC_START0 = 0x0A324655
UF2_MAGIC_START1 = 0x9E5D5157
UF2_MAGIC_END    = 0x0AB16F30

FLAG_FAMILY_ID_PRESENT = 0x00002000

RP2350_ARM_S_FAMILY_ID = 0xE48BFF59

XIP_BASE = 0x10000000

PAYLOAD_SIZE = 256

def write_uf2(data, path, base=XIP_BASE):
    # Pad to a whole number of payload blocks.
    if len(data) % PAYLOAD_SIZE:
        data += b"\xff" * (PAYLOAD_SIZE - len(data) % PAYLOAD_SIZE)

    nr_blocks = len(data) // PAYLOAD_SIZE

    with open(path, "wb") as f:
        for blk in range(nr_blocks):
            payload = data[blk*PAYLOAD_SIZE:(blk+1)*PAYLOAD_SIZE]
            block = struct.pack("<8I",
                                UF2_MAGIC_START0,
                                UF2_MAGIC_START1,
                                FLAG_FAMILY_ID_PRESENT,
                                base + blk*PAYLOAD_SIZE,
                                PAYLOAD_SIZE,
                                blk,
                                nr_blocks,
                                RP2350_ARM_S_FAMILY_ID)
            block += payload + b"\x00" * (476 - PAYLOAD_SIZE)
            block += struct.pack("<I", UF2_MAGIC_END)
            assert len(block) == 512
            f.write(block)

    print("%s: %u blocks (%u bytes)" % (path, nr_blocks, len(data)))


def main(argv):
    if len(argv) != 3:
        print("Usage: %s <input.bin> <output.uf2>" % argv[0])
        return 1

    with open(argv[1], "rb") as f:
        data = f.read()

    write_uf2(data, argv[2])
    return 0

if __name__ == "__main__":
    sys.exit(main(sys.argv))

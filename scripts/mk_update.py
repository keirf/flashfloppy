# mk_update.py new <output> <firmware> <model>
# mk_update.py old <output> <firmware>
# mk_update.py verify <update_file>
#
# Convert a raw firmware binary into an update file for our bootloader.
#
# New Update Format (Little endian, unless otherwise stated):
#   File Header:
#     4 bytes: 'FFUP'
#     4 bytes: <offset to catalogue>
#     4 bytes: <number of catalogue entries>
#   Catalogue Entry:
#     1 byte:  <hw_model>
#     3 bytes: mbz
#     4 bytes: <offset>
#     4 bytes: <length>
#   Catalog Footer:
#     2 bytes: 'FZ'
#     2 bytes: CRC16-CCITT, seed 0xFFFF (big endian)
#   Payload Footer:
#     2 bytes: 'FY'
#     2 bytes: CRC16-CCITT, seed 0xFFFF (big endian)
#   File Footer:
#     4 bytes: CRC32 (MPEG-2, big endian)
#     4 bytes: 'FFUP'
#
# Old Update Format:
#  N bytes: <raw binary data>
#  2 bytes: 'FY'
#  2 bytes: CRC16-CCITT, seed 0xFFFF, stored big endian
#
# Written & released by Keir Fraser <keir.xen@gmail.com>
#
# This is free and unencumbered software released into the public domain.
# See the file COPYING for more details, or visit <http://unlicense.org>.

import crcmod.predefined
import re, struct, sys, os

name_to_hw_model = { 'stm32f105': 1,
                     'at32f435': 4 }

hw_model_to_name = { 1: 'STM32F105',
                     4: 'AT32F435' }

class Firmware:
    def __init__(self,model,binary):
        self.model = model
        self.binary = binary
    def __str__(self):
        s = hw_model_to_name[self.model] + ': '
        s += '%d bytes' % len(self.binary)
        return s

class Catalog:

    def __init__(self, f=None):
        self.catalog = []
        if f is None:
            return

        b = f.read()
        assert len(b) > 12, 'short header'

        # Check the file footer
        crc32 = crcmod.predefined.Crc('crc-32-mpeg')
        crc32.update(b[:-4])
        assert crc32.crcValue == 0, 'bad footer crc32'
        assert b[-4:] == b'FFUP', 'bad footer signature'

        # Check the file header
        sig, off, nr = struct.unpack('<4s2I', b[:12])
        assert sig == b'FFUP', 'bad header signature'
        assert off == 12, 'unexpected header offset'
        header_size = off + nr*12 + 4
        assert len(b) >= header_size, 'short header'

        # Check the header CRC16
        crc16 = crcmod.predefined.Crc('crc-ccitt-false')
        crc16.update(b[:header_size])
        assert crc16.crcValue == 0

        for i in range(nr):
            # Read catalog entry and payload
            m, o, l = struct.unpack('<B3x2I', b[off+i*12:off+(i+1)*12])
            assert len(b) >= o+l, 'payload past end of file'
            fw = b[o:o+l]
            # Check the payload CRC16
            crc16 = crcmod.predefined.Crc('crc-ccitt-false')
            crc16.update(fw)
            assert crc16.crcValue == 0
            # Check the payload footer
            sig, = struct.unpack('<2s', fw[-4:-2])
            assert sig == b'FY', 'Footer signature must be FY'
            # All good: Append to the catalog
            self.append(Firmware(m, fw))

    def append(self,firmware):
        # Models must be uniquely represented in the catalog
        for fw in self.catalog:
            assert fw.model != firmware.model, 'Model already in catalog'
        self.catalog.append(firmware)

    def serialise(self):
        # Header
        b = struct.pack('<4s2I', b'FFUP', 12, len(self.catalog))
        # Catalog entries
        off = 12 + len(self.catalog)*12 + 4
        for firmware in self.catalog:
            b += struct.pack('<B3x2I', firmware.model, off,
                             len(firmware.binary))
            off += len(firmware.binary)
        # Catalog footer
        b += b'FZ'
        crc16 = crcmod.predefined.Crc('crc-ccitt-false')
        crc16.update(b)
        b += struct.pack(">H", crc16.crcValue)
        # Payloads
        for firmware in self.catalog:
            b += firmware.binary
        # File footer
        crc32 = crcmod.predefined.Crc('crc-32-mpeg')
        crc32.update(b)
        b += struct.pack(">I4s", crc32.crcValue, b'FFUP')
        return b

# New: 'flashfloppy-*.upd'
def new_upd(argv):
    # Open the catalog, or else create a new one
    try:
        with open(argv[0], 'rb') as f:
            catalog = Catalog(f)
    except FileNotFoundError:
        catalog = Catalog()
    # Read the new firmware payload
    with open(argv[1], 'rb') as f:
        b = f.read()
    assert (len(b) & 3) == 0, "input is not longword padded"
    # Append the payload footer
    b += b'FY'
    crc16 = crcmod.predefined.Crc('crc-ccitt-false')
    crc16.update(b)
    b += struct.pack(">H", crc16.crcValue)
    # Add the new firmware to the catalog
    catalog.append(Firmware(name_to_hw_model[argv[2]], b))
    # Rewrite the catalog
    with open(argv[0], 'wb') as f:
        f.write(catalog.serialise())

# Old: 'FF_Gotek*.upd"
def old_upd(argv):
    in_f = open(argv[1], "rb")
    out_f = open(argv[0], "wb")
    in_dat = in_f.read()
    in_len = len(in_dat)
    assert (in_len & 3) == 0, "input is not longword padded"
    crc16 = crcmod.predefined.Crc('crc-ccitt-false')
    out_f.write(in_dat)
    crc16.update(in_dat)
    in_dat = struct.pack("cc", b'F', b'Y')
    out_f.write(in_dat)
    crc16.update(in_dat)
    in_dat = struct.pack(">H", crc16.crcValue)
    out_f.write(in_dat)

def verify_upd(argv):
    # Read the file footer to work out type of update file, old vs new
    with open(argv[0], 'rb') as f:
        f.seek(-4, os.SEEK_END)
        sig = f.read(2)
    if sig == b'FY':
        # Old
        print('Old Update File:')
        with open(argv[0], 'rb') as f:
            b = f.read()
        crc16 = crcmod.predefined.Crc('crc-ccitt-false')
        crc16.update(b)
        assert crc16.crcValue == 0
        print(' %s: %d bytes' % (hw_model_to_name[1], len(b)))
    else:
        # New
        print('New Update File:')
        with open(argv[0], 'rb') as f:
            catalog = Catalog(f)
        for firmware in catalog.catalog:
            print(' ' + str(firmware))

def main(argv):
    if argv[1] == 'new':
        dat = new_upd(argv[2:])
    elif argv[1] == 'old':
        dat = old_upd(argv[2:])
    elif argv[1] == 'verify':
        verify_upd(argv[2:])
        return
    else:
        assert False

if __name__ == "__main__":
    main(sys.argv)

# Local variables:
# python-indent: 4
# End:

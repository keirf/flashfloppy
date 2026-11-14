"""A FAT16 image of a USB drive with file and folder names in every encoding
that the firmware reads, for the integration tests and, written to a stick
by dd, for a Gotek: in folders named for their names' source, long names in
Cyrillic, in code page 437's letters and with characters that the firmware's
set lacks; and names that are only short ones, as a system writes them in
code page 866, in MIK, in 437, in 850, in 852 and in UTF-8, the MIK ones in small letters
too, which only MIK places at 0xb0 to 0xbf; folders and files of each,
every file a 360K PC image of zeros, which the firmware mounts, and every
folder holding one, A.IMG.

Usage: mk_names_image.py <image file>"""

import struct
import sys

SECTOR = 512
SECTORS = 32768  # 16 MB
FAT_SECTORS = 128
ROOT_ENTRIES = 512
DIR_CLUSTERS = 4
IMG = bytes(360 * 1024)

# The folders of the root, each with the short-name-codepage that reads its
# short names, the name of its image that the firmware lists first, and its
# entries: the short name's bytes, the long name or None, and whether it is
# a folder; then the name as the firmware shows it, without its extension.
FOLDERS = [
    ("LFN", "866", "?? smile", [
        (b"PAPKA~1", "\u041f\u0430\u043f\u043a\u0430", True,
         "\u041f\u0430\u043f\u043a\u0430"),
        (b"IGRA~1.IMG", "\u0418\u0433\u0440\u0430.img", False,
         "\u0418\u0433\u0440\u0430"),
        (b"ELKA~1.IMG", "\u0401\u043b\u043a\u0430.img", False,
         "\u0401\u043b\u043a\u0430"),
        (b"KASE~1.IMG", "K\u00e4se.img", False, "K\u00e4se"),
        (b"LODZ~1.IMG", "\u0141\u00f3d\u017a.img", False,
         "\u0141\u00f3d\u017a"),
        (b"SMILE~1.IMG", "\U0001f600 smile.img", False, "?? smile"),
    ]),
    ("SFN-437", "437", "CAF\u00c9", [
        (b"\x9aBER", None, True, "\u00dcBER"),
        (b"K\x8eSE.IMG", None, False, "K\u00c4SE"),
        (b"CAF\x90.IMG", None, False, "CAF\u00c9"),
    ]),
    ("SFN-850", "850", "\u00d8\u00c3\u00d0\u00ca", [
        (b"\xb5\xb6\xb7\xd4", None, True, "\u00c1\u00c2\u00c0\u00c8"),
        (b"\x9d\xc7\xd1\xd2.IMG", None, False, "\u00d8\u00c3\u00d0\u00ca"),
    ]),
    ("SFN-852", "852", "\u010c\u0160\u017d\u0158", [
        (b"\xc6\xa4\x8f\xa8", None, True, "\u0102\u0104\u0106\u0118"),
        (b"\xac\xe6\xa6\xfc.IMG", None, False,
         "\u010c\u0160\u017d\u0158"),
        (b"\xd1\xd0\xc7\xa5.IMG", None, False,
         "\u0110\u0111\u0103\u0105"),
    ]),
    ("SFN-866", "866", "\u0401\u041b\u041a\u0410", [
        (b"\x8f\x80\x8f\x8a\x80", None, True,
         "\u041f\u0410\u041f\u041a\u0410"),
        (b"\x88\x83\x90\x80.IMG", None, False,
         "\u0418\u0413\u0420\u0410"),
        (b"\xf0\x8b\x8a\x80.IMG", None, False,
         "\u0401\u041b\u041a\u0410"),
    ]),
    ("SFN-MIK", "mik", "\u0411\u044f\u043b\u0430", [
        (b"\x91\x8e\x94\x88\x9f", None, True,
         "\u0421\u041e\u0424\u0418\u042f"),
        (b"\x81\xbf\xab\xa0.IMG", None, False,
         "\u0411\u044f\u043b\u0430"),
        (b"\x8f\xba\xb2.IMG", None, False, "\u041f\u044a\u0442"),
    ]),
    ("SFN-UTF8", "866", "CAF\u00c9", [
        (b"\xd0\x81\xd0\x96", None, True, "\u0401\u0416"),
        (b"\xd0\x90\xd0\x93\xd0\x90\xd0\xa2.IMG", None, False,
         "\u0410\u0413\u0410\u0422"),
        (b"CAF\xc3\x89.IMG", None, False, "CAF\u00c9"),
    ]),
]


def short_name(name):
    """The 11 bytes of 8.3 name @name, the body and the extension padded."""
    body, _, ext = name.partition(b".")
    assert len(body) <= 8 and len(ext) <= 3
    return body.ljust(8) + ext.ljust(3)


def lfn_entries(lfn, sfn):
    """The entries of long name @lfn for 8.3 entry @sfn, last part first, as
    they go before it."""
    units = list(struct.unpack("<%dH" % (len(lfn.encode("utf-16-le")) // 2),
                               lfn.encode("utf-16-le")))
    nr = (len(units) + 12) // 13
    units += [0] + [0xffff] * (nr * 13 - len(units) - 1)
    total = 0
    for b in sfn:
        total = (((total & 1) << 7) + (total >> 1) + b) & 0xff
    out = []
    for i in range(nr, 0, -1):
        part = units[(i - 1) * 13:i * 13]
        out.append(bytes([i | (0x40 if i == nr else 0)])
                   + struct.pack("<5H", *part[0:5]) + bytes([0x0f, 0, total])
                   + struct.pack("<6H", *part[5:11]) + bytes(2)
                   + struct.pack("<2H", *part[11:13]))
    return out


class Volume:
    """The image as it is laid out: the FAT and the clusters in use."""

    def __init__(self):
        self.data = bytearray(SECTORS * SECTOR)
        self.fat = [0xfff8, 0xffff]
        self.root = 1 + 2 * FAT_SECTORS
        self.first = self.root + ROOT_ENTRIES * 32 // SECTOR

    def alloc(self, data):
        """Clusters for @data, in order, and the first of them."""
        first = len(self.fat)
        nr = max(1, -(-len(data) // SECTOR))
        for i in range(nr):
            self.fat.append(first + i + 1 if i + 1 < nr else 0xffff)
        at = (self.first + first - 2) * SECTOR
        self.data[at:at + len(data)] = data
        return first

    def boot(self):
        """The boot sector, of a FAT16 volume labeled NAMES."""
        b = bytearray(SECTOR)
        b[0:11] = b"\xeb\x3c\x90MKNAMES "
        struct.pack_into("<HBHBHHBHHHL", b, 11, SECTOR, 1, 1, 2, ROOT_ENTRIES,
                         SECTORS, 0xf8, FAT_SECTORS, 32, 2, 0)
        b[36] = 0x80
        b[38] = 0x29
        struct.pack_into("<L", b, 39, 0x20261025)
        b[43:62] = b"NAMES      FAT16   "
        b[510:512] = b"\x55\xaa"
        self.data[0:SECTOR] = b


def entry(name, attr, cluster, size):
    """A directory entry, of date 2026-10-25."""
    date = ((2026 - 1980) << 9) | (10 << 5) | 25
    if name[0] == 0xe5:
        name = b"\x05" + name[1:]
    return name + struct.pack("<BBBHHHHHHHL", attr, 0, 0, 0, date, date, 0, 0,
                              date, cluster, size)


def entries(items):
    """The directory entries of @items, (short name, long name, cluster,
    whether a folder, size), each long name before its 8.3 entry."""
    out = []
    for sfn, lfn, cluster, folder, size in items:
        sfn = short_name(sfn)
        if lfn is not None:
            out += lfn_entries(lfn, sfn)
        out.append(entry(sfn, 0x10 if folder else 0x20, cluster,
                         0 if folder else size))
    return out


def folder(v, items, parent):
    """A folder of @items below the folder of cluster @parent, 0 for the
    root: its first cluster."""
    first = len(v.fat)
    data = bytearray(DIR_CLUSTERS * SECTOR)
    v.alloc(data)
    ents = [entry(b".          ", 0x10, first, 0),
            entry(b"..         ", 0x10, parent, 0)] + entries(items)
    assert len(ents) * 32 <= len(data)
    at = (v.first + first - 2) * SECTOR
    for i, e in enumerate(ents):
        v.data[at + 32 * i:at + 32 * (i + 1)] = e
    return first


def build(path):
    """Writes the image to @path."""
    v = Volume()
    v.boot()
    root = []
    for name, _, _, items in FOLDERS:
        first = len(v.fat)
        v.alloc(bytearray(DIR_CLUSTERS * SECTOR))
        listed = []
        for sfn, lfn, is_folder, _ in items:
            if is_folder:
                sub = folder(v, [(b"A.IMG", None, v.alloc(IMG), False,
                                  len(IMG))], first)
                listed.append((sfn, lfn, sub, True, 0))
            else:
                listed.append((sfn, lfn, v.alloc(IMG), False, len(IMG)))
        ents = [entry(b".          ", 0x10, first, 0),
                entry(b"..         ", 0x10, 0, 0)] + entries(listed)
        at = (v.first + first - 2) * SECTOR
        assert len(ents) * 32 <= DIR_CLUSTERS * SECTOR
        for i, e in enumerate(ents):
            v.data[at + 32 * i:at + 32 * (i + 1)] = e
        root.append((name.encode(), None, first, True, 0))
    label = b"NAMES      " + bytes([0x08]) + bytes(20)
    for i, e in enumerate([label] + entries(root)):
        v.data[v.root * SECTOR + 32 * i:v.root * SECTOR + 32 * (i + 1)] = e
    assert len(v.fat) <= FAT_SECTORS * SECTOR // 2
    assert v.first + len(v.fat) - 2 <= SECTORS
    fat = struct.pack("<%dH" % len(v.fat), *v.fat)
    for n in range(2):
        at = (1 + n * FAT_SECTORS) * SECTOR
        v.data[at:at + len(fat)] = fat
    with open(path, "wb") as f:
        f.write(v.data)


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    build(sys.argv[1])

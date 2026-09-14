# mk_fat.py <srcdir> <output>
#
# Build a FAT16 volume image from a directory tree, for the RP2350 build's
# internal-flash image store (see src/pico2/flash_vol.c). The output is
# written as a raw binary and, if <output> ends in .uf2, as a UF2 targeting
# the store's flash offset so it can be dropped onto the RP2350 BOOTSEL drive.
#
# The volume always claims the full 3MB store in its BPB, but the output
# files are truncated after the last allocated cluster: free clusters are
# never read through the FAT, so whatever the flash holds there is ignored.
#
# No dependencies beyond the Python standard library.
#
# This is free and unencumbered software released into the public domain.
# See the file COPYING for more details, or visit <http://unlicense.org>.

import os, struct, sys, zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mk_uf2

# The image store starts 1MB into flash, above the firmware and its FF.CFG
# sector, and runs to the end of the 4MB QSPI flash fitted to every Pico 2.
# Must match src/pico2/flash_vol.c.
PART_OFF = 0x00100000
PART_END = 0x00400000

XIP_BASE = 0x10000000

# Geometry: 512-byte sectors and clusters. 6144 sectors -> 6063 data
# clusters, comfortably above the 4085 minimum that makes a volume FAT16.
SECSZ = 512
TOT_SEC = (PART_END - PART_OFF) // SECSZ
RSVD_SEC = 1
NUM_FATS = 2
ROOT_ENTS = 512
ROOT_SEC = ROOT_ENTS * 32 // SECSZ
FAT_SEC = 24                    # ceil((6065 entries * 2 bytes) / 512)
DATA_SEC = TOT_SEC - RSVD_SEC - NUM_FATS*FAT_SEC - ROOT_SEC
NR_CLUSTERS = DATA_SEC          # one sector per cluster
DATA_START = RSVD_SEC + NUM_FATS*FAT_SEC + ROOT_SEC

VOL_LABEL = b"FLASHFLOPPY"      # exactly 11 bytes

# Fixed timestamp, for deterministic images: 2026-09-13 12:00:00.
DOS_DATE = ((2026-1980) << 9) | (9 << 5) | 13
DOS_TIME = 12 << 11

SHORT_VALID = set(b"ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789$%'-_@~`!(){}^#&")


class Volume:
    def __init__(self):
        self.image = bytearray(TOT_SEC * SECSZ)
        self.fat = [0] * (NR_CLUSTERS + 2)
        self.fat[0] = 0xfff8
        self.fat[1] = 0xffff
        self.next_cluster = 2
        self.nr_files = self.nr_dirs = self.total = 0

    def alloc_chain(self, nr):
        """Allocate `nr` clusters as a chain; returns the first, 0 if nr==0."""
        if nr == 0:
            return 0
        first = self.next_cluster
        if first + nr - 1 > NR_CLUSTERS + 1:
            raise Exception("out of space (%u-byte volume)"
                            % (TOT_SEC * SECSZ))
        for i in range(nr):
            c = first + i
            self.fat[c] = c+1 if i < nr-1 else 0xffff
        self.next_cluster += nr
        return first

    def write_clusters(self, first, data):
        off = (DATA_START + (first-2)) * SECSZ
        self.image[off:off+len(data)] = data

    def finalise(self):
        boot = struct.pack("<3s8sHBHBHHBHHHIIBBBI11s8s",
                           b"\xeb\x3c\x90", b"MSDOS5.0",
                           SECSZ,           # bytes/sector
                           1,               # sectors/cluster
                           RSVD_SEC, NUM_FATS, ROOT_ENTS,
                           TOT_SEC,         # 16-bit total sectors
                           0xf8,            # media descriptor
                           FAT_SEC,
                           32, 4,           # sectors/track, heads (unused)
                           0, 0,            # hidden, 32-bit total sectors
                           0x80, 0, 0x29,   # drive number, rsvd, boot sig
                           zlib.crc32(bytes(self.image)) & 0xffffffff,
                           VOL_LABEL, b"FAT16   ")
        self.image[:len(boot)] = boot
        self.image[510:512] = b"\x55\xaa"
        for n in range(NUM_FATS):
            off = (RSVD_SEC + n*FAT_SEC) * SECSZ
            fat = struct.pack("<%uH" % len(self.fat), *self.fat)
            self.image[off:off+len(fat)] = fat
        # Truncate after the last allocated cluster: free clusters are never
        # read through the FAT, so the flash's old content there is ignored.
        used = (DATA_START + (self.next_cluster-2)) * SECSZ
        return bytes(self.image[:used])


def lfn_checksum(short):
    s = 0
    for c in short:
        s = (((s & 1) << 7) + (s >> 1) + c) & 0xff
    return s


def make_short_name(name, used):
    """8.3 rendering of `name`; returns (11 bytes, needs_lfn)."""
    base, dot, ext = name.rpartition(".")
    if not dot or not base:
        base, ext = name, ""

    def mangle(s, maxlen):
        out = bytearray()
        for c in s.upper().encode("ascii", "replace"):
            if len(out) == maxlen:
                break
            out.append(c if c in SHORT_VALID else ord("_"))
        return bytes(out)

    sbase, sext = mangle(base.replace(".", ""), 8), mangle(ext, 3)
    exact = (sbase.decode() + (("." + sext.decode()) if sext else "") == name)
    short = sbase.ljust(8) + sext.ljust(3)

    if short not in used:
        return short, not exact

    # Numeric-tail the base until unique.
    n = 0
    while True:
        n += 1
        tail = ("~%u" % n).encode()
        short = (sbase[:8-len(tail)] + tail).ljust(8) + sext.ljust(3)
        if short not in used:
            return short, True
        if n > 0xfffff:
            raise Exception("cannot make a unique short name for '%s'" % name)


def dirent(short, attr, cluster, size):
    return struct.pack("<11sBBBHHHHHHHI", short, attr, 0, 0,
                       DOS_TIME, DOS_DATE, DOS_DATE, 0,
                       DOS_TIME, DOS_DATE, cluster, size)


def name_entries(name, used):
    """LFN entries (if needed) plus the 32-byte header of the short entry."""
    short, needs_lfn = make_short_name(name, used)
    used.add(short)
    if not needs_lfn:
        return short, b""

    ucs = name.encode("utf-16le")
    if len(ucs) % 26:
        # NUL-terminate only if the name does not exactly fill its entries.
        ucs += b"\x00\x00" + b"\xff" * (-(len(ucs)+2) % 26)
    cksum = lfn_checksum(short)
    ents = b""
    nr = len(ucs) // 26
    for i in reversed(range(nr)):
        chunk = ucs[i*26:(i+1)*26]
        ents += struct.pack("<B10sBBB12sH4s",
                            (i+1) | (0x40 if i == nr-1 else 0),
                            chunk[:10], 0x0f, 0, cksum,
                            chunk[10:22], 0, chunk[22:26])
    return short, ents


def add_dir(vol, srcdir, parent_cluster, depth):
    """Build directory table for `srcdir`; returns (table bytes, cluster)."""
    entries = b""
    used = set()

    names = sorted(os.listdir(srcdir))
    dirs = [n for n in names if os.path.isdir(os.path.join(srcdir, n))
            and not n.startswith(".")]
    files = [n for n in names if os.path.isfile(os.path.join(srcdir, n))
             and not n.startswith(".")]

    # Reserve this directory's clusters up front so children allocate after
    # them (cosmetic; any order would work). Entry count isn't known until
    # names are processed, so do a dry run over the names first.
    nr_ents = (0 if parent_cluster is None else 2)
    dry = set()
    for n in dirs + files:
        short, lfn = name_entries(n, dry)
        nr_ents += 1 + len(lfn)//32

    if parent_cluster is None:
        if nr_ents + 1 > ROOT_ENTS:     # +1 for the volume label
            raise Exception("too many entries in the root directory")
        cluster = 0
        entries += dirent(VOL_LABEL, 0x08, 0, 0)
    else:
        nr_clusters = (nr_ents*32 + SECSZ-1) // SECSZ
        cluster = vol.alloc_chain(max(nr_clusters, 1))
        entries += dirent(b".          ", 0x10, cluster, 0)
        entries += dirent(b"..         ", 0x10, parent_cluster, 0)

    for n in dirs:
        short, lfn = name_entries(n, used)
        sub, sub_cluster = add_dir(vol, os.path.join(srcdir, n), cluster,
                                   depth+1)
        vol.write_clusters(sub_cluster, sub)
        entries += lfn + dirent(short, 0x10, sub_cluster, 0)
        vol.nr_dirs += 1

    for n in files:
        with open(os.path.join(srcdir, n), "rb") as f:
            data = f.read()
        short, lfn = name_entries(n, used)
        first = vol.alloc_chain((len(data) + SECSZ-1) // SECSZ)
        vol.write_clusters(first, data)
        entries += lfn + dirent(short, 0x20, first, len(data))
        print("  %s%s (%u bytes)" % ("  "*depth, n, len(data)))
        vol.nr_files += 1
        vol.total += len(data)

    return entries, cluster


def main(argv):
    if len(argv) != 3:
        print("Usage: %s <srcdir> <output>" % argv[0])
        return 1

    srcdir, output = argv[1:]
    if not os.path.isdir(srcdir):
        print("Error: '%s' is not a directory" % srcdir)
        return 1

    vol = Volume()
    try:
        root, _ = add_dir(vol, srcdir, None, 0)
    except Exception as e:
        print("Error: %s" % e)
        return 1
    root_off = (RSVD_SEC + NUM_FATS*FAT_SEC) * SECSZ
    vol.image[root_off:root_off+len(root)] = root
    image = vol.finalise()

    binfile = output
    if output.endswith(".uf2"):
        binfile = output[:-4] + ".bin"
    with open(binfile, "wb") as f:
        f.write(image)

    print("%s: %u files, %u dirs, %u bytes of data in a %u-byte volume"
          % (binfile, vol.nr_files, vol.nr_dirs, vol.total, TOT_SEC*SECSZ))

    if output.endswith(".uf2"):
        mk_uf2.write_uf2(image, output, XIP_BASE + PART_OFF)

    print("Flash it with:")
    print("  picotool load -o 0x%08x %s" % (XIP_BASE + PART_OFF, binfile))
    if output.endswith(".uf2"):
        print("or copy %s to the RP2350 BOOTSEL drive." % output)

    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))

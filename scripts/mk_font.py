# mk_font.py
#
# Translate a BDF font or PNG font image into a C header, or a BDF font into a
# PNG font image.
#
# Written & released by Keir Fraser <keir.xen@gmail.com>
#
# This is free and unencumbered software released into the public domain.
# See the file COPYING for more details, or visit <http://unlicense.org>.

import binascii, re, struct, sys, zlib

# The PNG uses the planned encoding: paired Latin capitals and small letters at
# 0x80-0xbf, ordered by base letter and accent; Russian in KOI8-R order at
# 0xc0-0xff. Unpaired Latin letters and the pound sign are at the right of the
# Latin rows. The firmware still generates its original ASCII-only font.
first = 0x20
ascii_codes = list(range(first, 0x7f))
latin = [
    0x00c1, 0x00c2, 0x00c4, 0x00c0, 0x00c5, 0x00c3, 0x00c6, 0x00c7,
    0x00d0, 0x00c9, 0x00ca, 0x00cb, 0x00c8, 0x00cd, 0x00ce, 0x00cf,
    0x00cc, 0x00d1, 0x00d3, 0x00d4, 0x00d6, 0x00d2, 0x00d8, 0x00d5,
    0x00de, 0x00da, 0x00db, 0x00dc, 0x00d9, 0x00dd, 0x0131, 0x00a3,
    0x00e1, 0x00e2, 0x00e4, 0x00e0, 0x00e5, 0x00e3, 0x00e6, 0x00e7,
    0x00f0, 0x00e9, 0x00ea, 0x00eb, 0x00e8, 0x00ed, 0x00ee, 0x00ef,
    0x00ec, 0x00f1, 0x00f3, 0x00f4, 0x00f6, 0x00f2, 0x00f8, 0x00f5,
    0x00fe, 0x00fa, 0x00fb, 0x00fc, 0x00f9, 0x00fd, 0x00ff, 0x00df,
]
russian = [ord(bytes([c]).decode("koi8_r")) for c in range(0xc0, 0x100)]
square = 0x25a1


def codes_of(extent):
    """The code points of the firmware's original uncompressed ASCII font."""
    return list(ascii_codes)


def rows_of(cols, width):
    """The glyph's 16 rows from its top and bottom pages of columns."""
    return [sum(((cols[x + (y // 8) * width] >> (y % 8)) & 1)
                << (width - 1 - x) for x in range(width))
            for y in range(16)]


def cols_of(rows, width):
    """A glyph's top and bottom pages of columns from its 16 rows."""
    return [sum(((rows[y] >> (width - 1 - x)) & 1) << (y % 8)
                for y in range(8 * page, 8 * page + 8))
            for page in range(2) for x in range(width)]


def cell_codes():
    """The Unicode code point of each encoding byte, None where unassigned."""
    cells = [None] * 256
    for code in ascii_codes:
        cells[code] = code
    cells[0x7f] = square
    for cell, code in zip(range(0x80, 0xc0), latin):
        cells[cell] = code
    for cell, code in zip(range(0xc0, 0x100), russian):
        cells[cell] = code
    return cells


def read_bdf(path, codes):
    """Read available @codes from a BDF font, returning 16-row glyphs."""
    with open(path, "r") as in_f:
        glyphs = {}
        for line in in_f:
            match = re.match(
                r"^FONTBOUNDINGBOX ([0-9]+) ([0-9]+) ([0-9]+) ([-0-9]+)", line)
            if match:
                break
        width = int(match.group(1))
        height = int(match.group(2))
        y_base = int(match.group(4))
        gap = 16 - height
        tgap = gap // 2
        bgap = 16 - height - tgap
        for line in in_f:
            match = re.match(r"^ENCODING ([0-9]+)", line)
            if not match:
                continue
            code = int(match.group(1))
            if code not in codes:
                continue
            x_shift = 0
            top_space = 0
            bottom_space = 0
            for line in in_f:
                bbx = re.match(
                    r"^BBX ([0-9]+) ([0-9]+) ([0-9]+) ([-0-9]+)", line)
                if bbx:
                    x_shift = int(bbx.group(3))
                    y_height = int(bbx.group(2))
                    y_shift = int(bbx.group(4))
                    bottom_space = y_shift - y_base
                    top_space = height - (bottom_space + y_height)
                if re.match("BITMAP", line):
                    break
            char = []
            top_space += tgap
            for i in range(top_space):
                char.append(0)
            for line in in_f:
                if re.match("ENDCHAR", line):
                    break
                match = re.match(r"([0-9A-F]+)", line)
                char.append(int(match.group(1), 16) >> x_shift)
            bottom_space += bgap
            for i in range(bottom_space):
                char.append(0)
            glyphs[code] = char
    return glyphs, width, height, tgap, bgap


def read_font(path):
    """Read an original ASCII C font, returning 16-bit columns by code point."""
    with open(path) as in_f:
        text = in_f.read()
    match = re.search(r"const uint8_t \w+\[\] aligned\(4\) = \{(.*?)\};",
                      text, re.S)
    data = [int(x, 16) for x in re.findall(r"0x([0-9a-f]+)", match.group(1))]
    width = len(data) // (2 * len(ascii_codes))
    glyphs = {}
    for n, code in enumerate(ascii_codes):
        cols = data[2 * width * n:2 * width * (n + 1)]
        glyphs[code] = [cols[x] | cols[width + x] << 8 for x in range(width)]
    return glyphs


def png_chunk(kind, data):
    """One PNG chunk, with its length and CRC."""
    return (struct.pack(">I", len(data)) + kind + data
            + struct.pack(">I", binascii.crc32(kind + data) & 0xffffffff))


def write_png(path, glyphs, width, height, tgap):
    """Write all 256 cells as a checkerboard PNG image."""
    white = bytes((0xff, 0xff, 0xff))
    gray = bytes((0xc0, 0xc0, 0xc0))
    black = bytes((0, 0, 0))
    cells = []
    for code in cell_codes():
        char = glyphs.get(code) if code is not None else None
        cells.append(char[tgap:tgap + height] if char else [0] * height)
    raw = bytearray()
    for cell_y in range(8):
        for y in range(height):
            raw.append(0)
            for cell_x in range(32):
                cell = cell_y * 32 + cell_x
                paper = white if (cell_x + cell_y) % 2 == 0 else gray
                bits = (cells[cell][y] >> (8 - width)) & ((1 << width) - 1)
                for x in range(width):
                    raw += black if bits & (1 << (width - 1 - x)) else paper
    header = struct.pack(">IIBBBBB", 32 * width, 8 * height, 8, 2, 0, 0, 0)
    with open(path, "wb") as out_f:
        out_f.write(b"\x89PNG\r\n\x1a\n")
        out_f.write(png_chunk(b"IHDR", header))
        out_f.write(png_chunk(b"IDAT", zlib.compress(bytes(raw), 9)))
        out_f.write(png_chunk(b"IEND", b""))


def paeth(a, b, c):
    """PNG's Paeth predictor."""
    p = a + b - c
    pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
    return a if pa <= pb and pa <= pc else b if pb <= pc else c


def read_png_image(path):
    """Read a non-interlaced 8-bit RGB PNG font image into 256 cell glyphs."""
    with open(path, "rb") as in_f:
        png = in_f.read()
    if not png.startswith(b"\x89PNG\r\n\x1a\n"):
        raise ValueError(path + ": not a PNG file")
    pos = 8
    header = None
    compressed = bytearray()
    while pos < len(png):
        size = struct.unpack(">I", png[pos:pos + 4])[0]
        kind = png[pos + 4:pos + 8]
        data = png[pos + 8:pos + 8 + size]
        crc = struct.unpack(">I", png[pos + 8 + size:pos + 12 + size])[0]
        if crc != binascii.crc32(kind + data) & 0xffffffff:
            raise ValueError(path + ": bad PNG chunk CRC")
        pos += 12 + size
        if kind == b"IHDR":
            header = struct.unpack(">IIBBBBB", data)
        elif kind == b"tRNS":
            raise ValueError(path + ": transparency is not supported")
        elif kind == b"IDAT":
            compressed += data
        elif kind == b"IEND":
            break
    if header is None:
        raise ValueError(path + ": no PNG header")
    image_width, image_height, depth, color, compression, filtering, interlace = header
    if (depth != 8 or color != 2 or compression != 0 or filtering != 0
            or interlace != 0):
        raise ValueError(path + ": expected a non-interlaced 8-bit RGB PNG")
    if image_width % 32 or image_height % 8:
        raise ValueError(path + ": expected 32 by 8 cells")
    width, height = image_width // 32, image_height // 8
    if not 1 <= width <= 8 or not 1 <= height <= 16:
        raise ValueError(path + ": unsupported cell size")
    raw = zlib.decompress(bytes(compressed))
    stride = image_width * 3
    if len(raw) != (stride + 1) * image_height:
        raise ValueError(path + ": wrong PNG pixel data length")
    rows = []
    prior = bytearray(stride)
    pos = 0
    for y in range(image_height):
        filter_type = raw[pos]
        row = bytearray(raw[pos + 1:pos + 1 + stride])
        pos += stride + 1
        if filter_type > 4:
            raise ValueError(path + ": unknown PNG filter")
        for x in range(stride):
            left = row[x - 3] if x >= 3 else 0
            above = prior[x]
            upper_left = prior[x - 3] if x >= 3 else 0
            if filter_type == 1:
                row[x] = (row[x] + left) & 0xff
            elif filter_type == 2:
                row[x] = (row[x] + above) & 0xff
            elif filter_type == 3:
                row[x] = (row[x] + (left + above) // 2) & 0xff
            elif filter_type == 4:
                row[x] = (row[x] + paeth(left, above, upper_left)) & 0xff
        pixels = [tuple(row[x:x + 3]) for x in range(0, stride, 3)]
        rows.append(pixels)
        prior = row
    glyphs = {}
    for cell_y in range(8):
        for cell_x in range(32):
            cell = cell_y * 32 + cell_x
            paper = ((0xff, 0xff, 0xff) if (cell_x + cell_y) % 2 == 0
                     else (0xc0, 0xc0, 0xc0))
            glyph = []
            for y in range(height):
                bits = 0
                for x in range(width):
                    pixel = rows[cell_y * height + y][cell_x * width + x]
                    if pixel == (0, 0, 0):
                        bits |= 1 << (width - 1 - x)
                    elif pixel != paper:
                        raise ValueError(
                            "%s: unexpected color in cell 0x%02x" % (path, cell))
                glyph.append(bits << (8 - width))
            glyphs[cell] = glyph
    return glyphs, width, height


def read_png_font(path, codes, allow_blank=False):
    """Read the requested code points from a PNG font image."""
    cells, width, height = read_png_image(path)
    assigned = cell_codes()
    for cell, code in enumerate(assigned):
        if code is None and any(cells[cell]):
            raise ValueError(
                "%s: unassigned cell 0x%02x is not blank; to add a character, "
                "update cell_codes() and codes_of() in scripts/mk_font.py and "
                "the firmware's font lookup and character mappings" % (path, cell))
    by_code = {code: cell for cell, code in enumerate(assigned)
               if code is not None}
    tgap = (16 - height) // 2
    bgap = 16 - height - tgap
    glyphs = {}
    for code in codes:
        char = [0] * tgap + cells[by_code[code]] + [0] * bgap
        if code != 0x20 and not allow_blank and not any(char):
            raise ValueError("%s: no glyph for U+%04X" % (path, code))
        glyphs[code] = char
    return glyphs, width


def write_c(generator, name, codes, glyphs, width):
    """Write the original uncompressed C font, byte for byte."""
    with open(name + ".c", "w") as out_f:
        out_f.write("/* Autogenerated by " + generator + " */\n")
        out_f.write("const uint8_t %s[] aligned(4) = {\n" % name)
        for n, code in enumerate(codes):
            char = glyphs[code]
            rows = [(c >> (8 - width)) & ((1 << width) - 1) for c in char]
            cols = cols_of(rows, width)
            for page in range(2):
                out_f.write("    ")
                for col in cols[page * width:(page + 1) * width]:
                    out_f.write("0x%02x, " % col)
                if page == 1:
                    out_f.write(" /* '%c' %02u */" % (code, n))
                out_f.write("\n")
        out_f.write("};\n")


def main(argv):
    if len(argv) > 1 and argv[1] == "--png":
        if len(argv) != 4:
            raise SystemExit("usage: mk_font.py --png INPUT.bdf OUTPUT.png")
        codes = [code for code in cell_codes() if code is not None]
        glyphs, width, height, tgap, bgap = read_bdf(argv[2], codes)
        write_png(argv[3], glyphs, width, height, tgap)
        return
    if len(argv) not in (3, 4):
        raise SystemExit("usage: mk_font.py INPUT.bdf|INPUT.png OUTPUT [ascii]")
    codes = codes_of(argv[3] if argv[3:] else "")
    if argv[1].lower().endswith(".png"):
        glyphs, width = read_png_font(argv[1], codes)
    else:
        glyphs, width, height, tgap, bgap = read_bdf(argv[1], codes)
        for code in codes:
            if code not in glyphs:
                raise ValueError("%s: no glyph for U+%04X" % (argv[1], code))
    write_c(argv[0], argv[2], codes, glyphs, width)


if __name__ == "__main__":
    main(sys.argv)

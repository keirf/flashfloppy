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

# The code points of the glyphs, in the order of the firmware's character
# set: generic currency and Central European letters at 0x01-0x1f; ASCII;
# Latin capitals at 0x80-0x9d and their small letters 32 cells below, ordered
# by base letter and accent; unpaired letters and the pound at the right;
# Russian in KOI8-R order; last the hollow-square fallback.
low = [
    0x00a4,
    0x0102, 0x0104, 0x0106, 0x0118, 0x013d,
    0x0141, 0x0143, 0x0154, 0x0158, 0x015a,
    0x015e, 0x0162, 0x016e, 0x0179, 0x017b,
    0x0103, 0x0105, 0x0107, 0x0119, 0x013e,
    0x0142, 0x0144, 0x0155, 0x0159, 0x015b,
    0x015f, 0x0163, 0x016f, 0x017a, 0x017c,
]
first = 0x20
ascii_codes = list(range(first, 0x7f))
latin = [
    0x00c1, 0x00c2, 0x00c4, 0x00c0, 0x00c5, 0x00c3, 0x00c6, 0x00c7,
    0x0110, 0x00c9, 0x00ca, 0x00cb, 0x00c8, 0x00cd, 0x00ce, 0x00cf,
    0x00cc, 0x00d1, 0x00d3, 0x00d4, 0x00d6, 0x00d2, 0x00d8, 0x00d5,
    0x00de, 0x00da, 0x00db, 0x00dc, 0x00d9, 0x00dd, 0x0131, 0x00a3,
    0x00e1, 0x00e2, 0x00e4, 0x00e0, 0x00e5, 0x00e3, 0x00e6, 0x00e7,
    0x0111, 0x00e9, 0x00ea, 0x00eb, 0x00e8, 0x00ed, 0x00ee, 0x00ef,
    0x00ec, 0x00f1, 0x00f3, 0x00f4, 0x00f6, 0x00f2, 0x00f8, 0x00f5,
    0x00fe, 0x00fa, 0x00fb, 0x00fc, 0x00f9, 0x00fd, 0x00ff, 0x00df,
]
russian = [ord(bytes([c]).decode("koi8_r")) for c in range(0xc0, 0x100)]
square = 0x25a1

# The glyphs go into flash packed, each its used rows alone: a header byte,
# the first used row << 4 | the number of rows - 1, then the rows, each the
# glyph's width in bits, column 0 first, packed end to end; a glyph equal to
# an earlier one shares its data. A 16-bit offset per glyph finds it. The
# struct oled_font of inc/util.h ties the two arrays and the width; the
# generated file is compiled with the firmware's decls.h included.

def codes_of(extent):
    """The code points of a font of extent @extent, "ascii" or "", in the
    order of its glyphs."""
    codes = list(ascii_codes)
    if extent != "ascii":
        codes = low + codes
        codes += [code for code in latin if code is not None] + russian
    return codes + [square]

def rows_of(cols, width):
    """A glyph's 16 rows, each its @width columns as bits, column 0 the
    highest, from its columns @cols, the top page's then the bottom page's,
    bit 0 the top pixel of a column."""
    return [sum(((cols[x + (y // 8) * width] >> (y % 8)) & 1)
                << (width - 1 - x) for x in range(width))
            for y in range(16)]

def pack(rows, width):
    """The packed bytes of a glyph of @rows, see above."""
    used = [y for y in range(16) if rows[y]]
    top, nr = (used[0], len(rows[used[0]:used[-1] + 1])) if used else (0, 1)
    bits = 0
    for y in range(top, top + nr):
        bits = (bits << width) | rows[y]
    nbits = nr * width
    pad = (-nbits) % 8
    bits <<= pad
    body = bits.to_bytes((nbits + pad) // 8, "big") if nbits else b""
    return bytes([top << 4 | (nr - 1)]) + body

def unpack(data, width):
    """The 16 rows of the glyph packed at the start of @data, see pack()."""
    top, nr = data[0] >> 4, (data[0] & 15) + 1
    nbits = nr * width
    bits = int.from_bytes(data[1:1 + (nbits + 7) // 8], "big") >> ((-nbits) % 8)
    rows = [0] * 16
    for i in range(nr):
        rows[top + nr - 1 - i] = bits & ((1 << width) - 1)
        bits >>= width
    return rows

def cols_of(rows, width):
    """A glyph's columns, as rows_of() takes them, from its 16 rows."""
    return [sum(((rows[y] >> (width - 1 - x)) & 1) << (y % 8)
                for y in range(8 * page, 8 * page + 8))
            for page in range(2) for x in range(width)]

def read_font(path):
    """The glyphs of generated font file @path by code point, each its
    columns as 16-bit values, bit 0 the top pixel."""
    with open(path) as f:
        text = f.read()
    width = int(re.search(r"= \{ (\d+),", text).group(1))
    arrays = {m.group(1): [int(x, 16) for x in re.findall(
        r"0x([0-9a-f]+)", m.group(2))] for m in re.finditer(
        r"(\w+)\[\] aligned\(4\) = \{(.*?)\};", text, re.S)}
    data = bytes(arrays[[k for k in arrays if k.endswith("_data")][0]])
    offs = arrays[[k for k in arrays if k.endswith("_off")][0]]
    codes = codes_of("ascii" if len(offs) == len(ascii_codes) + 1 else "")
    assert len(codes) == len(offs)
    glyphs = {}
    for code, off in zip(codes, offs):
        cols = cols_of(unpack(data[off:], width), width)
        glyphs[code] = [cols[x] | cols[width + x] << 8 for x in range(width)]
    return glyphs

def cell_codes():
    """The Unicode code point of each encoding byte. Cell 0x7f holds the
    hollow square; every other unassigned cell is blank."""
    cells = [None] * 256
    for cell, code in enumerate(low, 1):
        cells[cell] = code
    for code in ascii_codes:
        cells[code] = code
    cells[0x7f] = square
    for cell, code in zip(range(0x80, 0xc0), latin):
        cells[cell] = code
    for cell, code in zip(range(0xc0, 0x100), russian):
        cells[cell] = code
    return cells


def read_bdf(path, codes):
    """Read @codes from a BDF font, returning 16-row glyphs and its metrics."""
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
        bgap = gap - tgap
        for line in in_f:
            match = re.match("^ENCODING ([0-9]+)", line)
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
                    "^BBX ([0-9]+) ([0-9]+) ([0-9]+) ([-0-9]+)", line)
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
                match = re.match("([0-9A-F]+)", line)
                char.append(int(match.group(1), 16) >> x_shift)
            bottom_space += bgap
            for i in range(bottom_space):
                char.append(0)
            glyphs[code] = char
    assert all(code in glyphs for code in codes)
    return glyphs, width, height, tgap, bgap


def png_chunk(kind, data):
    """One PNG chunk, with its length and CRC."""
    return (struct.pack(">I", len(data)) + kind + data
            + struct.pack(">I", binascii.crc32(kind + data) & 0xffffffff))


def write_png(path, glyphs, width, height, tgap):
    """Write all 256 cells as a checkerboard PNG image of a BDF font."""
    white = bytes((0xff, 0xff, 0xff))
    gray = bytes((0xc0, 0xc0, 0xc0))
    black = bytes((0, 0, 0))
    cells = []
    for code in cell_codes():
        if code is None:
            cells.append([0] * height)
            continue
        char = glyphs.get(code, glyphs[square])
        cells.append(char[tgap:tgap + height])
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


def read_png_font(path, codes):
    """Read the firmware's code points from a PNG font image."""
    cells, width, height = read_png_image(path)
    assigned = cell_codes()
    for cell, code in enumerate(assigned):
        if code is None and any(cells[cell]):
            raise ValueError(
                "%s: unassigned cell 0x%02x is not blank; to add a character, "
                "update cell_codes() and codes_of() in scripts/mk_font.py, "
                "oled_font_index() in src/display/lcd_common.c, and the "
                "mappings in src/charset.c" % (path, cell))
    by_code = {code: cell for cell, code in enumerate(assigned)
               if code is not None}
    tgap = (16 - height) // 2
    bgap = 16 - height - tgap
    glyphs = {}
    for code in codes:
        char = [0] * tgap + cells[by_code[code]] + [0] * bgap
        if code != 0x20 and not any(char):
            raise ValueError("%s: no glyph for U+%04X" % (path, code))
        glyphs[code] = char
    return glyphs, width


def write_c(generator, name, codes, glyphs, width):
    """Write the packed C font, keeping the BDF converter's exact layout."""
    with open(name + ".c", "w") as out_f:
        out_f.write("/* Autogenerated by " + generator + " */\n")
        out_f.write("static const uint8_t %s_data[] aligned(4) = {\n" % name)
        data, offs, starts = bytearray(), [], {}
        for n, code in enumerate(codes):
            char = list(glyphs.get(code, glyphs[square]))
            rows = [(c >> (8 - width)) & ((1 << width) - 1) for c in char]
            packed = pack(rows, width)
            if packed in starts:
                offs.append(starts[packed])
                continue
            starts[packed] = len(data)
            offs.append(len(data))
            data += packed
            out_f.write("    " + "".join("0x%02x, " % b for b in packed))
            if code < 0x80:
                out_f.write(" /* '%c' %02u */\n" % (code, n))
            else:
                out_f.write(" /* U+%04X %02u */\n" % (code, n))
        out_f.write("};\n\n")
        out_f.write("static const uint16_t %s_off[] aligned(4) = {\n" % name)
        for i in range(0, len(offs), 8):
            out_f.write("    " + "".join("0x%04x, " % o for o in offs[i:i+8])
                        + "\n")
        out_f.write("};\n\n")
        out_f.write("const struct oled_font %s = { %u, %s_off, %s_data };\n"
                    % (name, width, name, name))


def main(argv):
    if len(argv) > 1 and argv[1] == "--png":
        if len(argv) != 4:
            raise SystemExit("usage: mk_font.py --png INPUT.bdf OUTPUT.png")
        glyphs, width, height, tgap, bgap = read_bdf(argv[2], codes_of(""))
        write_png(argv[3], glyphs, width, height, tgap)
        return
    if len(argv) not in (3, 4):
        raise SystemExit("usage: mk_font.py INPUT.bdf|INPUT.png OUTPUT [ascii]")
    codes = codes_of(argv[3] if argv[3:] else "")
    if argv[1].lower().endswith(".png"):
        glyphs, width = read_png_font(argv[1], codes)
    else:
        glyphs, width, height, tgap, bgap = read_bdf(argv[1], codes)
    write_c(argv[0], argv[2], codes, glyphs, width)


if __name__ == "__main__":
    main(sys.argv)

"""PNG font images by encoding byte, exact packed C conversion, and validation."""

import os
import subprocess
import sys
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, "scripts"))
import mk_font


class FontImages(unittest.TestCase):

    @staticmethod
    def columns(char, width):
        """The 16-bit columns of a PNG glyph padded to 16 rows."""
        rows = [(c >> (8 - width)) & ((1 << width) - 1) for c in char]
        cols = mk_font.cols_of(rows, width)
        return [cols[x] | cols[width + x] << 8 for x in range(width)]

    def test_unassigned_cell_error(self):
        """A glyph in an unassigned cell explains where to add a character."""
        original = mk_font.cell_codes
        with tempfile.TemporaryDirectory() as tmp:
            try:
                def with_glyph():
                    cells = original()
                    cells[0] = ord("A")
                    return cells
                mk_font.cell_codes = with_glyph
                committed = os.path.join(ROOT, "fonts", "6x16.png")
                glyphs, width, height = mk_font.read_png_font(
                    committed, mk_font.codes_of(""))
                png = os.path.join(tmp, "6x16.png")
                mk_font.write_png(png, glyphs, width, 16, 0)
            finally:
                mk_font.cell_codes = original
            with self.assertRaises(ValueError) as caught:
                mk_font.read_png_font(png, mk_font.codes_of(""))
            message = str(caught.exception)
            self.assertIn("unassigned cell 0x00", message)
            self.assertIn("cell_codes()", message)
            self.assertIn("codes_of()", message)
            self.assertIn("oled_font_index()", message)
            self.assertIn("src/charset.c", message)

    def test_cell_layout(self):
        """Every assigned cell has its glyph; unassigned cells are blank."""
        self.assertEqual(len(mk_font.low), 31)
        self.assertEqual(mk_font.low[0], 0x00a4)
        self.assertEqual(mk_font.latin[30], 0x0131)
        self.assertEqual(mk_font.latin[31], 0x00a3)
        self.assertEqual(mk_font.latin[62], 0x00ff)
        self.assertEqual(mk_font.latin[63], 0x00df)
        for n in range(15):
            self.assertEqual(chr(mk_font.low[n + 1]).lower(),
                             chr(mk_font.low[n + 16]))
        for n in range(30):
            self.assertEqual(chr(mk_font.latin[n]).lower(),
                             chr(mk_font.latin[n + 32]))
        for font, size in (("6x16", (6, 16)), ("8x16", (8, 16))):
            with self.subTest(font=font):
                png = os.path.join(ROOT, "fonts", font + ".png")
                cells, width, height = mk_font.read_png_image(png)
                self.assertEqual((width, height), size)
                assigned = mk_font.cell_codes()
                self.assertEqual({code for code in assigned if code is not None},
                                 set(mk_font.codes_of("")))
                for cell, code in enumerate(assigned):
                    if code is None:
                        self.assertFalse(any(cells[cell]),
                                         "cell 0x%02x is not blank" % cell)
                    elif code != 0x20:
                        self.assertTrue(any(cells[cell]),
                                        "cell 0x%02x is blank" % cell)
                self.assertEqual(assigned[0x01], 0x00a4)
                self.assertEqual(assigned[0x02], 0x0102)
                self.assertEqual(assigned[0x1f], 0x017c)
                self.assertEqual(assigned[0x7f], 0x25a1)
                self.assertEqual(assigned[0x9f], 0x00a3)
                self.assertEqual(assigned[0xbf], 0x00df)

    def test_small_cell_layout(self):
        """Small fonts hold ASCII and Cyrillic; every other cell is clear."""
        for font, size in (("7x8", (7, 8)), ("8x8", (8, 8))):
            with self.subTest(font=font):
                png = os.path.join(ROOT, "fonts", font + ".png")
                with open(png, "rb") as handle:
                    self.assertEqual(handle.read(26)[25], 6)
                cells, width, height = mk_font.read_png_image(png)
                self.assertEqual((width, height), size)
                for cell in range(256):
                    occupied = (0x20 <= cell < 0x80) or (cell >= 0xc0)
                    if occupied and cell != 0x20:
                        self.assertTrue(any(cells[cell]),
                                        "cell 0x%02x is blank" % cell)
                    else:
                        self.assertFalse(any(cells[cell]),
                                         "cell 0x%02x is not blank" % cell)

    def test_c_conversion(self):
        """Packed C fonts contain exactly the PNG's glyphs."""
        script = os.path.join(ROOT, "scripts", "mk_font.py")
        for font in ("6x16", "8x16", "7x8", "8x8"):
            with self.subTest(font=font), tempfile.TemporaryDirectory() as tmp:
                committed = os.path.join(ROOT, "fonts", font + ".png")
                extents = ((None, "ascii", "cyr")
                           if font in ("6x16", "8x16") else ("cyr",))
                for extent in extents:
                    outdir = os.path.join(tmp, extent or "full")
                    os.mkdir(outdir)
                    command = [sys.executable, script, committed,
                               "oled_font_" + font]
                    if extent:
                        command.append(extent)
                    subprocess.run(command, cwd=outdir, check=True)
                    generated_path = os.path.join(
                        outdir, "oled_font_" + font + ".c")
                    generated = mk_font.read_font(generated_path)
                    with open(generated_path) as handle:
                        c_text = handle.read()
                    codes = mk_font.codes_of(extent or "")
                    glyphs, width, height = mk_font.read_png_font(committed, codes)
                    self.assertIn(" = { %u, %u," % (width, height), c_text,
                                  extent)
                    expected = {code: self.columns(glyphs[code], width)
                                for code in codes}
                    self.assertEqual(generated, expected, extent)

    def test_png_round_trip(self):
        """PNG glyphs regenerate an identical image."""
        fonts = (("6x16", ""), ("8x16", ""), ("7x8", "cyr"), ("8x8", "cyr"))
        for font, extent in fonts:
            with self.subTest(font=font), tempfile.TemporaryDirectory() as tmp:
                committed = os.path.join(ROOT, "fonts", font + ".png")
                glyphs, width, height = mk_font.read_png_font(
                    committed, mk_font.codes_of(extent))
                png = os.path.join(tmp, font + ".png")
                mk_font.write_png(png, glyphs, width, height, 0, extent)
                self.assertEqual(mk_font.read_png_image(png),
                                 mk_font.read_png_image(committed))


if __name__ == "__main__":
    unittest.main()

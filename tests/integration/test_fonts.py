"""PNG font images by encoding byte and exact conversion to C and back."""

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

    @staticmethod
    def read_all(path):
        """Every assigned glyph, including cells still blank in a source font."""
        codes = [code for code in mk_font.cell_codes() if code is not None]
        return mk_font.read_png_font(path, codes, allow_blank=True)

    def test_unassigned_cell_error(self):
        """A glyph in an unassigned cell explains where to add a character."""
        original = mk_font.cell_codes
        with tempfile.TemporaryDirectory() as tmp:
            try:
                def with_glyph():
                    cells = original()
                    cells[1] = ord("A")
                    return cells
                mk_font.cell_codes = with_glyph
                committed = os.path.join(ROOT, "fonts", "6x16.png")
                glyphs, width = self.read_all(committed)
                png = os.path.join(tmp, "6x16.png")
                mk_font.write_png(png, glyphs, width, 16, 0)
            finally:
                mk_font.cell_codes = original
            with self.assertRaises(ValueError) as caught:
                mk_font.read_png_font(png, mk_font.codes_of(""))
            message = str(caught.exception)
            self.assertIn("unassigned cell 0x01", message)
            self.assertIn("cell_codes()", message)
            self.assertIn("codes_of()", message)
            self.assertIn("font lookup", message)
            self.assertIn("character mappings", message)

    def test_cell_layout(self):
        """Thirty case pairs, unpaired letters and pound at the right."""
        self.assertEqual(mk_font.latin[30], 0x0131)
        self.assertEqual(mk_font.latin[31], 0x00a3)
        self.assertEqual(mk_font.latin[62], 0x00ff)
        self.assertEqual(mk_font.latin[63], 0x00df)
        for n in range(30):
            self.assertEqual(chr(mk_font.latin[n]).lower(),
                             chr(mk_font.latin[n + 32]))
        for font, size in (("6x16", (6, 16)), ("8x16", (8, 16))):
            with self.subTest(font=font):
                png = os.path.join(ROOT, "fonts", font + ".png")
                cells, width, height = mk_font.read_png_image(png)
                self.assertEqual((width, height), size)
                assigned = mk_font.cell_codes()
                for cell, code in enumerate(assigned):
                    if code is None:
                        self.assertFalse(any(cells[cell]),
                                         "cell 0x%02x is not blank" % cell)
                for cell in range(0x21, 0x7f):
                    self.assertTrue(any(cells[cell]),
                                    "cell 0x%02x is blank" % cell)
                if font == "8x16":
                    self.assertTrue(any(cells[0x7f]))
                for cell in range(0x80, 0xc0):
                    if cell != 0x9e:
                        self.assertTrue(any(cells[cell]),
                                        "cell 0x%02x is blank" % cell)

    def test_ascii_c_conversion(self):
        """The original C font contains exactly the PNG's ASCII glyphs."""
        script = os.path.join(ROOT, "scripts", "mk_font.py")
        for font in ("6x16", "8x16"):
            with self.subTest(font=font), tempfile.TemporaryDirectory() as tmp:
                committed = os.path.join(ROOT, "fonts", font + ".png")
                subprocess.run(
                    [sys.executable, script, committed, "oled_font_" + font],
                    cwd=tmp, check=True)
                generated_path = os.path.join(tmp, "oled_font_" + font + ".c")
                generated = mk_font.read_font(generated_path)
                codes = mk_font.codes_of("")
                glyphs, width = mk_font.read_png_font(committed, codes)
                expected = {code: self.columns(glyphs[code], width)
                            for code in codes}
                self.assertEqual(generated, expected)

    def test_png_round_trip(self):
        """Every available source glyph regenerates an identical image."""
        for font, height, tgap in (("6x16", 16, 0), ("8x16", 16, 0)):
            with self.subTest(font=font), tempfile.TemporaryDirectory() as tmp:
                committed = os.path.join(ROOT, "fonts", font + ".png")
                glyphs, width = self.read_all(committed)
                png = os.path.join(tmp, font + ".png")
                mk_font.write_png(png, glyphs, width, height, tgap)
                self.assertEqual(mk_font.read_png_image(png),
                                 mk_font.read_png_image(committed))


if __name__ == "__main__":
    unittest.main()

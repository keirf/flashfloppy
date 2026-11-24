"""The minimal UI variant: reduced display rendering with normal name handling."""

import unittest

import ffemu_run as fr
import test_display as display
import test_names as names


PROGRAM = "ffemu-minimal-ui"


def pixels(r):
    """The OLED's pixel rows in run @r's dumps."""
    return [line for line in r.lines if line and set(line) <= set(".#")]


class MinimalUI(display.Case, unittest.TestCase):

    def test_wide_is_double_height(self):
        """A wide row has the same rendering as a double-height row."""
        runs = []
        for order in ("0w,1", "0d,1"):
            r = fr.run(
                PROGRAM, ["wait 10000 log Name:", "sleep 300", "dump"],
                settings=["display = ssd1306-128x64"],
                flash=["display-type = oled-128x64",
                       "display-order = " + order],
                files=display.DSK)
            self.assertWaits(r, 1)
            runs.append(r)
        self.assertEqual(pixels(runs[0]), pixels(runs[1]),
                         runs[0].report() + runs[1].report())

    def test_hex_phase_on_oled(self):
        """The Apple2 phase digit is hexadecimal, with no spinner glyph."""
        rows, r = self.rows(
            PROGRAM, display.half_tracks(2), "ssd1306-128x32", files=display.DSK)
        self.assertEqual(rows[2], "000/000   T:01.4     ", r.report())
        self.assertEqual(
            display.oled_text(pixels(r), 16, "6x16", False), rows[2],
            r.report())

    def test_ascii_fonts(self):
        """Every font setting uses ASCII 6x16, while the name still opens."""
        for font in ("6x16", "8x16", "7x8", "8x8"):
            with self.subTest(font=font):
                r = fr.run(
                    PROGRAM, names.MOUNT + ["log"],
                    settings=["display = ssd1306-128x32"],
                    flash=["display-type = oled-128x32",
                           "oled-font = " + font],
                    files={"\u0418\u0433\u0440\u0430.dsk": bytes(35 * 4096)})
                self.assertWaits(r, 1)
                self.assertIn("Name: '\u0418\u0433\u0440\u0430'", r.stdout,
                              r.report())
                fallback = fr.font_glyphs("6x16")[0x25a1]
                self.assertEqual(names.name_cells(r, "6x16", 4), [fallback] * 4,
                                 r.report())

    def test_no_scrolling(self):
        """A long name stays at its start after the usual scrolling pause."""
        r = fr.run(
            PROGRAM, ["wait 10000 log Name:", "sleep 300", "dump",
                      "sleep 5000", "dump"],
            settings=["display = ssd1306-128x32"],
            flash=["display-type = oled-128x32"],
            files={"ABCDEFGHIJKLMNOPQRSTUVW.DSK": bytes(35 * 4096)})
        self.assertWaits(r, 1)
        frames = pixels(r)
        self.assertEqual(len(frames), 64, r.report())
        self.assertEqual(frames[:32], frames[32:], r.report())

    def test_combined_row_uses_name_row(self):
        """Combined row 4 uses the image-name row; status is unchanged."""
        rows, r = self.rows(
            PROGRAM, display.half_tracks(2), "hd44780-16x2",
            flash=["display-order = 4,1"], files=display.DSK)
        self.assertEqual(
            rows, ["TEST".ljust(16), "000/000  T:01.4 "], r.report())

    def test_combined_row_uses_name_row_on_oled(self):
        """Row 4 falls back to row 0 on the OLED and on FF OSD."""
        rows, r = self.rows(
            PROGRAM, display.half_tracks(2), "ssd1306-128x32",
            flash=["display-order = C,S", "osd-display-order = C,S"],
            files=display.DSK)
        self.assertEqual(rows[0], "TEST".ljust(21), r.report())
        self.assertEqual(
            display.oled_text(pixels(r), 0, "6x16", False), rows[0],
            r.report())

    def test_directory_track_row_uses_directory_row(self):
        """Combined row 5 uses the ordinary directory row."""
        rows, r = self.rows(
            PROGRAM, display.half_tracks(2), "hd44780-16x2",
            flash=["display-order = 5,3"], files=display.DSK)
        self.assertEqual(
            rows, ["/".ljust(16), "/".ljust(16)], r.report())

    def test_show_dir_with_ignored(self):
        """Directory rows ignore slash styling in minimal UI."""
        rows, r = self.rows(
            PROGRAM, [], "hd44780-16x2",
            flash=["display-order = D,Z", "show-dir-with = prefix-slash"],
            files={"GAMES/A.DSK": display.DSK["TEST.DSK"]})
        self.assertEqual(
            rows, ["GAMES".ljust(16), "GAMES".ljust(16)], r.report())

    def test_no_font_test(self):
        """The Main Menu stays available without the Font Test item."""
        r = fr.run(
            PROGRAM, ["key remove", "sleep 500", "key select", "sleep 500",
                      "dump", "osd"],
            settings=["display = ssd1306-128x64"])
        self.assertWaits(r, 0)
        self.assertIn("Main Menu", r.stdout, r.report())
        self.assertNotIn("Font Test", r.stdout, r.report())

    def test_error_code_only(self):
        """An error screen shows its code without the meaning from fs_hint."""
        rows, r = self.rows(
            PROGRAM, ["sleep 1000"], "hd44780-16x2", files=display.Errors.BAD)
        self.assertEqual(rows, ["BAD".ljust(16), "E31".ljust(16)], r.report())


if __name__ == "__main__":
    unittest.main()

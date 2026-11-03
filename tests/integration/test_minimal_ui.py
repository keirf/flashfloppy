"""The minimal UI variant: reduced display rendering with normal name handling."""

import unittest

import ffemu_run as fr
import test_display as display


PROGRAM = "ffemu-minimal-ui"


def pixels(r):
    """The OLED's pixel rows in run @r's dumps."""
    return [line for line in r.lines if line and set(line) <= set(".#")]


class MinimalUI(display.Case, unittest.TestCase):

    def test_hex_phase_on_oled(self):
        """The Apple2 phase digit is hexadecimal, with no spinner glyph."""
        rows, r = self.rows(
            PROGRAM, display.half_tracks(2), "ssd1306-128x32", files=display.DSK)
        self.assertEqual(rows[2], "000/000   T:01.4     ", r.report())
        self.assertEqual(
            display.oled_text(pixels(r), 16, "6x13", False), rows[2],
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

    def test_error_code_only(self):
        """An error screen shows its code without the meaning from fs_hint."""
        bad = {"BAD.HFE": b"HXCPICFE" + bytes(600)}
        rows, r = self.rows(
            PROGRAM, ["sleep 1000"], "hd44780-16x2", files=bad)
        self.assertEqual(rows, ["BAD".ljust(16), "E31".ljust(16)], r.report())


if __name__ == "__main__":
    unittest.main()

"""The minimal UI variant: reduced display rendering with normal name handling."""

import unittest

import ffemu_run as fr
import test_display as display


PROGRAM = "ffemu-minimal-ui"


def pixels(r):
    """The OLED's pixel rows in run @r's dumps."""
    return [line for line in r.lines if line and set(line) <= set(".#")]


class MinimalUI(display.Case, unittest.TestCase):

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


if __name__ == "__main__":
    unittest.main()

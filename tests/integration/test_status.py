"""The status command's line on the display: the display fitted, by its
controller, and for a 7-segment display what it shows, as "wait display" and
"wait segments" match it."""

import unittest

import ffemu_run as fr


class DisplayStatus(fr.Case, unittest.TestCase):

    def display_line(self, display, cmds, nr_waits):
        """The display line of "status" with @display fitted, after @cmds, of
        which @nr_waits are waits."""
        r = fr.run(
            "ffemu", cmds + ["status"], settings=["display = " + display])
        self.assertWaits(r, nr_waits)
        lines = [line for line in r.lines if line.startswith("display: ")]
        self.assertEqual(len(lines), 1, r.report())
        return lines[0]

    def banner(self, display, text):
        """The display line of "status" with @display fitted, showing @text."""
        return self.display_line(display, [
            "wait 10000 log ** FlashFloppy", "key remove",
            "wait 5000 segments " + fr.led_segments(text)], 2)

    def test_tm1651(self):
        self.assertRegex(
            self.banner("tm1651-3digit", "fmf"),
            r'^display: TM1651 3 digits, on, brightness [0-7], '
            rf'showing "fmf", segments {fr.led_segments("fmf")}$')

    def test_74hc164(self):
        self.assertEqual(
            self.banner("74hc164-2digit", "ff"),
            'display: 74HC164 2 digits, showing "ff", segments '
            + fr.led_segments("ff"))

    def test_lcd(self):
        line = self.display_line(
            "hd44780-20x4", ["wait 10000 log I2C: LCD found at 0x27"], 1)
        self.assertEqual(line, "display: HD44780 20x4, ROM jap")


if __name__ == "__main__":
    unittest.main()

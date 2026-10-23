"""The LCD's and the OLED's rows as the firmware lays them out: the status
row's track. An OLED's rows are read as FF OSD gets them, beside it on the I2C
bus: the display's text, as many columns as it has."""

import unittest

import ffemu_run as fr

PC = {"A.IMG": bytes(737280)}


def steps(nr):
    """@nr steps of a Shugart host, which ffemu starts with the drive selected
    and DIR inward."""
    return ["key step", "sleep 5"] * nr


class Case(fr.Case):

    def rows(self, name, commands, display, flash=(), files=PC):
        """The rows of the display, through FF OSD for an OLED, once the image
        is in and @commands have run."""
        r = fr.run(
            name, ["wait 10000 log Name:"] + list(commands)
            + ["sleep 500", "dump", "osd"],
            settings=["display = " + display], flash=flash, files=files)
        self.assertWaits(r, 1)
        lcd = display.startswith("hd44780")
        out = r.stdout.split("\nosd: ")
        rows = [
            line[1:-1] for line in out[0 if lcd else 1].splitlines()
            if line.startswith("|") and line.endswith("|")]
        return rows, r


class ThreeDigits(Case, unittest.TestCase):
    """A track over 99 shows all of its digits, on 16 columns too."""

    def test_lcd_16_columns(self):
        rows, r = self.rows("ffemu", steps(100), "hd44780-16x2")
        self.assertEqual(rows[1], "000/000  T:100.0", r.report())

    def test_oled_8x16(self):
        rows, r = self.rows(
            "ffemu", steps(100), "ssd1306-128x64",
            flash=["oled-font = 8x16"])
        self.assertEqual(rows[2], "000/000  T:100.0", r.report())


if __name__ == "__main__":
    unittest.main()

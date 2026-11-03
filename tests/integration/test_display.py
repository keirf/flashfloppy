"""The LCD's and the OLED's rows as the firmware lays them out: the status
row's track, in the long form or in short with show-tracks-short, as the apple2
firmware always gives it, with the phases. An OLED's rows are read as FF OSD
gets them, beside it on the I2C bus: the display's text, as many columns as it
has."""

import unittest

import ffemu_run as fr

PC = {"A.IMG": bytes(737280)}
DSK = {"TEST.DSK": bytes(35 * 4096)}


def steps(nr):
    """@nr steps of a Shugart host, which ffemu starts with the drive selected
    and DIR inward."""
    return ["key step", "sleep 5"] * nr


def half_tracks(nr):
    """@nr half tracks of an Apple2 host's seek inward, two states of its
    phases each: the next phase on beside the last, then alone."""
    return ["key phase-in", "sleep 20"] * (2 * nr)


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


class TracksShort(Case, unittest.TestCase):
    """show-tracks-short = yes: the side, `T` and the track at the status
    row's right; `W` in place of `T` while writing, `*` for a write-protected
    image."""

    SHORT = ["show-tracks-short = yes"]

    def test_lcd_16_columns(self):
        rows, r = self.rows(
            "ffemu", steps(3), "hd44780-16x2", flash=self.SHORT)
        self.assertEqual(rows[1], "000/000     0T03", r.report())

    def test_lcd_20_columns(self):
        rows, r = self.rows(
            "ffemu", steps(3) + ["key side"], "hd44780-20x4",
            flash=self.SHORT + ["display-type = lcd-20x04"])
        self.assertEqual(rows[2], "000/000         1T03", r.report())

    def test_oled(self):
        rows, r = self.rows(
            "ffemu", steps(100), "ssd1306-128x32", flash=self.SHORT)
        self.assertEqual(rows[2], "000/000         0T100", r.report())

    def test_writing(self):
        rows, r = self.rows(
            "ffemu", ["key write"], "hd44780-16x2", flash=self.SHORT)
        self.assertEqual(rows[1], "000/000     0W00", r.report())

    def test_write_protected(self):
        rows, r = self.rows(
            "ffemu", [], "hd44780-16x2",
            flash=self.SHORT + ["write-protect = yes"])
        self.assertEqual(rows[1], "000/000     0*00", r.report())

    def test_long_by_default(self):
        rows, r = self.rows("ffemu", steps(3), "hd44780-16x2")
        self.assertEqual(rows[1], "000/000  T:03.0 ", r.report())


class Apple2(Case, unittest.TestCase):
    """The apple2 firmware uses the long track layout by default and the short
    one with show-tracks-short. An LCD shows the four phase bits as one
    uppercase hex digit; an OLED shows only the phase spinner, which FF OSD
    gets as a blank."""

    def test_lcd_16_columns(self):
        rows, r = self.rows(
            "ffemu-apple2", half_tracks(2), "hd44780-16x2", files=DSK)
        self.assertEqual(rows[1], "000/000  T:01.4 ", r.report())

    def test_lcd_20_columns(self):
        rows, r = self.rows(
            "ffemu-apple2", half_tracks(2), "hd44780-20x4",
            flash=["display-type = lcd-20x04"], files=DSK)
        self.assertEqual(rows[2], "000/000   T:01.4    ", r.report())

    def test_oled(self):
        rows, r = self.rows(
            "ffemu-apple2", half_tracks(2), "ssd1306-128x32", files=DSK)
        self.assertEqual(rows[2], "000/000   T:01       ", r.report())

    def test_oled_8x16(self):
        rows, r = self.rows(
            "ffemu-apple2", half_tracks(2), "ssd1306-128x64",
            flash=["oled-font = 8x16"], files=DSK)
        self.assertEqual(rows[2], "000/000  T:01   ", r.report())

    def test_short(self):
        rows, r = self.rows(
            "ffemu-apple2", half_tracks(2), "hd44780-16x2", files=DSK,
            flash=["show-tracks-short = yes"])
        self.assertEqual(rows[1], "000/000     4T01", r.report())

    def test_three_digits(self):
        rows, r = self.rows(
            "ffemu-apple2", half_tracks(200), "ssd1306-128x32", files=DSK)
        self.assertEqual(rows[2], "000/000   T:100      ", r.report())

    def test_three_digits_8x16(self):
        rows, r = self.rows(
            "ffemu-apple2", half_tracks(200), "ssd1306-128x64",
            flash=["oled-font = 8x16"], files=DSK)
        self.assertEqual(rows[2], "000/000  T:100  ", r.report())

    def status_pixels(self, order, font, tracks=1):
        """The OLED's pixels, as rows of `.` and `#`, with display-order
        @order on 128x64, once the head is @tracks half tracks in."""
        r = fr.run(
            "ffemu-apple2", ["wait 10000 log Name:"] + half_tracks(tracks)
            + ["sleep 500", "dump"],
            settings=["display = ssd1306-128x64"],
            flash=["display-type = oled-128x64", "display-order = " + order,
                   "oled-font = " + font],
            files=DSK)
        self.assertWaits(r, 1)
        return [line for line in r.lines if line and set(line) <= set(".#")]

    def test_spinner_6x13_geometry(self):
        """Each phase is a rectangle, together 9x9 at (2, 2) in 12x13."""
        frames = [self.status_pixels("0,1", "6x13", n) for n in range(1, 5)]
        left = 1 + 14*6
        bars = {
            0: (range(4, 9), range(9, 11)),
            1: (range(2, 4), range(4, 9)),
            2: (range(4, 9), range(2, 4)),
            3: (range(9, 11), range(4, 9)),
        }
        for frame, side in zip(frames, (1, 2, 3, 0)):
            actual = {(x-left, y-16) for y in range(16, 29)
                      for x in range(left, left + 12) if frame[y][x] == "#"}
            xs, ys = bars[side]
            self.assertEqual(actual, {(x, y) for x in xs for y in ys},
                             f"phase {side}")

    def check_spinner(self, font, x, w):
        """The spinner, @w pixels wide from @x, in the status row of double
        height is that of normal height, centered."""
        normal = self.status_pixels("0,1", font)
        tall = self.status_pixels("0,1d", font)
        spinner = [row[x:x+w] for row in normal[16:32]]
        self.assertIn("#", "".join(spinner))
        blank = ["." * w] * 8
        self.assertEqual(
            [row[x:x+w] for row in tall[16:48]], blank + spinner + blank)

    def test_spinner_double_height(self):
        self.check_spinner("6x13", 1 + 14*6, 12)

    def test_spinner_double_height_8x16(self):
        self.check_spinner("8x16", 13*8, 16)

    def test_error(self):
        """A mount error, on 16 columns right after the image's number,
        takes the spinner away, whose second cell would cover it."""
        r = fr.run(
            "ffemu-apple2", ["wait 10000 log Name:", "sleep 1000", "dump"],
            settings=["display = ssd1306-128x64"],
            flash=["display-type = oled-128x64", "oled-font = 8x16"],
            files={"BAD.HFE": b"HXCPICFE" + bytes(600)})
        self.assertWaits(r, 1)
        # Row 1 of double height, under rows 3 and 0 of the menus' layout:
        # cell 7 blank, the message from cell 8.
        tall = [line for line in r.lines
                if line and set(line) <= set(".#")][32:64]
        self.assertEqual({row[56:64] for row in tall}, {"." * 8}, r.report())
        self.assertIn("#", "".join(row[64:72] for row in tall), r.report())


if __name__ == "__main__":
    unittest.main()

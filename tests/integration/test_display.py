"""The LCD's and the OLED's rows as the firmware lays them out: the status
row's track, in the long form or in short with show-tracks-short, as the apple2
firmware always gives it, with the phases; display-order's row 4, the image's
name and its track in short; rows of double width; and the folders' names,
which row 3 scrolls and the image selector shows in brackets or after a slash;
a long name's scrolling by pixel; the Main Menu's font test; and the error
screen in its own layout.
An OLED's rows are read as FF OSD gets them, beside it on the I2C bus: the
display's text, as many columns as it has; or from its pixels, by the
firmware's glyphs."""

import os
import re
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


# The image selector on an image, as it waits there, with
# autoselect-file-secs = 0: it starts on a folder, which it sorts first, and
# waits there too, autoselect-folder-secs = 0.
SELECTOR = ["sleep 3000", "key right"]
SELECTOR_FLASH = ["autoselect-folder-secs = 0", "autoselect-file-secs = 0"]
SELECTOR_FILES = {"GAMES/A.IMG": bytes(737280), "B.IMG": bytes(737280)}

FONT_W = {"6x13": 6, "8x16": 8}


def oled_glyphs(font):
    """The firmware's glyphs of @font for ASCII, with which the fonts that the
    build generated from fonts/ begin, by their columns' bytes, the top
    page's and then the bottom page's, bit 0 a column's top pixel."""
    path = os.path.join(
        fr.OUT, "ffemu", "shugart", "gen", "oled_font_%s.c" % font)
    with open(path) as f:
        data = bytes(
            int(x, 16) for x in re.findall(r"0x([0-9a-f]{2})", f.read()))
    n = 2 * FONT_W[font]
    return {data[i*n:(i+1)*n]: chr(0x20 + i) for i in range(0x7f - 0x20)}


def oled_text(pixels, top, font, wide, tall=True):
    """The text of the OLED's row of double height, or of single height
    unless @tall, from pixel row @top of @pixels, rows of "." and "#", of
    double width too if @wide: each cell as the glyph that it shows, "@" for
    none, such as the phase spinner's."""
    glyphs, w = oled_glyphs(font), FONT_W[font]
    margin, sx = (1 if w == 6 else 0), (2 if wide else 1)
    sy = 2 if tall else 1
    text = ""
    for i in range((128 - margin) // w // sx):
        cols = []
        for page in (0, 8):
            for j in range(w):
                x = sx * (margin + i*w + j)
                cols.append(sum(
                    (pixels[top + sy * (page+b)][x] == "#") << b
                    for b in range(8)))
        text += glyphs.get(bytes(cols), "@")
    return text


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
    """The apple2 firmware's track in short: the phases on as their digits,
    dots for the others, after the OLED's phase spinner, which FF OSD gets as
    a blank, then `T` and the track in half tracks. A track of three digits
    takes the spinner's place where the row leaves no other."""

    def test_lcd_16_columns(self):
        rows, r = self.rows(
            "ffemu-apple2", half_tracks(1), "hd44780-16x2", files=DSK)
        self.assertEqual(rows[1], "000/000  .1..T01", r.report())

    def test_lcd_20_columns(self):
        rows, r = self.rows(
            "ffemu-apple2", half_tracks(1), "hd44780-20x4",
            flash=["display-type = lcd-20x04"], files=DSK)
        self.assertEqual(rows[2], "000/000      .1..T01", r.report())

    def test_oled(self):
        rows, r = self.rows(
            "ffemu-apple2", half_tracks(1), "ssd1306-128x32", files=DSK)
        self.assertEqual(rows[2], "000/000       .1..T01", r.report())

    def test_oled_8x16(self):
        rows, r = self.rows(
            "ffemu-apple2", half_tracks(1), "ssd1306-128x64",
            flash=["oled-font = 8x16"], files=DSK)
        self.assertEqual(rows[2], "000/000  .1..T01", r.report())

    def test_three_digits(self):
        rows, r = self.rows(
            "ffemu-apple2", half_tracks(100), "ssd1306-128x32", files=DSK)
        self.assertEqual(rows[2], "000/000      0...T100", r.report())

    def test_three_digits_8x16(self):
        rows, r = self.rows(
            "ffemu-apple2", half_tracks(100), "ssd1306-128x64",
            flash=["oled-font = 8x16"], files=DSK)
        self.assertEqual(rows[2], "000/000 0...T100", r.report())

    def status_pixels(self, order, font):
        """The OLED's pixels, as rows of `.` and `#`, with display-order
        @order on 128x64, once the head is a half track in."""
        r = fr.run(
            "ffemu-apple2", ["wait 10000 log Name:"] + half_tracks(1)
            + ["sleep 500", "dump"],
            settings=["display = ssd1306-128x64"],
            flash=["display-type = oled-128x64", "display-order = " + order,
                   "oled-font = " + font],
            files=DSK)
        self.assertWaits(r, 1)
        return [line for line in r.lines if line and set(line) <= set(".#")]

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
        self.check_spinner("6x13", 1 + 12*6, 12)

    def test_spinner_double_height_8x16(self):
        self.check_spinner("8x16", 7*8, 16)

    def test_error(self):
        """A mount error takes the whole status row, the phase spinner with
        it, as FF OSD gets the error screen's rows: the name, the message."""
        rows, r = self.rows(
            "ffemu-apple2", ["sleep 1000"], "ssd1306-128x64",
            flash=["display-type = oled-128x64", "oled-font = 8x16"],
            files={"BAD.HFE": b"HXCPICFE" + bytes(600)})
        self.assertTrue(rows[1].startswith("E31"), r.report())
        self.assertNotIn("000", rows[1], r.report())


class NameStatus(Case, unittest.TestCase):
    """Row 4 of display-order: the image's name, then its track in short at
    the row's right, a blank apart from the name, or the phase spinner on an
    OLED in Apple2 mode; on a row of 21 columns or more, 24 in Apple2 mode,
    the image's number before the name."""

    ORDER = ["display-order = 4,1", "osd-display-order = 4,1"]

    def test_lcd_16_columns(self):
        rows, r = self.rows(
            "ffemu", steps(3), "hd44780-16x2", flash=self.ORDER)
        self.assertEqual(rows[0], "A           0T03", r.report())

    def test_writing(self):
        rows, r = self.rows(
            "ffemu", ["key write"], "hd44780-16x2", flash=self.ORDER)
        self.assertEqual(rows[0], "A           0W00", r.report())

    def test_three_digits(self):
        rows, r = self.rows(
            "ffemu", steps(100), "hd44780-16x2", flash=self.ORDER)
        self.assertEqual(rows[0], "A          0T100", r.report())

    def test_oled(self):
        rows, r = self.rows(
            "ffemu", steps(3), "ssd1306-128x32", flash=self.ORDER)
        self.assertEqual(rows[0], "000/000 A        0T03", r.report())

    def test_lcd_40_columns(self):
        rows, r = self.rows(
            "ffemu", steps(3), "hd44780-40x2",
            flash=self.ORDER + ["display-type = lcd-40x02"])
        self.assertEqual(rows[0], "000/000 A" + 27 * " " + "0T03", r.report())

    def test_long_name_scrolls(self):
        """Within the columns that the track leaves it."""
        r = fr.run(
            "ffemu",
            ["wait 10000 log Name:",
             "wait 10000 display BCDEFGHIJKL 0T00"],
            settings=["display = hd44780-16x2"], flash=self.ORDER,
            files={"ABCDEFGHIJKLMNOPQRST.IMG": bytes(737280)})
        self.assertWaits(r, 2)

    def test_rows_scroll_apart(self):
        """The name scrolls in row 4, within the columns that the track
        leaves it, and not in row 0, which holds it whole."""
        r = fr.run(
            "ffemu", ["wait 10000 log Name:",
                      "wait 10000 display BCDEFGHIJKL 0T00", "sleep 50",
                      "dump"],
            settings=["display = hd44780-16x2"],
            flash=["display-order = 0,4"],
            files={"ABCDEFGHIJKLMNOP.IMG": bytes(737280)})
        self.assertWaits(r, 2)
        rows = [line[1:-1] for line in r.lines
                if line.startswith("|") and line.endswith("|")]
        self.assertEqual(rows[0], "ABCDEFGHIJKLMNOP", r.report())

    def test_three_digits_scroll(self):
        """A name that fits beside a track of two digits scrolls beside one
        of three."""
        r = fr.run(
            "ffemu", ["wait 10000 log Name:"] + steps(100)
            + ["wait 10000 display BCDEFGHIJK 0T100"],
            settings=["display = hd44780-16x2"], flash=self.ORDER,
            files={"ABCDEFGHIJK.IMG": bytes(737280)})
        self.assertWaits(r, 2)

    def selector_rows(self, display, flash=(), name="ABCDEFGHIJKLMNOP.IMG"):
        """The LCD's rows while the image selector waits on image @name."""
        r = fr.run(
            "ffemu", SELECTOR + ["sleep 500", "dump"],
            settings=["display = " + display],
            flash=self.ORDER + SELECTOR_FLASH + list(flash),
            files={"GAMES/A.IMG": bytes(737280), name: bytes(737280)})
        self.assertWaits(r, 0)
        return [line[1:-1] for line in r.lines
                if line.startswith("|") and line.endswith("|")], r

    def test_selector(self):
        """While no image is in, the row is the name's alone."""
        rows, r = self.selector_rows("hd44780-16x2")
        self.assertEqual(rows[0], "ABCDEFGHIJKLMNOP", r.report())

    def test_selector_40_columns(self):
        """Without the image's number either, which comes with the track."""
        rows, r = self.selector_rows(
            "hd44780-40x2", ["display-type = lcd-40x02"], "B.IMG")
        self.assertEqual(rows[0], "B" + 39 * " ", r.report())

    def test_apple2_lcd(self):
        rows, r = self.rows(
            "ffemu-apple2", half_tracks(1), "hd44780-16x2", flash=self.ORDER,
            files=DSK)
        self.assertEqual(rows[0], "TEST     .1..T01", r.report())

    def test_apple2_oled(self):
        """The spinner, which FF OSD gets as a blank, before the phases."""
        rows, r = self.rows(
            "ffemu-apple2", half_tracks(1), "ssd1306-128x32",
            flash=self.ORDER, files=DSK)
        self.assertEqual(rows[0], "TEST          .1..T01", r.report())

    def test_apple2_lcd_40_columns(self):
        rows, r = self.rows(
            "ffemu-apple2", half_tracks(1), "hd44780-40x2",
            flash=self.ORDER + ["display-type = lcd-40x02"], files=DSK)
        self.assertEqual(
            rows[0], "000/000 TEST" + 21 * " " + ".1..T01", r.report())


class DoubleWidth(Case, unittest.TestCase):
    """A row given as `w` in display-order, double height and double width on
    an OLED: the left half of the row, magnified. The status row has a layout
    of its own there, the track alone at the row's right, the mode of the long
    form at its left, and in Apple2 mode the phase spinner of one cell, which
    gives way to a track of three digits where the row has no room. Row 4
    keeps its layout, with the spinner of one cell and without the phases in
    Apple2 mode."""

    def pixels(self, name, commands, order, font, flash=(), files=PC,
               mounted=True):
        """The pixels of a 128x64 OLED, with display-order @order, once the
        image is in, if @mounted, and @commands have run."""
        wait = ["wait 10000 log Name:"] if mounted else []
        r = fr.run(
            name, wait + list(commands) + ["sleep 500", "dump"],
            settings=["display = ssd1306-128x64"],
            flash=["display-type = oled-128x64", "display-order = " + order,
                   "oled-font = " + font] + list(flash),
            files=files)
        self.assertWaits(r, len(wait))
        return [line for line in r.lines if line and set(line) <= set(".#")]

    def check(self, name, commands, order, expected, flash=(), files=PC,
              mounted=True):
        """The text of the double-width row that @order puts first, in each
        font, as @expected gives it."""
        for font, text in expected.items():
            with self.subTest(font=font):
                pixels = self.pixels(
                    name, commands, order, font, flash, files, mounted)
                self.assertEqual(oled_text(pixels, 0, font, True), text)

    def test_name(self):
        self.check(
            "ffemu", [], "0w,1", {"6x13": "ABCDEFGHIJ", "8x16": "ABCDEFGH"},
            files={"ABCDEFGHIJKL.IMG": bytes(737280)})

    def test_status(self):
        self.check(
            "ffemu", steps(3), "1w,0",
            {"6x13": "    T:03.0", "8x16": "  T:03.0"})

    def test_status_three_digits(self):
        self.check(
            "ffemu", steps(100), "1w,0",
            {"6x13": "   T:100.0", "8x16": " T:100.0"})

    def test_status_short(self):
        self.check(
            "ffemu", steps(100), "1w,0",
            {"6x13": "     0T100", "8x16": "   0T100"},
            flash=["show-tracks-short = yes"])

    def test_status_unmounted(self):
        """The image's type, and the depth at the row's right."""
        self.check(
            "ffemu", SELECTOR, "1w,0",
            {"6x13": "IMG    D:0", "8x16": "IMG  D:0"},
            flash=SELECTOR_FLASH, files=SELECTOR_FILES,
            mounted=False)

    def test_info_unmounted(self):
        """The drive's free and total space at the row's right."""
        self.check(
            "ffemu", SELECTOR, "2w,0",
            {"6x13": "  0.3/0.3G", "8x16": "0.3/0.3G"},
            flash=SELECTOR_FLASH, files=SELECTOR_FILES,
            mounted=False)

    def test_name_status_unmounted(self):
        """Row 4 is the name's alone while no image is in."""
        self.check(
            "ffemu", SELECTOR, "4w,0",
            {"6x13": "ABCDEFGH  ", "8x16": "ABCDEFGH"},
            flash=SELECTOR_FLASH,
            files={"GAMES/A.IMG": bytes(737280),
                   "ABCDEFGH.IMG": bytes(737280)},
            mounted=False)

    def test_info_mounted(self):
        """The image's encoding, then its sides, density and cylinders at the
        row's right."""
        self.check(
            "ffemu", [], "2w,0", {"6x13": "MFM   DD80", "8x16": "MFM DD80"})

    def test_name_status(self):
        self.check(
            "ffemu", steps(3), "4w,1",
            {"6x13": "A     0T03", "8x16": "A   0T03"})

    def test_name_status_long(self):
        """A name that the row cannot hold, at the start of its scrolling:
        no blank before the track."""
        self.check(
            "ffemu", steps(3), "4w,1",
            {"6x13": "ABCDEF0T03", "8x16": "ABCD0T03"},
            files={"ABCDEFGHIJKL.IMG": bytes(737280)})

    def test_apple2_status(self):
        """Phases 0 and 1 on: a half track in, at the first state."""
        self.check(
            "ffemu-apple2", ["key phase-in"], "1w,0",
            {"6x13": "  @01..T00", "8x16": "@01..T00"}, files=DSK)

    def test_apple2_status_three_digits(self):
        self.check(
            "ffemu-apple2", half_tracks(100), "1w,0",
            {"6x13": " @0...T100", "8x16": "0...T100"}, files=DSK)

    def test_apple2_name_status(self):
        self.check(
            "ffemu-apple2", half_tracks(100), "4w,1",
            {"6x13": "ABC  @T100", "8x16": "ABC@T100"},
            files={"ABC.DSK": DSK["TEST.DSK"]})

    def test_name_status_osd(self):
        """FF OSD gets row 4 laid out for the row's whole width."""
        rows, r = self.rows(
            "ffemu", steps(3), "ssd1306-128x64",
            flash=["display-type = oled-128x64", "display-order = 4w,1",
                   "osd-display-order = 4,1"])
        self.assertEqual(rows[0], "000/000 A        0T03", r.report())

    def test_name_scrolls_apart(self):
        """The name scrolls in the double-width row, whose columns cannot
        hold it, and not in the row of normal width that FF OSD gets: one
        step in, after the first pause."""
        r = fr.run(
            "ffemu", ["wait 10000 log Name:", "sleep 2100", "dump", "osd"],
            settings=["display = ssd1306-128x64"],
            flash=["display-type = oled-128x64", "display-order = 0w,1",
                   "osd-display-order = 0,1"],
            files={"ABCDEFGHIJKLMNOP.IMG": bytes(737280)})
        self.assertWaits(r, 1)
        pixels = [line for line in r.lines if line and set(line) <= set(".#")]
        self.assertEqual(
            oled_text(pixels, 0, "6x13", True), "BCDEFGHIJK", r.report())
        osd = [line[1:-1] for line in r.stdout.split("\nosd: ")[1].splitlines()
               if line.startswith("|") and line.endswith("|")]
        self.assertEqual(osd[0], "ABCDEFGHIJKLMNOP     ", r.report())

    def test_apple2_spinner(self):
        """The spinner of one cell is that of two cells in a row of normal
        height, centered."""
        normal = self.pixels(
            "ffemu-apple2", ["key phase-in"], "0,1", "6x13", files=DSK)
        wide = self.pixels(
            "ffemu-apple2", ["key phase-in"], "1w,0", "6x13", files=DSK)
        spinner = [row[73:85] for row in normal[16:32]]
        self.assertIn("#", "".join(spinner))
        blank = ["." * 12] * 8
        self.assertEqual(
            [row[26:38] for row in wide[0:32]], blank + spinner + blank)

    def test_osd(self):
        """FF OSD gets the status row of normal width."""
        rows, r = self.rows(
            "ffemu", steps(3), "ssd1306-128x64",
            flash=["display-type = oled-128x64", "display-order = 1w,0"])
        self.assertEqual(rows[2], "000/000   T:03.0     ", r.report())

    def test_lcd(self):
        """An LCD takes `w` as `d`: as a row of normal height."""
        rows, r = self.rows(
            "ffemu", steps(3), "hd44780-16x2",
            flash=["display-order = 1w,0"])
        self.assertEqual(rows, ["000/000  T:03.0 ", "A" + 15 * " "])

    def test_ff_cfg(self):
        """From FF.CFG, which the firmware puts in flash and resets: a reset
        ends a wait, so it is slept through. ffemu gives the rows by their
        letters."""
        files = dict(PC, **{"FF.CFG": "display-order = 1w,0\n"})
        r = fr.run(
            "ffemu", ["sleep 6000", "wait 10000 log Name:", "sleep 500",
                      "dump", "flash"],
            settings=["display = ssd1306-128x32"], files=files)
        self.assertWaits(r, 1)
        self.assertEqual(r.flash().get("display-order"), "Sw,N", r.report())
        pixels = [line for line in r.lines if line and set(line) <= set(".#")]
        self.assertEqual(oled_text(pixels, 0, "6x13", True), "    T:00.0")


class Letters(Case, unittest.TestCase):
    """display-order's rows by their initials, N, S, I, D, C, and B for the
    blank row: the same layouts as the digits, which stay taken, in either
    case, in osd-display-order too; ffemu writes the letters back."""

    def oled(self, order):
        """A 128x64 OLED's pixels with display-order @order, once the image
        is in."""
        r = fr.run(
            "ffemu", ["wait 10000 log Name:"] + steps(3)
            + ["sleep 500", "dump"],
            settings=["display = ssd1306-128x64"],
            flash=["display-type = oled-128x64", "display-order = " + order],
            files=PC)
        self.assertWaits(r, 1)
        return [line for line in r.lines if line and set(line) <= set(".#")]

    def test_oled(self):
        self.assertEqual(self.oled("D,Nw,S"), self.oled("3,0w,1"))

    def test_lower_case(self):
        self.assertEqual(self.oled("d,nw,s"), self.oled("3,0w,1"))

    def test_lcd(self):
        """Combined, blank, status, info on a 20x4 LCD."""
        flash = ["display-type = lcd-20x04"]
        letters, r = self.rows("ffemu", steps(3), "hd44780-20x4",
                               flash + ["display-order = C,B,S,I"])
        digits, _ = self.rows("ffemu", steps(3), "hd44780-20x4",
                              flash + ["display-order = 4,7,1,2"])
        self.assertEqual(letters, digits, r.report())
        self.assertEqual(letters[1], " " * 20, r.report())

    def test_osd(self):
        letters, r = self.rows("ffemu", steps(3), "hd44780-16x2",
                               ["osd-display-order = S,N"])
        digits, _ = self.rows("ffemu", steps(3), "hd44780-16x2",
                              ["osd-display-order = 1,0"])
        self.assertEqual(letters, digits, r.report())

    def test_ff_cfg(self):
        """Through the firmware's own parser, from FF.CFG, in either case:
        the flash that it writes, as ffemu gives it."""
        files = dict(PC, **{"FF.CFG": "display-order = b,Sw,n\n"})
        r = fr.run("ffemu", ["sleep 6000", "wait 10000 log Name:", "flash"],
                   files=files)
        self.assertWaits(r, 1)
        self.assertEqual(r.flash().get("display-order"), "B,Sw,N",
                         r.report())

    def test_written_back(self):
        """Letters in flash come back as letters, digits as letters too."""
        for order, back in (("D,Nd,S", "D,Nd,S"), ("c,b,s", "C,B,S"),
                            ("4w,1", "Cw,S")):
            with self.subTest(order=order):
                r = fr.run("ffemu", ["flash"],
                           flash=["display-order = " + order])
                self.assertEqual(r.flash().get("display-order"), back,
                                 r.report())


class FolderScroll(fr.Case, unittest.TestCase):
    """The folder's name scrolls in row 3, as the image's in row 0, where the
    row cannot hold it whole; in a double-width row within the columns that
    show, while FF OSD gets it whole. The image selector starts on the
    folder, the root's only entry, and enters it after
    autoselect-folder-secs, then mounts its image."""

    def test_lcd(self):
        """Once the image is in: the selector's name, the folder's in
        brackets, scrolls too."""
        r = fr.run(
            "ffemu", ["wait 15000 log Name:",
                      "wait 10000 display BCDEFGHIJKLMNOPQRSTU"],
            settings=["display = hd44780-20x4"],
            flash=["display-type = lcd-20x04"],
            files={"ABCDEFGHIJKLMNOPQRSTUVWXY/A.IMG": bytes(737280)})
        self.assertWaits(r, 2)

    def test_double_width(self):
        """Seen every 200 ms for 5 s, through the pauses at its ends."""
        r = fr.run(
            "ffemu", ["wait 15000 log Name:"]
            + ["sleep 200", "dump", "osd"] * 25,
            settings=["display = ssd1306-128x64"],
            flash=["display-type = oled-128x64", "display-order = 3w,0",
                   "osd-display-order = 3,0"],
            files={"ABCDEFGHIJKL/A.IMG": bytes(737280)})
        self.assertWaits(r, 1)
        pixels = [line for line in r.lines if line and set(line) <= set(".#")]
        wide = {oled_text(pixels[i:i+64], 0, "6x13", True)
                for i in range(0, len(pixels), 64)}
        self.assertIn("ABCDEFGHIJ", wide, r.report())
        self.assertIn("CDEFGHIJKL", wide, r.report())
        osd = {[line for line in part.splitlines() if line.startswith("|")][0]
               for part in r.stdout.split("\nosd: ")[1:]}
        self.assertEqual(osd, {"|ABCDEFGHIJKL         |"}, r.report())


class DirSlash(fr.Case, unittest.TestCase):
    """The image selector names a folder in brackets, or with a leading slash
    with show-dir-slash = yes, a subfolder's ".." too. It starts on the folder,
    sorted before the images, and waits there for a button, as
    autoselect-folder-secs = 0 asks; Select enters it, at its image, and Left
    goes on to its ".."."""

    FILES = {"GAMES/A.IMG": bytes(737280), "B.IMG": bytes(737280)}

    def check(self, names, flash=(), ff_cfg=""):
        files = dict(self.FILES)
        files["FF.CFG"] = "autoselect-folder-secs = 0\n" + ff_cfg
        # The firmware writes FF.CFG's options to flash and resets, which
        # ends a wait: slept.
        r = fr.run(
            "ffemu", ["sleep 6000", "wait 10000 display " + names[0],
                      "key select", "sleep 500", "key left",
                      "wait 10000 display " + names[1]],
            settings=["display = hd44780-16x2"], flash=flash, files=files)
        self.assertWaits(r, 2)

    def test_brackets(self):
        self.check(["[GAMES]", "[..]"])

    def test_slash(self):
        self.check(["/GAMES", "/.."], ff_cfg="show-dir-slash = yes\n")

    def test_slash_from_flash(self):
        self.check(["/GAMES", "/.."], flash=["show-dir-slash = yes"])


def glyph_columns(font, text):
    """The pixel columns of @text in @font, each a tuple of its 16 pixels, top
    down, True where lit."""
    glyphs = {c: g for g, c in oled_glyphs(font).items()}
    w = FONT_W[font]
    cols = []
    for ch in text:
        g = glyphs[ch]
        for j in range(w):
            cols.append(tuple(
                bool((g[j] if b < 8 else g[w + j]) >> (b % 8) & 1)
                for b in range(16)))
    return cols


def row_columns(pixels, top, x0, x1, large=False, wide=False):
    """The pixel columns @x0 to @x1 - 1, at the font's own scale, of the
    OLED's text row from pixel row @top of @pixels, rows of "." and "#": of
    double height if @large, of double width too if @wide."""
    sy, sx = (2 if large else 1), (2 if wide else 1)
    return [tuple(pixels[top + sy*b][sx*x] == "#" for b in range(16))
            for x in range(x0, x1)]


class PixelScroll(fr.Case, unittest.TestCase):
    """With display-scroll-by-pixel = yes, an OLED scrolls a long name a pixel
    column at a time, at the speed that display-scroll-rate, per character,
    gives: in a double-width row a column of the font, two of the display;
    in row 4 only the name, beside its track. An LCD goes on by character.
    The OLED is seen every 25 ms from 1.8 s after the image is in, at
    display-scroll-rate = 300: 50 ms per pixel column of the 6x13 font after
    the pause of 2 s; the names are two characters too long."""

    def frames(self, order, font, name,
               flash=("display-scroll-by-pixel = yes",), nr=70):
        """@nr frames of a 128x64 OLED's pixels, with display-order @order and
        the image @name."""
        r = fr.run(
            "ffemu", ["wait 10000 log Name:", "sleep 1800"]
            + ["sleep 25", "dump"] * nr,
            settings=["display = ssd1306-128x64"],
            flash=["display-type = oled-128x64", "display-order = " + order,
                   "oled-font = " + font, "display-scroll-rate = 300"]
            + list(flash),
            files={name + ".IMG": bytes(737280)})
        self.assertWaits(r, 1)
        pixels = [line for line in r.lines if line and set(line) <= set(".#")]
        self.assertEqual(len(pixels), 64 * nr, r.report())
        return [pixels[i:i+64] for i in range(0, len(pixels), 64)], r

    def shifts(self, frames, stream, x0, n, large=False, wide=False):
        """The name's shift in each frame: the pixel columns of @stream that
        the top row's @n columns from @x0 show, None where none fit."""
        out = []
        for f in frames:
            cols = row_columns(f, 0, x0, x0 + n, large, wide)
            out.append(next((s for s in range(len(stream) - n + 1)
                             if stream[s:s+n] == cols), None))
        return out

    def check_by_pixel(self, shifts, end, r):
        """That @shifts go up from 0 to @end, and rest there, a column at a
        time: two between two looks at most, the OLED showing a new frame
        every 25 ms or so, which may miss a step."""
        self.assertNotIn(None, shifts, r.report())
        self.assertEqual(shifts[0], 0, shifts)
        self.assertEqual(shifts[-1], end, shifts)
        self.assertLessEqual(
            {b - a for a, b in zip(shifts, shifts[1:])}, {0, 1, 2}, shifts)

    def check_speed(self, shifts, w):
        """That a character of @w pixel columns goes by in the 300 ms that
        display-scroll-rate gives."""
        self.assertAlmostEqual(
            (shifts.index(2 * w) - shifts.index(w)) * 25, 300, delta=25)

    def test_6x13(self):
        name = "ABCDEFGHIJKLMNOPQRSTUVW"
        frames, r = self.frames("0,1", "6x13", name)
        shifts = self.shifts(frames, glyph_columns("6x13", name), 1, 126)
        self.check_by_pixel(shifts, 12, r)
        self.check_speed(shifts, 6)

    def test_8x16(self):
        name = "ABCDEFGHIJKLMNOPQR"
        frames, r = self.frames("0,1", "8x16", name)
        shifts = self.shifts(frames, glyph_columns("8x16", name), 0, 128)
        self.check_by_pixel(shifts, 16, r)
        self.check_speed(shifts, 8)

    def test_by_character(self):
        """By default, a character at a time, at the same speed."""
        name = "ABCDEFGHIJKLMNOPQRSTUVW"
        frames, r = self.frames("0,1", "6x13", name, flash=())
        shifts = self.shifts(frames, glyph_columns("6x13", name), 1, 126)
        self.assertNotIn(None, shifts, r.report())
        self.assertEqual(sorted(set(shifts)), [0, 6, 12], shifts)
        self.check_speed(shifts, 6)

    def test_double_width(self):
        """A column of the font at a time, in the 10 columns that show."""
        name = "ABCDEFGHIJKL"
        frames, r = self.frames("0w,1", "6x13", name)
        shifts = self.shifts(
            frames, glyph_columns("6x13", name), 1, 60, True, True)
        self.check_by_pixel(shifts, 12, r)

    def test_name_status(self):
        """In row 4 the name scrolls within its 8 columns, after the image's
        number; the track stays."""
        name = "ABCDEFGHIJ"
        frames, r = self.frames("4,1", "6x13", name)
        shifts = self.shifts(frames, glyph_columns("6x13", name), 49, 48)
        self.check_by_pixel(shifts, 12, r)
        tracks = {tuple(row_columns(f, 0, 97, 127)) for f in frames}
        self.assertEqual(
            tracks, {tuple(glyph_columns("6x13", " 0T00"))}, r.report())

    def test_endless(self):
        """With display-scroll-pause = 0, on through the gap after the name
        and round to its start."""
        name = "ABCDEFGHIJKLMNOPQRSTUVW"
        frames, r = self.frames(
            "0,1", "6x13", name, nr=100,
            flash=("display-scroll-by-pixel = yes",
                   "display-scroll-pause = 0"))
        shifts = self.shifts(
            frames, glyph_columns("6x13", name + "    " + name), 1, 126)
        self.assertNotIn(None, shifts, r.report())
        period = 6 * (len(name) + 4)
        steps = {(b - a) % period for a, b in zip(shifts, shifts[1:])}
        self.assertEqual(steps, {0, 1}, shifts)

    def test_lcd(self):
        """An LCD scrolls by character as before."""
        r = fr.run(
            "ffemu", ["wait 10000 log Name:",
                      "wait 5000 display BCDEFGHIJKLMNOPQ"],
            settings=["display = hd44780-16x2"],
            flash=["display-type = lcd-16x02",
                   "display-scroll-by-pixel = yes"],
            files={"ABCDEFGHIJKLMNOPQRSTUVW.IMG": bytes(737280)})
        self.assertWaits(r, 2)


def rom_cells(codes, rom="cyr"):
    """Codes @codes as ffemu's LCD of character ROM @rom shows them in a
    dump: the character that each code's glyph stands for, from ffemu's
    table, and a white square for one without, the user-defined codes below
    the ROM's first among them."""
    path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..",
                        "..", "ffemu", "lcd_font_%s.h" % rom)
    with open(path) as f:
        text = f.read()
    uni = {}
    for m in re.finditer(r"/\* 0x([0-9a-f]{2}) \*/((?:\s*0x[0-9a-f]{4},?)+)",
                         text):
        for i, u in enumerate(re.findall(r"0x([0-9a-f]{4})", m.group(2))):
            uni[int(m.group(1), 16) + i] = int(u, 16)
    return "".join(chr(uni.get(c, 0) or 0x25a1) for c in codes)


# The Main Menu, entered with no drive mounted, and its item Font Test, two
# steps back from the first, past Exit.
FONT_TEST = ["key remove", "sleep 500", "key select", "sleep 500",
             "key left", "sleep 200", "key left", "sleep 200", "key select",
             "sleep 500"]


class FontTest(Case, unittest.TestCase):
    """The Main Menu's Font Test: every code of the display with its glyph,
    16 to a row and their codes on the row below, a page of one or two such
    pairs; Left and Right page, Select goes back to the menu."""

    LCD20 = ["display-type = lcd-20x04"]
    OLED64 = ["display-type = oled-128x64"]

    def oled(self, name, keys, flash, files=PC, display="ssd1306-128x64"):
        """The OLED's pixel rows on the font test's page that @keys show."""
        r = fr.run(name, ["wait 10000 log Name:"] + FONT_TEST + list(keys)
                   + ["sleep 300", "dump"], settings=["display = " + display],
                   flash=flash, files=files)
        self.assertWaits(r, 1)
        return [line for line in r.lines if line and set(line) <= set(".#")]

    def test_lcd_16x2(self):
        """One pair a page, the codes from 0x00, to the ROM as they are:
        CGRAM's eight, then MELT's capital soft sign and the rest; the keys
        in short; FF OSD beside it with the same rows."""
        rows, r = self.rows("ffemu", FONT_TEST, "hd44780-16x2")
        self.assertEqual(rows, [rom_cells(range(0x00, 0x10)),
                                "00-0F  Sel=exit "], r.report())
        osd = r.stdout.split("\nosd: ")[1].splitlines()
        self.assertEqual(osd[2], "|00-0F  Sel=exit |", r.report())

    def test_paging(self):
        """Right to the next 16 codes, Left from the first to the last 16,
        Select back to the menu."""
        rows, r = self.rows("ffemu", FONT_TEST + ["key right"],
                            "hd44780-16x2")
        self.assertEqual(rows, [rom_cells(range(0x10, 0x20)),
                                "10-1F  Sel=exit "], r.report())
        rows, r = self.rows("ffemu", FONT_TEST + ["key left"],
                            "hd44780-16x2")
        self.assertEqual(rows, [rom_cells(range(0xf0, 0x100)),
                                "F0-FF  Sel=exit "], r.report())
        rows, r = self.rows("ffemu", FONT_TEST + ["key select"],
                            "hd44780-16x2")
        self.assertEqual(rows, ["**Main Menu**   ", "Font Test       "],
                         r.report())

    def test_lcd_20x4(self):
        """Two pairs a page, in the rows' own order, the keys in full."""
        rows, r = self.rows("ffemu", FONT_TEST + ["key right"],
                            "hd44780-20x4", self.LCD20)
        self.assertEqual(rows, [rom_cells(range(0x20, 0x30)) + "    ",
                                "20-2F  Select: exit ",
                                rom_cells(range(0x30, 0x40)) + "    ",
                                "30-3F" + " " * 15], r.report())

    def test_oled_128x64(self):
        """Four rows of single height, read by the firmware's glyphs, in each
        font: ASCII's at 0x40, the keys short where 16 columns are all."""
        for font, keys in (("6x13", "  Select: exit"), ("8x16", "  Sel=exit")):
            with self.subTest(font=font):
                pixels = self.oled("ffemu", ["key right", "sleep 200",
                                             "key right"],
                                   self.OLED64 + ["oled-font = " + font])
                width = len(oled_text(pixels, 0, font, False, False))
                self.assertEqual(
                    [oled_text(pixels, top, font, False, False)
                     for top in (0, 16, 32, 48)],
                    [row.ljust(width) for row in (
                        "@ABCDEFGHIJKLMNO", "40-4F" + keys,
                        "PQRSTUVWXYZ[\\]^_", "50-5F")])

    def test_oled_codes_without_glyph(self):
        """The control codes draw alike, as the font's mark of a code without
        a glyph, 0x10 and on too, the phase spinner's in the apple2
        firmware, which the font test draws as a glyph, on a 128x32 OLED of
        one pair a page."""
        r = fr.run("ffemu-apple2", ["wait 10000 log Name:"] + FONT_TEST
                   + ["dump", "key right", "sleep 300", "dump"],
                   settings=["display = ssd1306-128x32"], files=DSK)
        self.assertWaits(r, 1)
        lines = [line for line in r.lines if line and set(line) <= set(".#")]
        pages = (lines[:32], lines[32:64])
        cells = [tuple(row[1 + 6*i:7 + 6*i] for row in page[:16])
                 for page in pages for i in range(16)]
        self.assertEqual(set(cells), {cells[0]}, r.report())
        self.assertTrue(any("#" in row for row in cells[0]), r.report())
        self.assertEqual(oled_text(pages[1], 16, "6x13", False, False),
                         "10-1F  Select: exit  ")


class Errors(fr.Case, unittest.TestCase):
    """An error's code, `E` for FlashFloppy's own and `F` for FatFS's, on an
    error screen of its own, whatever display-order says: a mount error with the image's name above the message, both rows
    double height on a 128x64 OLED and single height on an LCD, and on a
    128x32 OLED the name after the message in one double row; the end of
    the drive's session, here a drive without an image, with the message
    alone in the status row, double height on an OLED. With
    display-error-wide the double rows are double width too, the message
    scrolling from its code. The message is the code and whatever follows
    it, so that these hold with the code's meaning after it and without."""

    BAD = {"BAD.HFE": b"HXCPICFE" + bytes(600)}
    NONE = {"README.TXT": b"No image here.\n"}

    def run_error(self, files, display, flash=(), wait="log **Error",
                  after=()):
        """The run, once the error is shown and @after has run."""
        r = fr.run(
            "ffemu", ["wait 10000 " + wait, "sleep 500", "dump", "osd"]
            + list(after),
            settings=["display = " + display], flash=flash, files=files)
        self.assertWaits(r, 1)
        return r

    def lcd(self, files, display, flash=(), wait="log **Error"):
        """The LCD's rows once the error is shown."""
        r = self.run_error(files, display, flash, wait)
        rows = [line[1:-1] for line in r.stdout.split("\nosd: ")[0]
                .splitlines() if line.startswith("|") and line.endswith("|")]
        return rows, r

    def oled(self, files, height, flash=(), wait="log **Error"):
        """The OLED's pixel rows once the error is shown."""
        r = self.run_error(
            files, "ssd1306-128x%u" % height,
            ["display-type = oled-128x%u" % height] + list(flash), wait)
        return [line for line in r.lines
                if line and set(line) <= set(".#")], r

    def test_mount_error(self):
        rows, r = self.lcd(self.BAD, "hd44780-16x2", wait="log Name:")
        self.assertEqual(rows[0], "BAD".ljust(16), r.report())
        self.assertTrue(rows[1].startswith("E31"), r.report())

    def test_mount_error_20x4(self):
        """The other rows blank."""
        rows, r = self.lcd(self.BAD, "hd44780-20x4",
                           flash=["display-type = lcd-20x04"],
                           wait="log Name:")
        self.assertEqual(rows[0], "BAD".ljust(20), r.report())
        self.assertTrue(rows[1].startswith("E31"), r.report())
        self.assertEqual(rows[2:], [" " * 20] * 2, r.report())

    def test_mount_error_oled64(self):
        """Both rows double height."""
        pixels, r = self.oled(self.BAD, 64, wait="log Name:")
        self.assertEqual(oled_text(pixels, 0, "6x13", False),
                         "BAD".ljust(21), r.report())
        self.assertTrue(oled_text(pixels, 32, "6x13", False).startswith("E31"),
                        r.report())

    def frames(self, files, flash, wide, wait="log Name:"):
        """The texts of a 128x32 OLED's double row, seen every 250 ms for 7 s
        once the error is shown: at rest first."""
        r = fr.run(
            "ffemu", ["wait 10000 " + wait]
            + ["sleep 250", "dump", "osd"] * 28,
            settings=["display = ssd1306-128x32"],
            flash=["display-type = oled-128x32"] + list(flash), files=files)
        self.assertWaits(r, 1)
        pixels = [line for line in r.lines if line and set(line) <= set(".#")]
        return [oled_text(pixels[i:i+32], 0, "6x13", wide)
                for i in range(0, len(pixels), 32)], r

    def test_mount_error_oled32(self):
        """The message and the name after it in one double row."""
        pixels, r = self.oled(self.BAD, 32, wait="log Name:")
        text = oled_text(pixels, 0, "6x13", False)
        self.assertTrue(text.startswith("E31"), r.report())
        self.assertTrue(text.rstrip().endswith(": BAD"), r.report())

    def test_mount_error_oled32_scrolls(self):
        """A long name scrolls into view after the message, which the row
        rests on first, until its end is at the row's end."""
        texts, r = self.frames(
            {"AVERYLONGIMAGENAME.HFE": b"HXCPICFE" + bytes(600)}, (), False)
        self.assertTrue(texts[0].startswith("E31"), r.report())
        self.assertNotIn("NAME", texts[0], r.report())
        self.assertTrue(any(t.endswith(": AVERYLONGIMAGENAME"[-21:])
                            for t in texts), r.report())

    def test_mount_error_oled32_wide(self):
        """Double width with display-error-wide, the name in view or
        scrolling by."""
        texts, r = self.frames(self.BAD, ["display-error-wide = yes"], True)
        self.assertTrue(texts[0].startswith("E31"), r.report())
        self.assertTrue(any("BAD" in t for t in texts), r.report())

    def test_session_end(self):
        """In the status row, the first blank; the log line says what the
        error means too."""
        rows, r = self.lcd(self.NONE, "hd44780-16x2",
                           wait="log **Error 34")
        self.assertEqual(rows[0], " " * 16, r.report())
        self.assertTrue(rows[1].startswith("E34"), r.report())

    def test_session_end_20x4(self):
        rows, r = self.lcd(self.NONE, "hd44780-20x4",
                           flash=["display-type = lcd-20x04"])
        self.assertEqual(rows[0], " " * 20, r.report())
        self.assertTrue(rows[1].startswith("E34"), r.report())
        self.assertEqual(rows[2:], [" " * 20] * 2, r.report())

    def test_session_end_oled32(self):
        """Double height over the whole panel."""
        pixels, r = self.oled(self.NONE, 32)
        self.assertTrue(oled_text(pixels, 0, "6x13", False).startswith("E34"),
                        r.report())

    def test_session_end_oled64(self):
        """Double height where the mount error has it, below a blank row."""
        pixels, r = self.oled(self.NONE, 64)
        self.assertEqual(oled_text(pixels, 0, "6x13", False), " " * 21,
                         r.report())
        self.assertTrue(oled_text(pixels, 32, "6x13", False).startswith("E34"),
                        r.report())

    def test_display_order_ignored(self):
        """The user's display-order, the folder's row alone here, does not
        change the error screen, and comes back with the next button."""
        r = self.run_error(
            {"DIR/BAD.HFE": b"HXCPICFE" + bytes(600)}, "hd44780-16x2",
            ["display-order = 3"], wait="log Name:",
            after=["key right", "sleep 500", "dump"])
        rows = [line[1:-1] for line in r.stdout.splitlines()
                if line.startswith("|") and line.endswith("|")]
        self.assertEqual(rows[0], "BAD".ljust(16), r.report())
        self.assertTrue(rows[1].startswith("E31"), r.report())
        self.assertEqual(rows[-2:], ["DIR             ", " " * 16],
                         r.report())

    def test_display_order_w_ignored(self):
        """A `w` row of display-order, here the only one, neither."""
        pixels, r = self.oled(self.BAD, 64, ["display-order = 4w"],
                              wait="log Name:")
        self.assertTrue(oled_text(pixels, 32, "6x13", False).startswith("E31"),
                        r.report())

    def test_wide(self):
        """With display-error-wide the message scrolls, its code first; FF
        OSD gets the status row whole. Seen every 250 ms for 5 s."""
        r = fr.run(
            "ffemu", ["wait 10000 log **Error"]
            + ["sleep 250", "dump", "osd"] * 20,
            settings=["display = ssd1306-128x32"],
            flash=["display-type = oled-128x32", "display-error-wide = yes"],
            files=self.NONE)
        self.assertWaits(r, 1)
        pixels = [line for line in r.lines if line and set(line) <= set(".#")]
        wide = {oled_text(pixels[i:i+32], 0, "6x13", True)
                for i in range(0, len(pixels), 32)}
        self.assertTrue(any(t.startswith("E34") for t in wide), r.report())
        osd = {line for part in r.stdout.split("\nosd: ")[1:]
               for line in part.splitlines() if "E34" in line}
        self.assertEqual(len(osd), 1, r.report())
        self.assertTrue(osd.pop().startswith("|E34"), r.report())

    def test_wide_mount_error_oled64(self):
        """Both rows double width on a 128x64."""
        pixels, r = self.oled(self.BAD, 64, ["display-error-wide = yes"],
                              wait="log Name:")
        self.assertEqual(oled_text(pixels, 0, "6x13", True), "BAD".ljust(10),
                         r.report())
        self.assertTrue(oled_text(pixels, 32, "6x13", True).startswith("E31"),
                        r.report())


if __name__ == "__main__":
    unittest.main()

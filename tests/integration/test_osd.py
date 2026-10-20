"""FF OSD on the I2C bus beside an OLED or an LCD: the firmware finds it at
boot and sends it the display's text rows, in the order of osd-display-order,
as many columns as the display has; FF OSD shows three rows at most. It is not
fitted with osd = no, nor with a 7-segment display, which has no I2C bus."""

import unittest

import ffemu_run as fr

PC = {"A.IMG": bytes(737280)}


class Osd(fr.Case, unittest.TestCase):

    def osd(self, display, settings=(), flash=()):
        """The header and the rows of the script's "osd" once the image is
        in."""
        r = fr.run(
            "ffemu", ["wait 10000 log Name: 'A'", "sleep 1000", "osd", "log"],
            settings=["display = " + display] + list(settings), flash=flash,
            files=PC)
        self.assertWaits(r, 1)
        header = [line for line in r.lines if line.startswith("osd: ")]
        rows = [
            line[1:-1].rstrip() for line in r.lines
            if line.startswith("|") and line.endswith("|")]
        self.assertEqual(len(header), 1, r.report())
        return header[0], rows, r

    def test_lcd(self):
        """The folder, the image's name and the status, as many columns as the
        LCD's."""
        header, rows, r = self.osd("hd44780-16x2")
        self.assertTrue(header.startswith("osd: 16x3 on "), r.report())
        self.assertEqual(rows, ["/", "A", "000/000  T:00.0"], r.report())
        self.assertIn("I2C: FF OSD found", r.stdout)

    def test_oled(self):
        header, rows, r = self.osd("ssd1306-128x32")
        self.assertTrue(header.startswith("osd: 21x3 on "), r.report())
        self.assertEqual(rows, ["/", "A", "000/000   T:00.0"], r.report())

    def test_display_order(self):
        header, rows, r = self.osd(
            "hd44780-16x2", flash=["osd-display-order = 1,0"])
        self.assertEqual(rows, ["000/000  T:00.0", "A"], r.report())

    def test_four_rows(self):
        """Of an osd-display-order of four rows, the first three, which is all
        that FF OSD takes."""
        header, rows, r = self.osd(
            "hd44780-16x2", flash=["osd-display-order = 3,0,1,2"])
        self.assertTrue(header.startswith("osd: 16x3 on "), r.report())
        self.assertEqual(rows, ["/", "A", "000/000  T:00.0"], r.report())

    def test_unfitted(self):
        header, rows, r = self.osd("hd44780-16x2", settings=["osd = no"])
        self.assertEqual(header, "osd: none", r.report())
        self.assertNotIn("FF OSD found", r.stdout)

    def test_led(self):
        header, rows, r = self.osd("tm1651-3digit")
        self.assertEqual(header, "osd: none", r.report())


if __name__ == "__main__":
    unittest.main()

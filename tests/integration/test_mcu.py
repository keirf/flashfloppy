"""The AT32F435's display driver, which ffemu runs in place of the board's own
with the setting mcu = 435, through a model of that MCU's I2C: on each kind of
display that the drivers serve it draws what the board's own draws, and it
tells an SH1106 from an SSD1306 as that does."""

import re
import unittest

import ffemu_run as fr

PC = {"PC.IMG": bytes(737280)}
DUMP_RE = re.compile(r"^([.#]+|[|].*[|])$")


class DisplayDriver(fr.Case, unittest.TestCase):

    def dump(self, display, display_type, controller, mcu):
        """The display's dump with the display driver of @mcu, once the image
        is in."""
        r = fr.run("ffemu", [
            "wait 10000 log %s" % controller, "wait 10000 log Name: 'PC'",
            "sleep 1000", "dump"],
            settings=["display = " + display, "mcu = " + mcu],
            flash=["display-type = " + display_type], files=PC)
        self.assertWaits(r, 2)
        rows = [line for line in r.lines if DUMP_RE.match(line)]
        self.assertTrue(rows, r.report())
        return rows

    def check(self, display, display_type, controller):
        own = self.dump(display, display_type, controller, "415")
        at435 = self.dump(display, display_type, controller, "435")
        self.assertEqual(own, at435)

    def test_ssd1306_64(self):
        self.check("ssd1306-128x64", "oled-128x64", "OLED: SSD1306")

    def test_sh1106_32(self):
        self.check("sh1106-128x32", "oled-128x32", "OLED: SH1106")

    def test_lcd_20x4(self):
        self.check("hd44780-20x4", "lcd-20x04", "I2C: LCD found at 0x27")


if __name__ == "__main__":
    unittest.main()

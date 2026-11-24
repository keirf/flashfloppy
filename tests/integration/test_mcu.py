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

    def dump(self, display, display_type, controller, mcu, font=None):
        """The display's dump with the display driver of @mcu, once the image
        is in."""
        flash = ["display-type = " + display_type]
        if font:
            flash.append("oled-font = " + font)
        r = fr.run("ffemu", [
            "wait 10000 log %s" % controller, "wait 10000 log Name: 'PC'",
            "sleep 1000", "dump"],
            settings=["display = " + display, "mcu = " + mcu],
            flash=flash, files=PC)
        self.assertWaits(r, 2)
        rows = [line for line in r.lines if DUMP_RE.match(line)]
        self.assertTrue(rows, r.report())
        return rows

    def check(self, display, display_type, controller, font=None):
        own = self.dump(display, display_type, controller, "415", font)
        at435 = self.dump(display, display_type, controller, "435", font)
        self.assertEqual(own, at435)

    def test_ssd1306_64(self):
        self.check("ssd1306-128x64", "oled-128x64", "OLED: SSD1306")

    def test_sh1106_32(self):
        self.check("sh1106-128x32", "oled-128x32", "OLED: SH1106")

    def test_lcd_20x4(self):
        self.check("hd44780-20x4", "lcd-20x04", "I2C: LCD found at 0x27")

    def test_small_fonts(self):
        """Both drivers draw every small-font layout identically."""
        for font in ("7x8", "8x8"):
            for model, size in (("ssd1306", 32), ("ssd1306", 64),
                                ("sh1106", 32), ("sh1106", 64)):
                with self.subTest(font=font, model=model, size=size):
                    display = "%s-128x%d" % (model, size)
                    self.check(display, "oled-128x%d" % size,
                               "OLED: " + model.upper(), font)


if __name__ == "__main__":
    unittest.main()

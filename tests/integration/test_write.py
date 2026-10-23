"""The host's write signal, WGATE or the Apple2 write request: the firmware's
write mark on each kind of display while the signal is active, gone again once
it is off. ffemu's host sends no data with it, so such a write brings nothing
to the image."""

import unittest

import ffemu_run as fr

PC = {"PC.IMG": bytes(737280)}
DSK = {"TEST.DSK": bytes(35 * 4096)}


class WriteMarks(fr.Case, unittest.TestCase):

    def check(self, name, display, files, marked, unmarked):
        kind = "segments" if display.startswith("tm1651") else "display"
        # The firmware takes the signal's edge only once the image is in: the
        # drive shown first.
        r = fr.run(name, [
            "wait 10000 %s %s" % (kind, unmarked),
            "key write",
            "wait 3000 %s %s" % (kind, marked),
            "key write",
            "wait 3000 %s %s" % (kind, unmarked)],
            settings=["display = " + display], files=files)
        self.assertWaits(r, 3)

    def test_step_dir_lcd(self):
        """W before the track."""
        self.check(
            "ffemu", "hd44780-16x2", PC, "000/000W T:", "000/000  T:")

    def test_step_dir_led(self):
        """The side's letter after the track, k for side 0, becomes v."""
        self.check("ffemu", "tm1651-3digit", PC, "00 3f 09", "00 3f 08")

    def test_apple2_lcd(self):
        """`W` in place of the `T` before the track."""
        self.check("ffemu-apple2", "hd44780-16x2", DSK, "W00", "T00")

    def test_apple2_led(self):
        """The top segment of the phase digit lights."""
        self.check(
            "ffemu-apple2", "tm1651-3digit", DSK, "01 3f 3f", "00 3f 3f")


if __name__ == "__main__":
    unittest.main()

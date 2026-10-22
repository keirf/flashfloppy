"""An Apple2 host stepping the head: the apple2 firmware shows the head's
position in half tracks, on an LED and on an LCD."""

import unittest

import ffemu_run as fr

DSK = {"TEST.DSK": bytes(35 * 4096)}


class Steps(fr.Case, unittest.TestCase):
    """Phase 0 is on at rest; each "phase-in" moves to the next state of a
    seek: phases 0 and 1, phase 1 (a half track in), phases 1 and 2, phase 2
    (a track in)."""

    def check_steps(self, display, steps):
        kind = "segments" if display.startswith("tm1651") else "display"
        cmds = ["wait 10000 log Name: 'TEST'"]
        for nr, shown in steps:
            cmds += ["key phase-in"] * nr
            cmds.append("wait 3000 %s %s" % (kind, shown))
        r = fr.run(
            "ffemu-apple2", cmds, settings=["display = " + display],
            files=DSK)
        self.assertWaits(r, 1 + len(steps))

    def test_led(self):
        """The position and the side's letter, k for side 0."""
        self.check_steps(
            "tm1651-3digit",
            [(0, "00 3f 08"), (2, "00 06 08"), (2, "00 5b 08")])

    def test_lcd(self):
        self.check_steps(
            "hd44780-16x2", [(0, "T:00.0"), (2, "T:01.0"), (2, "T:02.0")])


if __name__ == "__main__":
    unittest.main()

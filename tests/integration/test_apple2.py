"""An Apple2 host stepping the head: the apple2 firmware shows the phases
that the host holds on and the head's position in half tracks, on an LED and
on an LCD."""

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
        """The phases on the first digit's lower segments, 1 the left
        one and 2 the middle one, then the position."""
        self.check_steps(
            "tm1651-3digit",
            [(0, "00 3f 3f"), (2, "10 3f 06"), (2, "40 3f 5b")])

    def test_lcd(self):
        """The phases on as their digits, dots for the others, then the
        position."""
        self.check_steps(
            "hd44780-16x2",
            [(0, ".... T00"), (2, ".1.. T01"), (2, "..2. T02")])


if __name__ == "__main__":
    unittest.main()

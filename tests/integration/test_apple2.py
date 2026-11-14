"""An Apple2 host stepping the head: the apple2 firmware shows the phases
that the host holds on and the image's track, on an LED and on an LCD."""

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
        for keys, shown in steps:
            cmds += keys + ["sleep 20"]
            cmds.append("wait 3000 %s %s" % (kind, shown))
        r = fr.run(
            "ffemu-apple2", cmds, settings=["display = " + display],
            files=DSK)
        self.assertWaits(r, 1 + len(steps))

    def test_led(self):
        """The phases on the first digit's lower segments, 1 the left one and
        2 the middle one, then the position."""
        self.check_steps("tm1651-3digit", [
            ([], "00 3f 3f"), (["key phase-in"] * 2, "10 3f 3f"),
            (["key phase-in"] * 2, "40 3f 06")])

    def test_led_three_digits(self):
        """A position of three digits takes all three digits, the phases
        left out."""
        r = fr.run(
            "ffemu-apple2", ["wait 10000 log Name: 'TEST'"]
            + ["key phase-in", "sleep 20"] * 400
            + ["wait 3000 segments 06 3f 3f"],
            settings=["display = tm1651-3digit"], files=DSK)
        self.assertWaits(r, 2)

    def test_lcd(self):
        """The track, then the four phase bits as one uppercase hex digit."""
        self.check_steps("hd44780-16x2", [
            ([], "T:00.0"), (["key phase-in"], "T:00.3"),
            (["key phase-in"], "T:00.2"), (["key phase-in"], "T:00.6"),
            (["key phase-in"], "T:01.4"), (["key phase-in"], "T:01.C"),
            (["key phase-in"], "T:01.8"), (["key phase-in"], "T:01.9"),
            (["key phase-in"], "T:02.1"), (["key release"], "T:02.0")])


if __name__ == "__main__":
    unittest.main()

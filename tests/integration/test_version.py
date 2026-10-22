"""The version on a 7-segment display: shown, after the CPU's clock and the
RAM, as "UER" and then each part of the version between its points, a frame of
as many characters as there are digits at a time, when the right button is
pressed and released while no USB drive is mounted."""

import unittest

import ffemu_run as fr


class Version(fr.Case, unittest.TestCase):

    def check_frames(self, display, width, banner):
        frames = fr.version_frames("ffemu", width)
        cmds = [
            "wait 10000 log ** FlashFloppy",
            "key remove",
            "wait 5000 segments " + banner,
            "key right",
            # Four frames of the CPU and the RAM first, a second each.
            "wait 8000 segments " + fr.segments(frames[0])]
        cmds += ["wait 3000 segments " + fr.segments(f) for f in frames[1:]]
        r = fr.run("ffemu", cmds, settings=["display = " + display])
        self.assertWaits(r, len(cmds) - 2)

    def test_3_digits(self):
        self.check_frames("tm1651-3digit", 3, "71 40 71")

    def test_2_digits(self):
        self.check_frames("74hc164-2digit", 2, "71 71")

    def test_after_drive_pulled_while_track_shown(self):
        """A drive pulled while the display shows the track, within seconds of
        mounting an image, leaves the display in no state that swallows the
        next press."""
        frames = fr.version_frames("ffemu", 3)
        r = fr.run("ffemu", [
            "wait 10000 log Name: 'TEST'",
            "key remove",
            "wait 5000 segments 71 40 71",
            "key right",
            "wait 8000 segments " + fr.segments(frames[0])],
            settings=["display = tm1651-3digit"],
            files={"TEST.IMG": bytes(737280)})
        self.assertWaits(r, 3)


if __name__ == "__main__":
    unittest.main()

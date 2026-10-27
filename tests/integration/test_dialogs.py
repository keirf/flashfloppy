"""Every dialog of each program drawn by the check-dialogs command, in each
focus and state: no part of a dialog drawn over another part, or over the
dialog's frame."""

import unittest

import ffemu_run as fr


class Dialogs(fr.Case, unittest.TestCase):

    def check(self, name):
        r = fr.run(name, ["wait 10000 log ** FlashFloppy", "check-dialogs"])
        self.assertWaits(r, 1)
        summary = [line for line in r.lines if line.startswith("dialogs: ")]
        self.assertEqual(len(summary), 1, r.report())
        self.assertRegex(
            summary[0], r"^dialogs: [1-9][0-9]* drawn, 0 overlaps$",
            r.report())

    def test_shugart(self):
        self.check("ffemu")

    def test_apple2(self):
        self.check("ffemu-apple2")


if __name__ == "__main__":
    unittest.main()

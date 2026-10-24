"""The virtual clock, which the tests run on unless $FFEMU_CLOCK is host: a
script gives the same output on every run, the times its waits took and the
display's dumps included, also where the firmware waits for a button in a loop
that neither reads the clock nor calls ffemu."""

import unittest

import ffemu_run as fr

IMAGES = {"A.IMG": bytes(737280), "B.IMG": bytes(737280)}
ERROR = {"README.TXT": "No image here.\n"}


@unittest.skipIf(
    fr.CLOCK != "virtual", "the host's clock gives no such guarantee")
class VirtualClock(fr.Case, unittest.TestCase):

    def check_same(self, commands, files, nr_waits):
        first = fr.run("ffemu", commands, files=files)
        self.assertWaits(first, nr_waits)
        for _ in range(2):
            again = fr.run("ffemu", commands, files=files)
            self.assertEqual(again.returncode, 0, again.report())
            self.assertEqual(again.stdout, first.stdout)

    def test_images(self):
        """The encoder, the host's signals and a button, with images."""
        self.check_same([
            "wait 10000 log Name:", "dump", "key cw", "wait 3000 log 'B'",
            "sleep 500", "dump", "key sel", "key motor", "key step",
            "key step", "sleep 300", "dump", "key write", "sleep 200", "dump",
            "key write", "key right", "sleep 1000", "dump", "status"],
            IMAGES, 2)

    def test_error_screen(self):
        """The error screen of a drive with no image, which waits for a
        button."""
        self.check_same([
            "wait 10000 log **Error", "sleep 2000", "dump", "key right",
            "sleep 500", "dump", "key left", "sleep 1000", "dump", "status"],
            ERROR, 1)


if __name__ == "__main__":
    unittest.main()

"""What an error code means, beside the code wherever an LCD row shows it, as
"E31 Bad image", and in the console's line of the error; these look for the
text in any row, whatever the error screen's layout."""

import unittest

import ffemu_run as fr

# An HFE image that is not one, and a drive with no image.
BAD = {"BAD.HFE": b"HXCPICFE" + bytes(600)}
NONE = {"README.TXT": b"No image here.\n"}


class Meanings(fr.Case, unittest.TestCase):

    def check(self, files, display, waits, flash=()):
        """@waits, each a wait's condition, all met on @display."""
        r = fr.run("ffemu", ["wait 10000 " + w for w in waits],
                   settings=["display = " + display], flash=flash,
                   files=files)
        self.assertWaits(r, len(waits))

    def test_mount_error(self):
        self.check(BAD, "hd44780-16x2", ["display E31 Bad image"])

    def test_mount_error_20x4(self):
        self.check(BAD, "hd44780-20x4", ["display E31 Bad image"],
                   ["display-type = lcd-20x04"])

    def test_session_end(self):
        """The end of the drive's session, the console's line saying what the
        error means too."""
        self.check(NONE, "hd44780-16x2",
                   ["log **Error 34 No images", "display E34 No images"])

    def test_session_end_20x4(self):
        self.check(NONE, "hd44780-20x4", ["display E34 No images"],
                   ["display-type = lcd-20x04"])


if __name__ == "__main__":
    unittest.main()

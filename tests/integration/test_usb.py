"""The USB drive made from a directory: FAT16 when the settings say
usb-fat = fat16, and what a FAT16 drive leaves out, as copying files to such a
drive would."""

import re
import unittest

import ffemu_run as fr

PC = {"A.IMG": bytes(737280)}
USB_RE = re.compile(
    r"^usb: inserted, (FAT\d+), .*, (\d+) left out for lack of room, ")


class Fat16(fr.Case, unittest.TestCase):

    def usb(self, r):
        """The file system and the entries left out for lack of room, from
        each "status"."""
        return [
            (m.group(1), int(m.group(2)))
            for m in map(USB_RE.match, r.lines) if m]

    def test_setting(self):
        """The firmware mounts the image from a FAT16 drive."""
        r = fr.run(
            "ffemu", ["wait 10000 log Name: 'A'", "status"],
            settings=["usb-fat = fat16"], files=PC)
        self.assertWaits(r, 1)
        self.assertEqual(self.usb(r), [("FAT16", 0)], r.report())

    def test_command(self):
        """The script's usb-fat, as the Externals dialog, inserts the drive
        again, and the firmware mounts the image anew from the FAT16 drive."""
        r = fr.run("ffemu", [
            "wait 10000 log Name: 'A'", "status", "usb-fat fat16",
            "wait 10000 log Name: 'A'", "status"], files=PC)
        self.assertWaits(r, 2)
        self.assertEqual(
            self.usb(r), [("FAT32", 0), ("FAT16", 0)], r.report())

    def test_root_full(self):
        """Of more entries in the root folder than the 511 that FAT16's 512
        hold beside the label, the last by name are left out, and the firmware,
        finding no room there for IMAGE_A.CFG, stops with FatFS error 7, as
        with a real drive."""
        files = dict(PC)
        files.update(("F%03u.TXT" % i, b"") for i in range(520))
        r = fr.run(
            "ffemu", ["wait 10000 log **Error 7", "status", "log"],
            settings=["usb-fat = fat16"], files=files)
        self.assertWaits(r, 1)
        self.assertEqual(self.usb(r), [("FAT16", 10)], r.report())
        self.assertIn("first F510.TXT", r.stdout)


if __name__ == "__main__":
    unittest.main()

"""The firmware's writes to the USB drive, kept as files in ffemu's state
folder, drive/: the files that they made differ from the medium's, at their
paths on the drive; a file written back as the medium has it leaves. The
folder goes on through a power cycle, as the writes do, and becomes
drive.bak/ when they are dropped, at a fresh start."""

import os
import tempfile
import unittest

import ffemu_run as fr

PC = {"A.IMG": bytes(737280)}
# The image mounted, which truncates IMAGE_A.CFG; Select then ejects it,
# writing "\\EJ" there, and a second Select mounts it again.
MOUNT = ["wait 10000 log Name:", "sleep 500"]
EJECT = ["key select", "sleep 1500"]


class Drive(fr.Case, unittest.TestCase):

    def test_mount(self):
        """IMAGE_A.CFG, which the medium lacked, empty; not the image, which
        the firmware does not write; the console names the folder."""
        r = fr.run("ffemu", MOUNT + ["log"], files=PC)
        self.assertWaits(r, 1)
        self.assertEqual(r.files, {"IMAGE_A.CFG": b""}, r.report())
        self.assertIn("USB drive's writes go to ", r.stdout, r.report())

    def test_fat16(self):
        """On a FAT16 drive, whose root folder has a place of its own."""
        r = fr.run("ffemu", MOUNT + EJECT + ["status"],
                   settings=["usb-fat = fat16"], files=PC)
        self.assertIn("FAT16", r.stdout, r.report())
        self.assertEqual(r.files, {"IMAGE_A.CFG": b"\\EJ"}, r.report())

    def test_eject(self):
        r = fr.run("ffemu", MOUNT + EJECT, files=PC)
        self.assertEqual(r.files, {"IMAGE_A.CFG": b"\\EJ"}, r.report())

    def test_written_back(self):
        """The mount after the eject writes IMAGE_A.CFG empty, as the medium
        has it: it leaves the folder."""
        r = fr.run("ffemu", MOUNT + EJECT + ["key select", "sleep 3000"],
                   files=dict(PC, **{"IMAGE_A.CFG": b""}))
        self.assertEqual(r.files, {}, r.report())

    def test_ff_folder(self):
        """At its path on the drive: in FF when the drive has that folder."""
        r = fr.run("ffemu", MOUNT + EJECT,
                   files=dict(PC, **{"FF/README.TXT": b"Mine.\\n"}))
        self.assertEqual(r.files, {"FF/IMAGE_A.CFG": b"\\EJ"}, r.report())

    def test_host_clock(self):
        """On the host's clock, which a thread of its own keeps the folder
        on, and the quit's last pass."""
        r = fr.run("ffemu", MOUNT + EJECT, files=PC, clock="host")
        self.assertEqual(r.files, {"IMAGE_A.CFG": b"\\EJ"}, r.report())

    def test_power_cycle(self):
        """The writes go on through a power cycle, Home, and so does the
        folder."""
        with tempfile.TemporaryDirectory(prefix="ffemu-test-") as state:
            r = fr.run("ffemu", MOUNT + EJECT + ["key reset", "sleep 3000"],
                       files=PC, state=state)
            self.assertEqual(r.files, {"IMAGE_A.CFG": b"\\EJ"}, r.report())
            self.assertFalse(os.path.exists(
                os.path.join(state, "ffemu-shugart", "drive.bak")))

    def test_fresh_start(self):
        """A fresh start drops the writes: the last folder becomes
        drive.bak/."""
        with tempfile.TemporaryDirectory(prefix="ffemu-test-") as state:
            fr.run("ffemu", MOUNT + EJECT, files=PC, state=state)
            r = fr.run("ffemu", MOUNT, files=PC, state=state)
            self.assertEqual(r.files, {"IMAGE_A.CFG": b""}, r.report())
            bak = fr.read_tree(
                os.path.join(state, "ffemu-shugart", "drive.bak"))
            self.assertEqual(bak, {"IMAGE_A.CFG": b"\\EJ"}, r.report())


if __name__ == "__main__":
    unittest.main()

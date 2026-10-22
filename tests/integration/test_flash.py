"""The configuration in flash: what FF.CFG puts there, which ffemu keeps in its
settings and the firmware finds there after a restart, and a value in flash
that the firmware does not know."""

import unittest

import ffemu_run as fr

FF_CFG = "interface = ibmpc\nstep-volume = 3\n"


class Flash(fr.Case, unittest.TestCase):

    def test_ff_cfg_to_flash_and_back(self):
        # The firmware writes FF.CFG's options to flash and resets, which ends
        # a wait: slept.
        first = fr.run(
            "ffemu", ["sleep 6000", "wait 10000 log Intf IBM PC", "flash"],
            files={"FF.CFG": FF_CFG})
        self.assertWaits(first, 1)
        self.assertEqual(
            first.flash().get("interface"), "ibmpc", first.report())
        self.assertEqual(
            first.flash().get("step-volume"), "3", first.report())

        # The same flash, from the settings, with no FF.CFG on the drive.
        second = fr.run(
            "ffemu", ["wait 10000 log Intf IBM PC", "flash"],
            ffemurc=first.ffemurc)
        self.assertWaits(second, 1)
        self.assertEqual(second.flash(), first.flash(), second.report())

    def test_unknown_interface(self):
        """An interface value of a later firmware or of firmware 307, sa390 as
        6, is taken as jc, the default, rather than indexing the interface
        tables past their ends."""
        r = fr.run(
            "ffemu", ["wait 10000 log Intf Shugart"],
            flash=["interface = 0x06"])
        self.assertWaits(r, 1)


if __name__ == "__main__":
    unittest.main()

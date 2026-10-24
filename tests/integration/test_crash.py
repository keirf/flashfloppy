"""A fault ends ffemu with a report on standard error: the signal, the
thread, the registers and the stack, whose frames name the program."""

import unittest

import ffemu_run as fr


class Crash(fr.Case, unittest.TestCase):

    def test_report(self):
        """The script command crash writes through a null pointer, in the
        script's thread; the status is 128 plus the signal."""
        r = fr.run("ffemu", ["crash"])
        self.assertEqual(r.returncode, 128 + 11, r.report())
        head = ("ffemu: crashed with signal 11 SIGSEGV at 0x0 in the script "
                "thread")
        self.assertIn(head, r.stderr, r.report())
        rest = r.stderr.split(head, 1)[1].splitlines()[1:]
        self.assertTrue(rest[0].startswith("  rip 0x"), r.report())
        frames = rest[1:]
        self.assertGreaterEqual(len(frames), 2, r.report())
        self.assertTrue(any("ffemu-shugart" in f for f in frames), r.report())


if __name__ == "__main__":
    unittest.main()

"""The section of IMG.CFG that the firmware applies to a raw sector image,
as the script's "imgcfg" gives it and the IMG.CFG pane shows it: chosen by
ffemu as the firmware chooses it, which the geometry on the display's info
row confirms, since each section of these tests gives a geometry of its own.
"""

import unittest

import ffemu_run as fr

# Sections telling apart by their geometry: the info row says the sides, the
# density and the cylinders. A 737280-byte image is 80-2-9 by its size alone.
IMG_CFG = """# Geometries for the tests
[Akai::737280]
cyls = 80
heads = 1
secs = 18
bps = 512

[foo::100]
cyls = 70
heads = 2
secs = 9
bps = 512

[]
cyls = 40
heads = 2
secs = 18
bps = 512

[::368640]
cyls = 40
heads = 2
secs = 9
bps = 512
"""


class ImgCfg(fr.Case, unittest.TestCase):

    def mount(self, files):
        """Mounts the one image of @files on a 20x4 LCD: the "imgcfg" header,
        the section's lines, and the LCD's info row."""
        r = fr.run(
            "ffemu", ["wait 10000 log Name:", "wait 10000 display MFM",
                      "sleep 100", "dump", "imgcfg"],
            settings=["display = hd44780-20x4"],
            flash=["display-type = lcd-20x4"], files=files)
        self.assertWaits(r, 2)
        header = [i for i, l in enumerate(r.lines) if l.startswith("imgcfg:")]
        self.assertEqual(len(header), 1, r.report())
        i = header[0]
        rows = [l[1:-1] for l in r.lines[:i] if l.startswith("|")]
        section = [l[1:-1] for l in r.lines[i + 1:] if l.startswith("|")]
        self.assertEqual(len(rows), 4, r.report())
        return r.lines[i], section, rows[3].strip(), r

    def test_tag(self):
        """A section tagged as the image, in another letter case, with the
        image's size: the section's lines as in the file."""
        header, section, info, r = self.mount(
            {"IMG.CFG": IMG_CFG, "A.AKAI.IMG": bytes(737280)})
        self.assertEqual(header, "imgcfg: section [Akai::737280], 5 lines")
        self.assertEqual(
            section,
            ["[Akai::737280]", "cyls = 80", "heads = 1", "secs = 18",
             "bps = 512"], r.report())
        self.assertEqual(info, "MFM SS/HD 80T", r.report())

    def test_default(self):
        """An untagged image takes the empty section, not the sized one."""
        header, section, info, r = self.mount(
            {"IMG.CFG": IMG_CFG, "B.IMG": bytes(737280)})
        self.assertEqual(header, "imgcfg: default section, 5 lines")
        self.assertEqual(section[0], "[]", r.report())
        self.assertEqual(info, "MFM DS/HD 40T", r.report())

    def test_size(self):
        """A section of the image's size alone beats the empty one."""
        header, section, info, r = self.mount(
            {"IMG.CFG": IMG_CFG, "C.IMG": bytes(368640)})
        self.assertEqual(header, "imgcfg: section [::368640], 5 lines")
        self.assertEqual(info, "MFM DS/DD 40T", r.report())

    def test_size_mismatch(self):
        """A tagged section of another size is ruled out, and the empty one
        applies."""
        header, section, info, r = self.mount(
            {"IMG.CFG": IMG_CFG, "D.FOO.IMG": bytes(737280)})
        self.assertEqual(header, "imgcfg: default section, 5 lines")
        self.assertEqual(info, "MFM DS/HD 40T", r.report())

    def test_no_section(self):
        """No section fits: the geometry comes from the image's size."""
        header, section, info, r = self.mount(
            {"IMG.CFG": "[akai::737280]\ncyls = 80\nheads = 1\nsecs = 18\n",
             "B.IMG": bytes(737280)})
        self.assertEqual(header, "imgcfg: no section applies")
        self.assertEqual(section, [], r.report())
        self.assertEqual(info, "MFM DS/DD 80T", r.report())

    def test_no_file(self):
        header, section, info, r = self.mount({"B.IMG": bytes(737280)})
        self.assertEqual(header, "imgcfg: none")
        self.assertEqual(info, "MFM DS/DD 80T", r.report())

    def test_other_type(self):
        """An image of a type that does not read IMG.CFG."""
        header, section, info, r = self.mount(
            {"IMG.CFG": IMG_CFG, "A.ADF": bytes(901120)})
        self.assertEqual(header, "imgcfg: none")

    def test_ff_folder(self):
        """With a folder FF, the IMG.CFG there is read, not the root's."""
        header, section, info, r = self.mount(
            {"FF/IMG.CFG": IMG_CFG,
             "IMG.CFG": "[]\ncyls = 80\nheads = 2\nsecs = 9\n",
             "B.IMG": bytes(737280)})
        self.assertEqual(header, "imgcfg: default section, 5 lines")
        self.assertEqual(info, "MFM DS/HD 40T", r.report())


if __name__ == "__main__":
    unittest.main()

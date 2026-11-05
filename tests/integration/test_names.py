"""Names of files and folders that are not ASCII, which the firmware holds in
the character set of inc/charset.h: a folder entered and an image mounted by
their long names in Cyrillic, and names drawn on the OLED in each font; the
drive of mk_names_image.py, whose names come in every encoding, as the
firmware reads them and as ffemu's unpack names its files; and the short
names of ffemu's drive made from a folder, in each code page."""

import os
import re
import tempfile
import unittest

import ffemu_run as fr
import mk_names_image

# IGRY/IGRA.IMG in Cyrillic, a folder of the root and a PC image in it, which
# the selector enters and mounts by itself.
IGRA = {"\u0418\u0433\u0440\u044b/\u0438\u0433\u0440\u0430.img":
        bytes(737280)}

# The OLED's fonts: the 6x16, the 8x16, and the 8x16 made from the 6x16, as
# the STM32F105's logfile and debug images draw it.
FONTS = {
    "6x16": {},
    "8x16": {"flash": ["oled-font = 8x16"]},
    "synth": {"flash": ["oled-font = 8x16"],
              "settings": ["oled-8x16 = synth"]},
}
MOUNT = ["wait 15000 log Name:", "sleep 300", "dump"]


def font_glyphs(font):
    """The glyphs of the build's OLED font @font, 6x16 or 8x16, by their code
    points: each its columns, a bit a pixel row from the top."""
    return fr.font_glyphs(font)


def glyph(font, code):
    """The columns of character @code in @font of FONTS, as the OLED shows
    it: the 8x16 made from the 6x16 is that glyph with a second copy a
    column to the right, with aliases sharing their canonical glyph."""
    aliases = {
        0x401: 0x415, 0x451: 0x435,
        0x10c: 0x106, 0x10d: 0x107, 0x160: 0x15a, 0x161: 0x15b,
        0x17d: 0x17b, 0x17e: 0x17c, 0x10e: ord("D"), 0x10f: ord("d"),
        0x164: ord("T"), 0x165: ord("t"), 0x147: ord("N"), 0x148: ord("n"),
        0x11a: ord("E"), 0x11b: ord("e"), 0x139: ord("L"), 0x13a: ord("l"),
        0xd0: 0x110, 0xf0: 0x111, 0x150: 0xd6, 0x151: 0xf6,
        0x170: 0xdc, 0x171: 0xfc, 0x218: 0x15e, 0x219: 0x15f,
        0x21a: 0x162, 0x21b: 0x163,
    }
    code = aliases.get(code, code)
    if font != "synth":
        return font_glyphs(font)[code]
    g = font_glyphs("6x16")[code] + [0, 0]
    return [g[x] | (g[x - 1] if x else 0) for x in range(8)]


def pixels(r):
    """The OLED's pixel rows in run @r's dump."""
    lines = r.stdout.splitlines()
    top = next(i for i, l in enumerate(lines) if l.startswith("display: "))
    return lines[top + 1:]


def name_cells(r, font, nr):
    """The first @nr cells of the OLED's top text row in run @r's dump."""
    rows = pixels(r)[:16]
    x0, w = (1, 6) if font == "6x16" else (0, 8)
    return [[sum((rows[y][x0 + w*i + x] != ".") << y for y in range(16))
             for x in range(w)] for i in range(nr)]


class Names(fr.Case, unittest.TestCase):

    def test_cyrillic_long_names(self):
        """The image mounted by its long name, which the console shows in
        UTF-8."""
        r = fr.run("ffemu", ["wait 15000 log Name:", "log"], files=IGRA)
        self.assertWaits(r, 1)
        self.assertIn("Name: '\u0438\u0433\u0440\u0430' Type: img",
                      r.stdout, r.report())

    def test_oled_russian(self):
        """Russian letters by the font's own glyphs, Yo's two among them,
        in each font: Yolka, yozh and Igra."""
        name = ("\u0401\u043b\u043a\u0430 \u0451\u0436 "
                "\u0418\u0433\u0440\u0430")
        for font, opts in FONTS.items():
            with self.subTest(font=font):
                r = fr.run("ffemu", MOUNT,
                           files={name + ".img": bytes(737280)}, **opts)
                self.assertWaits(r, 1)
                self.assertEqual(name_cells(r, font, len(name)),
                                 [glyph(font, ord(c)) for c in name],
                                 r.report())

    def test_oled_437(self):
        """Kaese, L stroke, sharp S and pound; removed peseta as '?'."""
        want = [ord("K"), 0xe4, ord("s"), ord("e"), ord("?"), 0x141,
                0x00a3, 0xdf]
        for font, opts in FONTS.items():
            with self.subTest(font=font):
                r = fr.run("ffemu", MOUNT, files={
                    "K\u00e4se\u20a7\u0141\u00a3\u00df.img":
                    bytes(737280)}, **opts)
                self.assertWaits(r, 1)
                self.assertEqual(name_cells(r, font, len(want)),
                                 [glyph(font, c) for c in want], r.report())

    def test_oled_852_letters(self):
        """Currency and every dedicated Central European letter in each font."""
        groups = (
            "\u00a4\u0102\u0104\u0106\u0118\u013d\u0141\u0143",
            "\u0154\u0158\u015a\u015e\u0162\u016e\u0179\u017b",
            "\u0103\u0105\u0107\u0119\u013e\u0142\u0144\u0155",
            "\u0159\u015b\u015f\u0163\u016f\u017a\u017c",
        )
        self.assertEqual(sum(map(len, groups)), 31)
        for font, opts in FONTS.items():
            for name in groups:
                with self.subTest(font=font, name=name):
                    r = fr.run("ffemu", MOUNT,
                               files={name + ".img": bytes(737280)}, **opts)
                    self.assertWaits(r, 1)
                    self.assertEqual(name_cells(r, font, len(name)),
                                     [glyph(font, ord(c)) for c in name],
                                     r.report())

    def test_oled_central_european_aliases(self):
        """Czech, Slovak, Hungarian, Romanian and Icelandic glyph aliases."""
        groups = (
            "\u010c\u010d\u0160\u0161\u017d\u017e\u010e\u010f",
            "\u0164\u0165\u0147\u0148\u011a\u011b\u0139\u013a",
            "\u0110\u0111\u00d0\u00f0\u0150\u0151\u0170\u0171",
            "\u0218\u0219\u021a\u021b",
        )
        for font, opts in FONTS.items():
            for name in groups:
                with self.subTest(font=font, name=name):
                    r = fr.run("ffemu", MOUNT,
                               files={name + ".img": bytes(737280)}, **opts)
                    self.assertWaits(r, 1)
                    self.assertEqual(name_cells(r, font, len(name)),
                                     [glyph(font, ord(c)) for c in name],
                                     r.report())

    def test_oled_850_letters(self):
        """Every Latin letter added from 850, in each OLED font mode."""
        groups = (
            "\u00f8\u00e3\u00d8\u00c3\u00f0\u00d0\u00ca\u00cb",
            "\u00c8\u0131\u00cd\u00ce\u00cf\u00cc\u00d3\u00df",
            "\u00d4\u00d2\u00f5\u00d5\u00c1\u00c2\u00c0\u00fe",
            "\u00de\u00da\u00db\u00d9\u00fd\u00dd",
        )
        self.assertEqual(sum(map(len, groups)), 30)
        for font, opts in FONTS.items():
            for name in groups:
                with self.subTest(font=font, name=name):
                    r = fr.run("ffemu", MOUNT,
                               files={name + ".img": bytes(737280)}, **opts)
                    self.assertWaits(r, 1)
                    self.assertEqual(name_cells(r, font, len(name)),
                                     [glyph(font, ord(c)) for c in name],
                                     r.report())


# Yolka, Yo in it, which MIK lacks; Lisa, which MIK holds; Kaese with a
# umlaut, which 437 holds and 866 does not.
YOLKA = "\u0401\u043b\u043a\u0430"
LISA = "\u041b\u0438\u0441\u0430"
KAESE = "K\u00e4se"


class ShortNames(fr.Case, unittest.TestCase):
    """The short names of ffemu's drive made from a folder, without long
    names, in the code page of usb-drive-sfn and by the rules of
    usb-drive-sfn-writing: the name that the firmware mounts, reading them
    in the code page of short-name-codepage, or as UTF-8 unless
    detect-utf8-in-short-names says no; and the console's lines on names
    whose short names lose characters."""

    def mounted(self, name, sfn, by="linux", page=None, more=(),
                flash=()):
        """The name of the image that the firmware mounts from a drive of
        image @name, short names in @sfn by @by's rules, read in code page
        @page, or the default, with @flash's options too; and the run."""
        r = fr.run("ffemu", ["wait 15000 log Name:", "log"] + list(more),
                   settings=["usb-drive-lfn = no", "usb-drive-sfn = " + sfn,
                             "usb-drive-sfn-writing = " + by],
                   flash=(["short-name-codepage = " + page] if page else [])
                   + list(flash),
                   files={name + ".img": bytes(737280)})
        self.assertWaits(r, 1)
        m = re.search(r"Name: '([^']*)' Type:", r.stdout)
        self.assertIsNotNone(m, r.report())
        return m.group(1), r

    def test_866(self):
        """866 read as 866: capitals, Yo printed by its Ye alias."""
        name, r = self.mounted(YOLKA, "866")
        self.assertEqual(name, "\u0415\u041b\u041a\u0410", r.report())

    def test_866_read_as_437(self):
        """866 read as 437: 437's characters at its bytes, '?' for Yo's,
        which 437 has as box drawing."""
        name, r = self.mounted(YOLKA, "866", page="437")
        self.assertEqual(name, "?\u00ef\u00e8\u00c7", r.report())

    def test_437_read_as_866(self):
        """437 read as 866: a Russian O in place of the umlauted A."""
        name, r = self.mounted(KAESE, "437")
        self.assertEqual(name, "K\u041eSE", r.report())
        name, r = self.mounted(KAESE, "437", page="437")
        self.assertEqual(name, "K\u00c4SE", r.report())

    def test_850(self):
        """850 written by either system and read as 850, all in capitals."""
        source = "\u00f8\u00e3\u00d8\u00c3\u00f0\u00d0\u00ca\u00cb"
        for by in ("linux", "windows"):
            with self.subTest(by=by):
                name, r = self.mounted(source, "850", by=by, page="850",
                                       more=("status",))
                self.assertEqual(
                    name, "\u00d8\u00c3\u00d8\u00c3\u0110\u0110\u00ca\u00cb",
                    r.report())
                self.assertIn("short names in 850 by %s rules" % by,
                              r.stdout, r.report())

    def test_852(self):
        """852 written by either system and read with its glyph aliases."""
        source = "\u0103\u0105\u0107\u0119\u013e\u0142\u0144\u0155"
        for by in ("linux", "windows"):
            with self.subTest(by=by):
                name, r = self.mounted(source, "852", by=by, page="852",
                                       more=("status",))
                self.assertEqual(
                    name, "\u0102\u0104\u0106\u0118\u013d\u0141\u0143\u0154",
                    r.report())
                self.assertIn("short names in 852 by %s rules" % by,
                              r.stdout, r.report())
            with self.subTest(by=by, aliases=True):
                name, r = self.mounted("\u010d\u0161\u017e\u010f\u0165\u0148\u011b\u013a",
                                       "852", by=by, page="852")
                self.assertEqual(name, "\u0106\u015a\u017bDTNEL", r.report())

    def test_utf8(self):
        """UTF-8 read as UTF-8 whatever short-name-codepage says."""
        for page in ("866", "437", "850", "852"):
            with self.subTest(page=page):
                name, r = self.mounted(YOLKA, "utf8", page=page)
                self.assertEqual(name, "\u0415\u041b\u041a\u0410",
                                 r.report())

    def test_utf8_detection_off(self):
        """UTF-8 read in 866 with detect-utf8-in-short-names = no: its lead
        bytes, box drawing there, as '?', its other bytes as letters."""
        name, r = self.mounted(YOLKA, "utf8", page="866",
                               flash=["detect-utf8-in-short-names = no"])
        self.assertEqual(name, "?\u0411?\u042b?\u042a?\u0420", r.report())

    def test_mik(self):
        """MIK read as MIK; Yo, which it lacks, as '_' with a tail."""
        name, r = self.mounted(LISA, "mik", page="mik")
        self.assertEqual(name, "\u041b\u0418\u0421\u0410", r.report())
        name, r = self.mounted(YOLKA, "mik", page="mik")
        self.assertEqual(name, "_\u041b\u041a\u0410~1", r.report())

    def test_systems(self):
        """An umlauted a in 866: Windows writes A, Linux '_' with a tail."""
        name, r = self.mounted(KAESE, "866", by="windows")
        self.assertEqual(name, "KASE", r.report())
        name, r = self.mounted(KAESE, "866", by="linux")
        self.assertEqual(name, "K_SE~1", r.report())

    def test_script(self):
        """The script's commands for the settings, each inserting the drive
        again, which the status line then names; its line of the drive names
        the image mounted in UTF-8, the 437 short name read as 866."""
        inserted = "wait 15000 log USB drive inserted"
        r = fr.run("ffemu", ["wait 15000 log Name:", "usb-drive-sfn 437",
                             inserted, "usb-drive-sfn-writing windows",
                             inserted, "usb-drive-lfn no", inserted,
                             "wait 15000 log Name:", "status"],
                   settings=["usb-drive-sfn-writing = linux"],
                   files={KAESE + ".img": bytes(737280)})
        self.assertWaits(r, 5)
        self.assertIn("usb: inserted, FAT32, short names in 437 by windows "
                      "rules, no long names, ", r.stdout, r.report())
        self.assertIn(", floppy K\u041eSE.img\n", r.stdout, r.report())

    def test_log(self):
        """A line for a name whose short name loses a character, with the
        first such character, and none for one that keeps them all."""
        name, r = self.mounted(YOLKA, "437")
        self.assertIn('USB drive: "%s.img" has U+0401, which short names '
                      'in 437 lack' % YOLKA, r.stdout, r.report())
        name, r = self.mounted(YOLKA, "866")
        self.assertNotIn("which short names", r.stdout, r.report())

    def test_log_limit(self):
        """A hundred such lines at most, then their count."""
        files = {"%s%03d.img" % (YOLKA, i): bytes(1) for i in range(103)}
        r = fr.run("ffemu", ["wait 10000 log more names", "log"],
                   settings=["usb-drive-sfn = 437"], files=files)
        self.assertWaits(r, 1)
        self.assertEqual(r.stdout.count("which short names in 437 lack"),
                         100, r.report())
        self.assertIn("USB drive: 3 more names that lose characters in "
                      "short names in 437", r.stdout, r.report())

class NamesImage(fr.Case, unittest.TestCase):
    """mk_names_image.py's drive, with the short-name-codepage that each
    folder's short names need: the folder entered from the root, then its own
    folder entered and that folder's image mounted; and its first image
    mounted, its name read on the OLED by its glyphs."""

    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory(prefix="ffemu-test-")
        cls.image = os.path.join(cls.tmp.name, "names.img")
        mk_names_image.build(cls.image)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def mount(self, i, keys):
        """Folder @i of the root entered, @keys pressed in it, until an image
        is mounted: the run."""
        _, page, _, _ = mk_names_image.FOLDERS[i]
        r = fr.run("ffemu", ["sleep 1000"] + ["key right", "sleep 200"] * i
                   + ["sleep 2500"] + keys
                   + ["wait 15000 log Name:", "sleep 300", "log", "dump"],
                   flash=["short-name-codepage = " + page],
                   drive_image=self.image)
        self.assertWaits(r, 1)
        return r

    def test_folders(self):
        """Each folder's own folder entered by its name."""
        for i, (name, _, _, _) in enumerate(mk_names_image.FOLDERS):
            with self.subTest(folder=name):
                r = self.mount(i, [])
                self.assertIn("Name: 'A' Type: img", r.stdout, r.report())

    def test_names(self):
        """Each folder's image listed first."""
        for i, (name, _, shown, _) in enumerate(mk_names_image.FOLDERS):
            with self.subTest(folder=name, image=shown):
                r = self.mount(i, ["key right"])
                self.assertEqual(name_cells(r, "6x16", len(shown)),
                                 [glyph("6x16", ord(c)) for c in shown],
                                 r.report())


class UnpackNames(fr.Case, unittest.TestCase):
    """mk_names_image.py's drive unpacked to a folder, with the usb-drive-sfn
    of each folder of short names alone: its files and folders named in UTF-8
    as that code page reads their short names, as the firmware shows them."""

    PAGES = {"SFN-437": "437", "SFN-850": "850", "SFN-852": "852",
             "SFN-866": "866", "SFN-MIK": "mik", "SFN-UTF8": "utf8"}

    def test_names(self):
        with tempfile.TemporaryDirectory(prefix="ffemu-test-") as tmp:
            image = os.path.join(tmp, "names.img")
            mk_names_image.build(image)
            for folder, _, _, items in mk_names_image.FOLDERS:
                if folder not in self.PAGES:
                    continue
                with self.subTest(folder=folder):
                    out = os.path.join(tmp, folder)
                    r = fr.run(
                        "ffemu", ["sleep 1000", "unpack " + out],
                        settings=["usb-drive-sfn = " + self.PAGES[folder]],
                        drive_image=image)
                    want = sorted(shown + ("" if is_dir else ".IMG")
                                  for _, _, is_dir, shown in items)
                    self.assertEqual(
                        sorted(os.listdir(os.path.join(out, folder))), want,
                        r.report())


# Russian letters, Yo among them, both hard signs and the capital soft sign;
# a 437 letter that A00 has and one that it lacks; the removed one-half and not
# signs, which read as '?'; and the generic currency sign.
ROM_NAME = ("\u0401\u043b\u043a\u0430 \u042a\u044a \u042c\u00e4 "
            "\u00e9\u00bd\u00ac\u00a4")
ROM_CENTRAL = ("\u0102\u0103\u0106\u0107\u0158\u0159\u015e\u015f"
               "\u0162\u0163\u0150\u0151\u0170\u0171\u0110\u0111")


class RomCharset(fr.Case, unittest.TestCase):
    """ROM_NAME on a 16x2 LCD of each ROM, by lcd-charset, and on FF OSD."""

    def rows(self, rom, charset=None, ff_cfg=None, name=ROM_NAME):
        """The LCD's rows, then FF OSD's, once the image is in, lcd-charset
        set in flash as @charset or by FF.CFG @ff_cfg, which resets the
        firmware when it changes the option."""
        files = {name + ".img": bytes(737280)}
        if ff_cfg:
            files["FF.CFG"] = ff_cfg
        r = fr.run(
            "ffemu", ["sleep 6000"] * bool(ff_cfg)
            + ["wait 10000 log Name:", "sleep 1000", "dump", "osd", "flash"],
            settings=["display = hd44780-16x2", "lcd-rom = " + rom],
            flash=["lcd-charset = " + charset] if charset else [],
            files=files)
        self.assertWaits(r, 1)
        rows = [line[1:-1].rstrip() for line in r.lines
                if line.startswith("|") and line.endswith("|")]
        self.assertEqual(len(rows), 5, r.report())
        return rows, r

    def test_jap(self):
        """Russian by KOI-7, Yo as Ye; A00's own a umlaut, base e for e acute,
        '?' for removed signs, and a full block for currency. FF OSD uses '?'
        for both a umlaut and currency."""
        rows, r = self.rows("jap", "jap")
        self.assertEqual(rows[0], "Elka _\u2190 X\u00e4 e??\u2588", r.report())
        self.assertEqual(rows[3], "Elka _\u2592 Xa e???", r.report())

    def test_eur(self):
        """Russian by KOI-7, Yo as Ye; A02's house on 0x7f, its a umlaut, e
        acute and currency, then '?' for the removed signs and on FF OSD."""
        rows, r = self.rows("eur", "eur")
        self.assertEqual(
            rows[0], "Elka _\u2302 X\u00e4 \u00e9??\u00a4", r.report())
        self.assertEqual(rows[3], "Elka _\u2592 Xa e???", r.report())

    def test_legacy_names(self):
        """The old ROM-code names read as their new names in ffemu's flash."""
        for rom, charset, expected in (
                ("a00", "a00", "jap"), ("a02", "a02", "eur")):
            with self.subTest(charset=charset):
                rows, r = self.rows(rom, charset)
                self.assertEqual(
                    r.flash().get("lcd-charset"), expected, r.report())

    def test_cyr(self):
        """The ROM's Russian letters, Yo shown as Ye, Latin stand-ins, its e
        acute and a full block for currency, then '?' for removed signs."""
        for rom in ("cyr-melt", "cyr-sitronix"):
            with self.subTest(rom=rom):
                rows, r = self.rows(rom, "cyr")
                self.assertEqual(
                    rows[0], "E\u043b\u043aa \u042a\u044a ba "
                    "\u00e9??\u2588", r.report())
                self.assertEqual(rows[3], "Elka _\u2592 Xa e???", r.report())

    def test_ff_cfg(self):
        """lcd-charset read from FF.CFG, which puts it in flash."""
        rows, r = self.rows("cyr-melt", ff_cfg="lcd-charset = cyr\n")
        self.assertEqual(r.flash().get("lcd-charset"), "cyr", r.report())
        self.assertEqual(
            rows[0],
            "E\u043b\u043aa \u042a\u044a ba \u00e9??\u2588",
            r.report())

    def test_central_european(self):
        """Central European letters by base, with A02's own umlauts."""
        for rom, charset, expected in (
                ("jap", "jap", "AaCcRrSsTtO\u00f6U\u00fcDd"),
                ("eur", "eur", "AaCcRrSsTt\u00d6\u00f6\u00dc\u00fcDd"),
                ("cyr-melt", "cyr", "AaCcRrSsTtOoUuDd"),
                ("cyr-sitronix", "cyr", "AaCcRrSsTtOoUuDd")):
            with self.subTest(rom=rom):
                rows, r = self.rows(rom, charset, name=ROM_CENTRAL)
                self.assertEqual(rows[0], expected, r.report())
                self.assertEqual(rows[3], "AaCcRrSsTtOoUuDd", r.report())

    def test_ascii(self):
        """An ASCII name as it is, whatever the ROM; lcd-rom = cyr, the name of
        MELT's ROM before Sitronix's came, read as cyr-melt."""
        r = fr.run(
            "ffemu", ["wait 10000 log Name:", "sleep 1000", "dump", "status"],
            settings=["display = hd44780-16x2", "lcd-rom = cyr"],
            flash=["lcd-charset = cyr"],
            files={"Game-1 (v2).img": bytes(737280)})
        self.assertWaits(r, 1)
        self.assertIn("|Game-1 (v2)     |", r.lines, r.report())
        self.assertIn("display: HD44780 16x2, ROM cyr-melt", r.lines,
                      r.report())


if __name__ == "__main__":
    unittest.main()

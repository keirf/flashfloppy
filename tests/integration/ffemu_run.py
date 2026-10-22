"""Runs an ffemu program for the integration tests, as ffemu's own command
interface allows: each run gets settings, a state folder and a USB drive of its
own, made up by the test, and its commands on standard input; what the commands
printed is then parsed.

The programs are those of $FFEMU_OUT, the "out" folder of a build; else of out/
at the top of the tree. They run on ffemu's virtual clock, which gives the same
output on every run, unless $FFEMU_CLOCK is host: then in real time. The tests
let the firmware run by "wait", which ends as soon as the firmware gets there,
rather than by "sleep", except over a firmware reset, which ends a wait. Times
in the commands are the firmware's."""

# Written with the help of Claude Opus 5.5.

import os
import re
import subprocess
import tempfile

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
OUT = os.environ.get("FFEMU_OUT", os.path.join(ROOT, "out"))
CLOCK = os.environ.get("FFEMU_CLOCK", "virtual")
LED_7SEG_C = os.path.join(ROOT, "src", "display", "led_7seg.c")

WAIT_RE = re.compile(r"^wait (\w+) (.*): (ok|timed out) after (\d+) ms$")
FIRMWARE_RE = re.compile(r"^firmware: ([^,]*), ")


def program(name):
    """The path of ffemu program @name, ffemu or ffemu-apple2."""
    path = os.path.join(OUT, name, name)
    if os.path.exists(path + ".exe"):
        return path + ".exe"
    if not os.path.exists(path):
        raise FileNotFoundError("%s: not built; build it by make ffemu" % path)
    return path


class Run:
    """What one run of a program printed, and the settings it left."""

    def __init__(self, stdout, stderr, returncode, ffemurc):
        self.stdout = stdout
        self.stderr = stderr
        self.returncode = returncode
        self.ffemurc = ffemurc
        self.lines = stdout.splitlines()

    def waits(self):
        """The waits' results, in their order: (kind, text, ok, ms)."""
        out = []
        for line in self.lines:
            m = WAIT_RE.match(line)
            if m:
                out.append((
                    m.group(1), m.group(2), m.group(3) == "ok",
                    int(m.group(4))))
        return out

    def flash(self):
        """The options of the last [Flash mem] section printed by "flash"."""
        options = {}
        for line in self.lines:
            if line == "[Flash mem]":
                options = {}
            elif " = " in line and not line.startswith("#"):
                name, value = line.split(" = ", 1)
                options[name] = value
        return options

    def report(self):
        """The output, for a failure's message."""
        return "exit %d\n--- stdout:\n%s--- stderr:\n%s" % (
            self.returncode, self.stdout, self.stderr)


def run(name, commands, settings=(), flash=(), ffemurc=None, files=None,
        timeout=120):
    """Runs program @name on @commands, then quits it. Its settings are the
    lines @settings, then a [Flash mem] section of the lines @flash if any,
    unless @ffemurc gives the whole file; its USB drive is a folder holding
    @files, a dict of names and contents."""
    with tempfile.TemporaryDirectory(prefix="ffemu-test-") as tmp:
        config = os.path.join(tmp, "config")
        os.makedirs(os.path.join(config, name))
        rc = os.path.join(config, name, "ffemurc")
        if ffemurc is None:
            lines = list(settings)
            if flash:
                lines += ["[Flash mem]"] + list(flash)
            ffemurc = "".join(line + "\n" for line in lines)
        with open(rc, "w", newline="") as f:
            f.write(ffemurc)
        drive = os.path.join(tmp, "drive")
        os.makedirs(drive)
        for fname, data in (files or {}).items():
            with open(os.path.join(drive, fname), "wb") as f:
                f.write(data if isinstance(data, bytes) else data.encode())
        env = dict(
            os.environ, XDG_CONFIG_HOME=config,
            XDG_STATE_HOME=os.path.join(tmp, "state"), FFEMU_CLOCK=CLOCK)
        script = "".join(c + "\n" for c in list(commands) + ["key quit"])
        p = subprocess.run(
            [program(name), drive], input=script.encode(), env=env,
            capture_output=True, timeout=timeout)
        with open(rc, newline="") as f:
            left = f.read()
    return Run(
        p.stdout.decode(errors="replace"), p.stderr.decode(errors="replace"),
        p.returncode, left)


_versions = {}


def version(name):
    """The firmware version that program @name reports."""
    if name not in _versions:
        r = run(name, ["wait 10000 log ** FlashFloppy", "status"])
        for line in r.lines:
            m = FIRMWARE_RE.match(line)
            if m and m.group(1):
                _versions[name] = m.group(1)
                break
            if m:
                raise RuntimeError(
                    "%s reports an empty version: built with no VER?" % name)
        else:
            raise RuntimeError(
                "%s reports no version:\n%s" % (name, r.report()))
    return _versions[name]


def segments(cells):
    """The text of a "wait ... segments" for 7-segment cells, a byte per
    digit."""
    return " ".join("%02x" % c for c in cells)


_font = {}


def led_cells(text):
    """The 7-segment cells of @text, a byte per character, from the tables of
    letters and digits in the firmware's display driver; `-` is the middle
    segment, anything else blank."""
    if not _font:
        with open(LED_7SEG_C) as f:
            src = f.read()
        for name, first in (("letters", "a"), ("digits", "0")):
            m = re.search(r"\b%s\[\] = \{(.*?)\};" % name, src, re.S)
            cells = re.findall(r"0x[0-9a-fA-F]{2}", m.group(1))
            for i, cell in enumerate(cells):
                _font[chr(ord(first) + i)] = int(cell, 16)
        _font["-"] = 0x40
    return [_font.get(c, 0) for c in text.lower()]


def version_frames(name, width):
    """The frames that program @name's firmware shows for its version on @width
    digits, after those of the CPU and the RAM: UER, then each part of the
    version between its points, cut into frames of @width characters."""
    frames = [led_cells("UER")[:width]]
    for part in version(name).split("."):
        for i in range(0, len(part), width):
            frames.append(led_cells(part[i:i + width].ljust(width)))
    return frames


class Case:
    """Assertions on runs, mixed into a unittest.TestCase."""

    def assertWaits(self, r, nr):
        """That @r printed @nr waits and that every one ended ok."""
        waits = r.waits()
        self.assertEqual(r.returncode, 0, r.report())
        self.assertEqual(
            len(waits), nr, "waits that ended: %s\n%s" % (waits, r.report()))
        for kind, text, ok, ms in waits:
            self.assertTrue(
                ok, "wait %s %s timed out\n%s" % (kind, text, r.report()))

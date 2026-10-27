"""What each option takes, as the Flash mem dialog offers it and the options
command prints it, against examples/FF.CFG: a name or flag that FF.CFG gives
an option and ffemu's table lacks, or the other way round, fails here."""

import os
import re
import unittest

import ffemu_run as fr

FF_CFG = os.path.join(
    os.path.dirname(os.path.abspath(__file__)), "..", "..", "examples",
    "FF.CFG")

# The options whose values code of their own parses, such as display-type.
TEXT = {
    "display-type", "display-order", "osd-display-order", "motor-delay",
    "chgrst"}

NAME_RE = re.compile(r"[a-z0-9][a-z0-9-]*$")


def ff_cfg_blocks():
    """Each option of FF.CFG with the comment lines above it, which options
    on consecutive lines share."""
    blocks, block, after_option = {}, [], False
    with open(FF_CFG, encoding="ascii") as f:
        for line in f:
            line = line.rstrip("\n")
            m = re.match(r"([a-z0-9-]+) = ", line)
            if m:
                blocks[m.group(1)] = block
                after_option = True
            elif line.startswith("#"):
                if after_option:
                    block = []
                block.append(line)
                after_option = False
            else:
                block, after_option = [], False
    return blocks


def ff_cfg_names(block):
    """The names that comment @block gives its option: those of its Values
    line, or of the lines that start with a name and a colon; and whether
    the Values line has a number N."""
    for line in block:
        m = re.match(r"#\s*Values:\s*(.*)", line)
        if m:
            text = re.sub(r"\(.*?\)", "", m.group(1))
            text = text.replace("[", ",").replace("]", "")
            items = (i.strip() for i in re.split(r"[|,;]", text))
            return {i for i in items if NAME_RE.match(i)}, "N" in text
    names = set()
    for line in block:
        m = re.match(r"#\s+([a-z0-9][a-z0-9-]*):", line)
        if m:
            names.add(m.group(1))
    return names, False


class Options(unittest.TestCase):

    def test_table_matches_ff_cfg(self):
        r = fr.run("ffemu", ["options"])
        table = {}
        for line in r.lines:
            words = line.split()
            if words[:1] == ["option"]:
                table[words[1]] = (
                    words[2], {w for w in words[3:] if w[0] != "+"},
                    {w[1:] for w in words[3:] if w[0] == "+"})
        blocks = ff_cfg_blocks()
        self.assertEqual(set(table), set(blocks), r.report())
        for name, block in blocks.items():
            with self.subTest(option=name):
                kind, values, flags = table[name]
                names, number = ff_cfg_names(block)
                if name in TEXT:
                    self.assertEqual(kind, "text")
                    continue
                self.assertNotEqual(kind, "text")
                if number:
                    self.assertEqual(kind, "number")
                self.assertEqual(values | flags, names)


if __name__ == "__main__":
    unittest.main()

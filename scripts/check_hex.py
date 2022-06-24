# check_hex.py
# 
# Check the base address and size of segments in an Intel Hex target file,
# based on known constraints for the specified MCU target.
# 
# Written & released by Keir Fraser <keir.xen@gmail.com>
# 
# This is free and unencumbered software released into the public domain.
# See the file COPYING for more details, or visit <http://unlicense.org>.

import os, re, sys
from intelhex import IntelHex

def kb(n):
    return n*1024

def _fatal(s):
    print('*** ' + s, file=sys.stderr)

def fatal(s):
    _fatal(s)
    sys.exit(1)

class Target:

    FLASH_BASE = 0x8000000

    def __init__(self, bootloader_size, flash_size, page_size):
        self._bootloader_size = bootloader_size
        self._flash_size = flash_size
        self._page_size = page_size

    def bootloader_base(self):
        return self.FLASH_BASE

    def bootloader_size(self):
        return self._bootloader_size

    def firmware_base(self):
        return self.FLASH_BASE + self._bootloader_size

    def firmware_size(self):
        # Allow for bootloader at start of Flash, and cached config at end
        return self._flash_size - self._bootloader_size - self._page_size

targets = { 'stm32f105': Target(kb(32), kb(128), kb(2)),
            'at32f435': Target(kb(48), kb(256), kb(2)) }

def usage(argv):
    fatal("Usage: %s file.hex target" % argv[0])

def main(argv):

    if len(argv) != 3:
        usage(argv)

    h = argv[1]
    try:
        t = targets[argv[2]]
    except KeyError:
        _fatal('Unknown target: ' + argv[2])
        usage(argv)

    # Informational prefix for log lines
    prefix = h
    m = re.match(r'.*/out/(.*)', os.path.abspath(h))
    if m is not None:
        prefix = m.group(1)

    ih = IntelHex(h)
    for (s,e) in ih.segments():
        sz = e - s
        if s == t.bootloader_base():
            if sz > t.bootloader_size():
                fatal('%s: Bootloader overflows by %d bytes'
                      % (prefix, sz - t.bootloader_size()))
            print('%s: Bootloader has %d bytes headroom'
                  % (prefix, t.bootloader_size() - sz))
        elif s == t.firmware_base():
            if sz > t.firmware_size():
                fatal('%s: Firmware overflows by %d bytes'
                      % (prefix, sz - t.firmware_size()))
            print('%s: Firmware has %d bytes headroom'
                  % (prefix, t.firmware_size() - sz))
        else:
            fatal('%s: Unexpected start address %x' % (prefix, s))

if __name__ == "__main__":
    main(sys.argv)

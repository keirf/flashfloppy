# srcdir.py
#
# Helper script to locate the relative source folder for a given object folder.
# For example:
#  objdir = /path/to/out/stm32f105/prod/floppy/usb
#  srcdir = ../../../../../src/usb
# 
# Written & released by Keir Fraser <keir.xen@gmail.com>
# 
# This is free and unencumbered software released into the public domain.
# See the file COPYING for more details, or visit <http://unlicense.org>.

import sys, re

# /out/<mcu>/<level>/<target>
NR_LEVELS = 4

objdir = sys.argv[1]

# stem = /out/<mcu>/<level>/target[/<rest_of_path>]
stem = objdir[objdir.rfind('/out'):]

# stem = [/<rest_of_path>]
m = re.match('/[^/]*'*NR_LEVELS+'(/.*)?', stem)
stem = '' if m.group(1) is None else m.group(1)

# srcdir = path to sources, relative to objdir
srcdir = '../'*(NR_LEVELS+stem.count('/')) + 'src' + stem
print(srcdir)

# srcdir.py
#
# Helper script to locate the source folder for a given object folder.
# The path is relative if the object folder is within the source tree.
# For example:
#  objdir = /path/to/out/stm32f105/prod/floppy/usb
#  root   = /path/to
#  srcdir = ../../../../../src/usb
# Otherwise (out-of-tree build) the path is absolute.
# For example:
#  objdir = /build/out/stm32f105/prod/floppy/usb
#  root   = /path/to
#  srcdir = /path/to/src/usb
# 
# Written & released by Keir Fraser <keir.xen@gmail.com>
# 
# This is free and unencumbered software released into the public domain.
# See the file COPYING for more details, or visit <http://unlicense.org>.

import sys, os, re

# /out/<mcu>/<level>/<target>
NR_LEVELS = 4

objdir = sys.argv[1]
root = sys.argv[2]

# stem = /out/<mcu>/<level>/target[/<rest_of_path>]
stem = objdir[objdir.rfind('/out'):]

# stem = [/<rest_of_path>]
m = re.match('/[^/]*'*NR_LEVELS+'(/.*)?', stem)
stem = '' if m.group(1) is None else m.group(1)

# srcdir = path to sources, relative to objdir if that is within the tree
srcdir = root + '/src' + stem
if objdir.startswith(root + '/'):
    srcdir = os.path.relpath(srcdir, objdir)
print(srcdir)

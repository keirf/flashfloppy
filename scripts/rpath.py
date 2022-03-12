import sys, re

# out/<mcu>/<level>/<target>/
NR_LEVELS = 4

root, curdir = sys.argv[1], sys.argv[2]

# special case for root Makefile, which resides outside src/
if root == curdir:
    print('.')
    sys.exit(0)

# stump = /out/<mcu>/<level>/target[/<rest_of_path>]
stump = curdir[len(root):]

# stump = [/<rest_of_path>]
m = re.match('/[^/]*'*NR_LEVELS+'(/.*)?', stump)
stump = '' if m.group(1) is None else m.group(1)

rpath = '../'*(NR_LEVELS+stump.count('/')) + 'src' + stump
print(rpath)

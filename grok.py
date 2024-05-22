# Takes pulseview Hexadeciomal digits export

import re, sys
from bitarray import bitarray

f = open(sys.argv[1], 'r')

MHZ=6
CELL=4*MHZ
THRESH=2*CELL-MHZ//2

b = bitarray()
count, prev, run = 0, 128, 0
for line in f:
    if count == 1000:
        break
    if not line.startswith('RDATA'):
        continue
    count += 1
    for i in range(6, len(line)-1, 3):
        x = int(line[i:i+2], 16)
        for j in range(8):
            level = x & 128
            if level != prev:
                prev = level
                if level == 0:
                    while run > THRESH:
                        b.append(False)
                        run -= CELL
                    b.append(True)
                    run = 0
            run += 1
            x <<= 1
print(b)

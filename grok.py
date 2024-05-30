# Takes pulseview Hexadeciomal digits export

import re, sys
from bitarray import bitarray

f = open(sys.argv[1], 'r')

MHZ=6
CELL=4*MHZ
THRESH=2*CELL-MHZ//2

class Bits:
    def __init__(self):
        self.b = bitarray()
        self.prev, self.run = 128, 0
    def process(self,line):
        for i in range(6, len(line)-1, 3):
            x = int(line[i:i+2], 16)
            for j in range(8):
                level = x & 128
                if level != self.prev:
                    self.prev = level
                    if level == 0:
                        while self.run > THRESH:
                            self.b.append(False)
                            self.run -= CELL
                        self.b.append(True)
                        self.run = 0
                self.run += 1
                x <<= 1
        
wdata = Bits()
rdata = Bits()
wreq = Bits()
count = 0
for line in f:
    if count == 100000:
        break
    if line.startswith('WDATA'):
        wdata.process(line)
    elif line.startswith('RDATA'):
        rdata.process(line)
    elif line.startswith('/WREQ'):
        wreq.process(line)
    else:
        continue
    count += 1
print(wdata.b)
print(rdata.b)
print(wreq.b)

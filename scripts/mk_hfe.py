# mk_hfe.py
#
# Make a blank HFE image.
#
# Written & released by Keir Fraser <keir.xen@gmail.com>
#
# This is free and unencumbered software released into the public domain.
# See the file COPYING for more details, or visit <http://unlicense.org>.

import sys,struct,argparse

def main(argv):
  parser = argparse.ArgumentParser(
    formatter_class=argparse.ArgumentDefaultsHelpFormatter)
  parser.add_argument("--rate", type=int, default=250,
                      help="data rate, kbit/s")
  parser.add_argument("--rpm", type=int, default=300,
                      help="rotational rate, rpm")
  parser.add_argument("--cyls", type=int, default=80,
                      help="number of cylinders")
  parser.add_argument("--sides", type=int, default=2,
                      help="number of sides")
  parser.add_argument("outfile", help="output filename")
  args = parser.parse_args(argv[1:])

  bits = (args.rate * 1000 * 60) // args.rpm
  bits *= 2 # clock bits
  bits *= 2 # 2 sides
  bytes = (bits + 7) // 8     # convert to bytes, rounded up
  bytes = (bytes + 15) & ~15  # round up to 16-byte boundary
  blocks = (bytes + 511) // 512 # convert to 512-byte blocks, rounded up

  print("Geometry: %u cylinders, %u sides" % (args.cyls, args.sides))
  print("%ukbit/s @ %uRPM -> %u Encoded Bits"
        % (args.rate, args.rpm, bits/2))
  print("Data per HFE Track: %u bytes, %u blocks" % (bytes, blocks))
  
  # Header
  out_f = open(args.outfile, "wb")
  out_f.write(struct.pack("<8s4B2H2BH",
                          b"HXCPICFE",# signature
                          0,          # revision
                          args.cyls,  # nr_tracks
                          args.sides, # nr_sides
                          0xff,       # track_encoding
                          args.rate,  # bitrate
                          0,          # rpm
                          0xfe,       # interface_mode
                          1,          # rsvd
                          1))         # track_list_offset
  out_f.write(bytearray(b'\xff'*(512-20)))

  # TLUT
  tlut_blocks = (args.cyls*4 + 511) // 512
  base = 1 + tlut_blocks
  for i in range(args.cyls):
    out_f.write(struct.pack("<2H", base, bytes))
    base += blocks
  out_f.write(bytearray(b'\xff'*(tlut_blocks*512-args.cyls*4)))

  # Data
  out_f.write(bytearray(b'\x88'*(blocks*512*args.cyls)))
    
if __name__ == "__main__":
    main(sys.argv)

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
  parser.add_argument("--hard-sectors", type=int, default=0,
                      help="number of hard sectors")
  parser.add_argument("outfile", help="output filename")
  args = parser.parse_args(argv[1:])

  v3 = args.hard_sectors != 0

  bits = (args.rate * 1000 * 60) // args.rpm
  bits *= 2 # clock bits
  bits *= 2 # 2 sides
  bytes = (bits + 7) // 8 # convert to bytes, rounded up
  if not v3:
    bytes = (bytes + 15) & ~15 # round up to 16-byte boundary
  raw_bytes = bytes
  if args.hard_sectors:
    bytes += (args.hard_sectors + 1) * 2 # 2 sides
  blocks = (bytes + 511) // 512 # convert to 512-byte blocks, rounded up

  print("Geometry: %u cylinders, %u sides" % (args.cyls, args.sides))
  print("%ukbit/s @ %uRPM -> %u Encoded Bits"
        % (args.rate, args.rpm, bits/2))
  print("Data per HFE Track: %u bytes, %u blocks" % (bytes, blocks))
  
  # Header
  out_f = open(args.outfile, "wb")
  sig = b'HXCHFEV3' if v3 else b"HXCPICFE"
  out_f.write(struct.pack("<8s4B2H2BH",
                          sig,        # signature
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
  if not args.hard_sectors:
    out_f.write(bytearray(b'\x88'*(blocks*512*args.cyls)))
  else:
    trk_side = bytearray(b'\x88')*(raw_bytes//2)
    sector_len = float(raw_bytes//2) / args.hard_sectors
    trk_side.insert(-int(sector_len)//2, 0x8F)
    for sector in range(args.hard_sectors-1, -1, -1):
      trk_side.insert(int(sector_len*sector), 0x8F)
    assert len(trk_side) == bytes//2
    trk_side += b'\x0F'*(256 - len(trk_side)%256)
    assert len(trk_side) == blocks*256

    trk = bytearray()
    for pos in range(0, len(trk_side), 256):
      trk.extend(trk_side[pos:pos+256])
      trk.extend(trk_side[pos:pos+256])
    for _ in range(args.cyls):
      out_f.write(trk)
    
if __name__ == "__main__":
    main(sys.argv)

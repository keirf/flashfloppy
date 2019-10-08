# mk_qd.py
#
# Make a blank QD (Quick Disk) image.
#
# Written & released by Keir Fraser <keir.xen@gmail.com>
#
# This is free and unencumbered software released into the public domain.
# See the file COPYING for more details, or visit <http://unlicense.org>.

import sys,struct,argparse

def main(argv):
  parser = argparse.ArgumentParser(
    formatter_class=argparse.ArgumentDefaultsHelpFormatter)
  parser.add_argument("--window", type=float, default=5.5,
                      help="data window, seconds")
  parser.add_argument("--total", type=float, default=8.0,
                      help="total length, seconds")
  parser.add_argument("outfile", help="output filename")
  args = parser.parse_args(argv[1:])

  bit_ms = 0.004916
  total_bytes = int(args.total * 1000.0 / bit_ms / 8)
  window_bytes = int(args.window * 1000.0 / bit_ms / 8)
  init_bytes = int(500.0 / bit_ms / 8)

  assert (2*init_bytes + window_bytes) < total_bytes, "Window too large"
  print("Lead-In: %.2f sec -> %u bytes" % (0.5, init_bytes))
  print("Window:  %.2f sec -> %u bytes" % (args.window, window_bytes))
  print("TOTAL:   %.2f sec -> %u bytes" % (args.total, total_bytes))

  # Header
  out_f = open(args.outfile, "wb")
  out_f.write(struct.pack("<3x2s3x", b"QD"))
  out_f.write(bytearray(b'\x00'*(512-8)))

  # Track
  out_f.write(struct.pack("<4I", 1024, total_bytes, init_bytes,
                          init_bytes + window_bytes))
  out_f.write(bytearray(b'\x00'*(512-16)))

  # Data
  blocks = (total_bytes + 511) // 512
  out_f.write(bytearray(b'\x11'*(blocks*512)))
    
if __name__ == "__main__":
    main(sys.argv)

# mk_qd.py
#
# Make a blank QD (Quick Disk) image.
#
# Written & released by Keir Fraser <keir.xen@gmail.com>
#
# This is free and unencumbered software released into the public domain.
# See the file COPYING for more details, or visit <http://unlicense.org>.

import sys,struct,argparse

def round_up(x, y):
    return (x + y - 1) & ~(y - 1)

def main(argv):
    parser = argparse.ArgumentParser(
      formatter_class=argparse.ArgumentDefaultsHelpFormatter)
    parser.add_argument("--lead-in", type=float, default=0.5,
                        help="lead-in, seconds")
    parser.add_argument("--window", type=float, default=5.5,
                        help="data window, seconds")
    parser.add_argument("--total", type=float, default=8.0,
                        help="total length, seconds")
    parser.add_argument("--round", action="store_true",
                        help="round values up to 512-byte block size")
    parser.add_argument("outfile", help="output filename")
    args = parser.parse_args(argv[1:])

    lead_in, window, total = args.lead_in, args.window, args.total

    assert lead_in >= 0.1, "Insufficient lead-in"
    assert total - window - lead_in >= 0.1, "Insufficient lead-out"

    bit_ms = 0.004916
    total_bytes = int(total * 1000.0 / bit_ms / 8)
    window_bytes = int(window * 1000.0 / bit_ms / 8)
    init_bytes = int(lead_in * 1000.0 / bit_ms / 8)

    if args.round:
        total_bytes = round_up(total_bytes, 512)
        window_bytes = round_up(window_bytes, 512)
        init_bytes = round_up(init_bytes, 512)

    print("Lead-In: %.2f sec -> %u bytes" % (lead_in, init_bytes))
    print("Window:  %.2f sec -> %u bytes" % (window, window_bytes))
    print("TOTAL:   %.2f sec -> %u bytes" % (total, total_bytes))

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

# Local variables:
# python-indent: 4
# End:

#!/usr/bin/env python3
"""nnue_convert.py - fold a 768-feature trainer net into the engine's 704 layout.

The engine (nnue.c) uses a 704 one-hot feature layout:
    rows 0..31     own king        32 buckets (file folded to a-d)
    rows 32..79    own pawns       48 squares (ranks 2-7, compact index 1..6)
    rows 80..335   own N,B,R,Q     4 x 64
    rows 336..399  enemy king      64
    rows 400..447  enemy pawns     48
    rows 448..703  enemy N,B,R,Q   4 x 64

The trainer emits the plain 768 layout, row = type*64 + sq, with type IDs:
    0=WP 1=WN 2=WB 3=WR 4=WQ 5=WK 6=BP 7=BN 8=BB 9=BR 10=BQ 11=BK

Converting is a fixed gather permutation (a subset of the 768 rows reordered
to the engine layout). The king's files e-h fold into the a-d buckets (the
documented 704 compression); those 768 rows are simply unused.

RAW TRAINER FILE (no header, little-endian; trailing bytes such as the
trainer's "bullet" padding are ignored):
    w1:   [768][N]  signed bytes, feature-major (w1[type*64+sq][j])
    b1:   [N]       signed bytes, layer-1 bias (shared by both perspectives)
    w2:   [2N]      signed bytes (white POV first, then black POV)
    bias: [1] i16   output bias (quantized at 128*64)

ENGINE BLOB (what the engine loads, little-endian):
    bytes  0..3   magic "NNUE"
          4..5   version u16 = 2 (ReLU^2 - the only supported architecture)
          6..7   features u16 = 704
          8..9   N u16
         10..11  reserved u16 = 0
         12..     w1:  704*N signed bytes, feature-major
                  b1:  N signed bytes
                  w2:  2N signed bytes
                  bias: i16

The engine architecture (see NNUE.md): feature transformer x256. A one-output
v2 blob keeps the source x64 weights and uses (act^2*w2)>>9. An eight-output
v4 blob requantizes them to x32 in [-64,63] and uses (act^2*w2)>>8. The real
output scale remains 1.0 = 256 cp = out >> 5.

Usage:
    python nnue_convert.py short-v2.nnue chess-v2.net
"""

import argparse
import struct
import sys

MAGIC = b"NNUE"


def engine_to_768(row):
    """Engine 704 row index -> (768 type, 768 square) for the WHITE perspective."""
    if row < 32:                 # own king, bucket = rank*8 + folded file (0..3)
        return 5, row
    if row < 80:                 # own pawn, idx 0..47 -> rank 1..6, file 0..7
        i = row - 32
        return 0, ((i >> 3) + 1) * 8 + (i & 7)
    if row < 336:                # own N/B/R/Q
        off = row - 80
        return 1 + (off >> 6), off & 63
    if row < 400:                # enemy king
        return 11, row - 336
    if row < 448:                # enemy pawn
        i = row - 400
        return 6, ((i >> 3) + 1) * 8 + (i & 7)
    off = row - 448              # enemy N/B/R/Q
    return 7 + (off >> 6), off & 63


def read_trainer(path):
    with open(path, "rb") as f:
        data = f.read()
    # size must be at least w1 + b1 + w2 + bias; extra trailing bytes ignored
    w1_sz = 768 * 64
    b1_sz = 64
    w2_sz = 128
    need = w1_sz + b1_sz + w2_sz + 2
    if len(data) < need:
        sys.exit(f"{path}: too short ({len(data)} < {need})")
    w1 = data[0:w1_sz]
    b1 = data[w1_sz:w1_sz + b1_sz]
    w2 = data[w1_sz + b1_sz:w1_sz + b1_sz + w2_sz]
    bias = struct.unpack_from("<h", data, w1_sz + b1_sz + w2_sz)[0]
    return w1, b1, w2, bias


def write_net(path, w1, b1, w2, bias, output_buckets):
    if output_buckets == 8:
        wide = struct.unpack(f"<{len(w2)}b", w2)
        narrow = []
        for value in wide:
            value = ((value + 1) // 2 if value >= 0
                     else -((-value + 1) // 2))
            narrow.append(max(-64, min(63, value)))
        w2 = struct.pack(f"<{len(narrow)}b", *narrow)
    with open(path, "wb") as f:
        f.write(MAGIC)
        if output_buckets == 1:
            f.write(struct.pack("<HHHH", 2, 704, 64, 0))   # v2 ReLU^2
        else:
            f.write(struct.pack("<HHHH", 4, 704, 64, output_buckets))
        f.write(w1)
        f.write(b1)
        f.write(w2 * output_buckets)
        f.write(struct.pack(f"<{output_buckets}h", *([bias] * output_buckets)))


def main():
    ap = argparse.ArgumentParser(description="fold 768 trainer net -> 704 engine blob")
    ap.add_argument("--output-buckets", type=int, choices=(1, 8), default=1,
                    help="write one output (v2) or narrow/replicate it into eight v4 buckets")
    ap.add_argument("src", help="raw 768 trainer file (short-v2.nnue)")
    ap.add_argument("dst", help="output engine blob (chess-v2.net)")
    args = ap.parse_args()
    src, dst = args.src, args.dst
    w1, b1, w2, bias = read_trainer(src)

    rows = [None] * 704
    for r in range(704):
        t, sq = engine_to_768(r)
        rows[r] = t * 64 + sq
    w1_out = b"".join(w1[r * 64:(r + 1) * 64] for r in rows)

    write_net(dst, w1_out, b1, w2, bias, args.output_buckets)
    version = 2 if args.output_buckets == 1 else 4
    print(f"{src}: folded 768 -> 704, wrote {dst} (v{version} ReLU^2, "
          f"output_buckets={args.output_buckets}, bias={bias})")


if __name__ == "__main__":
    main()

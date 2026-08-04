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
          4..5   version u16 = 1
          6..7   features u16 = 704
          8..9   N u16
         10..11  reserved u16 = 0
         12..     w1:  704*N signed bytes, feature-major
                  b1:  N signed bytes
                  w2:  2N signed bytes
                  bias: i16

Usage:
    python nnue_convert.py short-hl64.nnue chess.net
"""

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


def write_net(path, w1, b1, w2, bias):
    with open(path, "wb") as f:
        f.write(MAGIC)
        f.write(struct.pack("<HHHH", 1, 704, 64, 0))
        f.write(w1)
        f.write(b1)
        f.write(w2)
        f.write(struct.pack("<h", bias))


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    src, dst = sys.argv[1], sys.argv[2]
    w1, b1, w2, bias = read_trainer(src)

    rows = [None] * 704
    for r in range(704):
        t, sq = engine_to_768(r)
        rows[r] = t * 64 + sq
    w1_out = b"".join(w1[r * 64:(r + 1) * 64] for r in rows)

    write_net(dst, w1_out, b1, w2, bias)
    print(f"{src}: folded 768 -> 704, wrote {dst} "
          f"({len(w1_out)} + {len(b1)} + {len(w2)} + 2 + 12 bytes, bias={bias})")


if __name__ == "__main__":
    main()

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

Blob format (both 768 trainer output and 704 engine net):
    bytes  0..3   magic "NNUE"
          4..5   version u16 = 1
          6..7   features u16 (768 here, 704 in the engine blob)
          8..9   N u16            (hidden size per perspective)
         10..11  reserved u16 = 0
        12..     w1:  features*N signed bytes, feature-major (w1[f][j])
                 w2:  2*N signed bytes (white POV first, then black POV)
                 bias: int32 little-endian

Usage:
    python nnue_convert.py trainer768.bin engine.net
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


def read_net(path):
    with open(path, "rb") as f:
        data = f.read()
    if len(data) < 12 or data[:4] != MAGIC:
        sys.exit(f"{path}: bad magic or too short")
    ver, feats, n, _res = struct.unpack_from("<HHHH", data, 4)
    if ver != 1:
        sys.exit(f"{path}: unsupported version {ver}")
    w1_sz = feats * n
    w2_sz = 2 * n
    if len(data) != 12 + w1_sz + w2_sz + 4:
        sys.exit(f"{path}: bad size {len(data)} (expected {12 + w1_sz + w2_sz + 4})")
    w1 = data[12:12 + w1_sz]
    w2 = data[12 + w1_sz:12 + w1_sz + w2_sz]
    (bias,) = struct.unpack_from("<i", data, 12 + w1_sz + w2_sz)
    return feats, n, w1, w2, bias


def write_net(path, feats, n, w1, w2, bias):
    with open(path, "wb") as f:
        f.write(MAGIC)
        f.write(struct.pack("<HHHH", 1, feats, n, 0))
        f.write(w1)
        f.write(w2)
        f.write(struct.pack("<i", bias))


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    src, dst = sys.argv[1], sys.argv[2]
    feats, n, w1, w2, bias = read_net(src)

    if feats == 704:
        print(f"{src}: already 704 features, copying verbatim")
        write_net(dst, 704, n, w1, w2, bias)
        print(f"wrote {dst} (704x{n}, {len(w1) + len(w2) + 4 + 12} bytes)")
        return
    if feats != 768:
        sys.exit(f"{src}: features={feats}, expected 768 (or already-converted 704)")

    rows = [None] * 704
    for r in range(704):
        t, sq = engine_to_768(r)
        rows[r] = t * 64 + sq
    w1_out = b"".join(w1[r * n:(r + 1) * n] for r in rows)

    write_net(dst, 704, n, w1_out, w2, bias)
    print(f"{src}: folded 768 -> 704, wrote {dst} (704x{n}, "
          f"{len(w1_out) + len(w2) + 4 + 12} bytes)")
    if n != 64:
        print(f"note: N={n}; the engine expects NNUE_N=64, rebuild with NNUE_N={n}")


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""Requantize an eight-bucket v3 net from x64 w2 to narrow x32 blob v4.

The engine evaluates v3 terms as (activation^2 * w2) >> 9. Blob v4 stores
round(w2/2) in [-64,63] and shifts by 8, retaining the same real output scale
while allowing a direct 128-row signed product table in one 64 KB segment.
Layer-1 weights and the x8192 output biases are copied byte-for-byte.
"""

import argparse
import struct
from pathlib import Path


FEATURES = 704
HIDDEN = 64
BUCKETS = 8
W1_SIZE = FEATURES * HIDDEN
W2_SIZE = BUCKETS * 2 * HIDDEN
EXPECTED_SIZE = 12 + W1_SIZE + HIDDEN + W2_SIZE + 2 * BUCKETS


def halve(value, mode):
    if mode == "away":
        narrowed = ((value + 1) // 2 if value >= 0
                    else -((-value + 1) // 2))
    elif mode == "zero":
        narrowed = value // 2 if value >= 0 else -((-value) // 2)
    elif mode == "floor":
        narrowed = value // 2
    elif mode == "ceil":
        narrowed = -((-value) // 2)
    else:
        narrowed = round(value / 2)
    return max(-64, min(63, narrowed))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("src", help="wide x64 eight-bucket v3 net")
    parser.add_argument("dst", help="narrow x32 eight-bucket v4 net")
    parser.add_argument("--rounding", choices=("away", "zero", "floor", "ceil", "even"),
                        default="away", help="odd-weight half-step rule (default: away)")
    args = parser.parse_args()

    data = bytearray(Path(args.src).read_bytes())
    if len(data) != EXPECTED_SIZE:
        raise SystemExit(f"{args.src}: expected {EXPECTED_SIZE} bytes, got {len(data)}")
    magic, version, features, hidden, buckets = struct.unpack_from("<4sHHHH", data)
    if (magic, version, features, hidden, buckets) != (b"NNUE", 3, FEATURES, HIDDEN, BUCKETS):
        raise SystemExit(f"{args.src}: expected an eight-bucket v3 net")

    w2_offset = 12 + W1_SIZE + HIDDEN
    wide = struct.unpack_from(f"<{W2_SIZE}b", data, w2_offset)
    narrow = [halve(value, args.rounding) for value in wide]
    struct.pack_into("<H", data, 4, 4)
    struct.pack_into(f"<{W2_SIZE}b", data, w2_offset, *narrow)
    Path(args.dst).write_bytes(data)

    exact = sum(1 for old, new in zip(wide, narrow) if old == 2 * new)
    clipped = sum(1 for old, new in zip(wide, narrow) if old == 127 and new == 63)
    print(
        f"{args.src}: v3 x64 -> {args.dst}: v4 x32, "
        f"range=[{min(narrow)},{max(narrow)}], exact={exact}/{W2_SIZE}, "
        f"positive_clips={clipped}, rounding={args.rounding}"
    )


if __name__ == "__main__":
    main()

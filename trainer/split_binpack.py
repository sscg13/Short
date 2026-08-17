#!/usr/bin/env python3
"""split_binpack.py - split a binpack into N parts at BINP-block boundaries.

    python split_binpack.py input.binpack outdir [--parts N] [--prefix NAME]

The binpack format is a stream of self-contained BINP blocks (each = "BINP" +
uint32 LE size + payload). A game (chain) never spans a block, so splitting at
block boundaries keeps every game whole and each output is a valid binpack in
its own right. Blocks are distributed ROUND-ROBIN so each part is a
statistically representative sample of the whole file (important for the
pc-spline two-pass resampler, which shapes each converted part's own
distribution).

Output files: <outdir>/<prefix>-00.binpack .. <outdir>/<prefix>-15.binpack
(exact block bytes preserved; sizes sum to the input). Pure stdlib; I/O bound.
"""

import argparse
import os
import sys
import time


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("input", help="input .binpack")
    ap.add_argument("outdir", help="output directory (created if missing)")
    ap.add_argument("--parts", type=int, default=16, help="number of parts (default 16)")
    ap.add_argument("--prefix", default="part", help="output name prefix (default 'part')")
    args = ap.parse_args()

    if args.parts < 1:
        sys.exit("--parts must be >= 1")
    os.makedirs(args.outdir, exist_ok=True)

    outs = []
    for i in range(args.parts):
        path = os.path.join(args.outdir, f"{args.prefix}-{i:02d}.binpack")
        outs.append(open(path, "wb"))

    t0 = time.time()
    nblocks = 0
    nbytes = 0
    with open(args.input, "rb") as fh:
        while True:
            magic = fh.read(4)
            if len(magic) == 0:
                break
            if magic != b"BINP":
                sys.exit(f"bad block magic {magic!r} at offset {fh.tell() - 4}")
            size_raw = fh.read(4)
            if len(size_raw) != 4:
                sys.exit("truncated block header")
            size = int.from_bytes(size_raw, "little")
            payload = fh.read(size)
            if len(payload) != size:
                sys.exit(f"truncated block: wanted {size}, got {len(payload)}")

            k = nblocks % args.parts          # round-robin
            outs[k].write(magic)
            outs[k].write(size_raw)
            outs[k].write(payload)
            nblocks += 1
            nbytes += size + 8
            if nblocks % 20000 == 0:
                sys.stderr.write(f"  {nblocks} blocks, {nbytes/1e9:.2f} GB, "
                                 f"{time.time()-t0:.0f}s\n")

    for f in outs:
        f.close()

    print(f"split {args.input}:")
    print(f"  blocks: {nblocks}, bytes: {nbytes:,}")
    for i, f in enumerate(outs):
        sz = os.path.getsize(os.path.join(args.outdir, f"{args.prefix}-{i:02d}.binpack"))
        print(f"  {args.prefix}-{i:02d}.binpack: {sz:,} bytes ({sz/1e9:.3f} GB)")
    print(f"done in {time.time()-t0:.1f}s")


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""Stream fixed-size SH01 record files into one deterministic interleaved file.

Each input keeps its internal order.  Blocks are emitted round-robin, so the
output mixes the source splits without loading the multi-gigabyte dataset into
memory.  The output is byte-compatible with the existing record contract.
"""

import argparse
import glob
import os
import struct
import sys

HEADER_SIZE = 16
RECORD_SIZE = 40
HEADER = struct.Struct("<4sIII")


def expand_inputs(patterns):
    paths = []
    for pattern in patterns:
        matches = sorted(glob.glob(pattern))
        paths.extend(matches if matches else [pattern])
    result = []
    seen = set()
    for path in paths:
        path = os.path.abspath(path)
        if path not in seen:
            seen.add(path)
            result.append(path)
    return result


def inspect(path):
    size = os.path.getsize(path)
    if size < HEADER_SIZE:
        raise ValueError(f"{path}: truncated SH01 header")
    with open(path, "rb") as src:
        raw = src.read(HEADER_SIZE)
    magic, record_size, count, reserved = HEADER.unpack(raw)
    if magic != b"SH01":
        raise ValueError(f"{path}: bad magic {magic!r}")
    if record_size != RECORD_SIZE:
        raise ValueError(f"{path}: record size {record_size}, expected {RECORD_SIZE}")
    if reserved != 0:
        raise ValueError(f"{path}: reserved header field is {reserved}")
    expected = HEADER_SIZE + count * RECORD_SIZE
    if size != expected:
        raise ValueError(f"{path}: size {size}, expected {expected}")
    return count


def interleave(inputs, output, block_records):
    counts = [inspect(path) for path in inputs]
    total = sum(counts)
    output = os.path.abspath(output)
    if output in inputs:
        raise ValueError("output must not also be an input")
    parent = os.path.dirname(output)
    if parent:
        os.makedirs(parent, exist_ok=True)
    temp = output + ".tmp"
    try:
        sources = [open(path, "rb") for path in inputs]
        for src in sources:
            src.seek(HEADER_SIZE)
        try:
            with open(temp, "wb") as dst:
                dst.write(HEADER.pack(b"SH01", RECORD_SIZE, total, 0))
                remaining = counts[:]
                while any(remaining):
                    for i, src in enumerate(sources):
                        take = min(block_records, remaining[i])
                        if not take:
                            continue
                        data = src.read(take * RECORD_SIZE)
                        if len(data) != take * RECORD_SIZE:
                            raise ValueError(f"{inputs[i]}: truncated record data")
                        dst.write(data)
                        remaining[i] -= take
        finally:
            for src in sources:
                src.close()
        os.replace(temp, output)
    except Exception:
        try:
            os.remove(temp)
        except FileNotFoundError:
            pass
        raise
    return counts, total


def main():
    parser = argparse.ArgumentParser(
        description="stream SH01 records into one deterministic block-interleaved file"
    )
    parser.add_argument(
        "inputs", nargs="+", help="input .records files or glob patterns"
    )
    parser.add_argument("--output", required=True, help="unified output .records file")
    parser.add_argument(
        "--block-records",
        type=int,
        default=65536,
        help="records emitted from each source per round (default: 65536)",
    )
    args = parser.parse_args()
    if args.block_records < 1:
        parser.error("--block-records must be positive")
    inputs = expand_inputs(args.inputs)
    if not inputs:
        parser.error("no input files")
    try:
        counts, total = interleave(inputs, args.output, args.block_records)
    except (OSError, ValueError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1
    print(f"inputs: {len(inputs)}")
    print(f"records: {total}")
    print(f"block: {args.block_records}")
    for path, count in zip(inputs, counts):
        print(f"  {os.path.basename(path)}: {count}")
    print(f"output: {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

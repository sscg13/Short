"""One-time chunk-local shuffle for SH01 record files."""

import argparse
import os
import struct
from pathlib import Path

import torch


HEADER = 16
RECORD = 40


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input")
    parser.add_argument("output")
    parser.add_argument("--chunk-records", type=int, default=1_048_576)
    parser.add_argument("--seed", type=int, default=1)
    args = parser.parse_args()
    if args.chunk_records < 1:
        parser.error("chunk-records must be positive")

    with open(args.input, "rb") as source:
        magic, record_size, count, reserved = struct.unpack("<4sIII", source.read(HEADER))
    if (magic, record_size, reserved) != (b"SH01", RECORD, 0):
        raise SystemExit("invalid SH01 input")

    mapped = torch.from_file(
        args.input, shared=False, size=HEADER + count * RECORD, dtype=torch.uint8
    )
    generator = torch.Generator(device="cpu").manual_seed(args.seed)
    output = Path(args.output)
    output.parent.mkdir(parents=True, exist_ok=True)
    temp = output.with_suffix(output.suffix + ".tmp")
    with open(temp, "wb") as target:
        target.write(struct.pack("<4sIII", b"SH01", RECORD, count, 0))
        for first in range(0, count, args.chunk_records):
            size = min(args.chunk_records, count - first)
            chunk = mapped[HEADER + first * RECORD : HEADER + (first + size) * RECORD]
            order = torch.randperm(size, generator=generator)
            target.write(chunk.view(size, RECORD)[order].contiguous().numpy().tobytes())
            print(f"shuffled {first + size}/{count}")
    os.replace(temp, output)
    print(f"output={output} records={count} chunk={args.chunk_records}")


if __name__ == "__main__":
    main()

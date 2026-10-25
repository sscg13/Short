"""Atomically replace trainer outputs, retrying transient filesystem failures."""
import json
import os
from pathlib import Path
import time


def atomic_write(path, writer):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(f"{path.name}.{os.getpid()}.pending")
    written = False
    for attempt in range(10):
        try:
            if not written:
                writer(temporary)
                written = True
            os.replace(temporary, path)
            return
        except OSError:
            if attempt == 9:
                raise
            time.sleep(min(0.1 * 2 ** attempt, 1))


def write_bytes(path, value):
    atomic_write(path, lambda temporary: temporary.write_bytes(value))


def write_json(path, value):
    write_bytes(path, (json.dumps(value, indent=2, allow_nan=False) + "\n").encode("utf-8"))


def save_checkpoint(path, value):
    import torch
    atomic_write(path, lambda temporary: torch.save(value, temporary))

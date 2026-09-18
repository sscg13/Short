"""PyTorch training loop for the 704->128->8 material-bucket SCReLU^2 NNUE."""

import argparse
import os
import struct
import time
from pathlib import Path

import torch

import nnue_extension


FEATURES = 704
HIDDEN = 64
OUTPUT_BUCKETS = 8
OUTPUT_STRIDE = 2 * HIDDEN
W2_MIN = -64
W2_MAX = 63
RECORD_SIZE = 40
HEADER_SIZE = 16
PARAM_COUNT = FEATURES * HIDDEN + HIDDEN + OUTPUT_BUCKETS * OUTPUT_STRIDE + OUTPUT_BUCKETS
W1_END = FEATURES * HIDDEN
B1_END = W1_END + HIDDEN
W2_END = B1_END + OUTPUT_BUCKETS * OUTPUT_STRIDE
BIAS_END = W2_END + OUTPUT_BUCKETS


def read_records(path, limit=0):
    with open(path, "rb") as source:
        magic, record_size, count, reserved = struct.unpack("<4sIII", source.read(16))
    if magic != b"SH01" or record_size != RECORD_SIZE or reserved != 0:
        raise ValueError(f"invalid record header: {path}")
    if limit:
        count = min(count, limit)
    mapped = torch.from_file(
        path, shared=False, size=HEADER_SIZE + count * RECORD_SIZE, dtype=torch.uint8
    )
    return mapped[HEADER_SIZE:].view(count, RECORD_SIZE), count


def round_away_from_zero(values):
    return torch.where(values >= 0, torch.floor(values + 0.5), torch.ceil(values - 0.5))


def signed_bytes(values, low, high):
    quantized = round_away_from_zero(values).clamp(low, high).to(torch.int16)
    return bytes((int(value) & 0xFF for value in quantized.cpu().tolist()))


def export_net(weights, path):
    weights = weights.detach().cpu()
    w1 = signed_bytes(weights[:W1_END], -128, 127)
    b1 = signed_bytes(weights[W1_END:B1_END], -128, 127)
    w2 = signed_bytes(weights[B1_END:W2_END], W2_MIN, W2_MAX)
    biases = round_away_from_zero(weights[W2_END:BIAS_END]).clamp(-32768, 32767)
    biases = [int(value) for value in biases.cpu().tolist()]
    output = Path(path)
    output.parent.mkdir(parents=True, exist_ok=True)
    temp = output.with_suffix(output.suffix + ".tmp")
    with open(temp, "wb") as target:
        target.write(struct.pack("<4sHHHH", b"NNUE", 4, FEATURES, HIDDEN, OUTPUT_BUCKETS))
        target.write(w1)
        target.write(b1)
        target.write(w2)
        target.write(struct.pack(f"<{OUTPUT_BUCKETS}h", *biases))
    os.replace(temp, output)


def save_checkpoint(path, weights, optimizer, generator, next_epoch, args,
                    final_lr, best_val_loss):
    """Atomically save the full-precision training state for an exact resume."""
    output = Path(path)
    output.parent.mkdir(parents=True, exist_ok=True)
    temp = output.with_suffix(output.suffix + ".tmp")
    state = {
        "format": "short-nnue-trainer-v2-narrow-w2",
        "weights": weights.detach().cpu(),
        "optimizer": optimizer.state_dict(),
        "generator_state": generator.get_state(),
        "next_epoch": next_epoch,
        "epochs": args.epochs,
        "lr": args.lr,
        "final_lr": final_lr,
        "weight_decay": args.weight_decay,
        "lambda": args.lambda_value,
        "batch_size": args.batch_size,
        "chunk_records": args.chunk_records,
        "shuffle_chunks": args.shuffle_chunks,
        "local_shuffle": not args.no_local_shuffle,
        "threads": args.threads,
        "best_val_loss": best_val_loss,
    }
    torch.save(state, temp)
    os.replace(temp, output)


def load_checkpoint(path, args, final_lr):
    """Load and validate state whose schedule continues under this command."""
    state = torch.load(path, map_location="cpu", weights_only=True)
    if state.get("format") != "short-nnue-trainer-v2-narrow-w2":
        raise ValueError(f"unsupported training checkpoint: {path}")
    expected = {
        "epochs": args.epochs,
        "lr": args.lr,
        "final_lr": final_lr,
        "weight_decay": args.weight_decay,
        "lambda": args.lambda_value,
        "batch_size": args.batch_size,
        "chunk_records": args.chunk_records,
        "shuffle_chunks": args.shuffle_chunks,
        "local_shuffle": not args.no_local_shuffle,
        "threads": args.threads,
    }
    for key, value in expected.items():
        if state.get(key) != value:
            raise ValueError(
                f"checkpoint {key}={state.get(key)!r}, command requests {value!r}"
            )
    weights = state.get("weights")
    if not isinstance(weights, torch.Tensor) or weights.numel() != PARAM_COUNT:
        raise ValueError(f"invalid checkpoint weights: {path}")
    next_epoch = int(state.get("next_epoch", -1))
    if next_epoch < 0 or next_epoch > args.epochs:
        raise ValueError(f"invalid checkpoint epoch: {next_epoch}")
    return state, weights.to(torch.float32).clone(), next_epoch


def clamp_quantized_ranges_(weights):
    """Keep the QAT parameters in the exact ranges representable by the net."""
    with torch.no_grad():
        weights[:B1_END].clamp_(-128.0, 127.0)
        weights[B1_END:W2_END].clamp_(W2_MIN, W2_MAX)
        weights[W2_END:BIAS_END].clamp_(-32768.0, 32767.0)


def load_net(path):
    with open(path, "rb") as source:
        raw = source.read()
    magic, version, features, hidden, reserved = struct.unpack("<4sHHHH", raw[:12])
    if magic != b"NNUE" or features != FEATURES or hidden != HIDDEN:
        raise ValueError(f"unsupported net header: {path}")
    if version == 2 and reserved == 0:
        buckets = 1
    elif version in (3, 4) and reserved == OUTPUT_BUCKETS:
        buckets = OUTPUT_BUCKETS
    else:
        raise ValueError(f"unsupported net header: {path}")
    expected = 12 + W1_END + HIDDEN + buckets * OUTPUT_STRIDE + 2 * buckets
    if len(raw) != expected:
        raise ValueError(f"unexpected net size: {path}")
    offset = 12
    w1 = torch.tensor(struct.unpack(f"<{W1_END}b", raw[offset : offset + W1_END]))
    offset += W1_END
    b1 = torch.tensor(struct.unpack(f"<{HIDDEN}b", raw[offset : offset + HIDDEN]))
    offset += HIDDEN
    count = buckets * OUTPUT_STRIDE
    w2 = torch.tensor(struct.unpack(f"<{count}b", raw[offset : offset + count]))
    offset += count
    biases = torch.tensor(struct.unpack(f"<{buckets}h", raw[offset : offset + 2 * buckets]))
    if buckets == 1:
        w2 = w2.repeat(OUTPUT_BUCKETS)
        biases = biases.repeat(OUTPUT_BUCKETS)
    if version != 4:
        w2 = w2 * 0.5
    return torch.cat((w1, b1, w2, biases)).to(torch.float32)


def initial_weights(path, seed):
    if path:
        return load_net(path)
    generator = torch.Generator(device="cpu").manual_seed(seed)
    weights = torch.empty(PARAM_COUNT, dtype=torch.float32)
    with torch.no_grad():
        weights[:W1_END].normal_(0.0, 1.0, generator=generator)
        weights[W1_END:B1_END].fill_(32.0)
        weights[B1_END:W2_END].normal_(0.0, 0.5, generator=generator)
        weights[W2_END:BIAS_END].zero_()
    return weights


def run_batches(records, indices, weights, gradients, batch_size, lambda_value, train, optimizer=None):
    total_loss = 0.0
    total_count = 0
    for start in range(0, indices.numel(), batch_size):
        batch_indices = indices[start : start + batch_size]
        if train:
            optimizer.zero_grad(set_to_none=True)
        loss = nnue_extension.process_batch(
            records, batch_indices, weights, gradients, lambda_value
        )
        if train:
            weights.grad = gradients
            optimizer.step()
            clamp_quantized_ranges_(weights)
        count = batch_indices.numel()
        total_loss += loss * count
        total_count += count
    return total_loss / max(1, total_count)


def run_sequential_range(records, first, count, weights, gradients, batch_size, lambda_value):
    total_loss = 0.0
    total_count = 0
    for start in range(0, count, batch_size):
        size = min(batch_size, count - start)
        loss = nnue_extension.process_range(
            records, first + start, size, weights, gradients, lambda_value
        )
        total_loss += loss * size
        total_count += size
    return total_loss / max(1, total_count)


def train_sequential_range(records, first, count, weights, gradients, batch_size,
                           lambda_value, optimizer):
    """Train directly from a contiguous (normally pre-shuffled) record range."""
    total_loss = 0.0
    total_count = 0
    for start in range(0, count, batch_size):
        size = min(batch_size, count - start)
        optimizer.zero_grad(set_to_none=True)
        loss = nnue_extension.process_range(
            records, first + start, size, weights, gradients, lambda_value
        )
        weights.grad = gradients
        optimizer.step()
        clamp_quantized_ranges_(weights)
        total_loss += loss * size
        total_count += size
    return total_loss / max(1, total_count)


def run_chunked_epoch(records, count, chunk_records, generator, weights, gradients,
                      batch_size, lambda_value, optimizer, shuffle_chunks, shuffle_within):
    chunk_count = (count + chunk_records - 1) // chunk_records
    chunk_order = (torch.randperm(chunk_count, generator=generator)
                   if shuffle_chunks else torch.arange(chunk_count))
    total_loss = 0.0
    total_count = 0
    for chunk_id in chunk_order.tolist():
        first = chunk_id * chunk_records
        size = min(chunk_records, count - first)
        if shuffle_within:
            indices = torch.randperm(size, generator=generator, dtype=torch.int64) + first
            loss = run_batches(
                records, indices, weights, gradients, batch_size, lambda_value, True, optimizer
            )
        else:
            loss = train_sequential_range(
                records, first, size, weights, gradients, batch_size,
                lambda_value, optimizer
            )
        total_loss += loss * size
        total_count += size
    return total_loss / max(1, total_count)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("records", help="SH01 record file, preferably the unified file")
    parser.add_argument("--output", default="nets/trained-v2.net")
    parser.add_argument("--best-output", default=None,
                        help="optional path for the lowest-validation-loss checkpoint")
    parser.add_argument("--checkpoint-output", default=None,
                        help="atomically save full optimizer state after every epoch")
    parser.add_argument("--resume-checkpoint", default=None,
                        help="resume full-precision weights, optimizer, RNG, and epoch schedule")
    parser.add_argument("--snapshot-every", type=int, default=0,
                        help="export an epoch-numbered checkpoint every N epochs")
    parser.add_argument("--init-net", default=None,
                        help="optional v2/v3/v4 net to initialize or fine-tune")
    parser.add_argument("--evaluate-only", action="store_true",
                        help="report validation loss for the initialized net without training")
    parser.add_argument("--epochs", type=int, default=1)
    parser.add_argument("--batch-size", type=int, default=4096)
    parser.add_argument("--chunk-records", type=int, default=1_048_576,
                        help="runtime shuffle chunk size (default: 1048576)")
    parser.add_argument("--shuffle-chunks", action="store_true",
                        help="randomize chunk order instead of using sequential disk access")
    parser.add_argument("--no-local-shuffle", action="store_true",
                        help="do not shuffle records within chunks (for pre-shuffled files)")
    parser.add_argument("--max-records", type=int, default=0)
    parser.add_argument("--val-fraction", type=float, default=0.01)
    parser.add_argument("--lr", type=float, default=8.75e-4)
    parser.add_argument("--final-lr", type=float, default=None,
                        help="final LR for linear epoch decay (default: same as --lr)")
    parser.add_argument("--weight-decay", type=float, default=0.0)
    parser.add_argument("--lambda", dest="lambda_value", type=float, default=1.0)
    parser.add_argument("--threads", type=int, default=4,
                        help="C++ gradient worker threads (default: 4)")
    parser.add_argument("--seed", type=int, default=1)
    args = parser.parse_args()
    if args.init_net and args.resume_checkpoint:
        parser.error("--init-net and --resume-checkpoint are mutually exclusive")
    if args.epochs < 1 or args.batch_size < 1 or args.chunk_records < 1:
        parser.error("epochs, batch-size, and chunk-records must be positive")
    if args.snapshot_every < 0:
        parser.error("snapshot-every must be non-negative")
    if args.weight_decay < 0.0:
        parser.error("weight-decay must be non-negative")
    if args.final_lr is not None and args.final_lr < 0.0:
        parser.error("final-lr must be non-negative")
    if args.threads < 0:
        parser.error("threads must be non-negative")
    if not 0.0 <= args.val_fraction < 1.0:
        parser.error("val-fraction must be in [0, 1)")
    if not 0.0 <= args.lambda_value <= 1.0:
        parser.error("lambda must be in [0, 1]")

    torch.set_num_threads(max(1, torch.get_num_threads()))
    if args.threads:
        nnue_extension.set_threads(args.threads)
    records, count = read_records(args.records, args.max_records)
    generator = torch.Generator(device="cpu").manual_seed(args.seed)
    val_count = int(count * args.val_fraction)
    train_count = count - val_count
    final_lr = args.lr if args.final_lr is None else args.final_lr
    checkpoint = None
    start_epoch = 0
    if args.resume_checkpoint:
        checkpoint, weights, start_epoch = load_checkpoint(
            args.resume_checkpoint, args, final_lr
        )
        generator.set_state(checkpoint["generator_state"])
    else:
        weights = initial_weights(args.init_net, args.seed)
    weights.requires_grad_(True)
    gradients = torch.empty_like(weights)
    if args.evaluate_only:
        if not val_count:
            parser.error("--evaluate-only requires a nonzero validation split")
        started = time.perf_counter()
        with torch.no_grad():
            val_loss = run_sequential_range(
                records, train_count, val_count, weights, gradients,
                args.batch_size, args.lambda_value
            )
        elapsed = time.perf_counter() - started
        print(f"records={count} validation={val_count} parameters={PARAM_COUNT} "
              f"lambda={args.lambda_value} threads={args.threads}")
        print(f"validation={val_loss:.9g} seconds={elapsed:.1f} "
              f"positions_per_second={val_count / elapsed:.0f}")
        return
    optimizer = torch.optim.AdamW([weights], lr=args.lr, weight_decay=args.weight_decay)
    if checkpoint is not None:
        optimizer.load_state_dict(checkpoint["optimizer"])

    print(f"records={count} train={train_count} validation={val_count}")
    print(f"parameters={PARAM_COUNT} batch={args.batch_size} chunk={args.chunk_records} "
          f"lambda={args.lambda_value} threads={args.threads}")
    best_val_loss = (float(checkpoint.get("best_val_loss", float("inf")))
                     if checkpoint is not None else float("inf"))
    if checkpoint is not None:
        print(f"resumed={args.resume_checkpoint} next_epoch={start_epoch + 1}")
    for epoch in range(start_epoch, args.epochs):
        started = time.perf_counter()
        fraction = epoch / max(1, args.epochs - 1)
        current_lr = args.lr + (final_lr - args.lr) * fraction
        for group in optimizer.param_groups:
            group["lr"] = current_lr
        train_loss = run_chunked_epoch(
            records, train_count, args.chunk_records, generator, weights, gradients,
            args.batch_size, args.lambda_value, optimizer, args.shuffle_chunks,
            not args.no_local_shuffle
        )
        with torch.no_grad():
            val_loss = run_sequential_range(
                records, train_count, val_count, weights, gradients,
                args.batch_size, args.lambda_value
            ) if val_count else float("nan")
        elapsed = time.perf_counter() - started
        print(f"epoch={epoch + 1} lr={current_lr:.9g} train={train_loss:.9g} "
              f"val={val_loss:.9g} seconds={elapsed:.1f} "
              f"positions_per_second={count / elapsed:.0f}")
        export_net(weights, args.output)
        print(f"exported={args.output}")
        if args.snapshot_every and (epoch + 1) % args.snapshot_every == 0:
            output = Path(args.output)
            snapshot = output.with_name(
                f"{output.stem}-epoch{epoch + 1}{output.suffix}"
            )
            export_net(weights, snapshot)
            print(f"snapshot_exported={snapshot}")
        if args.best_output is not None and val_count and val_loss < best_val_loss:
            best_val_loss = val_loss
            export_net(weights, args.best_output)
            print(f"best_exported={args.best_output} best_val={best_val_loss:.9g}")
        if args.checkpoint_output is not None:
            save_checkpoint(args.checkpoint_output, weights, optimizer, generator,
                            epoch + 1, args, final_lr, best_val_loss)
            print(f"checkpoint_saved={args.checkpoint_output} next_epoch={epoch + 2}")


if __name__ == "__main__":
    main()

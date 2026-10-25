"""CPU NNUE training with QAT backpropagation or deterministic factored FD."""
import argparse
import atexit
import hashlib
import json
import math
from pathlib import Path
import struct
import sys
import time

import torch
import nnue_extension
from persistence import save_checkpoint, write_bytes, write_json

FEATURES, HIDDEN, NET_VERSION = 704, 64, 5
RECORD_SIZE, HEADER_SIZE = 40, 16
W1_END = FEATURES * HIDDEN
B1_END = W1_END + HIDDEN
W2_END = B1_END + 2 * HIDDEN
PARAM_COUNT = W2_END + 1
if (getattr(nnue_extension, "NET_VERSION", None) != NET_VERSION or
        getattr(nnue_extension, "TRAINER_API_VERSION", None) != 2):
    raise RuntimeError("Rebuild nnue_extension: python setup.py build_ext --inplace")
atexit.register(nnue_extension.shutdown)


def read_records(path, limit=0):
    path = Path(path)
    with path.open("rb") as source:
        header = source.read(HEADER_SIZE)
    if len(header) != HEADER_SIZE:
        raise ValueError(f"truncated record header: {path}")
    magic, size, count, reserved = struct.unpack("<4sIII", header)
    if (magic, size, reserved) != (b"SH01", RECORD_SIZE, 0):
        raise ValueError(f"invalid record header: {path}")
    if not count or path.stat().st_size != HEADER_SIZE + count * RECORD_SIZE:
        raise ValueError(f"empty or incorrectly sized record file: {path}")
    if limit < 0:
        raise ValueError("record limit must be non-negative")
    count = min(count, limit) if limit else count
    mapped = torch.from_file(str(path), shared=False,
                             size=HEADER_SIZE + count * RECORD_SIZE, dtype=torch.uint8)
    return mapped[HEADER_SIZE:].view(count, RECORD_SIZE), count


def curve_count(weights):
    extra = weights.numel() - PARAM_COUNT
    if extra not in (0, 256, HIDDEN * 256):
        raise ValueError("unexpected parameter count")
    return extra // 256


def round_away_from_zero(values):
    return torch.where(values >= 0, torch.floor(values + 0.5), torch.ceil(values - 0.5))


def clamp_quantized_ranges_(weights):
    with torch.no_grad():
        weights[:W2_END].clamp_(-128, 127)
        weights[W2_END:PARAM_COUNT].clamp_(-32768, 32767)
        weights[PARAM_COUNT:].clamp_(-65025, 65025)
        weights[PARAM_COUNT::256] = 0


def export_net(weights, path):
    curves = curve_count(weights)
    if not torch.isfinite(weights).all():
        raise ValueError("cannot export non-finite weights")
    q = round_away_from_zero(weights.detach().cpu())
    q[:W2_END].clamp_(-128, 127)
    q[W2_END:PARAM_COUNT].clamp_(-32768, 32767)
    if curves and (q[PARAM_COUNT:].abs().max() > 65025 or q[PARAM_COUNT::256].any()):
        raise ValueError("invalid activation response or nonzero origin")
    q = q.to(torch.int32)
    version = 8 if curves == HIDDEN else 7 if curves else NET_VERSION
    data = struct.pack("<4sHHHH", b"NNUE", version, FEATURES, HIDDEN, 0)
    data += struct.pack(f"<{W2_END}b", *q[:W2_END].tolist())
    data += struct.pack("<h", q[W2_END].item())
    if curves:
        data += struct.pack(f"<{curves * 256}i", *q[PARAM_COUNT:].tolist())
    write_bytes(path, data)
    return hashlib.sha256(data).hexdigest()


def load_net(path):
    data = Path(path).read_bytes()
    if len(data) < 12:
        raise ValueError(f"truncated net: {path}")
    magic, version, features, hidden, reserved = struct.unpack("<4sHHHH", data[:12])
    if ((magic, features, hidden, reserved) != (b"NNUE", FEATURES, HIDDEN, 0)
            or version not in (2, 5, 7, 8)):
        raise ValueError(f"unsupported net header: {path}")
    curves = HIDDEN if version == 8 else 1 if version == 7 else 0
    if len(data) != 12 + W2_END + 2 + curves * 1024:
        raise ValueError(f"unexpected net size: {path}")
    base = list(struct.unpack(f"<{W2_END}b", data[12:12 + W2_END]))
    base += list(struct.unpack("<h", data[12 + W2_END:14 + W2_END]))
    table = list(struct.unpack(f"<{curves * 256}i", data[14 + W2_END:]))
    if any(table[::256]) or any(abs(v) > 65025 for v in table):
        raise ValueError(f"invalid activation response or nonzero origin: {path}")
    weights = torch.tensor(base + table, dtype=torch.float32)
    if version == 2:
        rows = weights[:W1_END].view(FEATURES, HIDDEN)
        original = rows.clone()
        for rank in range(1, 8):
            for file in range(4):
                rows[rank * 4 + file].copy_(original[rank * 8 + file])
    return weights


def initial_weights(path, seed, curves=0):
    if path:
        weights = load_net(path)
        source_curves = curve_count(weights)
        if source_curves == curves:
            return weights
        if source_curves == 1 and curves == HIDDEN:
            return torch.cat((weights[:PARAM_COUNT], weights[PARAM_COUNT:].repeat(HIDDEN)))
        if source_curves:
            raise ValueError("cannot discard or merge custom activations; select a compatible activation mode")
    else:
        generator = torch.Generator().manual_seed(seed)
        weights = torch.empty(PARAM_COUNT, dtype=torch.float32)
        weights[:W1_END].normal_(0, 1, generator=generator)
        weights[W1_END:B1_END].fill_(32)
        weights[B1_END:W2_END].normal_(0, 0.5, generator=generator)
        weights[W2_END] = 0
    table = torch.arange(256, dtype=torch.float32).square().repeat(curves)
    return torch.cat((weights, table)) if curves else weights


class Backend:
    def __init__(self, args):
        self.options = dict(method=args.method, curves=args.curves,
                            freeze_curve=args.freeze_activation,
                            accumulator_step=args.accumulator_step,
                            response_step=args.response_step, exponent=args.loss_exponent)
        self.lambda_value = args.lambda_value
        nnue_extension.set_threads(args.threads or 1)

    def batch(self, records, weights, gradients, training=True, indices=None):
        if indices is None:
            return nnue_extension.process_range(records, 0, len(records), weights, gradients,
                                                 self.lambda_value, training=training, **self.options)
        return nnue_extension.process_batch(records, indices, weights, gradients,
                                             self.lambda_value, training=training, **self.options)


def scheduled_lr(initial_lr, final_lr, fraction, schedule):
    if schedule == "cosine":
        blend = 0.5 * (1 + math.cos(math.pi * fraction))
        return final_lr + (initial_lr - final_lr) * blend
    return initial_lr + (final_lr - initial_lr) * fraction


def make_optimizer(weights, args):
    base = weights[:PARAM_COUNT].detach().requires_grad_(True)
    groups = [{"params": [base], "lr": args.lr, "lr_scale": 1.0}]
    activation = None
    if args.curves and not args.freeze_activation:
        activation = weights[PARAM_COUNT:].detach().requires_grad_(True)
        groups.append({"params": [activation], "lr": args.lr * args.activation_lr_multiplier,
                       "lr_scale": args.activation_lr_multiplier})
    optimizer = torch.optim.AdamW(groups, lr=args.lr, weight_decay=args.weight_decay)
    return optimizer, base, activation


def evaluate(backend, records, weights, gradients, batch_size):
    if not len(records):
        return None
    total = 0.0
    for start in range(0, len(records), batch_size):
        batch = records[start:start + batch_size]
        total += backend.batch(batch, weights, gradients, training=False) * len(batch)
    return total / len(records)


def train_epoch(backend, records, weights, gradients, optimizer, base, activation, generator, args):
    chunks = (len(records) + args.chunk_records - 1) // args.chunk_records
    order = torch.randperm(chunks, generator=generator) if args.shuffle_chunks else torch.arange(chunks)
    total, steps = 0.0, 0
    for chunk in order.tolist():
        first = chunk * args.chunk_records
        part = records[first:min(first + args.chunk_records, len(records))]
        indices = torch.randperm(len(part), generator=generator) if not args.no_local_shuffle else None
        for start in range(0, len(part), args.batch_size):
            optimizer.zero_grad(set_to_none=True)
            selected = indices[start:start + args.batch_size] if indices is not None else None
            batch = part if selected is not None else part[start:start + args.batch_size]
            loss = backend.batch(batch, weights, gradients, indices=selected)
            base.grad = gradients[:PARAM_COUNT]
            if activation is not None:
                activation.grad = gradients[PARAM_COUNT:]
            optimizer.step()
            clamp_quantized_ranges_(weights)
            count = len(selected) if selected is not None else len(batch)
            total += loss * count
            steps += 1
    if not torch.isfinite(weights).all() or not torch.isfinite(gradients).all():
        raise RuntimeError("non-finite training state")
    digest = hashlib.sha256(json.dumps(order.tolist()).encode()).hexdigest()
    return total / len(records), steps, digest


CONFIG_FIELDS = ("method", "activation", "epochs", "batch_size", "chunk_records", "shuffle_chunks",
                 "no_local_shuffle", "max_records", "val_fraction", "lr", "final_lr", "lr_schedule",
                 "weight_decay", "lambda_value", "threads", "seed", "activation_lr_multiplier",
                 "freeze_activation", "accumulator_step", "response_step", "loss_exponent")


def parse_args(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("records", type=Path, help="SH01 records; use a pre-shuffled file for the FD50 recipe")
    parser.add_argument("--output", type=Path, default=Path("nets/trained.net"))
    parser.add_argument("--best-output", type=Path)
    parser.add_argument("--checkpoint", type=Path, help="optimizer/RNG checkpoint (default: output with .pt suffix)")
    parser.add_argument("--metrics", type=Path, help="epoch metrics (default: output with .json suffix)")
    parser.add_argument("--resume", type=Path, help="restore saved recipe, optimizer and shuffle state")
    parser.add_argument("--init-net", type=Path, help="v2/v5/v7/v8 net to fine-tune")
    parser.add_argument("--method", choices=("qat", "factored-fd"), default="qat")
    parser.add_argument("--activation", choices=("relu2", "shared", "per-neuron"))
    parser.add_argument("--activation-lr-multiplier", type=float, default=32)
    parser.add_argument("--freeze-activation", action="store_true")
    parser.add_argument("--accumulator-step", type=int, default=4)
    parser.add_argument("--response-step", type=int, default=256)
    parser.add_argument("--loss-exponent", type=float, default=2.5)
    parser.add_argument("--epochs", type=int, default=1)
    parser.add_argument("--batch-size", type=int, default=16384)
    parser.add_argument("--chunk-records", type=int, default=1048576)
    parser.add_argument("--shuffle-chunks", action="store_true")
    parser.add_argument("--no-local-shuffle", action="store_true")
    parser.add_argument("--max-records", type=int, default=0)
    parser.add_argument("--val-fraction", type=float, default=0.01)
    parser.add_argument("--lr", type=float)
    parser.add_argument("--final-lr", type=float)
    parser.add_argument("--lr-schedule", choices=("linear", "cosine"), default="linear")
    parser.add_argument("--weight-decay", type=float, default=0)
    parser.add_argument("--lambda", dest="lambda_value", type=float, default=1)
    parser.add_argument("--threads", type=int, default=4)
    parser.add_argument("--seed", type=int, default=1)
    tokens = list(sys.argv[1:] if argv is None else argv)
    args = parser.parse_args(tokens)
    checkpoint = None
    if args.resume:
        if args.init_net:
            parser.error("resume already restores weights; omit --init-net")
        checkpoint = torch.load(args.resume, map_location="cpu", weights_only=True)
        if checkpoint.get("format") != 1:
            parser.error("unsupported trainer checkpoint")
        explicit = {token.split("=")[0] for token in tokens if token.startswith("--")}
        for key, value in checkpoint["config"].items():
            flag = "--lambda" if key == "lambda_value" else "--" + key.replace("_", "-")
            if flag in explicit and getattr(args, key) != value:
                parser.error(f"resume would change {flag}; use the saved training recipe")
            setattr(args, key, value)
    args.activation = args.activation or ("shared" if args.method == "factored-fd" else "relu2")
    args.curves = {"relu2": 0, "shared": 1, "per-neuron": HIDDEN}[args.activation]
    args.lr = args.lr if args.lr is not None else (0.1 if args.method == "factored-fd" else 8.75e-4)
    args.final_lr = args.lr if args.final_lr is None else args.final_lr
    if args.method == "qat" and args.curves:
        parser.error("custom activations require --method factored-fd")
    if min(args.epochs, args.batch_size, args.chunk_records) < 1 or min(args.threads, args.max_records) < 0:
        parser.error("epochs, batch-size and chunk-records must be positive; threads and max-records non-negative")
    for key in ("lr", "final_lr", "weight_decay", "activation_lr_multiplier"):
        if not math.isfinite(getattr(args, key)) or getattr(args, key) < 0:
            parser.error(f"{key.replace('_', '-')} must be finite and non-negative")
    if not 0 <= args.val_fraction < 1 or not 0 <= args.lambda_value <= 1:
        parser.error("val-fraction must be in [0,1), lambda in [0,1]")
    if not math.isfinite(args.loss_exponent) or args.loss_exponent < 1:
        parser.error("loss-exponent must be finite and at least 1")
    if not 1 <= args.accumulator_step <= 255 or not 1 <= args.response_step <= 65025:
        parser.error("FD accumulator step must be 1..255 and response step 1..65025")
    return args, checkpoint


def main(argv=None):
    args, checkpoint = parse_args(argv)
    torch.set_num_threads(1)
    records, count = read_records(args.records, args.max_records)
    val_count = int(count * args.val_fraction)
    train_count = count - val_count
    stat = args.records.stat()
    dataset = {"records": count, "bytes": stat.st_size, "mtime_ns": stat.st_mtime_ns,
               "path": str(args.records.resolve())}
    config = {key: getattr(args, key) for key in CONFIG_FIELDS}
    generator = torch.Generator().manual_seed(args.seed)
    if checkpoint:
        if checkpoint["dataset"] != dataset:
            raise ValueError("resume dataset metadata differs from the checkpoint")
        weights = checkpoint["weights"].clone()
        if curve_count(weights) != args.curves:
            raise ValueError("checkpoint parameter count differs from its activation mode")
        generator.set_state(checkpoint["generator_state"])
    else:
        weights = initial_weights(args.init_net, args.seed, args.curves)
    gradients = torch.empty_like(weights)
    optimizer, base, activation = make_optimizer(weights, args)
    if checkpoint:
        optimizer.load_state_dict(checkpoint["optimizer"])
    backend = Backend(args)
    history = checkpoint["epochs"][:] if checkpoint else []
    start_epoch = len(history)
    best = checkpoint["best_validation_loss"] if checkpoint else None
    best_weights = checkpoint["best_weights"] if checkpoint else None
    step = checkpoint["step"] if checkpoint else 0
    checkpoint_path = args.checkpoint or args.output.with_suffix(".pt")
    metrics_path = args.metrics or args.output.with_suffix(".json")
    # Fail before training if outputs would overwrite a dataset, input net or one another.
    destinations = [args.output, checkpoint_path, metrics_path] + ([args.best_output] if args.best_output else [])
    sources = [args.records] + ([args.init_net] if args.init_net else [])
    resolved = [p.resolve() for p in destinations]
    if len(set(resolved)) != len(resolved) or any(p.resolve() in resolved for p in sources):
        raise ValueError("output paths must be distinct from one another and from input files")
    if args.resume and any(p.resolve() == args.resume.resolve() for p in
                           [args.output, metrics_path] + ([args.best_output] if args.best_output else [])):
        raise ValueError("only the checkpoint output may replace the resume checkpoint")
    print(json.dumps({"method": args.method, "activation": args.activation, "train": train_count,
                      "validation": val_count, "parameters": weights.numel(), "resume_epoch": len(history)}), flush=True)
    for epoch in range(start_epoch, args.epochs):
        started = time.perf_counter()
        lr = scheduled_lr(args.lr, args.final_lr, epoch / max(1, args.epochs - 1), args.lr_schedule)
        for group in optimizer.param_groups:
            group["lr"] = lr * group["lr_scale"]
        train_loss, steps, order = train_epoch(backend, records[:train_count], weights, gradients,
                                               optimizer, base, activation, generator, args)
        step += steps
        val_loss = evaluate(backend, records[train_count:], weights, gradients, args.batch_size)
        digest = export_net(weights, args.output)
        row = {"epoch": epoch + 1, "lr": lr, "activation_lr": optimizer.param_groups[-1]["lr"] if activation is not None else 0,
               "steps": step, "train_loss": train_loss, "validation_loss": val_loss,
               "order_sha256": order, "net_sha256": digest, "seconds": time.perf_counter() - started}
        history.append(row)
        if val_loss is not None and (best is None or val_loss < best):
            best = val_loss
            best_weights = weights.detach().clone()
            if args.best_output:
                export_net(weights, args.best_output)
        saved = {"format": 1, "config": config, "dataset": dataset, "weights": weights.detach(),
                 "optimizer": optimizer.state_dict(), "generator_state": generator.get_state(),
                 "step": step, "epochs": history, "best_validation_loss": best,
                 "best_weights": best_weights}
        save_checkpoint(checkpoint_path, saved)
        write_json(metrics_path, {"config": config, "dataset": dataset, "epochs": history,
                                  "best_validation_loss": best})
        print(json.dumps(row), flush=True)
    # Recreate outputs even when resuming an already completed checkpoint.
    if checkpoint and start_epoch == args.epochs:
        export_net(weights, args.output)
        save_checkpoint(checkpoint_path, checkpoint)
        write_json(metrics_path, {"config": config, "dataset": dataset, "epochs": history,
                                  "best_validation_loss": best})
    if args.best_output and best_weights is not None:
        export_net(best_weights, args.best_output)


if __name__ == "__main__":
    main()

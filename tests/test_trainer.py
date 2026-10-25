"""Independent scalar credit checks, format checks and interrupted-run replay.

Build trainer/nnue_extension first, then run: python tests/test_trainer.py
"""
import contextlib
from concurrent.futures import ThreadPoolExecutor
import io
import math
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "trainer"))
import torch
import trainer as t
import persistence


FENS = (
    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w",
    "r3k2r/ppp2ppp/2n1bn2/3qp3/3P4/2N1PN2/PPP1BPPP/R2Q1RK1 b",
    "6k1/5p2/8/3P4/8/8/1P6/1K6 w",
    "1k6/1p6/8/8/3p4/8/5P2/6K1 b",
    "8/8/8/8/8/8/4k3/6K1 w",
)


def fixtures():
    records = []
    for i, fen in enumerate(FENS):
        row = bytearray(40)
        board, stm = fen.split()
        for rank, text in enumerate(reversed(board.split("/"))):
            file = 0
            for char in text:
                if char.isdigit():
                    file += int(char)
                else:
                    square = rank * 8 + file
                    piece = ".PNBRQKpnbrqk".index(char)
                    row[square // 2] |= piece << (4 * (square % 2))
                    if piece in (6, 12):
                        row[33 if piece == 6 else 34] = square
                    file += 1
        row[32], row[37] = int(stm == "b"), i % 3
        row[38:40] = struct.pack("<h", 137 * i - 220)
        records.append(list(row))
    return torch.tensor(records, dtype=torch.uint8)


def feature_rows(record):
    """Direct board scan with independent color/perspective normalization."""
    result = [[], []]
    for perspective in range(2):
        king = record[33 + perspective]
        king = 63 - king if perspective else king
        for square in range(64):
            piece = (record[square // 2] >> (4 * (square % 2))) & 15
            if not piece:
                continue
            enemy = (piece > 6) != bool(perspective)
            kind = (piece - 1) % 6 + 1
            compact = 63 - square if perspective else square
            if king % 8 >= 4:
                compact ^= 7
            rank, file = divmod(compact, 8)
            if kind == 6:
                row = 336 + compact if enemy else rank * 4 + file
            elif kind == 1:
                if rank not in range(1, 7):
                    continue
                row = (400 if enemy else 32) + (rank - 1) * 8 + file
            else:
                row = (448 if enemy else 80) + (kind - 2) * 64 + compact
            result[perspective].append(row)
    return result


def probability(score):
    a = 1 / (1 + math.exp(-(score - 256) / 256))
    b = 1 / (1 + math.exp(-(-score - 256) / 256))
    return (1 + a - b) / 2, (a * (1 - a) + b * (1 - b)) / 512


def scalar_credit(record, weights, method, curves, mixture=0.7, exponent=2.5,
                  accumulator_step=4, response_step=256):
    q = t.round_away_from_zero(weights).tolist()
    q[:t.W2_END] = [max(-128, min(127, int(v))) for v in q[:t.W2_END]]
    q[t.W2_END] = max(-32768, min(32767, int(q[t.W2_END])))
    rows = feature_rows(record)

    def response(j, a):
        offset = j * 256 if curves == 64 else 0
        return int(q[t.PARAM_COUNT + offset + a]) if curves else a * a

    accumulators = [[int(q[t.W1_END + j]) + sum(int(q[r * 64 + j]) for r in view)
                     for j in range(64)] for view in rows]
    raw = q[t.W2_END]
    for p in range(2):
        base = 0 if p == record[32] else 64
        for j, acc in enumerate(accumulators[p]):
            raw += (response(j, max(0, min(255, acc))) * int(q[t.B1_END + base + j])) >> 9
    score = int(raw) >> 5
    predicted, derivative = probability(score)
    teacher = struct.unpack("<h", bytes(record[38:40]))[0]
    target = mixture * probability(teacher)[0] + (1 - mixture) * record[37] / 2
    error = predicted - target
    loss = abs(error) ** exponent
    draw = math.copysign(exponent * abs(error) ** (exponent - 1), error) * derivative / 32
    gradient = [0.0] * len(q)
    gradient[t.W2_END] = draw
    for p in range(2):
        base = 0 if p == record[32] else 64
        for j, acc in enumerate(accumulators[p]):
            a = max(0, min(255, acc))
            value, w = response(j, a), int(q[t.B1_END + base + j])
            gradient[t.B1_END + base + j] += draw * value / 512
            if method == "qat":
                credit = draw * a * w / 256 if 0 < acc < 255 else 0
            else:
                up = response(j, max(0, min(255, acc + accumulator_step))) * w >> 9
                down = response(j, max(0, min(255, acc - accumulator_step))) * w >> 9
                credit = draw * (up - down) / (2 * accumulator_step)
                if curves and a:
                    up = max(-65025, min(65025, value + response_step)) * w >> 9
                    down = max(-65025, min(65025, value - response_step)) * w >> 9
                    offset = j * 256 if curves == 64 else 0
                    gradient[t.PARAM_COUNT + offset + a] += draw * (up - down) / (2 * response_step)
            gradient[t.W1_END + j] += credit
            for row in rows[p]:
                gradient[row * 64 + j] += credit
    return loss, torch.tensor(gradient, dtype=torch.float32)


class TrainerTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        torch.set_num_threads(1)
        cls.records = fixtures()

    def weights(self, curves):
        weights = t.initial_weights(None, 7, curves)
        weights[:t.W1_END].view(704, 64)[:, 0] = 127
        weights[:t.W1_END].view(704, 64)[:, 1] = -128
        weights[t.W1_END:t.B1_END] = torch.arange(64) * 4 - 128
        weights[t.B1_END:t.W2_END] = torch.tensor([((j * 5) % 17) - 8 or 1 for j in range(128)])
        for j in range(curves):
            table = [0] + [((a * 257 + (j + 3) * 1237) % 130051) - 65025 for a in range(1, 256)]
            weights[t.PARAM_COUNT + j * 256:t.PARAM_COUNT + (j + 1) * 256] = torch.tensor(table)
        return weights

    def batch(self, weights, records=None, **kwargs):
        records = self.records if records is None else records
        gradient = torch.full_like(weights, float("nan"))
        method = kwargs.pop("method", "factored-fd")
        loss = t.nnue_extension.process_range(records, 0, len(records), weights, gradient,
                                                kwargs.pop("mixture", 0.7), method=method,
                                                curves=t.curve_count(weights), **kwargs)
        return loss, gradient

    def test_scalar_gradients_and_loss(self):
        for method, curves in (("qat", 0), ("factored-fd", 0), ("factored-fd", 1), ("factored-fd", 64)):
            for threads in (1, 4):
                t.nnue_extension.set_threads(threads)
                weights = self.weights(curves)
                for record in self.records:
                    loss, gradient = self.batch(weights, record.view(1, 40), method=method)
                    expected_loss, expected = scalar_credit(record.tolist(), weights, method, curves)
                    self.assertAlmostEqual(loss, expected_loss, delta=2e-7)
                    torch.testing.assert_close(gradient, expected, rtol=4e-4, atol=2e-10)
        # Nondefault secant steps, loss exponent and mixed outcome/teacher target.
        weights = self.weights(64)
        loss, gradient = self.batch(weights, self.records[:1], accumulator_step=7,
                                    response_step=127, exponent=3.0, mixture=0.2)
        expected_loss, expected = scalar_credit(self.records[0].tolist(), weights, "factored-fd", 64,
                                                0.2, 3.0, 7, 127)
        self.assertAlmostEqual(loss, expected_loss, delta=2e-7)
        torch.testing.assert_close(gradient, expected, rtol=4e-4, atol=2e-10)

    def test_evaluation_freeze_and_indexed_batches(self):
        weights = self.weights(64)
        loss, gradient = self.batch(weights)
        frozen_loss, frozen = self.batch(weights, freeze_curve=True)
        self.assertEqual(loss, frozen_loss)
        self.assertTrue(torch.equal(gradient[:t.PARAM_COUNT], frozen[:t.PARAM_COUNT]))
        self.assertEqual(frozen[t.PARAM_COUNT:].count_nonzero(), 0)
        eval_loss, evaluation = self.batch(weights, training=False)
        self.assertEqual(loss, eval_loss)
        self.assertEqual(evaluation.count_nonzero(), 0)
        empty_loss, empty = self.batch(weights, self.records[:0])
        self.assertEqual(empty_loss, 0)
        self.assertEqual(empty.count_nonzero(), 0)
        indices = torch.tensor([4, 1, 4, 0, 2, 3], dtype=torch.int64)
        selected_loss, selected = self.batch(weights, self.records[indices].contiguous())
        indexed = torch.empty_like(weights)
        indexed_loss = t.nnue_extension.process_batch(self.records, indices, weights, indexed,
                                                      0.7, method="factored-fd", curves=64)
        self.assertEqual(selected_loss, indexed_loss)
        self.assertTrue(torch.equal(selected, indexed))

    def test_api_errors_and_worker_recovery(self):
        weights = self.weights(0)
        t.nnue_extension.set_threads(4)
        for kwargs in ({"method": "dust"}, {"accumulator_step": 0}, {"exponent": float("nan")}):
            with self.assertRaises(RuntimeError):
                self.batch(weights, **kwargs)
        with self.assertRaises(RuntimeError):
            self.batch(self.weights(1), method="qat")
        with self.assertRaises(RuntimeError):
            t.nnue_extension.process_range(self.records, 0, 1, weights, weights, 1.0)
        with self.assertRaises(RuntimeError):
            t.nnue_extension.process_range(self.records, 4, 2, weights, torch.empty_like(weights), 1.0)
        with self.assertRaises(RuntimeError):
            t.nnue_extension.process_batch(self.records, torch.tensor([-1]), weights,
                                             torch.empty_like(weights), 1.0)
        invalid = self.records[:1].clone()
        invalid[0, :32] = 0x11  # too many active features: exception originates in a worker
        with self.assertRaisesRegex(RuntimeError, "too many active"):
            self.batch(weights, invalid)
        self.assertTrue(math.isfinite(self.batch(weights)[0]))

    def test_independent_calls_and_shutdown(self):
        t.nnue_extension.set_threads(4)
        shared, per_neuron = self.weights(1), self.weights(64)
        expected = [self.batch(w) for w in (shared, per_neuron)]
        with ThreadPoolExecutor(max_workers=2) as workers:
            futures = [workers.submit(self.batch, w) for w in (shared, per_neuron) * 3]
            for i, future in enumerate(futures):
                loss, gradient = future.result()
                self.assertEqual(loss, expected[i % 2][0])
                self.assertTrue(torch.equal(gradient, expected[i % 2][1]))
        t.nnue_extension.shutdown()
        t.nnue_extension.shutdown()
        t.nnue_extension.set_threads(4)
        self.assertEqual(self.batch(shared)[0], expected[0][0])
        # L1 has zero subgradient at an exact teacher match.
        weights = self.weights(0)
        weights[t.B1_END:] = 0
        records = self.records[:1].clone()
        records[0, 38:40] = 0
        loss, gradient = self.batch(weights, records, exponent=1.0, mixture=1.0)
        self.assertEqual(loss, 0)
        self.assertEqual(gradient.count_nonzero(), 0)

    def test_atomic_replace_failure(self):
        with tempfile.TemporaryDirectory() as folder:
            target = Path(folder) / "metrics.json"
            target.write_bytes(b"previous")
            replace, attempts = persistence.os.replace, 0

            def transient(*args):
                nonlocal attempts
                attempts += 1
                if attempts < 3:
                    raise PermissionError("simulated transient sharing violation")
                return replace(*args)

            with patch.object(persistence.os, "replace", side_effect=transient), patch.object(persistence.time, "sleep"):
                persistence.write_bytes(target, b"completed")
            self.assertEqual(target.read_bytes(), b"completed")
            with patch.object(persistence.os, "replace", side_effect=PermissionError("simulated failure")), \
                    patch.object(persistence.time, "sleep"), self.assertRaises(PermissionError):
                persistence.write_bytes(target, b"unfinished")
            self.assertEqual(target.read_bytes(), b"completed")

    def test_formats_and_initialization(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "test.net"
            for curves, version in ((0, 5), (1, 7), (64, 8)):
                weights = self.weights(curves)
                t.export_net(weights, path)
                data = path.read_bytes()
                self.assertEqual(struct.unpack_from("<H", data, 4)[0], version)
                loaded = t.load_net(path)
                expected = t.round_away_from_zero(weights)
                expected[:t.W2_END].clamp_(-128, 127)
                self.assertTrue(torch.equal(loaded, expected))
                if curves == 1:
                    promoted = t.initial_weights(path, 0, 64)
                    self.assertTrue(torch.equal(promoted[t.PARAM_COUNT:], loaded[t.PARAM_COUNT:].repeat(64)))
                for invalid in (data[:-1], data + b"x"):
                    path.write_bytes(invalid)
                    with self.assertRaises(ValueError):
                        t.load_net(path)
                if curves:
                    invalid = bytearray(data)
                    struct.pack_into("<i", invalid, 45262, 1)
                    path.write_bytes(invalid)
                    with self.assertRaises(ValueError):
                        t.load_net(path)
            weights = self.weights(0)
            weights[0] = 1e30
            t.export_net(weights, path)
            self.assertEqual(t.load_net(path)[0], 127)
            # Legacy v2 aliases must be copied from original source rows.
            data = bytearray(path.read_bytes())
            struct.pack_into("<H", data, 4, 2)
            path.write_bytes(data)
            loaded = t.load_net(path)[:t.W1_END].view(704, 64)
            original = t.round_away_from_zero(weights[:t.W1_END]).clamp(-128, 127).view(704, 64)
            for rank in range(1, 8):
                for file in range(4):
                    self.assertTrue(torch.equal(loaded[rank * 4 + file], original[rank * 8 + file]))

    def assert_state_equal(self, a, b):
        if isinstance(a, torch.Tensor):
            self.assertTrue(torch.equal(a, b))
        elif isinstance(a, dict):
            self.assertEqual(a.keys(), b.keys())
            for key in a:
                self.assert_state_equal(a[key], b[key])
        elif isinstance(a, (list, tuple)):
            self.assertEqual(len(a), len(b))
            for left, right in zip(a, b):
                self.assert_state_equal(left, right)
        else:
            self.assertEqual(a, b)

    def test_interrupted_training_resume(self):
        with tempfile.TemporaryDirectory() as folder, contextlib.redirect_stdout(io.StringIO()):
            folder = Path(folder)
            records = self.records.repeat(8, 1)
            data = folder / "data.records"
            data.write_bytes(struct.pack("<4sIII", b"SH01", 40, len(records), 0)
                              + bytes(records.flatten().tolist()))
            common = [str(data), "--method", "factored-fd", "--epochs", "3", "--batch-size", "4",
                      "--chunk-records", "8", "--shuffle-chunks", "--lr", ".1", "--final-lr", ".001",
                      "--lr-schedule", "cosine", "--val-fraction", ".25", "--threads", "2"]
            t.main(common + ["--output", str(folder / "control.net")])
            original_epoch, calls = t.train_epoch, 0

            def interrupt(*args, **kwargs):
                nonlocal calls
                calls += 1
                if calls == 2:
                    raise KeyboardInterrupt()
                return original_epoch(*args, **kwargs)

            with patch.object(t, "train_epoch", side_effect=interrupt), self.assertRaises(KeyboardInterrupt):
                t.main(common + ["--output", str(folder / "resumed.net")])
            t.main([str(data), "--resume", str(folder / "resumed.pt"),
                    "--output", str(folder / "resumed.net"), "--best-output", str(folder / "best.net")])
            self.assertEqual((folder / "control.net").read_bytes(), (folder / "resumed.net").read_bytes())
            left = torch.load(folder / "control.pt", weights_only=True)
            right = torch.load(folder / "resumed.pt", weights_only=True)
            for key in ("weights", "optimizer", "generator_state", "config", "best_weights"):
                self.assert_state_equal(left[key], right[key])
            self.assertTrue((folder / "best.net").is_file())
            t.main([str(data), "--resume", str(folder / "resumed.pt"),
                    "--output", str(folder / "copied.net")])
            self.assertEqual((folder / "copied.net").read_bytes(), (folder / "control.net").read_bytes())
            self.assertTrue((folder / "copied.pt").is_file())
            with self.assertRaises(SystemExit), contextlib.redirect_stderr(io.StringIO()):
                t.parse_args([str(data), "--resume", str(folder / "resumed.pt"), "--epochs", "4"])
            malformed = folder / "malformed.records"
            malformed.write_bytes(data.read_bytes() + b"x")
            with self.assertRaises(ValueError):
                t.read_records(malformed)
            alternate = folder / "alternate.records"
            alternate.write_bytes(data.read_bytes())
            with self.assertRaisesRegex(ValueError, "dataset metadata"):
                t.main([str(alternate), "--resume", str(folder / "resumed.pt"),
                        "--output", str(folder / "alternate.net")])


if __name__ == "__main__":
    unittest.main()

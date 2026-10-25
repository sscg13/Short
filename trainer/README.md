# NNUE trainer

The CPU trainer uses PyTorch's AdamW optimizer with either conventional QAT
backpropagation or **factored FD**. Factored FD replaces the experimental
random-direction DUST/SPSA implementation. It measures local secants through the
actual quantized activation/output products, multiplies them by the clean loss
derivative, and scatters the credits to active feature rows. The linear head
retains an STE for output rounding. It estimates a different gradient from
simultaneous-noise DUST; it is not a full finite difference of the loss.

## Build and run

Use an x86-64 Python environment with PyTorch and a C++17 compiler supporting
AVX2/F16C. Build from this directory:

```powershell
python setup.py build_ext --inplace
```

On Windows, setup.py discovers clang-cl and the installed MSVC/Windows SDK,
falling back to MSVC; NNUE_CLANG_CL can select another clang-cl executable.
Linux uses the configured C++ compiler through PyTorch's build tooling.
The trainer uses this extension directly; no standalone DLL or fixed DLL search
path is needed. Rebuild after changing the core or extension interface.

Existing QAT commands continue to use fixed ReLU² and export v5 nets:

```powershell
python trainer.py data/train.records --method qat --epochs 20 --output nets/qat.net
```

The matched shared-activation FD50 pretraining recipe uses a pre-shuffled SH01
file, shuffled 1,048,576-record chunks and sequential records within each chunk:

```powershell
python trainer.py data/train.records --method factored-fd --activation shared --epochs 50 --lr .1 --final-lr .001 --lr-schedule cosine --activation-lr-multiplier 32 --batch-size 16384 --chunk-records 1048576 --shuffle-chunks --no-local-shuffle --threads 4 --seed 1 --output nets/fd50.net
```

The final 1% of records is held out by default. Use --init-net for fine-tuning,
--max-records for a bounded run, and --best-output to retain the lowest validation
loss separately. Final-epoch selection and best-loss selection are distinct;
playing strength requires a game test. AdamW uses beta .9/.999, epsilon 1e-8,
and zero weight decay by default. PyTorch uses one optimizer thread; --threads
controls the persistent C++ record workers.
Zero selects one worker, retaining the previous command-line convention.

## Activations and compatibility

| Activation | Parameters | Export |
|---|---:|---|
| relu2 | Fixed clamp(acc,0,255)² | v5, 45,262 bytes |
| shared | One 256-entry signed table | v7, 46,286 bytes |
| per-neuron | 64 independent tables, each shared across perspectives | v8, 110,798 bytes |

QAT defaults to relu2; factored-fd defaults to shared. Custom activations require
factored-fd. Every origin stays zero; remaining responses are bounded to
[-65025,65025]. There are no smoothness or monotonicity constraints. The default
accumulator and response secant steps are 4 and 256; --accumulator-step and
--response-step set them explicitly. --activation-lr-multiplier scales the table
learning rate. --freeze-activation preserves a loaded table while learning the
base weights; relu2 skips table-gradient work entirely.

The loader accepts v2/v5/v7/v8, applies the legacy v2 row remap once, and rejects
malformed lengths, headers, response bounds and origins. Fine-tuning can promote
a shared table to per-neuron copies. It refuses to silently discard or average
custom tables. Export uses the engine's rounding and signed product conventions.

## Checkpoints

Each epoch writes the net, an optimizer/RNG checkpoint and JSON metrics. The
checkpoint and metrics default to the output stem with .pt/.json suffixes;
--checkpoint and --metrics override them. Writes use atomic replacement with
bounded retries. The checkpoint contains the complete epoch history and best
weights, so a metrics interruption can be recovered after the checkpoint saves.

```powershell
python trainer.py data/train.records --resume nets/fd50.pt --output nets/fd50.net
```

Resume restores the original method, activation, LR schedule, epoch budget,
optimizer and shuffle generator. Explicit changes to the saved recipe are
rejected. Dataset path, count, size and modification time must match. Recovery
restarts at the last completed epoch; it does not resume within an epoch.
Use --init-net for a new fine-tuning schedule with a fresh optimizer.

## Validation

After building, run from the repository root:

```powershell
python tests/test_trainer.py
```

Checks include independent scalar QAT/FD credits, clipping and signed curves,
all-parameter gradients, shared/per-neuron/fixed modes, evaluation and frozen
tables, indexed batches, invalid API inputs, worker exception recovery, v2/v5/v7/v8
formats, and exact optimizer/RNG replay after an interrupted training run.
The FD algorithm and curves are training tools; the shipping engine network is
selected independently through the engine validation/OpenBench workflow.

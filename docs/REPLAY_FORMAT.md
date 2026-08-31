# Replay format

FasTris `.ftr` files use the current compact binary replay format. Legacy text replay layouts are intentionally unsupported.

A replay stores only deterministic inputs and the information required to reproduce the run:

- piece seed
- game mode and simulation rules
- total simulated duration
- timestamped gameplay input transitions
- final deterministic-state SHA-256

It does not store video frames or board snapshots on disk.

## Binary layout

The file begins with the fixed eight-byte current-format magic:

```text
FASTRIS 0x01
```

Numeric metadata is encoded with unsigned variable-length integers where practical. Input events are stored as:

```text
[varuint timestamp delta] [1-byte action/pressed value]
```

The timestamp is the delta from the previous event in integer microseconds. The event byte stores one of the eight gameplay actions (`Left` through `Hold`) in the low bits and pressed/released state in the high bit. Pause and Restart are application controls and are not legal replay events.

The final SHA-256 is stored as 32 raw bytes. The in-memory replay representation uses the same raw 32-byte digest, avoiding hex conversion on normal replay paths.

## Human-readable Python decoder

The repository includes a dependency-free decoder for inspecting `.ftr` files without opening the game:

```bash
python scripts/decode_replay.py replays/example.ftr
```

It prints the replay mode, seed, duration, all handling/rule values, the stored final-state SHA-256, an input summary, and a timestamped `PRESS` / `RELEASE` timeline in plain English. To save the decoded text, use:

```bash
python scripts/decode_replay.py replay.ftr -o replay.txt
```

Use `--summary-only` to omit the potentially long event timeline. The Python script strictly validates the binary layout but does not re-simulate the game; use the native `fastris_verify` tool when deterministic final-state verification is required.

## Replay filenames

User-saved replays use the mode's first display word plus the local save timestamp:

```text
FasTris_replay_<Mode>_YYYY-MM-DD_HH-MM-SS.ftr
```

For example, a Sprint replay saved on 31 August 2026 at 19:30:45 is named `FasTris_replay_Sprint_2026-08-31_19-30-45.ftr`. Sandbox/Custom runs use `Sandbox` as the mode word. The internal rolling `last.ftr` autosave remains separate so the **Last Replay** menu continues to work.

## Bounded decoding and validation

The parser is strict and bounded before simulation. It rejects malformed or unreasonable data, including:

- files larger than the current replay-size limit
- unsupported magic/layout
- invalid modes, gameplay actions, flags, or rule ranges
- more than the allowed number of events
- timestamps that overflow or exceed replay duration
- durations beyond the replay limit
- malformed/truncated hashes
- unexpected trailing bytes

Delta timestamps inherently preserve monotonic event order.

Native tools can decode the compact format synchronously because normal files are tiny. The Web build uses the same `ReplayDecoder` incrementally with both a per-frame time budget and an event-count budget, so a hostile maximum-size replay cannot monopolize the browser UI thread while being parsed.

## Verification and indexing

Loading a replay starts one incremental deterministic re-simulation. The same pass:

- verifies the final deterministic-state hash
- builds bounded in-memory seek checkpoints
- records exact piece-lock markers
- records exact line-clear markers
- records exact T-spin markers
- records exact Perfect Clear markers

Analysis markers are emitted directly by the game engine at the exact simulation timestamp. They are not inferred by periodically comparing statistics.

Normal replays use approximately five-second checkpoints. Very long replays automatically increase checkpoint spacing so the index never exceeds the hard checkpoint-count limit. Checkpoints remain runtime-only and are never written into `.ftr`.

Backward/long forward seeking restores the nearest available checkpoint and simulates only the short remaining interval instead of rebuilding from time zero.

The game clock also repairs stale scheduled timers and has an anti-stall iteration guard, so corrupted/future state cannot turn an overdue timer into millions of one-microsecond catch-up loops.

When normal playback reaches the replay end, FasTris performs one additional cheap hash of the viewer's already-computed final state. No verification is repeated every frame.

## What verification means

`REPLAY VERIFIED` means the stored inputs deterministically reproduce the final state/hash recorded by the replay.

SHA-256 is an integrity checksum, not a client-authenticity signature. A modified client can create a modified replay and calculate a new hash. A competitive server must validate the replay and independently re-simulate it, deriving score, time, lines, and other results itself instead of trusting client-provided result fields.

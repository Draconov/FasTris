# Replay format

FasTris `.ftr` files use the current compact binary replay format. Legacy text replay layouts are intentionally unsupported.

A replay stores only deterministic inputs and the information required to reproduce the run:

- piece seed
- game mode and simulation rules
- total simulated duration
- timestamped input transitions
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

The timestamp is the delta from the previous event in integer microseconds. The event byte stores the action ID in the low bits and the pressed/released state in the high bit.

The final SHA-256 is stored as 32 raw bytes rather than 64 hexadecimal characters.

This makes replay size scale primarily with the number of actual input transitions rather than run duration.

## Validation limits

The parser is strict and bounded before any replay is simulated. It rejects malformed or unreasonable data, including:

- files larger than the current replay-size limit
- unsupported magic/layout
- invalid modes, actions, flags, or rule ranges
- more than the allowed number of events
- timestamps that overflow, exceed replay duration, or decode incorrectly
- durations beyond the replay limit
- malformed/truncated hashes
- unexpected trailing bytes

Delta timestamps inherently preserve monotonic event order in the encoded stream.

## Verification and indexing

Loading a replay starts one incremental deterministic re-simulation. The app gives that work a small per-frame budget so long replays do not freeze the Web build.

That single pass performs several jobs at once:

- verifies the final deterministic-state hash
- creates in-memory seek checkpoints approximately every five seconds
- indexes piece locks
- indexes line clears
- indexes T-spins
- indexes Perfect Clears

The checkpoints are runtime-only and are not written into `.ftr`, keeping replay files small. Backward seeking restores the nearest checkpoint and simulates only the short remaining interval instead of starting from zero.

When normal playback itself reaches the replay end, FasTris performs one additional cheap hash of the viewer's already-computed final state. No verification is repeated every frame.

## What verification means

`REPLAY VERIFIED` means the stored inputs deterministically reproduce the final state/hash recorded by the replay.

SHA-256 is an integrity checksum, not a client authenticity signature. A modified client can create a modified replay and calculate a new hash. A competitive server must therefore validate the replay and independently re-simulate it, deriving score, time, lines, and other results itself instead of trusting client-provided result fields.

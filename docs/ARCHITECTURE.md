# Architecture

## Determinism boundary

`fasttris_core` must not depend on SDL, wall-clock APIs, graphics, audio, operating-system key repeat, or monitor refresh rate.

The desktop layer translates SDL nanosecond event timestamps to an integer microsecond simulation timeline and calls:

```cpp
game.advanceTo(eventTimeUs);
game.press(action);
```

Replays perform the same calls. This is why a run can be reconstructed without recording video or frame-by-frame board snapshots.

## RNG streams

Piece generation uses PCG32 through `Bag7`.

Garbage uses a separately seeded PCG32 stream. Cosmetic systems should use another RNG and must never consume gameplay RNG values.

Seed derivation is versioned. If a future release changes a randomizer algorithm, introduce a new replay/randomizer version instead of silently changing existing seeds.

## Timing

All gameplay time uses signed 64-bit integer microseconds.

Scheduled events include:

- gravity ticks
- DAS expiry / ARR repeat
- lock deadline
- Ultra timeout

When multiple simulation events have the same timestamp, the engine resolves them in a deterministic order.

## Board

The board has 10 columns, 20 visible rows and 4 hidden spawn rows. Occupancy is kept as a 10-bit row mask while cell IDs are kept separately for rendering/statistics.

## Renderer isolation

The SDL renderer reads the core state but never mutates rules based on frame timing. Particles, line flashes, screen shake and richer skin systems can be added later without changing replay results.

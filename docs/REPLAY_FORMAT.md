# Replay format v1

FasTris replay files are UTF-8 text so runs remain easy to inspect during development.

Example:

```text
FASTTRIS_REPLAY 1
seed 12345
mode 0
simver 2
das 140
arr 25
...
event 15321 left 1
event 42110 left 0
event 48652 cw 1
event 61100 cw 0
event 70200 hard 1
duration 70200
hash <sha256>
```

Times are integer microseconds from the start of the simulated run, excluding paused wall-clock time.

`simver` identifies the deterministic gameplay-rules revision. It is separate from both the application `VERSION` and the replay-file syntax version. This lets scoring, finesse, or mode semantics improve while old recorded runs can still be replayed with their original deterministic behavior.

Replays written before `simver` existed are treated as simulation rules version 1. New runs use the current simulation rules version.

Custom-mode settings are stored in the replay as well, including gravity, line goal, time limit, and starting garbage.

The final hash is SHA-256 over a canonical deterministic-state serialization. A verifier rebuilds the run from the replay and compares the hash.

A future incompatible change to the replay file syntax itself should create replay format v2 rather than reusing the v1 header.

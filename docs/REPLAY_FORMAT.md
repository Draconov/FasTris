# Replay format v1

FasTris replay files are UTF-8 text for easy inspection during development.

Example:

```text
FASTTRIS_REPLAY 1
seed 12345
mode 0
das 100
arr 0
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

The final hash is SHA-256 over a canonical deterministic-state serialization. A verifier rebuilds the run from the replay and compares the hash.

Future incompatible changes should create replay format v2 instead of altering v1 semantics.

# Replay format v1

FasTris replay files are UTF-8 text so runs remain easy to inspect during development.

Example:

```text
FASTTRIS_REPLAY 1
seed 12345
mode 0
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

FasTris intentionally supports only the current replay layout. The loader requires the current field set and rejects missing or unknown fields instead of maintaining legacy replay behavior.

Sandbox-mode settings are stored in the replay as well, including gravity, line goal, time limit, and starting garbage.

The final hash is SHA-256 over the current canonical deterministic-state serialization. A verifier rebuilds the run from the replay and compares the hash.

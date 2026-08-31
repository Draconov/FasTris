# Competitive netcode design

FasTris already separates deterministic simulation from presentation. A production online service should preserve that boundary.

## Recommended authoritative flow

1. Match server chooses match ID, ruleset version, piece seed and garbage seed.
2. Both clients receive the immutable match configuration.
3. Clients timestamp and send input transitions, not board images.
4. The server simulates the same `fasttris_core` rules and is authoritative for attacks, garbage and match end.
5. Clients may predict locally for zero-feeling input latency.
6. Periodic state hashes detect desyncs.
7. Final replay/input logs are retained for moderation and leaderboard verification.
8. Leaderboard/tournament services re-simulate submitted replays and derive the claimed result server-side.

## Do not trust

A ranked backend should never trust a client-provided score/time/APM alone. It should validate replay bounds/rules, re-simulate the submitted input log, and calculate the result itself.

The replay SHA-256 is an integrity/desync checksum, not an anti-cheat signature. A modified client can calculate a new hash for modified data, so hash equality alone must never authorize a leaderboard result.

## Same-seed races

For races, every participant can receive the same piece seed and ruleset. Their boards remain independent; results are compared by the objective (for example 40-line completion time). This makes daily/weekly challenges trivial to reproduce and audit.

## Transport

Transport is deliberately outside `fasttris_core`. UDP + reliability, QUIC, WebRTC data channels, or a custom relay can all drive the same input API. Do not put socket behavior into board/scoring code.

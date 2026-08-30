# FasTris

FasTris is a native C++20 and SDL3 falling-block game built for low latency, deterministic competition, reproducible seeds, and modern tetromino mechanics.

Gameplay simulation is independent from rendering. A run can be reproduced from its seed, rules, and timestamped input events regardless of monitor refresh rate or render FPS.

## Highlights

- Deterministic PCG32 RNG with a modern 7-bag randomizer
- Editable 64-bit seeds and daily seeded challenges
- SRS-style rotation with clockwise, counter-clockwise, and optional 180-degree rotation
- Hold, ghost piece, hard drop, soft drop, and sonic drop
- Configurable DAS, ARR, SDF, DCD, IRS, IHS, lock delay, and lock resets
- T-Spins, T-Spin Minis, Back-to-Back, combos, Perfect Clears, and attack calculation
- Deterministic garbage, cancellation, delay, cap, and two-player battle simulation
- Sprint, Ultra, Marathon, Zen, Cheese Race, Finesse, Seed Race, and Custom modes
- Live PPS, APM, KPP, attack, combo, B2B, T-Spin, Perfect Clear, and finesse statistics
- Deterministic replay recording, playback, SHA-256 verification, and a headless verifier
- Keyboard and gamepad rebinding
- High-refresh and uncapped rendering options
- Tournament rules lock

## Default controls

| Action | Keyboard | Gamepad |
| --- | --- | --- |
| Left | Left Arrow | D-pad Left |
| Right | Right Arrow | D-pad Right |
| Soft Drop | Down Arrow | D-pad Down |
| Hard Drop | Space | D-pad Up |
| Rotate CW | Up Arrow | South face button |
| Rotate CCW | Z | West face button |
| Rotate 180 | A | North face button |
| Hold | C | Left Shoulder |
| Pause | P | Back |
| Restart | F5 | Start |
| Save replay | F6 | Not assigned |
| Fullscreen | F11 | Not assigned |

Gameplay bindings can be changed from the Controls / Rebind menu.

## Seeds and deterministic runs

Start a Sprint run with an exact seed:

```bash
FasTris --seed 123456789 --mode sprint
```

Inspect a deterministic piece sequence without launching the SDL client:

```bash
fastris_sequence 123456789 70
```

Run the same daily Seed Race as every other client using the same rules:

```bash
FasTris --daily 2026-08-30 --mode seedrace
```

## Replays

Completed runs are saved as deterministic replay data rather than video. A replay stores the seed, rules, handling, timestamped inputs, duration, and final state hash.

Verify a replay independently:

```bash
fastris_verify replays/last.ftr
```

Replay format details are documented in [`docs/REPLAY_FORMAT.md`](docs/REPLAY_FORMAT.md).

## Build

FasTris uses CMake and SDL3. The default native build downloads the pinned SDL3 source dependency automatically on first configure.

### Windows

Requirements: Visual Studio 2022 with Desktop development with C++, and CMake 3.20 or newer.

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
.\build\Release\FasTris.exe
```

### Linux

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./build/FasTris
```

Some distributions require the usual SDL X11 or Wayland development packages.

### macOS

Requirements: Xcode Command Line Tools and CMake.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
open build/FasTris.app
```

### Web

With the Emscripten SDK activated:

```bash
emcmake cmake -S . -B build-web -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DFASTTRIS_BUILD_TESTS=OFF \
  -DFASTTRIS_BUILD_TOOLS=OFF
cmake --build build-web --parallel
```

Serve `build-web/` through a local HTTP server and open `index.html`.

### Android

Android builds use the Gradle project in `platform/android/`. Release signing and local Android requirements are covered in the release documentation.

### Core and tools only

For headless verification, testing, or backend use:

```bash
cmake -S . -B build-core -DFASTTRIS_BUILD_APP=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build build-core --parallel
ctest --test-dir build-core --output-on-failure
```

This configuration does not require SDL.

## Versioning and releases

The root [`VERSION`](VERSION) file is the single source of truth for the game version.

To prepare a new release, change only that version number and push the release commit to `main`. GitHub Actions builds Windows, Linux, macOS, Web, and Android packages and publishes the corresponding `vX.Y.Z` release only after all required jobs succeed.

Full release and Android signing instructions are in [`docs/RELEASING.md`](docs/RELEASING.md).

## Icon

The master icon location is:

```text
assets/icon/icon.png
```

A placeholder is included so the path already exists. Replace that file with the final square PNG when the artwork is ready. A 1024x1024 RGBA source is recommended.

## Repository layout

```text
FasTris/
|-- include/fasttris/     Core public headers
|-- src/core/             Deterministic simulation
|-- src/app/              SDL3 application
|-- src/tools/            Replay verifier, sequence tool, benchmark
|-- platform/android/     Android Gradle project
|-- platform/web/         Emscripten shell
|-- tests/                Determinism and rules tests
|-- docs/                 Architecture, replay, netcode, and release docs
|-- assets/icon/          Master application icon
|-- VERSION               Application version
`-- .github/workflows/    CI and release automation
```

## Online multiplayer

The repository contains deterministic battle and garbage simulation suitable for authoritative netcode, but it does not include a production matchmaking or ranked backend. The intended networking model is documented in [`docs/NETCODE.md`](docs/NETCODE.md).

## Testing

Run the complete configured test suite with:

```bash
ctest --test-dir build --output-on-failure
```

Architecture details are available in [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md).

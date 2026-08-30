<div align="center">
  <img src="assets/icon/icon.png" width="128" height="128" alt="FasTris icon">

# FasTris

**Tetris built for competitive players.**

[![Play Online](https://img.shields.io/badge/PLAY-ONLINE-14a8b2?style=for-the-badge&logo=github)](https://draconov.github.io/FasTris/)
[![Download Windows](https://img.shields.io/badge/DOWNLOAD-WINDOWS-0078D6?style=for-the-badge&logo=windows11&logoColor=white)](https://github.com/Draconov/FasTris/releases/latest/download/FasTris-Windows.zip)
[![Download Linux](https://img.shields.io/badge/DOWNLOAD-LINUX-FCC624?style=for-the-badge&logo=linux&logoColor=000000)](https://github.com/Draconov/FasTris/releases/latest/download/FasTris-Linux.tar.gz)
[![Download macOS](https://img.shields.io/badge/DOWNLOAD-macOS-000000?style=for-the-badge&logo=apple&logoColor=white)](https://github.com/Draconov/FasTris/releases/latest/download/FasTris-macOS.zip)
[![Download Android](https://img.shields.io/badge/DOWNLOAD-ANDROID-3DDC84?style=for-the-badge&logo=android&logoColor=white)](https://github.com/Draconov/FasTris/releases/latest/download/FasTris-Android.apk)
[![Download Web](https://img.shields.io/badge/DOWNLOAD-WEB-E34F26?style=for-the-badge&logo=html5&logoColor=white)](https://github.com/Draconov/FasTris/releases/latest/download/FasTris-Web.zip)

[![Build](https://github.com/Draconov/FasTris/actions/workflows/build.yml/badge.svg)](https://github.com/Draconov/FasTris/actions/workflows/build.yml)
[![Web](https://github.com/Draconov/FasTris/actions/workflows/pages.yml/badge.svg)](https://github.com/Draconov/FasTris/actions/workflows/pages.yml)
[![Latest Release](https://img.shields.io/github/v/release/Draconov/FasTris?label=release)](https://github.com/Draconov/FasTris/releases/latest)

</div>

---

## About

FasTris is a native C++20 and SDL3 falling-block game built around low input latency, deterministic competition, reproducible seeds, and modern tetromino mechanics.

Gameplay simulation is independent from rendering. The same seed, rules, and timestamped inputs reproduce the same run regardless of monitor refresh rate or render FPS.

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

Inspect its deterministic piece sequence without launching the SDL client:

```bash
fastris_sequence 123456789 70
```

Run a daily Seed Race:

```bash
FasTris --daily 2026-08-30 --mode seedrace
```

## Replays

Completed runs are stored as deterministic replay data rather than video. A replay contains the seed, rules, handling, timestamped inputs, duration, and final state hash.

Verify one independently:

```bash
fastris_verify replays/last.ftr
```

See [`docs/REPLAY_FORMAT.md`](docs/REPLAY_FORMAT.md) for the replay format.

## Build

FasTris uses CMake and SDL3. Native builds download the pinned SDL3 source dependency automatically on first configure.

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

Some distributions require SDL X11 or Wayland development packages.

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

Android builds use the Gradle project in `platform/android/`. Release signing and local Android requirements are documented in [`docs/RELEASING.md`](docs/RELEASING.md).

### Core and tools only

```bash
cmake -S . -B build-core -DFASTTRIS_BUILD_APP=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build build-core --parallel
ctest --test-dir build-core --output-on-failure
```

This configuration does not require SDL.

## Versioning and releases

[`VERSION`](VERSION) is the single source of truth for the game version. Change that file and push to `main`; GitHub Actions builds Windows, Linux, macOS, Web, and Android packages and publishes the corresponding release after all required jobs succeed.

## Online multiplayer

The repository contains deterministic battle and garbage simulation suitable for authoritative netcode. A production matchmaking and ranked backend is not included yet; the intended networking model is described in [`docs/NETCODE.md`](docs/NETCODE.md).

## Project structure

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
`-- .github/workflows/    CI, Pages, and release automation
```

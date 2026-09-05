#!/usr/bin/env python3
from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
errors: list[str] = []


def fail(message: str) -> None:
    errors.append(message)


version_path = ROOT / "VERSION"
version = version_path.read_text(encoding="utf-8").strip() if version_path.exists() else ""
match = re.fullmatch(r"(\d+)\.(\d+)\.(\d+)", version)
if not match:
    fail(f"VERSION must be MAJOR.MINOR.PATCH, got {version!r}")
else:
    major, minor, patch = map(int, match.groups())
    if minor >= 1000 or patch >= 1000:
        fail("VERSION minor and patch must be below 1000 for Android versionCode")
    android_code = major * 1_000_000 + minor * 1_000 + patch
    if android_code < 1 or android_code > 2_100_000_000:
        fail(f"Android versionCode would be out of range: {android_code}")

required_files = [
    "CMakeLists.txt",
    ".github/workflows/build.yml",
    ".github/workflows/release.yml",
    ".github/workflows/pages.yml",
    ".github/dependabot.yml",
    "platform/android/app/build.gradle",
    "platform/android/app/src/main/AndroidManifest.xml",
    "platform/web/shell.html",
]
for rel in required_files:
    if not (ROOT / rel).is_file():
        fail(f"missing required file: {rel}")

checks = {
    "CMakeLists.txt": ["project(FasTris", "com.draconov.fastris"],
    "platform/android/app/build.gradle": ["com.draconov.fastris"],
    "platform/web/shell.html": ["<title>FasTris</title>"],
}
for rel, needles in checks.items():
    path = ROOT / rel
    if not path.is_file():
        continue
    text = path.read_text(encoding="utf-8")
    for needle in needles:
        if needle not in text:
            fail(f"{rel} is missing expected identity marker: {needle}")


# CMake compatibility regression: Android intentionally uses CMake 3.22.1, while
# the project floor is 3.20. DOWNLOAD_EXTRACT_TIMESTAMP was added only in 3.24,
# so it must never appear directly inside FetchContent_Declare() at this floor.
cmake_text = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
minimum_match = re.search(r"cmake_minimum_required\(VERSION\s+(\d+)\.(\d+)", cmake_text)
if minimum_match and tuple(map(int, minimum_match.groups())) < (3, 24):
    for block in re.findall(r"FetchContent_Declare\(.*?\n\s*\)", cmake_text, flags=re.S):
        if "DOWNLOAD_EXTRACT_TIMESTAMP" in block:
            fail("CMake < 3.24 cannot use DOWNLOAD_EXTRACT_TIMESTAMP directly in FetchContent_Declare; use the version-gated compatibility args")
    if 'CMAKE_VERSION VERSION_GREATER_EQUAL "3.24"' not in cmake_text:
        fail("CMake must version-gate DOWNLOAD_EXTRACT_TIMESTAMP for the Android CMake 3.22.1 toolchain")

# CI regression checks for platform-specific failures seen on clean GitHub runners.
build_workflow = (ROOT / ".github/workflows/build.yml").read_text(encoding="utf-8")
release_workflow = (ROOT / ".github/workflows/release.yml").read_text(encoding="utf-8")
for name, workflow in [("build.yml", build_workflow), ("release.yml", release_workflow)]:
    if "libxtst-dev" not in workflow:
        fail(f"{name} must install libxtst-dev for SDL X11/XTEST builds")
    if "bash scripts/prepare_sdl_android.sh" not in workflow:
        fail(f"{name} must invoke prepare_sdl_android.sh through bash so Git file-mode loss cannot break Android CI")
    if "lipo -verify_arch" in workflow:
        fail(f"{name} uses the fragile/incorrect lipo -verify_arch invocation; verify lipo -archs output instead")


# Release jobs must rebuild/refresh the current VERSION release instead of being skipped by an existing tag.
if "release_needed" in release_workflow or "needs.version.outputs.release_needed" in release_workflow:
    fail("release.yml must not gate release jobs on release_needed")
if "git/refs/tags/$TAG" not in release_workflow or "-F force=true" not in release_workflow:
    fail("release.yml must refresh the existing VERSION tag to the tested main commit")
if "gh release delete-asset" not in release_workflow:
    fail("release.yml must remove old release assets before publishing the exact current payload")

# Distribution/readme regression checks.
dependabot = (ROOT / ".github/dependabot.yml").read_text(encoding="utf-8")
if 'groups:' not in dependabot or 'patterns:' not in dependabot or '- "*"' not in dependabot:
    fail("Dependabot GitHub Actions updates must remain grouped into one PR")
if "open-pull-requests-limit: 1" not in dependabot:
    fail("Dependabot GitHub Actions updates should be limited to one open PR")

pages_workflow = (ROOT / ".github/workflows/pages.yml").read_text(encoding="utf-8")
for needle in ["actions/deploy-pages@", "actions/upload-pages-artifact@", "build-web/index.wasm"]:
    if needle not in pages_workflow:
        fail(f"pages.yml is missing required Pages deployment marker: {needle}")

release_assets = [
    "FasTris.exe",
    "FasTris-Linux.tar.gz",
    "FasTris-macOS.zip",
    "FasTris-Web.zip",
    "FasTris.apk",
    "SHA256SUMS.txt",
]
for asset in release_assets:
    if asset not in release_workflow:
        fail(f"release.yml is missing required release asset: {asset}")

if "dist/FasTris.exe" not in release_workflow or "FASTTRIS_BUILD_TOOLS=OFF" not in release_workflow:
    fail("Windows release must publish the single FasTris.exe app without developer tools")
if "dist/FasTris.apk" not in release_workflow:
    fail("Android release must publish the clean FasTris.apk asset name")

if re.search(r"dist/FasTris-v[^\n\"']*\.(?:zip|tar\.gz|apk)", release_workflow):
    fail("release.yml must not publish versioned duplicate platform packages")


readme = (ROOT / "README.md").read_text(encoding="utf-8")
for needle in [
    "https://draconov.github.io/FasTris/",
    "releases/latest/download/FasTris.exe",
    "releases/latest/download/FasTris-Linux.tar.gz",
    "releases/latest/download/FasTris-macOS.zip",
    "releases/latest/download/FasTris-Web.zip",
    "releases/latest/download/FasTris.apk",
]:
    if needle not in readme:
        fail(f"README.md is missing expected distribution badge target: {needle}")

# Guideline-mode regression checks.
app_config_cpp=(ROOT / "src/app/app_config.cpp").read_text(encoding="utf-8")
app_main_cpp=(ROOT / "src/app/main.cpp").read_text(encoding="utf-8")
renderer_cpp=(ROOT / "src/app/renderer.cpp").read_text(encoding="utf-8")
for needle in ["guideline=", "c.rules.guideline"]:
    if needle not in app_config_cpp:
        fail(f"app_config.cpp is missing Guideline persistence marker: {needle}")
if "effectiveRulesForMode(cfg.rules, mode)" not in app_main_cpp:
    fail("main.cpp must resolve run rules through effectiveRulesForMode")
if "FOLLOW TETRIS GUIDELINES" not in renderer_cpp:
    fail("renderer.cpp must expose the Follow Tetris Guidelines settings row")
if "guideline_visual ? false : cfg.palette_affects_pieces" not in app_main_cpp:
    fail("main.cpp must force piece recoloring off for Guideline runs and replays")


# Music-system regression checks.
app_config_hpp=(ROOT / "src/app/app_config.hpp").read_text(encoding="utf-8")
music_manager_cpp=(ROOT / "src/app/music_manager.cpp").read_text(encoding="utf-8")
music_policy_cpp=(ROOT / "src/app/music_policy.cpp").read_text(encoding="utf-8")
for slot in ["menu", "gameplay", "intense"]:
    matches=[ROOT / "assets/music" / f"{slot}.{ext}" for ext in ["ogg", "mp3", "wav"]]
    matches=[path for path in matches if path.is_file()]
    if len(matches) != 1:
        fail(f"music slot {slot!r} must contain exactly one .ogg/.mp3/.wav source; found {len(matches)}")
for rel in ["scripts/embed_music.py", "src/app/music_manager.cpp", "src/app/music_policy.cpp"]:
    if not (ROOT / rel).is_file():
        fail(f"missing built-in music system file: {rel}")
if "music_volume{70}" not in app_config_hpp:
    fail("AppConfig must keep a persisted 0-100 music volume with a 70% default")
if "SDL_OpenAudioDeviceStream" not in music_manager_cpp or "kMusicCrossfadeSeconds" not in music_manager_cpp:
    fail("MusicManager must stream through SDL3 and use the shared crossfade duration")
if "kMusicIntenseOnPressure" not in music_policy_cpp or "pending_garbage_lines" not in music_policy_cpp:
    fail("music policy must retain mode-aware intensity and garbage-pressure handling")
if "Music slot '${slot}' requires exactly one source file" not in (ROOT / "CMakeLists.txt").read_text(encoding="utf-8"):
    fail("CMake must reject missing/duplicate menu/gameplay/intense music source formats")

# Android music startup must never be part of the first-frame critical path.
for marker in [
    "bool first_frame_presented{};",
    "bool lifecycle_suspended{};",
    'SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "openslES")',
    'SDL_SetHint(SDL_HINT_ANDROID_LOW_LATENCY_AUDIO, "0")',
    "music_startup.setLifecycleSuspended(lifecycle_suspended);",
    "if (!first_frame_presented) return;",
    "first_frame_presented = true;",
]:
    if marker not in app_main_cpp:
        fail(f"Android-safe delayed music startup marker missing from main.cpp: {marker}")
if "musicAudioCallback" not in music_manager_cpp:
    fail("MusicManager must refill playback from SDL's audio callback instead of the app iterate loop")
if re.search(r"void\s+MusicManager::update\(\)\s*\{[^}]*primeQueue", music_manager_cpp, flags=re.S):
    fail("MusicManager::update must never decode/refill audio on the SDL app loop")
if "primeQueue" in music_manager_cpp:
    fail("MusicManager must not use an app-loop polling primeQueue refill path")
if "kInitialPrebufferChunks" not in music_manager_cpp:
    fail("MusicManager must use a small bounded startup prebuffer before device resume")

# Android audio-device creation/decoding must not run synchronously inside the
# SDL main/event loop. SDL_InitSubSystem stays on the main thread, while the
# expensive MusicManager initialization runs on a worker and is joined only
# after it reports completion.
for marker in [
    "MusicStartupState music_startup;",
    "std::thread music_init_thread;",
    "music_init_done.store(true",
    "music.initialize()",
    "music_init_thread = std::thread",
    "if (!music_startup.ready()) return;",
]:
    if marker not in app_main_cpp:
        fail(f"Android non-blocking music bootstrap marker missing from main.cpp: {marker}")
if "updateMusic();\n        if(directNumberEditing())" in app_main_cpp:
    fail("updateMusic must run after SDL_RenderPresent so audio bootstrap cannot delay visible input feedback")

# Web music must be isolated from SDL input/audio, and every backend must fail open.
web_music_path = ROOT / "src/app/music_manager_web.cpp"
if not web_music_path.is_file():
    fail("missing dedicated Web Audio music backend: src/app/music_manager_web.cpp")
else:
    web_music_cpp = web_music_path.read_text(encoding="utf-8")
    for marker in ["AudioContext", "decodeAudioData", "passive: true", "ctx.suspend()", "ctx.resume()"]:
        if marker not in web_music_cpp:
            fail(f"Web music backend missing non-blocking Web Audio marker: {marker}")
    for forbidden in ["SDL_OpenAudioDeviceStream", "SDL_InitSubSystem", "SDL_PutAudioStreamData", "preventDefault(", "stopPropagation("]:
        if forbidden in web_music_cpp:
            fail(f"Web music backend must not couple audio to SDL/browser input: {forbidden}")

if "list(APPEND FASTTRIS_APP_SOURCES src/app/music_manager_web.cpp)" not in cmake_text:
    fail("CMake must compile the dedicated Web Audio backend for Emscripten")
if 'if(NOT EMSCRIPTEN AND ".ogg" IN_LIST FASTTRIS_MUSIC_EXTENSIONS)' not in cmake_text:
    fail("native OGG decoder dependency must be excluded from Emscripten")
if 'if(NOT EMSCRIPTEN AND ".mp3" IN_LIST FASTTRIS_MUSIC_EXTENSIONS)' not in cmake_text:
    fail("native MP3 decoder dependency must be excluded from Emscripten")

on_event_match = re.search(r"SDL_AppResult onEvent\(SDL_Event& ev\) \{(.*?)\n    SDL_AppResult iterate\(\)", app_main_cpp, flags=re.S)
if on_event_match and "ensureMusicInitialized(true)" in on_event_match.group(1):
    fail("SDL input event handling must never synchronously initialize Web music")
if "#if defined(__EMSCRIPTEN__)\n        ensureMusicInitialized(false);" not in app_main_cpp:
    fail("Web music backend must be initialized independently of user input")
if "fatal_error" not in music_manager_cpp or "SDL_DestroyAudioStream(impl_->output)" not in music_manager_cpp:
    fail("native MusicManager must tear down only music after fatal runtime audio failure")


# Cross-platform application icon regression checks.
icon_source = ROOT / "assets/icon/icon.png"
if not icon_source.is_file():
    fail("missing canonical FasTris icon source: assets/icon/icon.png")

icon_files = [
    "assets/icon/FasTris.ico",
    "assets/icon/FasTris.icns",
    "src/app/app_icon_data.hpp",
    "src/app/window_icon.hpp",
    "src/app/window_icon.cpp",
    "platform/windows/FasTris.rc.in",
    "platform/web/favicon.ico",
    "platform/web/favicon.png",
    "platform/web/apple-touch-icon.png",
    "platform/web/icon-192.png",
    "platform/web/icon-512.png",
    "platform/web/manifest.webmanifest",
    "platform/linux/com.draconov.fastris.desktop",
    "platform/linux/com.draconov.fastris.png",
]
for rel in icon_files:
    if not (ROOT / rel).is_file():
        fail(f"missing cross-platform icon asset/integration file: {rel}")

if (ROOT / "assets/icon/FasTris.ico").is_file():
    ico = (ROOT / "assets/icon/FasTris.ico").read_bytes()
    if len(ico) < 6 or ico[:4] != b"\x00\x00\x01\x00":
        fail("Windows FasTris.ico is not a valid ICO container")
if (ROOT / "assets/icon/FasTris.icns").is_file():
    icns = (ROOT / "assets/icon/FasTris.icns").read_bytes()
    if len(icns) < 8 or icns[:4] != b"icns":
        fail("macOS FasTris.icns is not a valid ICNS container")

web_shell = (ROOT / "platform/web/shell.html").read_text(encoding="utf-8")
for marker in [
    'rel="icon" href="favicon.png"',
    'rel="shortcut icon" href="favicon.ico"',
    'rel="apple-touch-icon" href="apple-touch-icon.png"',
    'rel="manifest" href="manifest.webmanifest"',
]:
    if marker not in web_shell:
        fail(f"web shell is missing icon metadata: {marker}")

for marker in [
    "enable_language(RC)",
    "platform/windows/FasTris.rc.in",
    "MACOSX_BUNDLE_ICON_FILE \"FasTris.icns\"",
    "${CMAKE_CURRENT_SOURCE_DIR}/platform/web/${_web_icon}",
    "platform/linux/com.draconov.fastris.desktop",
]:
    if marker not in cmake_text:
        fail(f"CMake icon integration missing marker: {marker}")

if '#include "window_icon.hpp"' not in app_main_cpp or "setFasTrisWindowIcon(win);" not in app_main_cpp:
    fail("desktop SDL window must apply the FasTris runtime icon after window creation")

linux_desktop_path = ROOT / "platform/linux/com.draconov.fastris.desktop"
if linux_desktop_path.is_file():
    linux_desktop = linux_desktop_path.read_text(encoding="utf-8")
    for marker in ["Name=FasTris", "Exec=FasTris", "Icon=com.draconov.fastris", "Categories=Game;BlocksGame;"]:
        if marker not in linux_desktop:
            fail(f"Linux desktop entry missing marker: {marker}")

manifest_text = (ROOT / "platform/android/app/src/main/AndroidManifest.xml").read_text(encoding="utf-8")
for marker in ['android:icon="@mipmap/ic_launcher"', 'android:roundIcon="@mipmap/ic_launcher_round"']:
    if marker not in manifest_text:
        fail(f"Android launcher icon wiring regressed: {marker}")

for marker in ["favicon.png", "manifest.webmanifest", "apple-touch-icon.png", "icon-192.png", "icon-512.png"]:
    if marker not in release_workflow:
        fail(f"release.yml must package web icon asset: {marker}")
if "com.draconov.fastris.desktop" not in release_workflow or "com.draconov.fastris.png" not in release_workflow:
    fail("release.yml must package Linux desktop icon integration")
if "build-web/favicon.png" not in pages_workflow or "build-web/manifest.webmanifest" not in pages_workflow:
    fail("pages.yml must verify generated Web icon/PWA files")

if errors:
    for error in errors:
        print(f"ERROR: {error}", file=sys.stderr)
    raise SystemExit(1)

print(f"FasTris repository sanity checks passed (version {version}).")

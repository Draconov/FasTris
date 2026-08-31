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

if errors:
    for error in errors:
        print(f"ERROR: {error}", file=sys.stderr)
    raise SystemExit(1)

print(f"FasTris repository sanity checks passed (version {version}).")

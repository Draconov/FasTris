#!/usr/bin/env bash
set -euo pipefail

: "${SDL_ANDROID_URL:?SDL_ANDROID_URL must be set}"
: "${SDL_ANDROID_SHA256:?SDL_ANDROID_SHA256 must be set}"

archive="${RUNNER_TEMP:-/tmp}/sdl-android.zip"
extract="${RUNNER_TEMP:-/tmp}/sdl-android"
dest="platform/android/app/libs/SDL3.aar"

rm -rf "$extract"
mkdir -p "$extract" "$(dirname "$dest")"

curl --fail --location --retry 4 --retry-all-errors --retry-delay 2 \
  "$SDL_ANDROID_URL" -o "$archive"
printf '%s  %s\n' "$SDL_ANDROID_SHA256" "$archive" | sha256sum -c -
unzip -q "$archive" -d "$extract"

mapfile -t aars < <(find "$extract" -type f -name 'SDL3-*.aar' -print)
if [[ ${#aars[@]} -ne 1 ]]; then
  echo "Expected exactly one SDL3 .aar in the Android archive; found ${#aars[@]}." >&2
  find "$extract" -type f -name '*.aar' -print >&2 || true
  exit 1
fi

cp "${aars[0]}" "$dest"
test -s "$dest"
echo "Prepared $dest"

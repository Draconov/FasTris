# Releasing FasTris

FasTris has one application-version source: the repository-root `VERSION` file.

The intended public repository is `https://github.com/Draconov/FasTris`. The release workflow explicitly requests `contents: write` only for its final publishing job, so it works with GitHub's read-only default token permissions while keeping build jobs read-only.

## Normal release

1. Change `VERSION`, for example from `0.1.0` to `0.2.0`.
2. Commit the version change together with the code you want in that release.
3. Push the commit to `main`.
4. `.github/workflows/release.yml` notices that `vX.Y.Z` does not exist and builds every release target.
5. Only after all platform builds succeed, the workflow creates GitHub Release `vX.Y.Z` and uploads the packages.

The release workflow checks every push to `main`, but it immediately skips platform release jobs when the current `VERSION` is already tagged. This also means a failed first release can be fixed and retried by pushing the CI fix without changing `VERSION`.

Do **not** edit version numbers in CMake, Gradle, source code, or workflow artifact names. They all read `VERSION`.

## Generated release assets

- `FasTris-vX.Y.Z-windows-x64.zip`
- `FasTris-vX.Y.Z-linux-x86_64.tar.gz`
- `FasTris-vX.Y.Z-macos-universal.zip` (Intel + Apple Silicon)
- `FasTris-vX.Y.Z-web.zip`
- `FasTris-vX.Y.Z-android.apk` when Android signing secrets are configured
- `FasTris-vX.Y.Z-android-unsigned.apk` otherwise
- `SHA256SUMS.txt`

GitHub also automatically provides source `.zip` and `.tar.gz` archives for the release tag.

## Android signing (one-time repository setup)

For an installable production APK with a stable upgrade signature, configure these GitHub Actions repository secrets:

- `ANDROID_KEYSTORE_BASE64` — base64 of the `.jks`/`.keystore` file
- `ANDROID_KEY_ALIAS`
- `ANDROID_KEYSTORE_PASSWORD`
- `ANDROID_KEY_PASSWORD`

Example for encoding the keystore on Linux/macOS:

```bash
base64 -i fastris-release.jks | tr -d '\n'
```

On PowerShell:

```powershell
[Convert]::ToBase64String([IO.File]::ReadAllBytes('fastris-release.jks'))
```

Never commit the private release keystore to the repository. If the secrets are absent, CI intentionally publishes an **unsigned** APK instead of inventing a temporary key that would make future Android upgrades incompatible.

## macOS signing

The workflow creates a universal macOS `.app` automatically. It is unsigned/not notarized unless Apple Developer signing is added later. The build itself does not require an Apple Developer account.

## Manual rebuild of a release

Run the `release` workflow with **Run workflow**. Manual rebuilds are allowed only when `vX.Y.Z` already points to the selected `main` commit; generated assets are then replaced instead of creating a duplicate release.

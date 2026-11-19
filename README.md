# NoiseBase

Automatic dialogue denoising for Windows, available as a VST3 effect and a standalone application.

[Download the latest NoiseBase release for Windows x64](https://github.com/lakam99/NoiseBase/releases/latest)

![NoiseBase Walnut interface](docs/images/walnut.png)

NoiseBase learns persistent background noise from pauses in speech and applies spectral noise reduction. A local Silero voice activity detector helps keep speech out of the noise profile. Audio processing runs on your computer without an account, API key, or upload.

The standalone application scans an entire recording before preview or export. The VST3 learns from the audio played through it; select **HOLD** to freeze a learned profile.

## Features

- Automatic noise profiling with speech-aware learning.
- Reduction, voice detail, dry/clean blend, output trim, and latency-aligned bypass.
- Mono and stereo processing with a shared stereo mask.
- Standalone file loading, automatic scanning, playback, and WAV export.
- Ivory, Graphite, and Walnut skins with generated PNG faceplates and hardware artwork. Labels, needles, scales, and knob pointers remain live.
- Host automation and saved control/skin state.

## Install and use

Download and extract the Windows ZIP from [Releases](https://github.com/lakam99/NoiseBase/releases).

Run `Install-NoiseBase.ps1` from the extracted `NoiseBase` folder. It installs the standalone app, its update helpers, a Start menu shortcut, and the VST3 in your user account. It requires no administrator rights. Use Windows Installed Apps to uninstall it; recordings and user settings are preserved.

For standalone use, launch **NoiseBase** from Start. Choose **LOAD TAPE**, wait for the automatic scan, then **PLAY** and adjust the controls. **PRINT WAV** writes a denoised copy and preserves the source. Portable use through `Standalone/NoiseBase.exe` is also supported; keep all bundled DLLs and helper scripts beside it. In-app installation requires an installed copy.

The installer also installs the VST3. Alternatively, copy the entire `VST3/NoiseBase.vst3` directory into a VST3 plug-in folder. Restart or rescan the DAW. Leave **AUTO** selected while playing a passage containing a pause, then use **HOLD** if needed. **LEARN** clears the profile and starts learning again.

The application is installed at `%LOCALAPPDATA%/Programs/NoiseBase`; the VST3 goes to `%LOCALAPPDATA%/Programs/Common/VST3/NoiseBase.vst3`. Add the latter's parent folder to your DAW's plug-in locations if necessary. VST3 backups are retained outside that scan folder. The installer verifies payload hashes, protects unowned directories, refuses downgrades and in-use files, and rolls back failed file swaps. See the [Windows user guide](docs/Windows-user-guide.txt).

## Automatic updates

The standalone app checks [GitHub Releases](https://github.com/lakam99/NoiseBase/releases) on startup, at most daily after a successful check. **UPDATES** offers a manual check and an automatic-check toggle. A newer stable version offers **Download update**, then **Install and restart** after verification. Installation preserves settings and keeps the preceding version as a backup. Close DAWs using the VST3 before installing. The VST3 itself does not check the network or replace loaded modules.

Downloads must match GitHub's SHA-256 asset digest, `SHA256SUMS.txt`, and every payload file in `release-manifest.json`. Paths and package identity are checked before installation. These checks detect corruption; release authenticity depends on HTTPS and this repository's security. Releases are not Authenticode-signed.

## Release pipeline

[Windows build and release](https://github.com/lakam99/NoiseBase/actions/workflows/release.yml) runs on main, pull requests, and stable version tags. Only a tag matching the CMake version publishes a release. The pipeline scans source/history, builds both formats, verifies audio and file workflows, tests installation/rollback/update policy, scans nested release archives, then uploads and verifies assets before publishing. Actions are pinned to full commit hashes; publishing uses a separate job with write permission.

To release a new version, update the CMake project version and release notes, commit, then push the corresponding `vMAJOR.MINOR.PATCH` tag. Manual workflow runs build and verify without publishing.

If publishing fails after a successful build, use **Resume verified release publication** with the original workflow run ID and stable tag. It verifies that the successful build belongs to that exact tag commit and repository, retrieves its artifact, and retries upload/digest verification without rebuilding. Published releases are left unchanged unless the existing assets exactly match the checksums.

## Build from source

This repository includes the JUCE source, Silero model, generated artwork, and ONNX Runtime CPU headers/runtime required by the current build. No submodule initialization or dependency download is required. Three unused JUCE Android demo Firebase configuration files are omitted; see the [vendor notes](third_party/NOISEBASE-VENDOR-NOTES.md).

Requirements: Windows x64, Visual Studio 2022 C++ Build Tools with Windows SDK, and CMake 3.22 or newer.

```powershell
git clone https://github.com/lakam99/NoiseBase.git
cd NoiseBase
.\Build-source.ps1
```

Build outputs:

- `build/NoiseBaseStandalone_artefacts/Release/NoiseBase.exe`
- `build/NoiseBase_artefacts/Release/VST3/NoiseBase.vst3/`

The build copies the required runtime DLLs alongside both formats. See [BUILDING.txt](BUILDING.txt) for the Ninja option and command-line verification utilities.

## Verification and limits

The Windows build passed engine timing/bypass/stereo checks at 44.1, 48, and 96 kHz, VST3 discovery/loading/state/editor creation and audio processing in a JUCE host, and the standalone load/scan/preview/export workflow. An independent visual critic reviewed all three native skin renders and the loaded recording state. The 1.1.0 audio renders matched 1.0.0 sample for sample on the validation excerpt.

See the [verification report](docs/Verification-1.1.0.json) and [visual review](docs/Visual-review-1.1.0.txt). Studio One and physical audio devices have not been tested.

Reported latency is 2048 samples at 44.1/48 kHz and 4096 samples at 96 kHz; standalone file export compensates this delay. This processor targets dialogue with persistent background noise. Strong wind impacts, clipping, and overlapping voices may remain. Large reduction settings can soften speech, so audition the result.

## Artwork and licenses

The interface uses generated PNG assets rather than SVG artwork. The images and exact built-in image generation prompts are in [resources/generated](resources/generated). Additional skin previews are in [docs/images](docs/images).

NoiseBase source is supplied under **AGPL-3.0-or-later**; see [LICENSE](LICENSE). This build uses JUCE's AGPL option. Third-party code, models, and notices retain their respective licenses, preserved in [Licenses](Licenses), `third_party`, and `resources`.

Included versions: JUCE 9.0.6 and ONNX Runtime CPU 1.32.0.

# NoiseBase

Automatic dialogue denoising for Windows, available as a VST3 effect and a standalone application.

[Download NoiseBase 1.1.0 for Windows x64](https://github.com/lakam99/NoiseBase/releases/tag/v1.1.0)

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

For standalone use, run `Standalone/NoiseBase.exe`. Keep both ONNX Runtime DLLs beside it. Choose **LOAD TAPE**, wait for the automatic scan, then **PLAY** and adjust the controls. **PRINT WAV** writes a denoised copy and preserves the source.

For VST3 use, run `Install-VST3.ps1` from the extracted release folder, or copy its entire `VST3/NoiseBase.vst3` directory into a VST3 plug-in folder. Restart or rescan the DAW. Leave **AUTO** selected while playing a passage containing a pause, then use **HOLD** if needed. **LEARN** clears the profile and starts learning again.

The default installer destination is `%LOCALAPPDATA%/Programs/Common/VST3`. It refuses to overwrite an existing installation. To upgrade, close the DAW and replace the old complete bundle manually. See the [Windows user guide](docs/Windows-user-guide.txt) for controls, installation details, and export behavior.

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

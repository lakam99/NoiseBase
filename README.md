# NoiseBase

Automatic dialogue denoising and procedural ambience for Windows x64 and Apple Silicon macOS, available as a VST3 effect and a standalone application.

[Download the latest NoiseBase release](https://github.com/lakam99/NoiseBase/releases/latest)

![NoiseBase Walnut interface](docs/images/walnut.png)

NoiseBase learns persistent background noise from pauses in speech and applies spectral noise reduction. A local Silero voice activity detector helps keep speech out of the noise profile. Audio processing runs on your computer without an account, API key, or upload.

The standalone application scans the entire recording and follows different backgrounds across stitched clips during preview and export. The VST3 automatically follows noise changes in the audio played through it. Select **HOLD** to freeze the current profile.

## Features

- Automatic whole-file noise profiling, with local profiles for changing backgrounds and speech-aware live adaptation.
- Automatic reduction of short high-frequency clothing/hair friction bursts, with harmonic and speech-onset protection.
- Per-instance session caches for live CLEAN and sidechain profiles, restored when a saved DAW song reopens.
- Reduction, voice detail, dry/clean blend, output trim, and latency-aligned bypass.
- Continuous procedural ambience from one reference clip, saved with the session.
- Optional VST3 sidechain reference: learn another track's background and generate matching ambience while the reference plays.
- Optional gap bridging with compensated lookahead, an adjustable maximum gap, and transitions into the next clip's background.
- Removed-noise monitoring, plus procedural seamless-loop and noise-base WAV export.
- Mono and stereo processing with a shared stereo mask.
- Standalone file loading, automatic scanning, playback, and WAV export.
- Classic cyan VFD for live values, filename, tape status, progress, and time.
- Generated transport icons for play/pause, stop, loop, and return to beginning.
- Ivory, Graphite, and Walnut skins with generated PNG faceplates and hardware artwork. Labels, needles, scales, and knob pointers remain live.
- Host automation and saved control/skin state.

## Install and use

**Apple Silicon Mac (M1/M2 and later):** requires macOS 14 Sonoma or later. Extract the `macOS-arm64.zip`, copy `NoiseBase.app` to Applications, and copy the complete `NoiseBase.vst3` bundle to `~/Library/Audio/Plug-Ins/VST3`. Use a native arm64 DAW. The Mac package is ad-hoc signed, not Developer ID signed or notarized; see the [Mac guide](docs/macOS-user-guide.txt) and [Apple's opening guidance](https://support.apple.com/en-us/102445). Mac update checks open the release page for manual installation. No Intel or AU build is included.

**Windows x64:**

Download and extract the Windows ZIP from [Releases](https://github.com/lakam99/NoiseBase/releases).

Run `Install-NoiseBase.ps1` from the extracted `NoiseBase` folder. It installs the standalone app, its update helpers, a Start menu shortcut, and the VST3 in your user account. It requires no administrator rights. Use Windows Installed Apps to uninstall it; recordings and user settings are preserved.

For standalone use, launch **NoiseBase** from Start. Choose **LOAD CLIP**, wait for the automatic scan, then **PLAY** and adjust the controls. **PRINT WAV** writes the selected output and preserves the source. Portable use through `Standalone/NoiseBase.exe` is also supported; keep all bundled DLLs and helper scripts beside it. In-app installation requires an installed copy.

The installer also installs the VST3. Alternatively, copy the entire `VST3/NoiseBase.vst3` directory into a VST3 plug-in folder. Restart or rescan the DAW. Leave **AUTO** selected: learning starts automatically and follows stable changes during speech pauses. **HOLD** keeps the active floor fixed. With a loaded file, **LEARN** re-analyzes the entire file and selects AUTO again. Without a loaded file, it resets learning from incoming DAW audio. A conventional VST3 cannot read future or unplayed host audio; use **LOAD CLIP** to scan a complete file for file export.

The application is installed at `%LOCALAPPDATA%/Programs/NoiseBase`; the VST3 goes to `%LOCALAPPDATA%/Programs/Common/VST3/NoiseBase.vst3`. Add the latter's parent folder to your DAW's plug-in locations if necessary. VST3 backups are retained outside that scan folder. The installer verifies payload hashes, protects unowned directories, refuses downgrades and in-use files, and rolls back failed file swaps. See the [Windows user guide](docs/Windows-user-guide.txt).

**Save your DAW song after NoiseBase has learned.** Each instance embeds its live CLEAN noise floor and sidechain profile cache in the plug-in state saved with that song. Reopening the saved song restores those profiles without another learning pass. AUTO recalls known timeline regions and continues adapting to new or changed material; LEARN clears the selected source's cache and rebuilds it as audio arrives. A live VST3 cannot scan unplayed audio. Up to 64 statistical regions per source are retained; no recorded waveform, external cache file or changes to the source recording are needed. Moving or replacing clips can require new learning at their new timeline positions.

The standalone tape deck uses icon buttons. **Play/pause** toggles playback, **stop** stops and rewinds, **return to beginning** jumps to zero while preserving play or pause, and **loop** repeats the recording during preview. A green jewel and `LOOP ON` in the VFD indicate that repeat is enabled. Disabling loop finishes the current pass. Clean and removed-noise exports process the original duration once. Procedural mode generates continuously and has its own export duration.

## Procedural noise profile and continuous ambience

Choose **LOAD CLIP** in either format, or drop one reference recording onto the faceplate. After the quiet-section scan, select **NOISE BASE** and **PROCEDURAL**. The generator creates fresh background continuously from the learned noise spectrum, channel levels, stereo correlation and gentle level variation. It carries on beyond the reference length and with silent host input. The standalone play/pause button controls generation; its elapsed display shows `INF`. The VST3 generates while the host processes it; host suspension or stopped processing also stops generation.

The compact statistical model is saved in DAW state and standalone preferences. It stores no recorded waveform and does not need the source file after restoration. It requires at least 0.35 seconds of confidently detected quiet background; uncertain floor estimates are refused for synthesis. Detection can misclassify material, so audition the result. It generates a statistical ambience bed rather than reconstructing separate birds, vehicles, impacts or other recognizable events.

**PRINT WAV** defaults to a **60-second seamless loop**, with an editable 1–3600 second duration. Its sample rate and mono/stereo layout follow the reference. MIX sets generated level, OUTPUT TRIM adjusts gain. Reduction and voice detail affect denoising, not procedural synthesis. Bypass restores the original, including dialogue. Older sessions retain their removed-noise route until you select PROCEDURAL explicitly.

## Another track as a procedural reference (VST3)

Insert NoiseBase on a dedicated ambience track or bus. Enable its **Reference** sidechain in your DAW and send the reference/dialogue track to that input. Select **REFERENCE → SIDECHAIN**, **NOISE BASE**, **PROCEDURAL**, and **AUTO**. Play a passage containing background between words: NoiseBase learns spectral shape, stereo balance/correlation and level variation, then generates fresh ambience on its main output. The reference signal is never mixed directly into that output. Use MIX and OUTPUT TRIM to set the bed level.

Sidechain output follows DAW playback and actual reference activity. Host stop, a disconnected send or digital silence fades the bed out; reference audio returning during playback fades it back in. **HOLD** freezes the texture while still following playback/input. In **AUTO**, seeking or wrapping the timeline recalls a cached destination profile when available; uncached destinations wait for a new quiet profile. HOLD retains its model across jumps. **LEARN** clears the reference cache and restarts analysis. The cache and source switch are saved with DAW state, but restored sidechain output still waits for reference audio. The input meter and numeric VFD level show the sidechain, and the VFD displays host play/stop and position when supplied. Offline rendering follows incoming reference audio even if the host's live transport flag is stopped. Hosts without transport information use input activity alone. **CLIP** retains continuous loaded-file generation; **LOAD CLIP** selects it automatically. Clean/removed-noise processing and bypass retain their main-input routing.

The sidechain learns causally as audio arrives, rather than reading the other track's future timeline. Speech detection uses a short look-ahead on the analysis worker and can still misclassify material; audition the generated bed. Synthesis matches a statistical texture, not recognizable events or spoken words. **PRINT WAV** can export the current sidechain model as a 60-second seamless loop. Standalone uses loaded clips rather than DAW sidechain routing.

For continuous backgrounds across edits, switch **FOLLOW → BRIDGE** and adjust **GAP TIME** (0.05–2 seconds, default 0.75). A gap shorter than that setting keeps the previous clip's ambience when reference audio is visible ahead. The next clip's learned profile takes over at its buffered timeline position with a smooth spectral transition. Longer gaps and the end of the supplied audio fade to silence; the bed does not start early before the first clip or re-enter early after a longer gap. HOLD preserves its fixed texture while using the same gap decisions.

BRIDGE buffers two seconds and reports that additional latency to the DAW for compensation. The gap slider changes the bridging distance without changing latency. Disabling BRIDGE restores normal latency; hosts may resynchronize when it changes. This checks only the selected future window in received audio, not the entire DAW arrangement. A new profile still needs sufficiently quiet reference material. Input metering shows the incoming sidechain, which leads the buffered output while lookahead is enabled.

## Removed-noise monitoring

Select **NOISE BASE** and **REMOVED** for the denoiser's estimated removed-noise stem, the 1.4 behavior. It can contain dialogue bleed where speech and noise overlap. Let AUTO learn a reliable floor from a pause, then use HOLD if desired. VOICE DETAIL protects speech from removal. MIX scales removed noise; zero MIX or zero REDUCTION gives silence in this mode. Removed-noise exports retain source timing, channel layout and supported WAV format/metadata. CLEAN selects denoised dialogue again.

## Automatic rustle reduction

CLEAN processing also detects short, broadband high-frequency friction bursts typical of hair or clothing brushing a lavalier. It follows changes in spectral shape and burst timing, protects narrow voice harmonics, and treats isolated speech onsets conservatively. Repeated irregular strokes permit stronger attenuation. Confidently detected bursts are excluded from live and whole-file noise-floor learning.

There is no extra switch, knob, training step or added lookahead delay. It works during live VST3 processing, standalone preview and file export, including HOLD. REDUCTION sets the existing attenuation limit; VOICE DETAIL influences speech protection. Zero REDUCTION, dry blend and bypass retain their existing behavior. Saved session profiles remain compatible.

This is selective reduction, not guaranteed separation of every clothing sound from speech. Ambiguous consonants receive lighter treatment; dense friction overlapping words can remain. Audition the dialogue as well as the removed-noise output.

## Window appearance

The generated cyan/glass icon is embedded in the app, taskbar window and Windows module resources. The standalone's native title bar and utility-window colors follow Windows app light/dark mode while keeping the selected Ivory, Graphite or Walnut faceplate. Host-owned VST3 window chrome remains the DAW's responsibility.

## Automatic updates

The standalone app checks [GitHub Releases](https://github.com/lakam99/NoiseBase/releases) on startup, at most daily after a successful check. **UPDATES** offers a manual check and an automatic-check toggle. On Windows, a newer stable version offers **Download update**, then **Install and restart** after verification. The VFD shows a segmented download bar and percentage, followed by an activity indicator while verifying and unpacking. Installation preserves settings and keeps the preceding version as a backup. Close DAWs using the VST3 before installing. The VST3 itself does not check the network or replace loaded modules.

Downloads must match GitHub's SHA-256 asset digest, `SHA256SUMS.txt`, and every payload file in `release-manifest.json`. Paths and package identity are checked before installation. These checks detect corruption; release authenticity depends on HTTPS and this repository's security. Releases are not Authenticode-signed.

## Release pipeline

[Windows and Apple Silicon release](https://github.com/lakam99/NoiseBase/actions/workflows/release.yml) runs on main, pull requests, and stable version tags. Only a tag matching the CMake version publishes a release. Both platform jobs must pass. The pipeline scans source/history, builds both formats, verifies audio and file workflows, tests Windows installation/rollback/update policy and Mac signatures, scans nested release archives, then uploads and verifies assets before publishing. Actions are pinned to full commit hashes; publishing uses a separate job with write permission.

To release a new version, update the CMake project version and release notes, commit, then push the corresponding `vMAJOR.MINOR.PATCH` tag. Manual workflow runs build and verify without publishing.

If publishing fails after a successful build, use **Resume verified release publication** with the original workflow run ID and stable tag. It verifies that the successful build belongs to that exact tag commit and repository, retrieves its artifact, and retries upload/digest verification without rebuilding. Published releases are left unchanged unless the existing assets exactly match the checksums.

## Build from source

This repository includes the JUCE source, Silero model, generated artwork, and ONNX Runtime CPU headers/runtime required by the current build. No submodule initialization or dependency download is required. Three unused JUCE Android demo Firebase configuration files are omitted; see the [vendor notes](third_party/NOISEBASE-VENDOR-NOTES.md).

Requirements: Windows x64, Visual Studio 2022 C++ Build Tools with Windows SDK, and CMake 3.22 or newer.

For Apple Silicon, install Xcode command-line tools and CMake, then run `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=14.0` followed by `cmake --build build --target NoiseBase_VST3 NoiseBaseStandalone --parallel 3`. The app is in `build/NoiseBaseStandalone_artefacts/Release/NoiseBase.app`. `python3 scripts/Test-Mac.py` runs native validation on a Mac with a graphical login session.

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

Release verification covers engine timing/bypass/stereo checks at 44.1, 48, and 96 kHz, transient friction reduction with voice/sibilant protection, stitched-background scans, real VST3 sidechain routing/transport/silence/seek behavior, lookahead gap limits and profile timing, CLEAN/sidechain cache restoration in fresh processes with AUTO enabled, delayed worker cleanup, and the standalone file workflow. An independent visual critic reviewed the native skins, VFD update progress, reference switch and gap controls. See the [1.10.0 release notes](docs/Release-1.10.0.md); native reports are included in release packages. The reported Studio Pro freeze has not been reproduced in that DAW, and physical audio devices have not been certified by these tests.

Reported latency is 2048 samples at 44.1/48 kHz and 4096 samples at 96 kHz; standalone file export compensates this delay. This processor targets dialogue with persistent background noise. Strong wind impacts, clipping, and overlapping voices may remain. Large reduction settings can soften speech, so audition the result.

## Artwork and licenses

The interface uses generated PNG assets rather than SVG artwork. The images and exact built-in image generation prompts are in [resources/generated](resources/generated). Additional skin previews are in [docs/images](docs/images).

NoiseBase source is supplied under **AGPL-3.0-or-later**; see [LICENSE](LICENSE). This build uses JUCE's AGPL option. Third-party code, models, and notices retain their respective licenses, preserved in [Licenses](Licenses), `third_party`, and `resources`.

Included versions: JUCE 9.0.6 and ONNX Runtime CPU 1.32.0.

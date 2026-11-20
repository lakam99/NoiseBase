NoiseBase 1.5.1 adds native Apple Silicon macOS builds alongside Windows x64.

- The Mac ZIP contains NoiseBase.app and the complete NoiseBase.vst3 bundle for M-series Macs, including M2. Minimum macOS is 14 Sonoma, as required by the bundled speech-detector runtime. An arm64/native DAW is required for the VST3; no Intel or AU build is included.
- Both Mac bundles include the local ONNX speech detector, procedural ambience generator, generated skins and app icon. No Rosetta, external runtime, account or audio upload is required.
- Mac release checks open GitHub Releases for manual installation. Windows' verified-download installer/update behavior is preserved.
- The Mac bundles have local ad-hoc code signatures, not Developer ID signing or Apple notarization. Read the included macOS guide and Apple's approval guidance before opening a downloaded build: https://support.apple.com/en-us/102445

Install: extract the Mac ZIP, copy NoiseBase.app to Applications, and copy NoiseBase.vst3 to ~/Library/Audio/Plug-Ins/VST3 before rescanning the native DAW. Close older instances before replacing them. Source, platform checksums and verification reports are included with the release.

The automated Mac job verifies native arm64 engine processing, VST3 host loading/state, silent-input procedural generation, saved-profile restore, file scanning/export, a 60-second procedural render, UI/icon resources and bundle signatures. It does not certify physical audio devices or the user's particular DAW.

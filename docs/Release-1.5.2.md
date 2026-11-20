NoiseBase 1.5.2 introduces the verified Apple Silicon distribution.

- Native NoiseBase.app and NoiseBase.vst3 for M-series Macs, including M2. Minimum macOS is 14 Sonoma; use a native arm64 DAW for the VST3. Rosetta, Intel and AU formats are not included.
- Includes the local speech detector, saved procedural noise profiles, continuous ambience generation, 60-second default seamless exports, generated skins and app icon.
- Speech-runtime environment destruction now precedes dynamic-runtime teardown, preventing the Mac shutdown crash found during verification.
- The generated VFD bezel is drawn from cached raster tiles with normalized origins for Quartz. Mac hosted-editor verification captures the embedded native NSView instead of the wrapper's black backing.
- Mac update checks open GitHub Releases for manual installation. Windows' verified-download installer behavior is preserved.

Extract the Mac ZIP, copy NoiseBase.app to Applications and the entire NoiseBase.vst3 bundle to ~/Library/Audio/Plug-Ins/VST3, then rescan the native DAW. The bundles have ad-hoc signatures, not Developer ID signatures or Apple notarization. Apple's approval guidance: https://support.apple.com/en-us/102445

Native arm64 audio, VST3, saved-profile, file workflow, procedural export, UI/icon, shutdown and code-signature tests run on the Mac build runner. Hardware devices and the user's particular DAW are not certified. The included Mac guide and verification reports describe the tested scope.

NoiseBase 1.4.0 adds noise-only monitoring/export, a generated VFD-style application icon, and OS-aware standalone window colors.

- The hardware switch selects CLEAN or NOISE ONLY. In the VST3, NOISE ONLY plays the estimated noise removed by the denoiser while the host track runs. The new host-automatable parameter is saved with the project. Older project states default to clean output.
- In the standalone app, the same switch selects both preview and PRINT WAV output. Noise exports retain the original sample count, sample rate, mono/stereo layout and supported WAV format/metadata. The source is preserved.
- The VFD identifies CLEAN, NOISE ONLY or ORIGINAL / BYPASS. Bypass always restores the original; it overrides noise-only and output trim. MIX scales the removed component in noise-only mode, so zero mix or zero reduction produces silence apart from numerical rounding.
- Noise-only is the removed-noise estimate, not guaranteed speech-free separation. Dialogue can bleed into it where speech overlaps noise. Learn a reliable quiet floor before judging it, and use VOICE DETAIL to protect speech from removal.
- The generated cyan/glass icon is embedded in the Windows executable and VST3 module, and used by the standalone window and existing Start menu installation.
- The standalone title bar, window background and JUCE utility dialogs follow Windows app light/dark mode, including changes while running. The selected hardware faceplate remains manual. The VST3 host controls its own outer window chrome.

Verification covers clean-plus-noise reconstruction, known foreground/background test tones, zero-mix/zero-reduction silence, bypass, stereo phase, timing at 44.1/48/96 kHz, live-host noise-only processing, preset compatibility, WAV export, embedded icon resources, and native light/dark title-bar flags. No audio is uploaded for processing.

Close NoiseBase and any DAW using its VST3 before installing. Update through UPDATES or run the installer from the Windows release package.

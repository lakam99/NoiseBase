NoiseBase 1.5.0 adds a procedural ambience generator learned from a single reference clip.

- LOAD CLIP scans quiet, speech-guarded sections. NOISE BASE + PROCEDURAL then generates fresh background indefinitely, including on silent VST3 input and beyond the reference's duration. It is not a loop of recorded source audio.
- The learned model contains aggregate noise spectra, per-channel energy, frequency-dependent stereo correlation and level variation. Independent random spectra with continuous overlap-add and slow band variation create the new texture. No source waveform or speech snippets are stored in the model.
- Profiles are embedded in DAW state and standalone preferences. Restored profiles generate without reopening or requiring the source file. A clip with at least 0.35 seconds of confidently detected background is required; uncertain floor estimates cannot drive this generator.
- PRINT WAV defaults to a 60-second seamless procedural loop. Duration is editable from 1 to 3600 seconds. Rendering streams with bounded memory. MIX controls generated level, OUTPUT TRIM sets gain; reduction/voice controls apply to denoising rather than synthesis.
- In standalone, play/pause controls continuous generation, stop resets the elapsed counter, and the VFD shows INF. The recording-loop button is unnecessary and disabled in this mode. In the VST3, the host clocks generation; LOAD CLIP and PRINT WAV are now available in its editor too.
- REMOVED retains 1.4's latency-aligned residual monitor. Old session states keep that routing until PROCEDURAL is explicitly selected. Bypass restores the original track in both modes.

This is a statistical ambience bed: it does not recreate distinct passing vehicles, bird calls, impacts or other recognizable events. Voice detection can misclassify material, so audition the learned texture. Physical-device/Studio One certification is not claimed.

Verification includes levels and stereo correlation at 44.1/48/96 kHz, seeded reproducibility with nonrepeating consecutive sections, malformed-profile rejection, seamless export boundaries matching a continuous generated sequence, profile restore without a source file, and sustained silent-input output beyond twice the reference duration. The existing denoising, VST3, transport, export, theme, icon and installer tests remain in the release pipeline.

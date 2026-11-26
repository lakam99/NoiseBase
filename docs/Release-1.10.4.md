# NoiseBase 1.10.4

Fix tonal, watery background artifacts during speech pauses. The denoiser previously gave instantaneous noise-bin peaks a fast path into its clean-signal estimate even when the voice detector indicated no speech. Those peaks could survive as changing narrow bands instead of a quieter version of the original background.

During confirmed speech pauses, the decision-directed prior now uses a slower, sample-rate-aware memory and reduces that instantaneous contribution. Detected speech and the existing speech hangover retain the fast response for voice attacks and consonants. The requested reduction floor still bounds suppression. No controls, latency, saved-state format or ambience-generation settings change.

A new regression verifies background waveform coherence and attenuation after speech at 44.1, 48 and 96 kHz, at both 14 and 24 dB reduction. All six cases fail against the previous engine and pass with this change. Existing tests cover dialogue/rustle protection, unity and bypass, meters, whole-track adaptation, reference input, procedural generation and profile persistence. Private user recordings are used only for local listening validation and are not included in the source or package.

# NoiseBase 1.10.3

Includes the Windows teardown fix and multi-instance regression checks from 1.10.2,
plus a correction to the reduction meter.

The meter now reads zero on digital silence and measures the attenuation actually
applied to the aligned signal. It follows Mix and Bypass and excludes Output Trim.
Previously it averaged internal spectral gains, which could show maximum reduction
with no input. The meter retains its existing 0–24 dB left-to-right scale.

New native checks cover silence before/after audio, known attenuation, dry Mix,
Bypass and output-trim independence at 44.1, 48 and 96 kHz. Audio output, parameter
IDs and saved profile formats are unchanged.

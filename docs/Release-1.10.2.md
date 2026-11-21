# NoiseBase 1.10.2

Fixes a Windows project-close hang when the last NoiseBase instance is removed
after an editor has been opened. The plugin could recreate Direct3D resources
while its DLL was unloading, blocking the host in graphics-driver cleanup.

- Keep skin and VFD images in CPU-backed storage and use software editor rendering.
- Release temporary native image-cache entries before DLL static teardown.
- Add a native multi-instance teardown regression host to Windows release checks:
  eight instances, repeated editor use, project close/reopen, actual module unloading,
  and worker-initiated closure marshalled to the VST3-required UI thread.
- Include teardown stage logs and reports in release packages.

The noise-reduction algorithm, lav-rustle cleanup, parameter IDs and saved profile
formats are unchanged. Existing sessions and learned profiles remain compatible.

Local mixed-project validation closed eight NoiseBase and eight VoxRestore instances
over five cycles without the reproduced deadlock. The longest measured processor-removal
phase was approximately 178 ms; this is not a timing guarantee for a full DAW project.
Native Studio Pro confirmation still requires reopening the affected song with this update.

Windows x64 and Apple Silicon packages follow the existing build and verification pipeline.
Mac packages are ad-hoc signed, not Developer ID signed or notarized.

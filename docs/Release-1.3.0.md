NoiseBase 1.3.0 adds a classic cyan vacuum fluorescent display and an icon-based standalone tape transport.

- Filename, tape status, elapsed/total time, control values, selected skin, scan/export progress, and update activity now appear in the VFD. Long lines scroll inside the glass.
- Generated PNG transport buttons provide return to beginning, play/pause, stop, and loop. The loop button has a green indicator and the VFD shows LOOP ON.
- Return to beginning preserves play or pause. Stop halts playback and rewinds. Loop repeats the whole recording during preview; turning it off finishes the current pass. Exports still print the recording once.
- The loop preference is remembered between standalone launches.
- The RedWeb text has been removed from the faceplate. Static hardware legends remain engraved.
- Ivory, Graphite, and Walnut retain their generated raster materials. The compact VST3 editor also uses the VFD for live values and status; tape transport remains a standalone feature.
- Reused preview captures now replace the previous PNG, preventing stale images during verification.

Transport verification covers the exact source-sample loop junction, playback beyond EOF, normal EOF after disabling loop, and return-to-start while playing or paused. The denoising algorithm and host parameter identifiers are unchanged.

Use **UPDATES → Check for updates** in the standalone app, or download the Windows package and run `Install-NoiseBase.ps1`. Close NoiseBase and any DAW using its VST3 before installing.

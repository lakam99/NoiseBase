NoiseBase 1.2.0 adds automatic GitHub Release checks to the standalone app, a verified updater, and a per-user Windows installer for the standalone app and VST3.

Download `NoiseBase-1.2.0-Windows-x64.zip`, extract it, and run `Install-NoiseBase.ps1` from its `NoiseBase` directory. The installer adds a Start menu shortcut and an Installed Apps entry. It installs the VST3 in the standard per-user VST3 directory. Rescan plug-ins in your DAW after installation.

The standalone app checks for stable releases on startup, at most once a day after a successful check. **UPDATES** provides a manual check and a switch for automatic checks. Downloads must match GitHub's asset digest, the published checksum manifest, and the package's payload hashes. Installation occurs after **Install and restart**. Close DAWs using the VST3 first. Settings and recordings are preserved, and the preceding installed version is retained as a backup.

Updates are delivered from this GitHub repository over HTTPS; they are not Authenticode-signed. No audio is sent to GitHub. The VST3 does not perform background networking or replace itself inside a running DAW; update it through the standalone app.

Tagged releases are built, tested, scanned for secrets (including vendored code and nested archives), packaged, and published by GitHub Actions. The updater/installer tests include corrupt payloads, URL validation, downgrade prevention, unknown-directory protection, in-use files, and rollback after a failed swap.

The generated analog artwork and denoising engine remain unchanged. Studio One and physical audio hardware are not part of the automated tests.

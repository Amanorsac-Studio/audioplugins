# Handoff

- Active milestone: M3 — Complete functional Windows suite
- Last completed task (2026-09-09): authored and shipped the factory preset banks for the analog line. 141 presets, 14 to 15 per product, embedded in each plugin and listed after Default, tagged by source and intent, read-only with save-as to the user folder (DEC-0016). Values are authored against our contracts, using the supplied Waves register for coverage only. All ten analog plugins build as Standalone and VST3; the analog engine audit passes with 0 failures including the new factory bank checks. Awaiting approval of the preset names before the installer. Outstanding before sale: the rack target does not build (KI-0008), and the server-signed licensing described in the integration document is not implemented (KI-0005).
- Last successful product command: `cmake --build --preset windows-release --target ALL_BUILD --parallel 2`.
- Last successful test command: `ctest --preset windows-release`.
- Last successful packaging command: `powershell -ExecutionPolicy Bypass -File installer/build-installer.ps1 -Configuration Release`.
- Current product failures: none.
- Final installer: `dist/Amanorsac_Studio_Mixing_Suite_0.1.2_Windows.exe`; SHA256 `20c85403db7d79bc0e4fcab086965919001acdf359337135f9fcbd19a456f5de`.
- Notes: a full Release build from clean takes well over ten minutes on this host; run it detached or with a long timeout. The uninstaller now performs direct, deterministic PowerShell cleanup.
- Remaining external validation: third-party DAW scan/automation exercise, approved audio-reference calibration, code signing, AAX, and macOS AU.
- Uncommitted state: entire repository is new and uncommitted; ready for an initial commit.

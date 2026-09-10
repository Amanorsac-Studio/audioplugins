# Status

- Milestone: M3 — Complete functional Windows suite (release engineering closed for this host)
- State: All D01-D10, A01-A10, and R01 products have processor-specific DSP, supplied per-product design surfaces with a paged live parameter inspector, VST3 and Standalone targets, and automated coverage. Release is the shipped configuration (DEC-0008, no LTO).
- Last known good build: complete Release `ALL_BUILD --parallel 2`; 21/21 VST3 and 21/21 Standalone artifacts, zero warnings. Debug matrix previously verified identically.
- Last known good test: 28/28 Release CTest pass plus 21/21 hidden Standalone startup smoke and 21/21 VST3 bundle/manifest audit.
- Installer: `dist/Amanorsac_Studio_Mixing_Suite_0.1.2_Windows.exe`, built from repaired Release artifacts, isolated install/uninstall verified (TEST-0033). SHA256 `20c85403db7d79bc0e4fcab086965919001acdf359337135f9fcbd19a456f5de`.
- Version: 0.1.2 everywhere (CMake project, installer filename, bootstrapper, install script, README).
- External release limitations: no generic third-party DAW scan, installer is unsigned, no AAX SDK/signing, no macOS AU build, and no approved audio-reference calibration set.
- Repository state: entire workspace is new and uncommitted; `git diff --check` clean after the 0.1.2 release build.

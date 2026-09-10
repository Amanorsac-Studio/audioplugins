# Amanorsac Studio Mixing Suite

JUCE 8/C++20 implementation of the complete Amanorsac Studio mixing suite: 20 individually loadable processors plus the analog-only `ANALOG MIX RACK`. Every product builds as VST3 and Standalone from a contract-derived parameter/state surface and uses processor-specific DSP.

## Build on Windows

```powershell
cmake --preset windows-vs2026
cmake --build --preset windows-release --target ALL_BUILD --parallel 2
ctest --preset windows-release
```

JUCE is pinned to `8.0.15`. Release is the shipped configuration and builds without link-time optimization (see DEC-0008); use the `windows-debug` build/test presets for debugging. Build products remain under `build/windows-vs2026`; they are not installed into system plugin folders. Keep Windows builds at `--parallel 2` on this host to avoid process exhaustion.

## Implemented products

- D01-D10: precision/dynamic EQ, multiband dynamics, limiter, de-esser, resonance control, spectral shaping, multi-tap delay, algorithmic reverb, and multiband stereo imaging.
- A01-A10: analog EQ/preamp/console, tape/valve saturation, FET/opto/VCA compression, passive EQ, and plate reverb.
- R01: fixed analog-only A01-A10 chain reusing the same DSP cores, with independently recallable module enables.
- D01 exposes a stable 24-band numbered bank; D02, D03, D07, D08, and D10 likewise use fixed numbered collection IDs.
- D02 and A06 expose optional external sidechain buses. D03 provides minimum-phase and 63-tap linear-phase splitting with reported latency.

The verified Release and Debug matrices each contain 21 VST3 bundles, 21 Standalone apps, and 28 automated tests. See `docs/dev/STATUS.md`, `docs/dev/HANDOFF.md`, and `logs/` for evidence and remaining release-engineering limitations.

## Installer

Run `dist/Amanorsac_Studio_Mixing_Suite_0.1.2_Windows.exe`. It installs all VST3s to the standard per-user VST3 folder, installs the Standalone apps, adds Start Menu shortcuts, and registers an uninstaller without requiring administrator access. Restart the DAW and trigger a VST3 rescan after installation.

Rebuild the installer from verified artifacts with:

```powershell
powershell -ExecutionPolicy Bypass -File installer/build-installer.ps1 -Configuration Release
```

The package is self-contained and unsigned; Windows SmartScreen may therefore show an unrecognized-publisher warning.

# Known Issues

## KI-0001 — Third-party host validation not performed

- Severity: P1 before commercial distribution
- Description: VST3 bundles and manifests are structurally audited and every Standalone starts, but the suite has not been scanned and exercised in an independent DAW host.

## KI-0002 — Approved audio-reference calibration unavailable

- Severity: P1 before model/factory-preset freeze
- Description: Analog cores are numerically tested and functional, but no product-owner-approved input/output reference corpus was supplied for perceptual or null-test calibration.

## KI-0003 — Installer is unsigned

- Severity: P1 before distribution
- Description: A self-contained per-user Windows installer exists and passes isolated install/uninstall verification, but it has no Authenticode signature and may trigger Windows SmartScreen. The installer packages the fully verified Release matrix (0.1.2).

## KI-0004 — Platform formats outside current toolchain

- Severity: P2
- Description: AAX requires its SDK/signing credentials; AU requires macOS. Neither is available on this Windows host.

## KI-0005 — Licensing is built but not armed, and has no activation UI

- Severity: P1 before sale
- Description: Signed node-locked licensing exists and passes its verification tests, but enforcement is disabled behind `AMANORSAC_LICENSING_ENABLED=0` and there is no activation window. A customer cannot install a licence without copying a file into %APPDATA% by hand. Arming it and building the activation UI are release blockers. See `docs/dev/LICENSING.md`.
- Note: no client-side scheme is uncrackable; this defeats casual sharing, not a determined attacker.

## KI-0006 — Preset UI is analog-only

- Severity: P2
- Description: PresetManager drives the host program list for every product, but only the analog editors (A01-A10, via AnalogChassis) expose the preset window, save/rename/delete dialogs, A/B and clipboard keys. The digital editors still need the same top-bar wiring. No factory presets exist by design until the parameter contracts are frozen.

## KI-0007 - The rack shows module controls generically, not on each faceplate

- Severity: P3
- Description: ANALOG MIX RACK lays the selected module contract out with the shared hardware widgets rather than reproducing that product own faceplate. Every control is present, real and correctly scaled, but a module in the rack does not look identical to the plugin. Reusing the ten dedicated editors needs them to take a product context rather than PluginProcessor directly.

## KI-0008 - The rack does not build

- Severity: P2, and it does not block the analog plugin installer
- Description: ANALOG MIX RACK was mid-rework when the direction changed. The ten plugins, their editors and the shared chassis all build and pass their tests; only the R01 target and its render harness are broken. Either finish the module faceplate embedding or drop the target from the release configuration before packaging.

## Resolved implementation issues

- The rack now exposes, automates and saves every module parameter, and is proven sample-identical to the plugins (DEC-0013).
- VALVE DRIVE EVEN / ODD and SILK PASSIVE EQ frequency selectors were inaudible in normal use and now work (DEC-0014).


- A02-A10 rebuilt on the shared Heritage chassis with per-backdrop layouts, product accents, real gain-reduction meters on the compressors and the full preset/A-B/undo top bar (DEC-0011).

- A01 HERITAGE EQ: all 35 parameters proven audible by the automated audit; toolbar keys that were previously invisible are drawn and wired (A/B, copy, paste, undo, redo, setup, menu); preset window, prev/next and save/rename/delete work (DEC-0010).

- Release matrix completed without LTO (DEC-0008); installer now ships Release binaries.
- R01 rack test target linked against its asset library so `BinaryData.h` resolves in every configuration.
- The generic scrolling editor was replaced with each product's supplied design surface and a compact, human-labelled, paged live-control inspector.
- PRISM EQ defaults to six active bands and bypasses identity filters, eliminating the reported startup CPU spike/no-audio behavior.
- ORBIT DELAY now has a sample-accurate dry path, bounded normalized feedback, cached tap state, and one shared modulation oscillator; the worst-case stress path runs faster than real time.
- SPACEVERB PRO and PLATE FOUR now use bounded, energy-preserving feedback matrices and pass twelve-second maximum-feedback stability tests.

- D01 fixed 24-band expansion and processor path are implemented.
- D02 advanced detector/sidechain/stereo/listen/delta paths are implemented.
- D03 linear-phase split and latency reporting are implemented.
- D04-D10 and A04-A10 processor-specific paths are implemented.
- R01 analog-only rack target is implemented and tested.

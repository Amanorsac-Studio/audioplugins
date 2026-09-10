# Decisions

## DEC-0001 — Shared contract-derived plugin wrapper

- Status: Accepted for Milestone 1
- Context: Twenty standalone binaries need consistent JUCE wrapper, state, UI, and host behavior while DSP modules remain independently owned.
- Decision: Generate each target from its authoritative JSON contract and compile a shared wrapper against a target-specific embedded contract. Processor-specific DSP remains selected by stable plugin ID.
- Consequence: Every product scans as a distinct binary without duplicating platform code.

## DEC-0002 — Normalize malformed exported parameter IDs

- Status: Accepted, reversible before preset beta
- Context: Machine JSON stores each `id` as `stable_id\nDisplay Label`, despite separately providing a label and requiring stable string IDs. Newlines are unsafe host parameter identifiers.
- Decision: Use the first line as the host-visible ID and the first line of `label` as the display name. Preserve source JSON unchanged.
- Alternative: Use newline-bearing IDs verbatim; rejected because host interoperability and preset serialization would be fragile.

## DEC-0003 — Pin JUCE 8.0.15

- Status: Accepted
- Context: The authoritative target is JUCE 8. JUCE 9 is outside scope.
- Decision: Pin the latest JUCE 8 maintenance release available at implementation time, `8.0.15`, through CMake FetchContent.

## DEC-0004 — Freeze standalone/rack processor-module boundary

- Status: Accepted for M2
- Context: The rack must reuse A01-A10 DSP rather than duplicate it, and UI/platform code must not access DSP internals.
- Decision: `ProcessorModule` owns prepare/reset/process, stable-ID setters, detector-only sidechain input, and latency reporting. Standalone wrappers and rack slots will own the same module implementation classes.
- Realtime rule: `process` is allocation-, lock-, log-, file-, and UI-free; control-thread setters feed atomic or smoothed module storage.

## DEC-0005 — D02/D03 per-band host ID expansion requires resolution

- Status: Accepted by product owner on 2026-08-31
- Context: D02 requires 1-12 bands and D03 requires 2-6 bands, but each machine contract defines only one generic `band.*` record (and one `xover.frequency`) while declaring those records automatable. The authoritative DOCX repeats the same generic IDs and does not define an indexed expansion or collection serialization rule.
- Decision: D02 exposes fixed slots `band.01.*` through `band.12.*`. D03 exposes `band.01.*` through `band.06.*` and `xover.01.frequency` through `xover.05.frequency`. `band_count` controls which leading slots are active; inactive slots remain stable and recallable.
- Migration: No preset beta or external session schema exists yet, so the generic template IDs are replaced before public freeze without a legacy migration.

## DEC-0006 — Expand all repeated collections into fixed numbered banks

- Status: Accepted for M3
- Context: D01, D07, D08, and D10 use the same generic collection notation as D02/D03 while requiring stable automatable host IDs.
- Decision: D01 exposes 24 bands, D07 six zones/five crossovers, D08 eight taps, and D10 five bands/four crossovers using two-digit numbered IDs. Collection size controls activate leading slots without changing host identity.

## DEC-0007 — Rack topology is a fixed analog chain

- Status: Accepted for M3
- Context: The source contract restricts rack membership to A01-A10 but defines no movable-slot or nested host-parameter schema.
- Decision: R01 hosts A01 through A10 in fixed order, reuses the same `AnchorDSP` algorithms and embedded module contracts, and exposes stable per-module enables. This preserves the analog-only invariant without inventing an undocumented slot serialization format.

## DEC-0008 — Release configuration builds without link-time optimization

- Status: Accepted for M3 release engineering on 2026-09-01
- Context: `juce_recommended_lto_flags` made each Release VST3/Standalone link take several minutes on the build host, so the full 21-product Release matrix was never completed and the installer shipped Debug binaries.
- Decision: Remove the LTO flag set from all product, rack, and test targets. Release keeps the standard JUCE optimized configuration flags (`/O2`-class codegen, `NDEBUG`), which is sufficient for the real-time DSP load of these processors.
- Consequence: The complete Release matrix builds in a practical time, is covered by the `windows-release` CTest preset, and is the configuration packaged by the installer. LTO can be reintroduced per target if a profiled hotspot justifies it.

## DEC-0009 — Offline node-locked licensing, built but not armed

- Status: Accepted on 2026-09-05
- Context: The suite is going on sale and needs copy protection. The stated goal was "uncrackable", which is not achievable for software executing on the customer's own CPU under their debugger; every major plugin vendor with a far larger security budget is cracked. The achievable goal is to make casual sharing impossible and cracking not worth the effort.
- Decision: Offline licences signed with a studio RSA private key, verified against a public key embedded in the product, bound to a hashed machine fingerprint. Two activations per purchase, enforced by us at issue time because licences are offline. The entitlement value is consumed by the audio path as a gain ramp rather than gating a boolean, so a careless patch leaves the product audibly wrong.
- Alternatives: iLok/PACE, genuinely stronger and hardware-backed, rejected for now on cost and customer friction; a hosted licence server rejected because studio machines are often offline.
- Consequence: No key generator can be written without the private key, and a copied licence will not verify on another machine. A determined cracker can still patch the binary. The private key becomes a critical asset: losing it makes every issued licence unreplaceable.
- Not armed: enforcement is behind `AMANORSAC_LICENSING_ENABLED`, default 0, so development and listening tests are unaffected. It is set to 1 once at release. See `docs/dev/LICENSING.md`.

## DEC-0010 — A01 HERITAGE EQ: professional-grade signal path, presets and control behaviour

- Status: Accepted on 2026-09-07
- Context: The A01 contract was extended to 35 parameters (per-band Q, high-band slope, per-band enables, EQ IN, FILTER IN, PHASE, filter slope, BYPASS) so every control on the approved faceplate drives a real engine value. The earlier processHeritage path, preset handling and knob behaviour did not meet the bar set by the reference products the studio measures against.
- Decision (DSP): fixed chain order input trim, HPF/LPF, five bands, character, auto gain, mix, output trim. LOW and HIGH are true RBJ shelves with slope control; mids are peaking bells. HPF/LPF offer 18 dB (biquad plus first-order) and 24 dB (two Butterworth biquads) slopes; HPF at its minimum legend reads OFF. Gains, mix and bypass are 20 ms smoothed. 2x/4x oversampling uses real polyphase IIR half-band stages with the latency reported to the host and a matching dry-path delay so MIX 0 % is sample-aligned. BYPASS is exposed through getBypassParameter so hosts drive their own switch.
- Decision (presets): one shared PresetManager for every product. Presets are XML files in the user data folder (schema, plugin ID, parameter map, tags, author). Only the built-in Default ships; factory presets wait for the parameter freeze as the specification requires. Loading writes through setValueNotifyingHost as a single undo step; A/B snapshots, clipboard copy/paste and host program lists all go through the same manager.
- Decision (controls): every knob shows its value while dragging, double-click opens typed entry, Ctrl-click restores the contract default, Shift-wheel is fine adjustment, and each mouse-down on the panel opens a new undo transaction so UNDO steps back one gesture.
- Verification: tests/dsp/HeritageEqTests.cpp (Amanorsac.A01.HeritageEq) proves every parameter is audible, shelves and bells behave as such, bypass/EQ IN/FILTER IN are identity, latency is 0/3/4 samples with an aligned dry path, and there are no NaNs across 44.1/96/192 kHz.

## DEC-0011 — One hardware chassis for every analog product

- Status: Accepted on 2026-09-07
- Context: A01 HERITAGE EQ set the approved standard: printed knobs with legends, machined keys, lamps, VU and LED meters, a top bar with preset window and tool keys, typed value entry, Ctrl-click reset, per-gesture undo and the preset/A-B/clipboard menus. A02-A10 were still drawn by an older parameter-driven chassis with different widgets and no preset UI.
- Decision: the Heritage widgets and the top-bar/preset behaviour were lifted into src/common/ui/AnalogChassis.h (namespace amanorsac::hw) as an AnalogChassis base. HeritageEditor and AnalogPageEditor both derive from it. AnalogPageEditor carries one explicit layout per product (buildIronPre ... buildPlateFour) with coordinates measured from each supplied clean backdrop, so every control lands in its machined bay. Only contract parameters are placed; nothing decorative pretends to be a control.
- Scales: knob legends are generated from the parameter contract through the parameter object itself (convertFrom0to1 / convertTo0to1), so skewed frequency scales print true positions, with interior marks rounded to the 1 / 1.5 / 2 / 3 / 5 / 7 / 10 series.
- Meters: the three compressors (A06, A07, A08) show real gain reduction. AnchorDSP publishes the deepest reduction per block and PluginProcessor::getGainReductionDb exposes it; the VU face switches to a 0-20 dB scale.
- Consequence: presets, A/B, copy/paste, undo/redo, setup and menu keys behave identically across the analog line. The older AnalogUI KnobCell/SegmentGroup/TopBar widgets are no longer used by any analog editor and can be retired once the digital editors stop depending on AnalogUI primitives.

## DEC-0012 — Analog line: shared front/back end, faceplate features made real, panel typeface

- Status: Accepted on 2026-09-07
- Context: The approved HF faceplates for A02-A10 show input trims, phase, HPF, bypass, link and era/tension controls that several contracts lacked, and the embedded condensed typeface was judged illegible.
- Decision (contracts): A02 gains hpf_in, mix, stereo_mode (L/R, M/S), bypass; A03 phase, hpf_in, eq_in, bypass; A04 phase, hpf, bypass; A05 input, phase, hpf, bypass; A06 phase, hpf (switch, 80 Hz), bypass; A07 input, phase, bypass; A08 input, output, phase, bypass; A09 input, phase, hpf, mix, bypass; A10 input, phase, hpf, era, tension, bypass. Every one drives the engine; no decorative controls remain.
- Decision (engine): HERITAGE EQ and PLATE FOUR own these stages in their signal paths. For the other eight, PluginProcessor provides one shared front end (input trim, phase, 2nd-order HPF with OFF at the bottom of its travel, M/S encode) and back end (M/S decode, mix against the trimmed input, output trim, 20 ms bypass crossfade) around the product engine, so behaviour is identical across the line and cannot drift per product. IRON PRE and CONSOLE ONE gate their existing HPF and EQ stages from hpf_in / eq_in inside the engine.
- Decision (type): system Inter / Segoe UI / Roboto (first installed) set slightly tracked with the true bold cut for captions and legends; engraved product names in a wide tracked serif. No embedded font ships; nothing is horizontally scaled.
- Consequence: 20 previously missing faceplate features are live across the line; contracts grew by 3-6 parameters per product, before any preset or session schema was frozen (DEC-0005 migration rule still applies).

## DEC-0013 - The rack is one parameter tree, and the console strip is one implementation

- Status: Accepted on 2026-09-07
- Context: R01 hosted ten AnchorDSP instances whose parameters lived in throwaway per-module processors. Nothing was exposed to the host, nothing was automatable, and none of it reached the session: reopening a saved project gave back an empty rack. The console strip (input trim, phase, HPF, M/S, mix, output trim, bypass) also existed only inside PluginProcessor, so those controls did nothing at all inside the rack.
- Decision: the rack keeps a single AudioProcessorValueTreeState holding its own contract plus every module contract under an "A01." to "A10." prefix, built by PluginSpec::appendParameters. AnchorDSP takes a parameter prefix so one engine can read its slice of a shared tree. The console strip moved out of PluginProcessor into AnalogFrontEnd, which the plugin and each rack slot both use. AnalogChassis became a template over the product, so the rack gets the same top bar, presets, undo, typed entry and widgets as the plugins.
- Consequence: 169 module parameters are real, automatable, session-saved host parameters; the rack has presets, A/B and clipboard through the shared PresetManager; each slot crossfades over 20 ms with its dry reference delayed by the engine reported latency, so enabling a module cannot click or smear; the rack reports the sum of its enabled slot latencies. The audit proves every module in the rack is sample-identical to the module as a plugin.

## DEC-0014 - Two engine defects found by the analog audit, and how they were fixed

- Status: Accepted on 2026-09-07
- Context: tests/dsp/AnalogEngineAudit.cpp drives all ten analog engines and the rack and asks, for every parameter in every contract, whether it changes the audio. Two controls did not.
- VALVE DRIVE, EVEN / ODD: the balance term sat inside the pentode branch only, so the control did nothing in the default triode setting and nothing in cascade. Even-order harmonics come from asymmetry, so EVEN / ODD now sets how far off centre the stage is driven, and applies to every topology. The default tone moves slightly; the control works everywhere, which it did not before.
- SILK PASSIVE EQ, LOW and HIGH FREQUENCY: boost and cut were summed into one shelf, so equal boost and cut cancelled exactly and the frequency selectors became inaudible. The passive hardware has separate boost and cut networks sharing a frequency selector, the cut sitting above the low boost and below the high boost. They are now separate curves, which restores the classic simultaneous boost-and-cut shape and makes the frequency controls always meaningful.
- Also corrected: HERITAGE EQ applied its auto gain to the dry signal as well as the wet, so MIX 0 % was not the untouched input. Auto gain now rides the wet path only, and the Heritage smoothers start at their first target instead of ramping up from silence on the first block.

## DEC-0015 - The rack is a host, not a fixed chain

- Status: Accepted on 2026-09-08, supersedes the fixed-order chain in DEC-0013
- Context: the first rack ran the ten modules in a fixed A01 to A10 order with nothing but per-module enables. That is a bundle wrapper, not a rack. The brief is a host the plugins live in, in the shape of T-RackS: reorder the chain, run modules in parallel, solo one to hear it alone, with the plugins still standing on their own in the DAW.
- Decision: every module is a slot carrying four routing controls of its own, on top of its whole contract: enabled, solo, order (1 to 10) and lane (A or B). The order and lane controls are recallable but not automatable, because they are structure rather than performance. Two lanes run in parallel and are summed with their own level and polarity; the rack only splits once lane B actually holds a module, so pressing SPLIT with nothing routed across cannot jump the level by 6 dB. Solo overrides the enables the way a console does. The editor draws the chain as draggable cards on one rail, or two when split, and the selected module's whole contract underneath.
- Latency: each lane accumulates its modules' reported latency and the shorter lane is delayed to match the longer one, so a module that reports latency cannot comb the parallel path. The rack reports the longer lane to the host.
- Consequence: 217 rack parameters plus 169 module parameters, all saved with the session. Reordering, lane assignment, solo and the split are proven by tests/dsp/AnalogEngineAudit.cpp, which also proves both lanes deliver a transient at the same sample.
- Not done: a slot cannot hold two instances of the same module, because a VST3 parameter list is fixed at construction and one slot per product is what keeps every module control a real host parameter.

## DEC-0016 - Factory presets ship inside the product, authored not copied

- Status: Accepted on 2026-09-09, lifts the deferral in the preset specification
- Context: the contracts are now frozen for the analog line and the bundle is going on sale, so the reason for shipping only a Default no longer holds. A register of Waves analog presets was supplied as a starting point and inspiration.
- Decision on content: the register is used for coverage only, that is, which sources and jobs a bank has to serve. Every value is authored against our own contracts, because our engines share no parameters with the reference plug-ins and copying stored numbers would be meaningless as well as wrong. Names are ours. Each preset carries Source, Intent, Genre, Intensity and Character tags.
- Decision on delivery: banks are authored as JSON under presets/factory, embedded in each product at build time, and listed after Default. They are read-only; saving over one writes a copy to the user folder. A bank may name an enum by its label and a switch by true or false, so the files stay readable by a person.
- Coverage: 141 presets, 14 to 15 per product, spanning vocal, drums, bass, guitar, keys, bus and master, plus creative extremes.
- Verification: tests/dsp/AnalogEngineAudit.cpp checks that every bank exists, that names are unique, that every id names a real parameter in that contract, and that every preset loads and measurably changes the sound against the product default.

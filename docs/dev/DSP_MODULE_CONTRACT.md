# DSP Module Contract

Status: Frozen for M2 platform integration under DEC-0004.

`ProcessorModule` is the sole DSP boundary used by a standalone plugin wrapper and, later, an Analog Mix Rack slot. A module exposes:

- immutable stable module ID;
- `prepare`, `reset`, and realtime-safe `process` lifecycle;
- stable-ID parameter setters called from the control thread;
- optional detector-only sidechain input;
- explicit latency reporting.

The audio callback must not allocate, lock, log, access files, or touch UI state. Parameter setters update atomic or smoothed storage owned by the module. Rack code will own the same module class used by the corresponding standalone analog plugin; it must not duplicate processor algorithms.

The interface does not define rack automation address expansion. Slot UUID and host-visible slot-index mapping remain rack-owned state concerns.


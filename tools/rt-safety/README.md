# rt-safety

Phase 0 harness that enforces the README's first design principle —
*real-time safety is enforced, not assumed* — by failing the build if anything
reachable from the audio callback could allocate, lock, or block.

## How it works

The real-time core is compiled as an OBJECT library (`talos_rt`), so every
external call the callback could make shows up as an *undefined symbol* in
those object files. `rt_symbol_check.py` runs `nm -u` on each object and
asserts that every undefined symbol is either:

- on an explicit whitelist (`memcpy`, `memset`, and the one-shot mach/pthread
  scheduling calls used by `RtThreadBoost`), or
- not matching a forbidden pattern (`malloc`/`new`/`free`, any `*mutex*` /
  `*lock*`, blocking syscalls, `__cxa_*`/`_Unwind_*`).

Anything the callback needs must live in `talos_rt`; the PortAudio glue
(`AudioEngine`) lives outside it and may allocate freely.

## Running

```bash
cmake -B build -DTALOSDSP_BUILD_RTSAFETY=ON
cmake --build build --target rt_safety_check
```

Or run everything the CI does:

```bash
cmake --build build --target check
```

## Notes

- Apple's `nm` and GNU `nm` both work; the leading `_` mach-o adds to C
  symbols is normalized away before matching.
- A full custom clang-tidy check (the roadmap's stretch goal) would catch
  allocations hidden behind inlined library code that produce no undefined
  symbol; the whitelist is the documented escape hatch until then.
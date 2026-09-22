# RT symbol audit

`talos_rt` contains the PortAudio callback, DSP and feature extraction. Thread lifecycle and controller loops live in `talos_control`, outside the callback. RT objects disable exception handling; stages are owned by value and cannot be deleted through `IDspStage`.

The checker parses both bare Apple `nm -u` output and GNU output, preserving C++ symbol names. References defined by another audited object are resolved internally; external references must appear on an explicit whitelist. Allocation, blocking and exception symbols are rejected. Compiler math routines and immutable RTTI metadata have explicit allowances.

This is a conservative object-symbol audit, not a proof of arbitrary indirect-call reachability or backend behavior. Platform scheduling calls are permitted for the first callback attempt. Sanitizer instrumentation is checked in a separate build with the symbol audit disabled.

```sh
cmake -B build
cmake --build build --target check
```

`check` builds/runs CTest (including parser regressions) and audits RT objects. `rt_safety_check` runs only the object audit. Parser fixtures verify that Apple/GNU allocation symbols are rejected rather than silently ignored.

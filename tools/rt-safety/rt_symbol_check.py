#!/usr/bin/env python3
"""Phase 0 rt-safety harness: verify no allocation or locking is reachable
from the audio callback.

Approach: the real-time core (talos_rt) is compiled into an OBJECT library so
that anything the callback can call must appear as a symbol in those object
files. We run `nm -u` on each object and assert that every *undefined* symbol
(an external reference) is either:

  * on the whitelist (libc builtins, mach/pthread scheduling calls used once
    by RtThreadBoost), or
  * not matching a forbidden pattern (allocation, deallocation, locking,
    blocking syscalls).

If the callback ever calls malloc/new/free/mutex/..., a forbidden symbol shows
up here and the build fails. A full clang-tidy plugin is the stretch goal; this
harness covers the Phase 0 requirement with zero LLVM toolchain coupling.

Usage:
  rt_symbol_check.py <object-file>... [--src-dir <dir>]
"""

import re
import subprocess
import sys

FORBIDDEN_PATTERNS = [
    re.compile(r"\b(malloc|calloc|realloc|free)\b"),
    re.compile(r"\bmmap|munmap|brk|sbrk\b"),
    re.compile(r"operator new|operator delete"),
    re.compile(r"_Znwm|_Znam|_ZdlPv|_ZdaPv"),  # mangled new/delete
    re.compile(r"mutex|pthread_mutex|lock_guard|unique_lock|shared_lock"),
    re.compile(r"sem_|semaphore"),
    re.compile(r"\b(read|write|open|close|fcntl|select|poll|epoll)\b"),
    re.compile(r"nanosleep|usleep|sleep"),
    re.compile(r"__cxa_|_Unwind_"),
]

# External symbols the RT core is allowed to reference. Everything here is a
# plain C call (memcpy/memset) or a one-shot scheduling syscall in
# RtThreadBoost; none of it allocates or blocks on a lock.
WHITELIST = {
    "_ZNSt3__16chrono12steady_clock3nowEv",
    "_ZNSt6chrono3_V212steady_clock3nowEv",
    "tanf",
    "pthread_setaffinity_np",
    "_ZTVN10__cxxabiv117__class_type_infoE",  # Immutable RTTI metadata.
    "_ZTVN10__cxxabiv120__si_class_type_infoE",
    "bzero",  # Compiler-generated zero fill on macOS.
    "__sincosf_stret",  # Apple fused sin/cos math routine.
    "sincosf",
    "memcpy",
    "memmove",
    "memset",
    "memcmp",
    "__stack_chk_fail",
    "_stack_chk_fail",
    "stack_chk_fail",
    "__memcpy_chk",
    "_memcpy_chk",
    "memcpy_chk",
    "__memset_chk",
    "_memset_chk",
    "memset_chk",
    # C math library (RT-safe: no allocation, no locking). Needed by the
    # limiter (tanh) and any future DSP; plain C calls after underscore
    # normalization.
    "tanh",
    "tanhf",
    "sin",
    "sinf",
    "cos",
    "cosf",
    "exp",
    "expf",
    "sqrt",
    "sqrtf",
    "fabs",
    "fabsf",
    "pow",
    "powf",
    "log",
    "logf",
    "floor",
    "ceil",
    # Clock reads for per-stage cost timing (steady_clock::now()).
    "clock_gettime",
    "mach_absolute_time",
    "mach_timebase_info",
    "mach_task_self_",
    "mach_thread_self",
    "thread_policy_set",
    "pthread_set_qos_class_self_np",
    "pthread_self",
    "pthread_setschedparam",
    "pthread_getschedparam",
    "_tlv_atexit",
    "__cxa_thread_atexit_impl",
}


def parse_undefined_symbols(object_file: str) -> list[str]:
    """Return undefined symbols referenced by a relocatable object."""
    result = subprocess.run(["nm", "-u", object_file], capture_output=True,
                            text=True, check=False)
    if result.returncode != 0:
        print(f"error: nm failed on {object_file}: {result.stderr.strip()}",
              file=sys.stderr)
        sys.exit(2)
    return parse_nm_output(result.stdout)


def normalize(symbol: str) -> str:
    # Strip exactly the Mach-O decoration, preserving C++/runtime spelling.
    if sys.platform == "darwin" and symbol.startswith("_"):
        return symbol[1:]
    return symbol


def parse_nm_output(output: str) -> list[str]:
    symbols = []
    for line in output.splitlines():
        tokens = line.split()
        if not tokens or line.endswith(":"):
            continue
        if len(tokens) == 1:
            symbols.append(normalize(tokens[0]))
        elif "U" in tokens or "u" in tokens:
            symbols.append(normalize(tokens[-1]))
    return symbols


def defined_symbols(objects: list[str]) -> set[str]:
    result = subprocess.run(["nm", "-g", *objects], capture_output=True, text=True, check=True)
    return {normalize(parts[-1]) for line in result.stdout.splitlines()
            if len(parts := line.split()) >= 3 and parts[-2] not in ("U", "u")}


def check_symbol(symbol: str, object_file: str) -> list[str]:
    errors = []
    for pattern in FORBIDDEN_PATTERNS:
        if pattern.search(symbol):
            errors.append(
                f"  {object_file}: undefined symbol '{symbol}' matches "
                f"forbidden pattern /{pattern.pattern}/ (allocation, locking, "
                f"or blocking call reachable from the audio callback)")
            return errors
    if symbol not in WHITELIST:
        errors.append(
            f"  {object_file}: undefined symbol '{symbol}' is not on the "
            f"rt-safety whitelist; add it to tools/rt-safety/rt_symbol_check.py "
            f"only if it is genuinely RT-safe (no allocation, no locks)")
    return errors


def main() -> int:
    args = sys.argv[1:]
    object_files = []
    for a in args:
        if a.startswith("--"):
            continue
        # CMake may hand us a single semicolon-joined list.
        object_files.extend(a.split(";"))
    object_files = [o for o in object_files if o]
    if not object_files:
        print("usage: rt_symbol_check.py <object-file>... [--src-dir <dir>]",
              file=sys.stderr)
        return 2

    internal = defined_symbols(object_files)
    errors = []
    for obj in object_files:
        for symbol in parse_undefined_symbols(obj):
            if symbol not in internal:
                errors.extend(check_symbol(symbol, obj))

    if errors:
        print("rt-safety FAIL: the following symbols are reachable from the "
              "audio callback:\n" + "\n".join(errors))
        return 1

    total = sum(len(parse_undefined_symbols(o)) for o in object_files)
    print(f"rt-safety OK: {len(object_files)} object(s) checked, "
          f"{total} undefined symbol(s) all safe.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
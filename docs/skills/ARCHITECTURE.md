# System Architecture & Design Principles

This document defines the architectural principles, subsystem boundaries, and clean-room requirements for the `wine-parity` repository.

---

## 1. Core Architectural Pillars

### Pillar 1: Clean-Room Reverse Engineering & Legal Hygiene

- **Strict Invariant:** Zero exposure to leaked Microsoft source code (WRK, Windows 2000/XP leaked archives) or decompiled proprietary binaries (via IDA Pro, Ghidra, etc.).
- **Specification Source:** Implementations must derive strictly from:
  1. Public Microsoft Learn / MSDN documentation.
  2. Public Wine header definitions and function signatures.
  3. Empirical differential behavior observed on genuine Windows VMs.

### Pillar 2: Generic Subsystem Fidelity (No Title-Specific Hacks)

- Subsystem implementations must solve generic operating system and library behavior.
- **Strict Invariant:** Never introduce application-specific branches, executable name checks (e.g. `if (strstr(cmdline, "app.exe"))`), or checksum detection to bypass bugs. If an application fails, identify and fix the underlying missing API behavior or error code.

### Pillar 3: Strict Separation of Harness and Target

- **Harness & Centralized Script Layer (`scripts/build.sh`, `scripts/launch.sh`, `scripts/stop.sh`, `scripts/test.sh`, and `scripts/lib/`):** Orchestrates virtual machines (QEMU `hvf`/`tcg` on macOS, `kvm` on Linux), transmits test binaries via VirtIO serial `qemu-ga`, manages QMP `loadvm` snapshots, caches Ground-Truth JSON traces, compiles single target tests/DLLs surgically, and computes field-level differential diffs.
- **Target Layer (`dlls/`, `patches/`, `wine/`):** The actual Wine compatibility code being developed, tested, and patched (prioritizing macOS native frameworks under `#ifdef __APPLE__` first, and Linux `#ifdef __linux__` second).
- The target layer must never contain dependencies on the test harness.

### Pillar 4: Centralized Parametrized Scripts & Surgical Single-Target Execution

- All developer and AI agent workflows operate exclusively through four canonical shell entrypoints in `scripts/`:
  1. `scripts/build.sh` (`--dll`, `--test`, `--arch x86_64|arm64`, `--export-patch`, `--all`)
  2. `scripts/launch.sh` (`--arch arm64|x86_64`, `--snapshot clean_ready`, `--status`, `--verify`)
  3. `scripts/stop.sh` (`--vm`, `--wine`, `--all`)
  4. `scripts/test.sh` (`--dll`, `--test`, `--filter`, `--live-vm`, `--discover-stubs`, `--fetch-spec`, `--lint`, `--ci`)
- Rebuilding the entire project or running unrelated test suites for a single API or test change is strictly prohibited.

---

## 2. Wine Subsystem Boundaries: Client DLL vs. Wineserver (macOS First, Linux Second)

Understanding where code belongs is critical to avoid architectural corruption:

```text
  ┌────────────────────────────────────────────────────────┐
  │                   Windows Application                  │
  └───────────────────────────┬────────────────────────────┘
                              │ Win32 API calls
                              ▼
  ┌────────────────────────────────────────────────────────┐
  │                      Client DLLs                       │
  │     (kernelbase.dll, ntdll.dll, bcrypt.dll, etc.)      │
  │                                                        │
  │  • Fast, stateless computations                        │
  │  • Parameter validation & conversion                   │
  │  • Direct mapping to macOS frameworks / POSIX syscalls │
  └───────────────┬────────────────────────┬───────────────┘
                  │                        │
   macOS Frameworks / POSIX                │ Unix Domain Socket IPC
   (CommonCrypto, Security,                │ (server/protocol.def)
    VideoToolbox, Mach/Darwin)             ▼
                  │         ┌──────────────────────────────┐
                  │         │          wineserver          │
                  │         │                              │
                  │         │  • Global handle table       │
                  │         │  • Mutexes / Semaphores / APC│
                  │         │  • Security tokens / SACL    │
                  │         │  • Process / Thread lifecycle│
                  ▼         └──────────────────────────────┘
  ┌────────────────────────────────────────────────────────┐
  │          macOS XNU / Darwin Kernel (Primary)           │
  │              Linux Kernel (Secondary)                  │
  └────────────────────────────────────────────────────────┘
```

1. **Client DLL (`dlls/*`):**
   - Stateless or thread-local operations (string handling, encryption routines, path conversions, media buffers, math).
   - On macOS (`__APPLE__`), prioritizes zero-dependency native system frameworks (`CommonCrypto`, `Security.framework`, `CoreFoundation`, `VideoToolbox`, `CoreMedia`, `libSystem`/Mach) paired with self-contained C99/C11 logic for exact Windows CNG/Win32 edge-case semantics.
   - Translates Win32 calling conventions and structs into internal representations.
2. **Wineserver (`server/*`):**
   - Centralized Unix daemon providing NT kernel semantics.
   - Any API that creates, duplicates, or inspects shared OS handles (file locks, events, semaphores, process tokens) MUST communicate with `wineserver` via protocol definitions.

---

## 3. Conformance Test & Clean-Room Spec Architecture

- **Dual-Mode Conformance Tests (`tests/conformance/<dll_name>/`):**
  - Include `tests/include/parity_test.h` and compile cleanly as standalone PE executables (`x86_64-w64-mingw32-gcc` and `aarch64-w64-mingw32-gcc`) as well as under Wine's `winetest` runner.
  - Combine standard `winetest` assertions (`START_TEST`, `ok()`, `todo_wine`) with structured differential probes (`PARITY_RECORD(...)`) emitting JSON records validated against `tests/schema/result.json` (supporting `exact`, `handle_state`, `masked`, and `roundtrip` comparison modes).
- **Clean-Room Spec Packets (`specs/<dll_name>/<api_name>.json`):**
  - Record official Microsoft Learn URLs/specifications, public Wine header signatures, and empirical Windows VM findings with full clean-room provenance auditing.

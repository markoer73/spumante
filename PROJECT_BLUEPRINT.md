# Wine Parity Project Blueprint

Detailed technical architecture and implementation roadmap for an automated AI coding pipeline targeting Wine feature parity.

---

## 1. Subsystem Roadmap & Phased Execution

Attempting to cover the entire Win32/NT surface simultaneously is impractical. Development proceeds in distinct phases:

### Phase 1: Ground-Truth Differential Harness (Infrastructure — macOS First, Linux Second)
* **Objective:** Build the automated test execution loop between a genuine Windows environment and Wine, prioritizing Apple Silicon / Intel macOS natively before Linux.
* **Canonical Script Interface (`scripts/`):**
  * All operations are centralized into 4 parametrized shell entrypoints (`scripts/build.sh`, `scripts/launch.sh`, `scripts/stop.sh`, `scripts/test.sh`) backed by modular libraries in `scripts/lib/` with surgical single-target execution by default.
* **Components:**
  * Headless Windows 10/11 VM running under QEMU (`-accel hvf` on Apple Silicon macOS for ARM64 Windows 11 + x64 emulation, `-accel tcg`/`hvf` for x86_64, or KVM on Linux).
  * Host-local VirtIO serial QEMU Guest Agent (`qemu-ga` at `/tmp/wine_parity_qga.sock`) for zero-network file transfer and execution, paired with QMP socket (`/tmp/wine_parity_qmp.sock`) `loadvm clean_ready` snapshots and Ground-Truth JSON caching (`tests/ground_truth/<dll>/`).
  * Isolated Wine execution environment (`WINEPREFIX=/tmp/wine_test_$$`, drive `Z:` disabled) supporting `<1s` single-DLL overrides (`WINEDLLOVERRIDES` + `WINEDLLPATH`) and dual `x86_64` / `arm64` MinGW-w64 cross-compilation.
  * JSON-based differential comparator (`tests/schema/result.json`) supporting `exact`, `handle_state`, `masked`, and `roundtrip` field comparison modes.

### Phase 2: Target Selection & Prioritization
* **Subsystem Focus:** Begin with self-contained, user-space dynamic link libraries (prioritizing native macOS frameworks under `#ifdef __APPLE__` such as `CommonCrypto`/`Security.framework`, `CoreFoundation`, `VideoToolbox`/`CoreMedia`, and Darwin/Mach APIs, with `#ifdef __linux__` fallbacks):
  1. `bcrypt.dll` and `ncrypt.dll` (Cryptography Next Generation APIs).
  2. `kernelbase.dll` / `ntdll.dll` missing utility APIs.
  3. `mfplat.dll` (Media Foundation platform APIs).
* **3-Layer Discovery Scanner (`./scripts/test.sh --discover-stubs`):**
  * Parses `.spec` `@ stub` entries, C AST/regex full and partial branch `FIXME`s, and optional `WINEDEBUG=+fixme,+relay` runtime logs.
  * Scores stubs by `wineserver` independence and macOS native framework readiness.

### Phase 3: The Agentic Coding Loop
* **Surgical CLI Capabilities (`scripts/` + `scripts/lib/`):**
  * `./scripts/test.sh --fetch-spec <dll>:<api_name>`: Fetches official Microsoft Learn specs, extracts public Wine header declarations, and persists an auditable Clean-Room Spec Packet (`specs/<dll>/<api_name>.json`).
  * `./scripts/build.sh --dll <dll> --test <test_name> [--arch x86_64|arm64]`: Surgically compiles only the single conformance test binary (using `tests/include/parity_test.h`) and/or single Wine override DLL without rebuilding unrelated targets.
  * `./scripts/test.sh --dll <dll> --test <test_name> [--filter <case>] [--live-vm]`: Executes the test against cached or live Windows VM Ground Truth and Wine, outputting structured field-level discrepancies.
  * `./scripts/build.sh --dll <dll> --export-patch`: Exports a conformant `git format-patch` patch with clean-room provenance metadata.

### Phase 4: Patch Formatting & Regression Guard
* **Conformance (`./scripts/test.sh --lint`):** Enforces WineHQ and repository coding conventions:
  * 4-space indentation, no tabs, pure C99/C11, classic `#ifndef` include guards (no `#pragma once`).
  * Wine debug logging (`TRACE`, `WARN`, `FIXME`, `ERR`; zero raw `printf`/`fprintf` in `dlls/`).
  * Strict Markdown CommonMark/GFM compliance with escaped square brackets (`\[...\]`).
* **Surgical Regression Testing & CI:** Targeted per-DLL regression runs and GitHub Actions CI running on `macos-latest` (primary) and `ubuntu-latest` (secondary).

---

## 2. Clean-Room Legal Safeguards

To ensure that contributions are clean and avoid copyright concerns:
1. **No Proprietary Decompilations:** The agent must never be fed disassemblies of proprietary Microsoft DLLs or decompiled code from tools like IDA Pro or Ghidra.
2. **No Leaked Code in Context:** Prompts must explicitly disallow reference to leaked Windows source archives (e.g. WRK, Windows 2000/XP leaked code).
3. **Pure Specification-Driven Development:** The agent's knowledge input must consist solely of:
   * Public MSDN/Microsoft Learn documentation (`specs/<dll>/<api>.json`).
   * Public Wine header definitions (`include/wine/*`).
   * Empirical test output differences between Windows and Wine.

---

## 3. Recommended Technology Stack

| Role | Tool / Technology |
| :--- | :--- |
| **Host System (Primary)** | macOS (Apple Silicon / Intel) with QEMU (`hvf` / `tcg`), Linux (`kvm`) secondary |
| **Ground Truth VM** | Windows 10/11 Enterprise Evaluation (QEMU + VirtIO Serial `qemu-ga` + QMP Snapshots) |
| **Wine Environment** | macOS Wine (`wine64` / upstream `wine` checkout) + `WINEDLLOVERRIDES` fast path |
| **Compilers** | `clang`/`gcc`, `x86_64-w64-mingw32-gcc`, `aarch64-w64-mingw32-gcc` |
| **Canonical Scripts** | `scripts/build.sh`, `scripts/launch.sh`, `scripts/stop.sh`, `scripts/test.sh` + `scripts/lib/` |
| **Diff & Logging** | JSON Schema (`tests/schema/result.json`) + Dual-mode C SDK (`tests/include/parity_test.h`) |

# Spumante Project Blueprint

Detailed technical architecture and implementation roadmap for an automated AI coding pipeline targeting Wine feature parity, aligned with upstream WineHQ (`wine-11.x`) while prioritizing **macOS first** (Apple Silicon & Intel) and **Linux second**.

---

## 1. Subsystem Roadmap & Phased Execution

Attempting to cover the entire Win32/NT surface simultaneously is impractical. Development proceeds in distinct phases:

### Phase 1: Ground-Truth Differential Harness (Infrastructure — macOS First, Linux Second)

* **Objective:** Build the automated test execution loop between a genuine Windows environment and Wine, prioritizing Apple Silicon / Intel macOS natively before Linux.
* **Canonical Script Interface (`scripts/`):**
  * All operations are centralized into 4 parametrized shell entrypoints (`scripts/build.sh`, `scripts/launch.sh`, `scripts/stop.sh`, `scripts/test.sh`) backed by modular libraries in `scripts/lib/` with surgical single-target execution by default.
* **Components:**
  * Headless Windows 10/11 VM running under QEMU (`-accel hvf` on Apple Silicon macOS for ARM64 Windows 11 + x64 emulation, `-accel tcg`/`hvf` for x86_64, or KVM on Linux).
  * Host-local VirtIO serial QEMU Guest Agent (`qemu-ga` at `/tmp/spumante_qga.sock`) for zero-network file transfer and execution, paired with QMP socket (`/tmp/spumante_qmp.sock`) `loadvm clean_ready` snapshots and Ground-Truth JSON caching (`tests/ground_truth/<dll>/`).
  * Isolated Wine execution environment (`WINEPREFIX=/tmp/wine_test_$$`, drive `Z:` disabled) supporting `<1s` single-DLL overrides (`WINEDLLOVERRIDES` + `WINEDLLPATH`) and dual `x86_64` / `arm64` MinGW-w64 cross-compilation.
  * JSON-based differential comparator (`tests/schema/result.json`) supporting `exact`, `handle_state`, `masked`, and `roundtrip` field comparison modes.

### Phase 2: Target Selection & Upstream WineHQ Missing-Feature Alignment

* **Subsystem Focus:** Borrow and backport from read-only upstream Wine (`.wine-upstream/` at `wine-11.19+` via `wine-borrow`) while prioritizing native macOS frameworks under `#ifdef __APPLE__` (`CommonCrypto`/`Security.framework`, `CoreFoundation`, `VideoToolbox`/`CoreMedia`, `AVFoundation`, `Metal`, and Darwin/Mach APIs) with `#ifdef __linux__` fallbacks:
  1. **`bcrypt.dll`, `ncrypt.dll`, and `crypt32.dll` (Cryptography Next Generation):**
     * **Upstream Gap (WineConf 2025 / `.wine-upstream/dlls/bcrypt`):** 23 `.spec` stubs, 90 `FIXME`s, 8 `todo_wine` tests. Upstream relies on GnuTLS across the Unix call boundary (incurring syscall overhead on symmetric ciphers, lacking `BCRYPT_PAD_NONE` asymmetric padding, Brainpool/custom ECC curve parameters, and MD5 OAEP padding).
     * **`spumante` Target:** Native macOS `CommonCrypto` & `Security.framework` acceleration for AES-GCM/CBC, SHA/HMAC, PBKDF2 (`BCryptDeriveKeyPBKDF2`), RNG (`BCryptGenRandom`), ECDH/ECDSA/RSA (`BCRYPT_PAD_NONE`, OAEP), and provider/context APIs (`BCryptEnumProviders`, `BCryptResolveProviders`, `GetCipherInterface`, `GetHashInterface`, `GetRngInterface`).
  2. **`ntdll.dll` and `kernelbase.dll` (Kernel, Sync & Apple Silicon x86/x86_64 Execution):**
     * **Upstream Gap (`.wine-upstream/dlls/ntdll` & `kernelbase`):** `ntdll` has 101 `.spec` stubs, 418 `FIXME`s, 273 `todo_wine`s; `kernelbase` has 68 `.spec` stubs and 206 `FIXME`s.
     * **`spumante` Target:**
       * **macOS Fast Sync (`NTSync` Parity):** Map NT synchronization primitives (`NtWaitForAlertByThreadId`, futexes/events) to Darwin `ulock` (`__ulock_wait2`/`__ulock_wake`) and Mach semaphores.
       * **Intel (`x86_64` & `i386`) Apps on Apple Silicon (`arm64`):** Support both (a) `x86_64` + New WoW64 (`--enable-archs=i386,x86_64`) under **Rosetta 2** (with write-fault, LDT, and debug-register workarounds in `signal_x86_64.c`/`virtual.c`) and (b) **Native `arm64-apple-darwin` + ARM64EC (`fexcore.dll`/`box64cpu.dll`)** by resolving the 3 macOS ARM64 blockers: Darwin `x18` platform-register save/restore in `signal_arm64.c`, 16 KB host page vs. 4 KB Windows PE page emulation in `virtual.c`, and `MAP_JIT`/`pthread_jit_write_protect_np` W^X toggling.
  3. **`winemac.drv` (macOS Windowing, Display & Metal Integration):**
     * **Upstream Gap (`.wine-upstream/dlls/winemac.drv` & OSPP Roadmap):** 11 `FIXME`s in `winemac.drv`, including unimplemented **cross-process child window Metal swapchains** (`window.c:1177`), **multi-monitor `ChangeDisplaySettingsEx` on non-primary adapters** (`display.c:767-772`), and **cross-process window DCs / layered window attributes** (`opengl.c:1139`, `window.c:1318`).
     * **`spumante` Target:** Implement `IOSurface`-backed cross-process child window `CAMetalLayer` swapchains and multi-monitor `CGDisplayMode` switching in `winemac.drv`.
  4. **`mfplat.dll` and Multimedia (`VideoToolbox` & `AVFoundation`):**
     * **Upstream Gap (`.wine-upstream/dlls/mfplat` & GSoC Roadmap):** 64 `.spec` stubs, 29 `FIXME`s, 86 `todo_wine` tests in `mfplat`; video capture only supports Linux `v4l2` (missing macOS `AVFoundation` video capture and `CLSID_AudioRecord`).
     * **`spumante` Target:** Implement missing `mfplat` buffer/attribute/guid/format APIs (`MFCreateMemoryBuffer`, `MFCreateMediaType`, `MFCreateAttributes`, `MFCreateGuid`, `GetD3DFormatFromMFSubtype`) and hardware-accelerated H.264/HEVC decode via macOS `VideoToolbox`/`CoreMedia` plus `AVFoundation` video capture.
  5. **Graphics & Shaders (`vkd3d-shader` MSL & `d2d1`):**
     * **Upstream Gap (`Direct3D.md` & `Ospp-Summer-Code.md`):** Missing `vkd3d-shader` **MSL (Metal Shading Language)** backend and unimplemented Direct2D primitives (arcs, layers, anti-aliasing, gradient brushes, true cubic Beziers).
* **3-Layer Discovery Scanner (`./scripts/test.sh --discover-stubs`):**
  * Parses `.spec` `@ stub` entries, C AST/regex full and partial branch `FIXME`s, and optional `WINEDEBUG=+fixme,+relay` runtime logs.
  * Scores stubs by `wineserver` independence and macOS native framework readiness.

### Phase 3: The Agentic Coding Loop

* **Surgical CLI Capabilities (`scripts/` + `scripts/lib/`):**
  * `./scripts/test.sh --fetch-spec <dll>:<api_name>`: Fetches official Microsoft Learn specs, extracts public Wine header declarations, and persists an auditable Clean-Room Spec Packet (`specs/<dll>/<api_name>.json`).
  * `./scripts/build.sh --borrow <dll> [--dry-run]`: Surgically borrows or 3-way backports a single DLL from read-only `.wine-upstream/` while preserving `#ifdef __APPLE__` acceleration blocks.
  * `./scripts/build.sh --dll <dll> --test <test_name> [--arch x86_64|arm64]`: Surgically compiles only the single conformance test binary (using `tests/include/parity_test.h`) and/or single Wine override DLL without rebuilding unrelated targets.
  * `./scripts/test.sh --dll <dll> --test <test_name> [--filter <case>] [--live-vm]`: Executes the test against cached or live Windows VM Ground Truth and Wine, outputting structured field-level discrepancies.
  * `./scripts/build.sh --dll <dll> --export-patch`: Exports a conformant `git format-patch` patch with clean-room provenance metadata.

### Phase 4: Patch Formatting, Security Audit & Regression Guard

* **Conformance (`./scripts/test.sh --lint`):** Enforces WineHQ and repository coding conventions:
  * 4-space indentation, no tabs, pure C99/C11, classic `#ifndef` include guards (no `#pragma once`).
  * Wine debug logging (`TRACE`, `WARN`, `FIXME`, `ERR`; zero raw `printf`/`fprintf` in `dlls/`).
  * Strict Markdown CommonMark/GFM compliance with inline-code bracketed badges (`` **`[...]`** ``).
* **4-Gate Security Scanner (`./scripts/test.sh --security`):** Enforces read-only upstream guard (`DISABLE_PUSH_READ_ONLY_UPSTREAM`), zero external URLs in static skills, C/Python SAST, and Clang ASan+UBSan (`-fsanitize=address,undefined`) runtime checks.
* **Surgical Regression Testing & CI:** Targeted per-DLL regression runs and GitHub Actions CI running on `macos-latest` (primary) and `ubuntu-latest` (secondary).

---

## 2. Clean-Room Legal Safeguards

To ensure that contributions are clean and avoid copyright concerns (aligned with WineHQ's `Clean-Room-Guidelines.md`):

1. **No Proprietary Decompilations:** The agent must never be fed disassemblies of proprietary Microsoft DLLs, Microsoft PDB debug symbols, `+relay` traces of native Microsoft components, or decompiled code from tools like IDA Pro or Ghidra.
2. **No Leaked or Tainted Code in Context:** Prompts must explicitly disallow reference to leaked Windows source archives (e.g. WRK, Windows 2000/XP leaked code) or ReactOS source/headers.
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
| **Wine Environment** | macOS Wine (`wine64` Rosetta 2 / ARM64EC) + `WINEDLLOVERRIDES` fast path + read-only `.wine-upstream/` |
| **Compilers** | `clang`/`gcc`, `x86_64-w64-mingw32-gcc`, `aarch64-w64-mingw32-gcc` |
| **Canonical Scripts** | `scripts/build.sh`, `scripts/launch.sh`, `scripts/stop.sh`, `scripts/test.sh` + `scripts/lib/` |
| **Diff & Logging** | JSON Schema (`tests/schema/result.json`) + Dual-mode C SDK (`tests/include/parity_test.h`) |

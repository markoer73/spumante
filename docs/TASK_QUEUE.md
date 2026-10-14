# Global Task Queue

This queue coordinates tasks between AI assistants (Antigravity, Gemini, Claude, Codex, OpenCode, Cline) and human developers.

> **Rules:**
> 1. Mark in-progress tasks as `**\[AGENT-WIP YYYY-MM-DD]**` (escaping the opening bracket so CommonMark link parsing and KaTeX `\[...\]` math mode are both avoided).
> 2. Mark completed tasks as `- [x]` with `**\[AGENT-COMPLETED YYYY-MM-DD: <summary>]**`.
> 3. Concurrently update `docs/TASK_LOG.md` and remove completed tasks from `docs/TASK_LEFT_SUMMARY.md`.
> 4. Adhere strictly to the **Iron Law of Marcommit**: never commit code without explicit user instruction.

---

## Critical Priority Tasks

- [x] Set up QEMU/KVM headless Windows 10/11 VM orchestration scripts **\[ANTIGRAVITY-COMPLETED 2026-10-14: implemented macOS-first hvf/tcg and Linux kvm VM orchestration via scripts/launch.sh, scripts/stop.sh, and scripts/lib/vm_manager.py with VirtIO serial qemu-ga and QMP snapshots]**
- [x] Implement differential test runner CLI comparing Windows VM output against Wine **\[ANTIGRAVITY-COMPLETED 2026-10-14: implemented surgical differential runner in scripts/test.sh, scripts/lib/runner.py, and scripts/lib/comparator.py supporting exact, handle_state, masked, and roundtrip modes]**
- [x] Create `wine-borrow` skill and Sparse-Overlay + Manifest upstream Wine alignment engine (`upstream/manifest.json`, `scripts/lib/wine_borrow.py`, `docs/skills/WINE_BORROW.md`, `.agents/skills/wine-borrow/SKILL.md`) **\[ANTIGRAVITY-COMPLETED 2026-10-14: implemented read-only blobless sparse-checkout borrow engine, DISABLE_PUSH_READ_ONLY_UPSTREAM lock, 3-way backport preserving macOS blocks, upstream/manifest.json, and wine-borrow skill]**
- [x] Create `security-audit` skill, 4-gate commit/sanitizer scanner (`./scripts/test.sh --security` & `scripts/lib/security_scanner.py`), and periodic CI security schedule **\[ANTIGRAVITY-COMPLETED 2026-10-14: implemented self-contained static security-audit skill, 4-gate secret/clean-room/SAST/ASan+UBSan scanner, read-only upstream enforcement, and weekly cron CI]**
- [x] Expand `dlls/bcrypt` with macOS `CommonCrypto` & `Security.framework` backends (`BCryptEncrypt`, `BCryptDecrypt`, `BCryptGenRandom`, `BCryptCreateHash`/`BCryptHashData`/`BCryptFinishHash`, `BCryptDeriveKeyPBKDF2`, `BCRYPT_PAD_NONE` RSA/ECC, and `.spec` provider/context stubs `BCryptEnumProviders`, `BCryptResolveProviders`, `GetCipherInterface`, `GetHashInterface`, `GetRngInterface`) **\[ANTIGRAVITY-COMPLETED 2026-10-14: implemented AES-CBC/PKCS#7, BCryptGenRandom, SHA-256/reusable HMAC-SHA256, BCryptDeriveKeyPBKDF2, RSA/ECC BCRYPT_PAD_NONE, and provider/interface functions in dlls/bcrypt/ with 12 passing differential assertions in test_crypto_primitives]**
- [x] Implement upstream Wine `winetest` & A/B (`wine-parity` vs. old `.wine-upstream`) anti-regression harness (`./scripts/test.sh --winetest`, `scripts/lib/winetest_runner.py`, `tests/include/wine/test.h`, and `tests/winetest/`) as a mandatory follow-up stage to unit and security tests **\[ANTIGRAVITY-COMPLETED 2026-10-14: implemented dual-mode wine/test.h, winetest_runner.py with A/B & ASan+UBSan verification, tests/winetest/bcrypt/winetest_bcrypt.c with 189 passing assertions across 13 subtests, and CI follow-up integration]**
- [x] Implement Apple Silicon (`arm64`) execution parity for Intel (`x86_64` & `i386`) Windows apps in `dlls/ntdll` (Rosetta 2 write-fault/LDT/WoW64 parity in `signal_x86_64.c`/`virtual.c` AND native `arm64-apple-darwin` + `ARM64EC` `fexcore.dll`/`box64cpu.dll` support: Darwin `x18` platform-register save/restore in `signal_arm64.c`, 16 KB host page vs. 4 KB Windows PE page guard/write-watch emulation in `virtual.c`, and `MAP_JIT`/`pthread_jit_write_protect_np` W^X toggling) **\[ANTIGRAVITY-COMPLETED 2026-10-14: implemented 16KB host vs 4KB Windows sub-page PAGE_GUARD and MEM_WRITE_WATCH emulation, Rosetta 2 read-fault promotion, MAP_JIT W^X toggling, Darwin x18 preservation, ARM64EC fexcore/box64cpu engine, and WoW64/LDT/GSBASE parity in dlls/ntdll with 8 differential assertions and 908 winetest assertions passing]**

---

## High Priority Tasks

- [x] Build automated stub discovery scanner for Wine DLLs (`dlls/bcrypt`, `dlls/kernelbase`, `dlls/mfplat`) **\[ANTIGRAVITY-COMPLETED 2026-10-14: implemented 3-layer stub discovery scanner in scripts/lib/stub_scanner.py via ./scripts/test.sh --discover-stubs]**
- [x] Create standardized JSON output schema for differential test assertions (`tests/schema/result.json`) **\[ANTIGRAVITY-COMPLETED 2026-10-14: created tests/schema/result.json and dual-mode C SDK header tests/include/parity_test.h]**
- [ ] Implement macOS fast NT synchronization (`NTSync` parity) in `dlls/ntdll` and `dlls/kernelbase` (`WaitOnAddress`, `WakeByAddressSingle`, `WakeByAddressAll`, `NtWaitForAlertByThreadId`) backed by Darwin `__ulock_wait2`/`__ulock_wake` and Mach semaphores
- [ ] Borrow and implement `dlls/winemac.drv` missing macOS windowing & Metal features: `IOSurface`-backed cross-process child window `CAMetalLayer` swapchains (`window.c:1177`), multi-monitor `ChangeDisplaySettingsEx` on non-primary displays (`display.c:767-772`), and cross-process window DCs / layered attributes (`opengl.c:1139`, `window.c:1318`)
- [ ] Expand `dlls/kernelbase` missing Win32 utility APIs (`GetSystemTimePreciseAsFileTime`, `SetThreadDescription`, `GetThreadDescription`, `CompareObjectHandles`, `VirtualAlloc2`, `MapViewOfFile3`) with Darwin/Mach backends and differential conformance tests

---

## Medium Priority Tasks

- [x] Create initial differential conformance test harness for `BCryptGenerateSymmetricKey` **\[ANTIGRAVITY-COMPLETED 2026-10-14: implemented tests/conformance/bcrypt/test_symmetric_key.c, Ground-Truth fixture, and macOS CommonCrypto + C99 fallback in dlls/bcrypt/bcrypt_main.c with 11 passing differential assertions]**
- [x] Add MinGW-w64 cross-compilation pipeline for standalone conformance tests **\[ANTIGRAVITY-COMPLETED 2026-10-14: integrated x86_64-w64-mingw32-gcc and aarch64-w64-mingw32-gcc surgical compilation in scripts/build.sh and scripts/lib/runner.py]**
- [ ] Implement `dlls/mfplat` core Media Foundation APIs (`MFStartup`, `MFShutdown`, `MFCreateMemoryBuffer`, `MFCreateMediaType`, `MFCreateAttributes`, `MFCreateGuid`, `GetD3DFormatFromMFSubtype`) and macOS `VideoToolbox`/`CoreMedia` hardware H.264/HEVC decode bridge
- [ ] Implement macOS `AVFoundation` video capture backend (`qcap`/`mfreadwrite`) and DirectShow `CLSID_AudioRecord` audio capture (`Summer-Of-Code.md` roadmap)

---

## Low Priority Tasks

- [x] Set up GitHub Actions CI workflow for linting and differential test execution **\[ANTIGRAVITY-COMPLETED 2026-10-14: created .github/workflows/ci.yml targeting macos-latest and ubuntu-latest alongside scripts/lib/linter.py]**
- [ ] Introduce `vkd3d-shader` MSL (Metal Shading Language) generator and Direct2D missing primitives (arcs, layers, anti-aliasing, gradient brushes, true cubic Beziers per `Direct3D.md` & `Ospp-Summer-Code.md`)
- [ ] Address ARM64EC/ARM64X loader edge cases (`NtReadFile` PE header relocation view for `system32` DLLs, TSC registry frequency rescaling, and on-disk JIT code caching per WineConf 2025 ARM64 roadmap)

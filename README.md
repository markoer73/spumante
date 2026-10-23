# Wine Parity Project (`wine-parity`)

An engineering initiative exploring the use of autonomous AI coding agents to systematically bring **Wine (Wine Is Not an Emulator)** to feature and UX parity with modern Windows—prioritizing **macOS first** (Apple Silicon & Intel, native Apple frameworks) and **Linux second**.

---

## 1. Project Overview & Premise

Wine has been in continuous development for over 30 years, translating Windows API calls (Win32, NT, COM, DirectX) to POSIX/Darwin system calls on macOS and Linux. While projects like Proton and DXVK have made massive strides in gaming, thousands of Win32 and NT APIs remain unimplemented stubs (`fixme:` annotations).

Rather than attempting an impossible real-time "AI-powered runtime" (which would be orders of magnitude too slow and non-deterministic), this project uses **agentic AI at development time** within a **closed-loop differential testing harness**.

---

## 2. The Core Architecture: Closed-Loop Ground-Truth Harness

```
 ┌───────────────────────────────────────────────────────────────┐
 │ 1. TARGET DISCOVERY (./scripts/test.sh --discover-stubs)      │
 │    • Layer 1: .spec '@ stub' declarations                     │
 │    • Layer 2: C source full/partial 'FIXME' branches          │
 │    • Layer 3: Dynamic WINEDEBUG=+relay,+fixme logs            │
 └───────────────────────────────┬───────────────────────────────┘
                                 │
                                 ▼
 ┌───────────────────────────────────────────────────────────────┐
 │ 2. CLEAN-ROOM SPEC & TEST SYNTHESIS                           │
 │    • Fetch spec packet (./scripts/test.sh --fetch-spec)       │
 │    • Write dual-mode C conformance test (parity_test.h)       │
 └───────────────────────────────┬───────────────────────────────┘
                                 │
                 ┌───────────────┴───────────────┐
                 ▼                               ▼
 ┌───────────────────────────────┐ ┌─────────────────────────────┐
 │ 3A. WINDOWS 10/11 VM (QEMU)   │ │ 3B. RUN ON WINE (macOS/Linux│
 │     • VirtIO serial qemu-ga   │ │     • Ephemeral /tmp prefix │
 │     • QMP loadvm + JSON cache │ │     • <1s single-DLL build  │
 └───────────────┬───────────────┘ └─────────────┬───────────────┘
                 │                               │
                 └───────────────┬───────────────┘
                                 │
                                 ▼
 ┌───────────────────────────────────────────────────────────────┐
 │ 4. SURGICAL IMPLEMENTATION & PATCH SYNTHESIS                  │
 │    • ./scripts/build.sh --dll <name> --test <test_name>       │
 │    • ./scripts/test.sh --dll <name> --test <test_name>        │
 │    • Loop until Wine output matches Ground Truth 100%         │
 └───────────────────────────────┬───────────────────────────────┘
                                 │
                                 ▼
 ┌───────────────────────────────────────────────────────────────┐
 │ 5. SECURITY, UPSTREAM WINETEST & SUBMISSION                   │
 │    • ./scripts/test.sh --security --dll <name> --test <name>  │
 │    • ./scripts/test.sh --winetest --dll <name>                │
 │    • ./scripts/test.sh --lint                                 │
 │    • ./scripts/build.sh --dll <name> --export-patch           │
 └───────────────────────────────────────────────────────────────┘
```

---

## 3. Canonical Script Interface & Surgical Single-Target Workflow

All host/VM orchestration, compilation, differential testing, stub discovery, and linting are centralized into **four canonical, parametrized shell scripts** in `scripts/` (backed by internal modules in `scripts/lib/`). Full-tree rebuilds are refused by default to prevent rebuilding 1,000+ targets when verifying a single API or test case:

```sh
# 1. Surgically build ONLY a single DLL and conformance test (<1s)
./scripts/build.sh --dll bcrypt --test test_symmetric_key

# 2. Run ONLY the targeted differential test (with optional case filter)
./scripts/test.sh --dll bcrypt --test test_symmetric_key --filter "AES128*"

# 3. Run 4-gate security audit & upstream Wine winetest A/B anti-regression suite
./scripts/test.sh --security --dll bcrypt --test test_crypto_primitives
./scripts/test.sh --winetest --dll bcrypt

# 4. Build & verify Apple Silicon (arm64) execution parity, ARM64X loader/TSC/JIT cache & macOS NTSync in ntdll and kernelbase
./scripts/build.sh --dll ntdll --test test_apple_silicon_parity
./scripts/test.sh --dll ntdll --test test_apple_silicon_parity
./scripts/build.sh --dll ntdll --test test_ntsync_macos_parity
./scripts/test.sh --dll ntdll --test test_ntsync_macos_parity
./scripts/build.sh --dll ntdll --test test_arm64x_loader_parity
./scripts/test.sh --dll ntdll --test test_arm64x_loader_parity
./scripts/test.sh --winetest --dll ntdll
./scripts/build.sh --dll kernelbase --test test_wait_on_address
./scripts/test.sh --dll kernelbase --test test_wait_on_address
./scripts/build.sh --dll kernelbase --test test_win32_utility_apis
./scripts/test.sh --dll kernelbase --test test_win32_utility_apis
./scripts/test.sh --winetest --dll kernelbase

# 5. Build & verify macOS windowing, multi-monitor ChangeDisplaySettingsEx & IOSurface Metal swapchains in winemac.drv
./scripts/build.sh --dll winemac.drv --test test_winemac_parity
./scripts/test.sh --dll winemac.drv --test test_winemac_parity
./scripts/test.sh --winetest --dll winemac.drv

# 6. Build & verify Media Foundation core APIs & macOS VideoToolbox H.264/HEVC hardware decode in mfplat
./scripts/build.sh --dll mfplat --test test_mfplat_parity
./scripts/test.sh --dll mfplat --test test_mfplat_parity
./scripts/test.sh --winetest --dll mfplat

# 7. Build & verify macOS AVFoundation video capture, DirectShow CLSID_AudioRecord & mfreadwrite bridge in qcap
./scripts/build.sh --dll qcap --test test_qcap_parity
./scripts/test.sh --dll qcap --test test_qcap_parity
./scripts/test.sh --winetest --dll qcap

# 8. Build & verify Direct2D primitives (arcs, layers, AA, gradients, cubic Beziers) & vkd3d-shader MSL in d2d1
./scripts/build.sh --dll d2d1 --test test_d2d1_msl_parity
./scripts/test.sh --dll d2d1 --test test_d2d1_msl_parity
./scripts/test.sh --winetest --dll d2d1

# 5. Discover & rank unimplemented stubs across Wine DLLs
./scripts/test.sh --discover-stubs --dll bcrypt

# 5. Generate an auditable Clean-Room Spec Packet in specs/<dll>/<api>.json
./scripts/test.sh --fetch-spec bcrypt:BCryptGenerateSymmetricKey

# 6. Launch/verify or stop the headless QEMU Windows VM & Guest Agent
./scripts/launch.sh --arch arm64 --dry-run
./scripts/stop.sh --all

# 7. Run WineHQ C99/C11, Markdown & KaTeX badge, and script boundary linter
./scripts/test.sh --lint

# 8. Complete Turnkey Build (All wine-parity DLLs/tests + Upstream Wine Installation)
#    --full-x86 / --x86      : Builds --all (x86_64) + upstream Wine (--enable-archs=i386,x86_64)
#                              to run 32-bit & 64-bit x86 Windows binaries on macOS (Rosetta 2 / Intel)
#    --full-arm64 / --arm64  : Builds --all (arm64) + upstream Wine (--enable-archs=arm64ec,i386)
#                              for native Apple Silicon ARM64 & ARM64EC execution
./scripts/build.sh --full-x86
./scripts/build.sh --full-arm64
```

---

## 4. Key Engineering Boundaries

* **Clean-Room Reverse Engineering:** AI prompts and implementations strictly use black-box inputs (Microsoft Learn documentation, public Wine headers, and differential test failure logs) with zero exposure to leaked Microsoft source code or proprietary disassemblies.
* **macOS-First Hardware & Framework Exploitation:** Prioritizes Apple Silicon (`hvf` virtualization, `CommonCrypto` / `Security.framework`, `CoreFoundation`, `VideoToolbox` / `CoreMedia`, Darwin/Mach APIs) before Linux (`kvm`, POSIX/GnuTLS).
* **Wineserver Statefulness:** Stateless utility APIs live in client DLLs (`dlls/*`); APIs managing cross-process handles, security tokens, or synchronization primitives require `wineserver` protocol IPC.

See [PROJECT_BLUEPRINT.md](PROJECT_BLUEPRINT.md) and [docs/skills/ARCHITECTURE.md](docs/skills/ARCHITECTURE.md) for detailed technical specifications.

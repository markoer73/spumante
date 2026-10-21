# Upstream Wine Alignment & The `wine-borrow` Skill

This document defines the mandatory architecture and workflow for keeping `wine-parity` aligned with the upstream Wine project (`gitlab.winehq.org/wine/wine.git`) while maintaining our **macOS-first** divergence and **strictly avoiding duplication of the full upstream Wine source tree**.

---

## 1. Core Principles & Hard Invariants

1. **Strict Read-Only Upstream Wine Repository (Hard Invariant)**:
   - The original Wine repository (`winehq.org`) is **STRICTLY READ-ONLY**. We never push to, modify, or configure write access to upstream Wine.
   - Immediately upon cloning `.wine-upstream/`, its push URL is locked to `DISABLE_PUSH_READ_ONLY_UPSTREAM`:
     ```sh
     git -C .wine-upstream remote set-url --push origin DISABLE_PUSH_READ_ONLY_UPSTREAM
     ```
   - `wine-parity` is hosted in its **own standalone GitHub repository**, where all commits, branches, and GitHub Actions CI workflows execute.

2. **Zero Full-Tree Duplication**:
   - Upstream Wine contains >5,000 source files and hundreds of megabytes of legacy subsystems. **NEVER copy or commit the full upstream Wine source tree into `wine-parity`.**
   - Upstream Wine is fetched on demand into an ignored local blobless sparse checkout at `.wine-upstream/` (`git clone --filter=blob:none --no-checkout --sparse`), materializing **only** `include/` and the specific `dlls/<name>` directories tracked in [`upstream/manifest.json`](../../upstream/manifest.json).

3. **Parallel Divergence with Surgical Backporting**:
   - `wine-parity` diverges from upstream Wine where needed to prioritize **macOS first** (Apple Silicon `hvf`, `CommonCrypto`/`Security.framework`, `CoreFoundation`, `VideoToolbox`/`CoreMedia`, Darwin/Mach APIs) and strict Windows 10/11 Ground-Truth parity.
   - Simultaneously, we continuously borrow and re-backport upstream Wine bug fixes, `.spec` updates, and new `winetest` cases from `.wine-upstream/` into `dlls/<name>/`.

4. **Centralized Script Execution Only (Gate 4)**:
   - Never create standalone shell scripts for upstream syncing or full builds. Use only the canonical entrypoints:
     - `./scripts/build.sh --borrow <dll> [--upstream-ref <sha_or_tag>] [--dry-run]`
     - `./scripts/build.sh --full-x86` (or `--x86` / `--full-x86_64`) — Builds all `wine-parity` DLLs/tests (`--all --arch x86_64`) + complete upstream Wine + parity overlay configured to run 32-bit `i386` and 64-bit `x86_64` Windows binaries on macOS (`--enable-archs=i386,x86_64 --without-x --without-wayland`, installing into `builds/wine-install-x86/` with `builds/wine-install-x86/bin/wine-parity`).
     - `./scripts/build.sh --full-arm64` (or `--arm64`) — Builds all `wine-parity` DLLs/tests (`--all --arch arm64`) + complete upstream Wine + parity overlay configured for native Apple Silicon ARM64 + ARM64EC/i386 (`--enable-archs=arm64ec,i386 --without-x --without-wayland`, installing into `builds/wine-install-arm64/` with `builds/wine-install-arm64/bin/wine-parity`).
     - `./scripts/test.sh --check-upstream --dll <dll>`

---

## 2. Sparse-Overlay + Manifest Architecture

```
  ┌─────────────────────────────────────────────────────────────────┐
  │ Upstream Wine Git (`gitlab.winehq.org/wine/wine.git`)           │
  │ [STRICTLY READ-ONLY — Push Disabled]                            │
  └───────────────────────────────┬─────────────────────────────────┘
                                  │ git clone --filter=blob:none --sparse
                                  ▼
  ┌─────────────────────────────────────────────────────────────────┐
  │ `.wine-upstream/` (Git-Ignored Local Sparse Cache)              │
  │   • Materializes ONLY `include/` + `dlls/<target_dll>/`         │
  │   • Push locked to `DISABLE_PUSH_READ_ONLY_UPSTREAM`            │
  └───────────────┬─────────────────────────────────┬───────────────┘
                  │ 3-Way Surgical Backport         │ Harvest Tests
                  ▼ (`git merge-file` / overlay)    ▼
  ┌───────────────────────────────┐ ┌───────────────────────────────┐
  │ `dlls/<dll_name>/`            │ │ `tests/conformance/<dll>/`    │
  │   • Preserves `#ifdef __APPLE__`│ │   • Dual-mode `parity_test.h` │
  │   • Tracked in `manifest.json`│ │   • Verified vs. Ground Truth │
  └───────────────────────────────┘ └───────────────────────────────┘
```

### [`upstream/manifest.json`](../../upstream/manifest.json)
Every borrowed DLL entry in `upstream/manifest.json` records:
- `upstream_access_mode`: Must always be `"read-only"`.
- `upstream_path`: Path inside upstream Wine (e.g., `dlls/bcrypt`).
- `base_upstream_commit`: The upstream commit SHA or tag currently synced.
- `merge_strategy`: `3-way-backport-preserving-macos-blocks`.
- `macos_acceleration`: Native Apple frameworks layered on top of upstream logic.
- `tracked_files` & `file_hashes`: SHA-256 digests of local files to detect drift.
- `required_conformance_tests`: Surgical tests that MUST pass before any upstream backport is accepted.

---

## 3. Step-by-Step `wine-borrow` Workflow for AI Agents

### Step 1: Audit Upstream Alignment & Read-Only Guard
Before borrowing or updating a DLL, run:
```sh
./scripts/test.sh --check-upstream --dll bcrypt
```
This verifies `upstream/manifest.json` (`"upstream_access_mode": "read-only"`), computes SHA-256 integrity of tracked files in `dlls/<dll>/`, and checks whether `.wine-upstream/` has newer upstream commits affecting `dlls/<dll>/`.

### Step 2: Preview or Execute Surgical Sparse Borrow / Backport
To initialize or update the read-only blobless sparse checkout for a single DLL and run a 3-way backport:
```sh
# Preview sparse-checkout, read-only push lock, and 3-way merge plan
./scripts/build.sh --borrow bcrypt --dry-run

# Perform sparse checkout + surgical 3-way merge for a single DLL
./scripts/build.sh --borrow bcrypt
```

### Step 3: Mandatory Differential & Security Regression Gate
After borrowing or backporting any upstream code into `dlls/<dll>/`:
1. Surgically build the target DLL and its conformance tests:
   ```sh
   ./scripts/build.sh --dll bcrypt --test test_symmetric_key
   ```
2. Run the differential conformance test against Windows Ground Truth:
   ```sh
   ./scripts/test.sh --dll bcrypt --test test_symmetric_key
   ```
3. Run the 4-gate security audit and WineHQ style linter:
   ```sh
   ./scripts/test.sh --security --dll bcrypt --test test_symmetric_key
   ./scripts/test.sh --lint
   ```
4. **Revert on Regression**: If any differential assertion or security gate fails, fix or revert before updating `upstream/manifest.json`.

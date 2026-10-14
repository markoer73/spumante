# Test Execution Rules, Centralized Scripts & Differential Testing Policy

## 1. The Iron Law of Centralized Scripts & Anti-Proliferation

All VM management, guest agent control, building, test execution, stub discovery, linting, and CI verification MUST be performed exclusively through four canonical, parametrized shell scripts in `scripts/` (backed by internal Python/shell modules in `scripts/lib/`):

1. **`scripts/build.sh`**: Compiles a single conformance test binary, a single Wine override DLL, or syncs/exports patches.
2. **`scripts/launch.sh`**: Starts/verifies the headless QEMU Windows 10/11 VM (`hvf`/`tcg` on macOS, `kvm` on Linux), QMP socket, and VirtIO serial `qemu-ga` channel, or restores a warm snapshot (`--snapshot clean_ready`).
3. **`scripts/stop.sh`**: Cleanly stops or resets the QEMU VM daemon, sockets, and ephemeral `/tmp/wine_test_*` prefixes.
4. **`scripts/test.sh`**: Runs surgical differential tests, stub discovery (`--discover-stubs`), clean-room spec packets (`--fetch-spec`), WineHQ style linting (`--lint`), and CI checks (`--ci`).

**Strict Anti-Proliferation Rule**: Do NOT create new standalone scripts in the repository root or scatter ad-hoc `.sh`/`.py` entrypoints across directories. Add flags/parameters to the four canonical scripts in `scripts/` and place internal implementation modules in `scripts/lib/`.

---

## 2. Surgical Single-Target Build & Execution Policy (Mandatory)

**Never rebuild the entire Wine tree or execute 1,000+ tests across hundreds of DLLs when verifying an isolated function, bug fix, or test case.**

```sh
# 1. Surgically build ONLY a single conformance test binary (x86_64 or arm64)
./scripts/build.sh --dll bcrypt --test test_symmetric_key --arch x86_64

# 2. Surgically build ONLY a single Wine DLL override (<1s incremental build)
./scripts/build.sh --dll bcrypt

# 3. Run ONLY the targeted differential test and specific test case filter
./scripts/test.sh --dll bcrypt --test test_symmetric_key --filter "AES128*"

# 4. Run WineHQ style & Markdown compliance linter on modified files
./scripts/test.sh --lint
```

- **Default Surgical Guard**: `scripts/build.sh` and `scripts/test.sh` require a specific `--dll` or `--test` target by default and refuse full-project builds unless `--all` is explicitly passed.
- **Ground-Truth Cache Acceleration**: During the iterative GREEN/REFACTOR loop on macOS Wine, `scripts/test.sh` compares Wine output against cached Ground-Truth JSON records in `tests/ground_truth/<dll>/` in milliseconds, and supports `--live-vm` to force fresh execution against the warm QEMU Windows VM.

---

## 3. Dual-Target Environment (macOS First, Linux Second)

Differential testing operates across two isolated environments:
1. **Windows VM (Ground Truth Oracle)**:
   - Genuine Windows 10/11 running headless under QEMU (`-accel hvf` on Apple Silicon macOS for ARM64 Windows 11 + x64 emulation, `-accel tcg`/`hvf` for x86_64, or KVM on Linux).
   - Host-local VirtIO serial `qemu-ga` Unix socket (`/tmp/wine_parity_qga.sock`) + QMP control socket (`/tmp/wine_parity_qmp.sock`) for sub-second `loadvm clean_ready` resets.
   - Zero external internet access (`-nic none` or restricted loopback).
2. **Wine Execution Environment**:
   - Ephemeral Wine prefix (`WINEPREFIX=/tmp/wine_test_$$`) with drive `Z:` isolated and `winemenubuilder.exe=d`.
   - Prioritizes native macOS frameworks (`CommonCrypto`/`Security.framework`, `CoreFoundation`, `VideoToolbox`/`CoreMedia`, Darwin/Mach) with `#ifdef __linux__` fallbacks.

---

## 4. Test Organization & Fixtures Policy

- All test cases live under `tests/conformance/<dll_name>/` and include `tests/include/parity_test.h`.
- Differential assertion outputs validate against `tests/schema/result.json` using field-level comparison modes (`exact`, `handle_state`, `masked`, `roundtrip`).
- Tests must be **hermetic, deterministic, and self-contained** with zero network calls.


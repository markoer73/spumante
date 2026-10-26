# Security Audit, Commit Scanning & Memory-Safety Playbook

This document is the self-contained, statically stored authority for security reviews, pre-commit scanning, C memory-safety verification, and periodic security testing in `spumante`.

> **Self-Contained Static Policy:** Security skills and rules in this repository must never depend on or link to floating external web URLs. All checklists, SAST patterns, and sanitizer workflows are stored statically in this file and executed deterministically via `./scripts/test.sh --security`.

---

## 1. The Four-Gate Automated Security Architecture (`./scripts/test.sh --security`)

Every commit, pull request, `marcommit` pre-flight, and weekly scheduled CI run executes four mandatory security gates implemented in [`scripts/lib/security_scanner.py`](../../scripts/lib/security_scanner.py):

### Gate A: Secret, Forbidden Artifact, Static-Skill & Clean-Room Scanner
1. **Secret & Credential Detection**:
   - Scans tracked and modified files for private keys (`BEGIN .* PRIVATE KEY`), cloud/API tokens (`AKIA[0-9A-Z]{16}`, `ghp_`, `github_pat_`, `AIza[0-9A-Za-z\-_]{35}`, `xox[baprs]-`), and hardcoded passwords/tokens.
2. **Forbidden Binary & Image Prevention**:
   - Rejects any unignored `.iso`, `.qcow2`, `.fd`, `.rom`, or precompiled `.exe`/`.dll`/`.dylib`/`.so` outside `builds/`.
3. **Clean-Room Anti-Decompilation Attestation**:
   - Scans `dlls/` and `tests/` for decompiler signatures (`IDA Pro`, `Hex-Rays`, `Ghidra`, `sub_140`, `__usercall`, `Windows Research Kernel`, `WRK`) to guarantee 100% black-box clean-room implementation.
4. **No Floating External URLs in Agent Skills/Rules**:
   - Verifies that `.agents/skills/*/SKILL.md`, `.agents/rules/*.md`, and `docs/skills/SECURITY_AUDIT.md` contain zero external HTTP or HTTPS web links, preventing supply-chain prompt injection or broken external references.

### Gate B: Static C/Python SAST & Sandbox Invariant Scanner
1. **Banned Unsafe C Functions (`dlls/` & `tests/`)**:
   - Rejects unbounded string/memory functions: `strcpy`, `strcat`, `sprintf`, `vsprintf`, `gets`, `alloca`, `scanf`, `sscanf`.
2. **Banned Unsafe Python Primitives (`scripts/lib/`)**:
   - Rejects `shell=True` in `subprocess` calls, `eval()`, `exec()`, `pickle.loads()`, and `yaml.load()` without `SafeLoader`.
3. **Host & VM Sandbox Invariant Verification**:
   - Verifies that [`scripts/lib/runner.py`](../../scripts/lib/runner.py) enforces ephemeral `WINEPREFIX` in `/tmp`, unlinks drive `Z:`, and sets `winemenubuilder.exe=d`.
   - Verifies that [`scripts/lib/vm_manager.py`](../../scripts/lib/vm_manager.py) enforces `-nic none` network isolation on the QEMU Windows VM.

### Gate C: C Memory-Safety Sanitizer Verification (ASan + UBSan)
- Surgically compiles the target Wine DLL (`dlls/<dll>/*.c`) and conformance test (`tests/conformance/<dll>/<test>.c`) with Clang/GCC sanitizers:
  ```sh
  -fsanitize=address,undefined -fno-omit-frame-pointer -g -O1
  ```
- Executes the sanitizer-instrumented binary inside an isolated ephemeral prefix with `ASAN_OPTIONS=detect_leaks=0:abort_on_error=1` and `UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1`, verifying zero buffer overflows, use-after-free bugs, null dereferences, signed integer overflows, or misaligned pointer accesses.

### Gate D: Differential Commit Review Checklist (Manual + Agent Pre-Commit)
Before staging or committing any C code in `dlls/`:
1. **Integer Overflow Before Allocation**:
   - Every `malloc`/`calloc`/`realloc` computing `count * size` or `header_size + payload_size` must check for unsigned wrap before allocation:
     ```c
     if (cbSecret > UINT32_MAX - sizeof(BCRYPT_KEY_DATA_BLOB_HEADER)) {
         return STATUS_INVALID_PARAMETER;
     }
     ```
2. **Output Buffer Bounds & Query-Size Semantics**:
   - Validate `pbOutput == NULL` (size query) before checking `cbOutput < required_len` (`STATUS_BUFFER_TOO_SMALL`), and never write past `cbOutput`.
3. **Handle Lifecycle & Secret Zeroization**:
   - Validate handle magic tags before dereferencing opaque `BCRYPT_HANDLE` / `HANDLE` pointers.
   - Invalidate magic tags (`key_obj->magic = 0`) and zero sensitive key material (`memset(key_obj->secret, 0, ...)`) before `free()`.

---

## 2. Canonical Commands & Periodic Schedule

```sh
# Run the full 4-gate security audit on a specific DLL and conformance test
./scripts/test.sh --security --dll bcrypt --test test_symmetric_key

# Run both WineHQ style linting and the 4-gate security audit (CI mode)
./scripts/test.sh --ci
```

In [`.github/workflows/ci.yml`](../../.github/workflows/ci.yml), the security audit runs automatically on:
- Every `push` and `pull_request` to `main`.
- A periodic weekly cron schedule (`0 6 * * 1` — Mondays at 06:00 UTC) across both `macos-latest` and `ubuntu-latest`.

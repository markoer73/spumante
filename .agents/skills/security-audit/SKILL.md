---
name: security-audit
description: Self-contained C memory-safety review, differential commit audit, secret/clean-room scanner, and ASan+UBSan sanitizer verification. Use before committing code, when modifying C DLLs or harness scripts, or during periodic security audits.
---

# Security Audit Skill (`security-audit`)

## Overview

This skill provides a 100% self-contained, local security review and automated scanning workflow for `spumante`. It contains zero external web links and executes through the canonical `./scripts/test.sh --security` entrypoint.

## When to Apply This Skill

- Before any commit (`marcommit`) or pull request.
- Whenever writing or modifying C code in `dlls/` or `tests/`.
- Whenever modifying VM orchestration or sandbox isolation in `scripts/lib/`.
- During periodic security reviews.

## Step-by-Step Security Audit Workflow

### Step 1: Differential C Memory-Safety Review (`c-review` & `differential-review`)
Inspect every modified C function in `dlls/<name>/` against these five mandatory invariants:
1. **Pointer & Handle Validation**: Check `hObject`/`hAlgorithm`/`hKey` for `NULL` and verify the internal struct magic number before dereferencing. Return `STATUS_INVALID_HANDLE` (`0xC0000008`) on mismatch.
2. **Buffer Size & Query Semantics**: Always set `*pcbResult = required_len` first; if `pbOutput == NULL`, return `STATUS_SUCCESS`; if `cbOutput < required_len`, return `STATUS_BUFFER_TOO_SMALL` (`0xC0000023`) before calling `memcpy`.
3. **Integer Overflow Guards**: Guard every `a + b` or `count * size` calculation against wrap-around before allocating heap memory or validating buffer bounds.
4. **Secret Zeroization & Use-After-Free Prevention**: In destruction routines (`BCryptDestroyKey`, `BCryptCloseAlgorithmProvider`), overwrite `magic = 0` and zero secret buffers (`memset`) before calling `free()`.
5. **No Banned Functions**: Never use `strcpy`, `strcat`, `sprintf`, `vsprintf`, `gets`, or `alloca`.

### Step 2: Execute the 4-Gate Automated Scanner (`./scripts/test.sh --security`)
Run the surgical security scanner on the target DLL and test:

```sh
./scripts/test.sh --security --dll bcrypt --test test_symmetric_key
```

Verify that all four gates report `"passed": true`:
1. `secret_and_clean_room_scan`: Zero leaked secrets/keys, zero forbidden binary images, zero IDA/Ghidra/WRK decompilation markers, and zero floating external URLs in skill/rule definitions.
2. `static_sast_and_sandbox_scan`: Zero banned C/Python functions and verified `WINEPREFIX` `/tmp` isolation (`winemenubuilder.exe=d`, drive `Z:` removed, QEMU `-nic none`).
3. `asan_ubsan_memory_safety`: Surgical compilation and execution with `-fsanitize=address,undefined -fno-omit-frame-pointer` passes with 0 sanitizer errors.
4. `ci_schedule_verification`: Confirms `.github/workflows/ci.yml` runs `./scripts/test.sh --security` on every push/PR and on a weekly cron schedule.

## Reference Documentation

See [`docs/skills/SECURITY_AUDIT.md`](../../../docs/skills/SECURITY_AUDIT.md) and [`docs/skills/THREAT_SECURITY_MODEL.md`](../../../docs/skills/THREAT_SECURITY_MODEL.md).

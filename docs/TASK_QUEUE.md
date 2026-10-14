# Global Task Queue

This queue coordinates tasks between AI assistants (Antigravity, Gemini, Claude, Codex, OpenCode, Cline) and human developers.

> **Rules:**
> 1. Mark in-progress tasks as `**\[AGENT-WIP YYYY-MM-DD\]**` with escaped square brackets.
> 2. Mark completed tasks as `- [x]` with `**\[AGENT-COMPLETED YYYY-MM-DD: <summary>\]**`.
> 3. Concurrently update `docs/TASK_LOG.md` and remove completed tasks from `docs/TASK_LEFT_SUMMARY.md`.
> 4. Adhere strictly to the **Iron Law of Marcommit**: never commit code without explicit user instruction.

---

## Critical Priority Tasks

- [x] Set up QEMU/KVM headless Windows 10/11 VM orchestration scripts **\[ANTIGRAVITY-COMPLETED 2026-10-14: implemented macOS-first hvf/tcg and Linux kvm VM orchestration via scripts/launch.sh, scripts/stop.sh, and scripts/lib/vm_manager.py with VirtIO serial qemu-ga and QMP snapshots\]**
- [x] Implement differential test runner CLI comparing Windows VM output against Wine **\[ANTIGRAVITY-COMPLETED 2026-10-14: implemented surgical differential runner in scripts/test.sh, scripts/lib/runner.py, and scripts/lib/comparator.py supporting exact, handle_state, masked, and roundtrip modes\]**
- [x] Create `wine-borrow` skill and Sparse-Overlay + Manifest upstream Wine alignment engine (`upstream/manifest.json`, `scripts/lib/wine_borrow.py`, `docs/skills/WINE_BORROW.md`, `.agents/skills/wine-borrow/SKILL.md`) **\[ANTIGRAVITY-COMPLETED 2026-10-14: implemented read-only blobless sparse-checkout borrow engine, DISABLE_PUSH_READ_ONLY_UPSTREAM lock, 3-way backport preserving macOS blocks, upstream/manifest.json, and wine-borrow skill\]**
- [x] Create `security-audit` skill, 4-gate commit/sanitizer scanner (`./scripts/test.sh --security` & `scripts/lib/security_scanner.py`), and periodic CI security schedule **\[ANTIGRAVITY-COMPLETED 2026-10-14: implemented self-contained static security-audit skill, 4-gate secret/clean-room/SAST/ASan+UBSan scanner, read-only upstream enforcement, and weekly cron CI\]**

---

## High Priority Tasks

- [x] Build automated stub discovery scanner for Wine DLLs (`dlls/bcrypt`, `dlls/kernelbase`, `dlls/mfplat`) **\[ANTIGRAVITY-COMPLETED 2026-10-14: implemented 3-layer stub discovery scanner in scripts/lib/stub_scanner.py via ./scripts/test.sh --discover-stubs\]**
- [x] Create standardized JSON output schema for differential test assertions (`tests/schema/result.json`) **\[ANTIGRAVITY-COMPLETED 2026-10-14: created tests/schema/result.json and dual-mode C SDK header tests/include/parity_test.h\]**

---

## Medium Priority Tasks

- [x] Create initial differential conformance test harness for `BCryptGenerateSymmetricKey` **\[ANTIGRAVITY-COMPLETED 2026-10-14: implemented tests/conformance/bcrypt/test_symmetric_key.c, Ground-Truth fixture, and macOS CommonCrypto + C99 fallback in dlls/bcrypt/bcrypt_main.c with 11 passing differential assertions\]**
- [x] Add MinGW-w64 cross-compilation pipeline for standalone conformance tests **\[ANTIGRAVITY-COMPLETED 2026-10-14: integrated x86_64-w64-mingw32-gcc and aarch64-w64-mingw32-gcc surgical compilation in scripts/build.sh and scripts/lib/runner.py\]**

---

## Low Priority Tasks

- [x] Set up GitHub Actions CI workflow for linting and differential test execution **\[ANTIGRAVITY-COMPLETED 2026-10-14: created .github/workflows/ci.yml targeting macos-latest and ubuntu-latest alongside scripts/lib/linter.py\]**

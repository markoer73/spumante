# Spumante Repository Best Practices & Agent Guide

This document is the canonical, compact specification of repository behavior, architectural policies, coding standards, and workflow conventions for all AI assistants (Antigravity, Gemini, Claude, Codex/OpenAI, OpenCode, Cline) and human contributors.

> [!IMPORTANT]
> All agent configuration entrypoints (`AGENTS.md`, `GEMINI.md`, `CLAUDE.md`, `CODEX.md`, `.clinerules`, `.cursorrules`) are unified symlinks to this file. Detailed subsystem guides are referenced in [Section 8](#8-deep-dive-reference-index).

---

## 0. Mandatory Pre-Flight Protocol (All Agents, Before Coding)

Every AI assistant MUST complete these five sequential gates before designing, modifying, or creating any code:

### Gate 1: Multi-Agent & Multi-Conversation Task Ownership
- Inspect `git status`, `docs/TASK_QUEUE.md`, and `docs/TASK_LOG.md`.
- **STRICT INVARIANT**: If any file in the working tree or any task in the queue is marked `*-WIP` by another agent—or belongs to another conversation/session—you are **STRICTLY FORBIDDEN** from modifying those files, staging them, committing them, or touching that task. Never assume uncommitted files in the working tree belong to your current conversation.
- Before beginning implementation, record your assigned task as **`[<AGENT_NAME>-WIP YYYY-MM-DD]`** in `docs/TASK_QUEUE.md` and `docs/TASK_LOG.md` (wrap the bracketed badge in inline code backticks to prevent CommonMark link warnings and KaTeX display-math errors).

### Gate 2: The Iron Law of Architecture
- **NO CODE MODIFICATIONS WITHOUT READING THE RELEVANT ARCHITECTURE FIRST.**
- Before touching implementation or test files:
  1. Inspect `docs/skills/ARCHITECTURE.md`.
  2. If touching test harnesses, VM automation, or Wine DLLs, inspect the corresponding subsystem documentation in `docs/skills/`.
  3. Verify compliance with:
     - Strict separation between the **Test Harness & Ground-Truth Oracles** and **Wine Subsystem Implementations**.
     - Clean-room reverse engineering: strictly zero leaked proprietary Windows source code, zero disassembled proprietary binaries, and zero application-specific hacks (`strcmp(app_name, ...)` is forbidden).
  4. If code is written before reading the architecture, revert it and read the documentation first.

### Gate 3: The Iron Law of Test-Driven Development (TDD)
- **NO PRODUCTION CODE WITHOUT A FAILING TEST FIRST.**
- For any API stub or implementation: write a minimal failing differential conformance test expressing desired behavior and edge cases. Verify it fails for the expected reason on Wine before writing implementation code. If code is written before the test, delete it and start over.

### Gate 4: The Iron Law of Centralized Scripts & Surgical Builds/Tests
- **ALL BUILD, VM, HARNESS, AND TEST OPERATIONS MUST USE CENTRALIZED PARAMETRIZED SCRIPTS IN `scripts/` WITH SURGICAL SINGLE-TARGET SCOPE BY DEFAULT.**
- Never create scattered top-level or one-off shell scripts. Use only:
  - `scripts/build.sh` — Surgical compilation of a single conformance test (`--test <name>`), a single Wine DLL (`--dll <name>`), cross-architecture targets (`--arch x86_64|arm64`), patch export (`--export-patch`), or sanitized turnkey builds (`--full-x86`, `--full-arm64`, `--dmg`).
  - `scripts/launch.sh` — Starts/verifies the headless QEMU Windows VM (macOS `hvf`/`tcg`, Linux KVM), VirtIO serial `qemu-ga` (`/tmp/spumante_qga.sock`), QMP (`/tmp/spumante_qmp.sock`), or snapshot restore (`--snapshot clean_ready`).
  - `scripts/stop.sh` — Cleanly stops running VM instances, guest agent sockets, or ephemeral Wine prefixes (`--vm`, `--wine`, `--all`).
  - `scripts/test.sh` — Runs surgical differential tests (`--dll <name> --test <name> --filter <case>`), upstream winetests (`--winetest`), security audits (`--security`), stub discovery (`--discover-stubs`), clean-room specs (`--fetch-spec`), style linting (`--lint`), or CI verification (`--ci`).
- **No Full-Tree Rebuild Waste**: AI coding agents are **STRICTLY FORBIDDEN** from rebuilding the entire Wine tree or running 1,000+ tests to verify a single function. Build and execute only the specific DLL and test binary under work.

### Gate 5: The Iron Law of Marcommit
- **NEVER AUTOMATICALLY COMMIT CODE WITHOUT EXPLICIT USER INSTRUCTION.**
- Do not run `git commit` after completing tasks unless explicitly instructed by the user. Leave modified files in the working tree for user review. Follow the workflow in [Section 7](#7-commit-hygiene--the-marcommit-workflow) when requested to commit.

---

## 1. Mission, Legal Invariants & Upstream Wine Layering

- **Goal**: Verifiable feature and user experience (UX) parity between Wine and modern Windows APIs through closed-loop automated testing and clean-room implementation.
- **Black-Box Behavioral Reproduction (Not Reverse Engineering)**:
  - Derive implementations exclusively from public specifications (Microsoft Learn / MSDN), public Wine headers, and black-box differential test behavior on genuine Windows VMs.
  - Zero exposure to leaked Windows source code (Windows Research Kernel, leaked NT/2000/XP trees) or proprietary decompilations.
- **Anti-Regression Unit Testing**: Maintain unit, differential, and winetest suites for pre-existing functionality and new APIs to protect against AI refactoring regressions.
- **Target Platform Hierarchy**: **macOS first** (Apple Silicon Mx & Intel x86_64; Metal graphics, VideoToolbox/CoreMedia hardware codecs, CommonCrypto/Security.framework, Darwin/Mach `__ulock_wait2` synchronization, Apple Neural Engine via Core ML), **Linux second** (Vulkan, VA-API, AVX-512/NEON).
- **No Application-Specific Hacks**: Fix generic subsystem behavior and API edge cases. Never branch on executable names (e.g., `if (strcmp(app_name, "photoshop.exe") == 0)`).

### Upstream Wine Layering, Diffs & Patches (`patches/`)
- **Layered "On Top" of Wine Upstream**: Spumante is constituted on top of upstream Wine (`gitlab.winehq.org/wine/wine.git`). Upstream Wine is strictly read-only (`.wine-upstream/` with push locked to `DISABLE_PUSH_READ_ONLY_UPSTREAM`). Never push to or commit into upstream Wine directly.
- **Patching via Unified Diffs (`patches/`)**: When direct changes to upstream Wine code outside our local DLL overlays (`dlls/`) are the best course of action to improve Spumante, maintain them as clean, unified `diff` patches in `patches/` (generated/managed via `./scripts/build.sh --export-patch` or Git diffs). This allows any developer to apply Spumante cleanly on top of specific Wine releases.
- **Re-evaluation on Upstream Updates**: Whenever upstream Wine code updates, re-evaluate, rebase, and test all existing diffs/patches in `patches/` against the new upstream commit to detect conflicts or obsoleted work.
- **Upstream Contribution Readiness & Human Reviewability**: Upstream Wine maintains strict policies against LLM-generated code due to provenance and licensing requirements. To allow contributing bug fixes upstream when valuable:
  - Keep diffs/patches short, clean, self-contained, and circumstantiated (minimal targeted bug fixes).
  - Ensure every patch is easily human-reviewable and strictly separable from Spumante-specific macOS enhancements, without compromising the Spumante user experience.

---

## 2. Architecture & Subsystem Policies

- **Differential Test Harness**: Headless genuine Windows 10/11 VM (via QEMU `hvf`/`tcg` on macOS, KVM on Linux) communicating over host-local VirtIO serial `qemu-ga` Unix socket (`/tmp/spumante_qga.sock`) with QMP snapshot resets (`/tmp/spumante_qmp.sock`) and Ground-Truth JSON caching (`tests/ground_truth/<dll>/<test>.json`) compared against Wine.
- **Dual-Compilation & Dual-Architecture Model**: Tests compile cleanly with `x86_64-w64-mingw32-gcc`, `aarch64-w64-mingw32-gcc`, and Wine's `winetest` runner using `tests/include/parity_test.h` and `tests/include/wine/test.h`.
- **Hybrid Wine DLL & `wine-borrow` Sparse-Overlay Workflow**:
  - Target DLL sources live in `dlls/<dll_name>/` and `patches/`, tracked in `upstream/manifest.json`.
  - Upstream Wine is fetched on demand into the git-ignored blobless sparse cache `.wine-upstream/`.
  - Use `./scripts/build.sh --borrow <dll>` and `./scripts/test.sh --check-upstream --dll <dll>` for surgical 3-way backports preserving `#ifdef __APPLE__` blocks.
- **Wineserver vs. Client DLL Separation**:
  - Stateless APIs or local buffer manipulation belong in client DLLs (`ntdll`, `kernelbase`, `user32`, etc.).
  - Cross-process state, handles, tokens, and synchronization MUST communicate via `wineserver` protocol IPC definitions. Never invent ad-hoc global IPC mechanisms.
- **Standardized Error Semantics**: Replicate authentic Windows `GetLastError()` and `NTSTATUS` error codes for invalid parameters, buffer overflows, and access violations.

---

## 3. Test-Driven Development (TDD) & Centralized Script Workflow

Apply the `test-driven-development` skill for all new implementations, bug fixes, and behavior changes:

- **Red-Green-Refactor Cycle**:
  1. **RED**: Write a minimal failing conformance test in `tests/conformance/<dll>/test_<name>.c`. Surgically build it (`./scripts/build.sh --dll <dll> --test <name>`) and run it (`./scripts/test.sh --dll <dll> --test <name>`) to confirm failure on Wine.
  2. **GREEN**: Write minimal C code in `dlls/<dll>/` to satisfy the test. Rebuild only that DLL (`./scripts/build.sh --dll <dll>`) and verify differential match with Windows ground truth.
  3. **REFACTOR**: Clean up design, verify WineHQ style (`./scripts/test.sh --lint`), run 4-gate security (`./scripts/test.sh --security --dll <dll> --test <name>`), and run upstream winetest (`./scripts/test.sh --winetest --dll <dll>`).
- **Hermetic & Fast**: Conformance tests must execute deterministically and quickly without network dependencies.

### Canonical Script Commands Quick Reference
```sh
# 1. Surgical DLL & Conformance Test Build
./scripts/build.sh --dll <name> [--test <name>] [--arch x86_64|arm64]

# 2. Upstream Wine Borrow & 3-Way Backport
./scripts/build.sh --borrow <dll> [--dry-run]

# 3. Export Unified Upstream Patch
./scripts/build.sh --dll <name> --export-patch

# 4. Turnkey Full Builds (Sanctioned full-tree modes)
./scripts/build.sh --full-x86     # Intel & Rosetta 2 x86_64/i386 build
./scripts/build.sh --full-arm64   # Native Apple Silicon ARM64/ARM64EC build
./scripts/build.sh --dmg          # macOS DMG installer package (add -nz to sign/notarize)

# 5. Differential Conformance Test Execution
./scripts/test.sh --dll <name> --test <name> [--filter <case>]

# 6. Upstream Wine winetest & A/B Anti-Regression
./scripts/test.sh --winetest --dll <name>

# 7. 4-Gate Security Audit & Static Verification
./scripts/test.sh --security --dll <name> --test <name>

# 8. Style, Markdown & Canonical Boundary Linter
./scripts/test.sh --lint

# 9. IDE & TestMate Visibility Verification
./scripts/build.sh --ide-tests && ./scripts/test.sh --ide-tests

# 10. Headless Windows Ground-Truth VM Lifecycle
./scripts/launch.sh               # Start VM & guest agent
./scripts/stop.sh --vm            # Stop VM daemon
```

### Mandatory Antigravity IDE & TestMate Visibility Constraint
Every test in the repository—including all C differential conformance suites (`tests/conformance/<dll>/test_*.c`), upstream Wine `winetest` suites (`tests/winetest/<dll>/winetest_*.c`), and Python unit tests (`tests/unit/test_*.py`)—MUST be visible, discoverable, and runnable inside **Antigravity IDE** via **C++ TestMate** (`--gtest_list_tests` / `--gtest_filter` / `--gtest_output=xml:` configured in `.vscode/settings.json`) and the **Python Test Explorer** (`unittest`). Run `./scripts/build.sh --ide-tests` and `./scripts/test.sh --ide-tests` (enforced by `./scripts/test.sh --lint`) whenever adding or modifying tests.

---

## 4. Code Style & System Conventions

- **Pure C Standards**: Wine implementations are written in standard C (C99/C11). Avoid compiler-specific extensions unless guarded by standard macros.
- **Formatting**: 4-space indentation, no tabs, K&R braces, `lower_snake_case` filenames, and Wine naming conventions (`WINAPI`, `NTSTATUS`, `BOOL`).
- **Explicit Types**: Use Win32 standard types (`DWORD`, `ULONG_PTR`, `HANDLE`, `BYTE`) for external signatures and standard fixed-width integers (`uint32_t`, `size_t`) for internal POSIX helpers.
- **File Length Limit**: Keep source files modular and under ~2000 lines. Break oversized files into helper units.
- **Defensive Memory Management**: Validate input pointers, buffer sizes, and alignment. Check for integer overflows during size calculations.

---

## 5. Header Conventions & Licensing

- **License Headers**: Every source and header file must include a conformant open-source license header (LGPL-2.1-or-later for Wine DLLs/tests; Apache-2.0 for Spumante launcher/scripts/tools).
- **Header Extension Policy**: C headers use `.h`. C++ helper headers use `.hpp`.
- **Include Guards**: Use standard `#ifndef _WINE_<MODULE>_H` / `#define _WINE_<MODULE>_H`. **Never use `#pragma once`**.

---

## 6. Centralized Debug Logging

- **NEVER use raw `printf` / `fprintf(stderr, ...)` / `std::cerr` in Wine subsystems!** Always use Wine logging macros:
  - `TRACE(...)`: Verbose tracing of function entry, parameters, and return values.
  - `WARN(...)`: Unexpected parameters or non-fatal anomalies.
  - `FIXME(...)`: Unimplemented stubs or known missing behaviors.
  - `ERR(...)`: Fatal conditions or critical internal failures.
- **Log Channels**: Declare debug channels using `WINE_DEFAULT_DEBUG_CHANNEL(...)` or `WINE_DECLARE_DEBUG_CHANNEL(...)`.
- **Inner Loops**: Never log in high-frequency inner loops (audio streams, raster loops, spinlocks) unless guarded by verbose trace flags.

---

## 7. Commit Hygiene & The `marcommit` Workflow

- **NEVER Automatically Commit**: Do not run `git commit` after completing tasks unless explicitly instructed by the user. Leave modified files in the working tree for user review.
- **The `marcommit` Workflow**: When explicitly instructed to commit ("marcommit", "commit changes", etc.):
  1. **Strict Conversation & Task Scope**: Stage ONLY files you authored or modified for the specific task requested in the CURRENT conversation. NEVER stage or commit changes made in another conversation or session.
  2. **No Broad Staging Commands (Mandatory)**: NEVER run `git add .`, `git add -A`, or `git add -u`. Always explicitly specify each individual file path (`git add <file1> <file2>`). Before committing, run `git diff --cached --name-only` to verify zero unrelated files are staged. Unstage foreign files immediately (`git restore --staged <file>`).
  3. **Single Task per Commit**: Each commit must encompass strictly ONE logical task. Never bundle multiple tasks.
  4. **Mandatory Documentation & Task Updates**: In `docs/TASK_QUEUE.md` and `docs/TASK_LOG.md`, update and complete ONLY the single task assigned to the current conversation. Strictly remove completed tasks from `docs/TASK_LEFT_SUMMARY.md`. All markdown entries must wrap bracketed badges in inline code backticks (`` **`[...]`** ``) to eliminate broken reference link warnings and KaTeX errors.
  5. **README Updates**: Update `README.md` if user-visible surface, CLI commands, or configuration changed.
  6. **Branch Reconciliation**: If you branched, reconcile to `main` and delete the branch.
  7. **Mandatory `Co-authored-by:` Trailer**: Append the trailer after a blank line:
     - Antigravity: `Co-authored-by: Antigravity <antigravity@google.com>`
     - Gemini: `Co-authored-by: Gemini <gemini@google.com>`
     - Claude: `Co-authored-by: Claude <claude@anthropic.com>`
     - Codex / OpenAI: `Co-authored-by: Codex <codex@openai.com>`
     - OpenCode: `Co-authored-by: OpenCode using <Model Name> <opencode@anomalyco.com>`
  8. **Never Commit**: Proprietary binaries, Windows OS ISOs/ROMs, credentials, secrets, VM image snapshots, build trees (`build/`, `builds/`), or local configs.

---

## 8. Deep-Dive Reference Index

| Topic | Authority Document |
| :--- | :--- |
| **Developer & Architecture Guide** | [`DEVELOPER_GUIDE.md`](DEVELOPER_GUIDE.md) |
| **System Architecture & Harness** | [`docs/skills/ARCHITECTURE.md`](docs/skills/ARCHITECTURE.md) |
| **Upstream Wine Borrow & Alignment** | [`docs/skills/WINE_BORROW.md`](docs/skills/WINE_BORROW.md), [`.agents/skills/wine-borrow/SKILL.md`](.agents/skills/wine-borrow/SKILL.md) |
| **Security Audit & Commit Scanning** | [`docs/skills/SECURITY_AUDIT.md`](docs/skills/SECURITY_AUDIT.md), [`.agents/skills/security-audit/SKILL.md`](.agents/skills/security-audit/SKILL.md) |
| **User Config, Native macOS UI & Containment** | [`docs/skills/USER_CONFIG_AND_CONTAINMENT.md`](docs/skills/USER_CONFIG_AND_CONTAINMENT.md), [`.agents/skills/user-config-containment/SKILL.md`](.agents/skills/user-config-containment/SKILL.md) |
| **Test-Driven Development (TDD)** | [`docs/skills/test-driven-development.md`](docs/skills/test-driven-development.md), [`docs/skills/TEST_RULES.md`](docs/skills/TEST_RULES.md) |
| **Testing Anti-Patterns** | [`docs/skills/testing-anti-patterns.md`](docs/skills/testing-anti-patterns.md) |
| **Task Coordination & Tracking** | [`docs/skills/TASK_COORDINATION.md`](docs/skills/TASK_COORDINATION.md) |
| **Commit Guidelines & Marcommit** | [`docs/skills/COMMIT_GUIDELINES.md`](docs/skills/COMMIT_GUIDELINES.md) |
| **C Systems Code Style** | [`docs/skills/CODE_STYLE.md`](docs/skills/CODE_STYLE.md) |
| **Debug Logging Standards** | [`docs/skills/DEBUG_LOGGING.md`](docs/skills/DEBUG_LOGGING.md) |
| **Documentation Standards** | [`docs/skills/DOCUMENTATION_STANDARD.md`](docs/skills/DOCUMENTATION_STANDARD.md) |
| **Threat & Security Model** | [`docs/skills/THREAT_SECURITY_MODEL.md`](docs/skills/THREAT_SECURITY_MODEL.md) |

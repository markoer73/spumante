# Wine Parity Repository Best Practices & Agent Guide

This document is the canonical, succinct specification of repository behavior, architectural policies, coding standards, and workflow conventions for all AI assistants (Antigravity, Gemini, Claude, Codex/OpenAI, OpenCode, Cline) and human contributors.

> [!IMPORTANT]
> All agent configuration entrypoints (`AGENTS.md`, `GEMINI.md`, `CLAUDE.md`, `CODEX.md`, `.clinerules`, `.cursorrules`) are unified symlinks to this file. Detailed subsystem guides are referenced in [Section 10](#10-deep-dive-reference-index).

---

## 0. Mandatory Pre-Flight Protocol & Iron Laws (All Agents, Before Coding)

Every AI assistant (Antigravity, Gemini, Claude, Codex, OpenCode, Cline) MUST complete these four sequential gates before designing, modifying, or creating any code:

### Gate 1: Multi-Agent & Multi-Conversation Task Ownership & Collision Avoidance

- Inspect `git status`, `docs/TASK_QUEUE.md`, and `docs/TASK_LOG.md`.
- **STRICT INVARIANT (Multi-Agent & Multi-Conversation)**: If any file in the working tree or any task in the queue is currently in progress or marked `*-WIP` by another agent—or belongs to another conversation/session—you are **STRICTLY FORBIDDEN** from modifying those files, staging them, committing them, or touching that task.
- Isolation applies across both different agents AND different conversation threads of the same agent. Never assume uncommitted files in the working tree belong to your current conversation.
- Before beginning implementation, record your assigned task as `\[<AGENT_NAME>-WIP\]` with a timestamp in `docs/TASK_QUEUE.md` and `docs/TASK_LOG.md` (ensuring square brackets are escaped to comply with Markdown specifications).

### Gate 2: The Iron Law of Architecture

- **THE IRON LAW**: `NO CODE MODIFICATIONS WITHOUT READING THE RELEVANT ARCHITECTURE FIRST`.
- Before touching any implementation or test files:
  1. Use your file reading tool to inspect `docs/skills/ARCHITECTURE.md`.
  2. If touching test harnesses, KVM/QEMU automation, or Wine DLL implementations, inspect the corresponding subsystem documentation.
  3. Verify that your planned implementation complies with:
     - The strict separation between the **Test Harness & Ground-Truth Oracles** and the **Wine Subsystem Implementations**.
     - Clean-room reverse engineering: strictly zero leaked proprietary Windows source code, zero disassembled proprietary binaries, and zero application-specific hacks.
  4. If code is written before reading the architecture, revert it and read the documentation first.

### Gate 3: The Iron Law of Test-Driven Development (TDD)

- **THE IRON LAW**: `NO PRODUCTION CODE WITHOUT A FAILING TEST FIRST`.
- For any API stub or implementation: write a minimal failing differential conformance test expressing desired behavior and edge cases. Verify it fails for the expected reason on Wine before writing any implementation code.
- If you write implementation code before the test, delete it and start over.

### Gate 4: The Iron Law of Centralized Scripts & Surgical Builds/Tests

- **THE IRON LAW**: `ALL BUILD, VM, HARNESS, AND TEST OPERATIONS MUST USE CENTRALIZED PARAMETRIZED SCRIPTS IN scripts/ WITH SURGICAL SINGLE-TARGET SCOPE BY DEFAULT`.
- **Anti-Proliferation Invariant**: Never create scattered top-level or one-off shell scripts. All developer and agent entrypoints are strictly centralized into four canonical, parametrized shell scripts in `scripts/` (backed by internal modules in `scripts/lib/`):
  - `scripts/build.sh` — Surgical compilation of a single conformance test (`--test <name>`), a single Wine DLL (`--dll <name>`), or cross-architecture targets (`--arch x86_64|arm64`). Refuses full-tree rebuilds unless `--all` is explicitly passed.
  - `scripts/launch.sh` — Starts and verifies the headless QEMU Windows VM (macOS `hvf`/`tcg` or KVM), QMP socket, and QEMU Guest Agent (`qemu-ga` VirtIO serial socket), or warms snapshots (`--snapshot clean_ready`) with strong pre-flight checks and fallbacks.
  - `scripts/stop.sh` — Cleanly stops or resets running QEMU VM instances, guest agent sockets, or ephemeral Wine prefixes (`--vm`, `--wine`, `--all`).
  - `scripts/test.sh` — Runs surgical differential tests (`--dll <name> --test <name> --filter <case>`), stub discovery (`--discover-stubs`), clean-room spec fetching (`--fetch-spec`), style linting (`--lint`), or CI verification (`--ci`) without rebuilding unrelated targets.
- **No Full-Tree Rebuild Waste**: AI coding agents are **STRICTLY FORBIDDEN** from rebuilding the entire Wine tree or 1,000+ tests to verify a single function or test case. Always build and execute only the specific DLL and test binary under work.

### Gate 5: The Iron Law of Marcommit

- **THE IRON LAW**: `NEVER AUTOMATICALLY COMMIT CODE WITHOUT EXPLICIT USER INSTRUCTION`.
- Do not run `git commit` after completing tasks unless explicitly instructed by the user. Leave modified files in the working tree for user review.
- When explicitly instructed to commit ("marcommit", "commit changes", etc.):
  1. **Strict Conversation & Task Scope**: Stage ONLY files and hunks that you personally created or modified for the specific task requested in the CURRENT conversation. NEVER stage or commit changes made in another conversation or session, even if made by the same agent identity.
  2. **No Broad Staging Commands (Mandatory)**: NEVER run `git add .`, `git add -A`, or `git add -u`. Always explicitly specify each individual file path (`git add <file1> <file2>`). Before committing, run `git diff --cached --name-only` to verify that strictly zero files outside your active task are staged. Unstage any foreign files immediately (`git restore --staged <file>`).
  3. **Single Task per Commit**: Each commit must encompass strictly ONE logical task. Never bundle multiple tasks together into a single commit unless the user explicitly requested bundling those specific tasks.
  4. **Mandatory Documentation & Task Updates**: Create all corresponding documentation for YOUR task. In `docs/TASK_QUEUE.md` and `docs/TASK_LOG.md`, update and complete ONLY the single task assigned to the current conversation. Strictly remove completed tasks from `docs/TASK_LEFT_SUMMARY.md` (which must exclusively list remaining uncompleted tasks) in the same patch. All markdown files authored or updated must strictly fulfill Markdown specifications properly, especially escaping square brackets (`\[...\]` on status badges, tags, and array/range notations) to eliminate broken reference link warnings. NEVER touch or complete entries for other tasks whose files happen to be in the working tree.
  5. **README Updates**: Describe tasks concisely in `README.md` if required (e.g. if user-visible surface, controls, CLI, or features changed).
  6. **Branch Reconciliation**: If you have branched, reconcile the branch to `main` and delete the branch.
  7. **Mandatory `Co-authored-by:` Trailer**: Introspect your actual agent identity and append the trailer after a blank line:
     - Antigravity: `Co-authored-by: Antigravity <antigravity@google.com>`
     - Gemini: `Co-authored-by: Gemini <gemini@google.com>`
     - Claude: `Co-authored-by: Claude <claude@anthropic.com>`
     - Codex / OpenAI: `Co-authored-by: Codex <codex@openai.com>`
     - OpenCode: `Co-authored-by: OpenCode using <Model Name> <opencode@anomalyco.com>`
  8. **Never Commit**: Proprietary binaries, Windows OS ISOs/ROMs, credentials, secrets, VM image snapshots, build trees (`build/`, `builds/`), or local configs.

---

## 1. Mission & Non-Negotiable Legal Invariants

- **Goal**: Achieve verifiable feature and user experience (UX) parity between Wine and modern Windows APIs through closed-loop automated testing and clean-room implementation.
- **Black-Box Behavioral Reproduction (Not Reverse Engineering)**: 
  - AI coding agents are explicitly tasked with reproducing user experience, observable runtime semantics, and feature/UX parity. Agents are never tasked with reverse engineering Windows internals.
  - Strictly **zero** exposure to leaked Windows source code (Windows Research Kernel, leaked NT/2000/XP trees) or proprietary decompilations (IDA Pro, Ghidra).
  - Implementations must derive exclusively from public specifications (Microsoft Learn / MSDN), public Wine headers, and black-box differential test behavior observed on genuine Windows VMs.
- **Anti-Regression Unit Testing**: Because AI models iterate and refactor rapidly, unit and differential tests must be established for all pre-existing functionalities in target DLLs as well as new APIs to prevent regressions.
- **Target Platform Hierarchy**: **macOS first** (Apple Silicon & x86_64, native Apple frameworks: `CommonCrypto`/`Security.framework`, `CoreFoundation`, `VideoToolbox`/`CoreMedia`, Metal, Darwin/Mach primitives) as the primary focus and differentiator, followed by **Linux second**.
- **Hardware Acceleration Exploitation (Explicit Target)**: Actively exploit host hardware acceleration capabilities where available, prioritizing Apple Silicon Mx processors on macOS (Metal graphics/compute, Apple Neural Engine via Core ML, VideoToolbox hardware codecs, Accelerate framework, unified memory) as primary, and Linux acceleration (Vulkan, VA-API, AVX-512/NEON) second.
- **No Application-Specific Hacks**: Fix generic subsystem behavior and API edge cases. Never branch on executable names (e.g. `if (strcmp(app_name, "photoshop.exe") == 0)`).

---

## 2. Architecture & Subsystem Policies

- **Differential Test Harness**: A ground-truth oracle running a genuine Windows 10/11 VM (via QEMU `hvf`/`tcg` on macOS or KVM on Linux, communicating over a host-local VirtIO serial `qemu-ga` Unix socket with QMP `loadvm` snapshot resets and Ground-Truth JSON caching) side-by-side with an isolated Wine execution environment.
- **Dual-Compilation & Dual-Architecture Model**: Conformance tests compile cleanly under both `x86_64-w64-mingw32-gcc` and `aarch64-w64-mingw32-gcc` as well as Wine's `winetest` runner using the dual-mode `tests/include/parity_test.h` header.
- **Hybrid Wine DLL & `wine-borrow` Sparse-Overlay Workflow (Strictly Read-Only Upstream)**: Target DLL sources and patches live in our own standalone `wine-parity` repository (`dlls/<dll_name>/` and `patches/`), tracked against upstream Wine via `upstream/manifest.json` (`"upstream_access_mode": "read-only"`) and a git-ignored blobless sparse checkout (`.wine-upstream/` with push locked to `DISABLE_PUSH_READ_ONLY_UPSTREAM`). Never touch or push to upstream Wine, and never duplicate the full upstream Wine tree in git; use the `wine-borrow` skill (`./scripts/build.sh --borrow <dll>` and `./scripts/test.sh --check-upstream --dll <dll>`) for surgical 3-way backports preserving `#ifdef __APPLE__` acceleration.
- **Wineserver vs. Client DLL Separation**:
  - Stateless APIs or local buffer manipulations belong in client DLLs (`ntdll`, `kernelbase`, `user32`, etc.).
  - Cross-process state, handles, security tokens, and synchronization objects MUST communicate via `wineserver` protocol IPC definitions. Never invent ad-hoc global IPC mechanisms.
- **Standardized Error Semantics**: Functions must replicate genuine Windows `GetLastError()` and `NTSTATUS` error codes for invalid parameters, buffer overflows, and access violations.

---

## 3. Test-Driven Development (TDD) & Centralized Script Workflow

Apply the `test-driven-development` skill for all new API implementations, bug fixes, and behavior changes:

- **The Iron Law**: `NO PRODUCTION CODE WITHOUT A FAILING TEST FIRST`. If you write code before the test, delete it and start over.
- **Red-Green-Refactor Cycle**:
  1. **RED**: Write a minimal failing conformance test targeting the missing API or edge case. Build it surgically (`./scripts/build.sh --dll <dll> --test <test>`) and run it (`./scripts/test.sh --dll <dll> --test <test>`) against the Windows ground truth and Wine to confirm the failure.
  2. **GREEN**: Write the minimal C code necessary to satisfy the test, rebuild only that single DLL (`./scripts/build.sh --dll <dll>`), and match the Windows ground-truth log.
  3. **REFACTOR**: Clean up design, remove duplication, and verify WineHQ coding standards (`./scripts/test.sh --lint`) and 4-gate security (`./scripts/test.sh --security`) while keeping tests green.
- **Hermetic & Fast**: Test targets must run quickly and deterministically without network dependencies.
- **Surgical Single-Target Execution**: Always use `./scripts/build.sh` and `./scripts/test.sh` with `--dll` / `--test` / `--filter` parameters. Never rebuild or run unrelated DLLs or test suites during iterative development.

---

## 4. Code Style & System Conventions

- **Pure C Standards**: Wine implementations are written in standard C (C99/C11). Avoid compiler-specific extensions unless guarded by standard macros.
- **Formatting**: 4-space indentation, no tabs, K&R braces, `lower_snake_case` filenames and Wine naming conventions (`WINAPI`, `NTSTATUS`, `BOOL`).
- **Explicit Types**: Use Win32 standard types (`DWORD`, `ULONG_PTR`, `HANDLE`, `BYTE`) for external signatures and standard fixed-width integers (`uint32_t`, `size_t`) for internal POSIX helpers.
- **File Length Limit**: Keep source files modular and under ~2000 lines. Break oversized files into helper units.
- **Defensive Memory Management**: Always validate input pointers, buffer sizes, and alignment. Check for integer overflows during size calculations.

---

## 5. Header Conventions & Licensing

- **License Headers**: Every source and header file must include a conformant open-source license header (LGPL 2.1+ / Apache 2.0 / MIT as applicable to the subsystem).
- **Header Extension Policy**: C headers must use `.h`. If C++ helper tools or orchestrators are used, use `.hpp` for C++ headers.
- **Include Guards**: Use standard `#ifndef _WINE_<MODULE>_H` / `#define _WINE_<MODULE>_H`. **Never use `#pragma once`**.

---

## 6. Centralized Debug Logging

- **NEVER use raw `printf` / `fprintf(stderr, ...)` / `std::cerr` in Wine subsystems!** Always use Wine logging macros:
  - `TRACE(...)`: Verbose tracing of function entry, parameters, and return values.
  - `WARN(...)`: Unexpected parameters or non-fatal anomalies.
  - `FIXME(...)`: Unimplemented stubs or known missing behaviors.
  - `ERR(...)`: Fatal conditions or critical internal failures.
- **Log Channels**: Declare appropriate debug channels using `WINE_DEFAULT_DEBUG_CHANNEL(...)` or `WINE_DECLARE_DEBUG_CHANNEL(...)`.
- **Inner Loops**: Never log in high-frequency inner loops (audio streams, raster loops, spinlocks) unless guarded by verbose trace flags.

---

## 7. Commit Hygiene & The `marcommit` Workflow

- **NEVER Automatically Commit**: Do not run `git commit` after completing tasks unless explicitly instructed by the user. Leave modified files in the working tree for user review.
- **The `marcommit` Workflow**: When requested to commit, follow strict hygiene:
  1. Stage ONLY files you authored for the single active task of the CURRENT conversation (`git add <file1> <file2>`). NEVER run `git add .` or `git add -A`.
  2. Single task per commit: Each commit must represent strictly ONE logical change.
  3. Mandatory documentation & task updates: Update `docs/TASK_QUEUE.md` and `docs/TASK_LOG.md`, and strictly remove completed tasks from `docs/TASK_LEFT_SUMMARY.md`. All markdown entries must properly escape square brackets (`\[...\]`).
  4. Include the mandatory `Co-authored-by:` trailer identifying your agent identity.
  5. Never commit credentials, VM disks, build artifacts, or proprietary binaries.

---

## 8. Deep-Dive Reference Index

| Topic | Authority Document |
| :--- | :--- |
| **System Architecture & Harness** | [`docs/skills/ARCHITECTURE.md`](docs/skills/ARCHITECTURE.md) |
| **Upstream Wine Borrow & Alignment** | [`docs/skills/WINE_BORROW.md`](docs/skills/WINE_BORROW.md), [`.agents/skills/wine-borrow/SKILL.md`](.agents/skills/wine-borrow/SKILL.md) |
| **Security Audit & Commit Scanning** | [`docs/skills/SECURITY_AUDIT.md`](docs/skills/SECURITY_AUDIT.md), [`.agents/skills/security-audit/SKILL.md`](.agents/skills/security-audit/SKILL.md) |
| **Test-Driven Development (TDD)** | [`docs/skills/test-driven-development.md`](docs/skills/test-driven-development.md), [`docs/skills/TEST_RULES.md`](docs/skills/TEST_RULES.md) |
| **Testing Anti-Patterns** | [`docs/skills/testing-anti-patterns.md`](docs/skills/testing-anti-patterns.md) |
| **Task Coordination & Tracking** | [`docs/skills/TASK_COORDINATION.md`](docs/skills/TASK_COORDINATION.md) |
| **Commit Guidelines & Marcommit** | [`docs/skills/COMMIT_GUIDELINES.md`](docs/skills/COMMIT_GUIDELINES.md) |
| **C Systems Code Style** | [`docs/skills/CODE_STYLE.md`](docs/skills/CODE_STYLE.md) |
| **Debug Logging Standards** | [`docs/skills/DEBUG_LOGGING.md`](docs/skills/DEBUG_LOGGING.md) |
| **Documentation Standards** | [`docs/skills/DOCUMENTATION_STANDARD.md`](docs/skills/DOCUMENTATION_STANDARD.md) |
| **Threat & Security Model** | [`docs/skills/THREAT_SECURITY_MODEL.md`](docs/skills/THREAT_SECURITY_MODEL.md) |

---
trigger: model_decision
description: Use this whenever running or modifying tests for the project, or working with differential test harnesses.
---

# Testing Rules, Centralized Scripts & Surgical Build Policy

The authoritative testing rules and policies are documented in `docs/skills/TEST_RULES.md`.
Please read and follow the instructions in [docs/skills/TEST_RULES.md](../../docs/skills/TEST_RULES.md) when building or working with tests:

1. **Centralized Entrypoints Only**: Use ONLY `scripts/build.sh`, `scripts/launch.sh`, `scripts/stop.sh`, and `scripts/test.sh` (backed by internal helpers in `scripts/lib/`). Never proliferate extra top-level or scattered scripts.
2. **Surgical Single-Target Scope**: Always pass `--dll <name>` and/or `--test <name>` (and `--filter <pattern>`) to build and verify only the target under work. Never rebuild the full project or run 1,000+ tests to verify an isolated change.
3. **Antigravity IDE & TestMate Visibility (Mandatory Constraint)**: Every C conformance test (`tests/conformance/<dll>/test_*.c`), upstream `winetest` suite (`tests/winetest/<dll>/winetest_*.c`), and Python unit test (`tests/unit/test_*.py`) MUST remain visible and runnable in **Antigravity IDE** via **C++ TestMate** (`.vscode/settings.json`, `--gtest_list_tests`) and Python `unittest`. Verify with `./scripts/build.sh --ide-tests` and `./scripts/test.sh --ide-tests`.


# Testing Anti-Patterns

This document identifies common testing pitfalls and anti-patterns that undermine reliability in systems programming and differential emulation.

---

## 1. Testing Mocks Instead of Reality (The Mock Trap)

- **Anti-Pattern:** Mocking out underlying operating system syscalls or memory managers and asserting that the mock was called.
- **Why It Fails:** Wine's purpose is to emulate real OS behaviors. Mocking POSIX calls hides kernel-level edge cases, memory page protections, and threading races.
- **Rule:** Test real behavior. In differential testing, compile the exact same C test binary and run it against genuine Windows and Wine.

---

## 2. Brittle or Non-Hermetic State

- **Anti-Pattern:** Relying on existing files in `C:\Windows`, pre-existing registry keys, or ambient user profile state.
- **Why It Fails:** Tests pass on one machine and fail on another, or fail when run in a different order.
- **Rule:** Every test must create its own temporary test keys, temporary files, and isolated state, cleaning them up in a `teardown()` routine.

---

## 3. Overspecified Implementation Tests

- **Anti-Pattern:** Asserting exact internal memory layouts, private structure fields, or intermediate internal function calls.
- **Why It Fails:** Prevents refactoring and creates false alarms when internal implementation changes without changing the external API contract.
- **Rule:** Test the public API contract: parameters, return codes, output buffers, side effects, and `GetLastError()` / `NTSTATUS`.

---

## 4. Broad Test Suite Rebuilds During Development

- **Anti-Pattern:** Running the entire global test suite for every minor edit.
- **Why It Fails:** Wastes minutes of compute time per iteration and breaks the fast TDD feedback loop.
- **Rule:** Always use targeted test execution (`scripts/run_differential_test.py --test <target>`). Run full regressions only before final PR / patch generation.

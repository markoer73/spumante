---
name: test-driven-development
description: Use when implementing any API, feature, or bugfix, before writing implementation code
---

# Test-Driven Development (TDD)

## Overview

Write the test first. Watch it fail. Write minimal code to pass.

**Core principle:** If you didn't watch the test fail, you don't know if it tests the right thing.

**Violating the letter of the rules is violating the spirit of the rules.**

## When to Use

**Always:**
- New API implementations or stubs
- Bug fixes and error-code alignment
- Refactoring internal subsystem logic
- Behavior changes

**Exceptions (ask your human partner):**
- Throwaway exploratory scripts
- Generated boilerplate bindings
- Static markdown documentation

Thinking "skip TDD just this once"? Stop. That's rationalization.

## The Iron Law

```
NO PRODUCTION CODE WITHOUT A FAILING TEST FIRST
```

Write code before the test? Delete it. Start over.

**No exceptions:**
- Don't keep it as "reference"
- Don't "adapt" it while writing tests
- Don't look at it
- Delete means delete

Implement fresh from tests. Period.

## Red-Green-Refactor

```
[RED: Write failing test] ──> [Verify fails for expected reason]
                                              │
                                              ▼
[REFACTOR: Clean design] <── [Verify all green] <── [GREEN: Write minimal code]
```

### 1. RED - Write Failing Test
- Write a minimal test targeting the specific API or edge case.
- In differential development:
  1. Compile and execute on the genuine Windows VM. Verify that the test runs and passes, generating the ground-truth expectation log.
  2. Compile and execute against Wine. Verify that the test **fails** (e.g. returns `ERROR_CALL_NOT_IMPLEMENTED` or wrong error code).
- Confirm the failure mode: If the test crashes with an unexpected segmentation fault instead of the target failure, fix the test harness first.

### 2. GREEN - Minimal Implementation
- Write strictly the minimal C code necessary to satisfy the test.
- Do not write speculative code for un-tested edge cases. If another edge case is needed, write another test first.

### 3. REFACTOR - Clean Up
- Clean up the code according to repository code style and Wine conventions.
- Remove duplicate logic, ensure proper memory release and lock management.
- Re-run the test to guarantee everything remains green.

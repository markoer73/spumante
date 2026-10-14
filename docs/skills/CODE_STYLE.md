# Code Style & Implementation Standards

This document defines code style, naming conventions, and programming practices for all C and C++ source code in this repository.

---

## 1. Language Standards & Portability

- **Wine Implementations**: Standard C (C99/C11). Avoid proprietary MSVC or GNU compiler extensions in core logic unless guarded by standard compatibility macros.
- **Harness & Tooling**: Python 3.11+ or portable standard C++20 (`CMAKE_CXX_EXTENSIONS OFF`).
- **Encapsulation**: Group related subsystem helpers into dedicated static translation units. Avoid sprawling global variables; maintain state within explicit context structs.
- **File Length Limit**: Keep source files under ~2000 lines. Break oversized files into modular sub-units.

---

## 2. Formatting & Indentation

- **Indentation**: 4 spaces per indentation level. Strictly **no tabs**.
- **Bracing Style**: K&R style (opening brace on the same line as the statement, closing brace aligned with the statement):
  ```c
  if (status != STATUS_SUCCESS) {
      WARN("Failed with status %08lx\n", status);
      return status;
  }
  ```
- **Line Length**: Soft limit at 100 columns; hard limit at 120 columns.
- **Naming Conventions**:
  - Functions and types exposed to Win32 callers must match official Microsoft naming: `BCryptGenerateSymmetricKey()`, `NTSTATUS`, `ULONG_PTR`.
  - Internal static helper functions: `lower_snake_case()`.
  - Constants & Macros: `UPPER_SNAKE_CASE`.

---

## 3. Memory Safety & Defensive Programming

- **Buffer Validation**: Always check output buffer pointers against `NULL` before dereferencing. If a buffer is optional or query-sized, handle `NULL` buffers according to Microsoft specifications (e.g. returning required size in `pcbResult` with `STATUS_BUFFER_TOO_SMALL` or `STATUS_SUCCESS`).
- **Integer Overflow Protection**: When allocating memory based on user-supplied element counts and element sizes, check for arithmetic overflow:
  ```c
  if (count > SIZE_MAX / sizeof(ELEMENT)) {
      return STATUS_INTEGER_OVERFLOW;
  }
  ```
- **Resource Ownership**: Ensure every allocated buffer or acquired lock is cleanly freed or released along all error return paths.

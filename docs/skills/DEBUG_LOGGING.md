# Debug Logging Standards

This document defines logging policies, category channels, and runtime diagnostic rules.

---

## 1. The Cardinal Rule of Logging

```
NEVER USE RAW printf() / fprintf(stderr, ...) / std::cerr IN PRODUCTION CODE
```

Uncontrolled console printing pollutes stdout/stderr, breaks automated test parsers, degrades performance, and cannot be controlled at runtime.

---

## 2. Wine Standard Logging Macros

All logging in Wine subsystem implementations must use the canonical Wine logging macros:

- **`TRACE(...)`**: Verbose logging for function entry, parameters, buffer sizes, and return values. Enabled with `WINEDEBUG=+<channel>`.
  ```c
  TRACE("(%p, %p, %lu): entry\n", handle, buffer, buffer_len);
  ```
- **`WARN(...)`**: Non-fatal anomalies, unexpected parameter values, or unusual conditions that might indicate caller bugs.
  ```c
  WARN("Unusual buffer alignment %lu\n", alignment);
  ```
- **`FIXME(...)`**: Explicit markers for unimplemented stubs or incomplete features. This is critical for discovery agents to locate missing functionality.
  ```c
  FIXME("(%p, %lu): stub\n", handle, flags);
  ```
- **`ERR(...)`**: Severe internal errors, failed system resource allocations, or corrupted internal state.
  ```c
  ERR("Failed to allocate internal context\n");
  ```

---

## 3. Channel Declarations

Every C file implementing a subsystem must declare its default debug channel at the top of the file:

```c
WINE_DEFAULT_DEBUG_CHANNEL(bcrypt);
```

When referencing an external channel across files, declare it explicitly:
```c
WINE_DECLARE_DEBUG_CHANNEL(relay);
```

---

## 4. Inner-Loop & High-Frequency Rules

- Never execute logging calls in performance-critical tight loops (such as block cipher encryption rounds, audio sample mixing, or spinlock waits) unless specifically guarded by an active trace level check.

# Threat & Security Model

This document outlines the security architecture, threat model, and isolation invariants for the `spumante` project.

---

## 1. Threat Landscape & Boundaries

When testing and emulating Windows software on Linux, three distinct security domains exist:

1. **Host System (Developer/Runner Environment)**: The Linux/macOS host machine running tests and agent tooling.
2. **Ground Truth Oracle (Windows VM)**: A virtualized Windows guest executing test binaries to establish baseline expectations.
3. **Compatibility Container (Wine Runtime)**: An isolated user-space environment running Wine.

---

## 2. Security Invariants

### Invariant 1: Host System Isolation
- **Wine Prefix Isolation:** Never run test executables against the default `~/.wine` prefix. All test executions must use ephemeral prefixes created in `/tmp` (e.g., `WINEPREFIX=/tmp/wine_test_$$`).
- **Filesystem Boundary Protection:** Wine by default maps the host root filesystem to drive `Z:`. Test environments must explicitly disable or isolate drive `Z:` to prevent untrusted test binaries from accessing or modifying host files.
- **Desktop Integration Disabled:** Disable winemenubuilder and desktop integration during test runs (`WINEDLLOVERRIDES="winemenubuilder.exe=d"`).

### Invariant 2: Virtual Machine Sandboxing
- The Windows VM must run on an isolated private virtual bridge with **strictly no outbound internet access**.
- The VM disk image should be backed by an ephemeral qcow2 overlay snapshot that is reverted to a clean state after each test session.

### Invariant 3: Memory Safety in Wine Implementations
- Untrusted user applications supply arbitrary pointers, lengths, and flags to Win32 APIs.
- Implementations must rigorously validate inputs:
  - Guard against integer wraps during buffer size calculations.
  - Return appropriate NTSTATUS codes (`STATUS_INVALID_PARAMETER`, `STATUS_BUFFER_TOO_SMALL`, `STATUS_ACCESS_VIOLATION`) rather than triggering host segmentation faults.

### Invariant 4: Credential & Secret Protection
- Strictly zero API keys, cloud credentials, private certificates, or proprietary license keys may ever be committed to the repository.

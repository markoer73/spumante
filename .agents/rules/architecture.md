---
trigger: model_decision
description: Use this whenever designing, modifying, or creating any code to ensure it complies with the system architecture and clean-room policy.
---

# Architecture Rules & The Iron Law of Architecture

The authoritative architectural guidelines are documented in `docs/skills/ARCHITECTURE.md`.

- **THE IRON LAW**: `NO CODE MODIFICATIONS WITHOUT READING THE RELEVANT ARCHITECTURE FIRST`.
- Before touching any implementation or test files:
  1. Inspect `docs/skills/ARCHITECTURE.md` to verify subsystem boundaries, client DLL vs. wineserver separation, and test harness orchestration.
  2. Maintain clean-room standards: strictly zero leaked proprietary Windows source code, zero decompiled binaries, and zero application-specific hacks.

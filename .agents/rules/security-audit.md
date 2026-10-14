---
trigger: model_decision
description: Use this whenever performing security reviews, scanning commits, preparing marcommit, auditing C memory safety, or modifying sandbox/VM isolation.
---

# Security Audit & Commit Scanning Rules

The authoritative security audit specification is stored locally in `docs/skills/SECURITY_AUDIT.md` and `.agents/skills/security-audit/SKILL.md`.

1. **Self-Contained Static Rules**: Never add floating external web links to `.agents/skills/` or `.agents/rules/`. Keep all security checklists and rules statically stored in the repository.
2. **Pre-Commit & CI Gate**: Always run `./scripts/test.sh --security --dll <dll> --test <test>` before completing a task or committing changes.
3. **Four Mandatory Checks**:
   - Secret, forbidden binary, and clean-room anti-decompilation scan.
   - Static C/Python SAST (`strcpy`/`sprintf`/`shell=True` ban) and `WINEPREFIX`/QEMU sandbox verification.
   - Dynamic C AddressSanitizer + UndefinedBehaviorSanitizer (`-fsanitize=address,undefined`) execution.
   - Periodic weekly scheduled CI verification.

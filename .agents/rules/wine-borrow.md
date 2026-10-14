---
trigger: model_decision
description: Use this whenever borrowing, syncing, or backporting code, headers, .spec files, or tests from upstream Wine, or checking upstream Wine alignment.
---

# Upstream Wine Alignment & The `wine-borrow` Skill

The authoritative upstream Wine borrowing and backporting policy is documented in `docs/skills/WINE_BORROW.md` and `.agents/skills/wine-borrow/SKILL.md`.

1. **Strict Read-Only Upstream Wine Repo (Hard Rule)**: Treat the original Wine repository (`winehq.org`) as **STRICTLY READ-ONLY**. Never push to or modify upstream Wine; `.wine-upstream/` must permanently disable push (`DISABLE_PUSH_READ_ONLY_UPSTREAM`). All commits and CI belong exclusively to the user's own `wine-parity` GitHub repository.
2. **Zero Full-Tree Duplication**: Never copy or commit the full upstream Wine tree into `wine-parity`. Always use the git-ignored blobless sparse checkout (`.wine-upstream/`) and track borrowed DLL state in [`upstream/manifest.json`](../../upstream/manifest.json).
3. **Canonical Script Entrypoints**:
   - Check upstream alignment: `./scripts/test.sh --check-upstream --dll <name>`
   - Preview or execute surgical sparse borrow/backport: `./scripts/build.sh --borrow <name> [--dry-run]`
4. **Preserve macOS Acceleration & Verify Parity**: Preserve `#ifdef __APPLE__` native framework blocks during 3-way merges and verify 100% pass rate via `./scripts/test.sh --dll <name> --test <test_name>` and `./scripts/test.sh --security` before updating `upstream/manifest.json`.

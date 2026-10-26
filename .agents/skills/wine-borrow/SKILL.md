---
name: wine-borrow
description: Borrow, align, and surgically backport DLL implementations, headers, and tests from read-only upstream Wine into spumante using blobless sparse checkouts (.wine-upstream/) and upstream/manifest.json without duplicating the Wine source tree.
---

# Wine-Borrow Skill (`wine-borrow`)

## Overview

`spumante` lives in its **own standalone GitHub repository** and works in parallel with upstream Wine (`gitlab.winehq.org/wine/wine.git`) while diverging to prioritize **macOS first** (Apple Silicon hardware acceleration, `CommonCrypto`/`Security.framework`, `CoreFoundation`, `VideoToolbox`/`CoreMedia`, Darwin/Mach primitives) and 100% empirical Windows 10/11 Ground-Truth parity.

Use this skill whenever:
- Borrowing a new DLL, header, or `.spec` file from upstream Wine.
- Re-backporting upstream Wine improvements, bug fixes, or `winetest` cases into an existing `dlls/<name>/` directory.
- Auditing divergence between `spumante` and upstream Wine.

## Iron Laws of `wine-borrow`

1. **STRICT READ-ONLY UPSTREAM WINE REPO (HARD INVARIANT)**:
   - The original Wine repository (`winehq.org`) is **STRICTLY READ-ONLY**.
   - Never push, open PRs, or configure write remotes pointing to upstream Wine.
   - `.wine-upstream/` must always lock its push URL to `DISABLE_PUSH_READ_ONLY_UPSTREAM` (`git -C .wine-upstream remote set-url --push origin DISABLE_PUSH_READ_ONLY_UPSTREAM`).
   - All commits, branches, and GitHub Actions workflows belong exclusively to our own `spumante` GitHub repository.
2. **NO FULL-TREE DUPLICATION**: Never copy or commit the full upstream Wine repository into `spumante`. Upstream Wine must only be accessed via the git-ignored blobless sparse cache (`.wine-upstream/`) configured with `git clone --filter=blob:none --no-checkout --sparse`.
3. **SURGICAL SINGLE-DLL SCOPE**: Materialize only `include` and the specific `dlls/<name>` directory under work via `./scripts/build.sh --borrow <dll>`.
4. **PRESERVE MACOS-FIRST ACCELERATION**: When performing 3-way backports (`git merge-file`), never clobber `#ifdef __APPLE__` native framework backends (`CommonCrypto`, `Security.framework`, `VideoToolbox`, Mach).
5. **MANDATORY DIFFERENTIAL & SECURITY GATE**: No upstream borrow or backport is complete until `./scripts/build.sh --dll <name> --test <test>`, `./scripts/test.sh --dll <name> --test <test>`, `./scripts/test.sh --security`, and `./scripts/test.sh --lint` pass with 0 discrepancies.

## Canonical Commands

All operations use the repository's canonical scripts in `scripts/`:

```sh
# 1. Check read-only upstream manifest alignment and local file integrity for a DLL
./scripts/test.sh --check-upstream --dll bcrypt

# 2. Preview the blobless read-only sparse-checkout and 3-way backport plan
./scripts/build.sh --borrow bcrypt --dry-run

# 3. Execute surgical borrow / 3-way backport for a single DLL
./scripts/build.sh --borrow bcrypt

# 4. Verify build, Ground-Truth differential parity, security, and WineHQ lint rules
./scripts/build.sh --dll bcrypt --test test_symmetric_key
./scripts/test.sh --dll bcrypt --test test_symmetric_key
./scripts/test.sh --security --dll bcrypt --test test_symmetric_key
./scripts/test.sh --lint
```

## Reference Documentation

Read [`docs/skills/WINE_BORROW.md`](../../../docs/skills/WINE_BORROW.md) and inspect [`upstream/manifest.json`](../../../upstream/manifest.json) for full architectural details.

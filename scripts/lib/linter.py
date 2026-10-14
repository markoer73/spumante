#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""WineHQ C99/C11 code style and CommonMark/GFM Markdown compliance linter."""

from __future__ import annotations

import re
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]

RAW_PRINT_RE = re.compile(r"\b(?:printf|fprintf|vprintf|vfprintf)\s*\(")
PRAGMA_ONCE_RE = re.compile(r"^\s*#\s*pragma\s+once\b", re.MULTILINE)


def lint_c_and_h_files() -> list[str]:
    """Check C and header files in dlls/ and tests/ for WineHQ style compliance."""
    errors: list[str] = []
    search_roots = [REPO_ROOT / "dlls", REPO_ROOT / "tests"]

    for root in search_roots:
        if not root.exists():
            continue
        for path in sorted(root.rglob("*")):
            if path.suffix not in (".c", ".h"):
                continue
            rel = path.relative_to(REPO_ROOT)
            text = path.read_text(encoding="utf-8", errors="replace")
            lines = text.splitlines()

            if "SPDX-License-Identifier:" not in text and "Copyright" not in text:
                errors.append(f"{rel}:1: Missing license header")

            if "\t" in text:
                errors.append(f"{rel}: Tab character found (use 4 spaces)")

            if path.suffix == ".h":
                if PRAGMA_ONCE_RE.search(text):
                    errors.append(f"{rel}: Forbidden '#pragma once' found (use #ifndef guard)")
                if "#ifndef " not in text or "#define " not in text:
                    errors.append(f"{rel}: Missing classic #ifndef/#define include guard")

            if "dlls" in rel.parts and path.suffix == ".c":
                for idx, line in enumerate(lines, start=1):
                    # Strip string literals and comments roughly
                    code_part = line.split("//", 1)[0]
                    if RAW_PRINT_RE.search(code_part):
                        errors.append(
                            f"{rel}:{idx}: Forbidden raw printf/fprintf in Wine DLL (use TRACE/WARN/FIXME/ERR)"
                        )
    return errors


def lint_markdown_files() -> list[str]:
    """Verify CommonMark/GFM and KaTeX compliance on status badges."""
    errors: list[str] = []
    unescaped_badge_re = re.compile(
        r"(?<!\\)\[(?:ANTIGRAVITY|GEMINI|CLAUDE|CODEX|OPENCODE|AGENT)-(?:WIP|COMPLETED)[^\]]*\]"
    )
    katex_hazard_re = re.compile(
        r"\\\[(?:ANTIGRAVITY|GEMINI|CLAUDE|CODEX|OPENCODE|AGENT)-(?:WIP|COMPLETED)[^\]]*[#&%$][^\]]*\\\]"
    )

    for md_path in sorted(REPO_ROOT.rglob("*.md")):
        if ".git" in md_path.parts or "builds" in md_path.parts or ".wine-upstream" in md_path.parts:
            continue
        rel = md_path.relative_to(REPO_ROOT)
        text = md_path.read_text(encoding="utf-8", errors="replace")
        in_fence = False
        for idx, line in enumerate(text.splitlines(), start=1):
            stripped = line.strip()
            if stripped.startswith("```"):
                in_fence = not in_fence
                continue
            if in_fence:
                continue
            # Remove inline code spans before checking badges
            no_inline = re.sub(r"`[^`]*`", "", line)
            if unescaped_badge_re.search(no_inline):
                errors.append(
                    f"{rel}:{idx}: Unescaped opening square bracket in agent badge (use \\[...])"
                )
            if katex_hazard_re.search(no_inline):
                errors.append(
                    f"{rel}:{idx}: KaTeX math collision in \\[...\\] badge containing #/&/%/$ (use \\[...] without closing backslash)"
                )
    return errors


def lint_script_proliferation() -> list[str]:
    """Enforce Gate 4: strictly 4 shell scripts in scripts/ and no root-level .sh scripts."""
    errors: list[str] = []
    root_sh = list(REPO_ROOT.glob("*.sh"))
    if root_sh:
        errors.append(f"Forbidden root-level shell scripts found: {[p.name for p in root_sh]}")

    scripts_dir = REPO_ROOT / "scripts"
    if scripts_dir.is_dir():
        sh_names = sorted(p.name for p in scripts_dir.glob("*.sh"))
        expected = ["build.sh", "launch.sh", "stop.sh", "test.sh"]
        if sh_names != expected:
            errors.append(f"scripts/ .sh files {sh_names} != canonical {expected}")
        py_root = list(scripts_dir.glob("*.py"))
        if py_root:
            errors.append(f"Python helpers must be in scripts/lib/, found in scripts/: {[p.name for p in py_root]}")
    return errors


def main() -> int:
    errors = lint_c_and_h_files() + lint_markdown_files() + lint_script_proliferation()
    if errors:
        for err in errors:
            print(f"[LINT ERROR] {err}", file=sys.stderr)
        return 1
    print("[LINT OK] All C/H files, Markdown files, and canonical script boundaries passed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())

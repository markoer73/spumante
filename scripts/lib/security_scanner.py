#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Four-gate security scanner: secrets/clean-room, static C/Python SAST, ASan+UBSan, and CI schedule."""

from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Any

REPO_ROOT = Path(__file__).resolve().parents[2]

SECRET_PATTERNS: list[tuple[str, re.Pattern[str]]] = [
    ("private_key", re.compile(r"-----BEGIN (?:RSA |EC |OPENSSH |DSA )?PRIVATE KEY-----")),
    ("aws_access_key", re.compile(r"\bAKIA[0-9A-Z]{16}\b")),
    ("github_token", re.compile(r"\b(?:ghp|gho|ghu|ghs|ghr)_[A-Za-z0-9_]{36,}\b")),
    ("google_api_key", re.compile(r"\bAIza[0-9A-Za-z\-_]{35}\b")),
    ("slack_token", re.compile(r"\bxox[baprs]-[0-9A-Za-z\-]{10,}\b")),
]

DECOMPILER_PATTERNS: list[tuple[str, re.Pattern[str]]] = [
    ("ida_subroutine", re.compile(r"\bsub_140[0-9A-Fa-f]{4,}\b")),
    ("hexrays_usercall", re.compile(r"__(?:usercall|userpurge)\b")),
    ("ghidra_undefined", re.compile(r"\bundefined[1248]\s+local_[0-9a-f]+\b")),
]

BANNED_C_FUNCS_RE = re.compile(r"\b(?:strcpy|strcat|sprintf|vsprintf|gets|alloca|scanf|sscanf)\s*\(")
BANNED_PY_CALLS_RE = re.compile(r"(?:shell\s*=\s*True|\beval\s*\(|\bexec\s*\(|\bpickle\.loads?\s*\()")


def _iter_repo_files() -> list[Path]:
    files: list[Path] = []
    for p in sorted(REPO_ROOT.rglob("*")):
        if not p.is_file():
            continue
        rel_parts = p.relative_to(REPO_ROOT).parts
        if any(part in (".git", ".wine-upstream", "builds", "build", "__pycache__") for part in rel_parts):
            continue
        files.append(p)
    return files


def gate_secret_and_clean_room_scan() -> dict[str, Any]:
    """Gate A: Scan for leaked secrets, forbidden binary images, decompiler markers, and floating skill URLs."""
    findings: list[str] = []
    forbidden_exts = {".iso", ".qcow2", ".fd", ".rom", ".exe", ".dll", ".dylib", ".so"}

    for path in _iter_repo_files():
        rel = path.relative_to(REPO_ROOT)
        if path.suffix.lower() in forbidden_exts:
            findings.append(f"{rel}: Forbidden binary or VM image in source tree")
            continue

        text = path.read_text(encoding="utf-8", errors="replace")
        for label, pat in SECRET_PATTERNS:
            if pat.search(text):
                findings.append(f"{rel}: Potential leaked secret ({label})")

        if rel.parts[0] in ("dlls", "tests") and path.suffix in (".c", ".h"):
            for label, pat in DECOMPILER_PATTERNS:
                if pat.search(text):
                    findings.append(f"{rel}: Clean-room violation ({label})")

    # Verify self-contained static skill/rule files have zero floating external URLs
    static_rule_dirs = [REPO_ROOT / ".agents" / "skills", REPO_ROOT / ".agents" / "rules"]
    for s_root in static_rule_dirs:
        if not s_root.exists():
            continue
        for md_file in sorted(s_root.rglob("*.md")):
            rel = md_file.relative_to(REPO_ROOT)
            txt = md_file.read_text(encoding="utf-8", errors="replace")
            if "http://" in txt or "https://" in txt:
                findings.append(f"{rel}: Floating external URL found in static skill/rule file")

    return {"passed": len(findings) == 0, "findings": findings}


def gate_static_sast_and_sandbox_scan() -> dict[str, Any]:
    """Gate B: Scan C/Python files for banned unsafe primitives and verify sandbox invariants."""
    findings: list[str] = []

    for c_root in (REPO_ROOT / "dlls", REPO_ROOT / "tests"):
        if not c_root.exists():
            continue
        for path in sorted(c_root.rglob("*")):
            if path.suffix not in (".c", ".h"):
                continue
            rel = path.relative_to(REPO_ROOT)
            for idx, line in enumerate(path.read_text(encoding="utf-8", errors="replace").splitlines(), 1):
                code = line.split("//", 1)[0]
                if BANNED_C_FUNCS_RE.search(code):
                    findings.append(f"{rel}:{idx}: Banned unsafe C function call")

    lib_dir = REPO_ROOT / "scripts" / "lib"
    for py_path in sorted(lib_dir.glob("*.py")):
        if py_path.name == "security_scanner.py":
            continue
        rel = py_path.relative_to(REPO_ROOT)
        for idx, line in enumerate(py_path.read_text(encoding="utf-8", errors="replace").splitlines(), 1):
            code = line.split("#", 1)[0]
            if BANNED_PY_CALLS_RE.search(code):
                findings.append(f"{rel}:{idx}: Banned unsafe Python primitive")

    # Verify THREAT_SECURITY_MODEL.md sandbox invariants in runner.py and vm_manager.py
    runner_txt = (lib_dir / "runner.py").read_text(encoding="utf-8")
    if 'prefix="wine_test_"' not in runner_txt or "winemenubuilder.exe=d" not in runner_txt:
        findings.append("scripts/lib/runner.py: Missing ephemeral WINEPREFIX or winemenubuilder.exe=d isolation")

    vm_txt = (lib_dir / "vm_manager.py").read_text(encoding="utf-8")
    if '"-nic"' not in vm_txt or '"none"' not in vm_txt:
        findings.append("scripts/lib/vm_manager.py: Missing '-nic none' QEMU network isolation")

    # Verify Hard Rule: Upstream Wine repository is strictly read-only
    manifest_path = REPO_ROOT / "upstream" / "manifest.json"
    if manifest_path.is_file():
        manifest_data = json.loads(manifest_path.read_text(encoding="utf-8"))
        if manifest_data.get("upstream_access_mode") != "read-only":
            findings.append("upstream/manifest.json: upstream_access_mode must be 'read-only'")
        if "markoer73/winehq-parity" not in manifest_data.get("project_repository", ""):
            findings.append("upstream/manifest.json: project_repository must point to markoer73/winehq-parity")
    borrow_txt = (lib_dir / "wine_borrow.py").read_text(encoding="utf-8")
    if "DISABLE_PUSH_READ_ONLY_UPSTREAM" not in borrow_txt:
        findings.append("scripts/lib/wine_borrow.py: Missing DISABLE_PUSH_READ_ONLY_UPSTREAM lock")

    git_config = REPO_ROOT / ".git" / "config"
    if git_config.is_file():
        cfg_txt = git_config.read_text(encoding="utf-8", errors="replace")
        if "gitlab.winehq.org" in cfg_txt or "source.winehq.org" in cfg_txt:
            findings.append(".git/config: Root repository remote must never point to upstream winehq.org")

    return {"passed": len(findings) == 0, "findings": findings}


def gate_asan_ubsan_memory_safety(dll: str, test_name: str) -> dict[str, Any]:
    """Gate C: Compile target DLL + conformance test with ASan+UBSan and execute."""
    host_cc = shutil.which("clang") or shutil.which("gcc") or shutil.which("cc")
    if not host_cc:
        return {"passed": False, "findings": ["No host C compiler found for ASan/UBSan"]}

    dll_dir = REPO_ROOT / "dlls" / dll
    test_c = REPO_ROOT / "tests" / "conformance" / dll / f"{test_name}.c"
    c_sources = sorted(str(p) for p in dll_dir.glob("*.c"))
    if dll == "kernelbase":
        ntdll_dir = REPO_ROOT / "dlls" / "ntdll"
        c_sources.extend(sorted(str(p) for p in ntdll_dir.glob("*.c")))

    with tempfile.TemporaryDirectory(prefix="wine_asan_", dir="/tmp") as tmp_dir:
        asan_bin = Path(tmp_dir) / f"{test_name}_asan.bin"
        darwin_frameworks = (
            [
                "-framework",
                "Security",
                "-framework",
                "CoreFoundation",
                "-framework",
                "IOSurface",
                "-framework",
                "CoreGraphics",
                "-framework",
                "VideoToolbox",
                "-framework",
                "CoreMedia",
                "-framework",
                "CoreVideo",
                "-framework",
                "AVFoundation",
                "-framework",
                "AudioToolbox",
                "-framework",
                "CoreAudio",
                "-lobjc",
                "-pthread",
            ]
            if sys.platform == "darwin"
            else ["-pthread"]
        )
        cmd = [
            host_cc,
            "-std=c11",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-O1",
            "-g",
            "-fno-omit-frame-pointer",
            "-fsanitize=address,undefined",
            str(test_c),
            *c_sources,
            *darwin_frameworks,
            "-o",
            str(asan_bin),
        ]
        comp = subprocess.run(cmd, cwd=REPO_ROOT, capture_output=True, text=True, check=False)
        if comp.returncode != 0:
            return {
                "passed": False,
                "findings": [f"ASan/UBSan compilation failed: {comp.stderr}"],
            }

        env = os.environ.copy()
        env["ASAN_OPTIONS"] = "detect_leaks=0:abort_on_error=1"
        env["UBSAN_OPTIONS"] = "print_stacktrace=1:halt_on_error=1"
        run_res = subprocess.run(
            [str(asan_bin)],
            cwd=REPO_ROOT,
            env=env,
            capture_output=True,
            text=True,
            check=False,
        )
        if run_res.returncode != 0:
            return {
                "passed": False,
                "findings": [f"ASan/UBSan runtime violation: {run_res.stderr}"],
            }

    return {
        "passed": True,
        "compiler": host_cc,
        "flags": "-fsanitize=address,undefined -fno-omit-frame-pointer",
        "findings": [],
    }


def gate_ci_schedule_verification() -> dict[str, Any]:
    """Gate D: Verify GitHub Actions CI includes both commit and periodic cron security runs."""
    ci_path = REPO_ROOT / ".github" / "workflows" / "ci.yml"
    findings: list[str] = []
    if not ci_path.is_file():
        return {"passed": False, "findings": ["Missing .github/workflows/ci.yml"]}
    txt = ci_path.read_text(encoding="utf-8")
    if "schedule:" not in txt or "cron:" not in txt:
        findings.append(".github/workflows/ci.yml: Missing periodic 'schedule: cron:' trigger")
    if "./scripts/test.sh --security" not in txt:
        findings.append(".github/workflows/ci.yml: Missing './scripts/test.sh --security' step")
    return {"passed": len(findings) == 0, "findings": findings}


def run_security_audit(dll: str = "bcrypt", test_name: str = "test_symmetric_key") -> dict[str, Any]:
    """Execute all 4 security gates and return a structured report."""
    g1 = gate_secret_and_clean_room_scan()
    g2 = gate_static_sast_and_sandbox_scan()
    g3 = gate_asan_ubsan_memory_safety(dll=dll, test_name=test_name)
    g4 = gate_ci_schedule_verification()

    passed = g1["passed"] and g2["passed"] and g3["passed"] and g4["passed"]
    return {
        "passed": passed,
        "target": f"{dll}:{test_name}",
        "gates": {
            "secret_and_clean_room_scan": g1,
            "static_sast_and_sandbox_scan": g2,
            "asan_ubsan_memory_safety": g3,
            "ci_schedule_verification": g4,
        },
    }


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Wine-Parity 4-Gate Security Scanner")
    parser.add_argument("--dll", default="bcrypt")
    parser.add_argument("--test", default="test_symmetric_key")
    args = parser.parse_args(argv)

    report = run_security_audit(dll=args.dll, test_name=args.test)
    print(json.dumps(report, indent=2))
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    sys.exit(main())

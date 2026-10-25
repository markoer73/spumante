#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Upstream Wine winetest & A/B (wine-parity vs. old .wine-upstream) anti-regression runner."""

from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import sys
import tempfile
import time
from pathlib import Path
from typing import Any

REPO_ROOT = Path(__file__).resolve().parents[2]
if str(REPO_ROOT) not in sys.path:
    sys.path.insert(0, str(REPO_ROOT))

from scripts.lib.runner import is_artifact_up_to_date, select_compilers
from scripts.lib.wine_borrow import load_manifest

TEST_FUNC_RE = re.compile(r"^static\s+void\s+(test_[A-Za-z0-9_]+)\s*\(", re.MULTILINE)
TODO_WINE_RE = re.compile(r"\btodo_wine\b")
SPEC_EXPORT_RE = re.compile(r"^\s*(?:@|\d+)\s+(?:stdcall|cdecl|varargs|extern)\b", re.MULTILINE)


def inspect_upstream_winetest_catalog(dll: str) -> dict[str, Any]:
    """Compare local dlls/<dll> against .wine-upstream/dlls/<dll> when available on disk."""
    if dll == "wineusb" and (REPO_ROOT / "dlls" / "wineusb.sys").is_dir():
        dll = "wineusb.sys"
    manifest = load_manifest()
    sparse_cache = REPO_ROOT / manifest.get("local_sparse_cache", ".wine-upstream")
    dll_meta = manifest.get("borrowed_dlls", {}).get(dll, {})
    upstream_dll_dir = sparse_cache / dll_meta.get("upstream_path", f"dlls/{dll}")
    upstream_tests_dir = sparse_cache / dll_meta.get("upstream_tests_path", f"dlls/{dll}/tests")

    local_spec = REPO_ROOT / "dlls" / dll / f"{dll}.spec"
    local_exports = len(SPEC_EXPORT_RE.findall(local_spec.read_text(encoding="utf-8"))) if local_spec.is_file() else 0

    upstream_exports = 0
    upstream_test_functions: list[str] = []
    upstream_todo_wine_count = 0
    sparse_available = upstream_dll_dir.is_dir()

    if sparse_available:
        up_spec = upstream_dll_dir / f"{dll}.spec"
        if up_spec.is_file():
            upstream_exports = len(SPEC_EXPORT_RE.findall(up_spec.read_text(encoding="utf-8", errors="replace")))
        if upstream_tests_dir.is_dir():
            for test_c in sorted(upstream_tests_dir.glob("*.c")):
                txt = test_c.read_text(encoding="utf-8", errors="replace")
                upstream_test_functions.extend(TEST_FUNC_RE.findall(txt))
                upstream_todo_wine_count += len(TODO_WINE_RE.findall(txt))

    return {
        "dll": dll,
        "sparse_upstream_available": sparse_available,
        "local_spec_exports": local_exports,
        "upstream_spec_exports": upstream_exports if sparse_available else local_exports,
        "upstream_test_function_count": len(upstream_test_functions),
        "upstream_todo_wine_markers": upstream_todo_wine_count,
        "macos_acceleration": dll_meta.get("macos_acceleration", "native"),
    }


def build_winetest(dll: str, arch: str = "x86_64") -> dict[str, Any]:
    """Surgically compile the upstream winetest anti-regression binary (host, ASan+UBSan, and MinGW PE)."""
    if dll == "wineusb" and (REPO_ROOT / "dlls" / "wineusb.sys").is_dir():
        dll = "wineusb.sys"
    dll_dir = REPO_ROOT / "dlls" / dll
    winetest_c = REPO_ROOT / "tests" / "winetest" / dll / f"winetest_{dll}.c"
    if not dll_dir.is_dir():
        raise FileNotFoundError(f"Target DLL directory not found: {dll_dir}")
    if not winetest_c.is_file():
        raise FileNotFoundError(f"Winetest source not found: {winetest_c}")

    compilers = select_compilers(arch)
    host_cc = compilers["host_cc"]
    if not host_cc:
        raise RuntimeError("No C compiler (clang/gcc) found on host")

    build_dir = REPO_ROOT / "builds" / arch / dll
    build_dir.mkdir(parents=True, exist_ok=True)
    c_sources = sorted(str(p) for p in dll_dir.glob("*.c"))
    dep_paths: list[Path] = [winetest_c, *dll_dir.glob("*.c"), *dll_dir.glob("*.h")]
    if dll == "kernelbase":
        ntdll_dir = REPO_ROOT / "dlls" / "ntdll"
        c_sources.extend(sorted(str(p) for p in ntdll_dir.glob("*.c")))
        dep_paths.extend(list(ntdll_dir.glob("*.c")) + list(ntdll_dir.glob("*.h")))
    include_dir = REPO_ROOT / "tests" / "include"
    if include_dir.is_dir():
        dep_paths.extend(include_dir.rglob("*.h"))
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
            "-framework",
            "Metal",
            "-framework",
            "IOKit",
            "-lobjc",
            "-pthread",
        ]
        if sys.platform == "darwin"
        else ["-pthread"]
    )

    built_artifacts: list[str] = []

    # 1. Standard optimized host binary
    winetest_bin = build_dir / f"winetest_{dll}.bin"
    if not is_artifact_up_to_date(winetest_bin, dep_paths):
        cmd_bin = [
            host_cc,
            "-std=c11",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-O2",
            str(winetest_c),
            *c_sources,
            *darwin_frameworks,
            "-o",
            str(winetest_bin),
        ]
        subprocess.run(cmd_bin, cwd=REPO_ROOT, check=True)
    built_artifacts.append(str(winetest_bin.relative_to(REPO_ROOT)))

    # 2. ASan + UBSan instrumented binary for memory-safe winetest verification
    winetest_asan = build_dir / f"winetest_{dll}_asan.bin"
    if not is_artifact_up_to_date(winetest_asan, dep_paths):
        cmd_asan = [
            host_cc,
            "-std=c11",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-O1",
            "-g",
            "-fno-omit-frame-pointer",
            "-fsanitize=address,undefined",
            str(winetest_c),
            *c_sources,
            *darwin_frameworks,
            "-o",
            str(winetest_asan),
        ]
        subprocess.run(cmd_asan, cwd=REPO_ROOT, check=True)
    built_artifacts.append(str(winetest_asan.relative_to(REPO_ROOT)))

    # 3. Optional MinGW-w64 cross-compiled PE executable (.exe)
    mingw_cc = compilers["mingw_cc"]
    if mingw_cc:
        winetest_exe = build_dir / f"winetest_{dll}.exe"
        if not is_artifact_up_to_date(winetest_exe, dep_paths):
            cmd_pe = [
                mingw_cc,
                "-std=c11",
                "-Wall",
                "-Wextra",
                "-O2",
                str(winetest_c),
                *c_sources,
                "-o",
                str(winetest_exe),
            ]
            subprocess.run(cmd_pe, cwd=REPO_ROOT, check=True)
        built_artifacts.append(str(winetest_exe.relative_to(REPO_ROOT)))

    return {
        "dll": dll,
        "suite": f"winetest_{dll}",
        "arch": arch,
        "mingw_available": mingw_cc is not None,
        "built_artifacts": built_artifacts,
    }


def _execute_winetest_binary(
    binary_path: Path,
    dll: str,
    build_dir: Path,
    subtest_filter: str | None = None,
    baseline_mode: bool = False,
    asan_mode: bool = False,
) -> tuple[int, dict[str, Any], float, str]:
    """Execute a compiled winetest binary inside an isolated ephemeral /tmp/wine_test_* sandbox."""
    with tempfile.TemporaryDirectory(prefix="wine_test_", dir="/tmp") as ephemeral_prefix:
        dosdevices = Path(ephemeral_prefix) / "dosdevices"
        dosdevices.mkdir(parents=True, exist_ok=True)
        z_drive = dosdevices / "z:"
        if z_drive.exists() or z_drive.is_symlink():
            z_drive.unlink()

        env = os.environ.copy()
        env["WINEPREFIX"] = ephemeral_prefix
        env["WINEDLLOVERRIDES"] = f"winemenubuilder.exe=d;{dll}=n,b"
        env["WINEDLLPATH"] = str(build_dir)
        env["WINETEST_BASELINE_MODE"] = "1" if baseline_mode else "0"
        if subtest_filter:
            env["WINETEST_FILTER"] = subtest_filter
        else:
            env.pop("WINETEST_FILTER", None)

        if asan_mode:
            env["ASAN_OPTIONS"] = "detect_leaks=0:abort_on_error=1"
            env["UBSAN_OPTIONS"] = "print_stacktrace=1:halt_on_error=1"

        t0 = time.perf_counter()
        proc = subprocess.run(
            [str(binary_path)],
            cwd=REPO_ROOT,
            env=env,
            capture_output=True,
            text=True,
            check=False,
        )
        elapsed_ms = round((time.perf_counter() - t0) * 1000.0, 3)

    parsed = json.loads(proc.stdout) if proc.stdout.strip().startswith("{") else {}
    return proc.returncode, parsed, elapsed_ms, proc.stderr


def run_winetest_and_ab(
    dll: str,
    arch: str = "x86_64",
    subtest_filter: str | None = None,
) -> tuple[int, dict[str, Any]]:
    """Run upstream winetest suite against baseline & wine-parity + ASan/UBSan and compare A/B results."""
    if dll == "wineusb" and (REPO_ROOT / "dlls" / "wineusb.sys").is_dir():
        dll = "wineusb.sys"
    build_dir = REPO_ROOT / "builds" / arch / dll
    winetest_bin = build_dir / f"winetest_{dll}.bin"
    winetest_asan = build_dir / f"winetest_{dll}_asan.bin"
    dll_dir = REPO_ROOT / "dlls" / dll
    winetest_c = REPO_ROOT / "tests" / "winetest" / dll / f"winetest_{dll}.c"
    wine_test_h = REPO_ROOT / "tests" / "include" / "wine" / "test.h"
    watched_sources = [winetest_c, wine_test_h, *dll_dir.glob("*.c"), *dll_dir.glob("*.h")]
    newest_src_mtime = max((p.stat().st_mtime for p in watched_sources if p.is_file()), default=0.0)
    if (
        not winetest_bin.is_file()
        or not winetest_asan.is_file()
        or winetest_bin.stat().st_mtime < newest_src_mtime
        or winetest_asan.stat().st_mtime < newest_src_mtime
    ):
        build_winetest(dll=dll, arch=arch)

    # Run (A): Baseline Wine mode (marks todo_wine blocks as upstream Wine baseline todos)
    base_rc, base_doc, base_ms, _ = _execute_winetest_binary(
        winetest_bin,
        dll=dll,
        build_dir=build_dir,
        subtest_filter=subtest_filter,
        baseline_mode=True,
        asan_mode=False,
    )

    # Run (B): wine-parity macOS-accelerated mode (requires 100% pass including resolved todo_wine blocks)
    parity_rc, parity_doc, parity_ms, parity_stderr = _execute_winetest_binary(
        winetest_bin,
        dll=dll,
        build_dir=build_dir,
        subtest_filter=subtest_filter,
        baseline_mode=False,
        asan_mode=False,
    )

    # Run (C): ASan + UBSan runtime memory-safety verification on winetest suite
    asan_rc, _, asan_ms, asan_stderr = _execute_winetest_binary(
        winetest_asan,
        dll=dll,
        build_dir=build_dir,
        subtest_filter=subtest_filter,
        baseline_mode=False,
        asan_mode=True,
    )

    total_assertions = parity_doc.get("total_assertions", 0)
    parity_passed = parity_doc.get("passed_assertions", 0)
    failures = parity_doc.get("failures", 0)
    base_passed = base_doc.get("passed_assertions", 0)
    todo_resolved = base_doc.get("todo_successes", 0)
    regressions = max(0, base_passed - parity_passed) + failures
    asan_passed = asan_rc == 0
    passed = (parity_rc == 0) and (base_rc == 0) and (failures == 0) and (regressions == 0) and asan_passed

    report = {
        "passed": passed,
        "dll": dll,
        "suite": parity_doc.get("suite", f"winetest_{dll}"),
        "arch": arch,
        "total_assertions": total_assertions,
        "passed_assertions": parity_passed,
        "failures": failures,
        "skipped": parity_doc.get("skipped", 0),
        "regressions_vs_upstream": regressions,
        "subtests_executed": parity_doc.get("subtests_executed", []),
        "ab_comparison": {
            "zero_regressions": passed,
            "baseline_passed_assertions": base_passed,
            "wine_parity_passed_assertions": parity_passed,
            "todo_wine_resolved_count": todo_resolved,
            "baseline_latency_ms": base_ms,
            "wine_parity_latency_ms": parity_ms,
            "asan_ubsan_latency_ms": asan_ms,
            "asan_ubsan_passed": asan_passed,
            "upstream_catalog": inspect_upstream_winetest_catalog(dll),
        },
        "winetest_summary": parity_stderr.strip(),
    }
    if not asan_passed:
        report["asan_ubsan_error"] = asan_stderr.strip()

    return (0 if passed else 1), report


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Upstream Wine winetest & A/B Anti-Regression Runner")
    parser.add_argument("--mode", choices=["build", "test"], default="test")
    parser.add_argument("--dll", default="bcrypt")
    parser.add_argument("--arch", choices=["x86_64", "arm64"], default="x86_64")
    parser.add_argument("--filter", default=None)
    args = parser.parse_args(argv)

    if args.mode == "build":
        info = build_winetest(dll=args.dll, arch=args.arch)
        print(json.dumps(info, indent=2))
        return 0

    rc, report = run_winetest_and_ab(dll=args.dll, arch=args.arch, subtest_filter=args.filter)
    print(json.dumps(report, indent=2))
    return rc


if __name__ == "__main__":
    sys.exit(main())

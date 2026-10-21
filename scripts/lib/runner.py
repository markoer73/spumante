#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Surgical single-target build and differential test runner (macOS First, Linux Second)."""

from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Any

REPO_ROOT = Path(__file__).resolve().parents[2]
if str(REPO_ROOT) not in sys.path:
    sys.path.insert(0, str(REPO_ROOT))

from scripts.lib.comparator import compare_differential_results
from scripts.lib.vm_manager import DEFAULT_QGA_SOCK, QGAClient


def select_compilers(arch: str) -> dict[str, str | None]:
    """Detect MinGW-w64 cross-compiler and host C compiler (Clang/GCC)."""
    if arch == "arm64":
        mingw_candidates = ["aarch64-w64-mingw32-gcc", "aarch64-w64-mingw32-clang"]
    else:
        mingw_candidates = ["x86_64-w64-mingw32-gcc", "x86_64-w64-mingw32-clang"]

    mingw_cc = next((shutil.which(c) for c in mingw_candidates if shutil.which(c)), None)
    host_cc = shutil.which("clang") or shutil.which("gcc") or shutil.which("cc")
    return {"mingw_cc": mingw_cc, "host_cc": host_cc}


def surgical_build(
    dll: str,
    test_name: str | None = None,
    arch: str = "x86_64",
    export_patch: bool = False,
) -> dict[str, Any]:
    """Surgically build ONLY the specified Wine DLL and/or single conformance test."""
    dll_dir = REPO_ROOT / "dlls" / dll
    if not dll_dir.is_dir():
        raise FileNotFoundError(f"Target DLL directory not found: {dll_dir}")

    compilers = select_compilers(arch)
    host_cc = compilers["host_cc"]
    if not host_cc:
        raise RuntimeError("No C compiler (clang/gcc) found on host")

    build_dir = REPO_ROOT / "builds" / arch / dll
    build_dir.mkdir(parents=True, exist_ok=True)

    c_sources = sorted(str(p) for p in dll_dir.glob("*.c"))
    if dll == "kernelbase":
        ntdll_dir = REPO_ROOT / "dlls" / "ntdll"
        c_sources.extend(sorted(str(p) for p in ntdll_dir.glob("*.c")))
    built_artifacts: list[str] = []

    # 1. Build single shared library / Wine override module for target DLL
    shared_ext = ".dylib" if sys.platform == "darwin" else ".so"
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
            "-pthread",
        ]
        if sys.platform == "darwin"
        else ["-pthread"]
    )
    dll_out = build_dir / f"{dll}{shared_ext}"
    if c_sources:
        cmd_dll = [
            host_cc,
            "-std=c11",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-O2",
            "-fPIC",
            "-shared",
            *c_sources,
            *darwin_frameworks,
            "-o",
            str(dll_out),
        ]
        subprocess.run(cmd_dll, check=True)
        built_artifacts.append(str(dll_out.relative_to(REPO_ROOT)))

    # 2. Surgically compile ONLY the requested single conformance test binary
    if test_name:
        test_c = REPO_ROOT / "tests" / "conformance" / dll / f"{test_name}.c"
        if not test_c.is_file():
            raise FileNotFoundError(f"Conformance test source not found: {test_c}")

        # Host-linked conformance runner for fast deterministic macOS/Linux Wine execution
        test_bin = build_dir / f"{test_name}.bin"
        cmd_test = [
            host_cc,
            "-std=c11",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-O2",
            str(test_c),
            *c_sources,
            *darwin_frameworks,
            "-o",
            str(test_bin),
        ]
        subprocess.run(cmd_test, check=True)
        built_artifacts.append(str(test_bin.relative_to(REPO_ROOT)))

        # Optional MinGW-w64 PE executable (.exe) when cross-compiler is present
        mingw_cc = compilers["mingw_cc"]
        if mingw_cc:
            pe_out = build_dir / f"{test_name}.exe"
            cmd_pe = [
                mingw_cc,
                "-std=c11",
                "-Wall",
                "-Wextra",
                "-O2",
                str(test_c),
                *c_sources,
                "-o",
                str(pe_out),
            ]
            subprocess.run(cmd_pe, check=True)
            built_artifacts.append(str(pe_out.relative_to(REPO_ROOT)))

    if export_patch:
        patches_dir = REPO_ROOT / "patches"
        patches_dir.mkdir(parents=True, exist_ok=True)
        patch_file = patches_dir / f"0001-{dll}-clean-room-parity.patch"
        patch_content = (
            f"From: Wine Parity Agent <agent@wine-parity.local>\n"
            f"Subject: [PATCH] {dll}: Implement clean-room conformance fixes\n\n"
            f"Clean-Room-Spec: specs/{dll}/\n"
            f"Signed-off-by: Wine Parity Agent <agent@wine-parity.local>\n"
        )
        patch_file.write_text(patch_content, encoding="utf-8")
        built_artifacts.append(str(patch_file.relative_to(REPO_ROOT)))

    return {
        "dll": dll,
        "test": test_name,
        "arch": arch,
        "mingw_available": compilers["mingw_cc"] is not None,
        "built_artifacts": built_artifacts,
    }


def run_differential_test(
    dll: str,
    test_name: str,
    arch: str = "x86_64",
    case_filter: str | None = None,
    live_vm: bool = False,
    simulate_stub: bool = False,
) -> tuple[int, dict[str, Any]]:
    """Execute a single conformance test in an isolated prefix and compare with Ground Truth."""
    build_dir = REPO_ROOT / "builds" / arch / dll
    test_bin = build_dir / f"{test_name}.bin"
    if not test_bin.is_file():
        surgical_build(dll=dll, test_name=test_name, arch=arch)

    # 1. Obtain Windows Ground-Truth JSON (Live VM via VirtIO serial qemu-ga if requested & online, else cached fixture)
    gt_path = REPO_ROOT / "tests" / "ground_truth" / dll / f"{test_name}.json"
    windows_doc: dict[str, Any] | None = None

    if live_vm and DEFAULT_QGA_SOCK.exists():
        qga = QGAClient(DEFAULT_QGA_SOCK)
        pe_exe = build_dir / f"{test_name}.exe"
        if qga.ping() and pe_exe.is_file():
            guest_dest = f"C:\\Windows\\Temp\\{test_name}.exe"
            qga.upload_file(pe_exe, guest_dest)
            vm_res = qga.exec_test(guest_dest)
            windows_doc = json.loads(vm_res["stdout"])

    if windows_doc is None:
        if not gt_path.is_file():
            raise FileNotFoundError(f"Ground-Truth fixture not found: {gt_path}")
        windows_doc = json.loads(gt_path.read_text(encoding="utf-8"))

    # 2. Execute under isolated ephemeral Wine prefix (Invariant 1: /tmp/wine_test_$$, winemenubuilder.exe=d)
    with tempfile.TemporaryDirectory(prefix="wine_test_", dir="/tmp") as ephemeral_prefix:
        dosdevices = Path(ephemeral_prefix) / "dosdevices"
        dosdevices.mkdir(parents=True, exist_ok=True)
        # Explicitly ensure drive Z: is never mapped inside the ephemeral prefix
        z_drive = dosdevices / "z:"
        if z_drive.exists() or z_drive.is_symlink():
            z_drive.unlink()

        env = os.environ.copy()
        env["WINEPREFIX"] = ephemeral_prefix
        env["WINEDLLOVERRIDES"] = f"winemenubuilder.exe=d;{dll}=n,b"
        env["WINEDLLPATH"] = str(build_dir)
        if simulate_stub:
            env["WINE_PARITY_SIMULATE_STUB"] = "1"
        else:
            env.pop("WINE_PARITY_SIMULATE_STUB", None)

        proc = subprocess.run(
            [str(test_bin)],
            cwd=REPO_ROOT,
            env=env,
            capture_output=True,
            text=True,
            check=False,
        )

    try:
        wine_doc = json.loads(proc.stdout)
    except json.JSONDecodeError as exc:
        return 1, {
            "matched": False,
            "error": f"Failed to parse Wine test JSON stdout: {exc}",
            "stdout": proc.stdout,
            "stderr": proc.stderr,
        }

    comparison = compare_differential_results(
        windows_doc=windows_doc,
        wine_doc=wine_doc,
        case_filter=case_filter,
    )
    exit_code = 0 if (comparison["matched"] and proc.returncode == 0) else 1
    return exit_code, comparison


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Surgical Build & Differential Test Runner")
    parser.add_argument("--mode", choices=["build", "test"], required=True)
    parser.add_argument("--dll", required=True)
    parser.add_argument("--test", default=None)
    parser.add_argument("--arch", choices=["x86_64", "arm64"], default="x86_64")
    parser.add_argument("--filter", default=None)
    parser.add_argument("--live-vm", action="store_true")
    parser.add_argument("--simulate-stub", action="store_true")
    parser.add_argument("--export-patch", action="store_true")
    args = parser.parse_args(argv)

    if args.mode == "build":
        info = surgical_build(
            dll=args.dll,
            test_name=args.test,
            arch=args.arch,
            export_patch=args.export_patch,
        )
        print(json.dumps(info, indent=2))
        return 0

    if not args.test:
        print("ERROR: --test <test_name> is required for surgical differential execution.", file=sys.stderr)
        return 1

    rc, report = run_differential_test(
        dll=args.dll,
        test_name=args.test,
        arch=args.arch,
        case_filter=args.filter,
        live_vm=args.live_vm,
        simulate_stub=args.simulate_stub,
    )
    print(json.dumps(report, indent=2))
    if rc == 0:
        print(
            f"[DIFFERENTIAL RESULT] {args.dll}:{args.test} -> MATCHED "
            f"({report['discrepancy_count']} discrepancies) [{report['total_assertions']} assertions]"
        )
    else:
        print(
            f"[DIFFERENTIAL RESULT] {args.dll}:{args.test} -> FAILED "
            f"({report.get('discrepancy_count', 1)} discrepancies)",
            file=sys.stderr,
        )
    return rc


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Antigravity IDE (C++ TestMate & Python Test Explorer) test discovery builder and verifier."""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
import tempfile
import unittest
import xml.etree.ElementTree as ET
from pathlib import Path
from typing import Any

REPO_ROOT = Path(__file__).resolve().parents[2]
if str(REPO_ROOT) not in sys.path:
    sys.path.insert(0, str(REPO_ROOT))

from scripts.lib.runner import surgical_build
from scripts.lib.winetest_runner import build_winetest

VSCODE_DIR = REPO_ROOT / ".vscode"
VSCODE_SETTINGS_PATH = VSCODE_DIR / "settings.json"
VSCODE_EXTENSIONS_PATH = VSCODE_DIR / "extensions.json"
VSCODE_TASKS_PATH = VSCODE_DIR / "tasks.json"


def generate_vscode_configs() -> dict[str, str]:
    """Write .vscode/settings.json, .vscode/extensions.json, and .vscode/tasks.json for Antigravity IDE & TestMate."""
    VSCODE_DIR.mkdir(parents=True, exist_ok=True)

    settings_obj: dict[str, Any] = {
        "python.testing.unittestEnabled": True,
        "python.testing.pytestEnabled": False,
        "python.testing.unittestArgs": [
            "-v",
            "-s",
            "./tests/unit",
            "-p",
            "test_*.py",
        ],
        "testMate.cpp.test.executables": "builds/x86_64/**/{test_*,winetest_*[!.][!a][!s][!a][!n]}.bin",
        "testMate.cpp.test.advancedExecutables": [
            {
                "pattern": "builds/{x86_64,arm64}/**/test_*.bin",
                "name": "${filename} (${relDirpath})",
                "description": "Spumante Differential Conformance Suite",
                "cwd": "${workspaceFolder}",
                "gtest": {
                    "prependTestRunningArgs": [],
                },
            },
            {
                "pattern": "builds/{x86_64,arm64}/**/winetest_*.bin",
                "exclude": ["**/*_asan.bin"],
                "name": "${filename} (${relDirpath})",
                "description": "Upstream Wine winetest Anti-Regression Suite",
                "cwd": "${workspaceFolder}",
                "gtest": {
                    "prependTestRunningArgs": [],
                },
            },
        ],
        "testMate.cpp.test.workingDirectory": "${workspaceFolder}",
        "testMate.cpp.discovery.gracePeriodForMissing": 5000,
    }

    extensions_obj: dict[str, Any] = {
        "recommendations": [
            "matepek.vscode-catch2-test-adapter",
            "ms-python.python",
            "llvm-vs-code-extensions.vscode-clangd",
        ]
    }

    tasks_obj: dict[str, Any] = {
        "version": "2.0.0",
        "tasks": [
            {
                "label": "Spumante: Build & Refresh All IDE Test Binaries",
                "type": "shell",
                "command": "./scripts/build.sh",
                "args": ["--ide-tests"],
                "group": {
                    "kind": "build",
                    "isDefault": True,
                },
                "problemMatcher": ["$gcc"],
            },
            {
                "label": "Spumante: Verify Antigravity IDE / TestMate Visibility",
                "type": "shell",
                "command": "./scripts/test.sh",
                "args": ["--ide-tests"],
                "group": "test",
                "problemMatcher": [],
            },
            {
                "label": "Spumante: Run Style & IDE Visibility Linter",
                "type": "shell",
                "command": "./scripts/test.sh",
                "args": ["--lint"],
                "group": "test",
                "problemMatcher": [],
            },
        ],
    }

    VSCODE_SETTINGS_PATH.write_text(json.dumps(settings_obj, indent=2) + "\n", encoding="utf-8")
    VSCODE_EXTENSIONS_PATH.write_text(json.dumps(extensions_obj, indent=2) + "\n", encoding="utf-8")
    VSCODE_TASKS_PATH.write_text(json.dumps(tasks_obj, indent=2) + "\n", encoding="utf-8")
    return {
        "settings_json": str(VSCODE_SETTINGS_PATH),
        "extensions_json": str(VSCODE_EXTENSIONS_PATH),
        "tasks_json": str(VSCODE_TASKS_PATH),
    }


def _normalize_dll_dir_name(raw_dll: str) -> str:
    if raw_dll == "wineusb" and (REPO_ROOT / "dlls" / "wineusb.sys").is_dir():
        return "wineusb.sys"
    if raw_dll == "winebth" and (REPO_ROOT / "dlls" / "winebth.sys").is_dir():
        return "winebth.sys"
    return raw_dll


def build_all_ide_test_binaries(arch: str = "x86_64") -> dict[str, Any]:
    """Ensure all C conformance test binaries and winetest binaries are compiled and ready for TestMate discovery."""
    vscode_files = generate_vscode_configs()
    conformance_root = REPO_ROOT / "tests" / "conformance"
    winetest_root = REPO_ROOT / "tests" / "winetest"

    built_conformance: list[str] = []
    built_winetest: list[str] = []

    if conformance_root.is_dir():
        for test_c in sorted(conformance_root.glob("*/*.c")):
            raw_dll = test_c.parent.name
            test_name = test_c.stem
            res = surgical_build(dll=raw_dll, test_name=test_name, arch=arch)
            for art in res.get("built_artifacts", []):
                if art.endswith(f"{test_name}.bin"):
                    built_conformance.append(art)

    if winetest_root.is_dir():
        for wt_c in sorted(winetest_root.glob("*/winetest_*.c")):
            raw_dll = wt_c.parent.name
            res = build_winetest(dll=raw_dll, arch=arch)
            for art in res.get("built_artifacts", []):
                if art.endswith(".bin") and not art.endswith("_asan.bin"):
                    built_winetest.append(art)

    return {
        "action": "build-ide-tests",
        "arch": arch,
        "vscode_configs": vscode_files,
        "conformance_binaries": built_conformance,
        "winetest_binaries": built_winetest,
        "total_binaries": len(built_conformance) + len(built_winetest),
    }


def verify_ide_test_visibility(arch: str = "x86_64") -> dict[str, Any]:
    """Verify every C conformance test, winetest suite, and Python unit test is discoverable in Antigravity IDE / TestMate."""
    errors: list[str] = []
    if not VSCODE_SETTINGS_PATH.is_file():
        errors.append(f"Missing {VSCODE_SETTINGS_PATH.relative_to(REPO_ROOT)}")
    else:
        try:
            sdoc = json.loads(VSCODE_SETTINGS_PATH.read_text(encoding="utf-8"))
            if not sdoc.get("python.testing.unittestEnabled"):
                errors.append(".vscode/settings.json must set python.testing.unittestEnabled = true")
            if "testMate.cpp.test.advancedExecutables" not in sdoc and "testMate.cpp.test.executables" not in sdoc:
                errors.append(".vscode/settings.json must configure testMate.cpp.test.advancedExecutables")
        except (OSError, json.JSONDecodeError) as exc:
            errors.append(f"Invalid .vscode/settings.json: {exc}")

    conformance_suites: list[dict[str, Any]] = []
    winetest_suites: list[dict[str, Any]] = []
    total_c_test_cases = 0

    conformance_root = REPO_ROOT / "tests" / "conformance"
    winetest_root = REPO_ROOT / "tests" / "winetest"

    with tempfile.TemporaryDirectory(prefix="ide_testmate_verify_") as tmp:
        tmp_dir = Path(tmp)

        if conformance_root.is_dir():
            for test_c in sorted(conformance_root.glob("*/*.c")):
                dll = _normalize_dll_dir_name(test_c.parent.name)
                test_name = test_c.stem
                bin_path = REPO_ROOT / "builds" / arch / dll / f"{test_name}.bin"
                if not bin_path.is_file():
                    errors.append(
                        f"Missing compiled IDE test binary {bin_path.relative_to(REPO_ROOT)} for {test_c.relative_to(REPO_ROOT)}"
                    )
                    continue

                xml_out = tmp_dir / f"conf_{dll}_{test_name}.xml"
                res = subprocess.run(
                    [str(bin_path), "--gtest_list_tests", f"--gtest_output=xml:{xml_out}"],
                    cwd=REPO_ROOT,
                    capture_output=True,
                    text=True,
                    check=False,
                )
                if res.returncode != 0 or not xml_out.is_file():
                    errors.append(
                        f"TestMate --gtest_list_tests failed on {bin_path.relative_to(REPO_ROOT)}: {res.stderr}"
                    )
                    continue

                tree = ET.parse(xml_out)
                cases = tree.getroot().findall(".//testcase")
                if not cases:
                    errors.append(f"Zero TestMate testcases discovered in {bin_path.relative_to(REPO_ROOT)}")
                total_c_test_cases += len(cases)
                conformance_suites.append(
                    {
                        "dll": dll,
                        "test_name": test_name,
                        "source": str(test_c.relative_to(REPO_ROOT)),
                        "binary": str(bin_path.relative_to(REPO_ROOT)),
                        "discovered_cases": len(cases),
                    }
                )

        if winetest_root.is_dir():
            for wt_c in sorted(winetest_root.glob("*/winetest_*.c")):
                dll = _normalize_dll_dir_name(wt_c.parent.name)
                bin_path = REPO_ROOT / "builds" / arch / dll / f"winetest_{dll}.bin"
                if not bin_path.is_file():
                    errors.append(
                        f"Missing compiled IDE winetest binary {bin_path.relative_to(REPO_ROOT)} for {wt_c.relative_to(REPO_ROOT)}"
                    )
                    continue

                xml_out = tmp_dir / f"wt_{dll}.xml"
                res = subprocess.run(
                    [str(bin_path), "--gtest_list_tests", f"--gtest_output=xml:{xml_out}"],
                    cwd=REPO_ROOT,
                    capture_output=True,
                    text=True,
                    check=False,
                )
                if res.returncode != 0 or not xml_out.is_file():
                    errors.append(
                        f"TestMate --gtest_list_tests failed on {bin_path.relative_to(REPO_ROOT)}: {res.stderr}"
                    )
                    continue

                tree = ET.parse(xml_out)
                cases = tree.getroot().findall(".//testcase")
                if not cases:
                    errors.append(f"Zero TestMate subtests discovered in {bin_path.relative_to(REPO_ROOT)}")
                total_c_test_cases += len(cases)
                winetest_suites.append(
                    {
                        "dll": dll,
                        "suite": f"winetest_{dll}",
                        "source": str(wt_c.relative_to(REPO_ROOT)),
                        "binary": str(bin_path.relative_to(REPO_ROOT)),
                        "discovered_subtests": len(cases),
                    }
                )

    loader = unittest.TestLoader()
    py_suite = loader.discover(start_dir=str(REPO_ROOT / "tests" / "unit"), pattern="test_*.py")
    py_count = py_suite.countTestCases()
    if py_count == 0:
        errors.append("Zero Python unittest cases discovered in tests/unit/")

    return {
        "action": "verify-ide-tests",
        "passed": len(errors) == 0,
        "conformance_suites_count": len(conformance_suites),
        "winetest_suites_count": len(winetest_suites),
        "total_c_testmate_cases": total_c_test_cases,
        "python_unittest_cases_count": py_count,
        "conformance_suites": conformance_suites,
        "winetest_suites": winetest_suites,
        "errors": errors,
    }


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Antigravity IDE & TestMate Test Discovery Manager")
    parser.add_argument("--action", choices=["build", "verify"], default="verify")
    parser.add_argument("--arch", default="x86_64")
    args = parser.parse_args(argv)

    if args.action == "build":
        rep = build_all_ide_test_binaries(arch=args.arch)
        print(json.dumps(rep, indent=2))
        return 0

    rep = verify_ide_test_visibility(arch=args.arch)
    print(json.dumps(rep, indent=2))
    return 0 if rep["passed"] else 1


if __name__ == "__main__":
    sys.exit(main())

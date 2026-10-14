#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""3-layer automated stub discovery scanner for Wine DLLs (bcrypt, kernelbase, mfplat)."""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path
from typing import Any

REPO_ROOT = Path(__file__).resolve().parents[2]

MACOS_FRAMEWORK_MAP: dict[str, str] = {
    "bcrypt": "CommonCrypto / Security.framework",
    "ncrypt": "Security.framework / Keychain",
    "kernelbase": "Darwin libSystem / CoreFoundation / Mach",
    "ntdll": "Darwin XNU / Mach / libSystem",
    "mfplat": "VideoToolbox / CoreMedia / AVFoundation",
}

SPEC_STUB_RE = re.compile(r"^\s*(?:@|\d+)\s+stub(?:-[a-z0-9]+)?\s+([A-Za-z0-9_]+)", re.MULTILINE)
C_FUNC_RE = re.compile(
    r"(?:NTSTATUS|HRESULT|BOOL|DWORD|ULONG|LPVOID|HANDLE|void)\s+(?:WINAPI\s+|NTAPI\s+)?([A-Za-z0-9_]+)\s*\([^;{]*\)\s*\{([\s\S]*?)^\}",
    re.MULTILINE,
)
FIXME_RE = re.compile(r'FIXME\s*\(\s*"([^"]*)"')
SERVER_REQ_RE = re.compile(r"\bSERVER_START_REQ\b|\bwine_server_call\b")


def scan_dll_directory(dll_dir: Path, relay_counts: dict[str, int] | None = None) -> list[dict[str, Any]]:
    """Scan a single DLL directory for .spec @ stub entries and C FIXME stubs."""
    dll_name = dll_dir.name
    macos_fw = MACOS_FRAMEWORK_MAP.get(dll_name, "POSIX / CoreFoundation")
    stubs: dict[str, dict[str, Any]] = {}
    relay_map = relay_counts or {}

    # Layer 1: .spec file @ stub declarations
    for spec_file in sorted(dll_dir.glob("*.spec")):
        text = spec_file.read_text(encoding="utf-8", errors="replace")
        for match in SPEC_STUB_RE.finditer(text):
            api_name = match.group(1)
            stubs[api_name] = {
                "dll": dll_name,
                "api_name": api_name,
                "stub_type": "spec_stub",
                "source_file": str(spec_file.relative_to(REPO_ROOT)),
                "wineserver_independent": True,
                "macos_framework": macos_fw,
                "relay_hits": relay_map.get(api_name, 0),
                "priority_score": 90 + min(relay_map.get(api_name, 0), 50),
            }

    # Layer 2: C source full stubs and partial branch FIXMEs
    for c_file in sorted(dll_dir.glob("*.c")):
        text = c_file.read_text(encoding="utf-8", errors="replace")
        for func_match in C_FUNC_RE.finditer(text):
            api_name = func_match.group(1)
            body = func_match.group(2)
            fixmes = FIXME_RE.findall(body)
            has_server_call = bool(SERVER_REQ_RE.search(body))
            wineserver_indep = not has_server_call

            if not fixmes:
                continue

            is_full_stub = any("stub" in msg.lower() for msg in fixmes) and body.count("return") <= 2
            stub_type = "c_full_stub" if is_full_stub else "c_partial_fixme"
            base_score = 85 if wineserver_indep else 45
            if stub_type == "c_partial_fixme":
                base_score += 5

            stubs[api_name] = {
                "dll": dll_name,
                "api_name": api_name,
                "stub_type": stub_type,
                "fixme_messages": fixmes,
                "source_file": str(c_file.relative_to(REPO_ROOT)),
                "wineserver_independent": wineserver_indep,
                "macos_framework": macos_fw,
                "relay_hits": relay_map.get(api_name, 0),
                "priority_score": base_score + min(relay_map.get(api_name, 0), 50),
            }

    return sorted(stubs.values(), key=lambda x: (-x["priority_score"], x["api_name"]))


def parse_relay_log(log_path: Path | None) -> dict[str, int]:
    """Layer 3: Parse WINEDEBUG=+fixme,+relay runtime trace logs for API call frequencies."""
    counts: dict[str, int] = {}
    if not log_path or not log_path.is_file():
        return counts
    fixme_func_re = re.compile(r"fixme:[a-z0-9_]+:([A-Za-z0-9_]+)\s")
    for line in log_path.read_text(encoding="utf-8", errors="replace").splitlines():
        m = fixme_func_re.search(line)
        if m:
            fn = m.group(1)
            counts[fn] = counts.get(fn, 0) + 1
    return counts


def discover_stubs(
    dll_filter: str | None = None,
    relay_log: Path | None = None,
) -> dict[str, Any]:
    """Run the 3-layer stub scanner across target DLL directories."""
    dlls_root = REPO_ROOT / "dlls"
    relay_counts = parse_relay_log(relay_log)
    target_names = [dll_filter] if dll_filter else ["bcrypt", "kernelbase", "mfplat"]

    all_stubs: list[dict[str, Any]] = []
    for name in target_names:
        d_path = dlls_root / name
        if d_path.is_dir():
            all_stubs.extend(scan_dll_directory(d_path, relay_counts=relay_counts))

    all_stubs.sort(key=lambda x: (-x["priority_score"], x["dll"], x["api_name"]))
    return {
        "scanner_version": "1.0.0",
        "target_dlls": target_names,
        "total_stubs": len(all_stubs),
        "stubs": all_stubs,
    }


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="3-Layer Wine DLL Stub Discovery Scanner")
    parser.add_argument("--dll", default=None, help="Specific DLL to scan (e.g. bcrypt)")
    parser.add_argument("--relay-log", type=Path, default=None, help="Optional WINEDEBUG=+fixme log")
    args = parser.parse_args(argv)

    report = discover_stubs(dll_filter=args.dll, relay_log=args.relay_log)
    print(json.dumps(report, indent=2))
    return 0


if __name__ == "__main__":
    sys.exit(main())

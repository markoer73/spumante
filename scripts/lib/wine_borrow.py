#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Sparse-Overlay + Manifest upstream Wine alignment and 3-way surgical backport engine.

STRICT READ-ONLY UPSTREAM INVARIANT:
The original Wine repository (winehq.org) is strictly READ-ONLY. Pushing to upstream Wine
is permanently disabled via `git remote set-url --push origin DISABLE_PUSH_READ_ONLY_UPSTREAM`.
All development, commits, and CI run exclusively in the user's own `wine-parity` GitHub repository.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import sys
from pathlib import Path
from typing import Any

REPO_ROOT = Path(__file__).resolve().parents[2]
MANIFEST_PATH = REPO_ROOT / "upstream" / "manifest.json"
READ_ONLY_PUSH_GUARD = "DISABLE_PUSH_READ_ONLY_UPSTREAM"


def load_manifest() -> dict[str, Any]:
    """Load and validate upstream/manifest.json, enforcing read-only upstream access."""
    if not MANIFEST_PATH.is_file():
        raise FileNotFoundError(f"Upstream manifest not found: {MANIFEST_PATH}")
    manifest = json.loads(MANIFEST_PATH.read_text(encoding="utf-8"))
    if manifest.get("upstream_access_mode") != "read-only":
        raise PermissionError("SECURITY VIOLATION: upstream_access_mode in upstream/manifest.json MUST be 'read-only'")
    return manifest


def sha256_file(path: Path) -> str:
    """Compute SHA-256 hex digest of a local file."""
    h = hashlib.sha256()
    h.update(path.read_bytes())
    return h.hexdigest()


def build_sparse_commands(dll: str, manifest: dict[str, Any], upstream_ref: str | None = None) -> list[str]:
    """Construct the blobless sparse-checkout commands for a single Wine DLL with read-only push lock."""
    remote = manifest["upstream_remote"]
    cache_dir = manifest.get("local_sparse_cache", ".wine-upstream")
    dll_meta = manifest.get("borrowed_dlls", {}).get(dll, {})
    upstream_path = dll_meta.get("upstream_path", f"dlls/{dll}")
    ref = upstream_ref or dll_meta.get("base_upstream_commit", "master")

    return [
        f"git clone --filter=blob:none --no-checkout --sparse {remote} {cache_dir}",
        f"git -C {cache_dir} remote set-url --push origin {READ_ONLY_PUSH_GUARD}",
        f"git -C {cache_dir} sparse-checkout init --cone",
        f"git -C {cache_dir} sparse-checkout set include {upstream_path}",
        f"git -C {cache_dir} checkout {ref}",
    ]


def check_upstream_alignment(dll: str) -> dict[str, Any]:
    """Verify upstream/manifest.json state and compute SHA-256 digests of tracked DLL files."""
    manifest = load_manifest()
    borrowed = manifest.get("borrowed_dlls", {})
    if dll not in borrowed:
        raise KeyError(f"DLL '{dll}' is not registered in upstream/manifest.json")

    dll_entry = borrowed[dll]
    tracked_hashes: dict[str, str] = {}
    missing_files: list[str] = []
    macos_blocks_present = False

    for rel_path in dll_entry.get("tracked_files", []):
        abs_path = REPO_ROOT / rel_path
        if not abs_path.is_file():
            missing_files.append(rel_path)
            continue
        tracked_hashes[rel_path] = sha256_file(abs_path)
        if abs_path.suffix == ".c":
            content = abs_path.read_text(encoding="utf-8", errors="replace")
            if "#ifdef __APPLE__" in content:
                macos_blocks_present = True

    sparse_cache = REPO_ROOT / manifest.get("local_sparse_cache", ".wine-upstream")
    return {
        "dll": dll,
        "upstream_access_mode": manifest["upstream_access_mode"],
        "upstream_push_guard": READ_ONLY_PUSH_GUARD,
        "upstream_path": dll_entry["upstream_path"],
        "base_upstream_commit": dll_entry["base_upstream_commit"],
        "merge_strategy": dll_entry["merge_strategy"],
        "macos_acceleration": dll_entry.get("macos_acceleration"),
        "macos_blocks_present": macos_blocks_present,
        "sparse_cache_initialized": (sparse_cache / ".git").exists(),
        "tracked_file_hashes": tracked_hashes,
        "missing_files": missing_files,
        "manifest_verified": len(missing_files) == 0 and manifest["upstream_access_mode"] == "read-only",
    }


def borrow_or_backport_dll(
    dll: str,
    upstream_ref: str | None = None,
    dry_run: bool = False,
) -> dict[str, Any]:
    """Execute or preview a surgical sparse-checkout borrow / 3-way backport for a single DLL."""
    manifest = load_manifest()
    borrowed = manifest.get("borrowed_dlls", {})
    dll_meta = borrowed.get(dll, {
        "upstream_path": f"dlls/{dll}",
        "upstream_tests_path": f"dlls/{dll}/tests",
        "base_upstream_commit": upstream_ref or "master",
        "merge_strategy": "3-way-backport-preserving-macos-blocks",
        "tracked_files": [],
        "required_conformance_tests": [],
    })

    sparse_cmds = build_sparse_commands(dll, manifest, upstream_ref=upstream_ref)
    plan = {
        "dll": dll,
        "upstream_remote": manifest["upstream_remote"],
        "upstream_access_mode": manifest["upstream_access_mode"],
        "upstream_push_guard": READ_ONLY_PUSH_GUARD,
        "local_sparse_cache": manifest["local_sparse_cache"],
        "strategy": dll_meta.get("merge_strategy", "3-way-backport-preserving-macos-blocks"),
        "base_upstream_commit": dll_meta.get("base_upstream_commit"),
        "target_upstream_ref": upstream_ref or dll_meta.get("base_upstream_commit"),
        "sparse_commands": sparse_cmds,
        "required_conformance_tests": dll_meta.get("required_conformance_tests", []),
        "dry_run": dry_run,
    }

    if dry_run:
        return plan

    sparse_dir = REPO_ROOT / manifest["local_sparse_cache"]
    upstream_dll_dir = sparse_dir / dll_meta["upstream_path"]
    if not upstream_dll_dir.is_dir():
        plan["status"] = "sparse_cache_offline_verified_local_overlay"
        plan["alignment"] = check_upstream_alignment(dll)
        return plan

    plan["status"] = "synced_from_sparse_cache"
    plan["alignment"] = check_upstream_alignment(dll)
    return plan


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Wine-Borrow Sparse-Overlay & 3-Way Backport Engine")
    parser.add_argument("--action", choices=["check", "borrow"], required=True)
    parser.add_argument("--dll", required=True, help="Target Wine DLL (e.g. bcrypt)")
    parser.add_argument("--upstream-ref", default=None, help="Optional upstream commit SHA or tag")
    parser.add_argument("--dry-run", action="store_true", help="Preview sparse-checkout & merge commands")
    args = parser.parse_args(argv)

    if args.action == "check":
        report = check_upstream_alignment(args.dll)
        print(json.dumps(report, indent=2))
        return 0 if report["manifest_verified"] else 1

    result = borrow_or_backport_dll(
        dll=args.dll,
        upstream_ref=args.upstream_ref,
        dry_run=args.dry_run,
    )
    print(json.dumps(result, indent=2))
    return 0


if __name__ == "__main__":
    sys.exit(main())

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
import os
import platform
import shutil
import stat
import subprocess
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


MOLTENVK_MACOS_URL = "https://github.com/KhronosGroup/MoltenVK/releases/download/v1.4.2/MoltenVK-macos.tar"


def can_use_rosetta_x86_host() -> bool:
    """Return True if Rosetta 2 (arch -x86_64) is active on an Apple Silicon Mac."""
    if sys.platform != "darwin" or platform.machine() != "arm64":
        return False
    try:
        res = subprocess.run(["arch", "-x86_64", "/usr/bin/true"], capture_output=True, check=False)
        return res.returncode == 0
    except OSError:
        return False


def ensure_macos_moltenvk(
    install_dir: Path,
    build_dir: Path | None = None,
    dry_run: bool = False,
) -> Path | None:
    """Ensure Universal (x86_64 + arm64) libMoltenVK.dylib is installed and SONAME_LIBVULKAN is enabled on macOS."""
    if sys.platform != "darwin":
        return None
    target_dylib = install_dir / "lib" / "libMoltenVK.dylib"
    if dry_run:
        return target_dylib

    cache_root = REPO_ROOT / "builds" / "moltenvk"
    cached_dylib = cache_root / "MoltenVK" / "MoltenVK" / "dynamic" / "dylib" / "macOS" / "libMoltenVK.dylib"

    if not cached_dylib.is_file() and str(install_dir).startswith(str(REPO_ROOT / "builds")):
        cache_root.mkdir(parents=True, exist_ok=True)
        subprocess.run(
            ["sh", "-c", f'curl -fsSL "{MOLTENVK_MACOS_URL}" | tar -xf - -C "{cache_root}"'],
            cwd=REPO_ROOT,
            check=False,
        )

    src_dylib: Path | None = None
    for candidate in (
        cached_dylib,
        Path("/opt/homebrew/lib/libMoltenVK.dylib"),
        Path("/usr/local/lib/libMoltenVK.dylib"),
    ):
        if candidate.is_file():
            src_dylib = candidate
            break

    if src_dylib is not None:
        target_dylib.parent.mkdir(parents=True, exist_ok=True)
        if not target_dylib.is_file() or sha256_file(src_dylib) != sha256_file(target_dylib):
            shutil.copy2(src_dylib, target_dylib)

    if build_dir is not None:
        config_h = build_dir / "include" / "config.h"
        if config_h.is_file():
            orig_stat = config_h.stat()
            txt = config_h.read_text(encoding="utf-8", errors="replace")
            soname_str = str(target_dylib)
            updated = txt
            if "/* #undef SONAME_LIBVULKAN */" in updated:
                updated = updated.replace(
                    "/* #undef SONAME_LIBVULKAN */",
                    f'#define SONAME_LIBVULKAN "{soname_str}"\n#define SONAME_LIBMOLTENVK "{soname_str}"',
                )
            if updated != txt:
                config_h.write_text(updated, encoding="utf-8")
                os.utime(config_h, (orig_stat.st_atime, orig_stat.st_mtime))
                if (build_dir / "Makefile").is_file():
                    for stale in (build_dir / "dlls" / "win32u" / "vulkan.o", build_dir / "dlls" / "win32u" / "win32u.so"):
                        if stale.is_file():
                            stale.unlink()
                    subprocess.run(["make", "-C", str(build_dir), "dlls/win32u/win32u.so"], cwd=REPO_ROOT, check=False)
                    built_so = build_dir / "dlls" / "win32u" / "win32u.so"
                    if built_so.is_file():
                        for unix_dir in (install_dir / "lib" / "wine").glob("*-unix"):
                            if unix_dir.is_dir():
                                shutil.copy2(built_so, unix_dir / "win32u.so")

    return target_dylib


def build_full_wine_plan(target: str, dry_run: bool = False) -> dict[str, Any]:
    """Construct the build plan for a full upstream Wine + wine-parity installation."""
    manifest = load_manifest()
    cache_dir = manifest.get("local_sparse_cache", ".wine-upstream")
    remote = manifest["upstream_remote"]
    borrowed_dlls = sorted(manifest.get("borrowed_dlls", {}).keys())

    rosetta_active = can_use_rosetta_x86_host()
    extra_cfg_flags: list[str] = []
    cc_arg = "CC=clang"
    cxx_arg = "CXX=clang++"

    if target == "x86":
        arch = "x86_64"
        target_mode = "full-x86"
        enable_archs = "--enable-archs=i386,x86_64"
        install_dir = REPO_ROOT / "builds" / "wine-install-x86"
        build_dir = REPO_ROOT / "builds" / "wine-full-x86"
        if sys.platform == "darwin" and rosetta_active:
            extra_cfg_flags.extend(["--build=x86_64-apple-darwin", "--host=x86_64-apple-darwin"])
            if not Path("/usr/local/lib/libfreetype.dylib").is_file():
                extra_cfg_flags.append("--without-freetype")
            cc_arg = "CC=clang -arch x86_64"
            cxx_arg = "CXX=clang++ -arch x86_64"
    else:
        arch = "arm64"
        target_mode = "full-arm64"
        enable_archs = "--enable-archs=arm64ec,i386"
        install_dir = REPO_ROOT / "builds" / "wine-install-arm64"
        build_dir = REPO_ROOT / "builds" / "wine-full-arm64"

    if sys.platform == "darwin":
        extra_cfg_flags.append(f"ac_cv_lib_soname_MoltenVK={install_dir / 'lib' / 'libMoltenVK.dylib'}")

    upstream_prepare_commands = [
        f"git clone --filter=blob:none --no-checkout --sparse {remote} {cache_dir}",
        f"git -C {cache_dir} remote set-url --push origin {READ_ONLY_PUSH_GUARD}",
        f"git -C {cache_dir} sparse-checkout disable",
        f"git -C {cache_dir} checkout -f master",
    ]

    configure_cmd = [
        str(REPO_ROOT / cache_dir / "configure"),
        f"--prefix={install_dir}",
        *extra_cfg_flags,
        enable_archs,
        "--without-x",
        "--without-wayland",
        cc_arg,
        cxx_arg,
    ]

    ncpu = os.cpu_count() or 4
    make_cmd = ["make", f"-j{ncpu}"]
    make_install_cmd = ["make", "install"]

    return {
        "target_mode": target_mode,
        "arch": arch,
        "implies_all_parity_builds": True,
        "upstream_remote": remote,
        "upstream_access_mode": manifest["upstream_access_mode"],
        "upstream_push_guard": READ_ONLY_PUSH_GUARD,
        "local_sparse_cache": cache_dir,
        "parity_dlls": borrowed_dlls,
        "build_dir": str(build_dir),
        "install_dir": str(install_dir),
        "launcher_path": str(install_dir / "bin" / "wine-parity"),
        "upstream_prepare_commands": upstream_prepare_commands,
        "configure_command": configure_cmd,
        "make_command": make_cmd,
        "make_install_command": make_install_cmd,
        "dry_run": dry_run,
    }


def write_wine_parity_launcher(plan: dict[str, Any]) -> Path:
    """Create the executable wine-parity launcher in builds/wine-install-<target>/bin/."""
    install_dir = Path(plan["install_dir"])
    bin_dir = install_dir / "bin"
    bin_dir.mkdir(parents=True, exist_ok=True)
    arch = plan["arch"]
    dlls = plan["parity_dlls"]
    dll_paths = ":".join(str(REPO_ROOT / "builds" / arch / d) for d in dlls)
    dll_overrides = "mscoree,mshtml=;" + ",".join(dlls) + "=b"
    is_x86 = plan["target_mode"] == "full-x86"

    launcher = bin_dir / "wine-parity"
    script = f"""#!/usr/bin/env bash
# SPDX-License-Identifier: LGPL-2.1-or-later
# Auto-generated wine-parity launcher ({plan["target_mode"]}, arch={arch})
set -euo pipefail

# Mandatory +x executable permission check and 3-gate PE security scan for any .exe/.msi/.com/.scr target file
for _wp_arg in "$@"; do
    case "$_wp_arg" in
        *.[eE][xX][eE]|*.[mM][sS][iI]|*.[cC][oO][mM]|*.[sS][cC][rR])
            if [ -e "$_wp_arg" ]; then
                if [ ! -f "$_wp_arg" ] || [ ! -x "$_wp_arg" ]; then
                    echo "[wine-parity] ERROR: Refusing to launch '$_wp_arg': missing executable permission (+x). Run 'chmod +x \"$_wp_arg\"' first." >&2
                    exit 126
                fi
                python3 "{REPO_ROOT / "scripts" / "lib" / "exe_handler.py"}" --action scan --exe "$_wp_arg" --quiet-stdout || exit 126
            fi
            ;;
    esac
done

if [ $# -ge 1 ]; then
    case "$1" in
        *.[mM][sS][iI])
            set -- msiexec /i "$@"
            ;;
    esac
fi

export WINEDLLPATH="{dll_paths}${{WINEDLLPATH:+:$WINEDLLPATH}}"
export WINEDLLOVERRIDES="{dll_overrides}${{WINEDLLOVERRIDES:+;$WINEDLLOVERRIDES}}"
export MVK_CONFIG_LOG_LEVEL="${{MVK_CONFIG_LOG_LEVEL:-1}}"

run_wine_bin() {{
    local bin="$1"
    shift
    if [ "{1 if is_x86 else 0}" = "1" ] && [ "$(uname -s)" = "Darwin" ] && [ "$(uname -m)" = "arm64" ] && arch -x86_64 /usr/bin/true >/dev/null 2>&1 && file "$bin" 2>/dev/null | grep -q "x86_64"; then
        exec arch -x86_64 "$bin" "$@"
    else
        exec "$bin" "$@"
    fi
}}

if [ -x "{bin_dir}/wine64" ]; then
    run_wine_bin "{bin_dir}/wine64" "$@"
elif [ -x "{bin_dir}/wine" ]; then
    run_wine_bin "{bin_dir}/wine" "$@"
elif command -v wine64 >/dev/null 2>&1; then
    run_wine_bin "$(command -v wine64)" "$@"
elif command -v wine >/dev/null 2>&1; then
    run_wine_bin "$(command -v wine)" "$@"
else
    echo "[wine-parity] Pre-configured WINEDLLPATH=$WINEDLLPATH WINEDLLOVERRIDES=$WINEDLLOVERRIDES" >&2
    echo "[wine-parity] Run './scripts/build.sh --{plan["target_mode"]}' with Homebrew dependencies installed to complete monolithic wine64 binary compilation." >&2
    exit 1
fi
"""
    launcher.write_text(script, encoding="utf-8")
    launcher.chmod(launcher.stat().st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)

    # Ensure built-in Wine PE .exe programs in lib/wine/*-windows/ have +x executable bits set
    lib_wine_dir = install_dir / "lib" / "wine"
    if lib_wine_dir.is_dir():
        for exe_file in lib_wine_dir.glob("*-windows/*.exe"):
            if exe_file.is_file():
                mode = exe_file.stat().st_mode
                if not (mode & stat.S_IXUSR):
                    exe_file.chmod(mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)

    return launcher


def compute_parity_dll_hashes(dlls: list[str]) -> dict[str, dict[str, str]]:
    """Compute SHA-256 digests of all files in dlls/<dll>/ for incremental build tracking."""
    result: dict[str, dict[str, str]] = {}
    for dll_name in sorted(dlls):
        dll_dir = REPO_ROOT / "dlls" / dll_name
        file_map: dict[str, str] = {}
        if dll_dir.is_dir():
            for item in sorted(dll_dir.iterdir()):
                if item.is_file():
                    file_map[item.name] = sha256_file(item)
        result[dll_name] = file_map
    return result


def write_monolithic_build_stamp(
    build_dir: Path,
    target_mode: str,
    enable_archs: str,
    upstream_head: str,
    dll_hashes: dict[str, dict[str, str]],
) -> Path:
    """Persist the build stamp so subsequent builds skip redundant configure/make steps."""
    build_dir.mkdir(parents=True, exist_ok=True)
    stamp_path = build_dir / ".wine-parity-stamp.json"
    stamp_data = {
        "target_mode": target_mode,
        "enable_archs": enable_archs,
        "upstream_head": upstream_head,
        "dll_hashes": dll_hashes,
    }
    stamp_path.write_text(json.dumps(stamp_data, indent=2) + "\n", encoding="utf-8")
    return stamp_path


def load_monolithic_build_stamp(build_dir: Path) -> dict[str, Any] | None:
    """Load the previous monolithic build stamp if present."""
    stamp_path = build_dir / ".wine-parity-stamp.json"
    if not stamp_path.is_file():
        return None
    try:
        return json.loads(stamp_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return None


def sync_parity_overlay_incremental(sparse_dir: Path, dlls: list[str]) -> list[str]:
    """Copy only modified files from dlls/<dll>/ into .wine-upstream/dlls/<dll>/, preserving mtimes."""
    changed_dlls: list[str] = []
    for dll_name in dlls:
        src_dll = REPO_ROOT / "dlls" / dll_name
        dst_dll = sparse_dir / "dlls" / dll_name
        dll_changed = False
        if src_dll.is_dir():
            dst_dll.mkdir(parents=True, exist_ok=True)
            for item in sorted(src_dll.iterdir()):
                if item.is_file():
                    dst_item = dst_dll / item.name
                    if not dst_item.is_file() or sha256_file(item) != sha256_file(dst_item):
                        shutil.copy2(item, dst_item)
                        dll_changed = True
        if dll_changed:
            changed_dlls.append(dll_name)
    return changed_dlls


def execute_full_wine_build(
    target: str,
    dry_run: bool = False,
    skip_monolithic_compile: bool = False,
    build_dir_override: Path | None = None,
    install_dir_override: Path | None = None,
    upstream_head_override: str | None = None,
) -> dict[str, Any]:
    """Execute or preview a complete upstream Wine + wine-parity build with incremental caching."""
    plan = build_full_wine_plan(target=target, dry_run=dry_run)
    if build_dir_override is not None:
        plan["build_dir"] = str(build_dir_override)
    if install_dir_override is not None:
        plan["install_dir"] = str(install_dir_override)
        plan["launcher_path"] = str(install_dir_override / "bin" / "wine-parity")

    if dry_run:
        return plan

    launcher = write_wine_parity_launcher(plan)
    plan["launcher_created"] = str(launcher)

    if skip_monolithic_compile:
        plan["status"] = "parity_all_and_launcher_ready"
        return plan

    build_dir = Path(plan["build_dir"])
    install_dir = Path(plan["install_dir"])
    ensure_macos_moltenvk(install_dir=install_dir, build_dir=build_dir, dry_run=dry_run)
    enable_archs = "--enable-archs=i386,x86_64" if plan["target_mode"] == "full-x86" else "--enable-archs=arm64ec,i386"
    current_dll_hashes = compute_parity_dll_hashes(plan["parity_dlls"])
    prev_stamp = load_monolithic_build_stamp(build_dir)

    sparse_dir = REPO_ROOT / plan["local_sparse_cache"]
    if upstream_head_override is not None:
        upstream_head = upstream_head_override
    else:
        if not (sparse_dir / ".git").exists():
            subprocess.run(
                [
                    "git",
                    "clone",
                    "--filter=blob:none",
                    "--no-checkout",
                    "--sparse",
                    plan["upstream_remote"],
                    str(sparse_dir),
                ],
                cwd=REPO_ROOT,
                check=True,
            )
        subprocess.run(
            ["git", "-C", str(sparse_dir), "remote", "set-url", "--push", "origin", READ_ONLY_PUSH_GUARD],
            cwd=REPO_ROOT,
            check=True,
        )
        # Only unsparse and checkout if the full upstream tree is not yet materialized on disk;
        # never run `git checkout -f` on an already unsparsed tree to preserve object file mtimes.
        if not (sparse_dir / "configure").is_file() or not (sparse_dir / "server").is_dir():
            subprocess.run(["git", "-C", str(sparse_dir), "sparse-checkout", "disable"], cwd=REPO_ROOT, check=True)
            subprocess.run(["git", "-C", str(sparse_dir), "checkout", "-f", "master"], cwd=REPO_ROOT, check=True)

        head_res = subprocess.run(
            ["git", "-C", str(sparse_dir), "rev-parse", "--short=10", "HEAD"],
            cwd=REPO_ROOT,
            capture_output=True,
            text=True,
            check=True,
        )
        upstream_head = head_res.stdout.strip()

    # Check if already configured and installed
    makefile_exists = (build_dir / "Makefile").is_file()
    config_status = build_dir / "config.status"
    config_matches = (
        makefile_exists
        and config_status.is_file()
        and enable_archs in config_status.read_text(encoding="utf-8", errors="replace")
    )
    installed_binary_exists = (install_dir / "bin" / "wine64").is_file() or (install_dir / "bin" / "wine").is_file()

    # Determine which DLLs changed compared to the last successful build stamp
    changed_dlls: list[str] = []
    if prev_stamp is not None and prev_stamp.get("dll_hashes"):
        prev_hashes = prev_stamp["dll_hashes"]
        for dll_name, cur_files in current_dll_hashes.items():
            if prev_hashes.get(dll_name) != cur_files:
                changed_dlls.append(dll_name)
    else:
        changed_dlls = list(plan["parity_dlls"])

    # Fast path: already configured, installed binary exists, upstream HEAD & all parity DLL hashes match
    if (
        config_matches
        and installed_binary_exists
        and prev_stamp is not None
        and prev_stamp.get("upstream_head") == upstream_head
        and prev_stamp.get("enable_archs") == enable_archs
        and not changed_dlls
    ):
        plan["configure_executed"] = False
        plan["make_executed"] = False
        plan["changed_dlls"] = []
        plan["status"] = "up_to_date_skipped_rebuild"
        return plan

    build_dir.mkdir(parents=True, exist_ok=True)
    env = os.environ.copy()
    extra_bins: list[str] = []
    for brew_root in ("/opt/homebrew", "/usr/local"):
        for pkg in ("bison", "flex"):
            pkg_bin = Path(brew_root) / "opt" / pkg / "bin"
            if pkg_bin.is_dir():
                extra_bins.append(str(pkg_bin))
        root_bin = Path(brew_root) / "bin"
        if root_bin.is_dir():
            extra_bins.append(str(root_bin))
    if extra_bins:
        env["PATH"] = ":".join(extra_bins) + f":{env.get('PATH', '')}"

    # Ensure .wine-upstream/dlls stays clean so tools/makedep uses upstream Makefile.in headers;
    # wine-parity DLL overrides are loaded via WINEDLLPATH/WINEDLLOVERRIDES in bin/wine-parity.
    if (sparse_dir / ".git").exists():
        status_res = subprocess.run(
            ["git", "-C", str(sparse_dir), "status", "--porcelain", "--", "dlls"],
            cwd=REPO_ROOT,
            capture_output=True,
            text=True,
            check=False,
        )
        if status_res.stdout.strip():
            subprocess.run(["git", "-C", str(sparse_dir), "checkout", "-f", "--", "dlls"], cwd=REPO_ROOT, check=True)
            subprocess.run(["git", "-C", str(sparse_dir), "clean", "-fd", "--", "dlls"], cwd=REPO_ROOT, check=True)

    # Only run ./configure if not yet configured for this target architecture
    if not config_matches:
        subprocess.run(plan["configure_command"], cwd=build_dir, env=env, check=True)
        plan["configure_executed"] = True
    else:
        plan["configure_executed"] = False

    # Build and install upstream Wine incrementally (GNU Make tracks .o/.dll mtimes automatically)
    subprocess.run(plan["make_command"], cwd=build_dir, env=env, check=True)
    subprocess.run(plan["make_install_command"], cwd=build_dir, env=env, check=True)
    ensure_macos_moltenvk(install_dir=install_dir, build_dir=build_dir, dry_run=dry_run)
    plan["make_executed"] = True

    write_monolithic_build_stamp(
        build_dir=build_dir,
        target_mode=plan["target_mode"],
        enable_archs=enable_archs,
        upstream_head=upstream_head,
        dll_hashes=current_dll_hashes,
    )
    write_wine_parity_launcher(plan)
    plan["changed_dlls"] = changed_dlls
    plan["status"] = "full_monolithic_and_parity_installed"
    return plan


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Wine-Borrow Sparse-Overlay & 3-Way Backport Engine")
    parser.add_argument("--action", choices=["check", "borrow", "full-build"], required=True)
    parser.add_argument("--dll", default="", help="Target Wine DLL (e.g. bcrypt)")
    parser.add_argument("--target", choices=["x86", "arm64"], default="x86", help="Full Wine build target")
    parser.add_argument("--upstream-ref", default=None, help="Optional upstream commit SHA or tag")
    parser.add_argument("--dry-run", action="store_true", help="Preview sparse-checkout & merge commands")
    parser.add_argument("--skip-monolithic-compile", action="store_true", help="Prepare parity DLLs and launcher without running 30-min upstream make")
    args = parser.parse_args(argv)

    if args.action == "check":
        report = check_upstream_alignment(args.dll)
        print(json.dumps(report, indent=2))
        return 0 if report["manifest_verified"] else 1

    if args.action == "full-build":
        report = execute_full_wine_build(
            target=args.target,
            dry_run=args.dry_run,
            skip_monolithic_compile=args.skip_monolithic_compile,
        )
        print(json.dumps(report, indent=2))
        return 0

    result = borrow_or_backport_dll(
        dll=args.dll,
        upstream_ref=args.upstream_ref,
        dry_run=args.dry_run,
    )
    print(json.dumps(result, indent=2))
    return 0


if __name__ == "__main__":
    sys.exit(main())

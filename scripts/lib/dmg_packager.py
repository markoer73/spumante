#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Turnkey macOS DMG installer packager for Spumante.app and Spumante Config.app."""

from __future__ import annotations

import argparse
import json
import os
import shutil
import stat
import struct
import subprocess
import sys
from pathlib import Path
from typing import Any

from exe_handler import (
    ALL_BUILTIN_APPS,
    APP_BUNDLE_DIR,
    BUILTIN_CLI_APPS,
    BUILTIN_GUI_APPS,
    BUNDLE_ID,
    CONFIG_APP_BUNDLE_DIR,
    CONFIG_BUNDLE_ID,
    REPO_ROOT,
    build_macos_exe_handler,
    ensure_builtin_windows_apps,
)

DMG_STAGING_DIR = REPO_ROOT / "builds" / "dmg_staging"
DMG_OUTPUT_PATH = REPO_ROOT / "builds" / "Spumante-Installer.dmg"
VOLUME_NAME = "Spumante"
RUNTIME_CACHE_ROOT = REPO_ROOT / "builds" / "runtime-cache"

# Developer-only payload that must never ship inside the DMG
RUNTIME_PRUNE_DIRS = ("include", "share/man", "share/applications")
RUNTIME_DEV_TOOLS = (
    "function_grep.pl",
    "widl",
    "winebuild",
    "winecpp",
    "winedump",
    "wineg++",
    "winegcc",
    "winemaker",
    "wmc",
    "wrc",
    "wine-parity",
)
MACHO_MAGICS = (b"\xfe\xed\xfa\xce", b"\xfe\xed\xfa\xcf", b"\xce\xfa\xed\xfe", b"\xcf\xfa\xed\xfe", b"\xca\xfe\xba\xbe")


def _forbidden_path_needles() -> list[bytes]:
    """Byte strings that must never appear in the DMG payload (build-machine and user specific paths)."""
    needles = {str(REPO_ROOT).encode(), str(Path.home()).encode(), b"wine-parity"}
    return sorted(needles, key=len, reverse=True)


def scan_portability_violations(root: Path, limit: int = 50) -> list[str]:
    """Return files under root that embed build-machine/user-specific paths."""
    needles = _forbidden_path_needles()
    offenders: list[str] = []
    for dirpath, _dirs, files in os.walk(root):
        for fname in files:
            fpath = Path(dirpath) / fname
            if fpath.is_symlink() or not fpath.is_file():
                continue
            try:
                blob = fpath.read_bytes()
            except OSError:
                continue
            for needle in needles:
                if needle in blob:
                    offenders.append(f"{fpath.relative_to(root)}: {needle.decode(errors='replace')}")
                    break
            if len(offenders) >= limit:
                return offenders
    return offenders


def _clone_tree(src: Path, dst: Path) -> None:
    """Copy a directory tree, using APFS clones (instant, no extra disk space) when available."""
    if dst.exists():
        shutil.rmtree(dst)
    dst.parent.mkdir(parents=True, exist_ok=True)
    if sys.platform == "darwin":
        res = subprocess.run(["cp", "-cR", str(src), str(dst)], capture_output=True, check=False)
        if res.returncode == 0:
            return
        if dst.exists():
            shutil.rmtree(dst)
    shutil.copytree(src, dst, symlinks=True)


def _runtime_fingerprint(src: Path) -> dict[str, Any]:
    count = 0
    newest = 0.0
    for dirpath, _dirs, files in os.walk(src):
        for fname in files:
            try:
                st = (Path(dirpath) / fname).lstat()
            except OSError:
                continue
            count += 1
            newest = max(newest, st.st_mtime)
    return {"file_count": count, "newest_mtime": newest, "schema": 2}


def _classify_binary(path: Path) -> tuple[str, int]:
    """Return ("macho", 0), ("pe", machine) or ("", 0)."""
    try:
        with path.open("rb") as f:
            head = f.read(4096)
    except OSError:
        return "", 0
    if head[:4] in MACHO_MAGICS:
        return "macho", 0
    if len(head) >= 0x40 and head[:2] == b"MZ":
        e_lfanew = struct.unpack_from("<I", head, 0x3C)[0]
        if e_lfanew + 6 <= len(head) and head[e_lfanew : e_lfanew + 4] == b"PE\x00\x00":
            return "pe", struct.unpack_from("<H", head, e_lfanew + 4)[0]
    return "", 0


def _run_batched(cmd_prefix: list[str], files: list[Path], batch: int = 200) -> None:
    for i in range(0, len(files), batch):
        subprocess.run([*cmd_prefix, *[str(f) for f in files[i : i + batch]]], capture_output=True, check=False)


def _pe_strip_tools() -> dict[int, str | None]:
    llvm_strip = shutil.which("llvm-strip") or str(Path("/opt/homebrew/opt/llvm/bin/llvm-strip"))
    return {
        0x8664: shutil.which("x86_64-w64-mingw32-strip"),
        0x014C: shutil.which("i686-w64-mingw32-strip"),
        0xAA64: llvm_strip,
        0xA641: llvm_strip,
    }


def strip_pe_debug_info(files: list[Path]) -> int:
    """Strip debug sections (absolute build paths) from PE files, preserving Wine's builtin signature."""
    tools = _pe_strip_tools()
    by_tool: dict[str, list[Path]] = {}
    markers: dict[Path, tuple[bytes, bytes]] = {}
    for fpath in files:
        kind, machine = _classify_binary(fpath)
        tool = tools.get(machine) if kind == "pe" else None
        if not tool or not Path(tool).is_file():
            continue
        by_tool.setdefault(tool, []).append(fpath)
        raw = fpath.read_bytes()
        markers[fpath] = (raw[0x40:0x60], raw)
    for tool, batch_files in by_tool.items():
        _run_batched([tool, "--strip-debug"], batch_files)
    for fpath, (marker, original) in markers.items():
        if fpath.read_bytes()[0x40:0x60] != marker:
            # Never lose Wine's "Wine builtin DLL" signature: restore the unstripped original
            fpath.write_bytes(original)
    return len(markers)


def _macho_fix_paths(path: Path, runtime_root: Path) -> None:
    """Rewrite absolute install names / dependencies inside the runtime to relocatable @rpath/@loader_path forms."""
    res = subprocess.run(["otool", "-L", str(path)], capture_output=True, text=True, check=False)
    deps = []
    for line in res.stdout.splitlines()[1:]:
        dep = line.strip().split(" (", 1)[0]
        if dep:
            deps.append(dep)
    id_res = subprocess.run(["otool", "-D", str(path)], capture_output=True, text=True, check=False)
    ids = [ln.strip() for ln in id_res.stdout.splitlines()[1:] if ln.strip()]
    changed = False
    for dylib_id in set(ids):
        if dylib_id.startswith("/") and not dylib_id.startswith(("/usr/lib/", "/System/")):
            subprocess.run(["install_name_tool", "-id", f"@rpath/{path.name}", str(path)], capture_output=True, check=False)
            changed = True
    for dep in set(deps):
        if dep in ids or not dep.startswith("/") or dep.startswith(("/usr/lib/", "/System/")):
            continue
        dep_name = Path(dep).name
        candidates = list(runtime_root.rglob(dep_name))
        if candidates:
            rel = os.path.relpath(candidates[0], path.parent)
            subprocess.run(
                ["install_name_tool", "-change", dep, f"@loader_path/{rel}", str(path)],
                capture_output=True,
                check=False,
            )
            changed = True
    if changed:
        subprocess.run(["codesign", "--force", "--sign", "-", str(path)], capture_output=True, check=False)


def prepare_portable_runtime(target: str) -> Path | None:
    """Produce a pruned, stripped, relocatable copy of builds/wine-install-<target> for the DMG (cached)."""
    src = REPO_ROOT / "builds" / f"wine-install-{target}"
    if not (src / "bin" / "wine").is_file():
        return None
    cache = RUNTIME_CACHE_ROOT / f"wine-install-{target}"
    stamp = RUNTIME_CACHE_ROOT / f"wine-install-{target}.stamp.json"
    fingerprint = _runtime_fingerprint(src)
    if cache.is_dir() and stamp.is_file():
        try:
            if json.loads(stamp.read_text(encoding="utf-8")) == fingerprint:
                return cache
        except (OSError, ValueError):
            pass

    _clone_tree(src, cache)
    for rel in RUNTIME_PRUNE_DIRS:
        shutil.rmtree(cache / rel, ignore_errors=True)
    for tool in RUNTIME_DEV_TOOLS:
        tool_path = cache / "bin" / tool
        if tool_path.is_file() or tool_path.is_symlink():
            tool_path.unlink()
    for static_lib in (cache / "lib" / "wine").glob("*/lib*.a"):
        static_lib.unlink()

    macho_files: list[Path] = []
    pe_files: list[Path] = []
    for dirpath, _dirs, files in os.walk(cache):
        for fname in files:
            fpath = Path(dirpath) / fname
            if fpath.is_symlink():
                continue
            kind, _machine = _classify_binary(fpath)
            if kind == "macho":
                macho_files.append(fpath)
            elif kind == "pe":
                pe_files.append(fpath)

    # Strip debug information (it embeds absolute source/object paths of the build machine)
    strip_pe_debug_info(pe_files)
    if sys.platform == "darwin":
        _run_batched(["strip", "-S"], macho_files)
        for fpath in macho_files:
            _macho_fix_paths(fpath, cache)
        _run_batched(["codesign", "--force", "--sign", "-"], macho_files)

    stamp.parent.mkdir(parents=True, exist_ok=True)
    stamp.write_text(json.dumps(fingerprint), encoding="utf-8")
    return cache


def stage_parity_overlays(resources_dir: Path) -> list[str]:
    """Bundle loadable Spumante parity DLL overlays (Wine .so/.dll builds) under Resources/parity/<arch>/<dll>."""
    staged: list[str] = []
    for arch in ("x86_64", "arm64"):
        arch_dir = REPO_ROOT / "builds" / arch
        if not arch_dir.is_dir():
            continue
        for dll_dir in sorted(p for p in arch_dir.iterdir() if p.is_dir()):
            for item in sorted(dll_dir.rglob("*")):
                if not item.is_file() or item.suffix.lower() not in (".so", ".dll", ".sys", ".drv"):
                    continue
                rel = item.relative_to(arch_dir)
                if any(part.endswith(".dSYM") for part in rel.parts):
                    continue
                dest = resources_dir / "parity" / arch / rel
                dest.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(item, dest)
                if sys.platform == "darwin" and _classify_binary(dest)[0] == "macho":
                    subprocess.run(["strip", "-S", str(dest)], capture_output=True, check=False)
                    _macho_fix_paths(dest, resources_dir)
                    subprocess.run(["codesign", "--force", "--sign", "-", str(dest)], capture_output=True, check=False)
                staged.append(str(Path("parity") / arch / rel))
    return staged


def _generate_installer_command_script() -> str:
    """Generate the double-clickable 'Install Spumante.command' macOS installer script.

    The installer is self-contained: it only uses files next to itself on the disk image and standard macOS
    tools. Configuration is initialized by the native Spumante Config binary (no scripting runtime needed).
    """
    return f"""#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Double-clickable macOS installer for Spumante.app and Spumante Config.app.
# Installs the launcher and configuration program, sets up CLI wrappers,
# registers .exe/.msi LaunchServices associations, initializes config.json (0600)
# and WINEPREFIX registry cascade, and opens the Spumante Config UI.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${{BASH_SOURCE[0]}}")" && pwd)"

TARGET_APPS_DIR=""
TARGET_BIN_DIR=""
CONFIG_FILE="${{SPUMANTE_CONFIG:-$HOME/.config/spumante/config.json}}"
WINEPREFIX_DIR="${{WINEPREFIX:-$HOME/.wine}}"
USER_INSTALL=0
OPEN_CONFIG=1
DRY_RUN=0

usage() {{
    cat <<EOF
Usage: ./Install\\ Spumante.command [options]

Options:
  --target-dir <dir>    Target Applications directory (default: /Applications or ~/Applications)
  --bin-dir <dir>       Directory for CLI wrappers 'spumante' and 'spumante-config'
  --config-file <path>  Path to user config.json (default: ~/.config/spumante/config.json)
  --wineprefix <dir>    Path to Wine prefix for registry cascade (default: ~/.wine)
  --user                Install into ~/Applications and ~/.local/bin without sudo
  --no-open-config      Do not automatically launch Spumante Config.app after install
  --dry-run             Print planned installation steps without modifying the system
  -h, --help            Show this help message
EOF
}}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --target-dir)
            TARGET_APPS_DIR="${{2:-}}"
            shift 2
            ;;
        --bin-dir)
            TARGET_BIN_DIR="${{2:-}}"
            shift 2
            ;;
        --config-file)
            CONFIG_FILE="${{2:-}}"
            shift 2
            ;;
        --wineprefix)
            WINEPREFIX_DIR="${{2:-}}"
            shift 2
            ;;
        --user)
            USER_INSTALL=1
            shift
            ;;
        --no-open-config)
            OPEN_CONFIG=0
            shift
            ;;
        --dry-run)
            DRY_RUN=1
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "[Spumante Installer] Unknown option: $1" >&2
            usage >&2
            exit 2
            ;;
    esac
done

if [[ -z "$TARGET_APPS_DIR" ]]; then
    if [[ "$USER_INSTALL" -eq 1 ]] || [[ ! -w "/Applications" ]]; then
        TARGET_APPS_DIR="$HOME/Applications"
    else
        TARGET_APPS_DIR="/Applications"
    fi
fi

if [[ -z "$TARGET_BIN_DIR" ]]; then
    if [[ "$USER_INSTALL" -eq 1 ]] || [[ ! -w "/usr/local/bin" ]]; then
        TARGET_BIN_DIR="$HOME/.local/bin"
    else
        TARGET_BIN_DIR="/usr/local/bin"
    fi
fi

SRC_LAUNCHER_APP="$SCRIPT_DIR/Spumante.app"
SRC_CONFIG_APP="$SCRIPT_DIR/Spumante Config.app"

if [[ ! -d "$SRC_LAUNCHER_APP" || ! -d "$SRC_CONFIG_APP" ]]; then
    echo "[Spumante Installer] ERROR: Spumante.app and Spumante Config.app must be next to this installer." >&2
    exit 1
fi

DEST_LAUNCHER_APP="$TARGET_APPS_DIR/Spumante.app"
DEST_CONFIG_APP="$TARGET_APPS_DIR/Spumante Config.app"
DEST_LAUNCHER_BIN="$DEST_LAUNCHER_APP/Contents/MacOS/Spumante"
DEST_CONFIG_BIN="$DEST_CONFIG_APP/Contents/MacOS/SpumanteConfig"

is_apple_silicon() {{
    if [[ "$(uname -s)" == "Darwin" ]] && ([[ "$(uname -m)" == "arm64" ]] || [[ "$(sysctl -n hw.optional.arm64 2>/dev/null || echo 0)" == "1" ]]); then
        return 0
    fi
    return 1
}}

is_rosetta2_installed() {{
    if [[ "${{SPUMANTE_SIMULATE_MISSING_ROSETTA:-0}}" == "1" ]]; then
        return 1
    fi
    if ! is_apple_silicon; then
        return 0
    fi
    if [[ -f "/Library/Apple/usr/libexec/oah/libRosettaRuntime" ]] && arch -x86_64 /usr/bin/true >/dev/null 2>&1; then
        return 0
    fi
    return 1
}}

ensure_rosetta2_installed() {{
    if is_rosetta2_installed; then
        return 0
    fi
    local sw_bin="${{SPUMANTE_SOFTWAREUPDATE_BIN:-/usr/sbin/softwareupdate}}"
    local consent="${{SPUMANTE_ROSETTA_CONSENT:-}}"
    if [[ -z "$consent" ]] && [[ "${{SPUMANTE_NONINTERACTIVE_ROSETTA:-0}}" == "1" ]]; then
        consent="yes"
    fi
    echo "[Spumante Installer] Apple Silicon detected without Rosetta 2."
    echo "[Spumante Installer] Rosetta 2 is required to run 32-bit and 64-bit Intel (x86/x86_64) Windows applications."
    if [[ -z "$consent" ]]; then
        if ! read -r -p "[Spumante Installer] Do you agree to install Rosetta 2 now? [y/N]: " consent; then
            consent="no"
        fi
    fi
    case "$consent" in
        [yY]|[yY][eE][sS]|1|[tT][rR][uU][eE]|[aA][gG][rR][eE][eE])
            ;;
        *)
            echo "[Spumante Installer] ERROR: Rosetta 2 installation declined by user. Aborting spumante installation." >&2
            exit 86
            ;;
    esac
    echo "[Spumante Installer] Installing Rosetta 2 for x86/x86_64 Windows app support..."
    if "$sw_bin" --install-rosetta --agree-to-license; then
        echo "[Spumante Installer] Rosetta 2 installation succeeded."
        return 0
    fi
    echo "[Spumante Installer] ERROR: Rosetta 2 is required on Apple Silicon to run 32/64-bit Intel (x86/x86_64) Windows applications." >&2
    echo "[Spumante Installer] Run '/usr/sbin/softwareupdate --install-rosetta --agree-to-license' and re-run this installer." >&2
    exit 86
}}

if [[ "$DRY_RUN" -eq 1 ]]; then
    ROSETTA_READY="false"
    if is_rosetta2_installed; then
        ROSETTA_READY="true"
    fi
    cat <<EOF
{{
  "action": "install-spumante",
  "dry_run": true,
  "target_applications_dir": "$TARGET_APPS_DIR",
  "installed_launcher_app": "$DEST_LAUNCHER_APP",
  "installed_config_app": "$DEST_CONFIG_APP",
  "target_bin_dir": "$TARGET_BIN_DIR",
  "config_file": "$CONFIG_FILE",
  "wineprefix": "$WINEPREFIX_DIR",
  "rosetta2_check_enabled": true,
  "rosetta2_requires_user_consent": true,
  "rosetta2_installed": $ROSETTA_READY,
  "rosetta2_install_command": "/usr/sbin/softwareupdate --install-rosetta --agree-to-license"
}}
EOF
    exit 0
fi

echo "=================================================================="
echo "  Spumante macOS Installer & Configuration Setup"
echo "=================================================================="
echo "  Applications Directory : $TARGET_APPS_DIR"
echo "  CLI Wrapper Directory  : $TARGET_BIN_DIR"
echo "  User Config File       : $CONFIG_FILE"
echo "  Wine Prefix            : $WINEPREFIX_DIR"
echo "------------------------------------------------------------------"

ensure_rosetta2_installed

mkdir -p "$TARGET_APPS_DIR"
mkdir -p "$TARGET_BIN_DIR"

rm -rf "$DEST_LAUNCHER_APP" "$DEST_CONFIG_APP"
ditto "$SRC_LAUNCHER_APP" "$DEST_LAUNCHER_APP"
ditto "$SRC_CONFIG_APP" "$DEST_CONFIG_APP"
chmod +x "$DEST_LAUNCHER_BIN" "$DEST_CONFIG_BIN"

cat > "$TARGET_BIN_DIR/spumante" <<WRAPPER_EOF
#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
exec "$DEST_LAUNCHER_BIN" "\\$@"
WRAPPER_EOF
chmod +x "$TARGET_BIN_DIR/spumante"

cat > "$TARGET_BIN_DIR/spumante-config" <<WRAPPER_CFG_EOF
#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
exec "$DEST_CONFIG_BIN" "\\$@"
WRAPPER_CFG_EOF
chmod +x "$TARGET_BIN_DIR/spumante-config"

# Register .exe and .msi file associations with macOS LaunchServices
"$DEST_LAUNCHER_BIN" --register >/dev/null 2>&1 || true

# Initialize user-level configuration (0600) and cascade to WINEPREFIX registry (native, no scripting runtime)
SPUMANTE_CONFIG="$CONFIG_FILE" WINEPREFIX="$WINEPREFIX_DIR" \\
    "$DEST_CONFIG_BIN" --config-status >/dev/null

echo "[Spumante Installer] Installed:"
echo "  1. $DEST_LAUNCHER_APP (Automatic .exe / .msi Launcher & Security Scanner)"
echo "  2. $DEST_CONFIG_APP (Native macOS Spumante Configuration Program)"
echo "  3. $TARGET_BIN_DIR/spumante"
echo "  4. $TARGET_BIN_DIR/spumante-config"

if [[ "$OPEN_CONFIG" -eq 1 ]]; then
    echo "[Spumante Installer] Opening Spumante Config.app..."
    if command -v open >/dev/null 2>&1; then
        open "$DEST_CONFIG_APP" || "$DEST_CONFIG_BIN" &
    else
        "$DEST_CONFIG_BIN" &
    fi
fi

echo "[Spumante Installer] Installation complete!"
"""


def _generate_dmg_readme() -> str:
    return """Spumante for macOS — Turnkey Installer & Configuration Package
===================================================================

Contents of this Disk Image:
  1. Spumante.app
     - Native macOS .exe / .msi launcher & LaunchServices handler.
     - Automatically routes 32/64-bit x86 PE binaries and ARM64/ARM64EC PE
       binaries, enforces the mandatory +x executable bit and 3-gate PE
       security scanner (Gate 1 Quarantine, Gate 2 Authenticode, Gate 3
       Static W^X/IAT + VirusTotal v3), and runs inside the macOS Seatbelt
       sandbox.

  2. Spumante Config.app
     - Standalone native macOS AppKit Settings application ("Spumante Settings").
     - Run this program anytime from /Applications or Spotlight to configure:
       * General: Windows version (Windows 11 / 10 / 8.1 / 7), Retina/HiDPI mode,
         and one-click .exe / .msi file handler registration.
       * Security: Pre-execution PE security gates and VirusTotal v3 cloud hash
         reputation (enable toggle + 0600-protected API key input).
       * Sandbox & Network: macOS Seatbelt containment, Z: drive stripping,
         sensitive host path protection (~/.ssh, ~/.aws, ~/.gnupg, Keychains),
         and incoming/outgoing firewall rules.
       * Graphics & Acceleration: IOSurface-backed CAMetalLayer swapchains,
         VideoToolbox hardware H.264/HEVC decode, and ARM64EC WJTC JIT cache.

  3. Install Spumante.command
     - Double-click to automatically install both Spumante.app and
       Spumante Config.app into /Applications (or ~/Applications), install
       the 'spumante' and 'spumante-config' CLI commands, initialize
       ~/.config/spumante/config.json (0600) + ~/.wine registry cascade,
       and open Spumante Config.app.

Quick Installation:
  - Option A (Recommended): Double-click "Install Spumante.command".
  - Option B (Drag & Drop): Drag "Spumante.app" and "Spumante Config.app"
    onto the "Applications" folder shortcut, then open "Spumante Config.app"
    to configure your Spumante environment.

Licensing:
  - Spumante Launcher, Menu Bar Tray App, Settings UI, Security Scanner &
    Sandbox Tooling: Apache License, Version 2.0 (see LICENSE and NOTICE).
  - Bundled Wine Runtime & Wine DLL Parity Overlays: GNU Lesser General
    Public License v2.1 or later (see LICENSE-LGPL-2.1 and NOTICE).
"""


def _detect_codesign_identity(explicit_identity: str | None = None) -> tuple[str, str]:
    """Resolve Developer ID Application signing identity (returning (sign_token, display_name))."""
    raw_explicit = (explicit_identity or "").strip()
    if not raw_explicit:
        for env_key in ("SPUMANTE_CODESIGN_IDENTITY", "CODESIGN_IDENTITY"):
            val = os.environ.get(env_key, "").strip()
            if val:
                raw_explicit = val
                break

    if sys.platform == "darwin" and shutil.which("security"):
        res = subprocess.run(
            ["security", "find-identity", "-v", "-p", "codesigning"],
            capture_output=True,
            text=True,
            check=False,
        )
        for line in res.stdout.splitlines():
            target_match = raw_explicit if raw_explicit else "Developer ID Application:"
            if target_match in line and '"' in line:
                tokens = line.strip().split()
                sha1_token = tokens[1] if len(tokens) >= 2 and len(tokens[1]) == 40 else ""
                parts = line.split('"')
                cn_name = parts[1].strip() if len(parts) >= 2 else ""
                if sha1_token and cn_name:
                    return sha1_token, raw_explicit or f"{cn_name} ({sha1_token})"
                if cn_name:
                    return cn_name, raw_explicit or cn_name

    if raw_explicit:
        return raw_explicit, raw_explicit
    return "-", "-"


def _resolve_appstoreconnect_api_credentials() -> tuple[str, str, str] | None:
    """Discover App Store Connect API (.p8) key path, key ID, and issuer ID from env or ~/.appstoreconnect."""
    key_path_str = os.environ.get("APP_STORE_CONNECT_API_KEY_PATH", "").strip()
    key_id = os.environ.get("APP_STORE_CONNECT_API_KEY_ID", "").strip()
    issuer_id = os.environ.get("APP_STORE_CONNECT_ISSUER_ID", "").strip()

    asc_dir = Path.home() / ".appstoreconnect"
    if not issuer_id:
        issuer_file = asc_dir / "issuer_id"
        if issuer_file.is_file():
            issuer_id = issuer_file.read_text(encoding="utf-8").strip()

    key_path: Path | None = Path(key_path_str) if key_path_str else None
    if not key_path or not key_path.is_file():
        candidates = sorted(asc_dir.rglob("AuthKey_*.p8")) if asc_dir.is_dir() else []
        if candidates:
            key_path = candidates[0]

    if key_path and key_path.is_file() and not key_id:
        stem = key_path.stem
        if stem.startswith("AuthKey_"):
            key_id = stem[len("AuthKey_") :]

    if key_path and key_path.is_file() and key_id and issuer_id:
        return str(key_path), key_id, issuer_id
    return None


def build_dmg_package(
    dry_run: bool = False,
    notarize: bool = False,
    codesign_identity: str | None = None,
    notary_profile: str | None = None,
) -> dict[str, Any]:
    """Build Spumante.app, SpumanteConfig.app, stage the DMG directory, and create (and optionally sign & notarize) Spumante-Installer.dmg."""
    handler_info = build_macos_exe_handler(dry_run=dry_run)

    staged_launcher_app = DMG_STAGING_DIR / "Spumante.app"
    staged_config_app = DMG_STAGING_DIR / "Spumante Config.app"
    staged_windows_apps = DMG_STAGING_DIR / "Windows Apps"
    installer_script = DMG_STAGING_DIR / "Install Spumante.command"
    apps_symlink = DMG_STAGING_DIR / "Applications"
    readme_file = DMG_STAGING_DIR / "README.txt"
    dmg_format = "UDZO"

    sign_token, resolved_identity = _detect_codesign_identity(codesign_identity)
    explicit_profile = (
        (notary_profile or "").strip()
        or os.environ.get("SPUMANTE_NOTARY_PROFILE", "").strip()
        or os.environ.get("NOTARYTOOL_PROFILE", "").strip()
    )
    resolved_profile = explicit_profile or "spumante-notary"
    asc_creds = _resolve_appstoreconnect_api_credentials()

    apple_id = (
        os.environ.get("SPUMANTE_APPLE_ID", "").strip()
        or os.environ.get("APPLE_ID", "").strip()
    )
    team_id = (
        os.environ.get("SPUMANTE_TEAM_ID", "").strip()
        or os.environ.get("APPLE_TEAM_ID", "").strip()
    )
    app_password = (
        os.environ.get("SPUMANTE_APP_PASSWORD", "").strip()
        or os.environ.get("APPLE_APP_SPECIFIC_PASSWORD", "").strip()
    )

    signed_targets = [
        str(staged_launcher_app),
        str(staged_config_app),
        str(DMG_OUTPUT_PATH),
    ]
    if not explicit_profile and asc_creds is not None:
        asc_key_path, asc_key_id, asc_issuer_id = asc_creds
        notarytool_cmd = [
            "xcrun",
            "notarytool",
            "submit",
            str(DMG_OUTPUT_PATH),
            "--key",
            asc_key_path,
            "--key-id",
            asc_key_id,
            "--issuer",
            asc_issuer_id,
            "--wait",
            "--output-format",
            "json",
        ]
    elif apple_id and team_id and app_password:
        notarytool_cmd = [
            "xcrun",
            "notarytool",
            "submit",
            str(DMG_OUTPUT_PATH),
            "--apple-id",
            apple_id,
            "--team-id",
            team_id,
            "--password",
            "@env:SPUMANTE_APP_PASSWORD",
            "--wait",
            "--output-format",
            "json",
        ]
    else:
        notarytool_cmd = [
            "xcrun",
            "notarytool",
            "submit",
            str(DMG_OUTPUT_PATH),
            "--keychain-profile",
            resolved_profile,
            "--wait",
            "--output-format",
            "json",
        ]
    stapler_cmd = ["xcrun", "stapler", "staple", str(DMG_OUTPUT_PATH)]
    stapler_validate_cmd = ["xcrun", "stapler", "validate", str(DMG_OUTPUT_PATH)]
    notarization_status = "disabled"
    dmg_create_errors: list[str] = []
    notary_submission_output: dict[str, Any] = {}
    bundled_runtimes: list[str] = []
    bundled_parity: list[str] = []
    portability_violations: list[str] = []

    if not dry_run:
        if DMG_STAGING_DIR.exists():
            shutil.rmtree(DMG_STAGING_DIR)
        DMG_STAGING_DIR.mkdir(parents=True, exist_ok=True)

        shutil.copytree(APP_BUNDLE_DIR, staged_launcher_app, symlinks=True)
        shutil.copytree(CONFIG_APP_BUNDLE_DIR, staged_config_app, symlinks=True)

        # Bundle the relocatable Wine runtime(s) and parity overlays inside Spumante.app
        resources_dir = staged_launcher_app / "Contents" / "Resources"
        for runtime_target in ("x86", "arm64"):
            runtime_cache = prepare_portable_runtime(runtime_target)
            if runtime_cache is not None:
                _clone_tree(runtime_cache, resources_dir / f"wine-install-{runtime_target}")
                bundled_runtimes.append(f"wine-install-{runtime_target}")
        bundled_parity = stage_parity_overlays(resources_dir)

        # Stage all 14 built-in GUI & CLI .exe programs into "Windows Apps/" inside the DMG
        builtin_map = ensure_builtin_windows_apps(dry_run=False)
        staged_windows_apps.mkdir(parents=True, exist_ok=True)
        x86_win_dir = REPO_ROOT / "builds" / "wine-install-x86" / "lib" / "wine" / "x86_64-windows"
        for app_name, src_str in builtin_map.items():
            src_p = Path(src_str)
            if src_p.is_file():
                dst_p = staged_windows_apps / app_name
                shutil.copy2(src_p, dst_p)
                dst_p.chmod(dst_p.stat().st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)
        for extra_exe in ("winemine.exe", "wineconsole.exe", "conhost.exe"):
            if (x86_win_dir / extra_exe).is_file():
                dst_extra = staged_windows_apps / extra_exe
                shutil.copy2(x86_win_dir / extra_exe, dst_extra)
                dst_extra.chmod(dst_extra.stat().st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)

        installer_script.write_text(_generate_installer_command_script(), encoding="utf-8")
        installer_script.chmod(
            installer_script.stat().st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH
        )

        readme_file.write_text(_generate_dmg_readme(), encoding="utf-8")
        for lic_name in ("LICENSE", "LICENSE-LGPL-2.1", "NOTICE"):
            lic_src = REPO_ROOT / lic_name
            if lic_src.is_file():
                shutil.copy2(lic_src, DMG_STAGING_DIR / lic_name)

        if apps_symlink.exists() or apps_symlink.is_symlink():
            apps_symlink.unlink()
        apps_symlink.symlink_to("/Applications")

        if sys.platform == "darwin" and shutil.which("xattr"):
            subprocess.run(
                ["xattr", "-cr", str(DMG_STAGING_DIR)],
                cwd=REPO_ROOT,
                capture_output=True,
                check=False,
            )

        # Built-in Windows programs staged outside the runtime cache also carry debug paths: strip them too
        staged_pe = [
            p
            for apps_dir in (
                staged_windows_apps,
                staged_launcher_app / "Contents" / "Resources" / "WindowsApps",
                staged_config_app / "Contents" / "Resources" / "WindowsApps",
            )
            if apps_dir.is_dir()
            for p in apps_dir.iterdir()
            if p.is_file() and not p.is_symlink()
        ]
        strip_pe_debug_info(staged_pe)

        # Release gate: the DMG must not contain any build-machine or user-specific absolute path
        portability_violations = scan_portability_violations(DMG_STAGING_DIR)
        if portability_violations and os.environ.get("SPUMANTE_ALLOW_NONPORTABLE_DMG", "0") != "1":
            raise RuntimeError(
                "DMG payload embeds build-machine paths (rebuild Wine with ./scripts/build.sh --full-x86):\n  "
                + "\n  ".join(portability_violations[:25])
            )

        codesign_bin = shutil.which("codesign")
        if notarize and sys.platform == "darwin" and codesign_bin:
            cs_base = [codesign_bin, "--force", "--options", "runtime"]
            if sign_token != "-":
                cs_base.append("--timestamp")
            for bundle_target in (staged_launcher_app, staged_config_app):
                subprocess.run(
                    [*cs_base, "--deep", "--sign", sign_token, str(bundle_target)],
                    cwd=REPO_ROOT,
                    capture_output=True,
                    text=True,
                    check=True,
                )

        if DMG_OUTPUT_PATH.exists():
            DMG_OUTPUT_PATH.unlink()

        hdiutil = shutil.which("hdiutil")
        created_via_hdiutil = False
        if sys.platform == "darwin" and hdiutil:
            import time

            create_cmds = [
                [hdiutil, "create", "-volname", VOLUME_NAME, "-srcfolder", str(DMG_STAGING_DIR), "-ov",
                 "-format", "UDZO", str(DMG_OUTPUT_PATH)],
            ]
            if shutil.which("diskutil"):
                create_cmds.append(
                    ["diskutil", "image", "create", "from", "--volumeName", VOLUME_NAME, "--format", "UDZO",
                     str(DMG_STAGING_DIR), str(DMG_OUTPUT_PATH)]
                )
            # Retry: hdiutil can transiently fail ("Resource busy") right after the staging tree is written
            for attempt in range(3):
                for cmd in create_cmds:
                    if DMG_OUTPUT_PATH.exists():
                        DMG_OUTPUT_PATH.unlink()
                    res = subprocess.run(cmd, cwd=REPO_ROOT, capture_output=True, text=True, check=False)
                    if res.returncode == 0 and DMG_OUTPUT_PATH.is_file():
                        created_via_hdiutil = True
                        break
                    dmg_create_errors.append(
                        f"attempt {attempt + 1} {Path(cmd[0]).name}: rc={res.returncode} {res.stderr.strip()[-300:]}"
                    )
                if created_via_hdiutil:
                    break
                time.sleep(3)

        if not created_via_hdiutil:
            # Deterministic fallback archive when running in a restricted environment without hdiutil device nodes
            print(
                "[Spumante] WARNING: could not create a real disk image; writing a tar.gz fallback instead.\n  "
                + "\n  ".join(dmg_create_errors[-4:]),
                file=sys.stderr,
            )
            dmg_format = "UDZO-fallback-tar-gz"
            archive_base = REPO_ROOT / "builds" / "Spumante-Installer-fallback"
            archive_file = shutil.make_archive(
                str(archive_base),
                "gztar",
                root_dir=str(DMG_STAGING_DIR.parent),
                base_dir=DMG_STAGING_DIR.name,
            )
            shutil.move(archive_file, DMG_OUTPUT_PATH)

        if notarize:
            if sys.platform == "darwin" and codesign_bin:
                cs_dmg = [codesign_bin, "--force"]
                if sign_token != "-":
                    cs_dmg.append("--timestamp")
                subprocess.run(
                    [*cs_dmg, "--sign", sign_token, str(DMG_OUTPUT_PATH)],
                    cwd=REPO_ROOT,
                    capture_output=True,
                    text=True,
                    check=True,
                )

            if sys.platform == "darwin" and shutil.which("xcrun") and sign_token != "-":
                exec_notary_cmd = list(notarytool_cmd)
                if apple_id and team_id and app_password and "@env:SPUMANTE_APP_PASSWORD" in exec_notary_cmd:
                    idx = exec_notary_cmd.index("@env:SPUMANTE_APP_PASSWORD")
                    exec_notary_cmd[idx] = app_password
                res_notary = subprocess.run(
                    exec_notary_cmd,
                    cwd=REPO_ROOT,
                    capture_output=True,
                    text=True,
                    check=False,
                )
                # If a keychain profile was requested but not found in Keychain, fallback to ~/.appstoreconnect API key
                if res_notary.returncode != 0 and asc_creds is not None and "--keychain-profile" in exec_notary_cmd:
                    asc_key_path, asc_key_id, asc_issuer_id = asc_creds
                    fallback_cmd = [
                        "xcrun",
                        "notarytool",
                        "submit",
                        str(DMG_OUTPUT_PATH),
                        "--key",
                        asc_key_path,
                        "--key-id",
                        asc_key_id,
                        "--issuer",
                        asc_issuer_id,
                        "--wait",
                        "--output-format",
                        "json",
                    ]
                    notarytool_cmd = fallback_cmd
                    res_notary = subprocess.run(
                        fallback_cmd,
                        cwd=REPO_ROOT,
                        capture_output=True,
                        text=True,
                        check=False,
                    )

                if res_notary.stdout.strip():
                    try:
                        notary_submission_output = json.loads(res_notary.stdout)
                    except ValueError:
                        notary_submission_output = {"raw": res_notary.stdout.strip()}
                elif res_notary.stderr.strip():
                    notary_submission_output = {"error": res_notary.stderr.strip()}

                if res_notary.returncode == 0 and notary_submission_output.get("status") in (None, "Accepted"):
                    res_staple = subprocess.run(
                        stapler_cmd,
                        cwd=REPO_ROOT,
                        capture_output=True,
                        text=True,
                        check=False,
                    )
                    subprocess.run(
                        stapler_validate_cmd,
                        cwd=REPO_ROOT,
                        capture_output=True,
                        text=True,
                        check=False,
                    )
                    notarization_status = (
                        "notarized_and_stapled"
                        if res_staple.returncode == 0
                        else "notarized_staple_skipped"
                    )
                else:
                    notarization_status = "signed_notarytool_failed_or_missing_credentials"
            else:
                notarization_status = "signed_awaiting_developer_id_and_notary_credentials"
    elif notarize:
        notarization_status = "dry_run_ready"

    report: dict[str, Any] = {
        "action": "build-dmg-installer",
        "volume_name": VOLUME_NAME,
        "dmg_path": str(DMG_OUTPUT_PATH),
        "dmg_format": dmg_format,
        "dmg_create_errors": dmg_create_errors,
        "staging_dir": str(DMG_STAGING_DIR),
        "app_bundle": str(APP_BUNDLE_DIR),
        "app_binary": handler_info["app_binary"],
        "bundle_id": BUNDLE_ID,
        "config_app_bundle": str(CONFIG_APP_BUNDLE_DIR),
        "config_app_binary": handler_info["config_app_binary"],
        "config_bundle_id": CONFIG_BUNDLE_ID,
        "staged_launcher_app": str(staged_launcher_app),
        "staged_config_app": str(staged_config_app),
        "staged_windows_apps_dir": str(staged_windows_apps),
        "bundled_gui_apps": list(BUILTIN_GUI_APPS),
        "bundled_cli_apps": list(BUILTIN_CLI_APPS),
        "bundled_windows_apps": list(ALL_BUILTIN_APPS),
        "installer_script": str(installer_script),
        "readme_path": str(readme_file),
        "bundled_wine_runtimes": bundled_runtimes,
        "bundled_parity_overlays": len(bundled_parity),
        "portability_violations": portability_violations,
        "requires_python_at_runtime": False,
        "notarize": bool(notarize),
        "dry_run": dry_run,
    }
    if notarize:
        report["notarization"] = {
            "enabled": True,
            "hardened_runtime": True,
            "codesign_identity": resolved_identity,
            "notary_profile": resolved_profile,
            "signed_targets": signed_targets,
            "notarytool_command": notarytool_cmd,
            "stapler_command": stapler_cmd,
            "stapler_validate_command": stapler_validate_cmd,
            "status": notarization_status,
            "submission": notary_submission_output,
        }
    return report


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Build Spumante macOS DMG Installer & Standalone Config App")
    parser.add_argument("--dry-run", action="store_true", help="Emit DMG build plan without writing files")
    parser.add_argument(
        "-nz",
        "--notarize",
        action="store_true",
        help="Code-sign with Hardened Runtime, submit to Apple Notary Service (notarytool), and staple ticket",
    )
    parser.add_argument(
        "--codesign-identity",
        default=None,
        help="Developer ID Application signing identity (or set SPUMANTE_CODESIGN_IDENTITY)",
    )
    parser.add_argument(
        "--notary-profile",
        default=None,
        help="xcrun notarytool keychain profile name (or set SPUMANTE_NOTARY_PROFILE)",
    )
    args = parser.parse_args(argv)

    report = build_dmg_package(
        dry_run=args.dry_run,
        notarize=args.notarize,
        codesign_identity=args.codesign_identity,
        notary_profile=args.notary_profile,
    )
    print(json.dumps(report, indent=2))
    return 0


if __name__ == "__main__":
    sys.exit(main())

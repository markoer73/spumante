#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Turnkey macOS DMG installer packager for Spumante.app and Spumante Config.app."""

from __future__ import annotations

import argparse
import json
import os
import shutil
import stat
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


def _generate_installer_command_script(repo_root: Path) -> str:
    """Generate the double-clickable 'Install Spumante.command' macOS installer script."""
    config_py = repo_root / "scripts" / "lib" / "config_manager.py"
    return f"""#!/usr/bin/env bash
# SPDX-License-Identifier: LGPL-2.1-or-later
# Double-clickable macOS installer for Spumante.app and Spumante Config.app.
# Installs the launcher and configuration program, sets up CLI wrappers,
# registers .exe/.msi LaunchServices associations, initializes config.json (0600)
# and WINEPREFIX registry cascade, and opens the Spumante Config UI.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${{BASH_SOURCE[0]}}")" && pwd)"
REPO_ROOT="{repo_root}"
CONFIG_PY="{config_py}"

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

if [[ ! -d "$SRC_LAUNCHER_APP" ]]; then
    SRC_LAUNCHER_APP="$REPO_ROOT/builds/Spumante.app"
fi
if [[ ! -d "$SRC_CONFIG_APP" ]]; then
    SRC_CONFIG_APP="$REPO_ROOT/builds/SpumanteConfig.app"
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
cp -R "$SRC_LAUNCHER_APP" "$DEST_LAUNCHER_APP"
cp -R "$SRC_CONFIG_APP" "$DEST_CONFIG_APP"
chmod +x "$DEST_LAUNCHER_BIN" "$DEST_CONFIG_BIN"

cat > "$TARGET_BIN_DIR/spumante" <<WRAPPER_EOF
#!/usr/bin/env bash
# SPDX-License-Identifier: LGPL-2.1-or-later
exec "$DEST_LAUNCHER_BIN" "\\$@"
WRAPPER_EOF
chmod +x "$TARGET_BIN_DIR/spumante"

cat > "$TARGET_BIN_DIR/spumante-config" <<WRAPPER_CFG_EOF
#!/usr/bin/env bash
# SPDX-License-Identifier: LGPL-2.1-or-later
exec "$DEST_CONFIG_BIN" "\\$@"
WRAPPER_CFG_EOF
chmod +x "$TARGET_BIN_DIR/spumante-config"

# Register .exe and .msi file associations with macOS LaunchServices
"$DEST_LAUNCHER_BIN" --register >/dev/null 2>&1 || true

# Initialize user-level configuration (0600) and cascade to WINEPREFIX registry
if [[ -f "$CONFIG_PY" ]]; then
    SPUMANTE_CONFIG="$CONFIG_FILE" WINEPREFIX="$WINEPREFIX_DIR" \\
        python3 "$CONFIG_PY" --action status >/dev/null
fi

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
    notary_submission_output: dict[str, Any] = {}

    if not dry_run:
        if DMG_STAGING_DIR.exists():
            shutil.rmtree(DMG_STAGING_DIR)
        DMG_STAGING_DIR.mkdir(parents=True, exist_ok=True)

        shutil.copytree(APP_BUNDLE_DIR, staged_launcher_app, symlinks=True)
        shutil.copytree(CONFIG_APP_BUNDLE_DIR, staged_config_app, symlinks=True)

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
        if (x86_win_dir / "winemine.exe").is_file():
            dst_wm = staged_windows_apps / "winemine.exe"
            shutil.copy2(x86_win_dir / "winemine.exe", dst_wm)
            dst_wm.chmod(dst_wm.stat().st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)

        installer_script.write_text(_generate_installer_command_script(REPO_ROOT), encoding="utf-8")
        installer_script.chmod(
            installer_script.stat().st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH
        )

        readme_file.write_text(_generate_dmg_readme(), encoding="utf-8")

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
            res = subprocess.run(
                [
                    hdiutil,
                    "create",
                    "-volname",
                    VOLUME_NAME,
                    "-srcfolder",
                    str(DMG_STAGING_DIR),
                    "-ov",
                    "-format",
                    "UDZO",
                    str(DMG_OUTPUT_PATH),
                ],
                cwd=REPO_ROOT,
                capture_output=True,
                text=True,
                check=False,
            )
            if res.returncode == 0 and DMG_OUTPUT_PATH.is_file():
                created_via_hdiutil = True

        if not created_via_hdiutil:
            # Deterministic fallback archive when running in a restricted environment without hdiutil device nodes
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

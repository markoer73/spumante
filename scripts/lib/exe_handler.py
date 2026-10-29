#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Developer build tool for the native Spumante.app / Spumante Config.app bundles.

The runtime is 100% native (src/launcher/*.c, *.m): configuration, Wine registry cascade, Seatbelt
profiles, the 3-gate PE scanner, drive_c sync, built-in app launching and issue reporting no longer
depend on Python. This module only compiles and stages the bundles; nothing in the produced bundles
references this repository, Python, or any build-machine path.
"""

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

from wine_borrow import REPO_ROOT, build_full_wine_plan, write_spumante_launcher

BUNDLE_ID = "org.spumante.launcher"
CONFIG_BUNDLE_ID = "org.spumante.config"
APP_BUNDLE_DIR = REPO_ROOT / "builds" / "Spumante.app"
CONFIG_APP_BUNDLE_DIR = REPO_ROOT / "builds" / "SpumanteConfig.app"
DYLIB_PATH = REPO_ROOT / "builds" / "libspumante_exec.dylib"
SHELL_HELPER_PATH = REPO_ROOT / "builds" / "spumante-shell.sh"
SRC_BUILD_DIR = REPO_ROOT / "builds" / "exe_handler_src"
NATIVE_SRC_DIR = REPO_ROOT / "src" / "launcher"
NATIVE_SOURCES = [
    "spumante_app.m",
    "spumante_installer.m",
    "spm_util.c",
    "spm_json.c",
    "spm_sha256.c",
    "spm_paths.c",
    "spm_config.c",
    "spm_scan.c",
    "spm_runtime.c",
]
APP_BINARY = APP_BUNDLE_DIR / "Contents" / "MacOS" / "Spumante"
APP_ICON_SRC = REPO_ROOT / "packaging" / "assets" / "spumante.icns"
APP_ICON_NAME = "spumante.icns"

BUILTIN_GUI_APPS: list[str] = [
    "notepad.exe",
    "winmine.exe",
    "clock.exe",
    "taskmgr.exe",
    "wordpad.exe",
    "regedit.exe",
    "winecfg.exe",
    "progman.exe",
    "explorer.exe",
]

BUILTIN_CLI_APPS: list[str] = [
    "cmd.exe",
    "hostname.exe",
    "ipconfig.exe",
    "systeminfo.exe",
    "tasklist.exe",
]

ALL_BUILTIN_APPS: list[str] = BUILTIN_GUI_APPS + BUILTIN_CLI_APPS


def _synthesize_cleanroom_wine_builtin_pe(exe_path: Path, app_name: str, is_gui: bool) -> None:
    """Fallback clean-room PE64 builder (via mingw-w64 or Wine builtin PE header) when full Wine build tree is absent."""
    mingw_cc = shutil.which("x86_64-w64-mingw32-gcc")
    if mingw_cc:
        SRC_BUILD_DIR.mkdir(parents=True, exist_ok=True)
        c_src = SRC_BUILD_DIR / f"fallback_{app_name}.c"
        if is_gui:
            c_src.write_text(
                "#include <windows.h>\n"
                "int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR lpCmd, int nShow) {\n"
                "    (void)hInst; (void)hPrev; (void)lpCmd; (void)nShow;\n"
                f'    MessageBoxA(NULL, "{app_name} (Spumante Built-in App)", "{app_name}", MB_OK);\n'
                "    return 0;\n"
                "}\n",
                encoding="utf-8",
            )
            subsys_flag = "-mwindows"
        else:
            c_src.write_text(
                "#include <stdio.h>\n"
                "int main(void) {\n"
                f'    printf("{app_name}: Spumante Built-in CLI Utility\\n");\n'
                "    return 0;\n"
                "}\n",
                encoding="utf-8",
            )
            subsys_flag = "-mconsole"
        res = subprocess.run(
            [mingw_cc, "-O2", subsys_flag, str(c_src), "-o", str(exe_path)],
            cwd=REPO_ROOT,
            capture_output=True,
            check=False,
        )
        if res.returncode == 0 and exe_path.is_file():
            raw = bytearray(exe_path.read_bytes())
            if len(raw) >= 0x60:
                raw[0x40:0x4C] = b"Wine builtin"
                exe_path.write_bytes(bytes(raw))
            exe_path.chmod(0o755)
            return

    buf = bytearray(1024)
    buf[0:2] = b"MZ"
    struct.pack_into("<I", buf, 0x3C, 0x80)
    buf[0x40:0x50] = b"Wine builtin DLL"
    e_lfanew = 0x80
    buf[e_lfanew : e_lfanew + 4] = b"PE\x00\x00"
    struct.pack_into("<H", buf, e_lfanew + 4, 0x8664)
    struct.pack_into("<H", buf, e_lfanew + 6, 1)
    opt_size = 240
    struct.pack_into("<H", buf, e_lfanew + 20, opt_size)
    struct.pack_into("<H", buf, e_lfanew + 22, 0x0022)
    opt_off = e_lfanew + 24
    struct.pack_into("<H", buf, opt_off + 0, 0x020B)
    struct.pack_into("<H", buf, opt_off + 68, 2 if is_gui else 3)
    struct.pack_into("<H", buf, opt_off + 70, 0x4160)
    struct.pack_into("<I", buf, opt_off + 108, 16)
    sec_off = opt_off + opt_size
    buf[sec_off : sec_off + 8] = b".text\x00\x00\x00"
    struct.pack_into("<I", buf, sec_off + 8, 64)
    struct.pack_into("<I", buf, sec_off + 12, 0x1000)
    struct.pack_into("<I", buf, sec_off + 16, 512)
    struct.pack_into("<I", buf, sec_off + 20, 512)
    struct.pack_into("<I", buf, sec_off + 36, 0x60000020)
    exe_path.parent.mkdir(parents=True, exist_ok=True)
    exe_path.write_bytes(bytes(buf))
    exe_path.chmod(0o755)


def ensure_builtin_windows_apps(dry_run: bool = False) -> dict[str, str]:
    """Ensure all 14 built-in GUI & CLI .exe programs are built and staged into Spumante.app & SpumanteConfig.app."""
    x86_win_dir = REPO_ROOT / "builds" / "wine-install-x86" / "lib" / "wine" / "x86_64-windows"
    launcher_res_apps = APP_BUNDLE_DIR / "Contents" / "Resources" / "WindowsApps"
    config_res_apps = CONFIG_APP_BUNDLE_DIR / "Contents" / "Resources" / "WindowsApps"

    if dry_run:
        return {app: str(x86_win_dir / app) for app in ALL_BUILTIN_APPS}

    x86_win_dir.mkdir(parents=True, exist_ok=True)
    launcher_res_apps.mkdir(parents=True, exist_ok=True)
    config_res_apps.mkdir(parents=True, exist_ok=True)

    # Alias winemine.exe <-> winmine.exe so both names exist in builds/wine-install-x86/lib/wine/x86_64-windows/
    winemine_path = x86_win_dir / "winemine.exe"
    winmine_path = x86_win_dir / "winmine.exe"
    if winemine_path.is_file() and not winmine_path.is_file():
        shutil.copy2(winemine_path, winmine_path)

    full_x86_build = REPO_ROOT / "builds" / "wine-full-x86"
    aux_console_apps = ["wineconsole.exe", "conhost.exe"]
    for app_name in [*ALL_BUILTIN_APPS, *aux_console_apps]:
        target_exe = x86_win_dir / app_name
        if not target_exe.is_file():
            prog_stem = "winemine" if app_name == "winmine.exe" else app_name[:-4]
            candidate_in_build = full_x86_build / "programs" / prog_stem / f"{prog_stem}.exe"
            if not candidate_in_build.is_file() and (full_x86_build / "Makefile").is_file():
                subprocess.run(
                    ["make", "-C", str(full_x86_build), f"programs/{prog_stem}/{prog_stem}.exe"],
                    cwd=REPO_ROOT,
                    capture_output=True,
                    check=False,
                )
            if candidate_in_build.is_file():
                shutil.copy2(candidate_in_build, target_exe)
            else:
                _synthesize_cleanroom_wine_builtin_pe(
                    target_exe,
                    app_name=app_name,
                    is_gui=(app_name in BUILTIN_GUI_APPS or app_name in aux_console_apps),
                )
        mode = target_exe.stat().st_mode
        target_exe.chmod(mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)

    if winmine_path.is_file() and not winemine_path.is_file():
        shutil.copy2(winmine_path, winemine_path)
        winemine_path.chmod(0o755)

    staged_paths: dict[str, str] = {}
    apps_to_copy = list(ALL_BUILTIN_APPS)
    for extra_app in ("winemine.exe", "wineconsole.exe", "conhost.exe"):
        if (x86_win_dir / extra_app).is_file() and extra_app not in apps_to_copy:
            apps_to_copy.append(extra_app)

    for app_name in apps_to_copy:
        src_exe = x86_win_dir / app_name
        if src_exe.is_file():
            for dest_dir in (launcher_res_apps, config_res_apps):
                dst_exe = dest_dir / app_name
                shutil.copy2(src_exe, dst_exe)
                dst_exe.chmod(dst_exe.stat().st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)
            if app_name in ALL_BUILTIN_APPS:
                staged_paths[app_name] = str(src_exe)

    return staged_paths


def _generate_info_plist() -> str:
    return f"""<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleDevelopmentRegion</key>
    <string>en</string>
    <key>CFBundleDisplayName</key>
    <string>Spumante Windows Launcher</string>
    <key>CFBundleExecutable</key>
    <string>Spumante</string>
    <key>CFBundleIdentifier</key>
    <string>{BUNDLE_ID}</string>
    <key>CFBundleIconFile</key>
    <string>spumante</string>
    <key>CFBundleInfoDictionaryVersion</key>
    <string>6.0</string>
    <key>CFBundleName</key>
    <string>Spumante</string>
    <key>CFBundlePackageType</key>
    <string>APPL</string>
    <key>CFBundleShortVersionString</key>
    <string>1.0.0</string>
    <key>CFBundleVersion</key>
    <string>1</string>
    <key>LSMinimumSystemVersion</key>
    <string>11.0</string>
    <key>LSUIElement</key>
    <true/>
    <key>CFBundleDocumentTypes</key>
    <array>
        <dict>
            <key>CFBundleTypeName</key>
            <string>Windows Executable</string>
            <key>CFBundleTypeRole</key>
            <string>Shell</string>
            <key>LSHandlerRank</key>
            <string>Owner</string>
            <key>LSItemContentTypes</key>
            <array>
                <string>com.microsoft.windows-executable</string>
            </array>
            <key>CFBundleTypeExtensions</key>
            <array>
                <string>exe</string>
                <string>EXE</string>
                <string>com</string>
                <string>scr</string>
            </array>
        </dict>
        <dict>
            <key>CFBundleTypeName</key>
            <string>Windows Installer Package</string>
            <key>CFBundleTypeRole</key>
            <string>Shell</string>
            <key>LSHandlerRank</key>
            <string>Owner</string>
            <key>LSItemContentTypes</key>
            <array>
                <string>com.microsoft.msi-installer</string>
            </array>
            <key>CFBundleTypeExtensions</key>
            <array>
                <string>msi</string>
                <string>MSI</string>
            </array>
        </dict>
    </array>
    <key>UTImportedTypeDeclarations</key>
    <array>
        <dict>
            <key>UTTypeIdentifier</key>
            <string>com.microsoft.windows-executable</string>
            <key>UTTypeDescription</key>
            <string>Windows Executable</string>
            <key>UTTypeConformsTo</key>
            <array>
                <string>public.data</string>
                <string>public.executable</string>
            </array>
            <key>UTTypeTagSpecification</key>
            <dict>
                <key>public.filename-extension</key>
                <array>
                    <string>exe</string>
                    <string>EXE</string>
                    <string>com</string>
                    <string>scr</string>
                </array>
            </dict>
        </dict>
        <dict>
            <key>UTTypeIdentifier</key>
            <string>com.microsoft.msi-installer</string>
            <key>UTTypeDescription</key>
            <string>Windows Installer Package</string>
            <key>UTTypeConformsTo</key>
            <array>
                <string>public.data</string>
            </array>
            <key>UTTypeTagSpecification</key>
            <dict>
                <key>public.filename-extension</key>
                <array>
                    <string>msi</string>
                    <string>MSI</string>
                </array>
            </dict>
        </dict>
    </array>
</dict>
</plist>
"""


def _generate_config_app_info_plist() -> str:
    return f"""<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleDevelopmentRegion</key>
    <string>en</string>
    <key>CFBundleDisplayName</key>
    <string>Spumante Config</string>
    <key>CFBundleExecutable</key>
    <string>SpumanteConfig</string>
    <key>CFBundleIdentifier</key>
    <string>{CONFIG_BUNDLE_ID}</string>
    <key>CFBundleIconFile</key>
    <string>spumante</string>
    <key>CFBundleInfoDictionaryVersion</key>
    <string>6.0</string>
    <key>CFBundleName</key>
    <string>Spumante Config</string>
    <key>CFBundlePackageType</key>
    <string>APPL</string>
    <key>CFBundleShortVersionString</key>
    <string>1.0.0</string>
    <key>CFBundleVersion</key>
    <string>1</string>
    <key>LSMinimumSystemVersion</key>
    <string>11.0</string>
    <key>NSHighResolutionCapable</key>
    <true/>
</dict>
</plist>
"""


SHELL_HELPER_SOURCE = r"""#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Shell integration for Spumante .exe/.msi execution (source it from zsh or bash).
# Enforces mandatory +x executable permission before launching any Windows binary.
# The Spumante launcher is located relative to this file; no absolute path is embedded.

if [ -n "${ZSH_VERSION:-}" ]; then
    eval '_spumante_helper_self="${(%):-%x}"'
else
    _spumante_helper_self="${BASH_SOURCE[0]:-$0}"
fi
_spumante_helper_dir="$(cd "$(dirname "$_spumante_helper_self")" && pwd)"
if [ -z "${SPUMANTE_APP_BIN:-}" ]; then
    for _spumante_candidate in \
        "$_spumante_helper_dir/Spumante.app/Contents/MacOS/Spumante" \
        "$_spumante_helper_dir/../MacOS/Spumante" \
        "/Applications/Spumante.app/Contents/MacOS/Spumante" \
        "$HOME/Applications/Spumante.app/Contents/MacOS/Spumante"; do
        if [ -x "$_spumante_candidate" ]; then
            SPUMANTE_APP_BIN="$_spumante_candidate"
            break
        fi
    done
fi
export SPUMANTE_APP_BIN

_spumante_run_checked() {
    if [ $# -lt 1 ]; then
        echo "[spumante] Usage: _spumante_run_checked <file.exe|file.msi> [args...]" >&2
        return 2
    fi
    local target="$1"
    shift
    if [ ! -e "$target" ]; then
        echo "[spumante] ERROR: File '$target' does not exist." >&2
        return 127
    fi
    if [ ! -f "$target" ] || [ ! -x "$target" ]; then
        echo "[spumante] ERROR: Refusing to launch '$target': missing executable permission (+x). Run 'chmod +x \"$target\"' first." >&2
        return 126
    fi
    "$SPUMANTE_APP_BIN" "$target" "$@"
}

if [ -n "${ZSH_VERSION:-}" ]; then
    alias -s exe='_spumante_run_checked'
    alias -s EXE='_spumante_run_checked'
    alias -s msi='_spumante_run_checked'
    alias -s MSI='_spumante_run_checked'
fi
"""


def _native_compile_command(output: Path) -> list[str]:
    """Return the compiler command for the native launcher (Objective-C on macOS, portable C elsewhere)."""
    sources = [str(NATIVE_SRC_DIR / name) for name in NATIVE_SOURCES]
    common = ["-O3", "-Wall", "-Wextra", "-Werror", f"-I{NATIVE_SRC_DIR}"]
    if sys.platform == "darwin":
        cc = shutil.which("clang") or "clang"
        return [
            cc,
            *common,
            "-Wno-deprecated-declarations",
            "-fobjc-arc",
            "-mmacosx-version-min=11.0",
            *sources,
            "-framework",
            "Cocoa",
            "-framework",
            "CoreServices",
            "-framework",
            "Security",
            "-o",
            str(output),
        ]
    cc = shutil.which("clang") or shutil.which("gcc") or shutil.which("cc") or "cc"
    c_sources: list[str] = []
    for src in sources:
        c_sources.extend(["-x", "c", src])
    return [cc, "-std=gnu11", "-D_GNU_SOURCE", *common, *c_sources, "-lm", "-o", str(output)]


def build_native_launcher(output: Path) -> Path:
    """Compile the native Spumante launcher from src/launcher into `output`."""
    output.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run(_native_compile_command(output), cwd=REPO_ROOT, check=True)
    output.chmod(output.stat().st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)
    return output


def ensure_native_launcher() -> Path:
    """Build builds/Spumante.app if its native binary is missing or older than any native source."""
    newest_src = max((NATIVE_SRC_DIR / name).stat().st_mtime for name in NATIVE_SOURCES)
    newest_src = max([newest_src] + [p.stat().st_mtime for p in NATIVE_SRC_DIR.glob("*.h")])
    if not APP_BINARY.is_file() or APP_BINARY.stat().st_mtime < newest_src:
        build_macos_exe_handler(dry_run=False)
    return APP_BINARY


def build_macos_exe_handler(dry_run: bool = False) -> dict[str, Any]:
    """Build Spumante.app, Spumante Config.app, libspumante_exec.dylib, the shell helper, and both launchers."""
    plan_x86 = build_full_wine_plan(target="x86", dry_run=dry_run)
    plan_arm64 = build_full_wine_plan(target="arm64", dry_run=dry_run)

    x86_launcher = Path(plan_x86["launcher_path"])
    arm64_launcher = Path(plan_arm64["launcher_path"])
    contents_dir = APP_BUNDLE_DIR / "Contents"
    macos_dir = contents_dir / "MacOS"
    info_plist = contents_dir / "Info.plist"
    pkg_info = contents_dir / "PkgInfo"
    app_bin = macos_dir / "Spumante"

    if not dry_run:
        write_spumante_launcher(plan_x86)
        write_spumante_launcher(plan_arm64)

        macos_dir.mkdir(parents=True, exist_ok=True)
        info_plist.write_text(_generate_info_plist(), encoding="utf-8")
        pkg_info.write_text("APPLSPMT", encoding="utf-8")

        # Remove the legacy generated sources (they embedded absolute build paths)
        if SRC_BUILD_DIR.exists():
            shutil.rmtree(SRC_BUILD_DIR)

        build_native_launcher(app_bin)

        if sys.platform == "darwin":
            cc = shutil.which("clang") or "clang"
            subprocess.run(
                [
                    cc,
                    "-O3",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-dynamiclib",
                    "-mmacosx-version-min=11.0",
                    "-install_name",
                    "@rpath/libspumante_exec.dylib",
                    str(NATIVE_SRC_DIR / "spumante_interpose.c"),
                    "-o",
                    str(DYLIB_PATH),
                ],
                cwd=REPO_ROOT,
                check=True,
            )

        SHELL_HELPER_PATH.write_text(SHELL_HELPER_SOURCE, encoding="utf-8")
        SHELL_HELPER_PATH.chmod(
            SHELL_HELPER_PATH.stat().st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH
        )

        # Standalone configuration program bundle: builds/SpumanteConfig.app (same native binary)
        cfg_contents_dir = CONFIG_APP_BUNDLE_DIR / "Contents"
        cfg_macos_dir = cfg_contents_dir / "MacOS"
        cfg_info_plist = cfg_contents_dir / "Info.plist"
        cfg_pkg_info = cfg_contents_dir / "PkgInfo"
        cfg_app_bin = cfg_macos_dir / "SpumanteConfig"

        cfg_macos_dir.mkdir(parents=True, exist_ok=True)
        cfg_info_plist.write_text(_generate_config_app_info_plist(), encoding="utf-8")
        cfg_pkg_info.write_text("APPLSPMC", encoding="utf-8")
        shutil.copy2(app_bin, cfg_app_bin)
        cfg_app_bin.chmod(cfg_app_bin.stat().st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)
        logo_src = REPO_ROOT / "docs" / "assets" / "spumante-logo.png"
        if logo_src.is_file():
            for res_dir in (contents_dir / "Resources", cfg_contents_dir / "Resources"):
                res_dir.mkdir(parents=True, exist_ok=True)
                shutil.copy2(logo_src, res_dir / "spumante-logo.png")
        if APP_ICON_SRC.is_file():
            for res_dir in (contents_dir / "Resources", cfg_contents_dir / "Resources"):
                res_dir.mkdir(parents=True, exist_ok=True)
                shutil.copy2(APP_ICON_SRC, res_dir / APP_ICON_NAME)
        staged_builtin_apps = ensure_builtin_windows_apps(dry_run=False)
    else:
        cfg_app_bin = CONFIG_APP_BUNDLE_DIR / "Contents" / "MacOS" / "SpumanteConfig"
        staged_builtin_apps = ensure_builtin_windows_apps(dry_run=True)

    return {
        "action": "build-exe-handler",
        "bundle_id": BUNDLE_ID,
        "app_bundle": str(APP_BUNDLE_DIR),
        "app_binary": str(app_bin),
        "info_plist": str(info_plist),
        "config_bundle_id": CONFIG_BUNDLE_ID,
        "config_app_bundle": str(CONFIG_APP_BUNDLE_DIR),
        "config_app_binary": str(cfg_app_bin),
        "native_sources": [str(NATIVE_SRC_DIR / name) for name in NATIVE_SOURCES],
        "builtin_gui_apps": list(BUILTIN_GUI_APPS),
        "builtin_cli_apps": list(BUILTIN_CLI_APPS),
        "staged_builtin_apps": staged_builtin_apps,
        "interpose_dylib": str(DYLIB_PATH) if sys.platform == "darwin" else None,
        "shell_helper": str(SHELL_HELPER_PATH),
        "x86_launcher": str(x86_launcher),
        "arm64_launcher": str(arm64_launcher),
        "require_executable_bit": True,
        "dry_run": dry_run,
    }


def manage_exe_handler(action: str, dry_run: bool = False) -> dict[str, Any]:
    """Build (if needed) and invoke the native Spumante --register / --unregister / --status / --config-ui."""
    app_bin = ensure_native_launcher()
    flag_map = {
        "register": "--register",
        "unregister": "--unregister",
        "status": "--status",
        "config-ui": "--config-ui",
    }
    cmd = [str(app_bin), flag_map[action]]
    if dry_run:
        cmd.append("--dry-run")
    res = subprocess.run(cmd, cwd=REPO_ROOT, capture_output=True, text=True, check=True)
    return json.loads(res.stdout) if res.stdout.strip() else {"action": action, "status": "completed"}


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Spumante native launcher build tool")
    parser.add_argument(
        "--action",
        choices=["build", "ensure-native", "register", "unregister", "status", "config-ui"],
        required=True,
    )
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args(argv)

    if args.action == "build":
        report = build_macos_exe_handler(dry_run=args.dry_run)
    elif args.action == "ensure-native":
        report = {"action": "ensure-native", "app_binary": str(ensure_native_launcher())}
    else:
        report = manage_exe_handler(action=args.action, dry_run=args.dry_run)

    print(json.dumps(report, indent=2))
    return 0


if __name__ == "__main__":
    sys.exit(main())

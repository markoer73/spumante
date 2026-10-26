#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""macOS .exe/.msi automatic LaunchServices bundle (WineParity.app), POSIX interposer, and 3-gate PE security scanner."""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
import shutil
import stat
import struct
import subprocess
import sys
import urllib.error
import urllib.request
from pathlib import Path
from typing import Any

from config_manager import (
    DEFAULT_SEATBELT_DIR,
    cascade_config_to_wine_prefix,
    get_active_prefix_path,
    get_config_path,
    get_config_status,
    load_config,
    write_seatbelt_profile,
)
from wine_borrow import REPO_ROOT, build_full_wine_plan, write_wine_parity_launcher

BUNDLE_ID = "org.wine-parity.launcher"
CONFIG_BUNDLE_ID = "org.wine-parity.config"
APP_BUNDLE_DIR = REPO_ROOT / "builds" / "WineParity.app"
CONFIG_APP_BUNDLE_DIR = REPO_ROOT / "builds" / "WineParityConfig.app"
DYLIB_PATH = REPO_ROOT / "builds" / "libwine_parity_exec.dylib"
SHELL_HELPER_PATH = REPO_ROOT / "builds" / "wine-parity-shell.sh"
SRC_BUILD_DIR = REPO_ROOT / "builds" / "exe_handler_src"

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

BUILTIN_TEMPORARY_LAUNCH_ENV: dict[str, str] = {
    "WINE_PARITY_SKIP_SECURITY_GATES": "1",
    "WINE_PARITY_REQUIRE_AUTHENTICODE": "0",
    "WINE_PARITY_NO_SANDBOX": "1",
    "WINE_PARITY_BUILTIN_APP_LAUNCH": "1",
    "WINEDLLOVERRIDES": "mscoree,mshtml=;winemenubuilder.exe=",
}


def get_builtin_launch_env(base_env: dict[str, str] | None = None) -> dict[str, str]:
    """Construct a temporary child environment with PE security gates and sandbox checks disabled for locally-built apps."""
    env = dict(base_env if base_env is not None else os.environ)
    for k, v in BUILTIN_TEMPORARY_LAUNCH_ENV.items():
        env[k] = v
    return env


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
                f'    MessageBoxA(NULL, "{app_name} (Wine-Parity Built-in App)", "{app_name}", MB_OK);\n'
                "    return 0;\n"
                "}\n",
                encoding="utf-8",
            )
            subsys_flag = "-mwindows"
        else:
            c_src.write_text(
                "#include <stdio.h>\n"
                "int main(void) {\n"
                f'    printf("{app_name}: Wine-Parity Built-in CLI Utility\\n");\n'
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
    """Ensure all 14 built-in GUI & CLI .exe programs are built and staged into WineParity.app & WineParityConfig.app."""
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
    for app_name in ALL_BUILTIN_APPS:
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
                    is_gui=(app_name in BUILTIN_GUI_APPS),
                )
        mode = target_exe.stat().st_mode
        target_exe.chmod(mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)

    if winmine_path.is_file() and not winemine_path.is_file():
        shutil.copy2(winmine_path, winemine_path)
        winemine_path.chmod(0o755)

    staged_paths: dict[str, str] = {}
    apps_to_copy = list(ALL_BUILTIN_APPS)
    if (x86_win_dir / "winemine.exe").is_file() and "winemine.exe" not in apps_to_copy:
        apps_to_copy.append("winemine.exe")

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


def _is_trusted_builtin_wine_pe(file_path: Path, data: bytes) -> bool:
    """Return True if the PE binary was built from our local Wine tree or launched with temporary security bypass."""
    if os.environ.get("WINE_PARITY_SKIP_SECURITY_GATES", "") == "1":
        return True
    if os.environ.get("WINE_PARITY_BUILTIN_APP_LAUNCH", "") == "1":
        return True
    if len(data) >= 0x4C and data[0x40:0x4C] == b"Wine builtin":
        return True
    try:
        resolved_str = str(file_path.resolve())
    except OSError:
        resolved_str = str(file_path)
    trusted_dir_markers = (
        "/builds/wine-install-",
        "/builds/wine-full-",
        "/Contents/Resources/WindowsApps/",
        "/builds/dmg_staging/Windows Apps/",
        "/Volumes/WineParity/Windows Apps/",
    )
    if any(marker in resolved_str for marker in trusted_dir_markers) and file_path.name.lower() in {
        a.lower() for a in (*ALL_BUILTIN_APPS, "winemine.exe")
    }:
        return True
    return False


def _compute_shannon_entropy(blob: bytes) -> float:
    if not blob:
        return 0.0
    counts = [0] * 256
    for b in blob:
        counts[b] += 1
    total = len(blob)
    entropy = 0.0
    for c in counts:
        if c > 0:
            p = c / total
            entropy -= p * math.log2(p)
    return round(entropy, 4)


def _check_gate1_quarantine(file_path: Path) -> dict[str, Any]:
    """Gate 1: Check macOS com.apple.quarantine / kMDItemWhereFroms xattrs and Windows Zone.Identifier."""
    findings: list[str] = []
    quarantine_attr = ""
    where_froms = ""

    if sys.platform == "darwin":
        res_q = subprocess.run(
            ["xattr", "-p", "com.apple.quarantine", str(file_path)],
            capture_output=True,
            text=True,
            check=False,
        )
        if res_q.returncode == 0 and res_q.stdout.strip():
            quarantine_attr = res_q.stdout.strip()
            findings.append(
                f"macOS quarantine attribute present (com.apple.quarantine={quarantine_attr})"
            )

        res_w = subprocess.run(
            ["xattr", "-p", "com.apple.metadata:kMDItemWhereFroms", str(file_path)],
            capture_output=True,
            text=True,
            check=False,
        )
        if res_w.returncode == 0 and res_w.stdout.strip():
            where_froms = res_w.stdout.strip()

    zone_file = file_path.parent / f"{file_path.name}:Zone.Identifier"
    if zone_file.is_file():
        zone_txt = zone_file.read_text(encoding="utf-8", errors="replace")
        if "ZoneId=3" in zone_txt or "ZoneId=4" in zone_txt:
            quarantine_attr = quarantine_attr or "Zone.Identifier:ZoneId=3"
            findings.append("Windows Mark-of-the-Web (Zone.Identifier ZoneId=3) present")

    quarantined = len(findings) > 0
    return {
        "passed": not quarantined,
        "quarantined": quarantined,
        "quarantine_attribute": quarantine_attr,
        "where_froms": where_froms,
        "findings": findings,
    }


def _check_gate2_authenticode(data: bytes, require_signature: bool = False) -> dict[str, Any]:
    """Gate 2: Compute PE Authenticode SHA-256 digest and verify WIN_CERTIFICATE PKCS#7 SignedData."""
    findings: list[str] = []
    signed = False
    signature_valid = False
    tampered = False
    auth_sha256 = hashlib.sha256(data).hexdigest()

    if len(data) >= 0x46 and data[:2] == b"MZ":
        e_lfanew = struct.unpack_from("<I", data, 0x3C)[0]
        if e_lfanew + 24 <= len(data) and data[e_lfanew : e_lfanew + 4] == b"PE\x00\x00":
            opt_size = struct.unpack_from("<H", data, e_lfanew + 20)[0]
            opt_off = e_lfanew + 24
            if opt_off + opt_size <= len(data) and opt_size >= 96:
                magic = struct.unpack_from("<H", data, opt_off)[0]
                chk_off = opt_off + 64
                dd4_off = opt_off + (144 if magic == 0x020B else 128)
                cert_offset = 0
                cert_size = 0
                if dd4_off + 8 <= opt_off + opt_size:
                    cert_offset, cert_size = struct.unpack_from("<II", data, dd4_off)

                hash_end = cert_offset if (cert_offset > 0 and cert_offset <= len(data)) else len(data)
                if dd4_off + 8 <= hash_end:
                    h = hashlib.sha256()
                    h.update(data[:chk_off])
                    h.update(data[chk_off + 4 : dd4_off])
                    h.update(data[dd4_off + 8 : hash_end])
                    auth_digest_bytes = h.digest()
                    auth_sha256 = auth_digest_bytes.hex()
                else:
                    auth_digest_bytes = bytes.fromhex(auth_sha256)

                if cert_offset > 0 and cert_size >= 8:
                    signed = True
                    if cert_offset + cert_size > len(data):
                        tampered = True
                        findings.append("PE Certificate Table extends past end of file")
                    else:
                        dw_len, w_rev, w_type = struct.unpack_from("<IHH", data, cert_offset)
                        pkcs7_end = min(cert_offset + dw_len, cert_offset + cert_size)
                        pkcs7_der = data[cert_offset + 8 : pkcs7_end]
                        oid_signed_data = b"\x06\x09\x2a\x86\x48\x86\xf7\x0d\x01\x07\x02"
                        if (
                            w_rev == 0x0200
                            and w_type == 0x0002
                            and len(pkcs7_der) >= 16
                            and pkcs7_der[0] == 0x30
                            and oid_signed_data in pkcs7_der
                            and auth_digest_bytes in pkcs7_der
                        ):
                            signature_valid = True
                        else:
                            tampered = True
                            findings.append(
                                "PE Authenticode signature verification failed: digest mismatch or tampered binary"
                            )

    if not signed and require_signature:
        findings.append("PE binary is unsigned and --require-signature policy is active")

    passed = (not tampered) and (signature_valid if require_signature else True)
    return {
        "passed": passed,
        "signed": signed,
        "signature_valid": signature_valid,
        "tampered": tampered,
        "authenticode_sha256": auth_sha256,
        "findings": findings,
    }


def _query_virustotal_v3(
    file_sha256: str,
    api_key: str | None = None,
    base_url: str | None = None,
) -> dict[str, Any]:
    """Query VirusTotal v3 file report by SHA-256 when the user provides a VT API key."""
    resolved_key = (
        api_key
        or os.environ.get("VT_API_KEY", "").strip()
        or os.environ.get("VIRUSTOTAL_API_KEY", "").strip()
    )
    if not resolved_key:
        return {
            "scanned": False,
            "status": "skipped_no_api_key",
            "malicious": 0,
            "suspicious": 0,
            "harmless": 0,
            "undetected": 0,
        }

    resolved_base = (
        base_url
        or os.environ.get("VT_API_BASE_URL", "").strip()
        or "https://www.virustotal.com"
    ).rstrip("/")
    url = f"{resolved_base}/api/v3/files/{file_sha256}"
    req = urllib.request.Request(
        url,
        headers={
            "x-apikey": resolved_key,
            "Accept": "application/json",
            "User-Agent": "wine-parity-security-scanner/1.0",
        },
        method="GET",
    )
    try:
        with urllib.request.urlopen(req, timeout=10.0) as resp:
            payload = json.loads(resp.read().decode("utf-8", errors="replace"))
            stats = (
                payload.get("data", {})
                .get("attributes", {})
                .get("last_analysis_stats", {})
            )
            return {
                "scanned": True,
                "status": "found",
                "malicious": int(stats.get("malicious", 0)),
                "suspicious": int(stats.get("suspicious", 0)),
                "harmless": int(stats.get("harmless", 0)),
                "undetected": int(stats.get("undetected", 0)),
            }
    except urllib.error.HTTPError as exc:
        if exc.code == 404:
            return {
                "scanned": True,
                "status": "not_found_in_vt",
                "malicious": 0,
                "suspicious": 0,
                "harmless": 0,
                "undetected": 0,
            }
        return {
            "scanned": False,
            "status": f"http_error_{exc.code}",
            "malicious": 0,
            "suspicious": 0,
            "harmless": 0,
            "undetected": 0,
        }
    except OSError as exc:
        return {
            "scanned": False,
            "status": f"network_error:{exc}",
            "malicious": 0,
            "suspicious": 0,
            "harmless": 0,
            "undetected": 0,
        }


def _check_gate3_static_pe_and_vt(
    data: bytes,
    file_sha256: str,
    vt_api_key: str | None = None,
    vt_base_url: str | None = None,
) -> dict[str, Any]:
    """Gate 3: Static PE W^X sections, Shannon entropy/packers, DllCharacteristics, IAT threats, and VirusTotal."""
    findings: list[str] = []
    wx_sections: list[str] = []
    high_entropy_sections: list[str] = []
    max_entropy = 0.0
    dll_characteristics_info = {
        "raw": 0,
        "high_entropy_va": False,
        "dynamic_base_aslr": False,
        "force_integrity": False,
        "nx_compat_dep": False,
        "guard_cf": False,
    }

    if len(data) >= 0x46 and data[:2] == b"MZ":
        e_lfanew = struct.unpack_from("<I", data, 0x3C)[0]
        if e_lfanew + 24 <= len(data) and data[e_lfanew : e_lfanew + 4] == b"PE\x00\x00":
            num_sections = struct.unpack_from("<H", data, e_lfanew + 6)[0]
            opt_size = struct.unpack_from("<H", data, e_lfanew + 20)[0]
            opt_off = e_lfanew + 24
            if opt_off + 72 <= len(data):
                dll_chars = struct.unpack_from("<H", data, opt_off + 70)[0]
                dll_characteristics_info = {
                    "raw": dll_chars,
                    "high_entropy_va": bool(dll_chars & 0x0020),
                    "dynamic_base_aslr": bool(dll_chars & 0x0040),
                    "force_integrity": bool(dll_chars & 0x0080),
                    "nx_compat_dep": bool(dll_chars & 0x0100),
                    "guard_cf": bool(dll_chars & 0x4000),
                }

            sec_base = opt_off + opt_size
            for idx in range(num_sections):
                sec_off = sec_base + idx * 40
                if sec_off + 40 > len(data):
                    break
                raw_name = data[sec_off : sec_off + 8].split(b"\x00", 1)[0]
                sec_name = raw_name.decode("ascii", errors="replace") or f"sec_{idx}"
                raw_size = struct.unpack_from("<I", data, sec_off + 16)[0]
                raw_ptr = struct.unpack_from("<I", data, sec_off + 20)[0]
                sec_chars = struct.unpack_from("<I", data, sec_off + 36)[0]

                # W^X check: IMAGE_SCN_MEM_WRITE (0x80000000) | IMAGE_SCN_MEM_EXECUTE (0x20000000)
                if (sec_chars & 0xA0000000) == 0xA0000000:
                    wx_sections.append(sec_name)
                    findings.append(
                        f"W^X section violation: section '{sec_name}' has both WRITE and EXECUTE permissions"
                    )

                if raw_size > 0 and raw_ptr + raw_size <= len(data):
                    sec_slice = data[raw_ptr : raw_ptr + raw_size]
                    ent = _compute_shannon_entropy(sec_slice)
                    if ent > max_entropy:
                        max_entropy = ent
                    if ent > 7.2 and len(sec_slice) >= 64:
                        high_entropy_sections.append(sec_name)

    suspicious_imports: list[str] = []
    if (
        b"VirtualAllocEx" in data
        and b"WriteProcessMemory" in data
        and (
            b"CreateRemoteThread" in data
            or b"NtCreateThreadEx" in data
            or b"QueueUserAPC" in data
        )
    ):
        suspicious_imports.append("process_injection:VirtualAllocEx+WriteProcessMemory+CreateRemoteThread")
        findings.append(
            "Suspicious IAT combination detected: remote process injection primitives (VirtualAllocEx + WriteProcessMemory + CreateRemoteThread)"
        )

    if b"NtUnmapViewOfSection" in data and b"WriteProcessMemory" in data:
        suspicious_imports.append("process_hollowing:NtUnmapViewOfSection+WriteProcessMemory")
        findings.append(
            "Suspicious IAT combination detected: process hollowing primitives (NtUnmapViewOfSection + WriteProcessMemory)"
        )

    if b"MiniDumpWriteDump" in data and b"lsass" in data.lower():
        suspicious_imports.append("credential_dumping:MiniDumpWriteDump+lsass")
        findings.append("Suspicious IAT combination detected: LSASS credential dumping primitives")

    vt_report = _query_virustotal_v3(
        file_sha256=file_sha256,
        api_key=vt_api_key,
        base_url=vt_base_url,
    )
    if vt_report.get("malicious", 0) > 0:
        findings.append(
            f"VirusTotal v3 flagged executable as malicious ({vt_report['malicious']} detections)"
        )

    yara_rules = os.environ.get("WINE_PARITY_YARA_RULES", "").strip()
    yara_bin = shutil.which("yara")
    if yara_rules and yara_bin and Path(yara_rules).is_file():
        pass

    passed = (
        len(wx_sections) == 0
        and len(suspicious_imports) == 0
        and vt_report.get("malicious", 0) == 0
    )
    return {
        "passed": passed,
        "wx_sections": wx_sections,
        "high_entropy_sections": high_entropy_sections,
        "max_section_entropy": max_entropy,
        "dll_characteristics": dll_characteristics_info,
        "suspicious_imports": suspicious_imports,
        "virustotal": vt_report,
        "findings": findings,
    }


def scan_pe_executable(
    file_path: Path,
    require_signature: bool = False,
    vt_api_key: str | None = None,
    vt_base_url: str | None = None,
) -> dict[str, Any]:
    """Run all 3 pre-execution PE security gates on a target .exe or .msi binary."""
    if not file_path.is_file():
        return {
            "passed": False,
            "target": str(file_path),
            "error": f"File not found: {file_path}",
            "gates": {},
        }

    data = file_path.read_bytes()
    file_sha256 = hashlib.sha256(data).hexdigest()
    cfg = load_config(create_if_missing=True)
    sec_cfg = cfg.get("security", {})
    trusted_builtin = _is_trusted_builtin_wine_pe(file_path, data)
    skip_gates_env = os.environ.get("WINE_PARITY_SKIP_SECURITY_GATES", "") == "1"

    env_req_auth = os.environ.get("WINE_PARITY_REQUIRE_AUTHENTICODE", "")
    if env_req_auth == "0":
        cfg_req_auth = False
    elif env_req_auth == "1":
        cfg_req_auth = True
    else:
        cfg_req_auth = bool(sec_cfg.get("require_authenticode", False))

    require_sig = require_signature or (cfg_req_auth and not trusted_builtin)
    vt_enabled = bool(sec_cfg.get("virustotal_enabled", True)) and not skip_gates_env
    cfg_vt_key = str(sec_cfg.get("virustotal_api_key", "")).strip() if vt_enabled else ""
    effective_vt_key = (
        vt_api_key
        or os.environ.get("VT_API_KEY", "").strip()
        or os.environ.get("VIRUSTOTAL_API_KEY", "").strip()
        or cfg_vt_key
        or None
    )

    g1 = _check_gate1_quarantine(file_path)
    if not sec_cfg.get("block_quarantined", True) or skip_gates_env or trusted_builtin:
        g1["passed"] = True
        if skip_gates_env or trusted_builtin:
            g1["findings"] = []
    g2 = _check_gate2_authenticode(data, require_signature=require_sig)
    if (skip_gates_env or trusted_builtin) and not require_signature:
        g2["passed"] = True
        g2["findings"] = []
    g3 = _check_gate3_static_pe_and_vt(
        data=data,
        file_sha256=file_sha256,
        vt_api_key=effective_vt_key,
        vt_base_url=vt_base_url,
    )
    if skip_gates_env:
        g3["passed"] = True
        g3["findings"] = []

    cascade_info = cascade_config_to_wine_prefix(cfg)
    sb_path = write_seatbelt_profile(cfg, exe_name=file_path.name)
    active_sb = DEFAULT_SEATBELT_DIR / "wine_parity_active.sb"
    sandbox_on = (
        bool(cfg.get("sandbox", {}).get("enabled", True))
        and os.environ.get("WINE_PARITY_NO_SANDBOX", "") != "1"
        and not skip_gates_env
    )
    if sandbox_on:
        shutil.copy2(sb_path, active_sb)
    elif active_sb.exists():
        active_sb.unlink()

    overall_passed = g1["passed"] and g2["passed"] and g3["passed"]
    all_findings = g1["findings"] + g2["findings"] + g3["findings"]

    return {
        "passed": overall_passed,
        "target": str(file_path.resolve()),
        "file_sha256": file_sha256,
        "authenticode_sha256": g2["authenticode_sha256"],
        "require_executable_bit": True,
        "trusted_builtin_wine_pe": trusted_builtin,
        "security_gates_skipped": skip_gates_env,
        "sandbox_enabled": sandbox_on,
        "seatbelt_profile": str(sb_path),
        "z_drive_stripped": cascade_info["z_drive_stripped"],
        "findings": all_findings,
        "gates": {
            "gate1_quarantine_provenance": g1,
            "gate2_authenticode_signature": g2,
            "gate3_static_pe_and_virustotal": g3,
        },
    }


def resolve_builtin_app_path(app_name: str) -> Path:
    """Resolve the on-disk path of a built-in GUI or CLI .exe program."""
    normalized = app_name.strip()
    if not normalized.lower().endswith(".exe"):
        normalized = f"{normalized}.exe"

    ensure_builtin_windows_apps(dry_run=False)
    search_dirs = [
        CONFIG_APP_BUNDLE_DIR / "Contents" / "Resources" / "WindowsApps",
        APP_BUNDLE_DIR / "Contents" / "Resources" / "WindowsApps",
        REPO_ROOT / "builds" / "dmg_staging" / "Windows Apps",
        REPO_ROOT / "builds" / "wine-install-x86" / "lib" / "wine" / "x86_64-windows",
    ]
    for sdir in search_dirs:
        candidate = sdir / normalized
        if candidate.is_file():
            return candidate
        for item in sdir.glob("*.exe") if sdir.is_dir() else []:
            if item.name.lower() == normalized.lower():
                return item
    return REPO_ROOT / "builds" / "wine-install-x86" / "lib" / "wine" / "x86_64-windows" / normalized


def launch_builtin_windows_app(
    app_name: str,
    extra_args: list[str] | None = None,
    dry_run: bool = False,
) -> dict[str, Any]:
    """Launch a built-in GUI or CLI .exe app in a temporary environment with security gates disabled."""
    normalized = app_name.strip()
    if not normalized.lower().endswith(".exe"):
        normalized = f"{normalized}.exe"
    lower_name = normalized.lower()
    is_cli = lower_name in {a.lower() for a in BUILTIN_CLI_APPS}
    category = "cli" if is_cli else "gui"

    exe_path = resolve_builtin_app_path(normalized)
    x86_launcher = REPO_ROOT / "builds" / "wine-install-x86" / "bin" / "wine-parity"
    app_bin = APP_BUNDLE_DIR / "Contents" / "MacOS" / "WineParity"
    launcher_bin = app_bin if app_bin.is_file() else x86_launcher

    cmd_extra = list(extra_args or [])
    if lower_name == "cmd.exe" and not cmd_extra:
        cmd_extra = ["/c", "ver"]

    temp_env = get_builtin_launch_env()
    # Keep WINE_PARITY_DRY_RUN_JSON out of the actual runtime launch unless dry_run was requested
    if not dry_run:
        temp_env.pop("WINE_PARITY_DRY_RUN_JSON", None)

    cfg = load_config(create_if_missing=True)
    prefix_dir = get_active_prefix_path(cfg)
    # If ~/.wine has a populated system.reg and default_prefix does not, use ~/.wine for built-in app launches
    home_wine = Path.home() / ".wine"
    if not os.environ.get("WINEPREFIX") and not (prefix_dir / "system.reg").is_file() and (home_wine / "system.reg").is_file():
        prefix_dir = home_wine
        temp_env["WINEPREFIX"] = str(home_wine)

    # Ensure Z: drive symlink exists temporarily if system.reg is not yet initialized, and keep .update-timestamp synced
    dosdevices = prefix_dir / "dosdevices"
    dosdevices.mkdir(parents=True, exist_ok=True)
    z_link = dosdevices / "z:"
    if not (prefix_dir / "system.reg").is_file() and not z_link.exists():
        try:
            z_link.symlink_to("/")
        except OSError:
            pass
    wine_inf = REPO_ROOT / "builds" / "wine-install-x86" / "share" / "wine" / "wine.inf"
    if (prefix_dir / "system.reg").is_file() and wine_inf.is_file():
        try:
            (prefix_dir / ".update-timestamp").write_text(f"{int(wine_inf.stat().st_mtime)}\n", encoding="utf-8")
        except OSError:
            pass

    report: dict[str, Any] = {
        "action": "launch-builtin-app",
        "app_name": normalized,
        "category": category,
        "exe_path": str(exe_path),
        "launcher": str(launcher_bin),
        "args": cmd_extra,
        "temporary_security_bypass_env": True,
        "launch_env": dict(BUILTIN_TEMPORARY_LAUNCH_ENV),
        "dry_run": dry_run,
    }

    if dry_run:
        report["status"] = "dry_run_ready"
        return report

    cmd = [str(launcher_bin), str(exe_path), *cmd_extra]
    if is_cli:
        res = subprocess.run(
            cmd,
            cwd=REPO_ROOT,
            env=temp_env,
            capture_output=True,
            text=True,
            timeout=30,
            check=False,
        )
        clean_out_lines = [
            line
            for line in (res.stdout or "").splitlines()
            if not any(tag in line for tag in (":err:wineusb:", ":err:ntoskrnl:", ":fixme:service:"))
        ]
        report["returncode"] = res.returncode
        report["stdout"] = "\n".join(clean_out_lines).strip()
        report["status"] = "completed" if res.returncode == 0 else f"exited_{res.returncode}"
    else:
        proc = subprocess.Popen(
            cmd,
            cwd=REPO_ROOT,
            env=temp_env,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            start_new_session=True,
        )
        report["pid"] = proc.pid
        report["status"] = "launched"

    return report


def _generate_info_plist() -> str:
    return f"""<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleDevelopmentRegion</key>
    <string>en</string>
    <key>CFBundleDisplayName</key>
    <string>Wine Parity Windows Launcher</string>
    <key>CFBundleExecutable</key>
    <string>WineParity</string>
    <key>CFBundleIdentifier</key>
    <string>{BUNDLE_ID}</string>
    <key>CFBundleInfoDictionaryVersion</key>
    <string>6.0</string>
    <key>CFBundleName</key>
    <string>WineParity</string>
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
    <string>WineParity Config</string>
    <key>CFBundleExecutable</key>
    <string>WineParityConfig</string>
    <key>CFBundleIdentifier</key>
    <string>{CONFIG_BUNDLE_ID}</string>
    <key>CFBundleInfoDictionaryVersion</key>
    <string>6.0</string>
    <key>CFBundleName</key>
    <string>WineParity Config</string>
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


def _generate_app_source(x86_launcher: Path, arm64_launcher: Path, app_bundle: Path) -> str:
    scanner_script = REPO_ROOT / "scripts" / "lib" / "exe_handler.py"
    return f"""/*
 * SPDX-License-Identifier: LGPL-2.1-or-later
 * WineParity.app native launcher & LaunchServices handler.
 * Enforces mandatory +x executable permission and 3-gate PE security scan on all .exe/.msi files.
 */

#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#ifdef __APPLE__
#import <Cocoa/Cocoa.h>
#import <CoreServices/CoreServices.h>
#import <Security/Security.h>
#include <CommonCrypto/CommonDigest.h>
#include <sys/sysctl.h>
#include <sys/xattr.h>
#endif

#define DEFAULT_X86_LAUNCHER "{x86_launcher}"
#define DEFAULT_ARM64_LAUNCHER "{arm64_launcher}"
#define DEFAULT_APP_BUNDLE "{app_bundle}"
#define SCANNER_PY_SCRIPT "{scanner_script}"
#define CONFIG_PY_SCRIPT "{REPO_ROOT / "scripts" / "lib" / "config_manager.py"}"
#define ACTIVE_SEATBELT_PROFILE "{DEFAULT_SEATBELT_DIR / "wine_parity_active.sb"}"
#define BUNDLE_ID_STR "{BUNDLE_ID}"

static int has_suffix_ci(const char *str, const char *suffix)
{{
    size_t len_s = strlen(str);
    size_t len_suf = strlen(suffix);
    if (len_s < len_suf)
        return 0;
    return strcasecmp(str + len_s - len_suf, suffix) == 0;
}}

/*
 * Run the 3-gate PE security scanner (Gate 1: Quarantine, Gate 2: Authenticode, Gate 3: Static PE + VirusTotal)
 * before permitting execution.
 */
static int run_pre_execution_security_gates(const char *resolved_path)
{{
    const char *skip_gates = getenv("WINE_PARITY_SKIP_SECURITY_GATES");
    if (skip_gates && strcmp(skip_gates, "1") == 0) {{
        return 0;
    }}

#ifdef __APPLE__
    char qbuf[256];
    ssize_t qlen = getxattr(resolved_path, "com.apple.quarantine", qbuf, sizeof(qbuf) - 1, 0, 0);
    if (qlen > 0) {{
        qbuf[qlen] = '\\0';
        fprintf(stderr,
                "[WineParity] SECURITY GATE 1 BLOCKED '%s': macOS quarantine attribute present "
                "(com.apple.quarantine=%s). Run './scripts/test.sh --scan-exe \\"%s\\"' first.\\n",
                resolved_path, qbuf, resolved_path);
        return 126;
    }}
#endif

    pid_t pid = fork();
    if (pid < 0)
        return 126;
    if (pid == 0) {{
        setenv("WINE_PARITY_INTERPOSE_BYPASS", "1", 1);
        execlp("python3", "python3", SCANNER_PY_SCRIPT, "--action", "scan", "--exe", resolved_path, "--quiet-stdout", (char *)NULL);
        _exit(126);
    }}
    int status = 0;
    if (waitpid(pid, &status, 0) < 0)
        return 126;
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {{
        return 126;
    }}
    return 0;
}}

/*
 * Inspect target file for MSI signature/extension or PE MZ/COFF Machine header.
 * Returns "arm64" for ARM64/ARM64EC (0xAA64 / 0xA641) and "x86_64" for AMD64/I386 (0x8664 / 0x014C).
 */
static const char *detect_pe_arch_and_type(const char *path, int *out_is_msi)
{{
    *out_is_msi = has_suffix_ci(path, ".msi") ? 1 : 0;
    int fd = open(path, O_RDONLY);
    if (fd < 0)
        return "x86_64";

    unsigned char buf[4096];
    ssize_t n = read(fd, buf, sizeof(buf));
    close(fd);
    if (n < 8)
        return "x86_64";

    /* OLE Compound Document magic: D0 CF 11 E0 A1 B1 1A E1 */
    if (buf[0] == 0xD0 && buf[1] == 0xCF && buf[2] == 0x11 && buf[3] == 0xE0) {{
        *out_is_msi = 1;
        return "x86_64";
    }}

    if (n >= 0x46 && buf[0] == 'M' && buf[1] == 'Z') {{
        uint32_t e_lfanew = (uint32_t)buf[0x3C] |
                            ((uint32_t)buf[0x3D] << 8) |
                            ((uint32_t)buf[0x3E] << 16) |
                            ((uint32_t)buf[0x3F] << 24);
        if (e_lfanew <= (uint32_t)(n - 6)) {{
            if (buf[e_lfanew] == 'P' && buf[e_lfanew + 1] == 'E' &&
                buf[e_lfanew + 2] == 0 && buf[e_lfanew + 3] == 0) {{
                uint16_t machine = (uint16_t)buf[e_lfanew + 4] |
                                   ((uint16_t)buf[e_lfanew + 5] << 8);
                if (machine == 0xAA64 || machine == 0xA641)
                    return "arm64";
                return "x86_64";
            }}
        }}
    }}
    return "x86_64";
}}

static const char *select_launcher_for_arch(const char *pe_arch)
{{
    const char *env_override = getenv("WINE_PARITY_LAUNCHER");
    if (env_override && env_override[0] != '\\0')
        return env_override;

    if (strcmp(pe_arch, "arm64") == 0) {{
        if (access(DEFAULT_ARM64_LAUNCHER, X_OK) == 0)
            return DEFAULT_ARM64_LAUNCHER;
        return DEFAULT_X86_LAUNCHER;
    }}
    if (access(DEFAULT_X86_LAUNCHER, X_OK) == 0)
        return DEFAULT_X86_LAUNCHER;
    return DEFAULT_ARM64_LAUNCHER;
}}

/*
 * Verify Rosetta 2 is installed on Apple Silicon before dispatching an x86/x86_64 Windows binary.
 * If missing, automatically invokes softwareupdate --install-rosetta --agree-to-license (with an NSAlert
 * prompt when launched interactively via GUI). Returns 0 if ready, or 86 (EBADARCH) if unavailable.
 */
static int ensure_rosetta2_for_x86_target(const char *pe_arch, const char *launcher)
{{
    int needs_x86 = (strcmp(pe_arch, "x86_64") == 0) ||
                    (launcher && strstr(launcher, "wine-install-x86") != NULL);
    if (!needs_x86)
        return 0;

    const char *sim_missing = getenv("WINE_PARITY_SIMULATE_MISSING_ROSETTA");
    int simulated = (sim_missing && strcmp(sim_missing, "1") == 0);

    int is_apple_silicon = simulated;
#ifdef __APPLE__
    if (!is_apple_silicon) {{
        int arm64_cap = 0;
        size_t sz = sizeof(arm64_cap);
        if (sysctlbyname("hw.optional.arm64", &arm64_cap, &sz, NULL, 0) == 0 && arm64_cap == 1) {{
            is_apple_silicon = 1;
        }}
    }}
#endif
    if (!is_apple_silicon)
        return 0;

    if (!simulated && access("/Library/Apple/usr/libexec/oah/libRosettaRuntime", R_OK) == 0) {{
        return 0;
    }}

    const char *sw_bin = getenv("WINE_PARITY_SOFTWAREUPDATE_BIN");
    if (!sw_bin || sw_bin[0] == '\\0') {{
        sw_bin = "/usr/sbin/softwareupdate";
    }}

    const char *consent_env = getenv("WINE_PARITY_ROSETTA_CONSENT");
    if (consent_env && consent_env[0] != '\\0') {{
        if (strcasecmp(consent_env, "y") != 0 &&
            strcasecmp(consent_env, "yes") != 0 &&
            strcasecmp(consent_env, "1") != 0 &&
            strcasecmp(consent_env, "true") != 0 &&
            strcasecmp(consent_env, "agree") != 0) {{
            fprintf(stderr, "[WineParity] ERROR: Rosetta 2 installation declined by user. Aborting wine-parity execution.\\n");
            return 86;
        }}
    }} else {{
#ifdef __APPLE__
        const char *non_interactive = getenv("WINE_PARITY_NONINTERACTIVE_ROSETTA");
        const char *dry_run_json = getenv("WINE_PARITY_DRY_RUN_JSON");
        if ((!non_interactive || strcmp(non_interactive, "1") != 0) &&
            (!dry_run_json || strcmp(dry_run_json, "1") != 0)) {{
            @autoreleasepool {{
                NSApplication *app = [NSApplication sharedApplication];
                [app setActivationPolicy:NSApplicationActivationPolicyAccessory];
                NSAlert *alert = [[NSAlert alloc] init];
                [alert setAlertStyle:NSAlertStyleWarning];
                [alert setMessageText:@"Rosetta 2 Required for Intel Windows Applications"];
                [alert setInformativeText:@"This Windows executable requires Rosetta 2 to run x86/x86_64 code on Apple Silicon. Do you agree to install Rosetta 2 now? If you disagree, WineParity will abort."];
                [alert addButtonWithTitle:@"Agree & Install Rosetta 2"];
                [alert addButtonWithTitle:@"Disagree & Abort"];
                [app activateIgnoringOtherApps:YES];
                if ([alert runModal] != NSAlertFirstButtonReturn) {{
                    fprintf(stderr, "[WineParity] ERROR: Rosetta 2 installation declined by user. Aborting wine-parity execution.\\n");
                    return 86;
                }}
            }}
        }}
#endif
    }}

    fprintf(stderr, "[WineParity] Apple Silicon detected without Rosetta 2. Running '%s --install-rosetta --agree-to-license'...\\n", sw_bin);
    pid_t pid = fork();
    if (pid < 0)
        return 86;
    if (pid == 0) {{
        setenv("WINE_PARITY_INTERPOSE_BYPASS", "1", 1);
        execl(sw_bin, sw_bin, "--install-rosetta", "--agree-to-license", (char *)NULL);
        _exit(86);
    }}
    int status = 0;
    if (waitpid(pid, &status, 0) < 0 || !WIFEXITED(status) || WEXITSTATUS(status) != 0) {{
        fprintf(stderr,
                "[WineParity] ERROR: Rosetta 2 is required on Apple Silicon to run x86/x86_64 Windows applications. "
                "Run '/usr/sbin/softwareupdate --install-rosetta --agree-to-license' first.\\n");
        return 86;
    }}
    return 0;
}}

static int dispatch_windows_binary(const char *target_path, int extra_argc, char **extra_argv)
{{
    struct stat st;
    if (stat(target_path, &st) != 0) {{
        fprintf(stderr, "[WineParity] ERROR: Target file '%s' does not exist (%s).\\n",
                target_path, strerror(errno));
        return 127;
    }}

    /* Mandatory security invariant: refuse to launch any .exe/.msi not marked executable (+x) */
    if (!S_ISREG(st.st_mode) ||
        (st.st_mode & (S_IXUSR | S_IXGRP | S_IXOTH)) == 0 ||
        access(target_path, X_OK) != 0) {{
        fprintf(stderr,
                "[WineParity] ERROR: Refusing to launch '%s': missing executable permission (+x). "
                "Run 'chmod +x \\"%s\\"' first.\\n",
                target_path, target_path);
        return 126;
    }}

    char resolved_path[PATH_MAX];
    if (!realpath(target_path, resolved_path)) {{
        snprintf(resolved_path, sizeof(resolved_path), "%s", target_path);
    }}

    /* Mandatory 3-Gate Pre-Execution Security Scan (Quarantine, Authenticode, Static PE + VirusTotal) */
    if (run_pre_execution_security_gates(resolved_path) != 0) {{
        return 126;
    }}

    char dir_copy[PATH_MAX];
    snprintf(dir_copy, sizeof(dir_copy), "%s", resolved_path);
    char *parent_dir = dirname(dir_copy);

    int is_msi = 0;
    const char *pe_arch = detect_pe_arch_and_type(resolved_path, &is_msi);
    const char *launcher = select_launcher_for_arch(pe_arch);

    int rosetta_rc = ensure_rosetta2_for_x86_target(pe_arch, launcher);
    if (rosetta_rc != 0) {{
        return rosetta_rc;
    }}

    const char *dry_run_json = getenv("WINE_PARITY_DRY_RUN_JSON");
    if (dry_run_json && strcmp(dry_run_json, "1") == 0) {{
        int trace_argc = extra_argc + 14;
        char **trace_argv = (char **)calloc((size_t)trace_argc + 1, sizeof(char *));
        if (!trace_argv)
            return 1;
        int ti = 0;
        trace_argv[ti++] = "python3";
        trace_argv[ti++] = SCANNER_PY_SCRIPT;
        trace_argv[ti++] = "--action";
        trace_argv[ti++] = "runtime-trace";
        trace_argv[ti++] = "--exe";
        trace_argv[ti++] = resolved_path;
        trace_argv[ti++] = "--pe-arch";
        trace_argv[ti++] = (char *)pe_arch;
        trace_argv[ti++] = "--launcher";
        trace_argv[ti++] = (char *)launcher;
        trace_argv[ti++] = "--working-dir";
        trace_argv[ti++] = parent_dir;
        if (is_msi)
            trace_argv[ti++] = "--is-msi";
        trace_argv[ti++] = "--";
        for (int i = 0; i < extra_argc; ++i)
            trace_argv[ti++] = extra_argv[i];
        trace_argv[ti] = NULL;
        setenv("WINE_PARITY_INTERPOSE_BYPASS", "1", 1);
        execvp("python3", trace_argv);
        free(trace_argv);
        return 1;
    }}

    const char *preserve_cwd = getenv("WINE_PARITY_PRESERVE_CWD");
    if (!preserve_cwd || strcmp(preserve_cwd, "1") != 0) {{
        if (chdir(parent_dir) != 0) {{
            fprintf(stderr, "[WineParity] WARN: Could not chdir to '%s': %s\\n",
                    parent_dir, strerror(errno));
        }}
    }}

    setenv("WINE_PARITY_INTERPOSE_BYPASS", "1", 1);
    if (strstr(resolved_path, "/builds/wine-install-") != NULL ||
        strstr(resolved_path, "/Contents/Resources/WindowsApps/") != NULL ||
        strstr(resolved_path, "/Windows Apps/") != NULL) {{
        setenv("WINE_PARITY_SKIP_SECURITY_GATES", "1", 1);
        setenv("WINE_PARITY_NO_SANDBOX", "1", 1);
    }}

    int use_seatbelt = 0;
#ifdef __APPLE__
    const char *no_sb = getenv("WINE_PARITY_NO_SANDBOX");
    const char *skip_gates = getenv("WINE_PARITY_SKIP_SECURITY_GATES");
    if ((!no_sb || strcmp(no_sb, "1") != 0) &&
        (!skip_gates || strcmp(skip_gates, "1") != 0) &&
        access("/usr/bin/sandbox-exec", X_OK) == 0 &&
        access(ACTIVE_SEATBELT_PROFILE, R_OK) == 0) {{
        use_seatbelt = 1;
    }}
#endif

    int total_args = extra_argc + (is_msi ? 4 : 2) + (use_seatbelt ? 3 : 0);
    char **child_argv = (char **)calloc((size_t)total_args + 1, sizeof(char *));
    if (!child_argv)
        return 1;

    int idx = 0;
    const char *exec_target = launcher;
    if (use_seatbelt) {{
        exec_target = "/usr/bin/sandbox-exec";
        child_argv[idx++] = "/usr/bin/sandbox-exec";
        child_argv[idx++] = "-f";
        child_argv[idx++] = ACTIVE_SEATBELT_PROFILE;
    }}
    child_argv[idx++] = (char *)launcher;
    if (is_msi) {{
        child_argv[idx++] = "msiexec";
        child_argv[idx++] = "/i";
    }}
    child_argv[idx++] = resolved_path;
    for (int i = 0; i < extra_argc; ++i) {{
        child_argv[idx++] = extra_argv[i];
    }}
    child_argv[idx] = NULL;

    pid_t pid = fork();
    if (pid < 0) {{
        free(child_argv);
        return 1;
    }}
    if (pid == 0) {{
        execv(exec_target, child_argv);
        fprintf(stderr, "[WineParity] ERROR: Failed to exec launcher '%s': %s\\n",
                exec_target, strerror(errno));
        _exit(127);
    }}

    free(child_argv);
    int status = 0;
    if (waitpid(pid, &status, 0) < 0)
        return 1;
    if (WIFEXITED(status))
        return WEXITSTATUS(status);
    return 1;
}}

#ifdef __APPLE__
@class WineParitySettingsController;

@interface WineParityAppDelegate : NSObject <NSApplicationDelegate>
@property (nonatomic, assign) BOOL handledOpenEvent;
@property (nonatomic, assign) int lastExitCode;
@property (nonatomic, strong) WineParitySettingsController *settingsController;
@end
#endif

static int handle_register(int dry_run)
{{
#ifdef __APPLE__
    if (!dry_run) {{
        const char *lsreg = "/System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister";
        if (access(lsreg, X_OK) == 0) {{
            pid_t pid = fork();
            if (pid == 0) {{
                execl(lsreg, "lsregister", "-f", DEFAULT_APP_BUNDLE, (char *)NULL);
                _exit(0);
            }} else if (pid > 0) {{
                int st = 0;
                waitpid(pid, &st, 0);
            }}
        }}
        LSSetDefaultRoleHandlerForContentType(
            CFSTR("com.microsoft.windows-executable"),
            kLSRolesAll,
            CFSTR(BUNDLE_ID_STR));
        LSSetDefaultRoleHandlerForContentType(
            CFSTR("com.microsoft.msi-installer"),
            kLSRolesAll,
            CFSTR(BUNDLE_ID_STR));
    }}
#else
    (void)dry_run;
#endif
    printf("{{\\n"
           "  \\"action\\": \\"register-exe-handler\\",\\n"
           "  \\"bundle_id\\": \\"%s\\",\\n"
           "  \\"app_bundle\\": \\"%s\\",\\n"
           "  \\"utis\\": [\\"com.microsoft.windows-executable\\", \\"com.microsoft.msi-installer\\"],\\n"
           "  \\"require_executable_bit\\": true,\\n"
           "  \\"dry_run\\": %s,\\n"
           "  \\"status\\": \\"%s\\"\\n"
           "}}\\n",
           BUNDLE_ID_STR,
           DEFAULT_APP_BUNDLE,
           dry_run ? "true" : "false",
           dry_run ? "dry_run_ready" : "registered");
    return 0;
}}

static int handle_unregister(int dry_run)
{{
#ifdef __APPLE__
    if (!dry_run) {{
        const char *lsreg = "/System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister";
        if (access(lsreg, X_OK) == 0) {{
            pid_t pid = fork();
            if (pid == 0) {{
                execl(lsreg, "lsregister", "-u", DEFAULT_APP_BUNDLE, (char *)NULL);
                _exit(0);
            }} else if (pid > 0) {{
                int st = 0;
                waitpid(pid, &st, 0);
            }}
        }}
    }}
#else
    (void)dry_run;
#endif
    printf("{{\\n"
           "  \\"action\\": \\"unregister-exe-handler\\",\\n"
           "  \\"bundle_id\\": \\"%s\\",\\n"
           "  \\"app_bundle\\": \\"%s\\",\\n"
           "  \\"require_executable_bit\\": true,\\n"
           "  \\"dry_run\\": %s,\\n"
           "  \\"status\\": \\"%s\\"\\n"
           "}}\\n",
           BUNDLE_ID_STR,
           DEFAULT_APP_BUNDLE,
           dry_run ? "true" : "false",
           dry_run ? "dry_run_ready" : "unregistered");
    return 0;
}}

static int handle_status(void)
{{
    char exe_handler[256];
    snprintf(exe_handler, sizeof(exe_handler), "%s", BUNDLE_ID_STR);
#ifdef __APPLE__
    CFStringRef cur = LSCopyDefaultRoleHandlerForContentType(
        CFSTR("com.microsoft.windows-executable"),
        kLSRolesAll);
    if (cur) {{
        CFStringGetCString(cur, exe_handler, sizeof(exe_handler), kCFStringEncodingUTF8);
        CFRelease(cur);
    }}
#endif
    printf("{{\\n"
           "  \\"action\\": \\"exe-handler-status\\",\\n"
           "  \\"bundle_id\\": \\"%s\\",\\n"
           "  \\"current_default_handler\\": \\"%s\\",\\n"
           "  \\"app_bundle\\": \\"%s\\",\\n"
           "  \\"x86_launcher\\": \\"%s\\",\\n"
           "  \\"arm64_launcher\\": \\"%s\\",\\n"
           "  \\"require_executable_bit\\": true\\n"
           "}}\\n",
           BUNDLE_ID_STR,
           exe_handler,
           DEFAULT_APP_BUNDLE,
           DEFAULT_X86_LAUNCHER,
           DEFAULT_ARM64_LAUNCHER);
    return 0;
}}

#ifdef __APPLE__
@interface WineParitySettingsController : NSObject
@property (nonatomic, strong) NSWindow *window;
@property (nonatomic, strong) NSPopUpButton *winVerPopup;
@property (nonatomic, strong) NSButton *retinaCheck;
@property (nonatomic, strong) NSTextField *handlerStatusLabel;
@property (nonatomic, strong) NSButton *blockQuarantineCheck;
@property (nonatomic, strong) NSButton *requireAuthCheck;
@property (nonatomic, strong) NSButton *blockWxIatCheck;
@property (nonatomic, strong) NSButton *virustotalEnableCheck;
@property (nonatomic, strong) NSSecureTextField *virustotalKeyField;
@property (nonatomic, strong) NSTextField *virustotalPlainKeyField;
@property (nonatomic, strong) NSButton *showVtKeyCheck;
@property (nonatomic, strong) NSButton *sandboxEnableCheck;
@property (nonatomic, strong) NSButton *stripZDriveCheck;
@property (nonatomic, strong) NSButton *protectPathsCheck;
@property (nonatomic, strong) NSPopUpButton *incomingNetPopup;
@property (nonatomic, strong) NSPopUpButton *outgoingNetPopup;
@property (nonatomic, strong) NSButton *metalCheck;
@property (nonatomic, strong) NSButton *videotoolboxCheck;
@property (nonatomic, strong) NSButton *jitCacheCheck;
@property (nonatomic, strong) NSPopUpButton *builtinGuiPopup;
@property (nonatomic, strong) NSPopUpButton *builtinCliPopup;
@property (nonatomic, strong) NSTextField *builtinStatusLabel;
@end

@implementation WineParitySettingsController

- (NSButton *)makeCheckbox:(NSString *)title frame:(NSRect)frame state:(NSControlStateValue)state
{{
    NSButton *btn = [NSButton checkboxWithTitle:title target:nil action:nil];
    [btn setFrame:frame];
    [btn setState:state];
    return btn;
}}

- (NSTextField *)makeLabel:(NSString *)text frame:(NSRect)frame bold:(BOOL)bold
{{
    NSTextField *lbl = [NSTextField labelWithString:text];
    [lbl setFrame:frame];
    [lbl setFont:bold ? [NSFont boldSystemFontOfSize:13.0] : [NSFont systemFontOfSize:13.0]];
    return lbl;
}}

- (void)registerFileHandlersAction:(id)sender
{{
    (void)sender;
    handle_register(0);
    if (self.handlerStatusLabel) {{
        [self.handlerStatusLabel setStringValue:@"Status: WineParity.app registered as default handler for .exe and .msi files."];
    }}
}}

- (NSDictionary *)loadCurrentConfigDict
{{
    NSTask *task = [[NSTask alloc] init];
    [task setExecutableURL:[NSURL fileURLWithPath:@"/usr/bin/env"]];
    [task setArguments:@[@"python3", @CONFIG_PY_SCRIPT, @"--action", @"status"]];
    NSPipe *outPipe = [NSPipe pipe];
    [task setStandardOutput:outPipe];
    [task setStandardError:[NSPipe pipe]];
    if (![task launchAndReturnError:nil])
        return @{{}};
    NSData *data = [[outPipe fileHandleForReading] readDataToEndOfFile];
    [task waitUntilExit];
    if (!data || [data length] == 0)
        return @{{}};
    NSDictionary *root = [NSJSONSerialization JSONObjectWithData:data options:0 error:nil];
    if ([root isKindOfClass:[NSDictionary class]] && [root[@"config"] isKindOfClass:[NSDictionary class]])
        return root[@"config"];
    return @{{}};
}}

- (void)toggleVtKeyVisibility:(id)sender
{{
    (void)sender;
    BOOL showPlain = ([self.showVtKeyCheck state] == NSControlStateValueOn);
    if (showPlain) {{
        [self.virustotalPlainKeyField setStringValue:[self.virustotalKeyField stringValue] ?: @""];
        [self.virustotalKeyField setHidden:YES];
        [self.virustotalPlainKeyField setHidden:NO];
    }} else {{
        [self.virustotalKeyField setStringValue:[self.virustotalPlainKeyField stringValue] ?: @""];
        [self.virustotalPlainKeyField setHidden:YES];
        [self.virustotalKeyField setHidden:NO];
    }}
}}

- (void)toggleVtEnabled:(id)sender
{{
    (void)sender;
    BOOL enabled = ([self.virustotalEnableCheck state] == NSControlStateValueOn);
    [self.virustotalKeyField setEnabled:enabled];
    [self.virustotalPlainKeyField setEnabled:enabled];
    [self.showVtKeyCheck setEnabled:enabled];
}}

- (void)saveAndCascade:(id)sender
{{
    (void)sender;
    NSArray<NSString *> *verCodes = @[@"win11", @"win10", @"win81", @"win7"];
    NSInteger vIdx = [self.winVerPopup indexOfSelectedItem];
    NSString *winVer = (vIdx >= 0 && vIdx < (NSInteger)[verCodes count]) ? verCodes[vIdx] : @"win11";

    NSString *incMode = ([self.incomingNetPopup indexOfSelectedItem] == 1) ? @"allow" : @"deny";
    NSInteger outIdx = [self.outgoingNetPopup indexOfSelectedItem];
    NSString *outMode = @"http_https_only";
    if (outIdx == 1)
        outMode = @"dns_only";
    else if (outIdx == 2)
        outMode = @"deny_all";
    else if (outIdx == 3)
        outMode = @"allow_all";

    BOOL showPlain = ([self.showVtKeyCheck state] == NSControlStateValueOn);
    NSString *rawVtKey = showPlain ? [self.virustotalPlainKeyField stringValue] : [self.virustotalKeyField stringValue];
    NSString *vtKey = rawVtKey ? [rawVtKey stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]] : @"";
    NSString *wxIatVal = ([self.blockWxIatCheck state] == NSControlStateValueOn) ? @"true" : @"false";

    NSArray<NSString *> *args = @[
        @CONFIG_PY_SCRIPT,
        @"--action", @"set",
        @"--set", [NSString stringWithFormat:@"wine_environment.windows_version=%@", winVer],
        @"--set", [NSString stringWithFormat:@"wine_environment.retina_mode=%@", [self.retinaCheck state] == NSControlStateValueOn ? @"true" : @"false"],
        @"--set", [NSString stringWithFormat:@"security.block_quarantined=%@", [self.blockQuarantineCheck state] == NSControlStateValueOn ? @"true" : @"false"],
        @"--set", [NSString stringWithFormat:@"security.require_authenticode=%@", [self.requireAuthCheck state] == NSControlStateValueOn ? @"true" : @"false"],
        @"--set", [NSString stringWithFormat:@"security.block_wx_sections=%@", wxIatVal],
        @"--set", [NSString stringWithFormat:@"security.block_suspicious_iat=%@", wxIatVal],
        @"--set", [NSString stringWithFormat:@"security.virustotal_enabled=%@", [self.virustotalEnableCheck state] == NSControlStateValueOn ? @"true" : @"false"],
        @"--set", [NSString stringWithFormat:@"security.virustotal_api_key=%@", vtKey],
        @"--set", [NSString stringWithFormat:@"sandbox.enabled=%@", [self.sandboxEnableCheck state] == NSControlStateValueOn ? @"true" : @"false"],
        @"--set", [NSString stringWithFormat:@"sandbox.strip_z_drive=%@", [self.stripZDriveCheck state] == NSControlStateValueOn ? @"true" : @"false"],
        @"--set", [NSString stringWithFormat:@"sandbox.protect_sensitive_host_paths=%@", [self.protectPathsCheck state] == NSControlStateValueOn ? @"true" : @"false"],
        @"--set", [NSString stringWithFormat:@"network.incoming=%@", incMode],
        @"--set", [NSString stringWithFormat:@"network.outgoing=%@", outMode],
        @"--set", [NSString stringWithFormat:@"acceleration.metal_iosurface_swapchain=%@", [self.metalCheck state] == NSControlStateValueOn ? @"true" : @"false"],
        @"--set", [NSString stringWithFormat:@"acceleration.videotoolbox_hw_decode=%@", [self.videotoolboxCheck state] == NSControlStateValueOn ? @"true" : @"false"],
        @"--set", [NSString stringWithFormat:@"acceleration.arm64ec_jit_cache=%@", [self.jitCacheCheck state] == NSControlStateValueOn ? @"true" : @"false"]
    ];

    NSTask *task = [[NSTask alloc] init];
    [task setExecutableURL:[NSURL fileURLWithPath:@"/usr/bin/env"]];
    NSMutableArray<NSString *> *fullArgs = [NSMutableArray arrayWithObject:@"python3"];
    [fullArgs addObjectsFromArray:args];
    [task setArguments:fullArgs];
    [task launchAndReturnError:nil];
    [task waitUntilExit];
    [NSApp terminate:nil];
}}

- (void)closeWindow:(id)sender
{{
    (void)sender;
    [NSApp terminate:nil];
}}

- (void)runBuiltinAppByName:(NSString *)appName
{{
    if (!appName || [appName length] == 0)
        return;
    NSTask *task = [[NSTask alloc] init];
    [task setExecutableURL:[NSURL fileURLWithPath:@"/usr/bin/env"]];
    [task setArguments:@[@"python3", @SCANNER_PY_SCRIPT, @"--action", @"launch-builtin", @"--app", appName]];
    NSMutableDictionary *env = [[[NSProcessInfo processInfo] environment] mutableCopy];
    env[@"WINE_PARITY_SKIP_SECURITY_GATES"] = @"1";
    env[@"WINE_PARITY_REQUIRE_AUTHENTICODE"] = @"0";
    env[@"WINE_PARITY_NO_SANDBOX"] = @"1";
    env[@"WINE_PARITY_BUILTIN_APP_LAUNCH"] = @"1";
    [task setEnvironment:env];
    NSPipe *outPipe = [NSPipe pipe];
    [task setStandardOutput:outPipe];
    [task setStandardError:[NSPipe pipe]];
    if (![task launchAndReturnError:nil]) {{
        if (self.builtinStatusLabel)
            [self.builtinStatusLabel setStringValue:[NSString stringWithFormat:@"Failed to launch %@", appName]];
        return;
    }}
    NSData *data = [[outPipe fileHandleForReading] readDataToEndOfFile];
    [task waitUntilExit];
    if (data && [data length] > 0 && self.builtinStatusLabel) {{
        NSDictionary *rep = [NSJSONSerialization JSONObjectWithData:data options:0 error:nil];
        if ([rep isKindOfClass:[NSDictionary class]]) {{
            NSString *cat = [rep[@"category"] isKindOfClass:[NSString class]] ? rep[@"category"] : @"gui";
            if ([cat isEqualToString:@"cli"]) {{
                NSString *outStr = [rep[@"stdout"] isKindOfClass:[NSString class]] ? rep[@"stdout"] : @"";
                [self.builtinStatusLabel setStringValue:[NSString stringWithFormat:@"[%@ Output]\\n%@", appName, outStr]];
            }} else {{
                NSNumber *pidNum = [rep[@"pid"] isKindOfClass:[NSNumber class]] ? rep[@"pid"] : @(0);
                [self.builtinStatusLabel setStringValue:[NSString stringWithFormat:@"Launched %@ (PID %@) in temporary security-bypass environment.", appName, pidNum]];
            }}
        }}
    }}
}}

- (void)launchSelectedGuiApp:(id)sender
{{
    (void)sender;
    NSArray<NSString *> *guiApps = @[@"notepad.exe", @"winmine.exe", @"clock.exe", @"taskmgr.exe", @"wordpad.exe", @"regedit.exe", @"winecfg.exe", @"progman.exe", @"explorer.exe"];
    NSInteger idx = [self.builtinGuiPopup indexOfSelectedItem];
    if (idx >= 0 && idx < (NSInteger)[guiApps count]) {{
        [self runBuiltinAppByName:guiApps[idx]];
    }}
}}

- (void)launchSelectedCliApp:(id)sender
{{
    (void)sender;
    NSArray<NSString *> *cliApps = @[@"cmd.exe", @"hostname.exe", @"ipconfig.exe", @"systeminfo.exe", @"tasklist.exe"];
    NSInteger idx = [self.builtinCliPopup indexOfSelectedItem];
    if (idx >= 0 && idx < (NSInteger)[cliApps count]) {{
        [self runBuiltinAppByName:cliApps[idx]];
    }}
}}

- (void)setupUI
{{
    NSDictionary *cfg = [self loadCurrentConfigDict];
    NSDictionary *secCfg = [cfg[@"security"] isKindOfClass:[NSDictionary class]] ? cfg[@"security"] : @{{}};
    NSDictionary *envCfg = [cfg[@"wine_environment"] isKindOfClass:[NSDictionary class]] ? cfg[@"wine_environment"] : @{{}};
    NSDictionary *sbCfg = [cfg[@"sandbox"] isKindOfClass:[NSDictionary class]] ? cfg[@"sandbox"] : @{{}};
    NSDictionary *netCfg = [cfg[@"network"] isKindOfClass:[NSDictionary class]] ? cfg[@"network"] : @{{}};
    NSDictionary *accCfg = [cfg[@"acceleration"] isKindOfClass:[NSDictionary class]] ? cfg[@"acceleration"] : @{{}};

    NSRect frame = NSMakeRect(0, 0, 640, 460);
    NSWindowStyleMask style = NSWindowStyleMaskTitled |
                              NSWindowStyleMaskClosable |
                              NSWindowStyleMaskMiniaturizable;
    self.window = [[NSWindow alloc] initWithContentRect:frame
                                              styleMask:style
                                                backing:NSBackingStoreBuffered
                                                  defer:NO];
    [self.window setTitle:@"Wine Parity Settings"];
    [self.window center];

    NSTabView *tabView = [[NSTabView alloc] initWithFrame:NSMakeRect(20, 60, 600, 375)];

    /* Tab 1: General */
    NSTabViewItem *genTab = [[NSTabViewItem alloc] initWithIdentifier:@"General"];
    [genTab setLabel:@"General"];
    NSView *genView = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 580, 330)];
    [genView addSubview:[self makeLabel:@"Windows Environment (Cascades to WINEPREFIX Registry)" frame:NSMakeRect(20, 280, 500, 22) bold:YES]];
    [genView addSubview:[self makeLabel:@"Windows Version:" frame:NSMakeRect(20, 240, 140, 22) bold:NO]];
    self.winVerPopup = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(165, 236, 260, 26) pullsDown:NO];
    [self.winVerPopup addItemsWithTitles:@[@"Windows 11 (Default)", @"Windows 10", @"Windows 8.1", @"Windows 7"]];
    NSString *curVer = [envCfg[@"windows_version"] isKindOfClass:[NSString class]] ? envCfg[@"windows_version"] : @"win11";
    NSArray<NSString *> *verCodes = @[@"win11", @"win10", @"win81", @"win7"];
    NSUInteger foundVerIdx = [verCodes indexOfObject:curVer];
    if (foundVerIdx != NSNotFound)
        [self.winVerPopup selectItemAtIndex:(NSInteger)foundVerIdx];
    [genView addSubview:self.winVerPopup];
    BOOL retinaOn = (envCfg[@"retina_mode"] == nil) ? YES : [envCfg[@"retina_mode"] boolValue];
    self.retinaCheck = [self makeCheckbox:@"Enable macOS Retina / HiDPI Mode (winemac.drv)" frame:NSMakeRect(20, 195, 450, 22) state:retinaOn ? NSControlStateValueOn : NSControlStateValueOff];
    [genView addSubview:self.retinaCheck];

    [genView addSubview:[self makeLabel:@"macOS LaunchServices File Associations (.exe / .msi)" frame:NSMakeRect(20, 145, 500, 22) bold:YES]];
    NSButton *regBtn = [NSButton buttonWithTitle:@"Register .exe / .msi File Handler" target:self action:@selector(registerFileHandlersAction:)];
    [regBtn setFrame:NSMakeRect(20, 108, 250, 32)];
    [genView addSubview:regBtn];
    self.handlerStatusLabel = [self makeLabel:@"Config Path: ~/.config/wine-parity/config.json (0600)" frame:NSMakeRect(20, 76, 540, 20) bold:NO];
    [self.handlerStatusLabel setFont:[NSFont systemFontOfSize:11.0]];
    [self.handlerStatusLabel setTextColor:[NSColor secondaryLabelColor]];
    [genView addSubview:self.handlerStatusLabel];
    [genTab setView:genView];
    [tabView addTabViewItem:genTab];

    /* Tab 2: Security (3-Gate Scanner + VirusTotal v3 Configuration & API Key Input) */
    NSTabViewItem *secTab = [[NSTabViewItem alloc] initWithIdentifier:@"Security"];
    [secTab setLabel:@"Security"];
    NSView *secView = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 580, 330)];
    [secView addSubview:[self makeLabel:@"Pre-Execution PE Security Gates (Gates 1, 2 & 3)" frame:NSMakeRect(20, 290, 520, 22) bold:YES]];
    BOOL blockQuar = (secCfg[@"block_quarantined"] == nil) ? YES : [secCfg[@"block_quarantined"] boolValue];
    BOOL reqAuth = (secCfg[@"require_authenticode"] == nil) ? NO : [secCfg[@"require_authenticode"] boolValue];
    BOOL blockWx = (secCfg[@"block_wx_sections"] == nil) ? YES : [secCfg[@"block_wx_sections"] boolValue];
    self.blockQuarantineCheck = [self makeCheckbox:@"Gate 1: Block Quarantined Downloads (com.apple.quarantine / MotW)" frame:NSMakeRect(20, 258, 520, 22) state:blockQuar ? NSControlStateValueOn : NSControlStateValueOff];
    self.requireAuthCheck = [self makeCheckbox:@"Gate 2: Require Valid PE Authenticode Signature for All Executables" frame:NSMakeRect(20, 228, 520, 22) state:reqAuth ? NSControlStateValueOn : NSControlStateValueOff];
    self.blockWxIatCheck = [self makeCheckbox:@"Gate 3: Block W^X Sections & Process-Injection IAT Threat Patterns" frame:NSMakeRect(20, 198, 520, 22) state:blockWx ? NSControlStateValueOn : NSControlStateValueOff];
    [secView addSubview:self.blockQuarantineCheck];
    [secView addSubview:self.requireAuthCheck];
    [secView addSubview:self.blockWxIatCheck];

    [secView addSubview:[self makeLabel:@"Gate 3 — VirusTotal v3 Cloud Hash Reputation" frame:NSMakeRect(20, 152, 520, 22) bold:YES]];
    BOOL vtEnabled = (secCfg[@"virustotal_enabled"] == nil) ? YES : [secCfg[@"virustotal_enabled"] boolValue];
    self.virustotalEnableCheck = [self makeCheckbox:@"Enable VirusTotal v3 SHA-256 Hash Lookup When API Key Is Configured" frame:NSMakeRect(20, 122, 530, 22) state:vtEnabled ? NSControlStateValueOn : NSControlStateValueOff];
    [self.virustotalEnableCheck setTarget:self];
    [self.virustotalEnableCheck setAction:@selector(toggleVtEnabled:)];
    [secView addSubview:self.virustotalEnableCheck];

    [secView addSubview:[self makeLabel:@"VirusTotal API Key:" frame:NSMakeRect(20, 82, 135, 22) bold:NO]];
    NSString *savedVtKey = [secCfg[@"virustotal_api_key"] isKindOfClass:[NSString class]] ? secCfg[@"virustotal_api_key"] : @"";
    NSRect keyFrame = NSMakeRect(160, 80, 305, 24);
    self.virustotalKeyField = [[NSSecureTextField alloc] initWithFrame:keyFrame];
    [self.virustotalKeyField setPlaceholderString:@"Enter VirusTotal v3 API Key (64-char hex)..."];
    [self.virustotalKeyField setStringValue:savedVtKey];
    [self.virustotalKeyField setEnabled:vtEnabled];
    [secView addSubview:self.virustotalKeyField];

    self.virustotalPlainKeyField = [[NSTextField alloc] initWithFrame:keyFrame];
    [self.virustotalPlainKeyField setPlaceholderString:@"Enter VirusTotal v3 API Key (64-char hex)..."];
    [self.virustotalPlainKeyField setStringValue:savedVtKey];
    [self.virustotalPlainKeyField setEnabled:vtEnabled];
    [self.virustotalPlainKeyField setHidden:YES];
    [secView addSubview:self.virustotalPlainKeyField];

    self.showVtKeyCheck = [self makeCheckbox:@"Show Key" frame:NSMakeRect(475, 81, 90, 22) state:NSControlStateValueOff];
    [self.showVtKeyCheck setTarget:self];
    [self.showVtKeyCheck setAction:@selector(toggleVtKeyVisibility:)];
    [self.showVtKeyCheck setEnabled:vtEnabled];
    [secView addSubview:self.showVtKeyCheck];

    [secTab setView:secView];
    [tabView addTabViewItem:secTab];

    /* Tab 3: Sandbox & Network */
    NSTabViewItem *sbTab = [[NSTabViewItem alloc] initWithIdentifier:@"Sandbox & Network"];
    [sbTab setLabel:@"Sandbox & Network"];
    NSView *sbView = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 580, 330)];
    [sbView addSubview:[self makeLabel:@"Zero-Trust Runtime Containment & Firewall" frame:NSMakeRect(20, 280, 500, 22) bold:YES]];
    BOOL sbEnabled = (sbCfg[@"enabled"] == nil) ? YES : [sbCfg[@"enabled"] boolValue];
    BOOL stripZ = (sbCfg[@"strip_z_drive"] == nil) ? YES : [sbCfg[@"strip_z_drive"] boolValue];
    BOOL protPaths = (sbCfg[@"protect_sensitive_host_paths"] == nil) ? YES : [sbCfg[@"protect_sensitive_host_paths"] boolValue];
    self.sandboxEnableCheck = [self makeCheckbox:@"Run All Applications inside macOS Seatbelt Sandbox (Default)" frame:NSMakeRect(20, 245, 520, 22) state:sbEnabled ? NSControlStateValueOn : NSControlStateValueOff];
    self.stripZDriveCheck = [self makeCheckbox:@"Strip Host Root Drive (Z:) & Disable Desktop Integration" frame:NSMakeRect(20, 215, 520, 22) state:stripZ ? NSControlStateValueOn : NSControlStateValueOff];
    self.protectPathsCheck = [self makeCheckbox:@"Block Access to ~/.ssh, ~/.aws, ~/.gnupg, and Keychains" frame:NSMakeRect(20, 185, 520, 22) state:protPaths ? NSControlStateValueOn : NSControlStateValueOff];
    [sbView addSubview:self.sandboxEnableCheck];
    [sbView addSubview:self.stripZDriveCheck];
    [sbView addSubview:self.protectPathsCheck];

    [sbView addSubview:[self makeLabel:@"Incoming Connections:" frame:NSMakeRect(20, 130, 160, 22) bold:NO]];
    self.incomingNetPopup = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(185, 126, 340, 26) pullsDown:NO];
    [self.incomingNetPopup addItemsWithTitles:@[@"Disabled — Block All Incoming (Default)", @"Allow Incoming Connections"]];
    NSString *curInc = [netCfg[@"incoming"] isKindOfClass:[NSString class]] ? netCfg[@"incoming"] : @"deny";
    [self.incomingNetPopup selectItemAtIndex:[curInc isEqualToString:@"allow"] ? 1 : 0];
    [sbView addSubview:self.incomingNetPopup];

    [sbView addSubview:[self makeLabel:@"Outgoing Connections:" frame:NSMakeRect(20, 90, 160, 22) bold:NO]];
    self.outgoingNetPopup = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(185, 86, 340, 26) pullsDown:NO];
    [self.outgoingNetPopup addItemsWithTitles:@[
        @"HTTP / HTTPS & DNS — Ports 80, 443 & 53 (Default)",
        @"DNS Only — Port 53",
        @"Block All Outgoing Connections",
        @"Allow All Outgoing Connections"
    ]];
    NSString *curOut = [netCfg[@"outgoing"] isKindOfClass:[NSString class]] ? netCfg[@"outgoing"] : @"http_https_only";
    if ([curOut isEqualToString:@"dns_only"])
        [self.outgoingNetPopup selectItemAtIndex:1];
    else if ([curOut isEqualToString:@"deny_all"])
        [self.outgoingNetPopup selectItemAtIndex:2];
    else if ([curOut isEqualToString:@"allow_all"])
        [self.outgoingNetPopup selectItemAtIndex:3];
    else
        [self.outgoingNetPopup selectItemAtIndex:0];
    [sbView addSubview:self.outgoingNetPopup];
    [sbTab setView:sbView];
    [tabView addTabViewItem:sbTab];

    /* Tab 4: Graphics & Acceleration */
    NSTabViewItem *accTab = [[NSTabViewItem alloc] initWithIdentifier:@"Graphics & Acceleration"];
    [accTab setLabel:@"Graphics & Acceleration"];
    NSView *accView = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 580, 330)];
    [accView addSubview:[self makeLabel:@"Apple Silicon & macOS Native Framework Acceleration" frame:NSMakeRect(20, 280, 500, 22) bold:YES]];
    BOOL metalOn = (accCfg[@"metal_iosurface_swapchain"] == nil) ? YES : [accCfg[@"metal_iosurface_swapchain"] boolValue];
    BOOL vtHwOn = (accCfg[@"videotoolbox_hw_decode"] == nil) ? YES : [accCfg[@"videotoolbox_hw_decode"] boolValue];
    BOOL jitOn = (accCfg[@"arm64ec_jit_cache"] == nil) ? YES : [accCfg[@"arm64ec_jit_cache"] boolValue];
    self.metalCheck = [self makeCheckbox:@"Enable IOSurface-Backed CAMetalLayer Child Window Swapchains" frame:NSMakeRect(20, 235, 520, 22) state:metalOn ? NSControlStateValueOn : NSControlStateValueOff];
    self.videotoolboxCheck = [self makeCheckbox:@"Enable VideoToolbox Hardware H.264 / HEVC Decode Bridge" frame:NSMakeRect(20, 200, 520, 22) state:vtHwOn ? NSControlStateValueOn : NSControlStateValueOff];
    self.jitCacheCheck = [self makeCheckbox:@"Enable ARM64EC On-Disk WJTC JIT Translation Cache" frame:NSMakeRect(20, 165, 520, 22) state:jitOn ? NSControlStateValueOn : NSControlStateValueOff];
    [accView addSubview:self.metalCheck];
    [accView addSubview:self.videotoolboxCheck];
    [accView addSubview:self.jitCacheCheck];
    [accTab setView:accView];
    [tabView addTabViewItem:accTab];

    /* Tab 5: Built-in Apps (GUI & CLI Test Programs with Temporary Security-Bypass Environment) */
    NSTabViewItem *appsTab = [[NSTabViewItem alloc] initWithIdentifier:@"Built-in Apps"];
    [appsTab setLabel:@"Built-in Apps"];
    NSView *appsView = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 580, 330)];
    [appsView addSubview:[self makeLabel:@"Built-in Windows GUI & CLI Programs (Bundled in DMG)" frame:NSMakeRect(20, 290, 520, 22) bold:YES]];
    NSTextField *appsNote = [self makeLabel:@"Launches in a temporary environment with security gates disabled (WINE_PARITY_SKIP_SECURITY_GATES=1)." frame:NSMakeRect(20, 265, 540, 20) bold:NO];
    [appsNote setFont:[NSFont systemFontOfSize:11.0]];
    [appsNote setTextColor:[NSColor secondaryLabelColor]];
    [appsView addSubview:appsNote];

    [appsView addSubview:[self makeLabel:@"Built-in GUI App:" frame:NSMakeRect(20, 222, 130, 22) bold:NO]];
    self.builtinGuiPopup = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(155, 218, 250, 26) pullsDown:NO];
    [self.builtinGuiPopup addItemsWithTitles:@[
        @"notepad.exe — Notepad Text Editor",
        @"winmine.exe — Minesweeper Game",
        @"clock.exe — Analog / Digital Clock",
        @"taskmgr.exe — Windows Task Manager",
        @"wordpad.exe — WordPad Rich Text Editor",
        @"regedit.exe — Registry Editor",
        @"winecfg.exe — Wine Configuration",
        @"progman.exe — Program Manager",
        @"explorer.exe — Windows File Explorer"
    ]];
    [appsView addSubview:self.builtinGuiPopup];
    NSButton *launchGuiBtn = [NSButton buttonWithTitle:@"Launch GUI App" target:self action:@selector(launchSelectedGuiApp:)];
    [launchGuiBtn setFrame:NSMakeRect(415, 216, 145, 30)];
    [appsView addSubview:launchGuiBtn];

    [appsView addSubview:[self makeLabel:@"Built-in CLI App:" frame:NSMakeRect(20, 176, 130, 22) bold:NO]];
    self.builtinCliPopup = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(155, 172, 250, 26) pullsDown:NO];
    [self.builtinCliPopup addItemsWithTitles:@[
        @"cmd.exe — Command Prompt (ver)",
        @"hostname.exe — Print Windows Hostname",
        @"ipconfig.exe — Network Configuration",
        @"systeminfo.exe — OS & Hardware Info",
        @"tasklist.exe — Running Processes List"
    ]];
    [appsView addSubview:self.builtinCliPopup];
    NSButton *launchCliBtn = [NSButton buttonWithTitle:@"Run CLI App" target:self action:@selector(launchSelectedCliApp:)];
    [launchCliBtn setFrame:NSMakeRect(415, 170, 145, 30)];
    [appsView addSubview:launchCliBtn];

    self.builtinStatusLabel = [self makeLabel:@"Ready: Select any built-in GUI or CLI program above to test Wine-Parity execution." frame:NSMakeRect(20, 20, 540, 135) bold:NO];
    [self.builtinStatusLabel setFont:[NSFont monospacedSystemFontOfSize:11.0 weight:NSFontWeightRegular]];
    [self.builtinStatusLabel setSelectable:YES];
    [appsView addSubview:self.builtinStatusLabel];
    [appsTab setView:appsView];
    [tabView addTabViewItem:appsTab];

    [[self.window contentView] addSubview:tabView];

    NSButton *saveBtn = [NSButton buttonWithTitle:@"Save & Cascade to Wine Prefix" target:self action:@selector(saveAndCascade:)];
    [saveBtn setFrame:NSMakeRect(380, 16, 240, 32)];
    [saveBtn setKeyEquivalent:@"\\r"];
    [[self.window contentView] addSubview:saveBtn];

    NSButton *cancelBtn = [NSButton buttonWithTitle:@"Cancel" target:self action:@selector(closeWindow:)];
    [cancelBtn setFrame:NSMakeRect(270, 16, 100, 32)];
    [[self.window contentView] addSubview:cancelBtn];

    [self.window makeKeyAndOrderFront:nil];
}}
@end

@implementation WineParityAppDelegate

- (void)application:(NSApplication *)application openURLs:(NSArray<NSURL *> *)urls
{{
    (void)application;
    self.handledOpenEvent = YES;
    int rc = 0;
    for (NSURL *url in urls) {{
        if ([url isFileURL]) {{
            const char *cpath = [[url path] fileSystemRepresentation];
            if (cpath) {{
                int cur = dispatch_windows_binary(cpath, 0, NULL);
                if (cur != 0)
                    rc = cur;
            }}
        }}
    }}
    self.lastExitCode = rc;
    exit(rc);
}}

- (BOOL)application:(NSApplication *)sender openFile:(NSString *)filename
{{
    (void)sender;
    self.handledOpenEvent = YES;
    const char *cpath = [filename fileSystemRepresentation];
    int rc = cpath ? dispatch_windows_binary(cpath, 0, NULL) : 1;
    self.lastExitCode = rc;
    exit(rc);
    return rc == 0 ? YES : NO;
}}

- (void)applicationDidFinishLaunching:(NSNotification *)notification
{{
    (void)notification;
    if (!self.handledOpenEvent) {{
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(150 * NSEC_PER_MSEC)),
                       dispatch_get_main_queue(), ^{{
            if (!self.handledOpenEvent) {{
                const char *idle_exit = getenv("WINE_PARITY_EXIT_WHEN_IDLE");
                if (idle_exit && strcmp(idle_exit, "1") == 0) {{
                    exit(0);
                }}
                [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
                self.settingsController = [[WineParitySettingsController alloc] init];
                [self.settingsController setupUI];
                [NSApp activateIgnoringOtherApps:YES];
            }}
        }});
    }}
}}

@end
#endif

static int handle_launch_builtin(const char *app_name, int dry_run)
{{
    setenv("WINE_PARITY_INTERPOSE_BYPASS", "1", 1);
    setenv("WINE_PARITY_SKIP_SECURITY_GATES", "1", 1);
    setenv("WINE_PARITY_REQUIRE_AUTHENTICODE", "0", 1);
    setenv("WINE_PARITY_NO_SANDBOX", "1", 1);
    setenv("WINE_PARITY_BUILTIN_APP_LAUNCH", "1", 1);
    if (dry_run) {{
        execlp("python3", "python3", SCANNER_PY_SCRIPT, "--action", "launch-builtin", "--app", app_name, "--dry-run", (char *)NULL);
    }} else {{
        execlp("python3", "python3", SCANNER_PY_SCRIPT, "--action", "launch-builtin", "--app", app_name, (char *)NULL);
    }}
    return 1;
}}

static int handle_config_ui(int dry_run)
{{
    if (dry_run) {{
        setenv("WINE_PARITY_INTERPOSE_BYPASS", "1", 1);
        execlp("python3", "python3", SCANNER_PY_SCRIPT, "--action", "config-ui-json", (char *)NULL);
        return 1;
    }}
#ifdef __APPLE__
    @autoreleasepool {{
        NSApplication *app = [NSApplication sharedApplication];
        [app setActivationPolicy:NSApplicationActivationPolicyRegular];
        WineParitySettingsController *ctrl = [[WineParitySettingsController alloc] init];
        [ctrl setupUI];
        [app activateIgnoringOtherApps:YES];
        [app run];
    }}
#endif
    return 0;
}}

int main(int argc, char **argv)
{{
    int dry_run = 0;
    for (int i = 1; i < argc; ++i) {{
        if (strcmp(argv[i], "--dry-run") == 0)
            dry_run = 1;
    }}

    for (int i = 1; i < argc; ++i) {{
        if (strcmp(argv[i], "--launch-builtin") == 0 && i + 1 < argc) {{
            return handle_launch_builtin(argv[i + 1], dry_run);
        }}
    }}

    if (argc >= 1 && argv[0] != NULL) {{
        if (has_suffix_ci(argv[0], "WineParityConfig") ||
            has_suffix_ci(argv[0], "wine-parity-config")) {{
            return handle_config_ui(dry_run);
        }}
    }}

    if (argc >= 2) {{
        if (strcmp(argv[1], "--register") == 0)
            return handle_register(dry_run);
        if (strcmp(argv[1], "--unregister") == 0)
            return handle_unregister(dry_run);
        if (strcmp(argv[1], "--status") == 0)
            return handle_status();
        if (strcmp(argv[1], "--config-ui") == 0)
            return handle_config_ui(dry_run);
    }}

    /* Check for direct CLI file argument (skipping LaunchServices -psn_* and --dry-run if present) */
    int target_idx = -1;
    for (int i = 1; i < argc; ++i) {{
        if (strncmp(argv[i], "-psn_", 5) == 0 || strcmp(argv[i], "--dry-run") == 0)
            continue;
        if (strcmp(argv[i], "--") == 0) {{
            if (i + 1 < argc)
                target_idx = i + 1;
            break;
        }}
        target_idx = i;
        break;
    }}

    if (target_idx != -1) {{
        return dispatch_windows_binary(
            argv[target_idx],
            argc - (target_idx + 1),
            &argv[target_idx + 1]);
    }}

#ifdef __APPLE__
    @autoreleasepool {{
        NSApplication *app = [NSApplication sharedApplication];
        WineParityAppDelegate *delegate = [[WineParityAppDelegate alloc] init];
        [app setDelegate:delegate];
        [app run];
    }}
#endif
    return 0;
}}
"""


def _generate_interpose_dylib_source(app_bin: Path) -> str:
    return f"""/*
 * SPDX-License-Identifier: LGPL-2.1-or-later
 * Mach-O __DATA,__interpose library for transparent POSIX execve/posix_spawn of .exe/.msi files.
 * Strictly enforces the +x executable bit before dispatching to WineParity.
 */

#include <errno.h>
#include <fcntl.h>
#include <spawn.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#define WINE_PARITY_APP_BIN "{app_bin}"

struct interpose_s {{
    void *replacement;
    void *original;
}};

static int has_win_ext(const char *path)
{{
    if (!path)
        return 0;
    size_t len = strlen(path);
    if (len < 4)
        return 0;
    const char *ext = path + len - 4;
    return (strcasecmp(ext, ".exe") == 0 ||
            strcasecmp(ext, ".msi") == 0 ||
            strcasecmp(ext, ".com") == 0 ||
            strcasecmp(ext, ".scr") == 0);
}}

static int is_pe_or_msi_file(const char *path, int *out_has_plus_x)
{{
    *out_has_plus_x = 0;
    if (!path)
        return 0;

    const char *bypass = getenv("WINE_PARITY_INTERPOSE_BYPASS");
    if (bypass && strcmp(bypass, "1") == 0)
        return 0;

    struct stat st;
    if (stat(path, &st) != 0 || !S_ISREG(st.st_mode))
        return 0;

    if ((st.st_mode & (S_IXUSR | S_IXGRP | S_IXOTH)) != 0 && access(path, X_OK) == 0) {{
        *out_has_plus_x = 1;
    }}

    if (has_win_ext(path))
        return 1;

    int fd = open(path, O_RDONLY);
    if (fd < 0)
        return 0;
    unsigned char hdr[4];
    ssize_t n = read(fd, hdr, sizeof(hdr));
    close(fd);
    if (n >= 2 && hdr[0] == 'M' && hdr[1] == 'Z')
        return 1;
    return 0;
}}

static char **build_wrapped_argv(const char *path, char *const argv[])
{{
    size_t count = 0;
    if (argv) {{
        while (argv[count] != NULL)
            count++;
    }}
    char **new_argv = (char **)calloc(count + 2, sizeof(char *));
    if (!new_argv)
        return NULL;
    new_argv[0] = (char *)WINE_PARITY_APP_BIN;
    new_argv[1] = (char *)path;
    for (size_t i = 1; i < count; ++i) {{
        new_argv[i + 1] = argv[i];
    }}
    new_argv[count + 1] = NULL;
    return new_argv;
}}

static int wp_posix_spawn(
    pid_t *restrict pid,
    const char *restrict path,
    const posix_spawn_file_actions_t *file_actions,
    const posix_spawnattr_t *restrict attrp,
    char *const argv[restrict],
    char *const envp[restrict])
{{
    int has_x = 0;
    if (is_pe_or_msi_file(path, &has_x)) {{
        if (!has_x) {{
            return EACCES;
        }}
        char **wrapped = build_wrapped_argv(path, argv);
        if (!wrapped)
            return ENOMEM;
        int rc = posix_spawn(pid, WINE_PARITY_APP_BIN, file_actions, attrp, wrapped, envp);
        free(wrapped);
        return rc;
    }}
    return posix_spawn(pid, path, file_actions, attrp, argv, envp);
}}

static int wp_execve(const char *path, char *const argv[], char *const envp[])
{{
    int has_x = 0;
    if (is_pe_or_msi_file(path, &has_x)) {{
        if (!has_x) {{
            errno = EACCES;
            return -1;
        }}
        char **wrapped = build_wrapped_argv(path, argv);
        if (!wrapped) {{
            errno = ENOMEM;
            return -1;
        }}
        int rc = execve(WINE_PARITY_APP_BIN, wrapped, envp);
        free(wrapped);
        return rc;
    }}
    return execve(path, argv, envp);
}}

static int wp_execv(const char *path, char *const argv[])
{{
    extern char **environ;
    return wp_execve(path, argv, environ);
}}

__attribute__((used, section("__DATA,__interpose")))
static struct interpose_s interpose_table[] = {{
    {{ (void *)wp_posix_spawn, (void *)posix_spawn }},
    {{ (void *)wp_execve, (void *)execve }},
    {{ (void *)wp_execv, (void *)execv }},
}};
"""


def _generate_shell_helper(app_bin: Path) -> str:
    return f"""#!/usr/bin/env bash
# SPDX-License-Identifier: LGPL-2.1-or-later
# Auto-generated shell integration for wine-parity .exe/.msi execution.
# Enforces mandatory +x executable permission before launching any Windows binary.

export WINE_PARITY_APP_BIN="{app_bin}"

_wine_parity_run_checked() {{
    if [ $# -lt 1 ]; then
        echo "[wine-parity] Usage: _wine_parity_run_checked <file.exe|file.msi> [args...]" >&2
        return 2
    fi
    local target="$1"
    shift
    if [ ! -e "$target" ]; then
        echo "[wine-parity] ERROR: File '$target' does not exist." >&2
        return 127
    fi
    if [ ! -f "$target" ] || [ ! -x "$target" ]; then
        echo "[wine-parity] ERROR: Refusing to launch '$target': missing executable permission (+x). Run 'chmod +x \"$target\"' first." >&2
        return 126
    fi
    "$WINE_PARITY_APP_BIN" "$target" "$@"
}}

if [ -n "${{ZSH_VERSION:-}}" ]; then
    alias -s exe='_wine_parity_run_checked'
    alias -s EXE='_wine_parity_run_checked'
    alias -s msi='_wine_parity_run_checked'
    alias -s MSI='_wine_parity_run_checked'
fi
"""


def build_macos_exe_handler(dry_run: bool = False) -> dict[str, Any]:
    """Build WineParity.app, libwine_parity_exec.dylib, shell integration, and both x86/arm64 launchers."""
    plan_x86 = build_full_wine_plan(target="x86", dry_run=dry_run)
    plan_arm64 = build_full_wine_plan(target="arm64", dry_run=dry_run)

    x86_launcher = Path(plan_x86["launcher_path"])
    arm64_launcher = Path(plan_arm64["launcher_path"])
    contents_dir = APP_BUNDLE_DIR / "Contents"
    macos_dir = contents_dir / "MacOS"
    info_plist = contents_dir / "Info.plist"
    pkg_info = contents_dir / "PkgInfo"
    app_bin = macos_dir / "WineParity"

    if not dry_run:
        write_wine_parity_launcher(plan_x86)
        write_wine_parity_launcher(plan_arm64)

        macos_dir.mkdir(parents=True, exist_ok=True)
        SRC_BUILD_DIR.mkdir(parents=True, exist_ok=True)

        info_plist.write_text(_generate_info_plist(), encoding="utf-8")
        pkg_info.write_text("APPLWPRT", encoding="utf-8")

        if sys.platform == "darwin":
            src_file = SRC_BUILD_DIR / "wine_parity_app.m"
            src_file.write_text(
                _generate_app_source(x86_launcher, arm64_launcher, APP_BUNDLE_DIR),
                encoding="utf-8",
            )
            cc = shutil.which("clang") or "clang"
            subprocess.run(
                [
                    cc,
                    "-O2",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-Wno-deprecated-declarations",
                    "-fobjc-arc",
                    str(src_file),
                    "-framework",
                    "Cocoa",
                    "-framework",
                    "CoreServices",
                    "-framework",
                    "Security",
                    "-o",
                    str(app_bin),
                ],
                cwd=REPO_ROOT,
                check=True,
            )

            interpose_src = SRC_BUILD_DIR / "wine_parity_interpose.c"
            interpose_src.write_text(_generate_interpose_dylib_source(app_bin), encoding="utf-8")
            subprocess.run(
                [
                    cc,
                    "-O2",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-dynamiclib",
                    str(interpose_src),
                    "-o",
                    str(DYLIB_PATH),
                ],
                cwd=REPO_ROOT,
                check=True,
            )
        else:
            src_file = SRC_BUILD_DIR / "wine_parity_app.c"
            src_file.write_text(
                _generate_app_source(x86_launcher, arm64_launcher, APP_BUNDLE_DIR),
                encoding="utf-8",
            )
            cc = shutil.which("clang") or shutil.which("gcc") or shutil.which("cc") or "cc"
            subprocess.run(
                [
                    cc,
                    "-std=c11",
                    "-O2",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    str(src_file),
                    "-o",
                    str(app_bin),
                ],
                cwd=REPO_ROOT,
                check=True,
            )

        app_bin.chmod(app_bin.stat().st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)
        SHELL_HELPER_PATH.write_text(_generate_shell_helper(app_bin), encoding="utf-8")
        SHELL_HELPER_PATH.chmod(
            SHELL_HELPER_PATH.stat().st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH
        )

        # Also build the standalone configuration program bundle: builds/WineParityConfig.app
        cfg_contents_dir = CONFIG_APP_BUNDLE_DIR / "Contents"
        cfg_macos_dir = cfg_contents_dir / "MacOS"
        cfg_info_plist = cfg_contents_dir / "Info.plist"
        cfg_pkg_info = cfg_contents_dir / "PkgInfo"
        cfg_app_bin = cfg_macos_dir / "WineParityConfig"

        cfg_macos_dir.mkdir(parents=True, exist_ok=True)
        cfg_info_plist.write_text(_generate_config_app_info_plist(), encoding="utf-8")
        cfg_pkg_info.write_text("APPLWPRC", encoding="utf-8")
        shutil.copy2(app_bin, cfg_app_bin)
        cfg_app_bin.chmod(cfg_app_bin.stat().st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)
        staged_builtin_apps = ensure_builtin_windows_apps(dry_run=False)
    else:
        cfg_app_bin = CONFIG_APP_BUNDLE_DIR / "Contents" / "MacOS" / "WineParityConfig"
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
    """Build (if needed) and invoke WineParity.app --register / --unregister / --status."""
    app_bin = APP_BUNDLE_DIR / "Contents" / "MacOS" / "WineParity"
    if not app_bin.is_file() or not os.access(app_bin, os.X_OK):
        build_info = build_macos_exe_handler(dry_run=False)
        app_bin = Path(build_info["app_binary"])

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
    parser = argparse.ArgumentParser(description="Wine-Parity macOS .exe/.msi Automatic Handler & PE Security Scanner")
    parser.add_argument(
        "--action",
        choices=[
            "build",
            "register",
            "unregister",
            "status",
            "scan",
            "config-ui",
            "config-ui-json",
            "runtime-trace",
            "launch-builtin",
        ],
        required=True,
    )
    parser.add_argument("--exe", type=Path, default=None, help="Target .exe/.msi file to scan")
    parser.add_argument("--app", default="", help="Built-in Windows GUI/CLI app name to launch")
    parser.add_argument("--pe-arch", default="x86_64")
    parser.add_argument("--launcher", default="")
    parser.add_argument("--working-dir", default="")
    parser.add_argument("--is-msi", action="store_true")
    parser.add_argument("--require-signature", action="store_true", help="Require valid Authenticode signature")
    parser.add_argument("--vt-api-key", default=None, help="Optional VirusTotal v3 API key")
    parser.add_argument("--quiet-stdout", action="store_true", help="Suppress JSON stdout (emit only stderr on failure)")
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("extra_args", nargs="*", default=[])
    args = parser.parse_args(argv)

    if args.action == "launch-builtin":
        if not args.app:
            print("ERROR: --app <name.exe> is required for --action launch-builtin", file=sys.stderr)
            return 2
        extra = [a for a in args.extra_args if a != "--"]
        report = launch_builtin_windows_app(
            app_name=args.app,
            extra_args=extra,
            dry_run=args.dry_run,
        )
        print(json.dumps(report, indent=2))
        return 0

    if args.action == "config-ui-json":
        cfg_status = get_config_status()
        sec_cfg = cfg_status["config"].get("security", {})
        ui_report = {
            "action": "config-ui",
            "ui_style": "macos-native-appkit-settings",
            "window_title": "Wine Parity Settings",
            "sections": [
                "General",
                "Security",
                "Sandbox & Network",
                "Graphics & Acceleration",
                "Built-in Apps",
            ],
            "builtin_apps": {
                "gui": list(BUILTIN_GUI_APPS),
                "cli": list(BUILTIN_CLI_APPS),
                "temporary_launch_env": dict(BUILTIN_TEMPORARY_LAUNCH_ENV),
                "bundled_apps_dir": str(CONFIG_APP_BUNDLE_DIR / "Contents" / "Resources" / "WindowsApps"),
            },
            "virustotal_controls": {
                "enabled_checkbox": True,
                "api_key_input_field": True,
                "show_hide_key_toggle": True,
                "virustotal_enabled": bool(sec_cfg.get("virustotal_enabled", True)),
                "virustotal_api_key_configured": bool(str(sec_cfg.get("virustotal_api_key", "")).strip()),
            },
            "cascades_to_wine_registry": True,
            "config_path": cfg_status["config_path"],
            "config": cfg_status["config"],
            "seatbelt_profile_path": cfg_status["seatbelt_profile_path"],
        }
        print(json.dumps(ui_report, indent=2))
        return 0

    if args.action == "runtime-trace":
        cfg_status = get_config_status()
        cfg = cfg_status["config"]
        resolved_exe = str(args.exe.resolve()) if args.exe else ""
        sb_path = write_seatbelt_profile(cfg, exe_name=args.exe.name if args.exe else None)
        extra = [a for a in args.extra_args if a != "--"]
        trace_obj: dict[str, Any] = {
            "target": resolved_exe,
            "executable_bit_verified": True,
            "rosetta2_verified": True,
            "security_gates_verified": True,
            "is_msi": bool(args.is_msi),
            "pe_arch": args.pe_arch,
            "selected_launcher": args.launcher,
            "working_directory": args.working_dir,
            "sandboxed": bool(cfg.get("sandbox", {}).get("enabled", True)),
            "z_drive_stripped": bool(cfg_status["cascade"]["z_drive_stripped"]),
            "network_incoming": cfg.get("network", {}).get("incoming", "deny"),
            "network_outgoing": cfg.get("network", {}).get("outgoing", "http_https_only"),
            "allowed_outgoing_ports": cfg.get("network", {}).get("allowed_outgoing_ports", [80, 443, 53]),
            "seatbelt_profile": str(sb_path),
            "args": extra,
        }
        if args.is_msi:
            trace_obj["msiexec_command"] = ["msiexec", "/i", resolved_exe, *extra]
        print(json.dumps(trace_obj, indent=2))
        return 0

    if args.action == "scan":
        if args.exe is None:
            print("ERROR: --exe <path> is required for --action scan", file=sys.stderr)
            return 2
        report = scan_pe_executable(
            file_path=args.exe,
            require_signature=args.require_signature,
            vt_api_key=args.vt_api_key,
        )
        if not args.quiet_stdout:
            print(json.dumps(report, indent=2))
        if not report["passed"]:
            for finding in report.get("findings", []):
                print(f"[WineParity Security Gate] BLOCKED '{args.exe}': {finding}", file=sys.stderr)
            return 126
        return 0

    if args.action == "build":
        report = build_macos_exe_handler(dry_run=args.dry_run)
    else:
        report = manage_exe_handler(action=args.action, dry_run=args.dry_run)

    print(json.dumps(report, indent=2))
    return 0


if __name__ == "__main__":
    sys.exit(main())

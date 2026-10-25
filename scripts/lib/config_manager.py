#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""User-level configuration manager, Wine registry hive cascader, and macOS Seatbelt sandbox generator."""

from __future__ import annotations

import argparse
import copy
import json
import os
import re
import stat
import sys
from pathlib import Path
from typing import Any

REPO_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_SEATBELT_DIR = REPO_ROOT / "builds" / "sandbox_profiles"

DEFAULT_CONFIG: dict[str, Any] = {
    "schema_version": 1,
    "wine_environment": {
        "windows_version": "win11",
        "retina_mode": True,
        "virtual_desktop": False,
        "virtual_desktop_resolution": "1920x1080",
        "prefix_mode": "shared",
        "default_prefix": "~/.wine-parity-prefix",
        "dll_overrides": {},
    },
    "security": {
        "require_executable_bit": True,
        "block_quarantined": True,
        "require_authenticode": False,
        "block_wx_sections": True,
        "block_suspicious_iat": True,
        "virustotal_enabled": True,
        "virustotal_api_key": "",
    },
    "sandbox": {
        "enabled": True,
        "strip_z_drive": True,
        "disable_winemenubuilder": True,
        "protect_sensitive_host_paths": True,
        "extra_denied_paths": [],
        "extra_allowed_paths": [],
    },
    "network": {
        "incoming": "deny",
        "allowed_incoming_ports": [],
        "outgoing": "http_https_only",
        "allowed_outgoing_ports": [80, 443],
    },
    "acceleration": {
        "metal_iosurface_swapchain": True,
        "videotoolbox_hw_decode": True,
        "arm64ec_jit_cache": True,
    },
    "app_profiles": {},
}


def get_config_path() -> Path:
    """Resolve active config.json path from WINE_PARITY_CONFIG or ~/.config/wine-parity/config.json."""
    env_cfg = os.environ.get("WINE_PARITY_CONFIG", "").strip()
    if env_cfg:
        return Path(env_cfg).expanduser().resolve()
    return (Path.home() / ".config" / "wine-parity" / "config.json").resolve()


def get_active_prefix_path(cfg: dict[str, Any]) -> Path:
    """Resolve active WINEPREFIX path from environment or config.json."""
    env_prefix = os.environ.get("WINEPREFIX", "").strip()
    if env_prefix:
        return Path(env_prefix).expanduser().resolve()
    default_prefix = cfg.get("wine_environment", {}).get("default_prefix", "~/.wine-parity-prefix")
    return Path(default_prefix).expanduser().resolve()


def _deep_merge(base: dict[str, Any], override: dict[str, Any]) -> dict[str, Any]:
    merged = copy.deepcopy(base)
    for k, v in override.items():
        if isinstance(v, dict) and isinstance(merged.get(k), dict) and k != "app_profiles":
            merged[k] = _deep_merge(merged[k], v)
        else:
            merged[k] = copy.deepcopy(v)
    return merged


def save_config(cfg: dict[str, Any], config_path: Path | None = None) -> Path:
    """Persist config.json with 0600 user-only permissions."""
    target = config_path or get_config_path()
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(json.dumps(cfg, indent=2) + "\n", encoding="utf-8")
    target.chmod(stat.S_IRUSR | stat.S_IWUSR)
    return target


def load_config(config_path: Path | None = None, create_if_missing: bool = True) -> dict[str, Any]:
    """Load config.json, merging with DEFAULT_CONFIG and creating on disk if missing."""
    target = config_path or get_config_path()
    if not target.is_file():
        cfg = copy.deepcopy(DEFAULT_CONFIG)
        if create_if_missing:
            save_config(cfg, target)
        return cfg

    try:
        raw = json.loads(target.read_text(encoding="utf-8"))
        if not isinstance(raw, dict):
            raw = {}
    except (OSError, json.JSONDecodeError):
        raw = {}

    merged = _deep_merge(DEFAULT_CONFIG, raw)
    return merged


def _parse_scalar_or_list(val_str: str) -> Any:
    s = val_str.strip()
    if s.lower() == "true":
        return True
    if s.lower() == "false":
        return False
    if re.fullmatch(r"-?\d+", s):
        return int(s)
    if "," in s and all(re.fullmatch(r"\d+", part.strip()) for part in s.split(",") if part.strip()):
        return [int(part.strip()) for part in s.split(",") if part.strip()]
    return s


def apply_key_value_updates(cfg: dict[str, Any], updates: list[str]) -> dict[str, Any]:
    """Apply CLI key=value updates (supporting app_profiles.<app.exe>.<field>=value)."""
    updated = copy.deepcopy(cfg)
    for item in updates:
        if "=" not in item:
            continue
        key_expr, raw_val = item.split("=", 1)
        key_expr = key_expr.strip()
        val = _parse_scalar_or_list(raw_val)

        if key_expr.startswith("app_profiles."):
            rest = key_expr[len("app_profiles.") :].strip()
            # Split from right by last dot so 'studio.exe.windows_version' splits into ('studio.exe', 'windows_version')
            if "." in rest:
                app_name, field_name = rest.rsplit(".", 1)
                app_name = app_name.strip()
                field_name = field_name.strip()
                updated.setdefault("app_profiles", {}).setdefault(app_name, {})[field_name] = val
            continue

        parts = [p.strip() for p in key_expr.split(".") if p.strip()]
        cur = updated
        for p in parts[:-1]:
            if p not in cur or not isinstance(cur[p], dict):
                cur[p] = {}
            cur = cur[p]
        if parts:
            cur[parts[-1]] = val

    # Keep allowed_outgoing_ports synchronized when outgoing mode changes
    net = updated.get("network", {})
    if net.get("outgoing") == "http_https_only" and not net.get("allowed_outgoing_ports"):
        net["allowed_outgoing_ports"] = [80, 443]

    return updated


def _upsert_wine_reg_section(reg_text: str, section_path: str, kv_pairs: dict[str, str]) -> str:
    """Non-destructively update or insert keys in a Wine .reg section while preserving all other keys."""
    header_pattern = re.compile(
        r"^(\[" + re.escape(section_path) + r"\][^\n]*\n)(.*?)(?=^\[|\Z)",
        re.MULTILINE | re.DOTALL,
    )
    match = header_pattern.search(reg_text)
    if match:
        sec_header = match.group(1)
        sec_body = match.group(2)
        lines = sec_body.splitlines()
        remaining_keys = dict(kv_pairs)
        new_lines: list[str] = []
        for line in lines:
            replaced = False
            for k, formatted_val in list(remaining_keys.items()):
                key_prefix = f'"{k}"='
                if line.strip().startswith(key_prefix):
                    new_lines.append(f'"{k}"={formatted_val}')
                    del remaining_keys[k]
                    replaced = True
                    break
            if not replaced:
                new_lines.append(line)
        for k, formatted_val in remaining_keys.items():
            new_lines.append(f'"{k}"={formatted_val}')
        rebuilt_section = sec_header + "\n".join(new_lines).rstrip("\n") + "\n\n"
        return reg_text[: match.start()] + rebuilt_section + reg_text[match.end() :]

    lines_to_add = [f"[{section_path}] 1700000000"]
    for k, formatted_val in kv_pairs.items():
        lines_to_add.append(f'"{k}"={formatted_val}')
    block = "\n".join(lines_to_add) + "\n\n"
    if not reg_text.endswith("\n\n"):
        reg_text = reg_text.rstrip("\n") + "\n\n"
    return reg_text + block


def cascade_config_to_wine_prefix(cfg: dict[str, Any], prefix_dir: Path | None = None) -> dict[str, Any]:
    """Cascade config.json settings into $WINEPREFIX/user.reg and $WINEPREFIX/dosdevices/ non-destructively."""
    target_prefix = prefix_dir or get_active_prefix_path(cfg)
    dosdevices_dir = target_prefix / "dosdevices"
    dosdevices_dir.mkdir(parents=True, exist_ok=True)

    z_stripped = False
    if cfg.get("sandbox", {}).get("strip_z_drive", True):
        for z_name in ("z:", "Z:"):
            z_path = dosdevices_dir / z_name
            if z_path.is_symlink() or z_path.exists():
                try:
                    z_path.unlink()
                except OSError:
                    pass
        z_stripped = not (dosdevices_dir / "z:").exists()

    user_reg = target_prefix / "user.reg"
    if user_reg.is_file():
        reg_text = user_reg.read_text(encoding="utf-8", errors="replace")
    else:
        reg_text = "WINE REGISTRY Version 2\n;; All keys relative to \\\\User\\\\S-1-5-21-0-0-0-1000\n\n"

    wine_env = cfg.get("wine_environment", {})
    win_ver = str(wine_env.get("windows_version", "win11"))
    retina = "y" if wine_env.get("retina_mode", True) else "n"

    reg_text = _upsert_wine_reg_section(
        reg_text,
        "Software\\\\Wine",
        {"Version": f'"{win_ver}"'},
    )
    reg_text = _upsert_wine_reg_section(
        reg_text,
        "Software\\\\Wine\\\\Mac Driver",
        {"RetinaMode": f'"{retina}"'},
    )
    reg_text = _upsert_wine_reg_section(
        reg_text,
        "Software\\\\Wine\\\\Direct3D",
        {"csmt": "dword:00000003"},
    )

    dll_overrides: dict[str, str] = {}
    if cfg.get("sandbox", {}).get("disable_winemenubuilder", True):
        dll_overrides["winemenubuilder.exe"] = '""'
    for dll_k, dll_v in wine_env.get("dll_overrides", {}).items():
        dll_overrides[str(dll_k)] = f'"{dll_v}"'
    if dll_overrides:
        reg_text = _upsert_wine_reg_section(
            reg_text,
            "Software\\\\Wine\\\\DllOverrides",
            dll_overrides,
        )

    for app_name, app_prof in cfg.get("app_profiles", {}).items():
        if isinstance(app_prof, dict) and "windows_version" in app_prof:
            reg_text = _upsert_wine_reg_section(
                reg_text,
                f"Software\\\\Wine\\\\AppDefaults\\\\{app_name}",
                {"Version": f'"{app_prof["windows_version"]}"'},
            )

    user_reg.write_text(reg_text, encoding="utf-8")
    return {
        "prefix_dir": str(target_prefix),
        "user_reg": str(user_reg),
        "z_drive_stripped": z_stripped,
    }


def generate_seatbelt_profile(cfg: dict[str, Any], exe_name: str | None = None) -> str:
    """Synthesize a macOS XNU sandbox-exec Seatbelt profile from config.json."""
    effective_cfg = copy.deepcopy(cfg)
    if exe_name and exe_name in cfg.get("app_profiles", {}):
        app_prof = cfg["app_profiles"][exe_name]
        if isinstance(app_prof, dict):
            effective_cfg = _deep_merge(effective_cfg, app_prof)

    sb_cfg = effective_cfg.get("sandbox", {})
    net_cfg = effective_cfg.get("network", {})
    home = str(Path.home())

    lines = [
        ";; Auto-generated by wine-parity config_manager.py",
        "(version 1)",
        "(allow default)",
        "",
        ";; Always allow local Unix domain sockets for wineserver IPC",
        "(allow network* (local unix))",
    ]

    if sb_cfg.get("protect_sensitive_host_paths", True):
        sensitive_paths = [
            f"{home}/.ssh",
            f"{home}/.aws",
            f"{home}/.gnupg",
            f"{home}/Library/Keychains",
            f"{home}/.config/wine-parity",
            *sb_cfg.get("extra_denied_paths", []),
        ]
        lines.append("")
        lines.append(";; Protect sensitive macOS host credential and configuration directories")
        lines.append("(deny file-read* file-write*")
        for sp in sensitive_paths:
            lines.append(f'    (subpath "{sp}")')
        lines.append(")")

    incoming_mode = str(net_cfg.get("incoming", "deny")).lower()
    lines.append("")
    lines.append(f";; Incoming network policy: {incoming_mode}")
    if incoming_mode == "allow":
        lines.append("(allow network-inbound)")
    elif incoming_mode == "custom_ports":
        lines.append("(deny network-inbound)")
        for port in net_cfg.get("allowed_incoming_ports", []):
            lines.append(f'(allow network-inbound (local tcp "*:{int(port)}"))')
    else:
        lines.append("(deny network-inbound)")

    outgoing_mode = str(net_cfg.get("outgoing", "http_https_only")).lower()
    lines.append("")
    lines.append(f";; Outgoing network policy: {outgoing_mode}")
    if outgoing_mode == "allow_all":
        lines.append("(allow network-outbound)")
    elif outgoing_mode == "deny_all":
        lines.append("(deny network-outbound)")
        lines.append("(allow network-outbound (local unix))")
    else:
        ports = net_cfg.get("allowed_outgoing_ports", [80, 443])
        if outgoing_mode == "http_https_only" and not ports:
            ports = [80, 443]
        lines.append("(deny network-outbound)")
        lines.append("(allow network-outbound (local unix))")
        lines.append('(allow network-outbound (remote udp "*:53") (remote tcp "*:53"))')
        for port in ports:
            lines.append(f'(allow network-outbound (remote tcp "*:{int(port)}"))')

    return "\n".join(lines) + "\n"


def write_seatbelt_profile(cfg: dict[str, Any], exe_name: str | None = None) -> Path:
    """Write the synthesized macOS Seatbelt profile to builds/sandbox_profiles/."""
    DEFAULT_SEATBELT_DIR.mkdir(parents=True, exist_ok=True)
    safe_tag = re.sub(r"[^A-Za-z0-9_.-]", "_", exe_name or "default")
    profile_path = DEFAULT_SEATBELT_DIR / f"wine_parity_{safe_tag}.sb"
    profile_path.write_text(generate_seatbelt_profile(cfg, exe_name=exe_name), encoding="utf-8")
    return profile_path


def get_config_status(
    config_path: Path | None = None,
    updates: list[str] | None = None,
) -> dict[str, Any]:
    """Load (and optionally update) config.json, cascade to Wine prefix, and return full status."""
    cfg_path = config_path or get_config_path()
    cfg = load_config(cfg_path, create_if_missing=True)
    if updates:
        cfg = apply_key_value_updates(cfg, updates)
        save_config(cfg, cfg_path)

    cascade_info = cascade_config_to_wine_prefix(cfg)
    sb_path = write_seatbelt_profile(cfg)
    sb_text = sb_path.read_text(encoding="utf-8")

    return {
        "action": "set-config" if updates else "config-status",
        "config_path": str(cfg_path),
        "config": cfg,
        "cascade": cascade_info,
        "seatbelt_profile_path": str(sb_path),
        "seatbelt_profile_preview": sb_text,
    }


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Wine-Parity User Configuration & Zero-Trust Sandbox Manager")
    parser.add_argument(
        "--action",
        choices=["status", "set", "prepare-runtime"],
        default="status",
    )
    parser.add_argument(
        "--set",
        dest="updates",
        action="append",
        default=[],
        help="Set key=value in config.json and cascade to WINEPREFIX",
    )
    parser.add_argument("--exe", default=None, help="Optional target executable name/path")
    args = parser.parse_args(argv)

    exe_name = Path(args.exe).name if args.exe else None
    status = get_config_status(updates=args.updates if args.updates else None)
    if exe_name:
        sb_path = write_seatbelt_profile(status["config"], exe_name=exe_name)
        status["seatbelt_profile_path"] = str(sb_path)
        status["seatbelt_profile_preview"] = sb_path.read_text(encoding="utf-8")

    print(json.dumps(status, indent=2))
    return 0


if __name__ == "__main__":
    sys.exit(main())

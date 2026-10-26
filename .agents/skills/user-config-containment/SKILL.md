---
name: user-config-containment
description: Manage spumante user-level configuration (~/.config/spumante/config.json), native macOS AppKit configuration UI, non-destructive cascading to Wine registry hives (user.reg/system.reg/dosdevices), and zero-trust runtime sandboxing (sandbox-exec Seatbelt + incoming/outgoing network firewall).
---

# User Configuration & Zero-Trust Containment Skill (`user-config-containment`)

## Overview

Use this skill whenever modifying, extending, or auditing:
- The user-level configuration system (`~/.config/spumante/config.json` and `scripts/lib/config_manager.py`).
- The native macOS AppKit configuration dialog (`builds/Spumante.app --config-ui` / `./scripts/launch.sh --config-ui`).
- Registry hive cascading from `config.json` into `$WINEPREFIX/user.reg`, `$WINEPREFIX/system.reg`, and `$WINEPREFIX/dosdevices/`.
- Runtime zero-trust containment (`sandbox-exec` Seatbelt profiles, `dosdevices/z:` stripping, and incoming/outgoing network rules).

## Mandatory Design Invariants

1. **Host Authority Before Wine Boots**:
   - Security policies, credentials, Seatbelt rules, and network firewall rules MUST reside in `~/.config/spumante/config.json` (`0600` mode, or `SPUMANTE_CONFIG` override) on the macOS host—never solely inside `$WINEPREFIX/user.reg` where an untrusted `.exe` can modify them.
2. **Native macOS Settings UX (`Spumante.app --config-ui`)**:
   - The configuration dialog must follow a clean, native macOS AppKit look-and-feel (replacing the need for cluttered Windows 95-style `winecfg` tabs) while cascading Windows environment choices into `user.reg`/`system.reg` so `winecfg` remains 100% compatible if opened by the user.
3. **Non-Destructive Registry Cascading**:
   - Cascading `config.json` to `$WINEPREFIX/user.reg` and `$WINEPREFIX/system.reg` must surgically update only the managed keys (`HKCU\Software\Wine` `Version`, `Mac Driver`, `DllOverrides`, `AppDefaults`) while preserving all other existing user registry keys.
4. **Secure-by-Default Zero-Trust Containment & Network Policy**:
   - `sandbox.enabled = true`: All `.exe` and `.msi` applications run sandboxed by default.
   - `sandbox.strip_z_drive = true`: Unlinks `$WINEPREFIX/dosdevices/z:` and sets `winemenubuilder.exe=d`.
   - `sandbox.protect_sensitive_host_paths = true`: Blocks read/write access to `~/.ssh`, `~/.aws`, `~/.gnupg`, `~/Library/Keychains`, and `~/.config/spumante` via XNU `sandbox-exec`.
   - `network.incoming = "deny"`: Incoming network connections are disabled by default.
   - `network.outgoing = "http_https_only"`: Outgoing network connections are restricted to TCP ports `80` (HTTP) and `443` (HTTPS) (plus DNS port `53` and local Unix sockets for `wineserver` IPC) by default.

## Canonical Commands

```sh
# Inspect active configuration, registry cascade status, and generated Seatbelt policy
./scripts/launch.sh --config-status

# Update configuration settings and cascade to WINEPREFIX registry hives
./scripts/launch.sh --set-config network.incoming=deny --set-config network.outgoing=http_https_only

# Open the native macOS AppKit Configuration Dialog
./scripts/launch.sh --config-ui

# Build the turnkey macOS DMG installer package (Spumante-Installer.dmg + Spumante Config.app)
# Pass -nz (or --notarize) to code-sign with Hardened Runtime, submit via notarytool, and staple the ticket
./scripts/build.sh --dmg
./scripts/build.sh --dmg -nz
```

## Reference Documentation

Read [`docs/skills/USER_CONFIG_AND_CONTAINMENT.md`](../../../docs/skills/USER_CONFIG_AND_CONTAINMENT.md) and [`docs/skills/THREAT_SECURITY_MODEL.md`](../../../docs/skills/THREAT_SECURITY_MODEL.md) for complete architectural details.

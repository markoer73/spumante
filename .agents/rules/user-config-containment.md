---
trigger: model_decision
description: Use this whenever working on user-level configuration (config.json), the native macOS configuration UI, Wine registry hive cascading (user.reg/dosdevices), or runtime sandboxing and network firewall rules.
---

# User Configuration & Zero-Trust Containment Rules

The authoritative design specification for `config.json`, the native macOS AppKit Settings dialog, Wine registry hive cascading, and default runtime zero-trust containment is defined in `docs/skills/USER_CONFIG_AND_CONTAINMENT.md` and `.agents/skills/user-config-containment/SKILL.md`.

- **Host-Side Security Source of Truth**: Keep security gates, Seatbelt sandbox rules, and network rules in `~/.config/wine-parity/config.json` (`0600` permissions), evaluated before `wineserver` or `wine64` boots.
- **Non-Destructive Registry Cascading**: Cascade Windows environment settings (`windows_version`, `retina_mode`, `dll_overrides`, `app_profiles`) into `$WINEPREFIX/user.reg` and `$WINEPREFIX/dosdevices/` without clobbering unrelated keys set via `winecfg`.
- **Secure Defaults**: By default, applications run sandboxed (`sandbox.enabled = true`, `strip_z_drive = true`), incoming networking is disabled (`network.incoming = "deny"`), and outgoing networking is restricted to HTTP/HTTPS ports `80` and `443` (`network.outgoing = "http_https_only"`).

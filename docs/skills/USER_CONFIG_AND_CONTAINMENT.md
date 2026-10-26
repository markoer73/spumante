# User Configuration, Native macOS Settings UI & Zero-Trust Containment

This document is the authoritative specification for `wine-parity`'s user-level configuration system (`~/.config/wine-parity/config.json`), native macOS AppKit Settings dialog (`WineParity.app --config-ui`), non-destructive registry cascading into upstream Wine's `WINEPREFIX`, and default runtime zero-trust containment (`sandbox-exec` Seatbelt + network filtering).

---

## 1. Core Architectural Design Principles

### Principle 1: Host-Side Authority Before `wineserver` Boots
- **Security Boundary Invariant**: Any `.exe` running inside a Wine prefix has full Win32 registry and filesystem write access to `$WINEPREFIX/user.reg`, `$WINEPREFIX/system.reg`, and `$WINEPREFIX/dosdevices/`.
- Therefore, security gates (`+x` check, macOS `com.apple.quarantine`, PE Authenticode verification, VirusTotal v3 API key), sandbox policies, and network firewall rules **must live on the macOS host** in `~/.config/wine-parity/config.json` (`0600` permissions, or overridden via `WINE_PARITY_CONFIG`) and be enforced *before* `wine64` or `wineserver` starts.

### Principle 2: Native macOS Look-and-Feel (`WineParity.app --config-ui`) Over Cluttered `winecfg`
- Upstream `winecfg` presents a dense Windows 95-style tab sheet that exposes low-level registry plumbing without covering macOS security gates, Apple Silicon hardware acceleration, or XNU Seatbelt sandboxing.
- `wine-parity` provides a **native macOS AppKit Configuration Dialog** built directly into `builds/WineParity.app` (`WineParity --config-ui` / `./scripts/launch.sh --config-ui`) inspired by macOS System Settings:
  1. **General & Windows Environment**: Target Windows version (`win11`, `win10`, `win81`, `win7`), Retina/HiDPI mode (`RetinaMode`), Virtual Desktop toggle, and active `WINEPREFIX` mode (`shared`, `per_app`, `ephemeral`).
  2. **Security & Verification**: Mandatory `+x` executable check, macOS Quarantine (`com.apple.quarantine`) enforcement, PE Authenticode requirement (`require_authenticode`), and optional VirusTotal v3 scan key.
  3. **Sandbox & Network Firewall**: Sandboxed execution toggle (enabled by default), Drive `Z:` stripping (`strip_z_drive`), sensitive host directory protection (`~/.ssh`, `~/.aws`, `~/.gnupg`, `~/Library/Keychains`), incoming network policy (`deny` by default), and outgoing network policy (`http_https_only` on TCP ports `80`, `443` & DNS port `53` by default).
  4. **Apple Silicon & Hardware Acceleration**: Metal / `IOSurface` swapchains, `VideoToolbox` H.264/HEVC hardware decode, and ARM64EC `WJTC` on-disk JIT cache.

### Principle 3: Automatic Non-Destructive Cascading to Wine Registry Hives (`user.reg` & `system.reg`)
- Saving or applying `config.json` automatically cascades relevant Windows environment settings into `$WINEPREFIX/user.reg`, `$WINEPREFIX/system.reg`, and `$WINEPREFIX/dosdevices/` so users never *need* to open `winecfg`:
  - `HKCU\Software\Wine` → `"Version"="win11"` (or configured version)
  - `HKCU\Software\Wine\Mac Driver` → `"RetinaMode"="y"` / `"n"`
  - `HKCU\Software\Wine\Direct3D` → `"csmt"=dword:00000003`
  - `HKCU\Software\Wine\DllOverrides` → merges user DLL overrides with `wine-parity` built-in overrides (`winemenubuilder.exe` disabled when sandboxed)
  - `HKCU\Software\Wine\AppDefaults\<app.exe>` → per-application Windows version and DLL overrides
  - `$WINEPREFIX/dosdevices/z:` → automatically unlinked when `sandbox.strip_z_drive` is `true`
- **Full `winecfg` Coexistence**: Cascading updates only the targeted registry keys inside `user.reg` and preserves all other keys. Users who still wish to run `winecfg` can do so at any time without conflict.

### Principle 4: Zero-Trust Default Containment & Network Policy
By default, every `.exe` and `.msi` launched via `WineParity.app` or `bin/wine-parity` runs under strict zero-trust containment unless explicitly relaxed in `config.json`:
1. **`sandbox.enabled = true`**: Wraps the Wine process tree in a generated macOS XNU Seatbelt profile (`sandbox-exec -f <profile.sb>`).
2. **`sandbox.strip_z_drive = true`**: Unlinks `$WINEPREFIX/dosdevices/z:` prior to launch and disables `winemenubuilder.exe`.
3. **`sandbox.protect_sensitive_host_paths = true`**: Kernel-level Seatbelt `(deny file-read* file-write* ...)` on `~/.ssh`, `~/.aws`, `~/.gnupg`, `~/Library/Keychains`, and `~/.config/wine-parity`.
4. **`network.incoming = "deny"`**: Blocks all inbound TCP/UDP listening sockets (`(deny network-inbound)`) while allowing local Unix domain sockets required by `wineserver` IPC.
5. **`network.outgoing = "http_https_only"`** (`allowed_outgoing_ports = [80, 443, 53]`): Blocks arbitrary outbound TCP/UDP connections (`(deny network-outbound)`) and explicitly permits only local Unix domain sockets, DNS resolution (`udp:53`, `tcp:53`), loopback (`localhost`), and outbound TCP connections on ports `80` (HTTP) and `443` (HTTPS). Supported modes: `"http_https_only"` (default), `"dns_only"`, `"allow_all"`, `"custom_ports"`, and `"deny_all"`.

---

## 2. Canonical Schema (`~/.config/wine-parity/config.json`)

```json
{
  "schema_version": 1,
  "wine_environment": {
    "windows_version": "win11",
    "retina_mode": true,
    "virtual_desktop": false,
    "virtual_desktop_resolution": "1920x1080",
    "prefix_mode": "shared",
    "default_prefix": "~/.wine-parity-prefix",
    "dll_overrides": {}
  },
  "security": {
    "require_executable_bit": true,
    "block_quarantined": true,
    "require_authenticode": false,
    "block_wx_sections": true,
    "block_suspicious_iat": true,
    "virustotal_api_key": ""
  },
  "sandbox": {
    "enabled": true,
    "strip_z_drive": true,
    "disable_winemenubuilder": true,
    "protect_sensitive_host_paths": true,
    "extra_denied_paths": [],
    "extra_allowed_paths": []
  },
  "network": {
    "incoming": "deny",
    "allowed_incoming_ports": [],
    "outgoing": "http_https_only",
    "allowed_outgoing_ports": [80, 443, 53]
  },
  "acceleration": {
    "metal_iosurface_swapchain": true,
    "videotoolbox_hw_decode": true,
    "arm64ec_jit_cache": true
  },
  "app_profiles": {}
}
```

---

## 3. Canonical Script Entrypoints & Turnkey macOS DMG Installer

All configuration, cascading, Seatbelt profile generation, native UI operations, and DMG packaging run through the repository's canonical scripts in `scripts/`:

```sh
# 1. Initialize or inspect the active configuration (and generated Seatbelt + registry cascade plan)
./scripts/launch.sh --config-status

# 2. Launch the native macOS AppKit Configuration Dialog (or --dry-run to preview state)
./scripts/launch.sh --config-ui

# 3. Update configuration keys from CLI and cascade to the Wine prefix registry hives
./scripts/launch.sh --set-config network.outgoing=http_https_only --set-config network.incoming=deny --set-config wine_environment.windows_version=win11

# 4. Build the turnkey macOS DMG installer package (builds/WineParity-Installer.dmg)
#    Includes WineParity.app, standalone WineParity Config.app, and Install WineParity.command
#    Pass -nz (or --notarize) to code-sign with Hardened Runtime, submit via xcrun notarytool, and staple the ticket
./scripts/build.sh --dmg
./scripts/build.sh --dmg -nz
```

### Turnkey macOS `.dmg` Package, Standalone `WineParity Config.app` & Apple Notarization (`-nz`)

Running `./scripts/build.sh --dmg` (implemented in [`scripts/lib/dmg_packager.py`](../../scripts/lib/dmg_packager.py)) builds `builds/WineParity-Installer.dmg` from `builds/dmg_staging/`, containing:
1. **`WineParity.app`** (`org.wine-parity.launcher`): The `.exe` / `.msi` LaunchServices dispatcher and 3-gate PE security scanner. Launching `WineParity.app` directly without a target file opens the native **Wine Parity Settings** window.
2. **`WineParity Config.app`** (`builds/WineParityConfig.app`, `org.wine-parity.config`): A standalone macOS AppKit application bundle that launches the 4-tab **Wine Parity Settings** window (`General`, `Security`, `Sandbox & Network`, `Graphics & Acceleration`) directly when opened from `/Applications`, Dock, or Spotlight.
3. **`Install WineParity.command`**: A double-clickable macOS installer script that installs `WineParity.app` and `WineParity Config.app` into `/Applications` (or `~/Applications`), creates the `wine-parity` and `wine-parity-config` CLI wrappers, registers `.exe` / `.msi` LaunchServices associations, initializes `~/.config/wine-parity/config.json` (`0600`) + `$WINEPREFIX/user.reg`, and opens `WineParity Config.app`.
4. **Apple Notarization (`-nz` / `--notarize`)**: Passing `-nz` (with optional `--codesign-identity <id>` and `--notary-profile <profile>`) signs all staged bundles and the `.dmg` with Hardened Runtime (`codesign --force --options runtime --timestamp`), submits the package via `xcrun notarytool submit ... --wait`, and staples the ticket via `xcrun stapler staple`.

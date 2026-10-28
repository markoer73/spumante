# User Configuration, Native macOS Settings UI & Zero-Trust Containment

This document is the authoritative specification for `spumante`'s user-level configuration system (`~/.config/spumante/config.json`), native macOS AppKit Settings dialog (`Spumante.app --config-ui`), non-destructive registry cascading into upstream Wine's `WINEPREFIX`, and default runtime zero-trust containment (`sandbox-exec` Seatbelt + network filtering).

---

## 1. Core Architectural Design Principles

### Principle 1: Host-Side Authority Before `wineserver` Boots
- **Security Boundary Invariant**: Any `.exe` running inside a Wine prefix has full Win32 registry and filesystem write access to `$WINEPREFIX/user.reg`, `$WINEPREFIX/system.reg`, and `$WINEPREFIX/dosdevices/`.
- Therefore, security gates (`+x` check, macOS `com.apple.quarantine`, PE Authenticode verification, VirusTotal v3 API key), sandbox policies, and network firewall rules **must live on the macOS host** in `~/.config/spumante/config.json` (`0600` permissions, or overridden via `SPUMANTE_CONFIG`) and be enforced *before* `wine64` or `wineserver` starts.

### Principle 2: Native macOS Look-and-Feel (`Spumante.app --config-ui`) Over Cluttered `winecfg`
- Upstream `winecfg` presents a dense Windows 95-style tab sheet that exposes low-level registry plumbing without covering macOS security gates, Apple Silicon hardware acceleration, or XNU Seatbelt sandboxing.
- `spumante` provides a **native macOS AppKit Configuration Dialog** built directly into `builds/Spumante.app` (`Spumante --config-ui` / `./scripts/launch.sh --config-ui`) inspired by macOS System Settings:
  1. **General & Windows Environment**: Target Windows version (`win11`, `win10`, `win81`, `win7`), Retina/HiDPI mode (`RetinaMode`), Virtual Desktop toggle, and active `WINEPREFIX` mode (`shared`, `per_app`, `ephemeral`).
  2. **Security & Verification**: Mandatory `+x` executable check, macOS Quarantine (`com.apple.quarantine`) enforcement, PE Authenticode requirement (`require_authenticode`), and optional VirusTotal v3 scan key.
  3. **Sandbox & Network Firewall**: Sandboxed execution toggle (enabled by default), Drive `Z:` stripping (`strip_z_drive`), sensitive host directory protection (`~/.ssh`, `~/.aws`, `~/.gnupg`, `~/Library/Keychains`), incoming network policy (`deny` by default), and outgoing network policy (`http_https_only` on TCP ports `80`, `443` & DNS port `53` by default).
  4. **Apple Silicon & Hardware Acceleration**: Metal / `IOSurface` swapchains, `VideoToolbox` H.264/HEVC hardware decode, and ARM64EC `WJTC` on-disk JIT cache.
  5. **Built-in Apps, Interactive Windows Console (`wineconsole.exe`) & Custom-Labeled Favourite Apps (`NSStatusItem` Menu Bar Tray)**:
     - `Spumante Config.app` runs as an always-available macOS menu bar tray icon (`NSStatusItem` with `Contents/Resources/spumante-logo.png`) that stays open when the Settings window is closed.
     - Users can configure custom **Favourite Windows Applications** with custom display labels (e.g. `{"label": "Task Manager", "path": "C:/Windows/taskman.exe"}`) in the **Built-in Apps** tab (`favoriteAppLabelField`, `favoriteAppPathField`, `browseFavoriteAppPathAction:`, `saveFavoriteAppEntryAction:`), from the menu bar (`Add Favourite Windows App…`), or via CLI (`--set-config "favorite_apps.add=Task Manager|C:/Windows/taskman.exe"`).
     - Built-in CLI utilities (`cmd.exe`, `hostname.exe`, `ipconfig.exe`, `systeminfo.exe`, `tasklist.exe`) launched from the menu bar tray or via **Open Console** (`--open-console`) execute inside Wine's native GUI Windows Console terminal (`wineconsole.exe cmd.exe /k <app.exe>`) so the terminal window stays open for inspection.

### Principle 3: Automatic Non-Destructive Cascading to Wine Registry Hives (`user.reg` & `system.reg`)
- Saving or applying `config.json` automatically cascades relevant Windows environment settings into `$WINEPREFIX/user.reg`, `$WINEPREFIX/system.reg`, and `$WINEPREFIX/dosdevices/` so users never *need* to open `winecfg`:
  - `HKCU\Software\Wine` → `"Version"="win11"` (or configured version)
  - `HKCU\Software\Wine\Mac Driver` → `"RetinaMode"="y"` / `"n"`
  - `HKCU\Software\Wine\Direct3D` → `"csmt"=dword:00000003`
  - `HKCU\Software\Wine\DllOverrides` → merges user DLL overrides with `spumante` built-in overrides (`winemenubuilder.exe` disabled when sandboxed)
  - `HKCU\Software\Wine\AppDefaults\<app.exe>` → per-application Windows version and DLL overrides
  - `$WINEPREFIX/dosdevices/z:` → automatically unlinked when `sandbox.strip_z_drive` is `true`
- **Full `winecfg` Coexistence**: Cascading updates only the targeted registry keys inside `user.reg` and preserves all other keys. Users who still wish to run `winecfg` can do so at any time without conflict.

### Principle 4: Zero-Trust Default Containment & Network Policy
By default, every `.exe` and `.msi` launched via `Spumante.app` or `bin/spumante` runs under strict zero-trust containment unless explicitly relaxed in `config.json`:
1. **`sandbox.enabled = true`**: Wraps the Wine process tree in a generated macOS XNU Seatbelt profile (`sandbox-exec -f <profile.sb>`).
2. **`sandbox.strip_z_drive = true`**: Unlinks `$WINEPREFIX/dosdevices/z:` prior to launch and disables `winemenubuilder.exe`.
3. **`sandbox.protect_sensitive_host_paths = true`**: Kernel-level Seatbelt `(deny file-read* file-write* ...)` on `~/.ssh`, `~/.aws`, `~/.gnupg`, `~/Library/Keychains`, and `~/.config/spumante`.
4. **`network.incoming = "deny"`**: Blocks all inbound TCP/UDP listening sockets (`(deny network-inbound)`) while allowing local Unix domain sockets required by `wineserver` IPC.
5. **`network.outgoing = "http_https_only"`** (`allowed_outgoing_ports = [80, 443, 53]`): Blocks arbitrary outbound TCP/UDP connections (`(deny network-outbound)`) and explicitly permits only local Unix domain sockets, DNS resolution (`udp:53`, `tcp:53`), loopback (`localhost`), and outbound TCP connections on ports `80` (HTTP) and `443` (HTTPS). Supported modes: `"http_https_only"` (default), `"dns_only"`, `"allow_all"`, `"custom_ports"`, and `"deny_all"`.

---

## 2. Canonical Schema (`~/.config/spumante/config.json`)

```json
{
  "schema_version": 1,
  "wine_environment": {
    "windows_version": "win11",
    "retina_mode": true,
    "virtual_desktop": false,
    "virtual_desktop_resolution": "1920x1080",
    "prefix_mode": "shared",
    "default_prefix": "~/.spumante",
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
  "favorite_apps": [
    {
      "label": "Task Manager",
      "path": "C:/Windows/taskman.exe"
    }
  ],
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

# 4. Build the turnkey macOS DMG installer package (builds/Spumante-Installer.dmg)
#    Includes Spumante.app, standalone Spumante Config.app, and Install Spumante.command
#    Pass -nz (or --notarize) to code-sign with Hardened Runtime, submit via xcrun notarytool, and staple the ticket
./scripts/build.sh --dmg
./scripts/build.sh --dmg -nz
```

### Turnkey macOS `.dmg` Package, Standalone `Spumante Config.app` & Apple Notarization (`-nz`)

Running `./scripts/build.sh --dmg` (implemented in [`scripts/lib/dmg_packager.py`](../../scripts/lib/dmg_packager.py)) builds `builds/Spumante-Installer.dmg` from `builds/dmg_staging/`, containing:
1. **`Spumante.app`** (`org.spumante.launcher`): The `.exe` / `.msi` LaunchServices dispatcher and 3-gate PE security scanner. Launching `Spumante.app` directly without a target file opens the native **Spumante Settings** window.
2. **`Spumante Config.app`** (`builds/SpumanteConfig.app`, `org.spumante.config`): A standalone macOS AppKit application bundle that launches the 4-tab **Spumante Settings** window (`General`, `Security`, `Sandbox & Network`, `Graphics & Acceleration`) directly when opened from `/Applications`, Dock, or Spotlight.
3. **`Install Spumante.command`**: A double-clickable macOS installer script that installs `Spumante.app` and `Spumante Config.app` into `/Applications` (or `~/Applications`), creates the `spumante` and `spumante-config` CLI wrappers, registers `.exe` / `.msi` LaunchServices associations, initializes `~/.config/spumante/config.json` (`0600`) + `$WINEPREFIX/user.reg`, and opens `Spumante Config.app`.
4. **Apple Notarization (`-nz` / `--notarize`)**: Passing `-nz` (with optional `--codesign-identity <id>` and `--notary-profile <profile>`) signs all staged bundles and the `.dmg` with Hardened Runtime (`codesign --force --options runtime --timestamp`), submits the package via `xcrun notarytool submit ... --wait`, and staples the ticket via `xcrun stapler staple`.

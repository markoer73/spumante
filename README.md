# Wine Parity Project (`wine-parity`)

An engineering initiative exploring the use of autonomous AI coding agents to systematically bring **Wine (Wine Is Not an Emulator)** to feature and UX parity with modern Windows—prioritizing **macOS first** (Apple Silicon & Intel, native Apple frameworks) and **Linux second**.

---

## 1. Project Overview & Premise

Wine has been in continuous development for over 30 years, translating Windows API calls (Win32, NT, COM, DirectX) to POSIX/Darwin system calls on macOS and Linux. While projects like Proton and DXVK have made massive strides in gaming, thousands of Win32 and NT APIs remain unimplemented stubs (`fixme:` annotations).

Rather than attempting an impossible real-time "AI-powered runtime" (which would be orders of magnitude too slow and non-deterministic), this project uses **agentic AI at development time** within a **closed-loop differential testing harness**.

---

## 2. The Core Architecture: Closed-Loop Ground-Truth Harness

```
 ┌───────────────────────────────────────────────────────────────┐
 │ 1. TARGET DISCOVERY (./scripts/test.sh --discover-stubs)      │
 │    • Layer 1: .spec '@ stub' declarations                     │
 │    • Layer 2: C source full/partial 'FIXME' branches          │
 │    • Layer 3: Dynamic WINEDEBUG=+relay,+fixme logs            │
 └───────────────────────────────┬───────────────────────────────┘
                                 │
                                 ▼
 ┌───────────────────────────────────────────────────────────────┐
 │ 2. CLEAN-ROOM SPEC & TEST SYNTHESIS                           │
 │    • Fetch spec packet (./scripts/test.sh --fetch-spec)       │
 │    • Write dual-mode C conformance test (parity_test.h)       │
 └───────────────────────────────┬───────────────────────────────┘
                                 │
                 ┌───────────────┴───────────────┐
                 ▼                               ▼
 ┌───────────────────────────────┐ ┌─────────────────────────────┐
 │ 3A. WINDOWS 10/11 VM (QEMU)   │ │ 3B. RUN ON WINE (macOS/Linux│
 │     • VirtIO serial qemu-ga   │ │     • Ephemeral /tmp prefix │
 │     • QMP loadvm + JSON cache │ │     • <1s single-DLL build  │
 └───────────────┬───────────────┘ └─────────────┬───────────────┘
                 │                               │
                 └───────────────┬───────────────┘
                                 │
                                 ▼
 ┌───────────────────────────────────────────────────────────────┐
 │ 4. SURGICAL IMPLEMENTATION & PATCH SYNTHESIS                  │
 │    • ./scripts/build.sh --dll <name> --test <test_name>       │
 │    • ./scripts/test.sh --dll <name> --test <test_name>        │
 │    • Loop until Wine output matches Ground Truth 100%         │
 └───────────────────────────────┬───────────────────────────────┘
                                 │
                                 ▼
 ┌───────────────────────────────────────────────────────────────┐
 │ 5. SECURITY, UPSTREAM WINETEST & SUBMISSION                   │
 │    • ./scripts/test.sh --security --dll <name> --test <name>  │
 │    • ./scripts/test.sh --winetest --dll <name>                │
 │    • ./scripts/test.sh --lint                                 │
 │    • ./scripts/build.sh --dll <name> --export-patch           │
 └───────────────────────────────────────────────────────────────┘
```

---

## 3. Canonical Script Interface & Surgical Single-Target Workflow

All host/VM orchestration, compilation, differential testing, stub discovery, and linting are centralized into **four canonical, parametrized shell scripts** in `scripts/` (backed by internal modules in `scripts/lib/`). Full-tree rebuilds are refused by default to prevent rebuilding 1,000+ targets when verifying a single API or test case:

```sh
# 1. Surgically build ONLY a single DLL and conformance test (<1s)
./scripts/build.sh --dll bcrypt --test test_symmetric_key

# 2. Run ONLY the targeted differential test (with optional case filter)
./scripts/test.sh --dll bcrypt --test test_symmetric_key --filter "AES128*"

# 3. Run 4-gate security audit & upstream Wine winetest A/B anti-regression suite
./scripts/test.sh --security --dll bcrypt --test test_crypto_primitives
./scripts/test.sh --winetest --dll bcrypt

# 4. Build & verify Apple Silicon (arm64) execution parity, ARM64X loader/TSC/JIT cache & macOS NTSync in ntdll and kernelbase
./scripts/build.sh --dll ntdll --test test_apple_silicon_parity
./scripts/test.sh --dll ntdll --test test_apple_silicon_parity
./scripts/build.sh --dll ntdll --test test_ntsync_macos_parity
./scripts/test.sh --dll ntdll --test test_ntsync_macos_parity
./scripts/build.sh --dll ntdll --test test_arm64x_loader_parity
./scripts/test.sh --dll ntdll --test test_arm64x_loader_parity
./scripts/test.sh --winetest --dll ntdll
./scripts/build.sh --dll kernelbase --test test_wait_on_address
./scripts/test.sh --dll kernelbase --test test_wait_on_address
./scripts/build.sh --dll kernelbase --test test_win32_utility_apis
./scripts/test.sh --dll kernelbase --test test_win32_utility_apis
./scripts/test.sh --winetest --dll kernelbase

# 5. Build & verify macOS windowing, multi-monitor ChangeDisplaySettingsEx & IOSurface Metal swapchains in winemac.drv
./scripts/build.sh --dll winemac.drv --test test_winemac_parity
./scripts/test.sh --dll winemac.drv --test test_winemac_parity
./scripts/test.sh --winetest --dll winemac.drv

# 6. Build & verify Media Foundation core APIs & macOS VideoToolbox H.264/HEVC hardware decode in mfplat
./scripts/build.sh --dll mfplat --test test_mfplat_parity
./scripts/test.sh --dll mfplat --test test_mfplat_parity
./scripts/test.sh --winetest --dll mfplat

# 7. Build & verify macOS AVFoundation video capture, DirectShow CLSID_AudioRecord & mfreadwrite bridge in qcap
./scripts/build.sh --dll qcap --test test_qcap_parity
./scripts/test.sh --dll qcap --test test_qcap_parity
./scripts/test.sh --winetest --dll qcap

# 8. Build & verify Direct2D primitives (arcs, layers, AA, gradients, cubic Beziers) & vkd3d-shader MSL in d2d1
./scripts/build.sh --dll d2d1 --test test_d2d1_msl_parity
./scripts/test.sh --dll d2d1 --test test_d2d1_msl_parity
./scripts/test.sh --winetest --dll d2d1

# 9. Build & verify macOS IOKit USB enumeration, libusb-1.0 & SetupDi service install in wineusb.sys
./scripts/build.sh --dll wineusb.sys --test test_wineusb_parity
./scripts/test.sh --dll wineusb.sys --test test_wineusb_parity
./scripts/test.sh --winetest --dll wineusb.sys

# 10. Build & verify macOS IOBluetooth HCI enumeration & SetupDi service install in winebth.sys
./scripts/build.sh --dll winebth.sys --test test_winebth_parity
./scripts/test.sh --dll winebth.sys --test test_winebth_parity
./scripts/test.sh --winetest --dll winebth.sys

# 5. Discover & rank unimplemented stubs across Wine DLLs
./scripts/test.sh --discover-stubs --dll bcrypt

# 5. Generate an auditable Clean-Room Spec Packet in specs/<dll>/<api>.json
./scripts/test.sh --fetch-spec bcrypt:BCryptGenerateSymmetricKey

# 6. Launch/verify or stop the headless QEMU Windows VM & Guest Agent
./scripts/launch.sh --arch arm64 --dry-run
./scripts/stop.sh --all

# 7. Run WineHQ C99/C11, Markdown & KaTeX badge, and script boundary linter
./scripts/test.sh --lint

# 8. Complete Turnkey Build (All wine-parity DLLs/tests + Upstream Wine Installation)
#    --full-x86 / --x86      : Builds --all (x86_64) + upstream Wine (--enable-archs=i386,x86_64)
#                              to run 32-bit & 64-bit x86 Windows binaries on macOS (Rosetta 2 / Intel)
#    --full-arm64 / --arm64  : Builds --all (arm64) + upstream Wine (--enable-archs=arm64ec,i386)
#                              for native Apple Silicon ARM64 & ARM64EC execution
./scripts/build.sh --full-x86
./scripts/build.sh --full-arm64

# 9. Build & Register macOS .exe/.msi Automatic LaunchServices Handler (WineParity.app + POSIX interposer)
#    Enforces mandatory +x executable permission (chmod +x) before launching any .exe or .msi
./scripts/build.sh --exe-handler
./scripts/launch.sh --register-exe-handler
./scripts/launch.sh --exe-handler-status
./scripts/stop.sh --unregister-exe-handler

# 10. Build Turnkey macOS DMG Installer Package (WineParity-Installer.dmg + standalone WineParity Config.app)
#     Pass -nz (or --notarize) to code-sign with Hardened Runtime, submit to Apple Notary Service (notarytool), and staple the ticket
./scripts/build.sh --dmg
./scripts/build.sh --dmg -nz
```

---

## 4. Seamless macOS `.exe` & `.msi` Execution (`WineParity.app` & POSIX Interposer)

On macOS, `wine-parity` provides a zero-dependency native LaunchServices bundle (`builds/WineParity.app`), a Mach-O `execve`/`posix_spawn` interposer (`builds/libwine_parity_exec.dylib`), and a shell integration script (`builds/wine-parity-shell.sh`) so users can launch Windows `.exe` and `.msi` files directly by **double-clicking in Finder**, running **`open app.exe`**, or executing **`./app.exe`** in Terminal.

### Mandatory `+x` Executable Permission & 3-Gate Pre-Execution Security Guard

All launch entrypoints (`bin/wine-parity`, `WineParity.app`, `libwine_parity_exec.dylib`, and `wine-parity-shell.sh`) enforce both the POSIX executable bit (`chmod +x`) and a **3-Gate Pre-Execution PE Security Scanner** (`./scripts/test.sh --scan-exe <file.exe>`) before allowing any `.exe`, `.msi`, `.com`, or `.scr` binary to run:
1. **`+x` Permission Check**: Refuses to launch any file lacking the `+x` executable bit with POSIX exit code **`126`** (`EACCES`).
2. **Gate 1 — macOS Quarantine & Provenance (`com.apple.quarantine`)**: Checks `com.apple.quarantine`, `com.apple.metadata:kMDItemWhereFroms`, and `Zone.Identifier` (`ZoneId=3`). Closes the macOS Gatekeeper Wine blind spot by refusing to launch quarantined `.exe`/`.msi` downloads until inspected and unquarantined.
3. **Gate 2 — PE Authenticode Signature & Digest Verification**: Computes the true PE Authenticode SHA-256 digest (excluding `OptionalHeader.CheckSum` and `DataDirectory[4]`) and verifies embedded `WIN_CERTIFICATE` PKCS#7 `SignedData` signatures, immediately blocking tampered binaries (and unsigned binaries when `--require-signature` or `WINE_PARITY_REQUIRE_AUTHENTICODE=1` is set).
4. **Gate 3 — Static PE Hardening, Entropy, IAT Threat Scan & VirusTotal v3 API**:
   - Blocks PE sections violating W^X (`IMAGE_SCN_MEM_WRITE | IMAGE_SCN_MEM_EXECUTE`).
   - Computes per-section Shannon byte entropy ($[0.0, 8.0]\text{ bits/byte}$) to identify packed/encrypted payloads and audits `DllCharacteristics` (`DYNAMIC_BASE` ASLR, `HIGH_ENTROPY_VA`, `NX_COMPAT` DEP, `GUARD_CF`).
   - Blocks binaries importing high-risk process-injection, hollowing, or credential-dumping API combinations (`VirtualAllocEx` + `WriteProcessMemory` + `CreateRemoteThread`, `NtUnmapViewOfSection`, `MiniDumpWriteDump`).
   - **VirusTotal v3 Lookup**: When `VT_API_KEY`, `VIRUSTOTAL_API_KEY`, or `--vt-api-key <key>` is provided, queries VirusTotal v3 (`/api/v3/files/<sha256>`) and blocks execution if any engine flags the binary as malicious.

### Architecture & Dispatch Behavior

1. **Finder Double-Click & `open app.exe` (`builds/WineParity.app`)**:
   - Registers Uniform Type Identifiers `com.microsoft.windows-executable` (`.exe`, `.com`, `.scr`) and `com.microsoft.msi-installer` (`.msi`) under bundle identifier `org.wine-parity.launcher`.
   - Receives `kAEOpenDocuments` AppleEvents via AppKit's `application:openURLs:` / `application:openFile:` as well as direct CLI arguments.
   - **PE Header (`MZ`/`COFF`) Auto-Dispatcher**: Reads `e_lfanew` → `PE\0\0` → `COFF Machine` from the binary header and automatically selects:
     - `builds/wine-install-arm64/bin/wine-parity` for `ARM64` (`0xAA64`) and `ARM64EC` (`0xA641`) binaries.
     - `builds/wine-install-x86/bin/wine-parity` for `AMD64` (`0x8664`) and `I386` (`0x014C`) binaries.
     - `msiexec /i <package.msi>` for `.msi` installer packages (detected by extension or OLE Compound Document magic `D0 CF 11 E0`).
   - **Working Directory Preservation**: Automatically switches the working directory (`chdir`) to the parent directory of the target `.exe`/`.msi` (`dirname(resolved_path)`) so relative DLL and asset loads work identically to Windows Explorer double-clicks (set `WINE_PARITY_PRESERVE_CWD=1` to keep the caller's working directory).
2. **Direct Terminal Execution (`./app.exe`)**:
   - **Interactive `zsh` Shell (`builds/wine-parity-shell.sh`)**: Registers `+x`-checked suffix aliases (`alias -s exe=...`, `alias -s msi=...`) via `_wine_parity_run_checked`.
   - **Child Processes & Build Scripts (`builds/libwine_parity_exec.dylib`)**: Uses Mach-O `__DATA,__interpose` to intercept `posix_spawn`, `execve`, and `execv`. Verifies `+x` permission (`EACCES` if missing) and transparently routes `+x` PE binaries through `WineParity`.

### Step-by-Step Usage Instructions

```sh
# 1. Build WineParity.app, libwine_parity_exec.dylib, wine-parity-shell.sh, and x86/arm64 launchers
#    (Also built automatically during ./scripts/build.sh --full-x86 or ./scripts/build.sh --full-arm64)
./scripts/build.sh --exe-handler

# 2. Register WineParity.app as the default macOS LaunchServices handler for .exe and .msi
./scripts/launch.sh --register-exe-handler

# 3. Verify current LaunchServices registration and +x policy status
./scripts/launch.sh --exe-handler-status

# 4. Run the 3-Gate Pre-Execution PE Security Scanner on an untrusted .exe (with optional VirusTotal API key)
./scripts/test.sh --scan-exe /path/to/app.exe
VT_API_KEY="your_vt_api_key" ./scripts/test.sh --scan-exe /path/to/app.exe --require-signature

# 5. Grant executable permission (+x) to a Windows binary or installer (required)
chmod +x /path/to/app.exe
chmod +x /path/to/installer.msi

# 6A. Launch via macOS `open` (or double-click in Finder)
open /path/to/app.exe
open /path/to/app.exe --args --windowed
open /path/to/installer.msi

# 6B. Launch directly in zsh (`./app.exe`) by sourcing the shell helper
source builds/wine-parity-shell.sh
/path/to/app.exe --windowed

# 6C. Launch from child processes / scripts calling execve() or posix_spawn() directly
DYLD_INSERT_LIBRARIES="$PWD/builds/libwine_parity_exec.dylib" /path/to/spawner_tool /path/to/app.exe

# 7. Unregister the macOS LaunchServices .exe/.msi handler when no longer needed
./scripts/stop.sh --unregister-exe-handler
```

---

## 5. User-Level Configuration, Native macOS Settings UI, Registry Cascade & Zero-Trust Containment

`wine-parity` separates **host-side security, sandboxing, and launcher policies** from **guest Windows compatibility settings**, storing user preferences in `~/.config/wine-parity/config.json` (created automatically with `0600` permissions, or overridden via `WINE_PARITY_CONFIG=/path/to/config.json`) and non-destructively cascading Windows environment keys into Wine's native registry hives (`$WINEPREFIX/user.reg` and `$WINEPREFIX/dosdevices/`).

### Precedence Chain

Every launch and security scan resolves settings in strict priority order:
1. **Explicit CLI Flags** (e.g., `--require-signature`, `--vt-api-key <key>`, `--set-config <dot.key=value>`)
2. **Environment Variables** (`WINE_PARITY_CONFIG`, `VT_API_KEY` / `VIRUSTOTAL_API_KEY`, `WINE_PARITY_REQUIRE_AUTHENTICODE`, `WINE_PARITY_NO_SANDBOX`, `WINEPREFIX`)
3. **Per-Executable Profile Overrides** (`app_profiles["<exe_basename_lower>"]` in `config.json`)
4. **Global User Configuration** (`~/.config/wine-parity/config.json`)

### Turnkey macOS `.dmg` Installer & Standalone Configuration Program (`WineParity Config.app`)

To make installation and configuration effortless on macOS, `./scripts/build.sh --dmg` builds a turnkey compressed disk image (`builds/WineParity-Installer.dmg`, staged from `builds/dmg_staging/` via `scripts/lib/dmg_packager.py`) containing:
- **`WineParity.app`** (`org.wine-parity.launcher`): The automatic `.exe` / `.msi` LaunchServices dispatcher and 3-gate PE security scanner. Opening `WineParity.app` directly from Finder without a target `.exe`/`.msi` file also launches the native **Wine Parity Settings** window.
- **`WineParity Config.app`** (`builds/WineParityConfig.app`, `org.wine-parity.config`): A dedicated, standalone native macOS AppKit Settings application that users can run anytime from `/Applications`, Dock, or Spotlight to configure `wine-parity`.
- **`Install WineParity.command`**: A double-clickable macOS installer script (also supporting CLI flags `--target-dir <dir>`, `--bin-dir <dir>`, `--config-file <path>`, `--wineprefix <dir>`, `--user`, `--no-open-config`, `--dry-run`) that:
  1. Installs `WineParity.app` and `WineParity Config.app` into `/Applications` (or `~/Applications`).
  2. Installs the `wine-parity` and `wine-parity-config` CLI commands into `/usr/local/bin` (or `~/.local/bin`).
  3. Registers `.exe` and `.msi` LaunchServices associations.
  4. Initializes `~/.config/wine-parity/config.json` (`0600`) and cascades settings to `~/.wine/user.reg`.
  5. Automatically opens **`WineParity Config.app`** so the user can immediately review or customize their setup.
- **`Applications` Symlink & `README.txt`**: Enables standard macOS drag-and-drop installation alongside the one-click installer script.
- **Apple Notarization & Hardened Runtime Signing (`-nz` / `--notarize`)**: Passing `-nz` (e.g. `./scripts/build.sh --dmg -nz` or `./scripts/build.sh -nz`) signs `WineParity.app`, `WineParity Config.app`, `Install WineParity.command`, and `builds/WineParity-Installer.dmg` with Hardened Runtime (`codesign --force --options runtime --timestamp`), submits the disk image to Apple Notary Service via `xcrun notarytool submit ... --wait` (using `--notary-profile <profile>` / `WINE_PARITY_NOTARY_PROFILE` or `WINE_PARITY_APPLE_ID` + `WINE_PARITY_TEAM_ID` + `WINE_PARITY_APP_PASSWORD`), and staples the offline Gatekeeper ticket via `xcrun stapler staple`.

### Native macOS AppKit Configuration Dialog (`WineParity Config.app` & `--config-ui`)

Instead of requiring users to navigate the legacy Windows-style `winecfg` tool for common workflows, `WineParity Config.app` (`builds/WineParityConfig.app`), `wine-parity-config`, and `./scripts/launch.sh --config-ui` open the native macOS AppKit Settings window (`WineParitySettingsController`) organized into four streamlined tabs:

1. **General**:
   - **Windows Version**: Selects reported Windows version (`win11`, `win10`, `win81`, `win7`) and cascades `Version` to `[Software\\Wine]` in `user.reg`.
   - **Architecture Mode**: Selects `auto` (PE `MZ`/`COFF` inspection), `arm64`, or `x86_64`.
   - **Working Directory Policy**: Toggles switching `cwd` to the `.exe`'s parent directory (`preserve_exe_cwd`).
   - **Open Classic `winecfg`…**: Launches upstream `winecfg` directly when advanced per-DLL or audio tab adjustments are desired.
2. **Security (3-Gate Scanner & VirusTotal v3 Configuration)**:
   - Toggles Gate 1 quarantine enforcement (`block_quarantined`), Gate 2 Authenticode signature enforcement (`require_authenticode`), and Gate 3 W^X / IAT process-injection blocking (`block_wx_sections` & `block_suspicious_iat`).
   - **VirusTotal v3 Cloud Hash Reputation**: Provides an enable/disable checkbox (`security.virustotal_enabled`), a masked API key input field (`NSSecureTextField` for `security.virustotal_api_key`), and a **"Show Key"** visibility toggle so users can enter, inspect, or update their VirusTotal v3 API key directly in the GUI (saved locally with `0600` permissions in `~/.config/wine-parity/config.json`).
3. **Sandbox & Network (Zero-Trust Containment)**:
   - **macOS `sandbox-exec` Containment**: Enabled by default (`sandbox.enabled = true`). Blocks access to sensitive host credential stores (`~/.ssh`, `~/.aws`, `~/.gnupg`, `~/Library/Keychains`), blocks host file writes outside `$WINEPREFIX` and the target `.exe` directory (`sandbox.allow_exe_dir_rw = true`), and removes the `$WINEPREFIX/dosdevices/z:` root `/` symlink (`sandbox.strip_z_drive = true`).
   - **Incoming Network Firewall (`network.incoming`)**: Defaults to **`deny`** (all inbound TCP/UDP ` network-inbound ` sockets blocked at the Darwin kernel layer via Seatbelt; configurable to `allow` or `custom_ports`).
   - **Outgoing Network Firewall (`network.outgoing`)**: Defaults to **`http_https_only`** (restricts remote TCP connections exclusively to ports **`80`** and **`443`** while keeping local Unix domain sockets and loopback IPC open for `wineserver`; configurable to `deny_all`, `allow_all`, or `custom_ports` via `network.allowed_outgoing_ports`).
4. **Graphics & Acceleration**:
   - Configures Retina High-DPI mode (`RetinaMode`), window decorations (`Decorated`), screen DPI (`LogPixels`), Direct3D renderer backend (` metal ` / `gl` / `vulkan`), and shader backend (`msl` / `glsl`), cascading directly into `[Software\\Wine\\Mac Driver]` and `[Software\\Wine\\Direct3D]`.

### Non-Destructive Wine Registry Cascade & Coexistence with `winecfg`

Whenever settings are saved in the macOS Configuration UI, updated via `./scripts/launch.sh --set-config`, or applied prior to `.exe` launch, `scripts/lib/config_manager.py` performs a surgical merge into `$WINEPREFIX/user.reg`:
- Updates **only** the managed keys inside `[Software\\Wine]`, `[Software\\Wine\\Mac Driver]`, `[Software\\Wine\\Direct3D]`, `[Software\\Wine\\DllOverrides]`, and `[Software\\Wine\\AppDefaults\\<app.exe>]`.
- Preserves all unmanaged keys, custom registry entries, and manual edits made through upstream `winecfg` or `regedit`.
- Automatically manages `$WINEPREFIX/dosdevices/z:` based on `sandbox.strip_z_drive`.

### Step-by-Step Configuration & Containment Instructions

```sh
# 1. Inspect current user configuration, active security gates, network firewall, and Seatbelt status
./scripts/launch.sh --config-status

# 2. Launch the native macOS AppKit Settings window (General, Security, Sandbox & Network, Graphics)
./scripts/launch.sh --config-ui

# 3. Configure Windows version and graphics settings from the CLI (automatically cascades to $WINEPREFIX/user.reg)
./scripts/launch.sh --set-config wine_environment.windows_version=win11 \
                    --set-config wine_environment.retina_mode=true \
                    --set-config wine_environment.dpi=144 \
                    --set-config wine_environment.d3d_renderer=metal

# 4. Configure Network Firewall policies (default: incoming=deny, outgoing=http_https_only on ports 80/443)
# 4A. Restore strict default firewall (no incoming connections; outgoing restricted to HTTP/80 & HTTPS/443)
./scripts/launch.sh --set-config network.incoming=deny \
                    --set-config network.outgoing=http_https_only

# 4B. Completely air-gap Windows applications (block all incoming and outgoing network traffic)
./scripts/launch.sh --set-config network.incoming=deny \
                    --set-config network.outgoing=deny_all

# 4C. Allow custom outgoing TCP ports (e.g., HTTPS 443 + custom game/service port 8443)
./scripts/launch.sh --set-config network.outgoing=custom_ports \
                    --set-config 'network.allowed_outgoing_ports=[443, 8443]'

# 5. Configure Zero-Trust Seatbelt Sandboxing & Z: drive root mapping
./scripts/launch.sh --set-config sandbox.enabled=true \
                    --set-config sandbox.strip_z_drive=true \
                    --set-config sandbox.allow_exe_dir_rw=true

# 6. Configure 3-Gate Pre-Execution Security Policy & VirusTotal API key in config.json (0600 permissions)
#    (Can also be configured and masked/unmasked interactively in the Security tab of `./scripts/launch.sh --config-ui`)
./scripts/launch.sh --set-config security.require_authenticode=false \
                    --set-config security.virustotal_enabled=true \
                    --set-config security.virustotal_api_key="YOUR_VT_API_KEY"

# 7. Use a custom configuration file path for an isolated session or test run
WINE_PARITY_CONFIG=/tmp/custom-wine-parity-config.json ./scripts/launch.sh --config-status
```

See [docs/skills/USER_CONFIG_AND_CONTAINMENT.md](docs/skills/USER_CONFIG_AND_CONTAINMENT.md) for the complete `config.json` schema, registry mapping table, and Seatbelt profile specification.

---

## 6. Key Engineering Boundaries

* **Clean-Room Reverse Engineering:** AI prompts and implementations strictly use black-box inputs (Microsoft Learn documentation, public Wine headers, and differential test failure logs) with zero exposure to leaked Microsoft source code or proprietary disassemblies.
* **macOS-First Hardware & Framework Exploitation:** Prioritizes Apple Silicon (`hvf` virtualization, `CommonCrypto` / `Security.framework`, `CoreFoundation`, `VideoToolbox` / `CoreMedia`, Darwin/Mach APIs) before Linux (`kvm`, POSIX/GnuTLS).
* **Wineserver Statefulness:** Stateless utility APIs live in client DLLs (`dlls/*`); APIs managing cross-process handles, security tokens, or synchronization primitives require `wineserver` protocol IPC.

See [PROJECT_BLUEPRINT.md](PROJECT_BLUEPRINT.md) and [docs/skills/ARCHITECTURE.md](docs/skills/ARCHITECTURE.md) for detailed technical specifications.

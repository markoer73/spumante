<p align="center">
  <img src="docs/assets/spumante-logo.png" alt="Spumante Logo" width="180" />
</p>

# Spumante for macOS

Run Windows `.exe` and `.msi` applications directly on macOS (Apple Silicon & Intel) with native Metal graphics, Retina High-DPI, a native macOS Settings app, and built-in zero-trust security & sandboxing.

---

## 1. Install Spumante

> **Warning — Rosetta 2 Is Required on Apple Silicon Macs:**
> Running 32-bit and 64-bit Intel (`x86` / `x86_64`) Windows applications on Apple Silicon (`M1` / `M2` / `M3` / `M4`) requires **Apple Rosetta 2**.
> - If Rosetta 2 is not yet installed on your Mac, **`Install Spumante.command`** and **`Spumante.app`** will prompt for your consent to install it automatically (`softwareupdate --install-rosetta`), and will abort installation if declined.
> - You can also install Rosetta 2 manually at any time in Terminal:
>   ```sh
>   softwareupdate --install-rosetta --agree-to-license
>   ```

### Option A: Install from the `.dmg` Package (Fastest)

1. Open **`Spumante-Installer.dmg`** (located in `builds/Spumante-Installer.dmg` if built locally).
2. Double-click **`Install Spumante.command`**.
   - Installs **`Spumante.app`** and **`Spumante Config.app`** into `/Applications`.
   - Installs the `spumante` and `spumante-config` terminal commands.
   - Registers `.exe` and `.msi` files so you can double-click them in Finder.
   - Opens **`Spumante Config.app`** so you are ready to go.

*(Alternatively, drag `Spumante.app` and `Spumante Config.app` into the `Applications` folder inside the DMG.)*

### Option B: Build from Scratch (2 Commands)

If you cloned this repository and want to build the entire runtime and `.dmg` installer from source:

```sh
# 1. Build Spumante for 32-bit & 64-bit Windows apps (Intel & Apple Silicon via Rosetta 2)
./scripts/build.sh --full-x86

# 2. Package the macOS DMG installer (add -nz to code-sign & notarize with Apple)
./scripts/build.sh --dmg
```

*(For native ARM64 / ARM64EC Windows apps on Apple Silicon, run `./scripts/build.sh --full-arm64` instead of `--full-x86`.)*

---

## 2. Configure Spumante (`Spumante Config.app`)

Open **`Spumante Config.app`** from `/Applications` (or run `spumante-config` in Terminal):

- **General**: Choose the reported Windows version (`Windows 11`, `Windows 10`, etc.), CPU architecture (`Auto-Detect`, `ARM64`, `x86_64`), or open classic `winecfg`.
- **Security**: Toggle macOS Quarantine checks, Authenticode signature verification, and optional **VirusTotal v3** cloud hash scanning.
- **Sandbox & Network**: Keep Windows apps isolated in a macOS Seatbelt sandbox (`Z:` drive hidden by default) and control incoming/outgoing network access.
- **Graphics & Acceleration**: Toggle Retina High-DPI mode, DPI scaling, and the Apple **Metal** Direct3D backend.
- **Built-in Apps**: Launch bundled Windows GUI and CLI apps with one click.

---

## 3. Run Any Windows `.exe` or `.msi` Program

> **Security Notice — Why `chmod +x` Is Required for Downloaded `.exe` and `.msi` Files:**
> By default, macOS does not consider Windows `.exe` and `.msi` files to be native executable applications and saves downloaded files without the POSIX executable flag (`+x`). As a deliberate security measure—preventing untrusted or accidentally clicked Windows binaries from running without your explicit authorization—Spumante requires the executable bit (`chmod +x`) and no macOS download quarantine attribute before launching external `.exe` or `.msi` files.
>
> **Automatic Post-Installer Sync (`drive_c`):** Whenever a Windows installer (such as the 7-Zip installer) finishes running under Spumante, Spumante **automatically sets `chmod +x`** on all newly installed `.exe` and `.msi` programs inside `$WINEPREFIX/drive_c/` (`~/.wine/drive_c/`). You can also trigger this at any time by clicking **"Sync Installed drive_c Apps (+x)"** in the **General** tab of **`Spumante Config.app`** or running `spumante --sync-drive-c` (`./scripts/launch.sh --sync-drive-c`).

```sh
# 1. Authorize a downloaded .exe or .msi file to run
xattr -d com.apple.quarantine /path/to/program.exe 2>/dev/null || true
chmod +x /path/to/program.exe

# 2. Launch it (or simply double-click it in Finder!)
open /path/to/program.exe
```

You can also run programs directly from Terminal:

```sh
spumante /path/to/program.exe
```

---

## 4. Quick-Start: Test Windows Programs Right Now

### A. Built-in Windows Apps (No Download Needed)

Spumante bundles 14 ready-to-run Windows GUI and CLI apps in the DMG (`Windows Apps/` folder) and inside **`Spumante Config.app`**:

1. Open **`Spumante Config.app`** and select the **`Built-in Apps`** tab.
2. Click **Launch** next to any GUI app:
   - **Notepad** (`notepad.exe`)
   - **Minesweeper** (`winmine.exe`)
   - **WordPad** (`wordpad.exe`)
   - **Task Manager** (`taskmgr.exe`)
   - **Clock** (`clock.exe`), **Registry Editor** (`regedit.exe`), **Program Manager** (`progman.exe`), **Wine Explorer** (`explorer.exe`)
3. Or click **Run** next to any CLI tool (`hostname.exe`, `systeminfo.exe`, `ipconfig.exe`, `tasklist.exe`) to view its live output.

### B. Try Popular Standalone Windows Apps (7-Zip & PuTTY)

Copy and paste these commands in Terminal to download and run real Windows programs in seconds:

#### Example 1: 7-Zip File Manager (`x64` Installer & GUI)

```sh
curl -L -o /tmp/7z-x64.exe https://www.7-zip.org/a/7z2408-x64.exe
chmod +x /tmp/7z-x64.exe
open /tmp/7z-x64.exe
```

Once the installer finishes, Spumante automatically marks `7zFM.exe` in `drive_c` as executable—launch the 7-Zip GUI directly:

```sh
open "$HOME/.wine/drive_c/Program Files/7-Zip/7zFM.exe"
```

#### Example 2: PuTTY (Standalone Portable `.exe` — Zero Install)

```sh
curl -L -o /tmp/putty.exe https://the.earth.li/~sgtatham/putty/latest/w64/putty.exe
chmod +x /tmp/putty.exe
open /tmp/putty.exe
```

---

## 5. Developer & Architecture Documentation

For contributor workflows, the closed-loop Windows VM differential testing harness, surgical single-DLL builds (`./scripts/build.sh` / `./scripts/test.sh`), Antigravity IDE / TestMate integration (`--ide-tests`), and subsystem internals:

- **[Developer & Architecture Guide (`DEVELOPER_GUIDE.md`)](DEVELOPER_GUIDE.md)**
- **[System Architecture (`docs/skills/ARCHITECTURE.md`)](docs/skills/ARCHITECTURE.md)**
- **[User Config & Containment Spec (`docs/skills/USER_CONFIG_AND_CONTAINMENT.md`)](docs/skills/USER_CONFIG_AND_CONTAINMENT.md)**
- **[Project Blueprint (`PROJECT_BLUEPRINT.md`)](PROJECT_BLUEPRINT.md)**

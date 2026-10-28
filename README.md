<p align="center">
  <img src="docs/assets/spumante-logo.png" alt="Spumante Logo" width="180" />
</p>

# Spumante for macOS

Run Windows `.exe` and `.msi` applications directly on macOS (Apple Silicon & Intel) with native Metal graphics, Retina High-DPI, a native macOS Settings app, and built-in zero-trust security & sandboxing.

> **Built on Top of [Wine](https://www.winehq.org/) — Licensing & Why Spumante Does Not Contribute Upstream:**
> Spumante is built on top of the [Wine](https://www.winehq.org/) project ([source repository](https://gitlab.winehq.org/wine/wine)). All original Spumante components (`src/launcher/`, `Spumante.app`, `Spumante Config.app`, `scripts/`, `specs/`, `docs/`) are licensed under the **Apache License 2.0** ([`LICENSE`](LICENSE), [`NOTICE`](NOTICE)), while the bundled Wine runtime and modified Wine DLL overlays (`dlls/`, `tests/`) remain under the **GNU Lesser General Public License v2.1 or later (`LGPL-2.1-or-later`)** ([`LICENSE-LGPL-2.1`](LICENSE-LGPL-2.1)). We use and import upstream Wine code and fully respect its LGPL-2.1-or-later license, but we do **not** directly contribute back to upstream Wine because Wine maintains an antiquated policy towards LLM-generated code. Instead, we maintain our enhancements and patches cleanly on top of upstream Wine to keep them clearly distinguished.
>
> Because we do not restrict our vision and actively leverage LLMs, we can provide much better, quicker, and complete support for features lacking in upstream Wine on macOS.

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

No Python, Xcode, or Homebrew needed: the disk image is self-contained (native apps plus a bundled Wine runtime) and works from any location.

1. Open **`Spumante-Installer.dmg`** (located in `builds/Spumante-Installer.dmg` if built locally).
2. Double-click **`Install Spumante.command`**.
   - Installs **`Spumante.app`** and **`Spumante Config.app`** into `/Applications`.
   - Installs the `spumante` and `spumante-config` terminal commands.
   - Registers `.exe` and `.msi` files so you can double-click them in Finder.
   - Opens **`Spumante Config.app`** so you are ready to go.

*(Alternatively, drag `Spumante.app` and `Spumante Config.app` into the `Applications` folder inside the DMG.)*

### Option B: Install via Homebrew Cask

Once a GitHub Release is published (see [`docs/RELEASE_AND_LAUNCH.md`](docs/RELEASE_AND_LAUNCH.md)), you can install `Spumante.app`, `Spumante Config.app`, and the `spumante` / `spumante-config` CLI tools via Homebrew:

```sh
brew install --cask markoer73/spumante/spumante
```

### Option C: Build from Scratch (2 Commands)

If you cloned this repository and want to build the entire runtime and `.dmg` installer from source (requires Xcode Command Line Tools, `python3`, and Homebrew build dependencies — see [DEVELOPER_GUIDE.md](DEVELOPER_GUIDE.md#2-developer-prerequisites--toolchain-setup)):

```sh
# 0. One-time developer prerequisites (if not already installed)
xcode-select --install 2>/dev/null || true
brew install python mingw-w64 bison flex pkg-config

# 1. Build Spumante for 32-bit & 64-bit Windows apps (Intel & Apple Silicon via Rosetta 2)
./scripts/build.sh --full-x86

# 2. Package the macOS DMG installer (add -nz to code-sign & notarize with Apple)
./scripts/build.sh --dmg
```

*(For native ARM64 / ARM64EC Windows apps on Apple Silicon, run `./scripts/build.sh --full-arm64` instead of `--full-x86`.)*

---

## 2. Configure Spumante (`Spumante Config.app` & macOS Menu Bar Icon)

Open **`Spumante Config.app`** from `/Applications` (or run `spumante-config` in Terminal). It opens the **Spumante Settings** window and stays active in your **macOS Menu Bar** with the Spumante glass icon for 1-click access at any time:

- **macOS Menu Bar Icon (Tray Menu)**: Click the Spumante glass in the menu bar to launch your **Favourite Windows Apps**, open **Windows Utilities** (`Task Manager`, `Command Prompt (wineconsole.exe)`, `Notepad`, `Registry Editor`, `Wine Configuration`, `File Explorer`), run any of the **More Built-in Apps**, open **Spumante Settings**, sync `drive_c` permissions, or submit a bug / AppDB report.
- **General**: Choose the reported Windows version (`Windows 11`, `Windows 10`, etc.), toggle macOS Retina High-DPI mode, or register `.exe`/`.msi` file associations.
- **Security**: Toggle macOS Quarantine checks, Authenticode signature verification, and optional **VirusTotal v3** cloud hash scanning.
- **Sandbox & Network**: Keep Windows apps isolated in a macOS Seatbelt sandbox (`Z:` drive hidden by default) and control incoming/outgoing network access.
- **Graphics & Acceleration**: Toggle Retina High-DPI mode, DPI scaling, and the Apple **Metal** Direct3D backend.
- **Built-in Apps & Favourite Custom Apps**: Launch bundled Windows GUI apps (`notepad.exe`, `taskmgr.exe`, `wineconsole.exe`, etc.), run built-in CLI tools either in a native **Windows Console window (`wineconsole.exe`)** or inline, and configure your own **Favourite Custom Windows Applications** with custom display labels (e.g. `taskman.exe` labelled `"Task Manager"`).

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

1. Open **`Spumante Config.app`** and select the **`Built-in Apps`** tab (or click the Spumante glass icon in the macOS menu bar).
2. Click **Launch GUI App** next to any GUI app:
   - **Notepad** (`notepad.exe`)
   - **Minesweeper** (`winmine.exe`)
   - **WordPad** (`wordpad.exe`)
   - **Task Manager** (`taskmgr.exe`)
   - **Windows Console Terminal** (`wineconsole.exe`)
   - **Clock** (`clock.exe`), **Registry Editor** (`regedit.exe`), **Program Manager** (`progman.exe`), **Wine Explorer** (`explorer.exe`)
3. Or select any CLI tool (`cmd.exe`, `hostname.exe`, `systeminfo.exe`, `ipconfig.exe`, `tasklist.exe`) and click **Open Console** (or launch it from the menu bar icon) to open it inside a native **Windows Console (`wineconsole.exe`)** window, or click **Inline** to capture its output inside the Settings window.

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

## 5. Tested Windows Applications (Spumante AppDB) & Bug Reporting

Like the WineHQ Application Database, Spumante tracks tested Windows applications using five compatibility tiers (`Platinum`, `Gold`, `Silver`, `Bronze`, `Garbage`):

- **[Spumante Application Compatibility Database (`docs/APPDB.md`)](docs/APPDB.md)** — View all verified standalone Windows applications (such as **7-Zip** and **PuTTY**) and bundled Windows GUI/CLI utilities.

### Submit a Bug Report or Tested Application

You can submit a bug report or share a newly tested Windows application in seconds—with your macOS version, CPU architecture, and active Spumante settings automatically pre-filled:

1. **From `Spumante Config.app` (One Click):**
   - Open **`Spumante Config.app`** and click **"Report a Bug…"** or **"Submit Tested App…"** at the bottom of the window.
2. **From Terminal:**
   ```sh
   # Open a pre-filled GitHub Bug Report in your browser
   ./scripts/launch.sh --report-bug --app "MyApp 1.0"

   # Open a pre-filled GitHub AppDB Compatibility Report in your browser
   ./scripts/launch.sh --report-app --app "MyApp 1.0" --rating Platinum
   ```
3. **Directly on GitHub:**
   - **[Report a Spumante Bug](https://github.com/markoer73/Spumante/issues/new?template=bug_report.yml)**
   - **[Submit a Tested Windows Application (AppDB)](https://github.com/markoer73/Spumante/issues/new?template=app_compatibility.yml)**

---

## 6. Developer & Architecture Documentation

For contributor workflows, the closed-loop Windows VM differential testing harness, surgical single-DLL builds (`./scripts/build.sh` / `./scripts/test.sh`), Antigravity IDE / TestMate integration (`--ide-tests`), and subsystem internals:

- **[Developer & Architecture Guide (`DEVELOPER_GUIDE.md`)](DEVELOPER_GUIDE.md)**
- **[Application Compatibility Database (`docs/APPDB.md`)](docs/APPDB.md)**
- **[System Architecture (`docs/skills/ARCHITECTURE.md`)](docs/skills/ARCHITECTURE.md)**
- **[User Config & Containment Spec (`docs/skills/USER_CONFIG_AND_CONTAINMENT.md`)](docs/skills/USER_CONFIG_AND_CONTAINMENT.md)**
- **[Release, Homebrew Cask & Launch Kit (`docs/RELEASE_AND_LAUNCH.md`)](docs/RELEASE_AND_LAUNCH.md)**
- **[Project Blueprint (`PROJECT_BLUEPRINT.md`)](PROJECT_BLUEPRINT.md)**

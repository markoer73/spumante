# Spumante Application Compatibility Database (AppDB)

Inspired by the [WineHQ Application Database (AppDB)](https://appdb.winehq.org/), this registry tracks Windows `.exe` and `.msi` applications tested with **Spumante** on macOS (Apple Silicon & Intel).

---

## 1. Compatibility Rating Scale

| Rating | Meaning |
| :--- | :--- |
| **Platinum** | Installs and runs flawlessly out-of-the-box under default Spumante settings with zero tweaks required. |
| **Gold** | Works flawlessly after a minor configuration adjustment in **`Spumante Config.app`** (e.g. toggling network rules, Windows version, or DLL overrides). |
| **Silver** | Works well for normal daily use with minor cosmetic or non-blocking limitations. |
| **Bronze** | Starts and runs, but has noticeable issues or requires workarounds. |
| **Garbage** | Fails to install or launch; crashes immediately or is unusable. |

---

## 2. How to Submit a Tested Application or Bug Report

You can submit a tested Windows application or file a bug report in two ways:

1. **One-Click from `Spumante Config.app` (Recommended)**:
   - Open **`Spumante Config.app`** and click **"Submit Tested App (AppDB)…"** or **"Report a Bug…"** in the bottom bar.
   - Spumante automatically pre-fills the GitHub form with your macOS version, CPU architecture, and active `config.json` settings.
2. **From Terminal or GitHub**:
   - Run `./scripts/launch.sh --report-app --app "App Name"` or `./scripts/launch.sh --report-bug --app "App Name"`.
   - Or open the [**Tested Windows Application Report Form**](https://github.com/markoer73/Spumante/issues/new?template=app_compatibility.yml) / [**Bug Report Form**](https://github.com/markoer73/Spumante/issues/new?template=bug_report.yml) directly on GitHub.

---

## 3. Standalone Third-Party Windows Applications

| Application | Version | Binary Arch | Rating | Tested Host | Notes |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **[7-Zip](https://www.7-zip.org/)** (`7z2408-x64.exe` & `7zFM.exe`) | `24.08` | `x86_64` | **Platinum** | macOS (Apple Silicon + Rosetta 2 & Intel) | Installer and File Manager GUI (`7zFM.exe`) work out-of-the-box; Spumante automatically syncs `chmod +x` on installed `drive_c` binaries. |
| **[PuTTY](https://www.chiark.greenend.org.uk/~sgtatham/putty/)** (`putty.exe`) | `0.81+` | `x86_64` | **Platinum** | macOS (Apple Silicon + Rosetta 2 & Intel) | Standalone portable `.exe` launches directly with Retina HiDPI rendering; enable port `22` or `Allow All Outgoing Connections` in `Spumante Config.app` for SSH sessions. |

---

## 4. Bundled Windows GUI & CLI Applications

All 14 built-in Windows applications shipped inside `Spumante-Installer.dmg` (`Windows Apps/`) and launchable from the **`Built-in Apps`** tab of **`Spumante Config.app`**:

| Application | Binary | Category | Rating | Notes |
| :--- | :--- | :--- | :--- | :--- |
| **Notepad** | `notepad.exe` | GUI | **Platinum** | Classic Windows text editor with native macOS font rendering (`tahoma.ttf` & CoreText/FreeType). |
| **Minesweeper** | `winmine.exe` | GUI | **Platinum** | Bundled Windows Minesweeper game (`winemine.exe` alias). |
| **WordPad** | `wordpad.exe` | GUI | **Platinum** | Rich text editor (`riched20` / `comctl32` v6 WinSxS manifest redirection). |
| **Task Manager** | `taskmgr.exe` | GUI | **Platinum** | Live Windows process and performance monitor. |
| **Clock** | `clock.exe` | GUI | **Platinum** | Analog and digital desktop clock. |
| **Registry Editor** | `regedit.exe` | GUI | **Platinum** | Inspects and edits `HKCU` / `HKLM` hives cascaded from `~/.config/spumante/config.json`. |
| **Wine Configuration** | `winecfg.exe` | GUI | **Platinum** | Coexists non-destructively with `Spumante Config.app`. |
| **Program Manager** | `progman.exe` | GUI | **Platinum** | Classic Windows Program Manager shell. |
| **Windows Explorer** | `explorer.exe` | GUI | **Platinum** | File browser and desktop shell. |
| **Command Prompt** | `cmd.exe` | CLI | **Platinum** | Interactive and batch Windows command interpreter. |
| **Hostname** | `hostname.exe` | CLI | **Platinum** | Prints the active Windows host name with zero `wineusb`/`winebth` driver errors. |
| **IP Configuration** | `ipconfig.exe` | CLI | **Platinum** | Displays network adapter addresses inside the sandbox. |
| **System Information** | `systeminfo.exe` | CLI | **Platinum** | Reports configured Windows version (`win11` by default) and hardware metrics. |
| **Task List** | `tasklist.exe` | CLI | **Platinum** | Lists active processes inside the Wine prefix. |

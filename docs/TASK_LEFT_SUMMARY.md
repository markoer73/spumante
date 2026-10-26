# Task Left Summary

Executive summary of all remaining open tasks in `docs/TASK_QUEUE.md`.

> **CRITICAL RULE:** Completed tasks MUST be removed from this file. This file must strictly and exclusively list remaining uncompleted work.

---

## Remaining Open Tasks

- [ ] Compile and install macOS `IOKit`-backed `wineusb.so` (`dlls/wineusb.sys/unixlib.c`) into `builds/wine-install-x86/lib/wine/x86_64-unix/wineusb.so` and `builds/wine-install-arm64/lib/wine/aarch64-unix/wineusb.so` (and wire into `scripts/lib/wine_borrow.py` / `scripts/build.sh` live prefix overlay) with upstream-compatible `__wine_unix_call_funcs` ABI so `wineusb.sys` `DriverEntry` (`__wine_init_unix_call`) succeeds when launching Windows executables (e.g. `HOSTNAME.EXE`) without `err:wineusb:DriverEntry Failed to initialize Unix library, status 0xc0000135`, `err:ntoskrnl:ZwLoadDriver ... wineusb: c0000135`, or `fixme:service:scmdatabase_autostart_services Auto-start service L"wineusb" failed to start: 126`
- [ ] Compile and install macOS `IOKit`/`IOBluetooth`-backed `winebth.so` (`dlls/winebth.sys/unixlib.c`) into `builds/wine-install-x86/lib/wine/x86_64-unix/winebth.so` and `builds/wine-install-arm64/lib/wine/aarch64-unix/winebth.so` (replacing the unpatched upstream `winebth.so` whose `bluetooth_init` fails with `STATUS_INTERNAL_ERROR` `0xc00000e5` when `bluez_dbus_init()` returns `NULL` on macOS) so `ntoskrnl.exe` `ZwLoadDriver` and `services.exe` `scmdatabase_autostart_services` start `winebth` cleanly without `err:ntoskrnl:ZwLoadDriver ... winebth: c00000e5` or `fixme:service:scmdatabase_autostart_services Auto-start service L"winebth" failed to start: 1359`
- [ ] Provision Universal (`x86_64 + arm64`) `libfreetype.6.dylib` (`--with-freetype` in `scripts/lib/wine_borrow.py` for `--full-x86` Rosetta 2 builds so `x86_64-unix/win32u.so` links `HAVE_FREETYPE` instead of skipping arm64-only `/opt/homebrew/lib/libfreetype.6.dylib`) and fix `SYSLINK_SetFont` in `dlls/comctl32` (`syslink.c` for both `comctl32.dll` and `comctl32_v6.dll`) to fall back to `GetCurrentObject(hdc, OBJ_FONT)` / `GetStockObject(DEFAULT_GUI_FONT)` / `GetStockObject(SYSTEM_FONT)` so built-in `builds/wine-install-*/share/wine/fonts/tahoma.ttf` and macOS `/System/Library/Fonts` render without `err:syslink:SYSLINK_SetFont Failed to create link font!`

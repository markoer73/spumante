# Task Left Summary

Executive summary of all remaining open tasks in `docs/TASK_QUEUE.md`.

> **CRITICAL RULE:** Completed tasks MUST be removed from this file. This file must strictly and exclusively list remaining uncompleted work.

---

## Remaining Open Tasks

- [ ] Provision Universal (`x86_64 + arm64`) `libfreetype.6.dylib` (`--with-freetype` in `scripts/lib/wine_borrow.py` for `--full-x86` Rosetta 2 builds so `x86_64-unix/win32u.so` links `HAVE_FREETYPE` instead of skipping arm64-only `/opt/homebrew/lib/libfreetype.6.dylib`) and fix `SYSLINK_SetFont` in `dlls/comctl32` (`syslink.c` for both `comctl32.dll` and `comctl32_v6.dll`) to fall back to `GetCurrentObject(hdc, OBJ_FONT)` / `GetStockObject(DEFAULT_GUI_FONT)` / `GetStockObject(SYSTEM_FONT)` so built-in `builds/wine-install-*/share/wine/fonts/tahoma.ttf` and macOS `/System/Library/Fonts` render without `err:syslink:SYSLINK_SetFont Failed to create link font!`

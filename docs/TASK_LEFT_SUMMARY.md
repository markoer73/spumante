# Task Left Summary

Executive summary of all remaining open tasks in `docs/TASK_QUEUE.md`.

> **CRITICAL RULE:** Completed tasks MUST be removed from this file. This file must strictly and exclusively list remaining uncompleted work.

---

## Remaining Open Tasks

### Critical Priority
- [ ] Implement Apple Silicon (`arm64`) execution parity for Intel (`x86_64` & `i386`) Windows apps in `dlls/ntdll` (Rosetta 2 write-fault/LDT/WoW64 parity in `signal_x86_64.c`/`virtual.c` AND native `arm64-apple-darwin` + `ARM64EC` `fexcore.dll`/`box64cpu.dll` support: Darwin `x18` platform-register save/restore in `signal_arm64.c`, 16 KB host page vs. 4 KB Windows PE page guard/write-watch emulation in `virtual.c`, and `MAP_JIT`/`pthread_jit_write_protect_np` W^X toggling)

### High Priority
- [ ] Implement macOS fast NT synchronization (`NTSync` parity) in `dlls/ntdll` and `dlls/kernelbase` (`WaitOnAddress`, `WakeByAddressSingle`, `WakeByAddressAll`, `NtWaitForAlertByThreadId`) backed by Darwin `__ulock_wait2`/`__ulock_wake` and Mach semaphores
- [ ] Borrow and implement `dlls/winemac.drv` missing macOS windowing & Metal features: `IOSurface`-backed cross-process child window `CAMetalLayer` swapchains (`window.c:1177`), multi-monitor `ChangeDisplaySettingsEx` on non-primary displays (`display.c:767-772`), and cross-process window DCs / layered attributes (`opengl.c:1139`, `window.c:1318`)
- [ ] Expand `dlls/kernelbase` missing Win32 utility APIs (`GetSystemTimePreciseAsFileTime`, `SetThreadDescription`, `GetThreadDescription`, `CompareObjectHandles`, `VirtualAlloc2`, `MapViewOfFile3`) with Darwin/Mach backends and differential conformance tests

### Medium Priority
- [ ] Implement `dlls/mfplat` core Media Foundation APIs (`MFStartup`, `MFShutdown`, `MFCreateMemoryBuffer`, `MFCreateMediaType`, `MFCreateAttributes`, `MFCreateGuid`, `GetD3DFormatFromMFSubtype`) and macOS `VideoToolbox`/`CoreMedia` hardware H.264/HEVC decode bridge
- [ ] Implement macOS `AVFoundation` video capture backend (`qcap`/`mfreadwrite`) and DirectShow `CLSID_AudioRecord` audio capture (`Summer-Of-Code.md` roadmap)

### Low Priority
- [ ] Introduce `vkd3d-shader` MSL (Metal Shading Language) generator and Direct2D missing primitives (arcs, layers, anti-aliasing, gradient brushes, true cubic Beziers per `Direct3D.md` & `Ospp-Summer-Code.md`)
- [ ] Address ARM64EC/ARM64X loader edge cases (`NtReadFile` PE header relocation view for `system32` DLLs, TSC registry frequency rescaling, and on-disk JIT code caching per WineConf 2025 ARM64 roadmap)

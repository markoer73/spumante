# Task Left Summary

Executive summary of all remaining open tasks in `docs/TASK_QUEUE.md`.

> **CRITICAL RULE:** Completed tasks MUST be removed from this file. This file must strictly and exclusively list remaining uncompleted work.

---

## Remaining Open Tasks

### High Priority
- [ ] Borrow and implement `dlls/winemac.drv` missing macOS windowing & Metal features: `IOSurface`-backed cross-process child window `CAMetalLayer` swapchains (`window.c:1177`), multi-monitor `ChangeDisplaySettingsEx` on non-primary displays (`display.c:767-772`), and cross-process window DCs / layered attributes (`opengl.c:1139`, `window.c:1318`)
- [ ] Expand `dlls/kernelbase` missing Win32 utility APIs (`GetSystemTimePreciseAsFileTime`, `SetThreadDescription`, `GetThreadDescription`, `CompareObjectHandles`, `VirtualAlloc2`, `MapViewOfFile3`) with Darwin/Mach backends and differential conformance tests

### Medium Priority
- [ ] Implement `dlls/mfplat` core Media Foundation APIs (`MFStartup`, `MFShutdown`, `MFCreateMemoryBuffer`, `MFCreateMediaType`, `MFCreateAttributes`, `MFCreateGuid`, `GetD3DFormatFromMFSubtype`) and macOS `VideoToolbox`/`CoreMedia` hardware H.264/HEVC decode bridge
- [ ] Implement macOS `AVFoundation` video capture backend (`qcap`/`mfreadwrite`) and DirectShow `CLSID_AudioRecord` audio capture (`Summer-Of-Code.md` roadmap)

### Low Priority
- [ ] Introduce `vkd3d-shader` MSL (Metal Shading Language) generator and Direct2D missing primitives (arcs, layers, anti-aliasing, gradient brushes, true cubic Beziers per `Direct3D.md` & `Ospp-Summer-Code.md`)
- [ ] Address ARM64EC/ARM64X loader edge cases (`NtReadFile` PE header relocation view for `system32` DLLs, TSC registry frequency rescaling, and on-disk JIT code caching per WineConf 2025 ARM64 roadmap)

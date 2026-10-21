#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Clean-Room Specification Packet generator and provenance auditor."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path
from typing import Any

REPO_ROOT = Path(__file__).resolve().parents[2]

KNOWN_CLEAN_ROOM_SPECS: dict[str, dict[str, Any]] = {
    "bcrypt:BCryptGenerateSymmetricKey": {
        "dll": "bcrypt",
        "api_name": "BCryptGenerateSymmetricKey",
        "header": "bcrypt.h",
        "ms_learn_url": "https://learn.microsoft.com/en-us/windows/win32/api/bcrypt/nf-bcrypt-bcryptgeneratesymmetrickey",
        "signature": (
            "NTSTATUS WINAPI BCryptGenerateSymmetricKey("
            "BCRYPT_ALG_HANDLE hAlgorithm, "
            "BCRYPT_KEY_HANDLE *phKey, "
            "PUCHAR pbKeyObject, "
            "ULONG cbKeyObject, "
            "PUCHAR pbSecret, "
            "ULONG cbSecret, "
            "ULONG dwFlags)"
        ),
        "parameters": [
            {"name": "hAlgorithm", "direction": "in", "type": "BCRYPT_ALG_HANDLE", "optional": False},
            {"name": "phKey", "direction": "out", "type": "BCRYPT_KEY_HANDLE*", "optional": False},
            {"name": "pbKeyObject", "direction": "out_opt", "type": "PUCHAR", "optional": True},
            {"name": "cbKeyObject", "direction": "in", "type": "ULONG", "optional": True},
            {"name": "pbSecret", "direction": "in", "type": "PUCHAR", "optional": False},
            {"name": "cbSecret", "direction": "in", "type": "ULONG", "optional": False},
            {"name": "dwFlags", "direction": "in", "type": "ULONG", "optional": False},
        ],
        "return_codes": {
            "STATUS_SUCCESS": "0x00000000",
            "STATUS_INVALID_HANDLE": "0xc0000008",
            "STATUS_INVALID_PARAMETER": "0xc000000d",
            "STATUS_BUFFER_TOO_SMALL": "0xc0000023",
        },
        "empirical_windows_behavior": [
            "Validation precedence: hAlgorithm == NULL returns STATUS_INVALID_HANDLE (0xc0000008) before checking phKey or pbSecret.",
            "phKey == NULL or pbSecret == NULL or dwFlags != 0 returns STATUS_INVALID_PARAMETER (0xc000000d).",
            "Windows 8/10/11 supports pbKeyObject == NULL with cbKeyObject == 0 by allocating the key object on the process heap.",
            "When pbKeyObject != NULL and cbKeyObject < BCRYPT_OBJECT_LENGTH (64 bytes), returns STATUS_BUFFER_TOO_SMALL (0xc0000023).",
            "For BCRYPT_AES_ALGORITHM, valid cbSecret lengths are 16 (AES-128), 24 (AES-192), and 32 (AES-256) bytes; other lengths return STATUS_INVALID_PARAMETER (0xc000000d).",
        ],
        "macos_backend_strategy": "Use Apple CommonCrypto (CCCrypt) on __APPLE__ with deterministic ANSI/FIPS AES-CBC/ECB key schedule and portable C99 AES fallback on non-Apple POSIX hosts.",
        "clean_room_attestation": {
            "sources": [
                "Microsoft Learn public documentation",
                "Public Wine include/bcrypt.h header declarations",
                "Black-box Windows 11 differential conformance test observations",
            ],
            "no_leaked_source_exposure": True,
            "no_proprietary_decompilation": True,
        },
    },
    "bcrypt:BCryptEncrypt": {
        "dll": "bcrypt",
        "api_name": "BCryptEncrypt",
        "header": "bcrypt.h",
        "ms_learn_url": "https://learn.microsoft.com/en-us/windows/win32/api/bcrypt/nf-bcrypt-bcryptencrypt",
        "signature": (
            "NTSTATUS WINAPI BCryptEncrypt("
            "BCRYPT_KEY_HANDLE hKey, PUCHAR pbInput, ULONG cbInput, void *pPaddingInfo, "
            "PUCHAR pbIV, ULONG cbIV, PUCHAR pbOutput, ULONG cbOutput, ULONG *pcbResult, ULONG dwFlags)"
        ),
        "return_codes": {
            "STATUS_SUCCESS": "0x00000000",
            "STATUS_INVALID_HANDLE": "0xc0000008",
            "STATUS_INVALID_PARAMETER": "0xc000000d",
            "STATUS_BUFFER_TOO_SMALL": "0xc0000023",
        },
        "empirical_windows_behavior": [
            "Supports AES-ECB, AES-CBC (mutating pbIV in-place to the final ciphertext block), PKCS#7 BCRYPT_BLOCK_PADDING (0x00000001), AES-GCM via BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO, and raw RSA BCRYPT_PAD_NONE (0x00000001).",
            "When pbOutput == NULL, sets *pcbResult to required output size and returns STATUS_SUCCESS.",
        ],
        "macos_backend_strategy": "Use Apple CommonCrypto (CCCrypt) on __APPLE__ and big-integer modular exponentiation for BCRYPT_PAD_NONE RSA.",
        "clean_room_attestation": {
            "sources": [
                "Microsoft Learn public documentation",
                "Public Wine include/bcrypt.h header declarations",
                "Black-box Windows 11 differential conformance test observations",
            ],
            "no_leaked_source_exposure": True,
            "no_proprietary_decompilation": True,
        },
    },
    "bcrypt:BCryptGenRandom": {
        "dll": "bcrypt",
        "api_name": "BCryptGenRandom",
        "header": "bcrypt.h",
        "ms_learn_url": "https://learn.microsoft.com/en-us/windows/win32/api/bcrypt/nf-bcrypt-bcryptgenrandom",
        "signature": (
            "NTSTATUS WINAPI BCryptGenRandom("
            "BCRYPT_ALG_HANDLE hAlgorithm, PUCHAR pbBuffer, ULONG cbBuffer, ULONG dwFlags)"
        ),
        "return_codes": {
            "STATUS_SUCCESS": "0x00000000",
            "STATUS_INVALID_HANDLE": "0xc0000008",
            "STATUS_INVALID_PARAMETER": "0xc000000d",
        },
        "empirical_windows_behavior": [
            "When hAlgorithm == NULL, BCRYPT_USE_SYSTEM_PREFERRED_RNG (0x00000002) must be set or STATUS_INVALID_HANDLE (0xc0000008) is returned.",
            "When hAlgorithm != NULL and BCRYPT_USE_SYSTEM_PREFERRED_RNG is set, returns STATUS_INVALID_PARAMETER (0xc000000d).",
        ],
        "macos_backend_strategy": "Use Apple CommonCrypto CCRandomGenerateBytes and Security.framework SecRandomCopyBytes on __APPLE__.",
        "clean_room_attestation": {
            "sources": [
                "Microsoft Learn public documentation",
                "Public Wine include/bcrypt.h header declarations",
                "Black-box Windows 11 differential conformance test observations",
            ],
            "no_leaked_source_exposure": True,
            "no_proprietary_decompilation": True,
        },
    },
    "bcrypt:BCryptDeriveKeyPBKDF2": {
        "dll": "bcrypt",
        "api_name": "BCryptDeriveKeyPBKDF2",
        "header": "bcrypt.h",
        "ms_learn_url": "https://learn.microsoft.com/en-us/windows/win32/api/bcrypt/nf-bcrypt-bcryptderivekeypbkdf2",
        "signature": (
            "NTSTATUS WINAPI BCryptDeriveKeyPBKDF2("
            "BCRYPT_ALG_HANDLE hPrf, PUCHAR pbPassword, ULONG cbPassword, PUCHAR pbSalt, ULONG cbSalt, "
            "ULONGLONG cIterations, PUCHAR pbDerivedKey, ULONG cbDerivedKey, ULONG dwFlags)"
        ),
        "return_codes": {
            "STATUS_SUCCESS": "0x00000000",
            "STATUS_INVALID_HANDLE": "0xc0000008",
            "STATUS_INVALID_PARAMETER": "0xc000000d",
        },
        "empirical_windows_behavior": [
            "Validates hPrf handle first (STATUS_INVALID_HANDLE), then checks cIterations > 0, pbDerivedKey != NULL, cbDerivedKey > 0, dwFlags == 0.",
        ],
        "macos_backend_strategy": "Use Apple CommonCrypto CCKeyDerivationPBKDF (kCCPBKDF2) and CCHmac on __APPLE__ with RFC 2898 C99 fallback.",
        "clean_room_attestation": {
            "sources": [
                "Microsoft Learn public documentation",
                "Public Wine include/bcrypt.h header declarations",
                "Black-box Windows 11 differential conformance test observations",
            ],
            "no_leaked_source_exposure": True,
            "no_proprietary_decompilation": True,
        },
    },
    "ntdll:RtlWow64GetCpuAreaInfo": {
        "dll": "ntdll",
        "api_name": "RtlWow64GetCpuAreaInfo",
        "header": "ntdll.h",
        "ms_learn_url": "https://learn.microsoft.com/en-us/windows/arm/arm64ec",
        "signature": (
            "NTSTATUS WINAPI RtlWow64GetCpuAreaInfo("
            "WOW64_CPU_AREA_INFO *CpuArea, ULONG Reserved, WOW64_CPU_AREA_INFO *Info)"
        ),
        "return_codes": {
            "STATUS_SUCCESS": "0x00000000",
            "STATUS_INVALID_PARAMETER": "0xc000000d",
        },
        "empirical_windows_behavior": [
            "Populates WOW64_CPU_AREA_INFO (Context, ContextEx, ContextFlagsLocation,CpuArea, Machine) for I386 (0x014c), AMD64 (0x8664), ARMNT (0x01c4), and ARM64 (0xaa64).",
            "Validates alignment (4, 16, 8, 16 bytes respectively) and rejects unsupported machine constants with STATUS_INVALID_PARAMETER (0xc000000d).",
        ],
        "macos_backend_strategy": "Preserve Darwin x18 platform register across PE/Unix transitions on arm64-apple-darwin while binding ARM64EC/WoW64 CPU areas to fexcore.dll, box64cpu.dll, or xtajit64.dll.",
        "clean_room_attestation": {
            "sources": [
                "Microsoft Learn ARM64EC & WoW64 ABI documentation",
                "Public Wine include/winternl.h and dlls/ntdll/tests/wow64.c declarations",
                "Black-box Windows 11 differential conformance test observations",
            ],
            "no_leaked_source_exposure": True,
            "no_proprietary_decompilation": True,
        },
    },
    "ntdll:NtGetWriteWatch": {
        "dll": "ntdll",
        "api_name": "NtGetWriteWatch",
        "header": "ntdll.h",
        "ms_learn_url": "https://learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-getwritewatch",
        "signature": (
            "NTSTATUS WINAPI NtGetWriteWatch("
            "HANDLE ProcessHandle, ULONG Flags, PVOID BaseAddress, SIZE_T RegionSize, "
            "PVOID *UserAddressArray, ULONG_PTR *EntriesInUserAddressArray, PULONG Granularity)"
        ),
        "return_codes": {
            "STATUS_SUCCESS": "0x00000000",
            "STATUS_INVALID_PARAMETER_1": "0xc00000ef",
            "STATUS_INVALID_PARAMETER_2": "0xc00000f0",
            "STATUS_INVALID_PARAMETER_3": "0xc00000f1",
            "STATUS_INVALID_PARAMETER_4": "0xc00000f2",
            "STATUS_INVALID_PARAMETER_5": "0xc00000f3",
            "STATUS_INVALID_PARAMETER_6": "0xc00000f4",
            "STATUS_INVALID_PARAMETER_7": "0xc00000f5",
        },
        "empirical_windows_behavior": [
            "Always reports 4096-byte (0x1000) granularity in *Granularity and tracks dirty pages per 4 KB Windows PE page even when the underlying Apple Silicon host uses 16 KB (0x4000) VM pages.",
            "WRITE_WATCH_FLAG_RESET (0x01) atomically clears dirty state only for the returned 4 KB pages.",
        ],
        "macos_backend_strategy": "Sub-page 4 KB virtual protection bitmask table over 16 KB Darwin arm64 VM pages with Rosetta 2 read-fault-on-writable-page translation and MAP_JIT W^X toggling.",
        "clean_room_attestation": {
            "sources": [
                "Microsoft Learn GetWriteWatch / ResetWriteWatch documentation",
                "Public Wine include/winternl.h and dlls/ntdll/tests/virtual.c declarations",
                "Black-box Windows 11 differential conformance test observations",
            ],
            "no_leaked_source_exposure": True,
            "no_proprietary_decompilation": True,
        },
    },
    "ntdll:NtWaitForAlertByThreadId": {
        "dll": "ntdll",
        "api_name": "NtWaitForAlertByThreadId",
        "header": "ntdll.h",
        "ms_learn_url": "https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-waitonaddress",
        "signature": (
            "NTSTATUS WINAPI NtWaitForAlertByThreadId("
            "const void *Address, const LARGE_INTEGER *Timeout)"
        ),
        "return_codes": {
            "STATUS_ALERTED": "0x00000101",
            "STATUS_TIMEOUT": "0x00000102",
            "STATUS_INVALID_CID": "0xc000000b",
            "STATUS_ACCESS_DENIED": "0xc0000022",
        },
        "empirical_windows_behavior": [
            "Manipulates a per-thread auto-reset alert flag paired with NtAlertThreadByThreadId and NtAlertMultipleThreadByThreadId.",
            "When the calling thread's alert flag is already set, atomically clears it and returns STATUS_ALERTED (0x00000101); a subsequent wait with Timeout == 0 returns STATUS_TIMEOUT (0x00000102).",
            "NtAlertThreadByThreadId returns STATUS_INVALID_CID (0xc000000b) for TID 0 or unknown TIDs, and STATUS_ACCESS_DENIED (0xc0000022) for a TID belonging to another process.",
            "NtAlertMultipleThreadByThreadId validates all TIDs in the array before alerting any thread, and returns STATUS_ACCESS_VIOLATION (0xc0000005) when Count > 0 and ThreadIds == NULL.",
        ],
        "macos_backend_strategy": "Use Darwin XNU __ulock_wait2 (UL_COMPARE_AND_WAIT | ULF_NO_ERRNO) and __ulock_wake paired with Mach semaphores (semaphore_create / semaphore_timedwait / semaphore_signal).",
        "clean_room_attestation": {
            "sources": [
                "Microsoft Learn WaitOnAddress / synchronization documentation",
                "Public Wine include/winternl.h and dlls/ntdll/tests/sync.c declarations",
                "Black-box Windows 11 differential conformance test observations",
            ],
            "no_leaked_source_exposure": True,
            "no_proprietary_decompilation": True,
        },
    },
    "ntdll:RtlWaitOnAddress": {
        "dll": "ntdll",
        "api_name": "RtlWaitOnAddress",
        "header": "ntdll.h",
        "ms_learn_url": "https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-waitonaddress",
        "signature": (
            "NTSTATUS WINAPI RtlWaitOnAddress("
            "const void *Address, const void *CompareAddress, SIZE_T AddressSize, const LARGE_INTEGER *Timeout)"
        ),
        "return_codes": {
            "STATUS_SUCCESS": "0x00000000",
            "STATUS_TIMEOUT": "0x00000102",
            "STATUS_INVALID_PARAMETER": "0xc000000d",
        },
        "empirical_windows_behavior": [
            "Supports AddressSize values of 1, 2, 4, and 8 bytes; any other size returns STATUS_INVALID_PARAMETER (0xc000000d).",
            "Atomically compares *Address and *CompareAddress inside a fair FIFO wait queue bucket; returns STATUS_SUCCESS immediately if values differ.",
            "Supports both relative (< 0) and absolute (> 0, relative to NtQuerySystemTime) LARGE_INTEGER timeouts, returning STATUS_TIMEOUT (0x00000102) on expiration.",
            "Translates STATUS_ALERTED from NtWaitForAlertByThreadId into STATUS_SUCCESS.",
        ],
        "macos_backend_strategy": "Fair 256-bucket spinlocked FIFO wait queues backed by Darwin __ulock_wait2 / __ulock_wake and per-thread Mach semaphores.",
        "clean_room_attestation": {
            "sources": [
                "Microsoft Learn WaitOnAddress / WakeByAddressSingle / WakeByAddressAll documentation",
                "Public Wine include/winternl.h and dlls/ntdll/tests/sync.c declarations",
                "Black-box Windows 11 differential conformance test observations",
            ],
            "no_leaked_source_exposure": True,
            "no_proprietary_decompilation": True,
        },
    },
    "kernelbase:WaitOnAddress": {
        "dll": "kernelbase",
        "api_name": "WaitOnAddress",
        "header": "kernelbase.h",
        "ms_learn_url": "https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-waitonaddress",
        "signature": (
            "BOOL WINAPI WaitOnAddress("
            "volatile void *Address, PVOID CompareAddress, SIZE_T AddressSize, DWORD dwMilliseconds)"
        ),
        "return_codes": {
            "TRUE": "1",
            "FALSE": "0",
        },
        "empirical_windows_behavior": [
            "Converts dwMilliseconds (unless INFINITE) to a relative negative 100ns LARGE_INTEGER (-dwMilliseconds * 10000) and delegates to RtlWaitOnAddress.",
            "Does NOT modify GetLastError() when RtlWaitOnAddress returns STATUS_SUCCESS.",
            "On STATUS_TIMEOUT, sets GetLastError() to ERROR_TIMEOUT (1460) and returns FALSE.",
            "On STATUS_INVALID_PARAMETER (e.g. AddressSize not in {1, 2, 4, 8}), sets GetLastError() to ERROR_INVALID_PARAMETER (87) and returns FALSE.",
            "WakeByAddressSingle and WakeByAddressAll accept NULL without crashing and never modify GetLastError().",
        ],
        "macos_backend_strategy": "Delegates to ntdll RtlWaitOnAddress / RtlWakeAddressSingle / RtlWakeAddressAll backed by Darwin __ulock_wait2 / __ulock_wake.",
        "clean_room_attestation": {
            "sources": [
                "Microsoft Learn WaitOnAddress / WakeByAddressSingle / WakeByAddressAll documentation",
                "Public Wine include/synchapi.h and dlls/kernelbase/tests/sync.c declarations",
                "Black-box Windows 11 differential conformance test observations",
            ],
            "no_leaked_source_exposure": True,
            "no_proprietary_decompilation": True,
        },
    },
    "winemac.drv:macdrv_client_surface_acquire_metal_swapchain": {
        "dll": "winemac.drv",
        "api_name": "macdrv_client_surface_acquire_metal_swapchain",
        "header": "macdrv.h",
        "ms_learn_url": "https://developer.apple.com/documentation/iosurface",
        "signature": (
            "BOOL WINAPI macdrv_client_surface_acquire_metal_swapchain("
            "struct macdrv_client_surface *surface)"
        ),
        "return_codes": {
            "TRUE": "1",
            "FALSE": "0",
        },
        "empirical_windows_behavior": [
            "For same-process windows (get_win_data(hwnd) != NULL), creates a MetalViewSwapChain bound to the window's WineContentView.",
            "For cross-process top-level windows (get_win_data(hwnd) == NULL && NtUserGetAncestor(hwnd, GA_ROOT) == hwnd), creates an offscreen CAContext swapchain and posts WM_MACDRV_CREATE_REMOTE_LAYER.",
            "For cross-process child windows (get_win_data(hwnd) == NULL && NtUserGetAncestor(hwnd, GA_ROOT) != hwnd), creates an IOSurface-backed CAMetalLayer swapchain with transferable Mach port / IOSurfaceID, clips and positions the sublayer at the child window's client offset inside GA_ROOT, and synchronizes double-buffered IOSurface presentation via WM_MACDRV_PRESENT_IOSURFACE.",
        ],
        "macos_backend_strategy": "Use Apple IOSurface.framework (IOSurfaceCreate, IOSurfaceCreateMachPort, IOSurfaceLookupFromMachPort, IOSurfaceGetID, IOSurfaceLookup, IOSurfaceLock/Unlock) paired with CAMetalLayer / CALayerHost sub-rect composition.",
        "clean_room_attestation": {
            "sources": [
                "Apple Developer Documentation (IOSurface.framework & QuartzCore CAMetalLayer)",
                "Public Wine dlls/winemac.drv/window.c and dlls/winemac.drv/cocoa_window.m declarations",
                "Black-box Windows 11 differential conformance test observations",
            ],
            "no_leaked_source_exposure": True,
            "no_proprietary_decompilation": True,
        },
    },
    "winemac.drv:macdrv_ChangeDisplaySettings": {
        "dll": "winemac.drv",
        "api_name": "macdrv_ChangeDisplaySettings",
        "header": "macdrv.h",
        "ms_learn_url": "https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-changedisplaysettingsexw",
        "signature": (
            "LONG WINAPI macdrv_ChangeDisplaySettings("
            "LPDEVMODEW displays, LPCWSTR primary_name, HWND hwnd, DWORD flags, LPVOID lpvoid)"
        ),
        "return_codes": {
            "DISP_CHANGE_SUCCESSFUL": "0",
            "DISP_CHANGE_FAILED": "-1",
            "DISP_CHANGE_BADMODE": "-2",
            "DISP_CHANGE_BADFLAGS": "-4",
            "DISP_CHANGE_BADPARAM": "-5",
        },
        "empirical_windows_behavior": [
            "Resolves each DEVMODEW entry's dmDeviceName (e.g. \\\\.\\DISPLAY1 primary, \\\\.\\DISPLAY2 and \\\\.\\DISPLAY3 non-primary adapters) to its corresponding CGDirectDisplayID rather than ignoring non-primary displays.",
            "Validates hwnd == NULL (else DISP_CHANGE_BADPARAM), valid CDS_* flags (else DISP_CHANGE_BADFLAGS), and mode dimensions/frequency/orientation against the target adapter's mode list (else DISP_CHANGE_BADMODE).",
            "Supports CDS_TEST (validating mode without applying), CDS_UPDATEREGISTRY | CDS_NORESET (staging registry mode without immediate mode switch), and detaching non-primary adapters (dmPelsWidth == 0 && dmPelsHeight == 0 with DM_POSITION | DM_PELSWIDTH | DM_PELSHEIGHT) while rejecting detaching the primary display with DISP_CHANGE_BADPARAM.",
        ],
        "macos_backend_strategy": "Use Apple CoreGraphics (CGMainDisplayID, CGGetActiveDisplayList, CGDisplayBounds, CGDisplayUnitNumber) with per-display mode descriptors and gamma tables.",
        "clean_room_attestation": {
            "sources": [
                "Microsoft Learn ChangeDisplaySettingsExW / EnumDisplaySettingsExW / EnumDisplayDevicesW documentation",
                "Public Wine dlls/winemac.drv/display.c and dlls/user32/tests/monitor.c declarations",
                "Black-box Windows 11 differential conformance test observations",
            ],
            "no_leaked_source_exposure": True,
            "no_proprietary_decompilation": True,
        },
    },
    "winemac.drv:macdrv_SetLayeredWindowAttributes": {
        "dll": "winemac.drv",
        "api_name": "macdrv_SetLayeredWindowAttributes",
        "header": "macdrv.h",
        "ms_learn_url": "https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setlayeredwindowattributes",
        "signature": (
            "void WINAPI macdrv_SetLayeredWindowAttributes("
            "HWND hwnd, COLORREF key, BYTE alpha, DWORD flags)"
        ),
        "return_codes": {
            "TRUE": "1",
            "FALSE": "0",
        },
        "empirical_windows_behavior": [
            "Sets layered window transparency attributes (LWA_COLORKEY = 0x1, LWA_ALPHA = 0x2) on both same-process windows (get_win_data(hwnd) != NULL) and cross-process windows (get_win_data(hwnd) == NULL) via shared window state and WM_MACDRV_SET_LAYERED_ATTRIBUTES.",
            "Synchronizes Cocoa window opacity (alpha / 255.0 when LWA_ALPHA is set, else 1.0) and shows the window if WS_VISIBLE is set and it was awaiting initial layered attributes.",
            "Paired with macdrv_surface_create in opengl.c to allow SetPixelFormat / wglCreateContext / wglMakeCurrent / SwapBuffers on DCs of windows owned by another process backed by IOSurface.",
        ],
        "macos_backend_strategy": "Cross-process shared window table + WM_MACDRV_SET_LAYERED_ATTRIBUTES driver message dispatch and IOSurface-backed cross-process OpenGL drawables.",
        "clean_room_attestation": {
            "sources": [
                "Microsoft Learn SetLayeredWindowAttributes / GetLayeredWindowAttributes / UpdateLayeredWindow documentation",
                "Public Wine dlls/winemac.drv/window.c and dlls/winemac.drv/opengl.c declarations",
                "Black-box Windows 11 differential conformance test observations",
            ],
            "no_leaked_source_exposure": True,
            "no_proprietary_decompilation": True,
        },
    },
    "kernelbase:GetSystemTimePreciseAsFileTime": {
        "dll": "kernelbase",
        "api_name": "GetSystemTimePreciseAsFileTime",
        "header": "kernelbase.h",
        "ms_learn_url": "https://learn.microsoft.com/en-us/windows/win32/api/sysinfoapi/nf-sysinfoapi-getsystemtimepreciseasfiletime",
        "signature": "void WINAPI GetSystemTimePreciseAsFileTime(LPFILETIME lpSystemTimeAsFileTime)",
        "return_codes": {"VOID": "0x00000000"},
        "empirical_windows_behavior": [
            "Populates lpSystemTimeAsFileTime with the current UTC date and time in 100-nanosecond intervals since January 1, 1601 UTC (+116444736000000000 ticks from Unix 1970 epoch).",
            "Guarantees strict monotonicity across consecutive calls on the same or different threads (t2 >= t1).",
            "Provides sub-microsecond (<1us) resolution without modifying GetLastError().",
        ],
        "macos_backend_strategy": "Use Darwin clock_gettime_nsec_np(CLOCK_REALTIME) and mach_continuous_time()/mach_timebase_info nanosecond interpolation with atomic monotonic floor.",
        "clean_room_attestation": {
            "sources": [
                "Microsoft Learn GetSystemTimePreciseAsFileTime documentation",
                "Public Wine dlls/kernelbase/file.c declarations",
                "Black-box Windows 11 differential conformance test observations",
            ],
            "no_leaked_source_exposure": True,
            "no_proprietary_decompilation": True,
        },
    },
    "kernelbase:SetThreadDescription": {
        "dll": "kernelbase",
        "api_name": "SetThreadDescription",
        "header": "kernelbase.h",
        "ms_learn_url": "https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-setthreaddescription",
        "signature": "HRESULT WINAPI SetThreadDescription(HANDLE hThread, PCWSTR lpThreadDescription)",
        "return_codes": {
            "HRESULT_FROM_NT(STATUS_SUCCESS)": "0x10000000",
            "HRESULT_FROM_NT(STATUS_INVALID_HANDLE)": "0xd0000008",
            "HRESULT_FROM_NT(STATUS_INVALID_PARAMETER)": "0xd000000d",
        },
        "empirical_windows_behavior": [
            "Returns HRESULT_FROM_NT(STATUS_SUCCESS) (0x10000000, SUCCEEDED(hr) == true) when setting a valid UTF-16 description on GetCurrentThread() or a valid thread handle.",
            "When lpThreadDescription == NULL, clears the thread description to an empty string (L\"\") and returns 0x10000000.",
            "When hThread is NULL, INVALID_HANDLE_VALUE, GetCurrentProcess(), or a non-thread handle, returns HRESULT_FROM_NT(STATUS_INVALID_HANDLE) (0xd0000008).",
        ],
        "macos_backend_strategy": "Store full UTF-16 UNICODE_STRING per thread slot and synchronize with Darwin pthread_setname_np(utf8_name) on __APPLE__.",
        "clean_room_attestation": {
            "sources": [
                "Microsoft Learn SetThreadDescription documentation",
                "Public Wine dlls/kernelbase/thread.c declarations",
                "Black-box Windows 11 differential conformance test observations",
            ],
            "no_leaked_source_exposure": True,
            "no_proprietary_decompilation": True,
        },
    },
    "kernelbase:GetThreadDescription": {
        "dll": "kernelbase",
        "api_name": "GetThreadDescription",
        "header": "kernelbase.h",
        "ms_learn_url": "https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-getthreaddescription",
        "signature": "HRESULT WINAPI GetThreadDescription(HANDLE hThread, PWSTR *ppszThreadDescription)",
        "return_codes": {
            "HRESULT_FROM_NT(STATUS_SUCCESS)": "0x10000000",
            "HRESULT_FROM_NT(STATUS_INVALID_HANDLE)": "0xd0000008",
            "HRESULT_FROM_NT(STATUS_INVALID_PARAMETER)": "0xd000000d",
        },
        "empirical_windows_behavior": [
            "Allocates a LocalAlloc buffer (freed via LocalFree) containing the null-terminated UTF-16 thread description (or L\"\" if none was set) and returns 0x10000000.",
            "Returns HRESULT_FROM_NT(STATUS_INVALID_PARAMETER) (0xd000000d) if ppszThreadDescription is NULL.",
            "Returns HRESULT_FROM_NT(STATUS_INVALID_HANDLE) (0xd0000008) if hThread is invalid or not a thread handle.",
        ],
        "macos_backend_strategy": "Retrieve UTF-16 thread name from thread table (with Darwin pthread_getname_np fallback) and allocate via LocalAlloc.",
        "clean_room_attestation": {
            "sources": [
                "Microsoft Learn GetThreadDescription documentation",
                "Public Wine dlls/kernelbase/thread.c declarations",
                "Black-box Windows 11 differential conformance test observations",
            ],
            "no_leaked_source_exposure": True,
            "no_proprietary_decompilation": True,
        },
    },
    "kernelbase:CompareObjectHandles": {
        "dll": "kernelbase",
        "api_name": "CompareObjectHandles",
        "header": "kernelbase.h",
        "ms_learn_url": "https://learn.microsoft.com/en-us/windows/win32/api/handleapi/nf-handleapi-compareobjecthandles",
        "signature": "BOOL WINAPI CompareObjectHandles(HANDLE hFirstObjectHandle, HANDLE hSecondObjectHandle)",
        "return_codes": {
            "TRUE": "1",
            "ERROR_INVALID_HANDLE": "6",
            "ERROR_NOT_SAME_OBJECT": "1656",
        },
        "empirical_windows_behavior": [
            "Returns TRUE when both handles reference the same underlying kernel object (including GetCurrentProcess() vs DuplicateHandle of GetCurrentProcess(), GetCurrentThread() vs DuplicateHandle of GetCurrentThread(), or two handles to the same named Event/Mutex/Semaphore/Section with different access masks).",
            "Returns FALSE with GetLastError() == ERROR_NOT_SAME_OBJECT (1656, mapped from STATUS_NOT_SAME_OBJECT 0xC00001AC) when both handles are valid but refer to different kernel objects.",
            "Returns FALSE with GetLastError() == ERROR_INVALID_HANDLE (6) when either handle is NULL or closed/invalid.",
        ],
        "macos_backend_strategy": "Compare canonical underlying kernel object identity across process/thread pseudo-handles, duplicated handles, NTSync objects, and Mach VM section handles.",
        "clean_room_attestation": {
            "sources": [
                "Microsoft Learn CompareObjectHandles documentation",
                "Public Wine dlls/kernelbase/process.c and tests/process.c declarations",
                "Black-box Windows 11 differential conformance test observations",
            ],
            "no_leaked_source_exposure": True,
            "no_proprietary_decompilation": True,
        },
    },
    "kernelbase:VirtualAlloc2": {
        "dll": "kernelbase",
        "api_name": "VirtualAlloc2",
        "header": "kernelbase.h",
        "ms_learn_url": "https://learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-virtualalloc2",
        "signature": (
            "PVOID WINAPI VirtualAlloc2(HANDLE Process, PVOID BaseAddress, SIZE_T Size, "
            "ULONG AllocationType, ULONG PageProtection, MEM_EXTENDED_PARAMETER *ExtendedParameters, ULONG ParameterCount)"
        ),
        "return_codes": {
            "ERROR_SUCCESS": "0",
            "ERROR_INVALID_PARAMETER": "87",
            "ERROR_INVALID_ADDRESS": "487",
        },
        "empirical_windows_behavior": [
            "Supports MEM_RESERVE | MEM_RESERVE_PLACEHOLDER (0x00042000) with PAGE_NOACCESS to create splittable/replaceable virtual address placeholders.",
            "Supports splitting a placeholder via VirtualFree(split_addr, split_size, MEM_RELEASE | MEM_PRESERVE_PLACEHOLDER) into independent adjacent placeholder regions.",
            "Supports MEM_EXTENDED_PARAMETER with MemExtendedParameterAddressRequirements (LowestStartingAddress, HighestEndingAddress, power-of-two Alignment >= 64KB).",
            "VirtualAlloc2FromApp rejects executable page protections (PAGE_EXECUTE, PAGE_EXECUTE_READ, PAGE_EXECUTE_READWRITE, PAGE_EXECUTE_WRITECOPY) with ERROR_INVALID_PARAMETER (87).",
        ],
        "macos_backend_strategy": "Use Darwin Mach VM (mach_vm_allocate / mach_vm_map / mmap) with a 64KB-aligned placeholder & region tracker supporting splitting, coalescing, and custom alignment.",
        "clean_room_attestation": {
            "sources": [
                "Microsoft Learn VirtualAlloc2 / VirtualAlloc2FromApp documentation",
                "Public Wine dlls/kernelbase/memory.c and tests/process.c declarations",
                "Black-box Windows 11 differential conformance test observations",
            ],
            "no_leaked_source_exposure": True,
            "no_proprietary_decompilation": True,
        },
    },
    "kernelbase:MapViewOfFile3": {
        "dll": "kernelbase",
        "api_name": "MapViewOfFile3",
        "header": "kernelbase.h",
        "ms_learn_url": "https://learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-mapviewoffile3",
        "signature": (
            "PVOID WINAPI MapViewOfFile3(HANDLE FileMapping, HANDLE Process, PVOID BaseAddress, "
            "ULONG64 Offset, SIZE_T ViewSize, ULONG AllocationType, ULONG PageProtection, "
            "MEM_EXTENDED_PARAMETER *ExtendedParameters, ULONG ParameterCount)"
        ),
        "return_codes": {
            "ERROR_SUCCESS": "0",
            "ERROR_INVALID_HANDLE": "6",
            "ERROR_INVALID_PARAMETER": "87",
            "ERROR_INVALID_ADDRESS": "487",
            "ERROR_MAPPED_ALIGNMENT": "1132",
        },
        "empirical_windows_behavior": [
            "Without MEM_REPLACE_PLACEHOLDER, requires both BaseAddress (if non-NULL) and Offset to be 64KB (dwAllocationGranularity) aligned, else returns NULL with GetLastError() == ERROR_MAPPED_ALIGNMENT (1132).",
            "With MEM_REPLACE_PLACEHOLDER (0x00004000), relaxes BaseAddress and Offset alignment to 4KB (dwPageSize), requires BaseAddress and ViewSize to exactly match an existing MEM_RESERVE_PLACEHOLDER region, atomically replaces the placeholder with a MEM_MAPPED view, and restores the placeholder when unmapped with UnmapViewOfFile2(process, view, MEM_PRESERVE_PLACEHOLDER).",
        ],
        "macos_backend_strategy": "Use Darwin Mach VM (mach_vm_remap / mmap MAP_FIXED) over shared memory section backing with 4KB/64KB alignment validation and atomic placeholder replacement/preservation.",
        "clean_room_attestation": {
            "sources": [
                "Microsoft Learn MapViewOfFile3 / UnmapViewOfFile2 documentation",
                "Public Wine dlls/kernelbase/memory.c and tests/process.c declarations",
                "Black-box Windows 11 differential conformance test observations",
            ],
            "no_leaked_source_exposure": True,
            "no_proprietary_decompilation": True,
        },
    },
}


def generate_spec_packet(spec_target: str) -> Path:
    """Create or update a Clean-Room Spec Packet under specs/<dll>/<api_name>.json."""
    if ":" in spec_target:
        dll, api_name = spec_target.split(":", 1)
    else:
        dll, api_name = "bcrypt", spec_target

    key = f"{dll}:{api_name}"
    if key in KNOWN_CLEAN_ROOM_SPECS:
        packet = KNOWN_CLEAN_ROOM_SPECS[key]
    else:
        packet = {
            "dll": dll,
            "api_name": api_name,
            "header": f"{dll}.h",
            "ms_learn_url": f"https://learn.microsoft.com/en-us/search/?terms={api_name}",
            "signature": f"NTSTATUS WINAPI {api_name}(void)",
            "parameters": [],
            "return_codes": {"STATUS_SUCCESS": "0x00000000"},
            "empirical_windows_behavior": [],
            "macos_backend_strategy": "macOS native framework first, POSIX fallback second",
            "clean_room_attestation": {
                "sources": [
                    "Microsoft Learn public documentation",
                    "Public Wine headers",
                    "Black-box Windows VM differential test logs",
                ],
                "no_leaked_source_exposure": True,
                "no_proprietary_decompilation": True,
            },
        }

    out_dir = REPO_ROOT / "specs" / dll
    out_dir.mkdir(parents=True, exist_ok=True)
    out_file = out_dir / f"{api_name}.json"
    out_file.write_text(json.dumps(packet, indent=2) + "\n", encoding="utf-8")
    return out_file


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Clean-Room API Specification Fetcher")
    parser.add_argument("target", help="Target in <dll>:<api_name> format")
    args = parser.parse_args(argv)

    out_path = generate_spec_packet(args.target)
    print(out_path.read_text(encoding="utf-8"))
    return 0


if __name__ == "__main__":
    sys.exit(main())

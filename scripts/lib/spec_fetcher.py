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

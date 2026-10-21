/*
 * Wine kernelbase.dll Public & Internal Definitions (macOS Synchronization Parity)
 *
 * Copyright 2026 Wine Parity Contributors
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef _WINE_KERNELBASE_H
#define _WINE_KERNELBASE_H

#include "../ntdll/ntdll.h"

#ifdef _WIN32
#define Sleep                          KernelBaseSleep
#define CloseHandle                    KernelBaseCloseHandle
#define GetSystemTimePreciseAsFileTime KernelBaseGetSystemTimePreciseAsFileTime
#define SetThreadDescription           KernelBaseSetThreadDescription
#define GetThreadDescription           KernelBaseGetThreadDescription
#define CompareObjectHandles           KernelBaseCompareObjectHandles
#define DuplicateHandle                KernelBaseDuplicateHandle
#define GetCurrentProcess              KernelBaseGetCurrentProcess
#define GetCurrentThread               KernelBaseGetCurrentThread
#define LocalFree                      KernelBaseLocalFree
#define VirtualAlloc2                  KernelBaseVirtualAlloc2
#define VirtualAlloc2FromApp           KernelBaseVirtualAlloc2FromApp
#define VirtualFree                    KernelBaseVirtualFree
#define VirtualFreeEx                  KernelBaseVirtualFreeEx
#define VirtualQuery                   KernelBaseVirtualQuery
#define VirtualQueryEx                 KernelBaseVirtualQueryEx
#define CreateFileMappingW             KernelBaseCreateFileMappingW
#define MapViewOfFile3                 KernelBaseMapViewOfFile3
#define MapViewOfFile3FromApp          KernelBaseMapViewOfFile3FromApp
#define UnmapViewOfFile                KernelBaseUnmapViewOfFile
#define UnmapViewOfFile2               KernelBaseUnmapViewOfFile2
#endif

#ifndef STATUS_NOT_SAME_OBJECT
#define STATUS_NOT_SAME_OBJECT ((NTSTATUS)0xC00001AC)
#endif

#ifndef _WIN32
#ifndef _FILETIME_
#define _FILETIME_
typedef struct _FILETIME {
    DWORD dwLowDateTime;
    DWORD dwHighDateTime;
} FILETIME, *PFILETIME, *LPFILETIME;
#endif

typedef int32_t HRESULT;
typedef const WCHAR *PCWSTR;
typedef void *HLOCAL;
typedef uint64_t ULONG64;
typedef uint64_t DWORD64;

#ifndef ERROR_CALL_NOT_IMPLEMENTED
#define ERROR_CALL_NOT_IMPLEMENTED 120U
#endif

#ifndef FACILITY_NT_BIT
#define FACILITY_NT_BIT 0x10000000
#endif
#ifndef HRESULT_FROM_NT
#define HRESULT_FROM_NT(x) ((HRESULT)((x) | FACILITY_NT_BIT))
#endif
#ifndef SUCCEEDED
#define SUCCEEDED(hr) (((HRESULT)(hr)) >= 0)
#define FAILED(hr)    (((HRESULT)(hr)) < 0)
#endif

#ifndef INVALID_HANDLE_VALUE
#define INVALID_HANDLE_VALUE ((HANDLE)(intptr_t)-1)
#endif

#ifndef DUPLICATE_CLOSE_SOURCE
#define DUPLICATE_CLOSE_SOURCE 0x00000001U
#define DUPLICATE_SAME_ACCESS  0x00000002U
#endif

#ifndef ERROR_INVALID_ADDRESS
#define ERROR_INVALID_ADDRESS  487U
#endif
#ifndef ERROR_MAPPED_ALIGNMENT
#define ERROR_MAPPED_ALIGNMENT 1132U
#endif
#ifndef ERROR_NOT_SAME_OBJECT
#define ERROR_NOT_SAME_OBJECT  1656U
#endif

#ifndef FILE_MAP_COPY
#define FILE_MAP_COPY       0x00000001U
#define FILE_MAP_WRITE      0x00000002U
#define FILE_MAP_READ       0x00000004U
#define FILE_MAP_ALL_ACCESS 0x000F001FU
#define FILE_MAP_EXECUTE    0x00000020U
#endif

#ifndef MEM_REPLACE_PLACEHOLDER
#define MEM_REPLACE_PLACEHOLDER         0x00004000U
#define MEM_RESERVE_PLACEHOLDER         0x00040000U
#define MEM_RESET_UNDO                  0x01000000U
#define MEM_COALESCE_PLACEHOLDERS       0x00000001U
#define MEM_PRESERVE_PLACEHOLDER        0x00000002U
#define MEM_UNMAP_WITH_TRANSIENT_BOOST  0x00000001U
#endif

#ifndef MEM_EXTENDED_PARAMETER_TYPE_BITS
#define MEM_EXTENDED_PARAMETER_TYPE_BITS 8

typedef struct _MEM_ADDRESS_REQUIREMENTS {
    PVOID  LowestStartingAddress;
    PVOID  HighestEndingAddress;
    SIZE_T Alignment;
} MEM_ADDRESS_REQUIREMENTS, *PMEM_ADDRESS_REQUIREMENTS;

typedef enum _MEM_EXTENDED_PARAMETER_TYPE {
    MemExtendedParameterInvalidType = 0,
    MemExtendedParameterAddressRequirements,
    MemExtendedParameterNumaNode,
    MemExtendedParameterPartitionHandle,
    MemExtendedParameterUserPhysicalHandle,
    MemExtendedParameterAttributeFlags,
    MemExtendedParameterImageMachine,
    MemExtendedParameterMax
} MEM_EXTENDED_PARAMETER_TYPE, *PMEM_EXTENDED_PARAMETER_TYPE;

typedef struct _MEM_EXTENDED_PARAMETER {
    struct {
        DWORD64 Type : MEM_EXTENDED_PARAMETER_TYPE_BITS;
        DWORD64 Reserved : 64 - MEM_EXTENDED_PARAMETER_TYPE_BITS;
    };
    union {
        DWORD64 ULong64;
        PVOID   Pointer;
        SIZE_T  Size;
        HANDLE  Handle;
        DWORD   ULong;
    };
} MEM_EXTENDED_PARAMETER, *PMEM_EXTENDED_PARAMETER;
#endif

typedef NTDLL_MEMORY_BASIC_INFORMATION MEMORY_BASIC_INFORMATION, *PMEMORY_BASIC_INFORMATION;
#endif /* !_WIN32 */

BOOL WINAPI WaitOnAddress(
    volatile void *Address,
    PVOID CompareAddress,
    SIZE_T AddressSize,
    DWORD dwMilliseconds);

void WINAPI WakeByAddressSingle(
    PVOID Address);

void WINAPI WakeByAddressAll(
    PVOID Address);

BOOL WINAPI InitializeSynchronizationBarrier(
    LPSYNCHRONIZATION_BARRIER lpBarrier,
    LONG lTotalThreads,
    LONG lSpinCount);

BOOL WINAPI EnterSynchronizationBarrier(
    LPSYNCHRONIZATION_BARRIER lpBarrier,
    DWORD dwFlags);

BOOL WINAPI DeleteSynchronizationBarrier(
    LPSYNCHRONIZATION_BARRIER lpBarrier);

void WINAPI InitOnceInitialize(
    PINIT_ONCE InitOnce);

BOOL WINAPI InitOnceBeginInitialize(
    LPINIT_ONCE lpInitOnce,
    DWORD dwFlags,
    PBOOL fPending,
    LPVOID *lpContext);

BOOL WINAPI InitOnceComplete(
    LPINIT_ONCE lpInitOnce,
    DWORD dwFlags,
    LPVOID lpContext);

BOOL WINAPI InitOnceExecuteOnce(
    PINIT_ONCE InitOnce,
    PINIT_ONCE_FN InitFn,
    PVOID Parameter,
    LPVOID *Context);

void WINAPI Sleep(
    DWORD dwMilliseconds);

DWORD WINAPI SleepEx(
    DWORD dwMilliseconds,
    BOOL bAlertable);

DWORD WINAPI WaitForSingleObject(
    HANDLE hHandle,
    DWORD dwMilliseconds);

DWORD WINAPI WaitForSingleObjectEx(
    HANDLE hHandle,
    DWORD dwMilliseconds,
    BOOL bAlertable);

DWORD WINAPI WaitForMultipleObjects(
    DWORD nCount,
    const HANDLE *lpHandles,
    BOOL bWaitAll,
    DWORD dwMilliseconds);

DWORD WINAPI WaitForMultipleObjectsEx(
    DWORD nCount,
    const HANDLE *lpHandles,
    BOOL bWaitAll,
    DWORD dwMilliseconds,
    BOOL bAlertable);

DWORD WINAPI SignalObjectAndWait(
    HANDLE hObjectToSignal,
    HANDLE hObjectToWaitOn,
    DWORD dwMilliseconds,
    BOOL bAlertable);

HANDLE WINAPI CreateEventW(
    LPSECURITY_ATTRIBUTES lpEventAttributes,
    BOOL bManualReset,
    BOOL bInitialState,
    LPCWSTR lpName);

HANDLE WINAPI CreateEventExW(
    LPSECURITY_ATTRIBUTES lpEventAttributes,
    LPCWSTR lpName,
    DWORD dwFlags,
    DWORD dwDesiredAccess);

HANDLE WINAPI OpenEventW(
    DWORD dwDesiredAccess,
    BOOL bInheritHandle,
    LPCWSTR lpName);

BOOL WINAPI SetEvent(
    HANDLE hEvent);

BOOL WINAPI ResetEvent(
    HANDLE hEvent);

BOOL WINAPI PulseEvent(
    HANDLE hEvent);

HANDLE WINAPI CreateMutexW(
    LPSECURITY_ATTRIBUTES lpMutexAttributes,
    BOOL bInitialOwner,
    LPCWSTR lpName);

HANDLE WINAPI CreateMutexExW(
    LPSECURITY_ATTRIBUTES lpMutexAttributes,
    LPCWSTR lpName,
    DWORD dwFlags,
    DWORD dwDesiredAccess);

HANDLE WINAPI OpenMutexW(
    DWORD dwDesiredAccess,
    BOOL bInheritHandle,
    LPCWSTR lpName);

BOOL WINAPI ReleaseMutex(
    HANDLE hMutex);

HANDLE WINAPI CreateSemaphoreW(
    LPSECURITY_ATTRIBUTES lpSemaphoreAttributes,
    LONG lInitialCount,
    LONG lMaximumCount,
    LPCWSTR lpName);

HANDLE WINAPI CreateSemaphoreExW(
    LPSECURITY_ATTRIBUTES lpSemaphoreAttributes,
    LONG lInitialCount,
    LONG lMaximumCount,
    LPCWSTR lpName,
    DWORD dwFlags,
    DWORD dwDesiredAccess);

HANDLE WINAPI OpenSemaphoreW(
    DWORD dwDesiredAccess,
    BOOL bInheritHandle,
    LPCWSTR lpName);

BOOL WINAPI ReleaseSemaphore(
    HANDLE hSemaphore,
    LONG lReleaseCount,
    LPLONG lpPreviousCount);

BOOL WINAPI CloseHandle(
    HANDLE hObject);

BOOL WINAPI QueryProcessCycleTime(
    HANDLE hProcess,
    uint64_t *CycleTime);

/* Win32 Utility, Thread Description, Handle Identity & Virtual Memory APIs (utility.c) */
void WINAPI GetSystemTimePreciseAsFileTime(
    LPFILETIME lpSystemTimeAsFileTime);

HRESULT WINAPI SetThreadDescription(
    HANDLE hThread,
    PCWSTR lpThreadDescription);

HRESULT WINAPI GetThreadDescription(
    HANDLE hThread,
    PWSTR *ppszThreadDescription);

BOOL WINAPI CompareObjectHandles(
    HANDLE hFirstObjectHandle,
    HANDLE hSecondObjectHandle);

BOOL WINAPI DuplicateHandle(
    HANDLE hSourceProcessHandle,
    HANDLE hSourceHandle,
    HANDLE hTargetProcessHandle,
    HANDLE *lpTargetHandle,
    DWORD dwDesiredAccess,
    BOOL bInheritHandle,
    DWORD dwOptions);

HANDLE WINAPI GetCurrentProcess(void);
HANDLE WINAPI GetCurrentThread(void);
HLOCAL WINAPI LocalFree(HLOCAL hMem);

PVOID WINAPI VirtualAlloc2(
    HANDLE Process,
    PVOID BaseAddress,
    SIZE_T Size,
    ULONG AllocationType,
    ULONG PageProtection,
    MEM_EXTENDED_PARAMETER *ExtendedParameters,
    ULONG ParameterCount);

PVOID WINAPI VirtualAlloc2FromApp(
    HANDLE Process,
    PVOID BaseAddress,
    SIZE_T Size,
    ULONG AllocationType,
    ULONG PageProtection,
    MEM_EXTENDED_PARAMETER *ExtendedParameters,
    ULONG ParameterCount);

BOOL WINAPI VirtualFree(
    PVOID lpAddress,
    SIZE_T dwSize,
    DWORD dwFreeType);

BOOL WINAPI VirtualFreeEx(
    HANDLE hProcess,
    PVOID lpAddress,
    SIZE_T dwSize,
    DWORD dwFreeType);

SIZE_T WINAPI VirtualQuery(
    LPCVOID lpAddress,
    PMEMORY_BASIC_INFORMATION lpBuffer,
    SIZE_T dwLength);

SIZE_T WINAPI VirtualQueryEx(
    HANDLE hProcess,
    LPCVOID lpAddress,
    PMEMORY_BASIC_INFORMATION lpBuffer,
    SIZE_T dwLength);

HANDLE WINAPI CreateFileMappingW(
    HANDLE hFile,
    LPSECURITY_ATTRIBUTES lpFileMappingAttributes,
    DWORD flProtect,
    DWORD dwMaximumSizeHigh,
    DWORD dwMaximumSizeLow,
    LPCWSTR lpName);

PVOID WINAPI MapViewOfFile3(
    HANDLE FileMapping,
    HANDLE Process,
    PVOID BaseAddress,
    ULONG64 Offset,
    SIZE_T ViewSize,
    ULONG AllocationType,
    ULONG PageProtection,
    MEM_EXTENDED_PARAMETER *ExtendedParameters,
    ULONG ParameterCount);

PVOID WINAPI MapViewOfFile3FromApp(
    HANDLE FileMapping,
    HANDLE Process,
    PVOID BaseAddress,
    ULONG64 Offset,
    SIZE_T ViewSize,
    ULONG AllocationType,
    ULONG PageProtection,
    MEM_EXTENDED_PARAMETER *ExtendedParameters,
    ULONG ParameterCount);

BOOL WINAPI UnmapViewOfFile(
    LPCVOID lpBaseAddress);

BOOL WINAPI UnmapViewOfFile2(
    HANDLE Process,
    PVOID BaseAddress,
    ULONG UnmapFlags);

typedef enum _KB_SYNC_OBJ_KIND {
    KB_SYNC_OBJ_NONE = 0,
    KB_SYNC_OBJ_EVENT = 1,
    KB_SYNC_OBJ_MUTEX = 2,
    KB_SYNC_OBJ_SEMAPHORE = 3
} KB_SYNC_OBJ_KIND;

void kb_register_sync_handle(HANDLE h, KB_SYNC_OBJ_KIND kind, LPCWSTR name);
int kb_close_registered_handle(HANDLE h, BOOL *out_result);
void WINAPI KernelBaseResetUtilityState(void);

#endif /* _WINE_KERNELBASE_H */

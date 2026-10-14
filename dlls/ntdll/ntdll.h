/*
 * Wine NTDLL Public & Internal Definitions (Apple Silicon arm64 & Intel x86_64/i386 Execution Parity)
 *
 * Copyright 2026 Wine Parity Contributors
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef _WINE_NTDLL_H
#define _WINE_NTDLL_H

#include <stddef.h>
#include <stdint.h>

#include "../../tests/include/parity_test.h"

#ifndef _WIN32
typedef uint8_t UCHAR;
#ifndef _WINE_TEST_H
typedef uint8_t BOOLEAN;
typedef uint64_t ULONGLONG;
#endif
typedef uint16_t USHORT;
typedef uint16_t WORD;
typedef int64_t LONGLONG;
typedef size_t SIZE_T;
typedef uintptr_t ULONG_PTR;
typedef intptr_t LONG_PTR;
typedef ULONG *PULONG;
typedef void *PVOID;
typedef const void *LPCVOID;
#else
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#ifndef NTAPI
#define NTAPI WINAPI
#endif

/* NTSTATUS codes */
#undef STATUS_INVALID_HANDLE
#define STATUS_INVALID_HANDLE             ((NTSTATUS)0xC0000008)
#undef STATUS_INVALID_PARAMETER
#define STATUS_INVALID_PARAMETER          ((NTSTATUS)0xC000000D)
#undef STATUS_GUARD_PAGE_VIOLATION
#define STATUS_GUARD_PAGE_VIOLATION       ((NTSTATUS)0x80000001)
#undef STATUS_ACCESS_VIOLATION
#define STATUS_ACCESS_VIOLATION           ((NTSTATUS)0xC0000005)

#ifndef STATUS_DATATYPE_MISALIGNMENT
#define STATUS_DATATYPE_MISALIGNMENT      ((NTSTATUS)0x80000002)
#endif
#ifndef STATUS_SINGLE_STEP
#define STATUS_SINGLE_STEP                ((NTSTATUS)0x80000004)
#endif
#ifndef STATUS_WX86_SINGLE_STEP
#define STATUS_WX86_SINGLE_STEP           ((NTSTATUS)0x4000001E)
#endif
#ifndef STATUS_UNSUCCESSFUL
#define STATUS_UNSUCCESSFUL               ((NTSTATUS)0xC0000001)
#endif
#ifndef STATUS_INVALID_INFO_CLASS
#define STATUS_INVALID_INFO_CLASS         ((NTSTATUS)0xC0000003)
#endif
#ifndef STATUS_INFO_LENGTH_MISMATCH
#define STATUS_INFO_LENGTH_MISMATCH       ((NTSTATUS)0xC0000004)
#endif
#ifndef STATUS_IN_PAGE_ERROR
#define STATUS_IN_PAGE_ERROR              ((NTSTATUS)0xC0000006)
#endif
#ifndef STATUS_NO_MEMORY
#define STATUS_NO_MEMORY                  ((NTSTATUS)0xC0000017)
#endif
#ifndef STATUS_CONFLICTING_ADDRESSES
#define STATUS_CONFLICTING_ADDRESSES      ((NTSTATUS)0xC0000018)
#endif
#ifndef STATUS_UNABLE_TO_FREE_VM
#define STATUS_UNABLE_TO_FREE_VM          ((NTSTATUS)0xC000001A)
#endif
#ifndef STATUS_ALREADY_COMMITTED
#define STATUS_ALREADY_COMMITTED          ((NTSTATUS)0xC0000021)
#endif
#ifndef STATUS_ACCESS_DENIED
#define STATUS_ACCESS_DENIED              ((NTSTATUS)0xC0000022)
#endif
#ifndef STATUS_NOT_COMMITTED
#define STATUS_NOT_COMMITTED              ((NTSTATUS)0xC000002D)
#endif
#ifndef STATUS_INVALID_PAGE_PROTECTION
#define STATUS_INVALID_PAGE_PROTECTION    ((NTSTATUS)0xC0000045)
#endif
#ifndef STATUS_FREE_VM_NOT_AT_BASE
#define STATUS_FREE_VM_NOT_AT_BASE        ((NTSTATUS)0xC000009F)
#endif
#ifndef STATUS_MEMORY_NOT_ALLOCATED
#define STATUS_MEMORY_NOT_ALLOCATED       ((NTSTATUS)0xC00000A0)
#endif
#ifndef STATUS_NOT_SUPPORTED
#define STATUS_NOT_SUPPORTED              ((NTSTATUS)0xC00000BB)
#endif
#ifndef STATUS_INVALID_PARAMETER_1
#define STATUS_INVALID_PARAMETER_1        ((NTSTATUS)0xC00000EF)
#endif
#ifndef STATUS_INVALID_PARAMETER_2
#define STATUS_INVALID_PARAMETER_2        ((NTSTATUS)0xC00000F0)
#endif
#ifndef STATUS_INVALID_PARAMETER_3
#define STATUS_INVALID_PARAMETER_3        ((NTSTATUS)0xC00000F1)
#endif
#ifndef STATUS_INVALID_PARAMETER_4
#define STATUS_INVALID_PARAMETER_4        ((NTSTATUS)0xC00000F2)
#endif
#ifndef STATUS_INVALID_PARAMETER_5
#define STATUS_INVALID_PARAMETER_5        ((NTSTATUS)0xC00000F3)
#endif
#ifndef STATUS_INVALID_PARAMETER_6
#define STATUS_INVALID_PARAMETER_6        ((NTSTATUS)0xC00000F4)
#endif
#ifndef STATUS_INVALID_PARAMETER_7
#define STATUS_INVALID_PARAMETER_7        ((NTSTATUS)0xC00000F5)
#endif
#ifndef STATUS_NO_LDT
#define STATUS_NO_LDT                     ((NTSTATUS)0xC0000117)
#endif
#ifndef STATUS_INVALID_LDT_SIZE
#define STATUS_INVALID_LDT_SIZE           ((NTSTATUS)0xC0000118)
#endif
#ifndef STATUS_INVALID_LDT_OFFSET
#define STATUS_INVALID_LDT_OFFSET         ((NTSTATUS)0xC0000119)
#endif
#ifndef STATUS_INVALID_LDT_DESCRIPTOR
#define STATUS_INVALID_LDT_DESCRIPTOR     ((NTSTATUS)0xC000011A)
#endif
#ifndef STATUS_EXECUTABLE_MEMORY_WRITE
#define STATUS_EXECUTABLE_MEMORY_WRITE    ((NTSTATUS)0xC0000602)
#endif

#define NtCurrentProcess() ((HANDLE)(intptr_t)-1)
#define NtCurrentThread()  ((HANDLE)(intptr_t)-2)

/* Page & Allocation Granularity Constants */
#define NTDLL_WIN_PAGE_SIZE                  0x1000U
#define NTDLL_WIN_PAGE_MASK                  0x0FFFU
#define NTDLL_WIN_PAGE_SHIFT                 12U
#define NTDLL_APPLE_SILICON_HOST_PAGE_SIZE   0x4000U
#define NTDLL_APPLE_SILICON_HOST_PAGE_MASK   0x3FFFU
#define NTDLL_SUBPAGES_PER_APPLE_HOST_PAGE   4U
#define NTDLL_ALLOCATION_GRANULARITY         0x10000U
#define NTDLL_ALLOCATION_GRANULARITY_MASK    0xFFFFU

/* Win32 / NT Virtual Memory Flags */
#ifndef MEM_COMMIT
#define MEM_COMMIT        0x00001000U
#define MEM_RESERVE       0x00002000U
#define MEM_DECOMMIT      0x00004000U
#define MEM_RELEASE       0x00008000U
#define MEM_FREE          0x00010000U
#define MEM_PRIVATE       0x00020000U
#define MEM_MAPPED        0x00040000U
#define MEM_RESET         0x00080000U
#define MEM_TOP_DOWN      0x00100000U
#define MEM_WRITE_WATCH   0x00200000U
#define MEM_PHYSICAL      0x00400000U
#define MEM_IMAGE         0x01000000U
#define MEM_LARGE_PAGES   0x20000000U
#endif

#ifndef PAGE_NOACCESS
#define PAGE_NOACCESS          0x01U
#define PAGE_READONLY          0x02U
#define PAGE_READWRITE         0x04U
#define PAGE_WRITECOPY         0x08U
#define PAGE_EXECUTE           0x10U
#define PAGE_EXECUTE_READ      0x20U
#define PAGE_EXECUTE_READWRITE 0x40U
#define PAGE_EXECUTE_WRITECOPY 0x80U
#define PAGE_GUARD             0x100U
#define PAGE_NOCACHE           0x200U
#define PAGE_WRITECOMBINE      0x400U
#endif

#ifndef WRITE_WATCH_FLAG_RESET
#define WRITE_WATCH_FLAG_RESET 0x01U
#endif

/* Per-4KB-page internal virtual protection bitmask */
#define VPROT_READ       0x01U
#define VPROT_WRITE      0x02U
#define VPROT_EXEC       0x04U
#define VPROT_WRITECOPY  0x08U
#define VPROT_GUARD      0x10U
#define VPROT_COMMITTED  0x20U
#define VPROT_WRITEWATCH 0x40U
#define VPROT_SYSTEM     0x80U

/* Fault exception types passed to NtdllHandleVirtualFault / virtual_handle_fault */
#define NTDLL_FAULT_READ    0U
#define NTDLL_FAULT_WRITE   1U
#define NTDLL_FAULT_EXECUTE 8U

/* MemoryInformationClass & VirtualMemoryInformationClass */
#define MemoryBasicInformation      0U
#define VmPrefetchInformation       0U
#define VmPagePriorityInformation   1U
#define VmCfgCallTargetInformation  2U
#define VmPageDirtyStateInformation 3U

/* Process / Thread ManageWritesToExecutableMemory classes */
#define ProcessManageWritesToExecutableMemory 89U
#define ThreadManageWritesToExecutableMemory  48U

/* ThreadInformationClass */
#define ThreadDescriptorTableEntry 6U
#define ThreadWow64Context         29U

/* SystemInformationClass */
#define SystemCpuInformation                       1U
#define SystemSupportedProcessorArchitectures      181U
#define SystemSupportedProcessorArchitectures2     230U

/* Machine Architecture Constants */
#ifndef IMAGE_FILE_MACHINE_UNKNOWN
#define IMAGE_FILE_MACHINE_UNKNOWN 0x0000U
#endif
#ifndef IMAGE_FILE_MACHINE_I386
#define IMAGE_FILE_MACHINE_I386    0x014cU
#endif
#ifndef IMAGE_FILE_MACHINE_ARMNT
#define IMAGE_FILE_MACHINE_ARMNT   0x01c4U
#endif
#ifndef IMAGE_FILE_MACHINE_AMD64
#define IMAGE_FILE_MACHINE_AMD64   0x8664U
#endif
#ifndef IMAGE_FILE_MACHINE_ARM64
#define IMAGE_FILE_MACHINE_ARM64   0xaa64U
#endif
#ifndef IMAGE_FILE_MACHINE_ARM64EC
#define IMAGE_FILE_MACHINE_ARM64EC 0xa641U
#endif

/* ARM64 Context Flags */
#define NTDLL_CONTEXT_ARM64                 0x00400000U
#define NTDLL_CONTEXT_ARM64_CONTROL         (NTDLL_CONTEXT_ARM64 | 0x00000001U)
#define NTDLL_CONTEXT_ARM64_INTEGER         (NTDLL_CONTEXT_ARM64 | 0x00000002U)
#define NTDLL_CONTEXT_ARM64_FLOATING_POINT  (NTDLL_CONTEXT_ARM64 | 0x00000004U)
#define NTDLL_CONTEXT_ARM64_DEBUG_REGISTERS (NTDLL_CONTEXT_ARM64 | 0x00000008U)
#define NTDLL_CONTEXT_ARM64_X18             (NTDLL_CONTEXT_ARM64 | 0x00000010U)
#define NTDLL_CONTEXT_ARM64_FULL            (NTDLL_CONTEXT_ARM64_CONTROL | NTDLL_CONTEXT_ARM64_INTEGER | NTDLL_CONTEXT_ARM64_FLOATING_POINT)

/* Internal restore flags for signal_arm64.c */
#define NTDLL_RESTORE_FLAGS_INCOMPLETE_FRAME_CONTEXT 0x01U
#define NTDLL_RESTORE_FLAGS_EMULATION                0x02U
#define NTDLL_RESTORE_FLAGS_DARWIN_X18_SAVED         0x04U

typedef struct _NTDLL_MEMORY_BASIC_INFORMATION {
    PVOID  BaseAddress;
    PVOID  AllocationBase;
    ULONG  AllocationProtect;
    SIZE_T RegionSize;
    ULONG  State;
    ULONG  Protect;
    ULONG  Type;
} NTDLL_MEMORY_BASIC_INFORMATION, *PNTDLL_MEMORY_BASIC_INFORMATION;

typedef struct _NTDLL_MEMORY_RANGE_ENTRY {
    PVOID  VirtualAddress;
    SIZE_T NumberOfBytes;
} NTDLL_MEMORY_RANGE_ENTRY, *PNTDLL_MEMORY_RANGE_ENTRY;

typedef struct _NTDLL_PROCESS_EXEC_WRITE_STATES {
    ULONG Version : 8;
    ULONG EnableWriteExceptions : 1;
    ULONG ThreadAllowWrites : 1;
    ULONG Spare : 22;
    ULONG KernelWriteToExecutableSignal;
} NTDLL_PROCESS_EXEC_WRITE_STATES;

typedef struct _NTDLL_THREAD_EXEC_WRITE_STATES {
    ULONG Version : 8;
    ULONG ThreadAllowWrites : 1;
    ULONG Spare : 23;
} NTDLL_THREAD_EXEC_WRITE_STATES;

typedef struct _NTDLL_JIT_WX_STATE {
    ULONG  host_page_size;
    ULONG  win_page_size;
    ULONG  map_jit_enabled;
    ULONG  wx_write_enabled;
    ULONG  wx_toggle_count;
    ULONG  icache_flush_count;
    ULONG  rosetta_read_fault_promoted_count;
} NTDLL_JIT_WX_STATE;

/* ARM64 Context & Darwin x18 State */
typedef struct _NTDLL_ARM64_NT_NEON128 {
    ULONGLONG Low;
    LONGLONG  High;
} NTDLL_ARM64_NT_NEON128;

typedef struct _NTDLL_ARM64_NT_CONTEXT {
    ULONG                  ContextFlags;
    ULONG                  Cpsr;
    ULONGLONG              X[31]; /* X0..X28, X29 (Fp), X30 (Lr) */
    ULONGLONG              Sp;
    ULONGLONG              Pc;
    NTDLL_ARM64_NT_NEON128 V[32];
    ULONG                  Fpcr;
    ULONG                  Fpsr;
    ULONG                  Bcr[8];
    ULONGLONG              Bvr[8];
    ULONG                  Wcr[2];
    ULONGLONG              Wvr[2];
    ULONG                  RestoreFlags;
} NTDLL_ARM64_NT_CONTEXT, *PNTDLL_ARM64_NT_CONTEXT;

typedef struct _NTDLL_DARWIN_X18_STATE {
    ULONGLONG darwin_platform_x18;
    ULONGLONG windows_teb_x18;
    ULONGLONG active_x18;
    ULONG     in_pe_mode;
    ULONG     pe_enter_count;
    ULONG     unix_reenter_count;
    ULONG     signal_handler_save_count;
    ULONG     restore_flags;
} NTDLL_DARWIN_X18_STATE;

typedef struct _WOW64_CPURESERVED {
    USHORT Flags;
    USHORT Machine;
} WOW64_CPURESERVED, *PWOW64_CPURESERVED;

typedef struct _WOW64_CPU_AREA_INFO {
    void *Context;
    void *ContextEx;
    void *ContextFlagsLocation;
    union {
        WOW64_CPURESERVED *CpuReserved;
        ULONG_PTR         *CpuArea;
    };
    union {
        ULONG ContextFlag;
        ULONG Reserved;
    };
    USHORT Machine;
} WOW64_CPU_AREA_INFO, *PWOW64_CPU_AREA_INFO;

typedef struct _NTDLL_CHPE_V2_CPU_AREA_INFO {
    ULONG                  InSimulation;
    ULONG                  SuspendDoorbell;
    ULONGLONG              EmulatorStackBase;
    ULONGLONG              EmulatorStackLimit;
    NTDLL_ARM64_NT_CONTEXT ContextAmd64;
    ULONG                  SuspendTriggeredCount;
} NTDLL_CHPE_V2_CPU_AREA_INFO;

typedef struct _CROSS_PROCESS_WORK_ENTRY {
    ULONG     next;
    ULONG     id;
    ULONGLONG addr;
    ULONGLONG size;
    ULONG     args[4];
} CROSS_PROCESS_WORK_ENTRY;

typedef struct _CROSS_PROCESS_WORK_HDR {
    ULONG first;
    ULONG counter;
} CROSS_PROCESS_WORK_HDR;

#define CROSS_PROCESS_LIST_FLUSH 0x80000000U
#define CROSS_PROCESS_LIST_ENTRY(list, pos) \
    ((CROSS_PROCESS_WORK_ENTRY *)((char *)(list) + ((pos) & ~CROSS_PROCESS_LIST_FLUSH)))

typedef struct _NTDLL_SYSTEM_CPU_INFORMATION {
    USHORT ProcessorArchitecture;
    USHORT ProcessorLevel;
    USHORT ProcessorRevision;
    USHORT MaximumProcessors;
    ULONG  ProcessorFeatureBits;
} NTDLL_SYSTEM_CPU_INFORMATION;

#ifndef _WIN32
typedef struct _SYSTEM_SUPPORTED_PROCESSOR_ARCHITECTURES_INFORMATION {
    DWORD Machine : 16;
    DWORD KernelMode : 1;
    DWORD UserMode : 1;
    DWORD Native : 1;
    DWORD Process : 1;
    DWORD WoW64Container : 1;
    DWORD ReservedZero0 : 11;
} SYSTEM_SUPPORTED_PROCESSOR_ARCHITECTURES_INFORMATION;
#endif

typedef struct _NTDLL_ARM64EC_EMULATOR_INFO {
    char   module_name[32];
    USHORT native_machine;
    USHORT guest_machine;
    ULONG  initialized;
    ULONG  jit_icache_flushes;
    ULONG  heavy_flush_requests;
    ULONG  processor_feature_mask;
} NTDLL_ARM64EC_EMULATOR_INFO;

/* x86_64 / i386 LDT, WoW64 Selector, and Rosetta 2 GSBASE structures */
typedef struct _NTDLL_LDT_ENTRY {
    USHORT LimitLow;
    USHORT BaseLow;
    union {
        struct {
            BYTE BaseMid;
            BYTE Flags1;
            BYTE Flags2;
            BYTE BaseHi;
        } Bytes;
        struct {
            ULONG BaseMid : 8;
            ULONG Type : 5;
            ULONG Dpl : 2;
            ULONG Pres : 1;
            ULONG LimitHi : 4;
            ULONG Sys : 1;
            ULONG Reserved_0 : 1;
            ULONG Default_Big : 1;
            ULONG Granularity : 1;
            ULONG BaseHi : 8;
        } Bits;
    } HighWord;
} NTDLL_LDT_ENTRY, *PNTDLL_LDT_ENTRY;

typedef struct _NTDLL_DESCRIPTOR_TABLE_ENTRY {
    ULONG           Selector;
    NTDLL_LDT_ENTRY Descriptor;
} NTDLL_DESCRIPTOR_TABLE_ENTRY, *PNTDLL_DESCRIPTOR_TABLE_ENTRY;

typedef struct _NTDLL_ROSETTA_GSBASE_STATE {
    ULONGLONG teb64_base;
    ULONGLONG teb32_base;
    ULONGLONG current_gsbase;
    ULONG     rosetta_active;
    ULONG     tsd_set_count;
    ULONG     gs_prefix_fault_fixup_count;
} NTDLL_ROSETTA_GSBASE_STATE;

/* Virtual Memory APIs (dlls/ntdll/virtual.c) */
NTSTATUS WINAPI NtAllocateVirtualMemory(
    HANDLE ProcessHandle,
    PVOID *BaseAddress,
    ULONG_PTR ZeroBits,
    SIZE_T *RegionSize,
    ULONG AllocationType,
    ULONG Protect);

NTSTATUS WINAPI NtFreeVirtualMemory(
    HANDLE ProcessHandle,
    PVOID *BaseAddress,
    SIZE_T *RegionSize,
    ULONG FreeType);

NTSTATUS WINAPI NtProtectVirtualMemory(
    HANDLE ProcessHandle,
    PVOID *BaseAddress,
    SIZE_T *RegionSize,
    ULONG NewProtect,
    ULONG *OldProtect);

NTSTATUS WINAPI NtQueryVirtualMemory(
    HANDLE ProcessHandle,
    LPCVOID BaseAddress,
    ULONG MemoryInformationClass,
    PVOID MemoryInformation,
    SIZE_T MemoryInformationLength,
    SIZE_T *ReturnLength);

NTSTATUS WINAPI NtGetWriteWatch(
    HANDLE ProcessHandle,
    ULONG Flags,
    PVOID BaseAddress,
    SIZE_T RegionSize,
    PVOID *UserAddressArray,
    ULONG_PTR *EntriesInUserAddressArray,
    PULONG Granularity);

NTSTATUS WINAPI NtResetWriteWatch(
    HANDLE ProcessHandle,
    PVOID BaseAddress,
    SIZE_T RegionSize);

NTSTATUS WINAPI NtSetInformationVirtualMemory(
    HANDLE ProcessHandle,
    ULONG VirtualMemoryInformationClass,
    ULONG_PTR NumberOfEntries,
    PNTDLL_MEMORY_RANGE_ENTRY VirtualAddresses,
    PVOID VirtualMemoryInformation,
    ULONG VirtualMemoryInformationLength);

NTSTATUS WINAPI NtSetInformationProcess(
    HANDLE ProcessHandle,
    ULONG ProcessInformationClass,
    PVOID ProcessInformation,
    ULONG ProcessInformationLength);

NTSTATUS WINAPI NtSetInformationThread(
    HANDLE ThreadHandle,
    ULONG ThreadInformationClass,
    PVOID ThreadInformation,
    ULONG ThreadInformationLength);

NTSTATUS WINAPI NtFlushInstructionCache(
    HANDLE ProcessHandle,
    LPCVOID BaseAddress,
    SIZE_T NumberOfBytesToFlush);

NTSTATUS WINAPI NtdllHandleVirtualFault(
    PVOID FaultAddress,
    ULONG FaultType,
    ULONG IsRosetta2Emulation);

NTSTATUS WINAPI NtdllSetSimulatedHostPageSize(ULONG HostPageSize);
NTSTATUS WINAPI NtdllQueryJitWxState(NTDLL_JIT_WX_STATE *State);
NTSTATUS WINAPI NtdllToggleJitWriteProtection(ULONG WriteEnabled);
void WINAPI NtdllResetVirtualState(void);

/* ARM64 & ARM64EC / WoW64 APIs (dlls/ntdll/signal_arm64.c) */
NTSTATUS WINAPI NtGetContextThread(
    HANDLE ThreadHandle,
    PNTDLL_ARM64_NT_CONTEXT Context);

NTSTATUS WINAPI NtSetContextThread(
    HANDLE ThreadHandle,
    const NTDLL_ARM64_NT_CONTEXT *Context);

NTSTATUS WINAPI RtlWow64GetCpuAreaInfo(
    PVOID CpuArea,
    ULONG Reserved,
    WOW64_CPU_AREA_INFO *Info);

USHORT WINAPI RtlWow64GetCurrentMachine(void);

NTSTATUS WINAPI RtlWow64GetProcessMachines(
    HANDLE ProcessHandle,
    USHORT *CurrentMachine,
    USHORT *TargetMachine);

NTSTATUS WINAPI RtlWow64IsWowGuestMachineSupported(
    USHORT WowGuestMachine,
    BOOLEAN *MachineIsSupported);

NTSTATUS WINAPI RtlGetNativeSystemInformation(
    ULONG SystemInformationClass,
    PVOID SystemInformation,
    ULONG SystemInformationLength,
    PULONG ReturnLength);

NTSTATUS WINAPI NtQuerySystemInformationEx(
    ULONG SystemInformationClass,
    PVOID InputBuffer,
    ULONG InputBufferLength,
    PVOID SystemInformation,
    ULONG SystemInformationLength,
    PULONG ReturnLength);

BOOLEAN WINAPI RtlWow64PushCrossProcessWorkOntoFreeList(
    CROSS_PROCESS_WORK_HDR *FreeList,
    CROSS_PROCESS_WORK_ENTRY *Entry);

CROSS_PROCESS_WORK_ENTRY * WINAPI RtlWow64PopCrossProcessWorkFromFreeList(
    CROSS_PROCESS_WORK_HDR *FreeList);

BOOLEAN WINAPI RtlWow64PushCrossProcessWorkOntoWorkList(
    CROSS_PROCESS_WORK_HDR *WorkList,
    CROSS_PROCESS_WORK_ENTRY *Entry,
    void **FlushEntry);

CROSS_PROCESS_WORK_ENTRY * WINAPI RtlWow64PopAllCrossProcessWorkFromWorkList(
    CROSS_PROCESS_WORK_HDR *WorkList,
    BOOLEAN *Flush);

BOOLEAN WINAPI RtlWow64RequestCrossProcessHeavyFlush(
    CROSS_PROCESS_WORK_HDR *WorkList);

NTSTATUS WINAPI NtdllDarwinX18Init(ULONGLONG DarwinPlatformX18, ULONGLONG WindowsTebX18);
NTSTATUS WINAPI NtdllDarwinX18EnterPe(void);
NTSTATUS WINAPI NtdllDarwinX18ExitToUnix(void);
NTSTATUS WINAPI NtdllDarwinX18SignalEnter(ULONGLONG FaultedX18);
NTSTATUS WINAPI NtdllDarwinX18SignalLeave(void);
NTSTATUS WINAPI NtdllDarwinX18QueryState(NTDLL_DARWIN_X18_STATE *State);

NTSTATUS WINAPI NtdllArm64EcSelectEmulator(const char *ModuleName);
NTSTATUS WINAPI NtdllArm64EcQueryEmulatorInfo(NTDLL_ARM64EC_EMULATOR_INFO *Info);
BOOLEAN WINAPI BTCpu64IsProcessorFeaturePresent(ULONG Feature);
void WINAPI UpdateProcessorInformation(NTDLL_SYSTEM_CPU_INFORMATION *Info);
void WINAPI BTCpu64FlushInstructionCache(LPCVOID BaseAddress, SIZE_T Size);
NTSTATUS WINAPI NtdllArm64EcHandleSuspendDoorbell(
    NTDLL_CHPE_V2_CPU_AREA_INFO *ChpeInfo,
    NTDLL_ARM64_NT_CONTEXT *Arm64Context);

/* x86_64 / i386 LDT, Selector & Rosetta 2 GSBASE APIs (dlls/ntdll/signal_x86_64.c) */
NTSTATUS WINAPI NtSetLdtEntries(
    ULONG Selector1,
    NTDLL_LDT_ENTRY Entry1,
    ULONG Selector2,
    NTDLL_LDT_ENTRY Entry2);

NTSTATUS WINAPI RtlWow64GetThreadSelectorEntry(
    HANDLE ThreadHandle,
    PNTDLL_DESCRIPTOR_TABLE_ENTRY SelectorEntry,
    ULONG Size,
    PULONG ReturnLength);

NTSTATUS WINAPI NtQueryInformationThread(
    HANDLE ThreadHandle,
    ULONG ThreadInformationClass,
    PVOID ThreadInformation,
    ULONG ThreadInformationLength,
    PULONG ReturnLength);

void WINAPI NtdllResetLdtState(ULONGLONG Teb64Base);
NTSTATUS WINAPI NtdllRosettaInitGsbase(ULONGLONG Teb64Base, ULONG RosettaActive);
NTSTATUS WINAPI NtdllRosettaCorruptGsbase(ULONGLONG CorruptedGsbase);
NTSTATUS WINAPI NtdllRosettaCheckInvalidGsbase(
    ULONG_PTR FaultRip,
    const BYTE *InstrBytes,
    SIZE_T InstrLen,
    NTDLL_ROSETTA_GSBASE_STATE *OutState);

#endif /* _WINE_NTDLL_H */

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
#ifndef STATUS_END_OF_FILE
#define STATUS_END_OF_FILE                ((NTSTATUS)0xC0000011)
#endif
#ifndef STATUS_CRC_ERROR
#define STATUS_CRC_ERROR                  ((NTSTATUS)0xC000003F)
#endif
#ifndef STATUS_INVALID_IMAGE_FORMAT
#define STATUS_INVALID_IMAGE_FORMAT       ((NTSTATUS)0xC000007B)
#endif

#undef STATUS_WAIT_0
#define STATUS_WAIT_0                     ((NTSTATUS)0x00000000)
#undef STATUS_ABANDONED_WAIT_0
#define STATUS_ABANDONED_WAIT_0           ((NTSTATUS)0x00000080)
#undef STATUS_USER_APC
#define STATUS_USER_APC                   ((NTSTATUS)0x000000C0)
#undef STATUS_KERNEL_APC
#define STATUS_KERNEL_APC                 ((NTSTATUS)0x00000100)
#undef STATUS_ALERTED
#define STATUS_ALERTED                    ((NTSTATUS)0x00000101)
#undef STATUS_TIMEOUT
#define STATUS_TIMEOUT                    ((NTSTATUS)0x00000102)
#undef STATUS_PENDING
#define STATUS_PENDING                    ((NTSTATUS)0x00000103)
#undef STATUS_OBJECT_NAME_EXISTS
#define STATUS_OBJECT_NAME_EXISTS         ((NTSTATUS)0x40000000)
#undef STATUS_INVALID_CID
#define STATUS_INVALID_CID                ((NTSTATUS)0xC000000B)
#undef STATUS_OBJECT_TYPE_MISMATCH
#define STATUS_OBJECT_TYPE_MISMATCH       ((NTSTATUS)0xC0000024)
#undef STATUS_OBJECT_NAME_INVALID
#define STATUS_OBJECT_NAME_INVALID        ((NTSTATUS)0xC0000033)
#undef STATUS_OBJECT_NAME_NOT_FOUND
#define STATUS_OBJECT_NAME_NOT_FOUND      ((NTSTATUS)0xC0000034)
#undef STATUS_OBJECT_NAME_COLLISION
#define STATUS_OBJECT_NAME_COLLISION      ((NTSTATUS)0xC0000035)
#undef STATUS_OBJECT_PATH_INVALID
#define STATUS_OBJECT_PATH_INVALID        ((NTSTATUS)0xC0000039)
#undef STATUS_OBJECT_PATH_NOT_FOUND
#define STATUS_OBJECT_PATH_NOT_FOUND      ((NTSTATUS)0xC000003A)
#undef STATUS_MUTANT_NOT_OWNED
#define STATUS_MUTANT_NOT_OWNED           ((NTSTATUS)0xC0000046)
#undef STATUS_SEMAPHORE_LIMIT_EXCEEDED
#define STATUS_SEMAPHORE_LIMIT_EXCEEDED   ((NTSTATUS)0xC0000047)
#undef STATUS_POSSIBLE_DEADLOCK
#define STATUS_POSSIBLE_DEADLOCK          ((NTSTATUS)0xC0000194)
#undef STATUS_RESOURCE_NOT_OWNED
#define STATUS_RESOURCE_NOT_OWNED         ((NTSTATUS)0xC0000264)

#ifndef _WIN32
typedef LONG *PLONG;
typedef LONG *LPLONG;
typedef BOOL *PBOOL;
typedef BOOL *LPBOOL;
typedef DWORD ACCESS_MASK;
typedef ACCESS_MASK *PACCESS_MASK;
typedef WCHAR *PWSTR;
typedef WCHAR *LPWSTR;

typedef union _LARGE_INTEGER {
    struct {
        DWORD LowPart;
        LONG  HighPart;
    } u;
    struct {
        DWORD LowPart;
        LONG  HighPart;
    };
    LONGLONG QuadPart;
} LARGE_INTEGER, *PLARGE_INTEGER;

typedef struct _SECURITY_ATTRIBUTES {
    DWORD  nLength;
    LPVOID lpSecurityDescriptor;
    BOOL   bInheritHandle;
} SECURITY_ATTRIBUTES, *PSECURITY_ATTRIBUTES, *LPSECURITY_ATTRIBUTES;

typedef struct _RTL_SRWLOCK {
    PVOID Ptr;
} RTL_SRWLOCK, *PRTL_SRWLOCK, SRWLOCK, *PSRWLOCK;

#define RTL_SRWLOCK_INIT {0}
#define SRWLOCK_INIT RTL_SRWLOCK_INIT

typedef struct _RTL_CONDITION_VARIABLE {
    PVOID Ptr;
} RTL_CONDITION_VARIABLE, *PRTL_CONDITION_VARIABLE, CONDITION_VARIABLE, *PCONDITION_VARIABLE;

#define RTL_CONDITION_VARIABLE_INIT {0}
#define CONDITION_VARIABLE_INIT RTL_CONDITION_VARIABLE_INIT
#define RTL_CONDITION_VARIABLE_LOCKMODE_SHARED 1U
#define CONDITION_VARIABLE_LOCKMODE_SHARED RTL_CONDITION_VARIABLE_LOCKMODE_SHARED

typedef union _RTL_RUN_ONCE {
    PVOID     Ptr;
    ULONG_PTR Value;
} RTL_RUN_ONCE, *PRTL_RUN_ONCE, INIT_ONCE, *PINIT_ONCE, *LPINIT_ONCE;

typedef DWORD (WINAPI *PRTL_RUN_ONCE_INIT_FN)(PRTL_RUN_ONCE, PVOID, PVOID *);
typedef BOOL (WINAPI *PINIT_ONCE_FN)(PINIT_ONCE, PVOID, PVOID *);

#define RTL_RUN_ONCE_INIT {0}
#define INIT_ONCE_STATIC_INIT RTL_RUN_ONCE_INIT
#define RTL_RUN_ONCE_CHECK_ONLY 1U
#define RTL_RUN_ONCE_ASYNC 2U
#define RTL_RUN_ONCE_INIT_FAILED 4U
#define RTL_RUN_ONCE_CTX_RESERVED_BITS 2
#define INIT_ONCE_CHECK_ONLY RTL_RUN_ONCE_CHECK_ONLY
#define INIT_ONCE_ASYNC RTL_RUN_ONCE_ASYNC
#define INIT_ONCE_INIT_FAILED RTL_RUN_ONCE_INIT_FAILED
#define INIT_ONCE_CTX_RESERVED_BITS RTL_RUN_ONCE_CTX_RESERVED_BITS

typedef struct _RTL_BARRIER {
    DWORD     Reserved1;
    DWORD     Reserved2;
    ULONG_PTR Reserved3[2];
    DWORD     Reserved4;
    DWORD     Reserved5;
} RTL_BARRIER, *PRTL_BARRIER, SYNCHRONIZATION_BARRIER, *PSYNCHRONIZATION_BARRIER, *LPSYNCHRONIZATION_BARRIER;

#define SYNCHRONIZATION_BARRIER_FLAGS_SPIN_ONLY  0x01U
#define SYNCHRONIZATION_BARRIER_FLAGS_BLOCK_ONLY 0x02U
#define SYNCHRONIZATION_BARRIER_FLAGS_NO_DELETE  0x04U

typedef struct _RTL_CRITICAL_SECTION {
    PVOID     DebugInfo;
    LONG      LockCount;
    LONG      RecursionCount;
    HANDLE    OwningThread;
    HANDLE    LockSemaphore;
    ULONG_PTR SpinCount;
} RTL_CRITICAL_SECTION, *PRTL_CRITICAL_SECTION, CRITICAL_SECTION, *PCRITICAL_SECTION, *LPCRITICAL_SECTION;

#ifndef INFINITE
#define INFINITE            0xFFFFFFFFU
#endif
#ifndef MAXIMUM_WAIT_OBJECTS
#define MAXIMUM_WAIT_OBJECTS 64U
#endif
#ifndef WAIT_OBJECT_0
#define WAIT_OBJECT_0       0x00000000U
#define WAIT_ABANDONED      0x00000080U
#define WAIT_ABANDONED_0    0x00000080U
#define WAIT_IO_COMPLETION  0x000000C0U
#define WAIT_TIMEOUT        0x00000102U
#define WAIT_FAILED         0xFFFFFFFFU
#endif

#ifndef ERROR_SUCCESS
#define ERROR_SUCCESS               0U
#define ERROR_INVALID_FUNCTION      1U
#define ERROR_FILE_NOT_FOUND        2U
#define ERROR_PATH_NOT_FOUND        3U
#define ERROR_ACCESS_DENIED         5U
#define ERROR_INVALID_HANDLE        6U
#define ERROR_NOT_ENOUGH_MEMORY     8U
#define ERROR_INVALID_PARAMETER     87U
#define ERROR_SEM_IS_SET            102U
#define ERROR_INSUFFICIENT_BUFFER   122U
#define ERROR_INVALID_NAME          123U
#define ERROR_ALREADY_EXISTS        183U
#define ERROR_NOT_OWNER             288U
#define ERROR_TOO_MANY_POSTS        298U
#define ERROR_TIMEOUT               1460U
#endif

#ifndef DELETE
#define DELETE                      0x00010000U
#define READ_CONTROL                0x00020000U
#define WRITE_DAC                   0x00040000U
#define WRITE_OWNER                 0x00080000U
#define SYNCHRONIZE                 0x00100000U
#define STANDARD_RIGHTS_REQUIRED    0x000F0000U
#define EVENT_QUERY_STATE           0x0001U
#define EVENT_MODIFY_STATE          0x0002U
#define EVENT_ALL_ACCESS            (STANDARD_RIGHTS_REQUIRED | SYNCHRONIZE | 0x3U)
#define MUTANT_QUERY_STATE          0x0001U
#define MUTANT_ALL_ACCESS           (STANDARD_RIGHTS_REQUIRED | SYNCHRONIZE | MUTANT_QUERY_STATE)
#define SEMAPHORE_QUERY_STATE       0x0001U
#define SEMAPHORE_MODIFY_STATE      0x0002U
#define SEMAPHORE_ALL_ACCESS        (STANDARD_RIGHTS_REQUIRED | SYNCHRONIZE | 0x3U)
#endif

#ifndef CREATE_EVENT_MANUAL_RESET
#define CREATE_EVENT_MANUAL_RESET   0x00000001U
#define CREATE_EVENT_INITIAL_SET    0x00000002U
#define CREATE_MUTEX_INITIAL_OWNER  0x00000001U
#endif
#endif /* !_WIN32 */

#ifndef EVENT_QUERY_STATE
#define EVENT_QUERY_STATE           0x0001U
#endif
#ifndef SEMAPHORE_QUERY_STATE
#define SEMAPHORE_QUERY_STATE       0x0001U
#endif

typedef struct _UNICODE_STRING {
    USHORT Length;
    USHORT MaximumLength;
    PWSTR  Buffer;
} UNICODE_STRING, *PUNICODE_STRING;
typedef const UNICODE_STRING *PCUNICODE_STRING;

typedef struct _OBJECT_ATTRIBUTES {
    ULONG           Length;
    HANDLE          RootDirectory;
    PUNICODE_STRING ObjectName;
    ULONG           Attributes;
    PVOID           SecurityDescriptor;
    PVOID           SecurityQualityOfService;
} OBJECT_ATTRIBUTES, *POBJECT_ATTRIBUTES;

#ifndef OBJ_INHERIT
#define OBJ_INHERIT            0x00000002U
#define OBJ_PERMANENT          0x00000010U
#define OBJ_EXCLUSIVE          0x00000020U
#define OBJ_CASE_INSENSITIVE   0x00000040U
#define OBJ_OPENIF             0x00000080U
#define OBJ_OPENLINK           0x00000100U
#define OBJ_KERNEL_HANDLE      0x00000200U
#define OBJ_VALID_ATTRIBUTES   0x000003F2U
#endif

#ifndef InitializeObjectAttributes
#define InitializeObjectAttributes(p, n, a, r, s) \
    do { \
        (p)->Length = sizeof(OBJECT_ATTRIBUTES); \
        (p)->RootDirectory = (r); \
        (p)->Attributes = (a); \
        (p)->ObjectName = (n); \
        (p)->SecurityDescriptor = (s); \
        (p)->SecurityQualityOfService = NULL; \
    } while (0)
#endif

typedef enum _EVENT_TYPE {
    NotificationEvent,
    SynchronizationEvent
} EVENT_TYPE;

typedef enum _EVENT_INFORMATION_CLASS {
    EventBasicInformation
} EVENT_INFORMATION_CLASS;

typedef struct _EVENT_BASIC_INFORMATION {
    EVENT_TYPE EventType;
    LONG       EventState;
} EVENT_BASIC_INFORMATION, *PEVENT_BASIC_INFORMATION;

typedef enum _SEMAPHORE_INFORMATION_CLASS {
    SemaphoreBasicInformation
} SEMAPHORE_INFORMATION_CLASS;

typedef struct _SEMAPHORE_BASIC_INFORMATION {
    LONG CurrentCount;
    LONG MaximumCount;
} SEMAPHORE_BASIC_INFORMATION, *PSEMAPHORE_BASIC_INFORMATION;

typedef enum _MUTANT_INFORMATION_CLASS {
    MutantBasicInformation
} MUTANT_INFORMATION_CLASS;

typedef struct _MUTANT_BASIC_INFORMATION {
    LONG    CurrentCount;
    BOOLEAN OwnedByCaller;
    BOOLEAN AbandonedState;
} MUTANT_BASIC_INFORMATION, *PMUTANT_BASIC_INFORMATION;

typedef enum _WAIT_TYPE {
    WaitAll,
    WaitAny
} WAIT_TYPE;

typedef struct _RTL_RWLOCK {
    RTL_CRITICAL_SECTION CriticalSection;
    HANDLE               SharedSem;
    ULONG                 SharedWaiters;
    HANDLE               ExclusiveSem;
    ULONG                ExclusiveWaiters;
    LONG                 NumberActive;
    HANDLE               OwningThreadId;
    DWORD                TimeoutBoost;
    PVOID                DebugInfo;
} RTL_RWLOCK, *PRTL_RWLOCK, *LPRTL_RWLOCK;

typedef void (WINAPI *PTIMER_APC_ROUTINE)(PVOID, ULONG, LONG);

typedef struct _NTDLL_MACOS_NTSYNC_STATE {
    ULONG ulock_available;
    ULONG mach_semaphore_available;
    ULONG ulock_wait_calls;
    ULONG ulock_wake_calls;
    ULONG mach_sem_wait_calls;
    ULONG mach_sem_signal_calls;
    ULONG wait_on_address_calls;
    ULONG wake_by_address_single_calls;
    ULONG wake_by_address_all_calls;
    ULONG tid_alert_wait_calls;
    ULONG tid_alert_wake_calls;
    ULONG active_sync_objects;
} NTDLL_MACOS_NTSYNC_STATE;

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

/* macOS Fast NT Synchronization (NTSync Parity) APIs (dlls/ntdll/sync.c) */
NTSTATUS WINAPI NtWaitForAlertByThreadId(
    const void *Address,
    const LARGE_INTEGER *Timeout);

NTSTATUS WINAPI NtAlertThreadByThreadId(
    HANDLE ThreadId);

NTSTATUS WINAPI NtAlertMultipleThreadByThreadId(
    HANDLE *ThreadIds,
    ULONG Count,
    const void *Boost1,
    const void *Boost2);

NTSTATUS WINAPI RtlWaitOnAddress(
    const void *Address,
    const void *CompareAddress,
    SIZE_T AddressSize,
    const LARGE_INTEGER *Timeout);

void WINAPI RtlWakeAddressSingle(
    const void *Address);

void WINAPI RtlWakeAddressAll(
    const void *Address);

NTSTATUS WINAPI NtCreateEvent(
    HANDLE *EventHandle,
    ACCESS_MASK DesiredAccess,
    const OBJECT_ATTRIBUTES *ObjectAttributes,
    EVENT_TYPE EventType,
    BOOLEAN InitialState);

NTSTATUS WINAPI NtOpenEvent(
    HANDLE *EventHandle,
    ACCESS_MASK DesiredAccess,
    const OBJECT_ATTRIBUTES *ObjectAttributes);

NTSTATUS WINAPI NtSetEvent(
    HANDLE EventHandle,
    LONG *PrevState);

NTSTATUS WINAPI NtResetEvent(
    HANDLE EventHandle,
    LONG *PrevState);

NTSTATUS WINAPI NtPulseEvent(
    HANDLE EventHandle,
    LONG *PrevState);

NTSTATUS WINAPI NtQueryEvent(
    HANDLE EventHandle,
    EVENT_INFORMATION_CLASS EventInformationClass,
    PVOID EventInformation,
    ULONG EventInformationLength,
    ULONG *ReturnLength);

NTSTATUS WINAPI NtSetEventBoostPriority(
    HANDLE EventHandle);

NTSTATUS WINAPI NtCreateSemaphore(
    HANDLE *SemaphoreHandle,
    ACCESS_MASK DesiredAccess,
    const OBJECT_ATTRIBUTES *ObjectAttributes,
    LONG InitialCount,
    LONG MaximumCount);

NTSTATUS WINAPI NtOpenSemaphore(
    HANDLE *SemaphoreHandle,
    ACCESS_MASK DesiredAccess,
    const OBJECT_ATTRIBUTES *ObjectAttributes);

NTSTATUS WINAPI NtReleaseSemaphore(
    HANDLE SemaphoreHandle,
    ULONG ReleaseCount,
    LONG *PreviousCount);

NTSTATUS WINAPI NtQuerySemaphore(
    HANDLE SemaphoreHandle,
    SEMAPHORE_INFORMATION_CLASS SemaphoreInformationClass,
    PVOID SemaphoreInformation,
    ULONG SemaphoreInformationLength,
    ULONG *ReturnLength);

NTSTATUS WINAPI NtCreateMutant(
    HANDLE *MutantHandle,
    ACCESS_MASK DesiredAccess,
    const OBJECT_ATTRIBUTES *ObjectAttributes,
    BOOLEAN InitialOwner);

NTSTATUS WINAPI NtOpenMutant(
    HANDLE *MutantHandle,
    ACCESS_MASK DesiredAccess,
    const OBJECT_ATTRIBUTES *ObjectAttributes);

NTSTATUS WINAPI NtReleaseMutant(
    HANDLE MutantHandle,
    LONG *PrevCount);

NTSTATUS WINAPI NtQueryMutant(
    HANDLE MutantHandle,
    MUTANT_INFORMATION_CLASS MutantInformationClass,
    PVOID MutantInformation,
    ULONG MutantInformationLength,
    ULONG *ReturnLength);

NTSTATUS WINAPI NtCreateKeyedEvent(
    HANDLE *KeyedEventHandle,
    ACCESS_MASK DesiredAccess,
    const OBJECT_ATTRIBUTES *ObjectAttributes,
    ULONG Flags);

NTSTATUS WINAPI NtOpenKeyedEvent(
    HANDLE *KeyedEventHandle,
    ACCESS_MASK DesiredAccess,
    const OBJECT_ATTRIBUTES *ObjectAttributes);

NTSTATUS WINAPI NtWaitForKeyedEvent(
    HANDLE KeyedEventHandle,
    const void *Key,
    BOOLEAN Alertable,
    const LARGE_INTEGER *Timeout);

NTSTATUS WINAPI NtReleaseKeyedEvent(
    HANDLE KeyedEventHandle,
    const void *Key,
    BOOLEAN Alertable,
    const LARGE_INTEGER *Timeout);

NTSTATUS WINAPI NtWaitForSingleObject(
    HANDLE ObjectHandle,
    BOOLEAN Alertable,
    const LARGE_INTEGER *TimeOut);

NTSTATUS WINAPI NtWaitForMultipleObjects(
    DWORD ObjectCount,
    const HANDLE *ObjectsArray,
    WAIT_TYPE WaitType,
    BOOLEAN Alertable,
    const LARGE_INTEGER *TimeOut);

NTSTATUS WINAPI NtSignalAndWaitForSingleObject(
    HANDLE hObjectToSignal,
    HANDLE hObjectToWaitOn,
    BOOLEAN bAlertable,
    const LARGE_INTEGER *TimeOut);

NTSTATUS WINAPI NtDelayExecution(
    BOOLEAN Alertable,
    const LARGE_INTEGER *Interval);

NTSTATUS WINAPI NtQuerySystemTime(
    PLARGE_INTEGER SystemTime);

NTSTATUS WINAPI NtQueryTimerResolution(
    ULONG *MinimumResolution,
    ULONG *MaximumResolution,
    ULONG *ActualResolution);

NTSTATUS WINAPI NtClose(
    HANDLE Handle);

void WINAPI RtlInitializeSRWLock(PRTL_SRWLOCK Lock);
void WINAPI RtlAcquireSRWLockExclusive(PRTL_SRWLOCK Lock);
void WINAPI RtlAcquireSRWLockShared(PRTL_SRWLOCK Lock);
void WINAPI RtlReleaseSRWLockExclusive(PRTL_SRWLOCK Lock);
void WINAPI RtlReleaseSRWLockShared(PRTL_SRWLOCK Lock);
BOOLEAN WINAPI RtlTryAcquireSRWLockExclusive(PRTL_SRWLOCK Lock);
BOOLEAN WINAPI RtlTryAcquireSRWLockShared(PRTL_SRWLOCK Lock);

void WINAPI RtlInitializeConditionVariable(PRTL_CONDITION_VARIABLE Variable);
NTSTATUS WINAPI RtlSleepConditionVariableCS(
    PRTL_CONDITION_VARIABLE Variable,
    PRTL_CRITICAL_SECTION Crit,
    PLARGE_INTEGER Timeout);
NTSTATUS WINAPI RtlSleepConditionVariableSRW(
    PRTL_CONDITION_VARIABLE Variable,
    PRTL_SRWLOCK Lock,
    PLARGE_INTEGER Timeout,
    ULONG Flags);
void WINAPI RtlWakeConditionVariable(PRTL_CONDITION_VARIABLE Variable);
void WINAPI RtlWakeAllConditionVariable(PRTL_CONDITION_VARIABLE Variable);

void WINAPI RtlRunOnceInitialize(PRTL_RUN_ONCE Once);
NTSTATUS WINAPI RtlRunOnceBeginInitialize(PRTL_RUN_ONCE Once, ULONG Flags, PVOID *Context);
NTSTATUS WINAPI RtlRunOnceComplete(PRTL_RUN_ONCE Once, ULONG Flags, PVOID Context);
NTSTATUS WINAPI RtlRunOnceExecuteOnce(
    PRTL_RUN_ONCE Once,
    PRTL_RUN_ONCE_INIT_FN Func,
    PVOID Param,
    PVOID *Context);

NTSTATUS WINAPI RtlInitBarrier(PRTL_BARRIER Barrier, ULONG TotalThreads, ULONG SpinCount);
BOOLEAN WINAPI RtlBarrier(PRTL_BARRIER Barrier, ULONG Flags);
NTSTATUS WINAPI RtlDeleteBarrier(PRTL_BARRIER Barrier);

void WINAPI RtlInitializeResource(LPRTL_RWLOCK RwLock);
void WINAPI RtlDeleteResource(LPRTL_RWLOCK RwLock);
BYTE WINAPI RtlAcquireResourceExclusive(LPRTL_RWLOCK RwLock, BYTE fWait);
BYTE WINAPI RtlAcquireResourceShared(LPRTL_RWLOCK RwLock, BYTE fWait);
void WINAPI RtlReleaseResource(LPRTL_RWLOCK RwLock);
void WINAPI RtlDumpResource(LPRTL_RWLOCK RwLock);

NTSTATUS WINAPI RtlInitializeCriticalSection(PRTL_CRITICAL_SECTION Crit);
NTSTATUS WINAPI RtlDeleteCriticalSection(PRTL_CRITICAL_SECTION Crit);
NTSTATUS WINAPI RtlEnterCriticalSection(PRTL_CRITICAL_SECTION Crit);
NTSTATUS WINAPI RtlLeaveCriticalSection(PRTL_CRITICAL_SECTION Crit);
BOOL WINAPI RtlTryEnterCriticalSection(PRTL_CRITICAL_SECTION Crit);

void WINAPI RtlInitUnicodeString(PUNICODE_STRING DestinationString, LPCWSTR SourceString);
ULONG WINAPI RtlNtStatusToDosError(NTSTATUS Status);

DWORD WINAPI NtdllGetCurrentThreadId(void);
void WINAPI NtdllRegisterForeignThreadId(DWORD ThreadId);
NTSTATUS WINAPI NtdllAbandonMutantsForThread(DWORD ThreadId);
NTSTATUS WINAPI NtdllQueryMacosNtsyncState(NTDLL_MACOS_NTSYNC_STATE *State);
void WINAPI NtdllResetSyncState(void);

/* =========================================================================
 * ARM64EC / ARM64X Loader Edge Cases (WineConf 2025 ARM64 Roadmap):
 *   1. ARM64X Dynamic Value Relocation Table (DVRT v1/v2) & NtReadFile system32 PE Header View
 *   2. TSC Registry Frequency Rescaling (~MHz & NtQueryPerformanceCounter)
 *   3. On-Disk JIT Code Caching (WJTC integrity-checked ARM64 translation cache)
 * ========================================================================= */

#define NTDLL_IMAGE_DYNAMIC_RELOCATION_ARM64X       0x00000006ULL
#define NTDLL_DVRT_ARM64X_FIXUP_TYPE_ZEROFILL       0U
#define NTDLL_DVRT_ARM64X_FIXUP_TYPE_VALUE          1U
#define NTDLL_DVRT_ARM64X_FIXUP_TYPE_DELTA          2U

typedef struct _NTDLL_TSC_SCALE_STATE {
    ULONGLONG host_cntfrq_hz;
    ULONGLONG target_tsc_hz;
    ULONGLONG scale_numerator;
    ULONGLONG scale_denominator;
    ULONG     registry_mhz;
    ULONG     configured;
} NTDLL_TSC_SCALE_STATE;

typedef struct _NTDLL_JIT_MODULE_KEY {
    ULONG  time_date_stamp;
    ULONG  checksum;
    ULONG  size_of_image;
    USHORT guest_machine;
    USHORT emulator_id;
} NTDLL_JIT_MODULE_KEY;

typedef struct _NTDLL_JIT_CACHE_STATS {
    ULONG initialized;
    ULONG cache_stores;
    ULONG cache_hits;
    ULONG cache_misses;
    ULONG corrupt_rejections;
    ULONG invalidated_entries;
    ULONG icache_flushes;
} NTDLL_JIT_CACHE_STATS;

NTSTATUS WINAPI NtdllArm64xBuildSyntheticImage(
    PVOID Buffer,
    SIZE_T BufferSize,
    ULONG DvRtVersion,
    SIZE_T *OutImageSize);

NTSTATUS WINAPI NtdllArm64xApplyDynamicRelocations(
    PVOID ImageBase,
    SIZE_T ImageSize);

NTSTATUS WINAPI NtReadFileArm64xPeView(
    const WCHAR *FilePath,
    USHORT CallerMachine,
    const void *RawFileBytes,
    SIZE_T RawFileSize,
    ULONGLONG ByteOffset,
    PVOID Buffer,
    ULONG Length,
    ULONG *BytesRead);

NTSTATUS WINAPI NtdllTscScaleConfigure(
    ULONGLONG HostCntFrqHz,
    ULONGLONG TargetTscHz,
    NTDLL_TSC_SCALE_STATE *OutState);

NTSTATUS WINAPI NtdllTscScaleReadTicks(
    ULONGLONG RawHostCntVct,
    ULONGLONG *OutScaledRdtsc);

NTSTATUS WINAPI NtdllTscQueryRegistryMhz(
    const WCHAR *SubKeyPath,
    const WCHAR *ValueName,
    ULONG *OutMhz);

NTSTATUS WINAPI NtQueryPerformanceCounter(
    PLARGE_INTEGER PerformanceCounter,
    PLARGE_INTEGER PerformanceFrequency);

NTSTATUS WINAPI NtdllJitCacheInit(const char *CacheDirPath);

NTSTATUS WINAPI NtdllJitCacheStoreBlock(
    const NTDLL_JIT_MODULE_KEY *ModuleKey,
    ULONG GuestRva,
    const void *Arm64Code,
    SIZE_T CodeSize);

NTSTATUS WINAPI NtdllJitCacheLookupBlock(
    const NTDLL_JIT_MODULE_KEY *ModuleKey,
    ULONG GuestRva,
    PVOID OutArm64Code,
    SIZE_T BufferSize,
    SIZE_T *OutCodeSize);

NTSTATUS WINAPI NtdllJitCacheCorruptBlockForTest(
    const NTDLL_JIT_MODULE_KEY *ModuleKey,
    ULONG GuestRva);

NTSTATUS WINAPI NtdllJitCacheInvalidateModule(
    const NTDLL_JIT_MODULE_KEY *ModuleKey,
    ULONG *OutRemovedCount);

NTSTATUS WINAPI NtdllJitCacheQueryStats(NTDLL_JIT_CACHE_STATS *OutStats);

#ifndef _WIN32
static inline DWORD GetCurrentThreadId(void)
{
    return NtdllGetCurrentThreadId();
}
#endif

#endif /* _WINE_NTDLL_H */

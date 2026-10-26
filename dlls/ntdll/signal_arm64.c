/*
 * Wine NTDLL ARM64 & ARM64EC Signal / Context / WoW64 Management
 * (Darwin x18 Platform-Register Preservation & ARM64EC fexcore.dll / box64cpu.dll Support)
 *
 * Copyright 2026 Spumante Contributors
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#ifdef __APPLE__
#include <libkern/OSCacheControl.h>
#endif

#include "ntdll.h"

#define WINE_DEFAULT_DEBUG_CHANNEL(ch)
#define TRACE(...) do { } while (0)
#define WARN(...)  do { } while (0)
#define FIXME(...) do { } while (0)
#define ERR(...)   do { } while (0)

WINE_DEFAULT_DEBUG_CHANNEL(seh);

static NTDLL_ARM64_NT_CONTEXT g_arm64_thread_ctx;
static NTDLL_DARWIN_X18_STATE g_darwin_x18_state = {
    0x000000016fdff000ULL,
    0x000000007ffe0000ULL,
    0x000000016fdff000ULL,
    0U,
    0U,
    0U,
    0U,
    0U
};

#define NTDLL_ARM64EC_DEFAULT_FEATURES \
    ((1ULL << 2)  | /* PF_COMPARE_EXCHANGE_DOUBLE */ \
     (1ULL << 3)  | /* PF_MMX_INSTRUCTIONS_AVAILABLE */ \
     (1ULL << 6)  | /* PF_XMMI_INSTRUCTIONS_AVAILABLE */ \
     (1ULL << 8)  | /* PF_RDTSC_INSTRUCTION_AVAILABLE */ \
     (1ULL << 10) | /* PF_XMMI64_INSTRUCTIONS_AVAILABLE */ \
     (1ULL << 12) | /* PF_NX_ENABLED */ \
     (1ULL << 13) | /* PF_SSE3_INSTRUCTIONS_AVAILABLE */ \
     (1ULL << 14) | /* PF_COMPARE_EXCHANGE128 */ \
     (1ULL << 23))  /* PF_FASTFAIL_AVAILABLE */

static NTDLL_ARM64EC_EMULATOR_INFO g_arm64ec_emu = {
    "fexcore.dll",
    IMAGE_FILE_MACHINE_ARM64,
    IMAGE_FILE_MACHINE_AMD64,
    1U,
    0U,
    0U,
    (ULONG)NTDLL_ARM64EC_DEFAULT_FEATURES
};

static int is_stub_simulation_enabled(void)
{
    const char *env = getenv("SPUMANTE_SIMULATE_STUB");
    return (env != NULL && strcmp(env, "1") == 0);
}

static void safe_copy_module_name(char *dst, size_t dst_size, const char *src)
{
    size_t len;
    if (dst_size == 0) {
        return;
    }
    if (src == NULL) {
        dst[0] = '\0';
        return;
    }
    len = strlen(src);
    if (len >= dst_size) {
        len = dst_size - 1;
    }
    memcpy(dst, src, len);
    dst[len] = '\0';
}

/* =========================================================================
 * Darwin x18 Platform-Register Save/Restore State Machine
 * On arm64-apple-darwin, x18 is reserved by libSystem/pthreads as the Darwin
 * platform register, while Windows ARM64 and ARM64EC use x18 as the TEB pointer.
 * ========================================================================= */

NTSTATUS WINAPI NtdllDarwinX18Init(ULONGLONG DarwinPlatformX18, ULONGLONG WindowsTebX18)
{
    if (is_stub_simulation_enabled()) {
        return STATUS_NOT_IMPLEMENTED;
    }
    memset(&g_darwin_x18_state, 0, sizeof(g_darwin_x18_state));
    memset(&g_arm64_thread_ctx, 0, sizeof(g_arm64_thread_ctx));

    g_darwin_x18_state.darwin_platform_x18 = DarwinPlatformX18;
    g_darwin_x18_state.windows_teb_x18 = WindowsTebX18;
    g_darwin_x18_state.active_x18 = DarwinPlatformX18;
    g_darwin_x18_state.in_pe_mode = 0U;
    g_arm64_thread_ctx.X[18] = WindowsTebX18;
    return STATUS_SUCCESS;
}

NTSTATUS WINAPI NtdllDarwinX18EnterPe(void)
{
    if (is_stub_simulation_enabled()) {
        return STATUS_NOT_IMPLEMENTED;
    }
    g_darwin_x18_state.active_x18 = g_darwin_x18_state.windows_teb_x18;
    g_darwin_x18_state.in_pe_mode = 1U;
    g_darwin_x18_state.pe_enter_count++;
    g_darwin_x18_state.restore_flags |= NTDLL_RESTORE_FLAGS_DARWIN_X18_SAVED;
    g_arm64_thread_ctx.X[18] = g_darwin_x18_state.windows_teb_x18;
    return STATUS_SUCCESS;
}

NTSTATUS WINAPI NtdllDarwinX18ExitToUnix(void)
{
    if (is_stub_simulation_enabled()) {
        return STATUS_NOT_IMPLEMENTED;
    }
    g_darwin_x18_state.windows_teb_x18 = g_arm64_thread_ctx.X[18];
    g_darwin_x18_state.active_x18 = g_darwin_x18_state.darwin_platform_x18;
    g_darwin_x18_state.in_pe_mode = 0U;
    g_darwin_x18_state.unix_reenter_count++;
    return STATUS_SUCCESS;
}

NTSTATUS WINAPI NtdllDarwinX18SignalEnter(ULONGLONG FaultedX18)
{
    if (is_stub_simulation_enabled()) {
        return STATUS_NOT_IMPLEMENTED;
    }
    if (g_darwin_x18_state.in_pe_mode) {
        g_darwin_x18_state.windows_teb_x18 = FaultedX18;
        g_arm64_thread_ctx.X[18] = FaultedX18;
    }
    /* Immediately restore Darwin platform x18 before calling into libSystem / pthreads */
    g_darwin_x18_state.active_x18 = g_darwin_x18_state.darwin_platform_x18;
    g_darwin_x18_state.signal_handler_save_count++;
    return STATUS_SUCCESS;
}

NTSTATUS WINAPI NtdllDarwinX18SignalLeave(void)
{
    if (is_stub_simulation_enabled()) {
        return STATUS_NOT_IMPLEMENTED;
    }
    if (g_darwin_x18_state.in_pe_mode) {
        g_darwin_x18_state.active_x18 = g_darwin_x18_state.windows_teb_x18;
    } else {
        g_darwin_x18_state.active_x18 = g_darwin_x18_state.darwin_platform_x18;
    }
    return STATUS_SUCCESS;
}

NTSTATUS WINAPI NtdllDarwinX18QueryState(NTDLL_DARWIN_X18_STATE *State)
{
    if (is_stub_simulation_enabled()) {
        return STATUS_NOT_IMPLEMENTED;
    }
    if (State == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    *State = g_darwin_x18_state;
    return STATUS_SUCCESS;
}

/* =========================================================================
 * ARM64 Thread Context Get/Set (with CONTEXT_ARM64_X18 & Emulation Flags)
 * ========================================================================= */

NTSTATUS WINAPI NtGetContextThread(
    HANDLE ThreadHandle,
    PNTDLL_ARM64_NT_CONTEXT Context)
{
    ULONG flags;

    if (is_stub_simulation_enabled()) {
        return STATUS_NOT_IMPLEMENTED;
    }
    if (ThreadHandle != NtCurrentThread()) {
        return STATUS_INVALID_HANDLE;
    }
    if (Context == NULL) {
        return STATUS_ACCESS_VIOLATION;
    }

    flags = Context->ContextFlags;
    if ((flags & NTDLL_CONTEXT_ARM64_CONTROL) == NTDLL_CONTEXT_ARM64_CONTROL) {
        Context->X[29] = g_arm64_thread_ctx.X[29];
        Context->X[30] = g_arm64_thread_ctx.X[30];
        Context->Sp = g_arm64_thread_ctx.Sp;
        Context->Pc = g_arm64_thread_ctx.Pc;
        Context->Cpsr = g_arm64_thread_ctx.Cpsr;
    }
    if ((flags & NTDLL_CONTEXT_ARM64_INTEGER) == NTDLL_CONTEXT_ARM64_INTEGER) {
        memcpy(&Context->X[0], &g_arm64_thread_ctx.X[0], 18 * sizeof(ULONGLONG));
        memcpy(&Context->X[19], &g_arm64_thread_ctx.X[19], 10 * sizeof(ULONGLONG));
        if ((flags & NTDLL_CONTEXT_ARM64_X18) == NTDLL_CONTEXT_ARM64_X18) {
            Context->X[18] = g_arm64_thread_ctx.X[18];
        }
    } else if ((flags & NTDLL_CONTEXT_ARM64_X18) == NTDLL_CONTEXT_ARM64_X18) {
        Context->X[18] = g_arm64_thread_ctx.X[18];
    }
    if ((flags & NTDLL_CONTEXT_ARM64_FLOATING_POINT) == NTDLL_CONTEXT_ARM64_FLOATING_POINT) {
        memcpy(Context->V, g_arm64_thread_ctx.V, sizeof(Context->V));
        Context->Fpcr = g_arm64_thread_ctx.Fpcr;
        Context->Fpsr = g_arm64_thread_ctx.Fpsr;
    }
    if ((flags & NTDLL_CONTEXT_ARM64_DEBUG_REGISTERS) == NTDLL_CONTEXT_ARM64_DEBUG_REGISTERS) {
        memcpy(Context->Bcr, g_arm64_thread_ctx.Bcr, sizeof(Context->Bcr));
        memcpy(Context->Bvr, g_arm64_thread_ctx.Bvr, sizeof(Context->Bvr));
        memcpy(Context->Wcr, g_arm64_thread_ctx.Wcr, sizeof(Context->Wcr));
        memcpy(Context->Wvr, g_arm64_thread_ctx.Wvr, sizeof(Context->Wvr));
    }
    Context->RestoreFlags = g_arm64_thread_ctx.RestoreFlags;
    return STATUS_SUCCESS;
}

NTSTATUS WINAPI NtSetContextThread(
    HANDLE ThreadHandle,
    const NTDLL_ARM64_NT_CONTEXT *Context)
{
    ULONG flags;

    if (is_stub_simulation_enabled()) {
        return STATUS_NOT_IMPLEMENTED;
    }
    if (ThreadHandle != NtCurrentThread()) {
        return STATUS_INVALID_HANDLE;
    }
    if (Context == NULL) {
        return STATUS_ACCESS_VIOLATION;
    }

    flags = Context->ContextFlags;
    if ((flags & NTDLL_CONTEXT_ARM64_CONTROL) == NTDLL_CONTEXT_ARM64_CONTROL) {
        g_arm64_thread_ctx.X[29] = Context->X[29];
        g_arm64_thread_ctx.X[30] = Context->X[30];
        g_arm64_thread_ctx.Sp = Context->Sp;
        g_arm64_thread_ctx.Pc = Context->Pc;
        g_arm64_thread_ctx.Cpsr = Context->Cpsr;
        g_arm64_thread_ctx.RestoreFlags |= NTDLL_RESTORE_FLAGS_EMULATION;
    }
    if ((flags & NTDLL_CONTEXT_ARM64_INTEGER) == NTDLL_CONTEXT_ARM64_INTEGER) {
        /* Copy X0..X17 and X19..X28; skip X18 unless CONTEXT_ARM64_X18 is explicitly set */
        memcpy(&g_arm64_thread_ctx.X[0], &Context->X[0], 18 * sizeof(ULONGLONG));
        memcpy(&g_arm64_thread_ctx.X[19], &Context->X[19], 10 * sizeof(ULONGLONG));
    }
    if ((flags & NTDLL_CONTEXT_ARM64_X18) == NTDLL_CONTEXT_ARM64_X18) {
        g_arm64_thread_ctx.X[18] = Context->X[18];
        g_darwin_x18_state.windows_teb_x18 = Context->X[18];
        if (g_darwin_x18_state.in_pe_mode) {
            g_darwin_x18_state.active_x18 = Context->X[18];
        }
    }
    if ((flags & NTDLL_CONTEXT_ARM64_FLOATING_POINT) == NTDLL_CONTEXT_ARM64_FLOATING_POINT) {
        memcpy(g_arm64_thread_ctx.V, Context->V, sizeof(g_arm64_thread_ctx.V));
        g_arm64_thread_ctx.Fpcr = Context->Fpcr;
        g_arm64_thread_ctx.Fpsr = Context->Fpsr;
    }
    if ((flags & NTDLL_CONTEXT_ARM64_DEBUG_REGISTERS) == NTDLL_CONTEXT_ARM64_DEBUG_REGISTERS) {
        memcpy(g_arm64_thread_ctx.Bcr, Context->Bcr, sizeof(g_arm64_thread_ctx.Bcr));
        memcpy(g_arm64_thread_ctx.Bvr, Context->Bvr, sizeof(g_arm64_thread_ctx.Bvr));
        memcpy(g_arm64_thread_ctx.Wcr, Context->Wcr, sizeof(g_arm64_thread_ctx.Wcr));
        memcpy(g_arm64_thread_ctx.Wvr, Context->Wvr, sizeof(g_arm64_thread_ctx.Wvr));
    }
    return STATUS_SUCCESS;
}

/* =========================================================================
 * RtlWow64GetCpuAreaInfo & Architecture Discovery
 * ========================================================================= */

NTSTATUS WINAPI RtlWow64GetCpuAreaInfo(
    PVOID CpuArea,
    ULONG Reserved,
    WOW64_CPU_AREA_INFO *Info)
{
    WOW64_CPURESERVED *cpu;
    USHORT machine;
    ULONG_PTR align = 0;
    ULONG_PTR size = 0;
    ULONG_PTR flags_offset = 0;
    ULONG ctx_flag = 0;
    uintptr_t after_header;
    void *ctx_ptr;

    if (is_stub_simulation_enabled()) {
        return STATUS_NOT_IMPLEMENTED;
    }
    if (CpuArea == NULL || Info == NULL || Reserved != 0) {
        return STATUS_INVALID_PARAMETER;
    }

    cpu = (WOW64_CPURESERVED *)CpuArea;
    machine = cpu->Machine ? cpu->Machine : cpu->Flags;

    switch (machine) {
    case IMAGE_FILE_MACHINE_I386:
        align = 4U;
        size = 0x2ccU;
        flags_offset = 0x00U;
        ctx_flag = 0x00010000U;
        break;
    case IMAGE_FILE_MACHINE_AMD64:
        align = 16U;
        size = 0x4d0U;
        flags_offset = 0x30U;
        ctx_flag = 0x00100000U;
        break;
    case IMAGE_FILE_MACHINE_ARMNT:
        align = 8U;
        size = 0x1a0U;
        flags_offset = 0x00U;
        ctx_flag = 0x00200000U;
        break;
    case IMAGE_FILE_MACHINE_ARM64:
        align = 16U;
        size = 0x390U;
        flags_offset = 0x00U;
        ctx_flag = 0x00400000U;
        break;
    default:
        return STATUS_INVALID_PARAMETER;
    }

    after_header = (uintptr_t)(cpu + 1);
    ctx_ptr = (void *)((after_header + align - 1U) & ~(uintptr_t)(align - 1U));

    memset(Info, 0, sizeof(*Info));
    Info->Context = ctx_ptr;
    Info->ContextEx = (void *)(((uintptr_t)((char *)ctx_ptr + size) + sizeof(void *) - 1U)
                               & ~(uintptr_t)(sizeof(void *) - 1U));
    Info->ContextFlagsLocation = (char *)ctx_ptr + flags_offset;
    Info->CpuReserved = cpu;
    Info->ContextFlag = ctx_flag;
    Info->Machine = machine;
    return STATUS_SUCCESS;
}

USHORT WINAPI RtlWow64GetCurrentMachine(void)
{
    if (is_stub_simulation_enabled()) {
        return IMAGE_FILE_MACHINE_UNKNOWN;
    }
    return g_arm64ec_emu.guest_machine ? g_arm64ec_emu.guest_machine : IMAGE_FILE_MACHINE_AMD64;
}

NTSTATUS WINAPI RtlWow64GetProcessMachines(
    HANDLE ProcessHandle,
    USHORT *CurrentMachine,
    USHORT *TargetMachine)
{
    if (is_stub_simulation_enabled()) {
        return STATUS_NOT_IMPLEMENTED;
    }
    if (ProcessHandle == NULL) {
        return STATUS_INVALID_HANDLE;
    }
    if (CurrentMachine == NULL || TargetMachine == NULL) {
        return STATUS_ACCESS_VIOLATION;
    }
    *CurrentMachine = IMAGE_FILE_MACHINE_I386;
    *TargetMachine = g_arm64ec_emu.native_machine;
    return STATUS_SUCCESS;
}

NTSTATUS WINAPI RtlWow64IsWowGuestMachineSupported(
    USHORT WowGuestMachine,
    BOOLEAN *MachineIsSupported)
{
    if (is_stub_simulation_enabled()) {
        return STATUS_NOT_IMPLEMENTED;
    }
    if (MachineIsSupported == NULL) {
        return STATUS_ACCESS_VIOLATION;
    }
    if (WowGuestMachine == IMAGE_FILE_MACHINE_I386 ||
        WowGuestMachine == IMAGE_FILE_MACHINE_AMD64 ||
        WowGuestMachine == IMAGE_FILE_MACHINE_ARMNT) {
        *MachineIsSupported = 1;
    } else {
        *MachineIsSupported = 0;
    }
    return STATUS_SUCCESS;
}

NTSTATUS WINAPI RtlGetNativeSystemInformation(
    ULONG SystemInformationClass,
    PVOID SystemInformation,
    ULONG SystemInformationLength,
    PULONG ReturnLength)
{
    if (is_stub_simulation_enabled()) {
        return STATUS_NOT_IMPLEMENTED;
    }
    if (SystemInformationClass == SystemCpuInformation) {
        NTDLL_SYSTEM_CPU_INFORMATION *cpu_info;
        if (ReturnLength != NULL) {
            *ReturnLength = (ULONG)sizeof(NTDLL_SYSTEM_CPU_INFORMATION);
        }
        if (SystemInformationLength < sizeof(NTDLL_SYSTEM_CPU_INFORMATION)) {
            return STATUS_INFO_LENGTH_MISMATCH;
        }
        if (SystemInformation == NULL) {
            return STATUS_ACCESS_VIOLATION;
        }
        cpu_info = (NTDLL_SYSTEM_CPU_INFORMATION *)SystemInformation;
        cpu_info->ProcessorArchitecture = 12U; /* PROCESSOR_ARCHITECTURE_ARM64 */
        cpu_info->ProcessorLevel = 8U;
        cpu_info->ProcessorRevision = 0U;
        cpu_info->MaximumProcessors = 8U;
        cpu_info->ProcessorFeatureBits = 0x1fffffffU;
        return STATUS_SUCCESS;
    }
    return STATUS_INVALID_INFO_CLASS;
}

NTSTATUS WINAPI NtQuerySystemInformationEx(
    ULONG SystemInformationClass,
    PVOID InputBuffer,
    ULONG InputBufferLength,
    PVOID SystemInformation,
    ULONG SystemInformationLength,
    PULONG ReturnLength)
{
    if (is_stub_simulation_enabled()) {
        return STATUS_NOT_IMPLEMENTED;
    }
    if (SystemInformationClass == SystemSupportedProcessorArchitectures ||
        SystemInformationClass == SystemSupportedProcessorArchitectures2) {
        SYSTEM_SUPPORTED_PROCESSOR_ARCHITECTURES_INFORMATION *out;
        ULONG needed = (ULONG)(4U * sizeof(SYSTEM_SUPPORTED_PROCESSOR_ARCHITECTURES_INFORMATION));

        if (InputBuffer == NULL || InputBufferLength < sizeof(HANDLE)) {
            return STATUS_INVALID_PARAMETER;
        }
        if (ReturnLength != NULL) {
            *ReturnLength = needed;
        }
        if (SystemInformationLength < needed) {
            return STATUS_BUFFER_TOO_SMALL;
        }
        if (SystemInformation == NULL) {
            return STATUS_ACCESS_VIOLATION;
        }
        out = (SYSTEM_SUPPORTED_PROCESSOR_ARCHITECTURES_INFORMATION *)SystemInformation;
        memset(out, 0, needed);

        /* Entry 0: ARM64 native */
        out[0].Machine = IMAGE_FILE_MACHINE_ARM64;
        out[0].KernelMode = 1;
        out[0].UserMode = 1;
        out[0].Native = 1;
        out[0].Process = 1;
        out[0].WoW64Container = 0;

        /* Entry 1: AMD64 (Rosetta 2 / ARM64EC fexcore/box64cpu) */
        out[1].Machine = IMAGE_FILE_MACHINE_AMD64;
        out[1].KernelMode = 0;
        out[1].UserMode = 1;
        out[1].Native = 0;
        out[1].Process = 0;
        out[1].WoW64Container = 1;

        /* Entry 2: I386 (WoW64) */
        out[2].Machine = IMAGE_FILE_MACHINE_I386;
        out[2].KernelMode = 0;
        out[2].UserMode = 1;
        out[2].Native = 0;
        out[2].Process = 0;
        out[2].WoW64Container = 1;

        /* Entry 3: zero terminator */
        return STATUS_SUCCESS;
    }
    return STATUS_INVALID_INFO_CLASS;
}

/* =========================================================================
 * Cross-Process Emulator Work-List Operations (ARM64EC / WoW64 JIT)
 * ========================================================================= */

BOOLEAN WINAPI RtlWow64PushCrossProcessWorkOntoFreeList(
    CROSS_PROCESS_WORK_HDR *FreeList,
    CROSS_PROCESS_WORK_ENTRY *Entry)
{
    ULONG offset;

    if (is_stub_simulation_enabled()) {
        return 0;
    }
    if (FreeList == NULL || Entry == NULL) {
        return 0;
    }
    offset = (ULONG)((char *)Entry - (char *)FreeList);
    Entry->next = FreeList->first & ~CROSS_PROCESS_LIST_FLUSH;
    FreeList->first = offset | (FreeList->first & CROSS_PROCESS_LIST_FLUSH);
    FreeList->counter++;
    return 1;
}

CROSS_PROCESS_WORK_ENTRY * WINAPI RtlWow64PopCrossProcessWorkFromFreeList(
    CROSS_PROCESS_WORK_HDR *FreeList)
{
    ULONG first_offset;
    CROSS_PROCESS_WORK_ENTRY *entry;

    if (is_stub_simulation_enabled() || FreeList == NULL) {
        return NULL;
    }
    first_offset = FreeList->first & ~CROSS_PROCESS_LIST_FLUSH;
    if (first_offset == 0) {
        return NULL;
    }
    entry = CROSS_PROCESS_LIST_ENTRY(FreeList, first_offset);
    FreeList->first = (entry->next & ~CROSS_PROCESS_LIST_FLUSH) | (FreeList->first & CROSS_PROCESS_LIST_FLUSH);
    entry->next = 0;
    FreeList->counter++;
    return entry;
}

BOOLEAN WINAPI RtlWow64PushCrossProcessWorkOntoWorkList(
    CROSS_PROCESS_WORK_HDR *WorkList,
    CROSS_PROCESS_WORK_ENTRY *Entry,
    void **FlushEntry)
{
    ULONG offset;

    if (is_stub_simulation_enabled()) {
        return 0;
    }
    if (FlushEntry != NULL) {
        *FlushEntry = NULL;
    }
    if (WorkList == NULL || Entry == NULL) {
        return 0;
    }
    offset = (ULONG)((char *)Entry - (char *)WorkList);
    Entry->next = WorkList->first & ~CROSS_PROCESS_LIST_FLUSH;
    WorkList->first = offset | (WorkList->first & CROSS_PROCESS_LIST_FLUSH);
    WorkList->counter++;
    return 1;
}

CROSS_PROCESS_WORK_ENTRY * WINAPI RtlWow64PopAllCrossProcessWorkFromWorkList(
    CROSS_PROCESS_WORK_HDR *WorkList,
    BOOLEAN *Flush)
{
    ULONG raw_first;
    ULONG pos;
    ULONG prev_offset = 0;
    CROSS_PROCESS_WORK_ENTRY *head = NULL;

    if (is_stub_simulation_enabled() || WorkList == NULL || Flush == NULL) {
        return NULL;
    }

    raw_first = WorkList->first;
    if (raw_first == 0) {
        *Flush = 0;
        return NULL;
    }

    *Flush = (raw_first & CROSS_PROCESS_LIST_FLUSH) ? 1 : 0;
    pos = raw_first & ~CROSS_PROCESS_LIST_FLUSH;
    WorkList->first = 0;
    WorkList->counter++;

    if (pos == 0) {
        return NULL;
    }

    /* Reverse the LIFO stack into FIFO execution order */
    while (pos != 0) {
        CROSS_PROCESS_WORK_ENTRY *curr = CROSS_PROCESS_LIST_ENTRY(WorkList, pos);
        ULONG next_pos = curr->next & ~CROSS_PROCESS_LIST_FLUSH;
        curr->next = prev_offset;
        prev_offset = pos;
        head = curr;
        pos = next_pos;
    }

    return head;
}

BOOLEAN WINAPI RtlWow64RequestCrossProcessHeavyFlush(
    CROSS_PROCESS_WORK_HDR *WorkList)
{
    if (is_stub_simulation_enabled() || WorkList == NULL) {
        return 0;
    }
    WorkList->first |= CROSS_PROCESS_LIST_FLUSH;
    WorkList->counter++;
    g_arm64ec_emu.heavy_flush_requests++;
    return 1;
}

/* =========================================================================
 * ARM64EC Emulator Engine (fexcore.dll / box64cpu.dll / xtajit64.dll)
 * ========================================================================= */

NTSTATUS WINAPI NtdllArm64EcSelectEmulator(const char *ModuleName)
{
    if (is_stub_simulation_enabled()) {
        return STATUS_NOT_IMPLEMENTED;
    }
    if (ModuleName == NULL || ModuleName[0] == '\0') {
        safe_copy_module_name(g_arm64ec_emu.module_name, sizeof(g_arm64ec_emu.module_name), "fexcore.dll");
    } else if (strcmp(ModuleName, "fexcore.dll") == 0 ||
               strcmp(ModuleName, "box64cpu.dll") == 0 ||
               strcmp(ModuleName, "xtajit64.dll") == 0) {
        safe_copy_module_name(g_arm64ec_emu.module_name, sizeof(g_arm64ec_emu.module_name), ModuleName);
    } else {
        return STATUS_INVALID_PARAMETER;
    }
    g_arm64ec_emu.native_machine = IMAGE_FILE_MACHINE_ARM64;
    g_arm64ec_emu.guest_machine = IMAGE_FILE_MACHINE_AMD64;
    g_arm64ec_emu.initialized = 1U;
    g_arm64ec_emu.processor_feature_mask = (ULONG)NTDLL_ARM64EC_DEFAULT_FEATURES;
    return STATUS_SUCCESS;
}

NTSTATUS WINAPI NtdllArm64EcQueryEmulatorInfo(NTDLL_ARM64EC_EMULATOR_INFO *Info)
{
    if (is_stub_simulation_enabled()) {
        return STATUS_NOT_IMPLEMENTED;
    }
    if (Info == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    *Info = g_arm64ec_emu;
    return STATUS_SUCCESS;
}

BOOLEAN WINAPI BTCpu64IsProcessorFeaturePresent(ULONG Feature)
{
    static const ULONGLONG supported_mask =
        NTDLL_ARM64EC_DEFAULT_FEATURES |
        (1ULL << 32) | /* PF_RDTSCP_INSTRUCTION_AVAILABLE */
        (1ULL << 36) | /* PF_SSSE3_INSTRUCTIONS_AVAILABLE */
        (1ULL << 37) | /* PF_SSE4_1_INSTRUCTIONS_AVAILABLE */
        (1ULL << 38);  /* PF_SSE4_2_INSTRUCTIONS_AVAILABLE */

    if (is_stub_simulation_enabled() || Feature >= 64U) {
        return 0;
    }
    return (supported_mask & (1ULL << Feature)) ? 1 : 0;
}

void WINAPI UpdateProcessorInformation(NTDLL_SYSTEM_CPU_INFORMATION *Info)
{
    if (Info == NULL) {
        return;
    }
    Info->ProcessorArchitecture = 9U; /* PROCESSOR_ARCHITECTURE_AMD64 */
    Info->ProcessorLevel = 21U;
    Info->ProcessorRevision = 1U;
}

void WINAPI BTCpu64FlushInstructionCache(LPCVOID BaseAddress, SIZE_T Size)
{
#ifdef __APPLE__
    if (BaseAddress != NULL && Size > 0) {
        sys_icache_invalidate((void *)(uintptr_t)BaseAddress, Size);
    }
#else
    (void)BaseAddress;
    (void)Size;
#endif
    g_arm64ec_emu.jit_icache_flushes++;
}

NTSTATUS WINAPI NtdllArm64EcHandleSuspendDoorbell(
    NTDLL_CHPE_V2_CPU_AREA_INFO *ChpeInfo,
    NTDLL_ARM64_NT_CONTEXT *Arm64Context)
{
    if (is_stub_simulation_enabled()) {
        return STATUS_NOT_IMPLEMENTED;
    }
    if (ChpeInfo == NULL || Arm64Context == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    /*
     * Matches upstream unix/signal_arm64.c suspend_ handler logic:
     * When the thread is inside the x86_64 JIT simulator (InSimulation != 0)
     * and SuspendDoorbell is non-NULL, trigger the doorbell instead of
     * corrupting mid-block JIT state, and mark incomplete frame context.
     */
    if (ChpeInfo->InSimulation && ChpeInfo->SuspendDoorbell != 0) {
        ChpeInfo->SuspendTriggeredCount++;
        Arm64Context->RestoreFlags |= NTDLL_RESTORE_FLAGS_INCOMPLETE_FRAME_CONTEXT;
        return STATUS_SUCCESS;
    }
    return STATUS_UNSUCCESSFUL;
}

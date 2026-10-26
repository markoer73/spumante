/*
 * Upstream Wine winetest anti-regression suite for ntdll.dll
 * (Apple Silicon arm64 & Intel x86_64/i386 Execution Parity:
 *  virtual.c 16KB/4KB sub-page PAGE_GUARD & MEM_WRITE_WATCH, MAP_JIT W^X,
 *  signal_arm64.c Darwin x18 & ARM64EC fexcore/box64cpu, and signal_x86_64.c WoW64/LDT/Rosetta 2)
 *
 * Adapted from upstream Wine dlls/ntdll/tests/virtual.c and dlls/ntdll/tests/wow64.c
 * Copyright 2003-2024 Alexandre Julliard
 * Copyright 2026 Spumante Contributors
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "../../include/wine/test.h"
#include "../../../dlls/ntdll/ntdll.h"

static void test_NtAllocateVirtualMemory(void)
{
    PVOID base = NULL;
    PVOID sub_base = NULL;
    SIZE_T size = 0;
    SIZE_T sub_size = 0;
    ULONG old_prot = 0;
    NTDLL_MEMORY_BASIC_INFORMATION mbi;
    SIZE_T ret_len = 0;
    NTSTATUS status;

    NtdllResetVirtualState();
    status = NtdllSetSimulatedHostPageSize(NTDLL_APPLE_SILICON_HOST_PAGE_SIZE);
    ok(status == STATUS_SUCCESS, "NtdllSetSimulatedHostPageSize failed: 0x%08x\n", status);

    /* Parameter validation */
    size = 0x4000;
    status = NtAllocateVirtualMemory(NULL, &base, 0, &size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    ok(status == STATUS_INVALID_HANDLE, "got 0x%08x\n", status);

    size = 0;
    status = NtAllocateVirtualMemory(NtCurrentProcess(), &base, 0, &size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    ok(status == STATUS_INVALID_PARAMETER, "got 0x%08x\n", status);

    size = 0x4000;
    status = NtAllocateVirtualMemory(NtCurrentProcess(), &base, 0, &size, MEM_RESERVE | MEM_COMMIT, PAGE_NOACCESS | PAGE_GUARD);
    ok(status == STATUS_INVALID_PAGE_PROTECTION, "got 0x%08x\n", status);

    /* Reserve 64 KB (16 x 4 KB Windows pages across 4 x 16 KB Apple Silicon host pages) */
    base = NULL;
    size = 0x10000;
    status = NtAllocateVirtualMemory(NtCurrentProcess(), &base, 0, &size, MEM_RESERVE, PAGE_READWRITE);
    ok(status == STATUS_SUCCESS, "NtAllocateVirtualMemory reserve failed: 0x%08x\n", status);
    ok(base != NULL && ((uintptr_t)base & 0xffffU) == 0, "base not 64KB aligned: %p\n", base);

    /* Commit first 16 KB (4 x 4 KB sub-pages) */
    sub_base = base;
    sub_size = 0x4000;
    status = NtAllocateVirtualMemory(NtCurrentProcess(), &sub_base, 0, &sub_size, MEM_COMMIT, PAGE_READWRITE);
    ok(status == STATUS_SUCCESS, "NtAllocateVirtualMemory commit failed: 0x%08x\n", status);

    /* Protect 4 KB sub-pages 0 and 1 with PAGE_GUARD inside the same 16 KB host page */
    sub_base = base;
    sub_size = 0x2000;
    status = NtProtectVirtualMemory(NtCurrentProcess(), &sub_base, &sub_size, PAGE_READWRITE | PAGE_GUARD, &old_prot);
    ok(status == STATUS_SUCCESS, "NtProtectVirtualMemory failed: 0x%08x\n", status);
    ok(old_prot == PAGE_READWRITE, "wrong old_prot 0x%x\n", old_prot);

    /* Fault on sub-page 0 (+0x0000): must return STATUS_GUARD_PAGE_VIOLATION once */
    status = NtdllHandleVirtualFault((uint8_t *)base + 0x0100, NTDLL_FAULT_WRITE, 0);
    ok(status == STATUS_GUARD_PAGE_VIOLATION, "expected STATUS_GUARD_PAGE_VIOLATION, got 0x%08x\n", status);

    status = NtdllHandleVirtualFault((uint8_t *)base + 0x0100, NTDLL_FAULT_WRITE, 0);
    ok(status == STATUS_SUCCESS, "expected STATUS_SUCCESS on second access, got 0x%08x\n", status);

    /* Sibling 4 KB sub-page 1 (+0x1000) in the same 16 KB host page must still be guarded */
    memset(&mbi, 0, sizeof(mbi));
    status = NtQueryVirtualMemory(NtCurrentProcess(), (uint8_t *)base + 0x1000,
                                  MemoryBasicInformation, &mbi, sizeof(mbi), &ret_len);
    ok(status == STATUS_SUCCESS, "NtQueryVirtualMemory failed: 0x%08x\n", status);
    ok(mbi.State == MEM_COMMIT, "wrong state 0x%x\n", mbi.State);
    ok(mbi.Protect == (PAGE_READWRITE | PAGE_GUARD), "sibling 4KB sub-page lost PAGE_GUARD: 0x%x\n", mbi.Protect);

    status = NtdllHandleVirtualFault((uint8_t *)base + 0x1200, NTDLL_FAULT_READ, 0);
    ok(status == STATUS_GUARD_PAGE_VIOLATION, "expected STATUS_GUARD_PAGE_VIOLATION on sibling sub-page 1, got 0x%08x\n", status);

    /* Decommit sub-page 2 (+0x2000) */
    sub_base = (uint8_t *)base + 0x2000;
    sub_size = 0x1000;
    status = NtFreeVirtualMemory(NtCurrentProcess(), &sub_base, &sub_size, MEM_DECOMMIT);
    ok(status == STATUS_SUCCESS, "NtFreeVirtualMemory decommit failed: 0x%08x\n", status);

    status = NtdllHandleVirtualFault((uint8_t *)base + 0x2000, NTDLL_FAULT_READ, 0);
    ok(status == STATUS_ACCESS_VIOLATION, "expected STATUS_ACCESS_VIOLATION on decommitted sub-page, got 0x%08x\n", status);

    /* Release with non-base pointer must fail with STATUS_FREE_VM_NOT_AT_BASE */
    sub_base = (uint8_t *)base + 0x1000;
    sub_size = 0;
    status = NtFreeVirtualMemory(NtCurrentProcess(), &sub_base, &sub_size, MEM_RELEASE);
    ok(status == STATUS_FREE_VM_NOT_AT_BASE, "got 0x%08x\n", status);

    size = 0;
    status = NtFreeVirtualMemory(NtCurrentProcess(), &base, &size, MEM_RELEASE);
    ok(status == STATUS_SUCCESS, "NtFreeVirtualMemory release failed: 0x%08x\n", status);
}

static void test_write_watch(void)
{
    PVOID base = NULL;
    SIZE_T size = 0x10000;
    PVOID results[64];
    ULONG_PTR count;
    ULONG granularity = 0;
    NTDLL_MEMORY_RANGE_ENTRY range;
    ULONG dirty_state = 0;
    NTSTATUS status;
    uint8_t *bytes;

    NtdllResetVirtualState();
    (void)NtdllSetSimulatedHostPageSize(NTDLL_APPLE_SILICON_HOST_PAGE_SIZE);

    status = NtAllocateVirtualMemory(NtCurrentProcess(), &base, 0, &size,
                                     MEM_RESERVE | MEM_COMMIT | MEM_WRITE_WATCH, PAGE_READWRITE);
    ok(status == STATUS_SUCCESS, "NtAllocateVirtualMemory MEM_WRITE_WATCH failed: 0x%08x\n", status);

    /* Upstream parameter validation checks */
    count = 64;
    status = NtGetWriteWatch(NULL, 0, base, size, results, &count, &granularity);
    ok(status == STATUS_INVALID_HANDLE, "got 0x%08x\n", status);

    status = NtGetWriteWatch(NtCurrentProcess(), 0, base, size, results, NULL, &granularity);
    ok(status == STATUS_ACCESS_VIOLATION, "got 0x%08x\n", status);

    status = NtGetWriteWatch(NtCurrentProcess(), 0, base, size, results, &count, NULL);
    ok(status == STATUS_ACCESS_VIOLATION, "got 0x%08x\n", status);

    status = NtGetWriteWatch(NtCurrentProcess(), 0xdeadbeefU, base, size, results, &count, &granularity);
    ok(status == STATUS_INVALID_PARAMETER_2, "got 0x%08x\n", status);

    status = NtGetWriteWatch(NtCurrentProcess(), 0, NULL, size, results, &count, &granularity);
    ok(status == STATUS_INVALID_PARAMETER_3, "got 0x%08x\n", status);

    status = NtGetWriteWatch(NtCurrentProcess(), 0, base, 0, results, &count, &granularity);
    ok(status == STATUS_INVALID_PARAMETER_4, "got 0x%08x\n", status);

    count = 0;
    status = NtGetWriteWatch(NtCurrentProcess(), 0, base, size, results, &count, &granularity);
    ok(status == STATUS_INVALID_PARAMETER_5, "got 0x%08x\n", status);

    count = 64;
    status = NtGetWriteWatch(NtCurrentProcess(), 0, base, size, NULL, &count, &granularity);
    ok(status == STATUS_INVALID_PARAMETER_5, "got 0x%08x\n", status);

    /* Initially clean */
    count = 64;
    status = NtGetWriteWatch(NtCurrentProcess(), 0, base, size, results, &count, &granularity);
    ok(status == STATUS_SUCCESS, "NtGetWriteWatch failed: 0x%08x\n", status);
    ok(count == 0, "expected 0 dirty pages, got %u\n", (unsigned int)count);
    ok(granularity == 0x1000U, "expected 4096 granularity, got %u\n", (unsigned int)granularity);

    /* Direct C memory write to 4 KB sub-page 1 (+0x1000) and fault on sub-page 3 (+0x3000) */
    bytes = (uint8_t *)base;
    bytes[0x1000] = 0x5a;
    status = NtdllHandleVirtualFault(bytes + 0x3100, NTDLL_FAULT_WRITE, 0);
    ok(status == STATUS_SUCCESS, "NtdllHandleVirtualFault failed: 0x%08x\n", status);

    count = 64;
    status = NtGetWriteWatch(NtCurrentProcess(), 0, base, size, results, &count, &granularity);
    ok(status == STATUS_SUCCESS, "NtGetWriteWatch failed: 0x%08x\n", status);
    ok(count == 2, "expected 2 dirty 4KB pages, got %u\n", (unsigned int)count);
    ok(results[0] == bytes + 0x1000, "wrong page 0: %p\n", results[0]);
    ok(results[1] == bytes + 0x3000, "wrong page 1: %p\n", results[1]);

    /* Reset via NtResetWriteWatch */
    status = NtResetWriteWatch(NtCurrentProcess(), base, size);
    ok(status == STATUS_SUCCESS, "NtResetWriteWatch failed: 0x%08x\n", status);

    count = 64;
    status = NtGetWriteWatch(NtCurrentProcess(), 0, base, size, results, &count, &granularity);
    ok(status == STATUS_SUCCESS && count == 0, "expected 0 dirty pages after reset, got %u\n", (unsigned int)count);

    /* Mark sub-page 2 (+0x2000) dirty via NtSetInformationVirtualMemory(VmPageDirtyStateInformation) */
    range.VirtualAddress = bytes + 0x2000;
    range.NumberOfBytes = 0x1000;
    dirty_state = 1;
    status = NtSetInformationVirtualMemory(NtCurrentProcess(), VmPageDirtyStateInformation,
                                           1, &range, &dirty_state, sizeof(dirty_state));
    ok(status == STATUS_SUCCESS, "NtSetInformationVirtualMemory failed: 0x%08x\n", status);

    count = 64;
    status = NtGetWriteWatch(NtCurrentProcess(), WRITE_WATCH_FLAG_RESET, base, size, results, &count, &granularity);
    ok(status == STATUS_SUCCESS && count == 1 && results[0] == bytes + 0x2000,
       "expected sub-page 2 dirty, got count=%u\n", (unsigned int)count);

    size = 0;
    status = NtFreeVirtualMemory(NtCurrentProcess(), &base, &size, MEM_RELEASE);
    ok(status == STATUS_SUCCESS, "NtFreeVirtualMemory failed: 0x%08x\n", status);
}

static void test_rosetta2_and_exec_write_exceptions(void)
{
    PVOID base = NULL;
    SIZE_T size = 0x4000;
    PVOID sub_page = NULL;
    SIZE_T sub_size = 0x1000;
    ULONG old_prot = 0;
    NTDLL_PROCESS_EXEC_WRITE_STATES proc_state;
    NTDLL_THREAD_EXEC_WRITE_STATES thread_state;
    NTDLL_JIT_WX_STATE wx_state;
    NTSTATUS status;

    NtdllResetVirtualState();
    status = NtAllocateVirtualMemory(NtCurrentProcess(), &base, 0, &size,
                                     MEM_RESERVE | MEM_COMMIT | MEM_WRITE_WATCH, PAGE_READWRITE);
    ok(status == STATUS_SUCCESS, "NtAllocateVirtualMemory failed: 0x%08x\n", status);

    /* Rosetta 2 read-fault on write-watched page is promoted to write fault */
    status = NtdllHandleVirtualFault((uint8_t *)base + 0x0200, NTDLL_FAULT_READ, 1);
    ok(status == STATUS_SUCCESS, "Rosetta 2 read-fault promotion failed: 0x%08x\n", status);

    /* Protect sub-page 1 as PAGE_WRITECOPY and trigger Rosetta 2 read fault -> promoted to write-copy */
    sub_page = (uint8_t *)base + 0x1000;
    sub_size = 0x1000;
    status = NtProtectVirtualMemory(NtCurrentProcess(), &sub_page, &sub_size, PAGE_WRITECOPY, &old_prot);
    ok(status == STATUS_SUCCESS, "NtProtectVirtualMemory PAGE_WRITECOPY failed: 0x%08x\n", status);

    status = NtdllHandleVirtualFault((uint8_t *)base + 0x1100, NTDLL_FAULT_READ, 1);
    ok(status == STATUS_SUCCESS, "Rosetta 2 write-copy fault failed: 0x%08x\n", status);

    /* Protect sub-page 2 as PAGE_EXECUTE_READWRITE and test ProcessManageWritesToExecutableMemory */
    sub_page = (uint8_t *)base + 0x2000;
    sub_size = 0x1000;
    status = NtProtectVirtualMemory(NtCurrentProcess(), &sub_page, &sub_size, PAGE_EXECUTE_READWRITE, &old_prot);
    ok(status == STATUS_SUCCESS, "NtProtectVirtualMemory PAGE_EXECUTE_READWRITE failed: 0x%08x\n", status);

    memset(&proc_state, 0, sizeof(proc_state));
    proc_state.Version = 1;
    proc_state.EnableWriteExceptions = 1;
    proc_state.ThreadAllowWrites = 0;
    status = NtSetInformationProcess(NtCurrentProcess(), ProcessManageWritesToExecutableMemory,
                                     &proc_state, sizeof(proc_state));
    ok(status == STATUS_SUCCESS, "NtSetInformationProcess failed: 0x%08x\n", status);

    status = NtdllHandleVirtualFault((uint8_t *)base + 0x2000, NTDLL_FAULT_WRITE, 0);
    ok(status == STATUS_EXECUTABLE_MEMORY_WRITE, "expected STATUS_EXECUTABLE_MEMORY_WRITE, got 0x%08x\n", status);

    memset(&thread_state, 0, sizeof(thread_state));
    thread_state.Version = 1;
    thread_state.ThreadAllowWrites = 1;
    status = NtSetInformationThread(NtCurrentThread(), ThreadManageWritesToExecutableMemory,
                                    &thread_state, sizeof(thread_state));
    ok(status == STATUS_SUCCESS, "NtSetInformationThread failed: 0x%08x\n", status);

    status = NtdllHandleVirtualFault((uint8_t *)base + 0x2000, NTDLL_FAULT_WRITE, 0);
    ok(status == STATUS_SUCCESS, "expected STATUS_SUCCESS after ThreadAllowWrites=1, got 0x%08x\n", status);

    /* MAP_JIT W^X toggling & NtFlushInstructionCache */
    status = NtdllToggleJitWriteProtection(1);
    ok(status == STATUS_SUCCESS, "NtdllToggleJitWriteProtection(1) failed: 0x%08x\n", status);
    status = NtdllToggleJitWriteProtection(0);
    ok(status == STATUS_SUCCESS, "NtdllToggleJitWriteProtection(0) failed: 0x%08x\n", status);
    status = NtFlushInstructionCache(NtCurrentProcess(), sub_page, 128);
    ok(status == STATUS_SUCCESS, "NtFlushInstructionCache failed: 0x%08x\n", status);

    memset(&wx_state, 0, sizeof(wx_state));
    status = NtdllQueryJitWxState(&wx_state);
    ok(status == STATUS_SUCCESS, "NtdllQueryJitWxState failed: 0x%08x\n", status);
    ok(wx_state.map_jit_enabled == 1, "expected map_jit_enabled=1\n");
    ok(wx_state.wx_toggle_count == 2, "expected wx_toggle_count=2, got %u\n", wx_state.wx_toggle_count);
    ok(wx_state.icache_flush_count == 1, "expected icache_flush_count=1, got %u\n", wx_state.icache_flush_count);
    ok(wx_state.rosetta_read_fault_promoted_count == 2,
       "expected rosetta_read_fault_promoted_count=2, got %u\n", wx_state.rosetta_read_fault_promoted_count);

    size = 0;
    (void)NtFreeVirtualMemory(NtCurrentProcess(), &base, &size, MEM_RELEASE);
}

static void test_darwin_x18_and_arm64_context(void)
{
    NTDLL_DARWIN_X18_STATE state;
    NTDLL_ARM64_NT_CONTEXT ctx;
    NTSTATUS status;

    status = NtdllDarwinX18Init(0x000000016fdff000ULL, 0x000000007ffe0000ULL);
    ok(status == STATUS_SUCCESS, "NtdllDarwinX18Init failed: 0x%08x\n", status);

    status = NtdllDarwinX18EnterPe();
    ok(status == STATUS_SUCCESS, "NtdllDarwinX18EnterPe failed: 0x%08x\n", status);

    status = NtdllDarwinX18QueryState(&state);
    ok(status == STATUS_SUCCESS && state.active_x18 == 0x000000007ffe0000ULL && state.in_pe_mode == 1,
       "wrong PE x18 state\n");

    /* Signal interrupts PE execution: Darwin platform x18 must be restored inside handler */
    status = NtdllDarwinX18SignalEnter(0x000000007ffe0000ULL);
    ok(status == STATUS_SUCCESS, "NtdllDarwinX18SignalEnter failed: 0x%08x\n", status);
    (void)NtdllDarwinX18QueryState(&state);
    ok(state.active_x18 == 0x000000016fdff000ULL, "signal handler did not restore Darwin platform x18\n");

    status = NtdllDarwinX18SignalLeave();
    ok(status == STATUS_SUCCESS, "NtdllDarwinX18SignalLeave failed: 0x%08x\n", status);
    (void)NtdllDarwinX18QueryState(&state);
    ok(state.active_x18 == 0x000000007ffe0000ULL, "signal leave did not restore PE TEB x18\n");

    /* NtSetContextThread with CONTEXT_ARM64_INTEGER must skip X18 unless CONTEXT_ARM64_X18 is set */
    memset(&ctx, 0, sizeof(ctx));
    ctx.ContextFlags = NTDLL_CONTEXT_ARM64_INTEGER | NTDLL_CONTEXT_ARM64_CONTROL;
    ctx.X[0] = 0x12345678ULL;
    ctx.X[18] = 0xbadbad00ULL;
    ctx.Pc = 0x140001000ULL;
    ctx.Sp = 0x0010f000ULL;
    status = NtSetContextThread(NtCurrentThread(), &ctx);
    ok(status == STATUS_SUCCESS, "NtSetContextThread failed: 0x%08x\n", status);

    memset(&ctx, 0, sizeof(ctx));
    ctx.ContextFlags = NTDLL_CONTEXT_ARM64_INTEGER | NTDLL_CONTEXT_ARM64_CONTROL | NTDLL_CONTEXT_ARM64_X18;
    status = NtGetContextThread(NtCurrentThread(), &ctx);
    ok(status == STATUS_SUCCESS, "NtGetContextThread failed: 0x%08x\n", status);
    ok(ctx.X[0] == 0x12345678ULL, "wrong X0\n");
    ok(ctx.X[18] == 0x000000007ffe0000ULL, "X18 was clobbered without CONTEXT_ARM64_X18\n");
    ok((ctx.RestoreFlags & NTDLL_RESTORE_FLAGS_EMULATION) != 0, "RESTORE_FLAGS_EMULATION not set\n");

    /* Explicitly update X18 with CONTEXT_ARM64_X18 */
    ctx.ContextFlags = NTDLL_CONTEXT_ARM64_X18;
    ctx.X[18] = 0x000000007ffe4000ULL;
    status = NtSetContextThread(NtCurrentThread(), &ctx);
    ok(status == STATUS_SUCCESS, "NtSetContextThread(CONTEXT_ARM64_X18) failed: 0x%08x\n", status);

    status = NtdllDarwinX18ExitToUnix();
    ok(status == STATUS_SUCCESS, "NtdllDarwinX18ExitToUnix failed: 0x%08x\n", status);
    (void)NtdllDarwinX18QueryState(&state);
    ok(state.active_x18 == 0x000000016fdff000ULL && state.windows_teb_x18 == 0x000000007ffe4000ULL,
       "wrong Unix exit x18 state\n");
}

static void test_architectures(void)
{
    NTDLL_SYSTEM_CPU_INFORMATION cpu_info;
    SYSTEM_SUPPORTED_PROCESSOR_ARCHITECTURES_INFORMATION archs[8];
    HANDLE proc = NtCurrentProcess();
    ULONG ret_len = 0;
    USHORT current_mach = 0;
    USHORT target_mach = 0;
    BOOLEAN supported = 0;
    NTSTATUS status;

    memset(&cpu_info, 0, sizeof(cpu_info));
    status = RtlGetNativeSystemInformation(SystemCpuInformation, &cpu_info, sizeof(cpu_info), &ret_len);
    ok(status == STATUS_SUCCESS, "RtlGetNativeSystemInformation failed: 0x%08x\n", status);
    ok(ret_len == sizeof(cpu_info), "wrong ret_len %u\n", ret_len);

    status = RtlWow64GetProcessMachines(NtCurrentProcess(), &current_mach, &target_mach);
    ok(status == STATUS_SUCCESS, "RtlWow64GetProcessMachines failed: 0x%08x\n", status);
    ok(current_mach == IMAGE_FILE_MACHINE_I386 && target_mach == IMAGE_FILE_MACHINE_ARM64,
       "wrong machines %x / %x\n", current_mach, target_mach);

    status = RtlWow64IsWowGuestMachineSupported(IMAGE_FILE_MACHINE_I386, &supported);
    ok(status == STATUS_SUCCESS && supported == 1, "I386 should be supported\n");

    status = RtlWow64IsWowGuestMachineSupported(IMAGE_FILE_MACHINE_AMD64, &supported);
    ok(status == STATUS_SUCCESS && supported == 1, "AMD64 should be supported\n");

    status = RtlWow64IsWowGuestMachineSupported(IMAGE_FILE_MACHINE_ARM64, &supported);
    ok(status == STATUS_SUCCESS && supported == 0, "ARM64 is native, not a WoW guest machine\n");

    memset(archs, 0, sizeof(archs));
    status = NtQuerySystemInformationEx(SystemSupportedProcessorArchitectures,
                                        &proc, sizeof(proc), archs, sizeof(archs), &ret_len);
    ok(status == STATUS_SUCCESS, "NtQuerySystemInformationEx failed: 0x%08x\n", status);
    ok(archs[0].Machine == IMAGE_FILE_MACHINE_ARM64 && archs[0].Native == 1, "missing native ARM64 entry\n");
    ok(archs[1].Machine == IMAGE_FILE_MACHINE_AMD64 && archs[1].UserMode == 1, "missing AMD64 entry\n");
    ok(archs[2].Machine == IMAGE_FILE_MACHINE_I386 && archs[2].WoW64Container == 1, "missing I386 entry\n");
    ok(archs[3].Machine == 0, "missing terminating zero entry\n");
}

static void test_selectors(void)
{
    NTDLL_DESCRIPTOR_TABLE_ENTRY info;
    NTDLL_LDT_ENTRY ds_entry;
    NTDLL_LDT_ENTRY zero_entry;
    NTDLL_ROSETTA_GSBASE_STATE gs_state;
    ULONG base, limit, sel, retlen;
    NTSTATUS status;
    static const BYTE gs_instr[9] = { 0x65, 0x48, 0x8b, 0x04, 0x25, 0x30, 0x00, 0x00, 0x00 };
    static const BYTE non_gs_instr[4] = { 0x48, 0x89, 0xe5, 0xc3 };

    memset(&info, 0, sizeof(info));
    memset(&ds_entry, 0, sizeof(ds_entry));
    memset(&zero_entry, 0, sizeof(zero_entry));
    memset(&gs_state, 0, sizeof(gs_state));

    NtdllResetLdtState(0x000000007ffde000ULL);

    retlen = 0xdeadbeefU;
    info.Selector = 0;
    status = RtlWow64GetThreadSelectorEntry(NtCurrentThread(), &info, sizeof(info) - 1, &retlen);
    ok(status == STATUS_INFO_LENGTH_MISMATCH, "wrong status 0x%08x\n", status);
    ok(retlen == 0xdeadbeefU, "retlen modified: 0x%x\n", retlen);

    retlen = 0xdeadbeefU;
    status = RtlWow64GetThreadSelectorEntry(NtCurrentThread(), &info, sizeof(info) + 1, &retlen);
    ok(status == STATUS_INFO_LENGTH_MISMATCH, "wrong status 0x%08x\n", status);
    ok(retlen == 0xdeadbeefU, "retlen modified: 0x%x\n", retlen);

    retlen = 0xdeadbeefU;
    status = RtlWow64GetThreadSelectorEntry(NtCurrentThread(), NULL, 0, &retlen);
    ok(status == STATUS_INFO_LENGTH_MISMATCH, "wrong status 0x%08x\n", status);
    ok(retlen == 0xdeadbeefU, "retlen modified: 0x%x\n", retlen);

    status = RtlWow64GetThreadSelectorEntry(NtCurrentThread(), &info, sizeof(info), NULL);
    ok(status == STATUS_SUCCESS, "wrong status 0x%08x\n", status);

    for (info.Selector = 0; info.Selector < 0x100U; info.Selector++) {
        retlen = 0xdeadbeefU;
        status = RtlWow64GetThreadSelectorEntry(NtCurrentThread(), &info, sizeof(info), &retlen);
        base = (ULONG)info.Descriptor.BaseLow |
               ((ULONG)info.Descriptor.HighWord.Bytes.BaseMid << 16) |
               ((ULONG)info.Descriptor.HighWord.Bytes.BaseHi << 24);
        limit = (ULONG)info.Descriptor.LimitLow |
                ((ULONG)info.Descriptor.HighWord.Bits.LimitHi << 16);
        sel = info.Selector | 3U;

        if (sel == 0x03U) {
            ok(status == STATUS_SUCCESS && retlen == sizeof(info.Descriptor), "null selector failed\n");
            ok(base == 0 && limit == 0, "null selector non-zero base/limit\n");
        } else if (sel == 0x23U || sel == 0x1bU) {
            ok(status == STATUS_SUCCESS && retlen == sizeof(info.Descriptor), "CS32 selector failed\n");
            ok(base == 0 && limit == 0xfffffU && info.Descriptor.HighWord.Bits.Type == 0x1bU &&
               info.Descriptor.HighWord.Bits.Dpl == 3U && info.Descriptor.HighWord.Bits.Pres == 1U &&
               info.Descriptor.HighWord.Bits.Default_Big == 1U && info.Descriptor.HighWord.Bits.Granularity == 1U,
               "CS32 descriptor mismatch\n");
        } else if (sel == 0x2bU) {
            ok(status == STATUS_SUCCESS && retlen == sizeof(info.Descriptor), "DS selector failed\n");
            ok(base == 0 && limit == 0xfffffU && info.Descriptor.HighWord.Bits.Type == 0x13U &&
               info.Descriptor.HighWord.Bits.Dpl == 3U && info.Descriptor.HighWord.Bits.Pres == 1U &&
               info.Descriptor.HighWord.Bits.Default_Big == 1U && info.Descriptor.HighWord.Bits.Granularity == 1U,
               "DS descriptor mismatch\n");
            ds_entry = info.Descriptor;
        } else if (sel == 0x53U || sel == 0x3bU) {
            ok(status == STATUS_SUCCESS && retlen == sizeof(info.Descriptor), "FS32 TEB selector failed\n");
            ok(base == 0x7ffe0000U && limit == 0xfffU && info.Descriptor.HighWord.Bits.Type == 0x13U &&
               info.Descriptor.HighWord.Bits.Granularity == 0U,
               "FS32 TEB descriptor mismatch: base=0x%x limit=0x%x\n", base, limit);
        } else if (status == STATUS_SUCCESS) {
            ok(retlen == sizeof(info.Descriptor), "wrong retlen %u\n", retlen);
            ok(!(sel & 4U), "unexpected success for LDT selector 0x%x\n", sel);
        } else {
            ok(status == STATUS_UNSUCCESSFUL || ((sel & 4U) && status == STATUS_NO_LDT),
               "unexpected error 0x%08x for selector 0x%x\n", status, info.Selector);
            ok(retlen == 0xdeadbeefU, "retlen modified on error\n");
        }
    }

    status = NtSetLdtEntries(0, ds_entry, 0, zero_entry);
    ok(status == STATUS_SUCCESS, "NtSetLdtEntries(0) failed: 0x%08x\n", status);

    status = NtSetLdtEntries(0x000fU, ds_entry, 0x001fU, ds_entry);
    ok(status == STATUS_SUCCESS, "NtSetLdtEntries(0x0f, 0x1f) failed: 0x%08x\n", status);

    info.Selector = 0x000fU;
    memset(&info.Descriptor, 0x9a, sizeof(info.Descriptor));
    status = NtQueryInformationThread(NtCurrentThread(), ThreadDescriptorTableEntry, &info, sizeof(info), NULL);
    ok(status == STATUS_SUCCESS, "NtQueryInformationThread(0x0f) failed: 0x%08x\n", status);
    ok(memcmp(&ds_entry, &info.Descriptor, sizeof(ds_entry)) == 0, "LDT 0x0f descriptor mismatch\n");

    info.Selector = 0x001fU;
    memset(&info.Descriptor, 0x9a, sizeof(info.Descriptor));
    status = NtQueryInformationThread(NtCurrentThread(), ThreadDescriptorTableEntry, &info, sizeof(info), NULL);
    ok(status == STATUS_SUCCESS, "NtQueryInformationThread(0x1f) failed: 0x%08x\n", status);
    ok(memcmp(&ds_entry, &info.Descriptor, sizeof(ds_entry)) == 0, "LDT 0x1f descriptor mismatch\n");

    /* Rosetta 2 %gs: prefix fault fixup */
    (void)NtdllRosettaInitGsbase(0x000000007ffde000ULL, 1);
    (void)NtdllRosettaCorruptGsbase(0x0ULL);
    status = NtdllRosettaCheckInvalidGsbase(0x140001000ULL, non_gs_instr, sizeof(non_gs_instr), &gs_state);
    ok(status == STATUS_ACCESS_VIOLATION, "non-GS instruction should not trigger GSBASE fixup\n");

    status = NtdllRosettaCheckInvalidGsbase(0x140001000ULL, gs_instr, sizeof(gs_instr), &gs_state);
    ok(status == STATUS_SUCCESS && gs_state.current_gsbase == 0x000000007ffde000ULL,
       "Rosetta 2 GSBASE fixup failed: 0x%08x\n", status);
}

static void test_cpu_area(void)
{
    static const struct {
        USHORT machine;
        NTSTATUS expect;
        ULONG_PTR align;
        ULONG_PTR size;
        ULONG_PTR offset;
        ULONG flag;
    } tests[] = {
        { IMAGE_FILE_MACHINE_I386,  STATUS_SUCCESS,           4U,  0x2ccU, 0x00U, 0x00010000U },
        { IMAGE_FILE_MACHINE_AMD64, STATUS_SUCCESS,          16U,  0x4d0U, 0x30U, 0x00100000U },
        { IMAGE_FILE_MACHINE_ARMNT, STATUS_SUCCESS,           8U,  0x1a0U, 0x00U, 0x00200000U },
        { IMAGE_FILE_MACHINE_ARM64, STATUS_SUCCESS,          16U,  0x390U, 0x00U, 0x00400000U },
        { 0x01c0U /* ARM */,        STATUS_INVALID_PARAMETER, 0U,  0U,     0U,    0U },
        { 0x01c2U /* THUMB */,      STATUS_INVALID_PARAMETER, 0U,  0U,     0U,    0U },
        { IMAGE_FILE_MACHINE_ARM64EC, STATUS_INVALID_PARAMETER, 0U, 0U,    0U,    0U }
    };
    USHORT buffer[2048];
    WOW64_CPURESERVED *cpu;
    WOW64_CPU_AREA_INFO info;
    size_t i;
    ULONG j;
    NTSTATUS status;

    memset(buffer, 0, sizeof(buffer));

    for (i = 0; i < ARRAY_SIZE(tests); i++) {
        for (j = 0; j < 8; j++) {
            void *expected_ctx;
            void *expected_ctx_ex;

            cpu = (WOW64_CPURESERVED *)(void *)(buffer + j);
            cpu->Flags = 0;
            cpu->Machine = tests[i].machine;
            memset(&info, 0, sizeof(info));

            status = RtlWow64GetCpuAreaInfo(cpu, 0, &info);
            ok(status == tests[i].expect, "%zu:%u: wrong status 0x%08x\n", i, j, status);
            if (status != STATUS_SUCCESS) {
                continue;
            }

            expected_ctx = (void *)(((uintptr_t)(cpu + 1) + tests[i].align - 1U)
                                    & ~(uintptr_t)(tests[i].align - 1U));
            expected_ctx_ex = (void *)(((uintptr_t)((char *)info.Context + tests[i].size) + sizeof(void *) - 1U)
                                       & ~(uintptr_t)(sizeof(void *) - 1U));

            ok(info.Context == expected_ctx, "%zu:%u: wrong Context %p != %p\n", i, j, info.Context, expected_ctx);
            ok(info.ContextEx == expected_ctx_ex, "%zu:%u: wrong ContextEx\n", i, j);
            ok(info.ContextFlagsLocation == (char *)info.Context + tests[i].offset,
               "%zu:%u: wrong ContextFlagsLocation\n", i, j);
            ok(info.CpuReserved == cpu, "%zu:%u: wrong CpuReserved\n", i, j);
            ok(info.ContextFlag == tests[i].flag, "%zu:%u: wrong ContextFlag 0x%08x\n", i, j, info.ContextFlag);
            ok(info.Machine == tests[i].machine, "%zu:%u: wrong Machine 0x%x\n", i, j, info.Machine);
        }
    }
}

static void test_cross_process_work_list(void)
{
    struct {
        CROSS_PROCESS_WORK_HDR free_list;
        CROSS_PROCESS_WORK_HDR work_list;
        CROSS_PROCESS_WORK_ENTRY entries[10];
    } list;
    ULONG i, next, count = 10;
    BOOLEAN res, flush;
    CROSS_PROCESS_WORK_ENTRY *ptr = NULL;
    CROSS_PROCESS_WORK_ENTRY *ret = NULL;
    NTDLL_SYSTEM_CPU_INFORMATION cpu_info;
    NTDLL_ARM64EC_EMULATOR_INFO emu_info;

    memset(&list, 0, sizeof(list));

    for (i = 0; i < count; i++) {
        res = RtlWow64PushCrossProcessWorkOntoFreeList(&list.free_list, &list.entries[i]);
        ok(res == 1, "%u: RtlWow64PushCrossProcessWorkOntoFreeList failed\n", i);
    }

    ok(list.free_list.counter == count, "wrong counter %u\n", list.free_list.counter);
    ok(CROSS_PROCESS_LIST_ENTRY(&list.free_list, list.free_list.first) == &list.entries[count - 1],
       "wrong first offset %u\n", list.free_list.first);
    for (i = count; i > 1; i--) {
        ok(CROSS_PROCESS_LIST_ENTRY(&list.free_list, list.entries[i - 1].next) == &list.entries[i - 2],
           "%u: wrong next offset\n", i);
    }
    ok(list.entries[0].next == 0, "wrong last next %x\n", list.entries[0].next);

    next = list.entries[count - 1].next;
    ptr = RtlWow64PopCrossProcessWorkFromFreeList(&list.free_list);
    ok(ptr == &list.entries[count - 1], "wrong popped ptr\n");
    ok(ptr->next == 0, "next not reset\n");
    ok(list.free_list.first == next, "wrong free_list.first\n");
    ok(list.free_list.counter == count + 1, "wrong counter %u\n", list.free_list.counter);

    ptr->next = 0xdeadU;
    ptr->id = 3U;
    ptr->addr = 0xdeadbeefULL;
    ptr->size = 0x1000ULL;
    ptr->args[0] = 7U;
    ret = (CROSS_PROCESS_WORK_ENTRY *)(uintptr_t)0xdeadbeefULL;
    res = RtlWow64PushCrossProcessWorkOntoWorkList(&list.work_list, ptr, (void **)&ret);
    ok(res == 1 && ret == NULL, "RtlWow64PushCrossProcessWorkOntoWorkList failed\n");
    ok(list.work_list.counter == 1U, "wrong work_list.counter %u\n", list.work_list.counter);
    ok(ptr == CROSS_PROCESS_LIST_ENTRY(&list.work_list, list.work_list.first), "wrong first work entry\n");
    ok(ptr->next == 0, "got non-zero next\n");

    next = list.work_list.first;
    ptr = RtlWow64PopCrossProcessWorkFromFreeList(&list.free_list);
    ok(list.free_list.counter == count + 2, "wrong counter\n");
    ptr->id = 20U;
    ptr->addr = 0x123456ULL;
    ptr->size = 0x2345ULL;
    res = RtlWow64PushCrossProcessWorkOntoWorkList(&list.work_list, ptr, (void **)&ret);
    ok(res == 1 && ret == NULL, "second push failed\n");
    ok(list.work_list.counter == 2U, "wrong counter %u\n", list.work_list.counter);
    ok(ptr->next == next, "wrong next link\n");

    flush = 0xccU;
    ptr = RtlWow64PopAllCrossProcessWorkFromWorkList(&list.work_list, &flush);
    ok(flush == 0, "flush should be FALSE\n");
    ok(list.work_list.counter == 3U && list.work_list.first == 0, "work_list not cleared\n");
    ok(ptr != NULL && ptr->addr == 0xdeadbeefULL && ptr->size == 0x1000ULL && ptr->next != 0,
       "first FIFO item mismatch\n");

    ptr = CROSS_PROCESS_LIST_ENTRY(&list.work_list, ptr->next);
    ok(ptr->addr == 0x123456ULL && ptr->size == 0x2345ULL && ptr->next == 0,
       "second FIFO item mismatch\n");

    res = RtlWow64PushCrossProcessWorkOntoWorkList(&list.work_list, ptr, (void **)&ret);
    ok(res == 1 && list.work_list.counter == 4U, "push failed\n");

    res = RtlWow64RequestCrossProcessHeavyFlush(&list.work_list);
    ok(res == 1 && list.work_list.counter == 5U, "heavy flush failed\n");
    ok((list.work_list.first & CROSS_PROCESS_LIST_FLUSH) != 0, "CROSS_PROCESS_LIST_FLUSH bit not set\n");

    flush = 0xccU;
    ptr = RtlWow64PopAllCrossProcessWorkFromWorkList(&list.work_list, &flush);
    ok(flush == 1 && list.work_list.counter == 6U && list.work_list.first == 0, "pop after flush failed\n");
    ok(ptr != NULL && ptr->addr == 0x123456ULL && ptr->next == 0, "wrong popped item\n");

    flush = 0xccU;
    ptr = RtlWow64PopAllCrossProcessWorkFromWorkList(&list.work_list, &flush);
    ok(flush == 0 && list.work_list.counter == 6U && ptr == NULL, "empty pop modified counter\n");

    /* ARM64EC emulator module checks (fexcore.dll, box64cpu.dll, xtajit64.dll) */
    ok(NtdllArm64EcSelectEmulator("box64cpu.dll") == STATUS_SUCCESS, "box64cpu.dll selection failed\n");
    (void)NtdllArm64EcQueryEmulatorInfo(&emu_info);
    ok(strcmp(emu_info.module_name, "box64cpu.dll") == 0, "wrong module_name %s\n", emu_info.module_name);

    ok(BTCpu64IsProcessorFeaturePresent(2) == 1 && BTCpu64IsProcessorFeaturePresent(38) == 1,
       "BTCpu64IsProcessorFeaturePresent missing expected x86_64 feature\n");

    memset(&cpu_info, 0xcc, sizeof(cpu_info));
    cpu_info.ProcessorArchitecture = 12U;
    UpdateProcessorInformation(&cpu_info);
    ok(cpu_info.ProcessorArchitecture == 9U && cpu_info.ProcessorLevel == 21U &&
       cpu_info.ProcessorRevision == 1U && cpu_info.MaximumProcessors == 0xccccU,
       "UpdateProcessorInformation mismatch\n");
}

static void test_event(void)
{
    HANDLE event = NULL;
    HANDLE event2 = NULL;
    LONG prev_state = 0xdeadbeef;
    NTSTATUS status;
    UNICODE_STRING str;
    OBJECT_ATTRIBUTES attr;
    EVENT_BASIC_INFORMATION info;
    static const WCHAR ev_name[] = { '\\','B','a','s','e','N','a','m','e','d','O','b','j','e','c','t','s','\\','t','e','s','t','E','v','e','n','t',0 };

    NtdllResetSyncState();
    RtlInitUnicodeString(&str, ev_name);
    InitializeObjectAttributes(&attr, &str, 0, 0, NULL);

    status = NtCreateEvent(&event, EVENT_ALL_ACCESS, &attr, (EVENT_TYPE)2, 0);
    ok(status == STATUS_INVALID_PARAMETER, "NtCreateEvent failed %08x\n", (unsigned int)status);

    status = NtCreateEvent(&event, EVENT_ALL_ACCESS, &attr, NotificationEvent, 0);
    ok(status == STATUS_SUCCESS, "NtCreateEvent failed %08x\n", (unsigned int)status);
    memset(&info, 0xcc, sizeof(info));
    status = NtQueryEvent(event, EventBasicInformation, &info, sizeof(info), NULL);
    ok(status == STATUS_SUCCESS, "NtQueryEvent failed %08x\n", (unsigned int)status);
    ok(info.EventType == NotificationEvent && info.EventState == 0,
       "NtQueryEvent failed, expected 0 0, got %d %d\n", (int)info.EventType, (int)info.EventState);
    NtClose(event);

    status = NtCreateEvent(&event, EVENT_ALL_ACCESS, &attr, SynchronizationEvent, 0);
    ok(status == STATUS_SUCCESS, "NtCreateEvent failed %08x\n", (unsigned int)status);

    status = NtPulseEvent(event, &prev_state);
    ok(status == STATUS_SUCCESS, "NtPulseEvent failed %08x\n", (unsigned int)status);
    ok(!prev_state, "prev_state = %x\n", (unsigned int)prev_state);

    status = NtOpenEvent(&event2, EVENT_ALL_ACCESS, &attr);
    ok(status == STATUS_SUCCESS, "NtOpenEvent failed %08x\n", (unsigned int)status);

    NtClose(event);
    event = event2;

    status = NtSetEvent(event, &prev_state);
    ok(status == STATUS_SUCCESS && prev_state == 0, "NtSetEvent failed: %08x\n", (unsigned int)status);

    memset(&info, 0xcc, sizeof(info));
    status = NtQueryEvent(event, EventBasicInformation, &info, sizeof(info), NULL);
    ok(status == STATUS_SUCCESS && info.EventType == SynchronizationEvent && info.EventState == 1,
       "NtQueryEvent failed\n");

    status = NtSetEvent(event, &prev_state);
    ok(status == STATUS_SUCCESS && prev_state == 1, "NtSetEvent failed\n");

    status = NtResetEvent(event, &prev_state);
    ok(status == STATUS_SUCCESS && prev_state == 1, "NtResetEvent failed\n");

    status = NtResetEvent(event, &prev_state);
    ok(status == STATUS_SUCCESS && prev_state == 0, "NtResetEvent failed\n");

    status = NtSetEventBoostPriority(event);
    ok(status == STATUS_SUCCESS, "NtSetEventBoostPriority failed: %08x\n", (unsigned int)status);

    memset(&info, 0xcc, sizeof(info));
    status = NtQueryEvent(event, EventBasicInformation, &info, sizeof(info), NULL);
    ok(status == STATUS_SUCCESS && info.EventState == 1, "expected EventState == 1\n");

    NtClose(event);
}

static void test_semaphore(void)
{
    SEMAPHORE_BASIC_INFORMATION info;
    OBJECT_ATTRIBUTES attr;
    UNICODE_STRING str;
    NTSTATUS status;
    HANDLE semaphore = NULL;
    LONG prev = 0;
    ULONG len = 0;
    LARGE_INTEGER zero_to;
    static const WCHAR sem_name[] = { '\\','B','a','s','e','N','a','m','e','d','O','b','j','e','c','t','s','\\','t','e','s','t','_','s','e','m',0 };

    zero_to.QuadPart = 0;
    RtlInitUnicodeString(&str, sem_name);
    InitializeObjectAttributes(&attr, &str, 0, 0, NULL);

    status = NtCreateSemaphore(&semaphore, SEMAPHORE_ALL_ACCESS, &attr, 2, 1);
    ok(status == STATUS_INVALID_PARAMETER, "Failed to reject invalid Semaphore(%08x)\n", (unsigned int)status);
    status = NtCreateSemaphore(&semaphore, SEMAPHORE_ALL_ACCESS, &attr, 1, 2);
    ok(status == STATUS_SUCCESS, "Failed to create Semaphore(%08x)\n", (unsigned int)status);

    status = NtQuerySemaphore(semaphore, SemaphoreBasicInformation, &info, 0, NULL);
    ok(status == STATUS_INFO_LENGTH_MISMATCH, "expected STATUS_INFO_LENGTH_MISMATCH, got %08x\n", (unsigned int)status);
    status = NtQuerySemaphore(semaphore, (SEMAPHORE_INFORMATION_CLASS)0x42, &info, sizeof(info), NULL);
    ok(status == STATUS_INVALID_INFO_CLASS, "expected STATUS_INVALID_INFO_CLASS, got %08x\n", (unsigned int)status);
    status = NtQuerySemaphore((HANDLE)(uintptr_t)0xdeadbeefU, SemaphoreBasicInformation, &info, sizeof(info), NULL);
    ok(status == STATUS_INVALID_HANDLE, "expected STATUS_INVALID_HANDLE, got %08x\n", (unsigned int)status);

    len = 0;
    memset(&info, 0xcc, sizeof(info));
    status = NtQuerySemaphore(semaphore, SemaphoreBasicInformation, &info, sizeof(info), &len);
    ok(status == STATUS_SUCCESS && info.CurrentCount == 1 && info.MaximumCount == 2 && len == sizeof(info),
       "NtQuerySemaphore failed\n");

    status = NtWaitForSingleObject(semaphore, FALSE, &zero_to);
    ok(status == STATUS_WAIT_0, "NtWaitForSingleObject failed %08x\n", (unsigned int)status);

    prev = (LONG)0xdeadbeefU;
    status = NtReleaseSemaphore(semaphore, (ULONG)-1, &prev);
    ok(status == STATUS_INVALID_PARAMETER && prev == (LONG)0xdeadbeefU, "NtReleaseSemaphore(-1) failed\n");

    status = NtReleaseSemaphore(semaphore, 0, &prev);
    ok(status == STATUS_INVALID_PARAMETER && prev == (LONG)0xdeadbeefU, "NtReleaseSemaphore(0) failed\n");

    status = NtReleaseSemaphore(semaphore, 3, &prev);
    ok(status == STATUS_SEMAPHORE_LIMIT_EXCEEDED && prev == (LONG)0xdeadbeefU, "NtReleaseSemaphore(3) failed\n");

    status = NtReleaseSemaphore(semaphore, 1, &prev);
    ok(status == STATUS_SUCCESS && prev == 0, "NtReleaseSemaphore(1) failed\n");

    status = NtReleaseSemaphore(semaphore, 1, &prev);
    ok(status == STATUS_SUCCESS && prev == 1, "NtReleaseSemaphore(1) second failed\n");

    status = NtReleaseSemaphore(semaphore, 1, &prev);
    ok(status == STATUS_SEMAPHORE_LIMIT_EXCEEDED, "NtReleaseSemaphore overflow failed\n");

    NtClose(semaphore);
}

static void test_mutant(void)
{
    MUTANT_BASIC_INFORMATION info;
    OBJECT_ATTRIBUTES attr;
    UNICODE_STRING str;
    NTSTATUS status;
    HANDLE mutant = NULL;
    ULONG len = 0;
    LONG prev = 0;
    LARGE_INTEGER zero_to;
    static const WCHAR mut_name[] = { '\\','B','a','s','e','N','a','m','e','d','O','b','j','e','c','t','s','\\','t','e','s','t','_','m','u','t',0 };

    zero_to.QuadPart = 0;
    RtlInitUnicodeString(&str, mut_name);
    InitializeObjectAttributes(&attr, &str, 0, 0, NULL);
    status = NtCreateMutant(&mutant, MUTANT_ALL_ACCESS, &attr, TRUE);
    ok(status == STATUS_SUCCESS, "Failed to create Mutant(%08x)\n", (unsigned int)status);

    status = NtQueryMutant(mutant, MutantBasicInformation, &info, 0, NULL);
    ok(status == STATUS_INFO_LENGTH_MISMATCH, "expected STATUS_INFO_LENGTH_MISMATCH\n");
    status = NtQueryMutant(mutant, (MUTANT_INFORMATION_CLASS)0x42, &info, sizeof(info), NULL);
    ok(status == STATUS_INVALID_INFO_CLASS, "expected STATUS_INVALID_INFO_CLASS\n");
    status = NtQueryMutant((HANDLE)(uintptr_t)0xdeadbeefU, MutantBasicInformation, &info, sizeof(info), NULL);
    ok(status == STATUS_INVALID_HANDLE, "expected STATUS_INVALID_HANDLE\n");

    memset(&info, 0xcc, sizeof(info));
    status = NtQueryMutant(mutant, MutantBasicInformation, &info, sizeof(info), &len);
    ok(status == STATUS_SUCCESS && info.CurrentCount == 0 && info.OwnedByCaller == TRUE &&
       info.AbandonedState == FALSE && len == sizeof(info), "NtQueryMutant initial failed\n");

    status = NtWaitForSingleObject(mutant, FALSE, &zero_to);
    ok(status == STATUS_WAIT_0, "recursive wait failed\n");

    status = NtQueryMutant(mutant, MutantBasicInformation, &info, sizeof(info), NULL);
    ok(status == STATUS_SUCCESS && info.CurrentCount == -1 && info.OwnedByCaller == TRUE, "recursive query failed\n");

    status = NtReleaseMutant(mutant, &prev);
    ok(status == STATUS_SUCCESS && prev == -1, "first release failed\n");

    status = NtReleaseMutant(mutant, &prev);
    ok(status == STATUS_SUCCESS && prev == 0, "second release failed\n");

    status = NtReleaseMutant(mutant, &prev);
    ok(status == STATUS_MUTANT_NOT_OWNED, "unowned release should return STATUS_MUTANT_NOT_OWNED\n");

    /* Abandonment test */
    status = NtWaitForSingleObject(mutant, FALSE, &zero_to);
    ok(status == STATUS_WAIT_0, "re-acquire failed\n");
    (void)NtdllAbandonMutantsForThread(GetCurrentThreadId());

    status = NtQueryMutant(mutant, MutantBasicInformation, &info, sizeof(info), NULL);
    ok(status == STATUS_SUCCESS && info.CurrentCount == 1 && info.OwnedByCaller == FALSE && info.AbandonedState == TRUE,
       "abandoned query mismatch\n");

    status = NtWaitForSingleObject(mutant, FALSE, &zero_to);
    ok(status == STATUS_ABANDONED_WAIT_0, "expected STATUS_ABANDONED_WAIT_0, got 0x%08x\n", (unsigned int)status);

    status = NtQueryMutant(mutant, MutantBasicInformation, &info, sizeof(info), NULL);
    ok(status == STATUS_SUCCESS && info.CurrentCount == 0 && info.OwnedByCaller == TRUE && info.AbandonedState == FALSE,
       "post-abandoned query mismatch\n");

    NtReleaseMutant(mutant, NULL);
    NtClose(mutant);
}

static void test_keyed_events(void)
{
    OBJECT_ATTRIBUTES attr;
    UNICODE_STRING str;
    HANDLE handle = NULL;
    HANDLE event = NULL;
    NTSTATUS status;
    LARGE_INTEGER timeout;
    static const WCHAR ke_name[] = { '\\','B','a','s','e','N','a','m','e','d','O','b','j','e','c','t','s','\\','W','i','n','e','T','e','s','t','E','v','e','n','t',0 };

    InitializeObjectAttributes(&attr, &str, 0, 0, NULL);
    RtlInitUnicodeString(&str, ke_name);

    status = NtCreateKeyedEvent(&handle, STANDARD_RIGHTS_REQUIRED | 0x3U | SYNCHRONIZE, &attr, 0);
    ok(status == STATUS_SUCCESS, "NtCreateKeyedEvent failed %08x\n", (unsigned int)status);

    timeout.QuadPart = 0;
    status = NtWaitForSingleObject(handle, FALSE, &timeout);
    ok(status == STATUS_WAIT_0, "NtWaitForSingleObject on KeyedEvent with SYNCHRONIZE failed %08x\n", (unsigned int)status);

    status = NtWaitForKeyedEvent(handle, (const void *)(uintptr_t)255U, 0, &timeout);
    ok(status == STATUS_INVALID_PARAMETER_1, "NtWaitForKeyedEvent odd key %08x\n", (unsigned int)status);
    status = NtReleaseKeyedEvent(handle, (const void *)(uintptr_t)255U, 0, &timeout);
    ok(status == STATUS_INVALID_PARAMETER_1, "NtReleaseKeyedEvent odd key %08x\n", (unsigned int)status);

    status = NtWaitForKeyedEvent(handle, (const void *)(uintptr_t)254U, 0, &timeout);
    ok(status == STATUS_TIMEOUT, "NtWaitForKeyedEvent timeout %08x\n", (unsigned int)status);
    status = NtReleaseKeyedEvent(handle, (const void *)(uintptr_t)254U, 0, &timeout);
    ok(status == STATUS_TIMEOUT, "NtReleaseKeyedEvent timeout %08x\n", (unsigned int)status);

    status = NtWaitForKeyedEvent(NULL, (const void *)(uintptr_t)8U, 0, &timeout);
    ok(status == STATUS_TIMEOUT, "NtWaitForKeyedEvent(NULL) %08x\n", (unsigned int)status);
    status = NtReleaseKeyedEvent(NULL, (const void *)(uintptr_t)8U, 0, &timeout);
    ok(status == STATUS_TIMEOUT, "NtReleaseKeyedEvent(NULL) %08x\n", (unsigned int)status);

    status = NtWaitForKeyedEvent((HANDLE)(uintptr_t)0xdeadbeefU, (const void *)(uintptr_t)9U, 0, &timeout);
    ok(status == STATUS_INVALID_PARAMETER_1, "NtWaitForKeyedEvent bad handle + odd key %08x\n", (unsigned int)status);
    status = NtWaitForKeyedEvent((HANDLE)(uintptr_t)0xdeadbeefU, (const void *)(uintptr_t)8U, 0, &timeout);
    ok(status == STATUS_INVALID_HANDLE, "NtWaitForKeyedEvent bad handle %08x\n", (unsigned int)status);

    NtClose(handle);

    /* Access rights checks */
    status = NtCreateKeyedEvent(&handle, 0x0001U /* KEYEDEVENT_WAIT */, &attr, 0);
    ok(status == STATUS_SUCCESS, "NtCreateKeyedEvent(WAIT) failed\n");
    status = NtWaitForKeyedEvent(handle, (const void *)(uintptr_t)8U, 0, &timeout);
    ok(status == STATUS_TIMEOUT, "NtWaitForKeyedEvent %08x\n", (unsigned int)status);
    status = NtReleaseKeyedEvent(handle, (const void *)(uintptr_t)8U, 0, &timeout);
    ok(status == STATUS_ACCESS_DENIED, "expected STATUS_ACCESS_DENIED on wake with wait-only handle\n");
    status = NtWaitForSingleObject(handle, FALSE, &timeout);
    ok(status == STATUS_ACCESS_DENIED, "expected STATUS_ACCESS_DENIED on WaitForSingleObject without SYNCHRONIZE\n");

    status = NtPulseEvent(handle, NULL);
    ok(status == STATUS_OBJECT_TYPE_MISMATCH, "NtPulseEvent on KeyedEvent should return STATUS_OBJECT_TYPE_MISMATCH\n");

    status = NtCreateEvent(&event, EVENT_ALL_ACCESS, &attr, NotificationEvent, FALSE);
    ok(status == STATUS_OBJECT_TYPE_MISMATCH, "NtCreateEvent colliding with KeyedEvent should return STATUS_OBJECT_TYPE_MISMATCH\n");
    NtClose(handle);
}

static void test_wait_on_address(void)
{
    SIZE_T size;
    NTSTATUS status;
    LARGE_INTEGER start, end, timeout;
    LONGLONG address = 0;
    LONGLONG compare = 0;
    NTDLL_MACOS_NTSYNC_STATE sync_state;

    RtlWakeAddressSingle(NULL);
    RtlWakeAddressAll(NULL);

    status = RtlWaitOnAddress(&address, &compare, 5, NULL);
    ok(status == STATUS_INVALID_PARAMETER, "got 0x%08x\n", (unsigned int)status);

    NtQuerySystemTime(&start);
    timeout.QuadPart = start.QuadPart + 10 * 10000; /* 10ms absolute */
    status = RtlWaitOnAddress(&address, &compare, 8, &timeout);
    NtQuerySystemTime(&end);
    ok(status == STATUS_TIMEOUT, "got 0x%08x\n", (unsigned int)status);
    ok(end.QuadPart >= start.QuadPart, "system time did not advance\n");

    for (size = 1; size <= 4; size <<= 1) {
        compare = ~0LL;
        compare = (LONGLONG)((ULONGLONG)compare << (size * 8));
        timeout.QuadPart = -5 * 10000; /* 5ms relative */
        status = RtlWaitOnAddress(&address, &compare, size, &timeout);
        ok(status == STATUS_TIMEOUT, "size %u: got 0x%08x\n", (unsigned int)size, (unsigned int)status);

        status = RtlWaitOnAddress(&address, &compare, size << 1, &timeout);
        ok(status == STATUS_SUCCESS, "size %u mismatch: got 0x%08x\n", (unsigned int)(size << 1), (unsigned int)status);
    }

    address = 0;
    compare = 1;
    status = RtlWaitOnAddress(&address, &compare, 8, NULL);
    ok(status == STATUS_SUCCESS, "got 0x%08x\n", (unsigned int)status);

    todo_wine {
        status = NtdllQueryMacosNtsyncState(&sync_state);
        ok(status == STATUS_SUCCESS && sync_state.ulock_available == 1 && sync_state.wait_on_address_calls >= 8,
           "expected Darwin __ulock_wait2 acceleration active on macOS\n");
    }
}

static void test_tid_alert(void)
{
    NTSTATUS ret;
    HANDLE tid;
    HANDLE tids[2];
    LARGE_INTEGER timeout;

    timeout.QuadPart = 0;
    tid = (HANDLE)(uintptr_t)GetCurrentThreadId();

    ret = NtWaitForAlertByThreadId(NULL, &timeout);
    ok(ret == STATUS_TIMEOUT, "got %08x\n", (unsigned int)ret);

    ret = NtWaitForAlertByThreadId((void *)(uintptr_t)0x123U, &timeout);
    ok(ret == STATUS_TIMEOUT, "got %08x\n", (unsigned int)ret);

    ret = NtAlertThreadByThreadId(0);
    ok(ret == STATUS_INVALID_CID, "got %08x\n", (unsigned int)ret);

    ret = NtAlertThreadByThreadId((HANDLE)(uintptr_t)0xdeadbeefU);
    ok(ret == STATUS_INVALID_CID, "got %08x\n", (unsigned int)ret);

    ret = NtAlertThreadByThreadId(tid);
    ok(ret == STATUS_SUCCESS, "got %08x\n", (unsigned int)ret);

    ret = NtAlertThreadByThreadId(tid);
    ok(ret == STATUS_SUCCESS, "got %08x\n", (unsigned int)ret);

    ret = NtWaitForAlertByThreadId((void *)(uintptr_t)0x123U, &timeout);
    ok(ret == STATUS_ALERTED, "got %08x\n", (unsigned int)ret);

    ret = NtWaitForAlertByThreadId((void *)(uintptr_t)0x123U, &timeout);
    ok(ret == STATUS_TIMEOUT, "got %08x\n", (unsigned int)ret);

    todo_wine {
        ret = NtAlertMultipleThreadByThreadId(NULL, 0, NULL, NULL);
        ok(ret == STATUS_SUCCESS, "got %08x\n", (unsigned int)ret);

        ret = NtAlertMultipleThreadByThreadId(NULL, 1, NULL, NULL);
        ok(ret == STATUS_ACCESS_VIOLATION, "got %08x\n", (unsigned int)ret);

        tids[0] = tid;
        tids[1] = (HANDLE)(uintptr_t)0xdeadbeefU;
        ret = NtAlertMultipleThreadByThreadId(tids, 2, NULL, NULL);
        ok(ret == STATUS_INVALID_CID, "got %08x\n", (unsigned int)ret);

        tids[1] = tid;
        ret = NtAlertMultipleThreadByThreadId(tids, 2, NULL, NULL);
        ok(ret == STATUS_SUCCESS, "got %08x\n", (unsigned int)ret);

        ret = NtWaitForAlertByThreadId(NULL, &timeout);
        ok(ret == STATUS_ALERTED, "got %08x\n", (unsigned int)ret);
    }
}

static void test_barrier_and_resource(void)
{
    RTL_BARRIER barrier;
    RTL_RWLOCK rwlock;
    NTSTATUS status;
    BOOLEAN b_ret;

    status = RtlInitBarrier(&barrier, 0, 0);
    ok(status == STATUS_INVALID_PARAMETER, "RtlInitBarrier(0) got 0x%08x\n", (unsigned int)status);

    status = RtlInitBarrier(&barrier, 1, 0);
    ok(status == STATUS_SUCCESS, "RtlInitBarrier(1) got 0x%08x\n", (unsigned int)status);

    b_ret = RtlBarrier(&barrier, 0);
    ok(b_ret == 1, "single-thread RtlBarrier should return TRUE\n");

    status = RtlDeleteBarrier(&barrier);
    ok(status == STATUS_SUCCESS, "RtlDeleteBarrier failed\n");

    RtlInitializeResource(&rwlock);
    ok(RtlAcquireResourceExclusive(&rwlock, TRUE) == 1, "RtlAcquireResourceExclusive failed\n");
    ok(RtlAcquireResourceShared(&rwlock, TRUE) == 1, "recursive shared on exclusive owner failed\n");
    RtlReleaseResource(&rwlock);
    RtlReleaseResource(&rwlock);

    ok(RtlAcquireResourceShared(&rwlock, TRUE) == 1, "RtlAcquireResourceShared failed\n");
    ok(RtlAcquireResourceExclusive(&rwlock, FALSE) == 0, "exclusive upgrade should fail\n");
    RtlReleaseResource(&rwlock);
    RtlDeleteResource(&rwlock);
}

static void test_arm64x_dvrt_and_ntreadfile_system32_pe_view(void)
{
    uint8_t img_v1[4096];
    uint8_t img_v2[4096];
    uint8_t raw_sys32[4096];
    uint8_t read_buf[512];
    SIZE_T img_size = 0;
    ULONG bytes_read = 0;
    NTSTATUS status;
    static const WCHAR sys32_path[] = {
        'C',':','\\','W','i','n','d','o','w','s','\\','S','y','s','t','e','m','3','2','\\','k','e','r','n','e','l','3','2','.','d','l','l',0
    };
    static const WCHAR app_path[] = {
        'C',':','\\','A','p','p','\\','p','l','u','g','i','n','.','d','l','l',0
    };

    memset(img_v1, 0, sizeof(img_v1));
    memset(img_v2, 0, sizeof(img_v2));
    memset(raw_sys32, 0, sizeof(raw_sys32));
    memset(read_buf, 0, sizeof(read_buf));

    todo_wine {
        status = NtdllArm64xBuildSyntheticImage(img_v1, sizeof(img_v1), 1U, &img_size);
        ok(status == STATUS_SUCCESS && img_size == 4096U, "NtdllArm64xBuildSyntheticImage v1 failed: 0x%08x\n", (unsigned int)status);
        ok(*(uint16_t *)(img_v1 + 0x84) == IMAGE_FILE_MACHINE_ARM64, "expected pre-DVRT Machine=0xAA64\n");

        status = NtdllArm64xApplyDynamicRelocations(img_v1, img_size);
        ok(status == STATUS_SUCCESS, "NtdllArm64xApplyDynamicRelocations v1 failed: 0x%08x\n", (unsigned int)status);
        ok(*(uint16_t *)(img_v1 + 0x84) == IMAGE_FILE_MACHINE_AMD64, "expected post-DVRT Machine=0x8664\n");
        ok(*(uint32_t *)(img_v1 + 0xA8) == 0x2000U, "expected post-DVRT AddressOfEntryPoint=0x2000\n");
        ok(*(uint32_t *)(img_v1 + 0x130) == 0U && *(uint32_t *)(img_v1 + 0x134) == 0U,
           "expected DataDirectory[5] BASERELOC zero-filled by DVRT\n");

        status = NtdllArm64xBuildSyntheticImage(img_v2, sizeof(img_v2), 2U, &img_size);
        ok(status == STATUS_SUCCESS, "NtdllArm64xBuildSyntheticImage v2 failed: 0x%08x\n", (unsigned int)status);
        status = NtdllArm64xApplyDynamicRelocations(img_v2, img_size);
        ok(status == STATUS_SUCCESS && *(uint32_t *)(img_v2 + 0x2E0) == 0x4C00U,
           "expected DVRT v2 negative 8-byte-scaled delta 0x4C00\n");

        /* NtReadFile PE header relocation view for system32 DLLs */
        (void)NtdllArm64xBuildSyntheticImage(raw_sys32, sizeof(raw_sys32), 1U, &img_size);
        status = NtReadFileArm64xPeView(
            sys32_path, IMAGE_FILE_MACHINE_AMD64, raw_sys32, img_size, 0ULL, read_buf, sizeof(read_buf), &bytes_read);
        ok(status == STATUS_SUCCESS && *(uint16_t *)(read_buf + 0x84) == IMAGE_FILE_MACHINE_AMD64,
           "expected AMD64 caller on system32 DLL to see Machine=0x8664\n");

        status = NtReadFileArm64xPeView(
            sys32_path, IMAGE_FILE_MACHINE_ARM64EC, raw_sys32, img_size, 0ULL, read_buf, sizeof(read_buf), &bytes_read);
        ok(status == STATUS_SUCCESS && *(uint16_t *)(read_buf + 0x84) == IMAGE_FILE_MACHINE_AMD64,
           "expected ARM64EC caller on system32 DLL to see Machine=0x8664\n");

        status = NtReadFileArm64xPeView(
            sys32_path, IMAGE_FILE_MACHINE_ARM64, raw_sys32, img_size, 0ULL, read_buf, sizeof(read_buf), &bytes_read);
        ok(status == STATUS_SUCCESS && *(uint16_t *)(read_buf + 0x84) == IMAGE_FILE_MACHINE_ARM64,
           "expected native ARM64 caller on system32 DLL to see raw Machine=0xAA64\n");

        status = NtReadFileArm64xPeView(
            app_path, IMAGE_FILE_MACHINE_AMD64, raw_sys32, img_size, 0ULL, read_buf, sizeof(read_buf), &bytes_read);
        ok(status == STATUS_SUCCESS && *(uint16_t *)(read_buf + 0x84) == IMAGE_FILE_MACHINE_ARM64,
           "expected non-system32 DLL read to bypass ARM64X PE header relocation\n");
    }
}

static void test_tsc_registry_rescaling_and_qpc(void)
{
    NTDLL_TSC_SCALE_STATE state;
    ULONGLONG scaled = 0;
    ULONG mhz = 0;
    LARGE_INTEGER qpc1, qpc2, qpf;
    NTSTATUS status;
    static const WCHAR cpu0_key[] = {
        'H','A','R','D','W','A','R','E','\\','D','E','S','C','R','I','P','T','I','O','N','\\',
        'S','y','s','t','e','m','\\','C','e','n','t','r','a','l','P','r','o','c','e','s','s','o','r','\\','0',0
    };
    static const WCHAR mhz_val[] = { '~','M','H','z',0 };

    memset(&state, 0, sizeof(state));
    qpc1.QuadPart = 0;
    qpc2.QuadPart = 0;
    qpf.QuadPart = 0;

    status = NtQueryPerformanceCounter(&qpc1, &qpf);
    ok(status == STATUS_SUCCESS && qpf.QuadPart == 10000000LL, "NtQueryPerformanceCounter failed: 0x%08x\n", (unsigned int)status);
    status = NtQueryPerformanceCounter(&qpc2, NULL);
    ok(status == STATUS_SUCCESS && qpc2.QuadPart >= qpc1.QuadPart, "NtQueryPerformanceCounter non-monotonic\n");

    todo_wine {
        status = NtdllTscScaleConfigure(24000000ULL, 3600000000ULL, &state);
        ok(status == STATUS_SUCCESS && state.scale_numerator == 150ULL && state.scale_denominator == 1ULL,
           "NtdllTscScaleConfigure 24MHz->3600MHz failed\n");

        status = NtdllTscScaleReadTicks(24000000ULL, &scaled);
        ok(status == STATUS_SUCCESS && scaled == 3600000000ULL, "NtdllTscScaleReadTicks 1s mismatch\n");

        status = NtdllTscQueryRegistryMhz(cpu0_key, mhz_val, &mhz);
        ok(status == STATUS_SUCCESS && mhz == 3600U, "NtdllTscQueryRegistryMhz expected 3600, got %u\n", (unsigned int)mhz);
    }
}

static void test_arm64ec_jit_disk_cache(void)
{
    NTDLL_JIT_MODULE_KEY key;
    NTDLL_JIT_CACHE_STATS stats;
    uint32_t code_in[4] = { 0xD2800540U, 0x91000400U, 0xD503201FU, 0xD65F03C0U };
    uint32_t code_out[4];
    SIZE_T out_size = 0;
    ULONG removed = 0;
    NTSTATUS status;

    memset(&key, 0, sizeof(key));
    memset(&stats, 0, sizeof(stats));
    memset(code_out, 0, sizeof(code_out));

    key.time_date_stamp = 0x6710ABCDU;
    key.checksum = 0x00123456U;
    key.size_of_image = 0x00040000U;
    key.guest_machine = IMAGE_FILE_MACHINE_AMD64;
    key.emulator_id = 1U;

    todo_wine {
        status = NtdllJitCacheInit("/tmp/spumante_winetest_ntdll_jit_cache");
        ok(status == STATUS_SUCCESS, "NtdllJitCacheInit failed: 0x%08x\n", (unsigned int)status);

        status = NtdllJitCacheStoreBlock(&key, 0x1040U, code_in, sizeof(code_in));
        ok(status == STATUS_SUCCESS, "NtdllJitCacheStoreBlock failed: 0x%08x\n", (unsigned int)status);

        status = NtdllJitCacheLookupBlock(&key, 0x1040U, code_out, sizeof(code_out), &out_size);
        ok(status == STATUS_SUCCESS && out_size == sizeof(code_in) && memcmp(code_in, code_out, sizeof(code_in)) == 0,
           "NtdllJitCacheLookupBlock hit mismatch\n");

        status = NtdllJitCacheCorruptBlockForTest(&key, 0x1040U);
        ok(status == STATUS_SUCCESS, "NtdllJitCacheCorruptBlockForTest failed\n");

        status = NtdllJitCacheLookupBlock(&key, 0x1040U, code_out, sizeof(code_out), &out_size);
        ok(status == STATUS_CRC_ERROR, "expected STATUS_CRC_ERROR on corrupted JIT cache block, got 0x%08x\n", (unsigned int)status);

        (void)NtdllJitCacheStoreBlock(&key, 0x2080U, code_in, sizeof(code_in));
        status = NtdllJitCacheInvalidateModule(&key, &removed);
        ok(status == STATUS_SUCCESS && removed == 1U, "NtdllJitCacheInvalidateModule failed\n");

        status = NtdllJitCacheQueryStats(&stats);
        ok(status == STATUS_SUCCESS && stats.corrupt_rejections == 1U && stats.invalidated_entries == 1U,
           "NtdllJitCacheQueryStats mismatch\n");
    }
}

static void test_actctx_winsxs_manifest_resolution_and_packaging(void)
{
    NTSTATUS status;
    HANDLE actctx = NULL;
    ACTCTXW ctx;
    ACTIVATION_CONTEXT_BASIC_INFORMATION basic_info;
    ACTCTX_SECTION_KEYED_DATA data;
    UNICODE_STRING class_name;
    SIZE_T written = 0;
    WCHAR sourceW[256];
    WCHAR btnW[32];
    WCHAR winDirW[64];
    ULONG_PTR cookie = 0;
    size_t i;

    /* Invalid arguments */
    status = RtlCreateActivationContext(NULL, NULL);
    ok(status == STATUS_INVALID_PARAMETER, "expected STATUS_INVALID_PARAMETER, got 0x%08x\n", (unsigned int)status);

    /* Valid creation */
    memset(&ctx, 0, sizeof(ctx));
    ctx.cbSize = sizeof(ctx);
    ctx.dwFlags = ACTCTX_FLAG_SOURCE_IS_ASSEMBLYREF;
    const char *src = "Microsoft.Windows.Common-Controls,version=\"6.0.0.0\",publicKeyToken=\"6595b64144ccf1df\",processorArchitecture=\"*\"";
    for (i = 0; src[i] && i < 255; ++i) sourceW[i] = (WCHAR)(unsigned char)src[i];
    sourceW[i] = 0;
    ctx.lpSource = sourceW;
    status = RtlCreateActivationContext(&actctx, &ctx);
    ok(status == STATUS_SUCCESS && actctx != NULL, "RtlCreateActivationContext failed: 0x%08x\n", (unsigned int)status);

    /* Activation & Deactivation */
    status = RtlActivateActivationContext(0, actctx, &cookie);
    ok(status == STATUS_SUCCESS && cookie != 0, "RtlActivateActivationContext failed: 0x%08x\n", (unsigned int)status);

    /* Query information */
    memset(&basic_info, 0, sizeof(basic_info));
    status = RtlQueryInformationActivationContext(0, actctx, NULL, ActivationContextBasicInformation, &basic_info, sizeof(basic_info), &written);
    ok(status == STATUS_SUCCESS && basic_info.hActCtx == actctx, "RtlQueryInformationActivationContext failed: 0x%08x\n", (unsigned int)status);

    /* Window class section find */
    const char *btn = "Button";
    for (i = 0; btn[i] && i < 31; ++i) btnW[i] = (WCHAR)(unsigned char)btn[i];
    btnW[i] = 0;
    class_name.Buffer = btnW;
    class_name.Length = 6 * sizeof(WCHAR);
    class_name.MaximumLength = sizeof(btnW);
    memset(&data, 0, sizeof(data));
    data.cbSize = sizeof(data);
    status = RtlFindActivationContextSectionString(0, NULL, ACTIVATION_CONTEXT_SECTION_WINDOW_CLASS_REDIRECTION, &class_name, &data);
    ok(status == STATUS_SUCCESS && data.ulDataFormatVersion == 1, "RtlFindActivationContextSectionString Button failed: 0x%08x\n", (unsigned int)status);

    RtlDeactivateActivationContext(0, cookie);
    RtlReleaseActivationContext(actctx);

    /* Wine install manifests packaging */
    const char *wdir = "C:\\windows";
    for (i = 0; wdir[i] && i < 63; ++i) winDirW[i] = (WCHAR)(unsigned char)wdir[i];
    winDirW[i] = 0;
    BOOL ok_inst = wine_install_manifests(winDirW);
    ok(ok_inst == TRUE, "wine_install_manifests failed: %d\n", ok_inst);
}

START_TEST(ntdll)
{
    WINETEST_RUN_SUBTEST(test_NtAllocateVirtualMemory);
    WINETEST_RUN_SUBTEST(test_write_watch);
    WINETEST_RUN_SUBTEST(test_rosetta2_and_exec_write_exceptions);
    WINETEST_RUN_SUBTEST(test_darwin_x18_and_arm64_context);
    WINETEST_RUN_SUBTEST(test_architectures);
    WINETEST_RUN_SUBTEST(test_selectors);
    WINETEST_RUN_SUBTEST(test_cpu_area);
    WINETEST_RUN_SUBTEST(test_cross_process_work_list);
    WINETEST_RUN_SUBTEST(test_event);
    WINETEST_RUN_SUBTEST(test_semaphore);
    WINETEST_RUN_SUBTEST(test_mutant);
    WINETEST_RUN_SUBTEST(test_keyed_events);
    WINETEST_RUN_SUBTEST(test_wait_on_address);
    WINETEST_RUN_SUBTEST(test_tid_alert);
    WINETEST_RUN_SUBTEST(test_barrier_and_resource);
    WINETEST_RUN_SUBTEST(test_arm64x_dvrt_and_ntreadfile_system32_pe_view);
    WINETEST_RUN_SUBTEST(test_tsc_registry_rescaling_and_qpc);
    WINETEST_RUN_SUBTEST(test_arm64ec_jit_disk_cache);
    WINETEST_RUN_SUBTEST(test_actctx_winsxs_manifest_resolution_and_packaging);
}

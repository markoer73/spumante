/*
 * Differential Conformance Test for Apple Silicon (arm64) Execution Parity
 * of Intel (x86_64 & i386) Windows Apps in dlls/ntdll:
 *   1. 16 KB Apple Silicon host page vs. 4 KB Windows PE sub-page PAGE_GUARD isolation
 *   2. 16 KB Apple Silicon host page vs. 4 KB Windows PE sub-page MEM_WRITE_WATCH tracking
 *   3. Rosetta 2 read-fault-on-writable-page promotion & ProcessManageWritesToExecutableMemory
 *   4. MAP_JIT / pthread_jit_write_protect_np W^X toggling & NtFlushInstructionCache
 *   5. Darwin x18 platform-register save/restore across PE/Unix/Signal transitions & CONTEXT_ARM64_X18
 *   6. RtlWow64GetCpuAreaInfo (I386/AMD64/ARMNT/ARM64) & ARM64EC fexcore.dll/box64cpu.dll engine
 *   7. Cross-process emulator work-list lock-free FIFO queue & heavy flush signaling
 *   8. Rosetta 2 / WoW64 LDT selector table (RtlWow64GetThreadSelectorEntry, NtSetLdtEntries) & %gs: fixup
 *
 * Copyright 2026 Wine Parity Contributors
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "../../include/parity_test.h"
#include "../../../dlls/ntdll/ntdll.h"

static void put_u32_le(uint8_t *dst, uint32_t v)
{
    dst[0] = (uint8_t)(v & 0xffU);
    dst[1] = (uint8_t)((v >> 8) & 0xffU);
    dst[2] = (uint8_t)((v >> 16) & 0xffU);
    dst[3] = (uint8_t)((v >> 24) & 0xffU);
}

int main(void)
{
    NTSTATUS status;

    parity_begin_document("ntdll", "test_apple_silicon_parity", "arm64", "wine_macos");

    /* =========================================================================
     * Case 1: 16 KB Apple Silicon host page vs. 4 KB Windows PE sub-page PAGE_GUARD
     * Two adjacent 4 KB Windows pages (P0 at +0x0000 and P1 at +0x1000) share the
     * same 16 KB (0x4000) Apple Silicon host VM page. Triggering PAGE_GUARD on P0
     * must clear PAGE_GUARD ONLY on P0 while preserving PAGE_GUARD on sibling P1.
     * ========================================================================= */
    {
        PVOID base = NULL;
        SIZE_T size = 0x4000;
        PVOID p0_1_base = NULL;
        SIZE_T p0_1_size = 0x2000;
        ULONG old_prot = 0;
        NTDLL_MEMORY_BASIC_INFORMATION mbi_p0;
        NTDLL_MEMORY_BASIC_INFORMATION mbi_p1;
        SIZE_T ret_len = 0;
        NTSTATUS st_fault_p0_first = STATUS_UNSUCCESSFUL;
        NTSTATUS st_fault_p0_second = STATUS_UNSUCCESSFUL;
        NTSTATUS st_fault_p1_first = STATUS_UNSUCCESSFUL;
        NTSTATUS st_fault_p1_second = STATUS_UNSUCCESSFUL;
        uint8_t digest[24];

        memset(&mbi_p0, 0, sizeof(mbi_p0));
        memset(&mbi_p1, 0, sizeof(mbi_p1));
        memset(digest, 0, sizeof(digest));

        NtdllResetVirtualState();
        status = NtdllSetSimulatedHostPageSize(NTDLL_APPLE_SILICON_HOST_PAGE_SIZE);
        if (status == STATUS_SUCCESS) {
            status = NtAllocateVirtualMemory(
                NtCurrentProcess(), &base, 0, &size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        }
        if (status == STATUS_SUCCESS && base != NULL) {
            p0_1_base = base;
            status = NtProtectVirtualMemory(
                NtCurrentProcess(), &p0_1_base, &p0_1_size, PAGE_READWRITE | PAGE_GUARD, &old_prot);
        }
        if (status == STATUS_SUCCESS && base != NULL) {
            uint8_t *bytes = (uint8_t *)base;
            st_fault_p0_first = NtdllHandleVirtualFault(bytes + 0x0100, NTDLL_FAULT_WRITE, 0);
            st_fault_p0_second = NtdllHandleVirtualFault(bytes + 0x0200, NTDLL_FAULT_WRITE, 0);

            (void)NtQueryVirtualMemory(
                NtCurrentProcess(), bytes + 0x0000, MemoryBasicInformation,
                &mbi_p0, sizeof(mbi_p0), &ret_len);
            (void)NtQueryVirtualMemory(
                NtCurrentProcess(), bytes + 0x1000, MemoryBasicInformation,
                &mbi_p1, sizeof(mbi_p1), &ret_len);

            st_fault_p1_first = NtdllHandleVirtualFault(bytes + 0x1080, NTDLL_FAULT_READ, 0);
            st_fault_p1_second = NtdllHandleVirtualFault(bytes + 0x1080, NTDLL_FAULT_READ, 0);

            put_u32_le(digest + 0, (uint32_t)st_fault_p0_first);
            put_u32_le(digest + 4, (uint32_t)st_fault_p0_second);
            put_u32_le(digest + 8, (uint32_t)mbi_p0.Protect);
            put_u32_le(digest + 12, (uint32_t)mbi_p1.Protect);
            put_u32_le(digest + 16, (uint32_t)st_fault_p1_first);
            put_u32_le(digest + 20, (uint32_t)st_fault_p1_second);

            size = 0;
            (void)NtFreeVirtualMemory(NtCurrentProcess(), &base, &size, MEM_RELEASE);
        }

        ok(status == STATUS_SUCCESS && mbi_p1.Protect == (PAGE_READWRITE | PAGE_GUARD),
           "Expected 4KB sub-page PAGE_GUARD isolation on 16KB host page, got status=0x%08x prot=0x%x\n",
           (unsigned int)status, (unsigned int)mbi_p1.Protect);

        parity_record_exact(
            "virtual_16k_host_4k_subpage_guard_isolation",
            "Virtual_16KHost_4KSubpage_GuardIsolation",
            "NtProtectVirtualMemory",
            status,
            GetLastError(),
            (ULONG)mbi_p1.Protect,
            digest,
            status == STATUS_SUCCESS ? sizeof(digest) : 0,
            status == STATUS_SUCCESS ? 1 : 0);
    }

    /* =========================================================================
     * Case 2: 16 KB Apple Silicon host page vs. 4 KB Windows PE sub-page MEM_WRITE_WATCH
     * Touching sub-pages 1 (+0x1000) and 3 (+0x3000) inside the first 16 KB host page
     * must report ONLY those 2 4-KB pages dirty (with granularity = 4096), and
     * WRITE_WATCH_FLAG_RESET must atomically reset their dirty bits.
     * ========================================================================= */
    {
        PVOID base = NULL;
        SIZE_T size = 0x10000;
        PVOID results[16];
        ULONG_PTR count = 16;
        ULONG granularity = 0;
        ULONG_PTR count_after_reset = 16;
        uint8_t digest[20];

        memset(results, 0, sizeof(results));
        memset(digest, 0, sizeof(digest));

        NtdllResetVirtualState();
        status = NtdllSetSimulatedHostPageSize(NTDLL_APPLE_SILICON_HOST_PAGE_SIZE);
        if (status == STATUS_SUCCESS) {
            status = NtAllocateVirtualMemory(
                NtCurrentProcess(),
                &base,
                0,
                &size,
                MEM_RESERVE | MEM_COMMIT | MEM_WRITE_WATCH,
                PAGE_READWRITE);
        }
        if (status == STATUS_SUCCESS && base != NULL) {
            uint8_t *bytes = (uint8_t *)base;
            (void)NtdllHandleVirtualFault(bytes + 0x1040, NTDLL_FAULT_WRITE, 0);
            (void)NtdllHandleVirtualFault(bytes + 0x3200, NTDLL_FAULT_WRITE, 0);

            status = NtGetWriteWatch(
                NtCurrentProcess(),
                WRITE_WATCH_FLAG_RESET,
                base,
                size,
                results,
                &count,
                &granularity);

            if (status == STATUS_SUCCESS && count == 2) {
                uint32_t off0 = (uint32_t)((uint8_t *)results[0] - bytes);
                uint32_t off1 = (uint32_t)((uint8_t *)results[1] - bytes);
                (void)NtGetWriteWatch(
                    NtCurrentProcess(),
                    0,
                    base,
                    size,
                    results,
                    &count_after_reset,
                    &granularity);

                put_u32_le(digest + 0, (uint32_t)count);
                put_u32_le(digest + 4, (uint32_t)granularity);
                put_u32_le(digest + 8, off0);
                put_u32_le(digest + 12, off1);
                put_u32_le(digest + 16, (uint32_t)count_after_reset);
            }

            size = 0;
            (void)NtFreeVirtualMemory(NtCurrentProcess(), &base, &size, MEM_RELEASE);
        }

        ok(status == STATUS_SUCCESS && count == 2 && granularity == 4096,
           "Expected 2 dirty 4KB sub-pages in NtGetWriteWatch, got status=0x%08x count=%u gran=%u\n",
           (unsigned int)status, (unsigned int)count, (unsigned int)granularity);

        parity_record_exact(
            "virtual_16k_host_4k_write_watch_granularity",
            "Virtual_16KHost_4KWriteWatchGranularity",
            "NtGetWriteWatch",
            status,
            GetLastError(),
            granularity,
            digest,
            status == STATUS_SUCCESS ? sizeof(digest) : 0,
            status == STATUS_SUCCESS ? 1 : 0);
    }

    /* =========================================================================
     * Case 3: Rosetta 2 read-fault-on-writable-page promotion & Executable Memory Write Guard
     * Under Rosetta 2, a hardware write fault on a read-permitted write-watched or
     * write-copy page is reported as EXCEPTION_READ_FAULT (NTDLL_FAULT_READ).
     * Also tests ProcessManageWritesToExecutableMemory / ThreadManageWritesToExecutableMemory.
     * ========================================================================= */
    {
        PVOID base = NULL;
        SIZE_T size = 0x4000;
        PVOID ro_page = NULL;
        SIZE_T ro_size = 0x1000;
        PVOID rwx_page = NULL;
        SIZE_T rwx_size = 0x1000;
        ULONG old_prot = 0;
        NTSTATUS st_rosetta_ww = STATUS_UNSUCCESSFUL;
        NTSTATUS st_ro_write = STATUS_UNSUCCESSFUL;
        NTSTATUS st_rwx_blocked = STATUS_UNSUCCESSFUL;
        NTSTATUS st_rwx_allowed = STATUS_UNSUCCESSFUL;
        PVOID ww_results[4];
        ULONG_PTR ww_count = 4;
        ULONG ww_gran = 0;
        NTDLL_PROCESS_EXEC_WRITE_STATES proc_state;
        NTDLL_THREAD_EXEC_WRITE_STATES thread_state;
        uint8_t digest[20];

        memset(ww_results, 0, sizeof(ww_results));
        memset(&proc_state, 0, sizeof(proc_state));
        memset(&thread_state, 0, sizeof(thread_state));
        memset(digest, 0, sizeof(digest));

        NtdllResetVirtualState();
        status = NtAllocateVirtualMemory(
            NtCurrentProcess(),
            &base,
            0,
            &size,
            MEM_RESERVE | MEM_COMMIT | MEM_WRITE_WATCH,
            PAGE_READWRITE);
        if (status == STATUS_SUCCESS && base != NULL) {
            uint8_t *bytes = (uint8_t *)base;

            /* 1) Rosetta 2 reports write to write-watched page +0x0000 as NTDLL_FAULT_READ */
            st_rosetta_ww = NtdllHandleVirtualFault(bytes + 0x0500, NTDLL_FAULT_READ, 1);
            (void)NtGetWriteWatch(
                NtCurrentProcess(), WRITE_WATCH_FLAG_RESET, base, 0x1000,
                ww_results, &ww_count, &ww_gran);

            /* 2) Protect +0x1000 as PAGE_READONLY; write fault must still fail with STATUS_ACCESS_VIOLATION */
            ro_page = bytes + 0x1000;
            (void)NtProtectVirtualMemory(
                NtCurrentProcess(), &ro_page, &ro_size, PAGE_READONLY, &old_prot);
            st_ro_write = NtdllHandleVirtualFault(bytes + 0x1100, NTDLL_FAULT_WRITE, 1);

            /* 3) Protect +0x2000 as PAGE_EXECUTE_READWRITE and enable ProcessManageWritesToExecutableMemory */
            rwx_page = bytes + 0x2000;
            (void)NtProtectVirtualMemory(
                NtCurrentProcess(), &rwx_page, &rwx_size, PAGE_EXECUTE_READWRITE, &old_prot);

            proc_state.Version = 1;
            proc_state.EnableWriteExceptions = 1;
            proc_state.ThreadAllowWrites = 0;
            status = NtSetInformationProcess(
                NtCurrentProcess(),
                ProcessManageWritesToExecutableMemory,
                &proc_state,
                sizeof(proc_state));

            st_rwx_blocked = NtdllHandleVirtualFault(bytes + 0x2040, NTDLL_FAULT_WRITE, 0);

            thread_state.Version = 1;
            thread_state.ThreadAllowWrites = 1;
            (void)NtSetInformationThread(
                NtCurrentThread(),
                ThreadManageWritesToExecutableMemory,
                &thread_state,
                sizeof(thread_state));

            st_rwx_allowed = NtdllHandleVirtualFault(bytes + 0x2040, NTDLL_FAULT_WRITE, 0);

            put_u32_le(digest + 0, (uint32_t)st_rosetta_ww);
            put_u32_le(digest + 4, (uint32_t)ww_count);
            put_u32_le(digest + 8, (uint32_t)st_ro_write);
            put_u32_le(digest + 12, (uint32_t)st_rwx_blocked);
            put_u32_le(digest + 16, (uint32_t)st_rwx_allowed);

            size = 0;
            (void)NtFreeVirtualMemory(NtCurrentProcess(), &base, &size, MEM_RELEASE);
        }

        ok(status == STATUS_SUCCESS && st_rwx_blocked == STATUS_EXECUTABLE_MEMORY_WRITE,
           "Expected Rosetta2 read-fault promotion and RWX write exception, got status=0x%08x\n",
           (unsigned int)status);

        parity_record_exact(
            "virtual_rosetta2_read_fault_and_exec_write_exceptions",
            "Virtual_Rosetta2ReadFault_And_ExecWriteExceptions",
            "NtSetInformationProcess",
            status,
            GetLastError(),
            (ULONG)ww_count,
            digest,
            status == STATUS_SUCCESS ? sizeof(digest) : 0,
            status == STATUS_SUCCESS ? 1 : 0);
    }

    /* =========================================================================
     * Case 4: Apple Silicon MAP_JIT / pthread_jit_write_protect_np W^X & Instruction Cache
     * Verifies W^X state transitions when allocating PAGE_EXECUTE_READWRITE memory,
     * toggling write-enable for JIT code emission, toggling back to execute-only,
     * and flushing the instruction cache via NtFlushInstructionCache.
     * ========================================================================= */
    {
        PVOID jit_base = NULL;
        SIZE_T jit_size = 0x4000;
        NTDLL_JIT_WX_STATE wx_state;
        uint8_t digest[20];

        memset(&wx_state, 0, sizeof(wx_state));
        memset(digest, 0, sizeof(digest));

        NtdllResetVirtualState();
        status = NtdllSetSimulatedHostPageSize(NTDLL_APPLE_SILICON_HOST_PAGE_SIZE);
        if (status == STATUS_SUCCESS) {
            status = NtAllocateVirtualMemory(
                NtCurrentProcess(),
                &jit_base,
                0,
                &jit_size,
                MEM_RESERVE | MEM_COMMIT,
                PAGE_EXECUTE_READWRITE);
        }
        if (status == STATUS_SUCCESS && jit_base != NULL) {
            (void)NtdllToggleJitWriteProtection(1);
            ((uint8_t *)jit_base)[0] = 0xc0;
            ((uint8_t *)jit_base)[1] = 0x03;
            ((uint8_t *)jit_base)[2] = 0x5f;
            ((uint8_t *)jit_base)[3] = 0xd6; /* ARM64 ret */
            (void)NtdllToggleJitWriteProtection(0);
            status = NtFlushInstructionCache(NtCurrentProcess(), jit_base, 64);
            if (status == STATUS_SUCCESS) {
                status = NtdllQueryJitWxState(&wx_state);
            }

            put_u32_le(digest + 0, (uint32_t)wx_state.win_page_size);
            put_u32_le(digest + 4, (uint32_t)wx_state.map_jit_enabled);
            put_u32_le(digest + 8, (uint32_t)wx_state.wx_write_enabled);
            put_u32_le(digest + 12, (uint32_t)wx_state.wx_toggle_count);
            put_u32_le(digest + 16, (uint32_t)wx_state.icache_flush_count);

            jit_size = 0;
            (void)NtFreeVirtualMemory(NtCurrentProcess(), &jit_base, &jit_size, MEM_RELEASE);
        }

        ok(status == STATUS_SUCCESS && wx_state.map_jit_enabled == 1 && wx_state.wx_toggle_count == 2,
           "Expected MAP_JIT W^X toggle and icache flush, got status=0x%08x\n",
           (unsigned int)status);

        parity_record_exact(
            "virtual_apple_silicon_map_jit_wx_and_icache_flush",
            "Virtual_AppleSilicon_MapJitWx_And_IcacheFlush",
            "NtFlushInstructionCache",
            status,
            GetLastError(),
            wx_state.wx_toggle_count,
            digest,
            status == STATUS_SUCCESS ? sizeof(digest) : 0,
            status == STATUS_SUCCESS ? 1 : 0);
    }

    /* =========================================================================
     * Case 5: Darwin x18 platform-register preservation across PE / Unix / Signal
     * On arm64-apple-darwin, x18 is reserved by libSystem/pthreads while Windows
     * ARM64/ARM64EC uses x18 as the TEB pointer. Verifies PE entry, signal handler
     * save/restore, NtSetContextThread(CONTEXT_ARM64_INTEGER vs CONTEXT_ARM64_X18),
     * and Unix exit.
     * ========================================================================= */
    {
        NTDLL_DARWIN_X18_STATE x18_pe;
        NTDLL_DARWIN_X18_STATE x18_sig;
        NTDLL_DARWIN_X18_STATE x18_after_sig;
        NTDLL_DARWIN_X18_STATE x18_unix;
        NTDLL_ARM64_NT_CONTEXT ctx;
        uint8_t digest[28];

        memset(&x18_pe, 0, sizeof(x18_pe));
        memset(&x18_sig, 0, sizeof(x18_sig));
        memset(&x18_after_sig, 0, sizeof(x18_after_sig));
        memset(&x18_unix, 0, sizeof(x18_unix));
        memset(&ctx, 0, sizeof(ctx));
        memset(digest, 0, sizeof(digest));

        status = NtdllDarwinX18Init(0x000000016fdff000ULL, 0x000000007ffe0000ULL);
        if (status == STATUS_SUCCESS) {
            (void)NtdllDarwinX18EnterPe();
            (void)NtdllDarwinX18QueryState(&x18_pe);

            /* Signal arrives while executing PE code with x18 == TEB */
            (void)NtdllDarwinX18SignalEnter(x18_pe.active_x18);
            (void)NtdllDarwinX18QueryState(&x18_sig);

            /* Leave signal back to PE code */
            (void)NtdllDarwinX18SignalLeave();
            (void)NtdllDarwinX18QueryState(&x18_after_sig);

            /* NtSetContextThread with CONTEXT_ARM64_INTEGER alone MUST NOT overwrite X[18] */
            ctx.ContextFlags = NTDLL_CONTEXT_ARM64_INTEGER;
            ctx.X[0] = 0x11223344ULL;
            ctx.X[18] = 0xdeadbeefULL;
            (void)NtSetContextThread(NtCurrentThread(), &ctx);

            memset(&ctx, 0, sizeof(ctx));
            ctx.ContextFlags = NTDLL_CONTEXT_ARM64_INTEGER | NTDLL_CONTEXT_ARM64_X18;
            (void)NtGetContextThread(NtCurrentThread(), &ctx);
            put_u32_le(digest + 0, (uint32_t)ctx.X[18]);

            /* Now explicitly set CONTEXT_ARM64_X18 */
            ctx.ContextFlags = NTDLL_CONTEXT_ARM64_INTEGER | NTDLL_CONTEXT_ARM64_X18;
            ctx.X[18] = 0x000000007ffe8000ULL;
            (void)NtSetContextThread(NtCurrentThread(), &ctx);

            /* Exit PE to Unix: active_x18 must restore Darwin platform x18 */
            status = NtdllDarwinX18ExitToUnix();
            (void)NtdllDarwinX18QueryState(&x18_unix);

            put_u32_le(digest + 4, (uint32_t)x18_pe.active_x18);
            put_u32_le(digest + 8, (uint32_t)x18_sig.active_x18);
            put_u32_le(digest + 12, (uint32_t)x18_after_sig.active_x18);
            put_u32_le(digest + 16, (uint32_t)x18_unix.active_x18);
            put_u32_le(digest + 20, (uint32_t)x18_unix.windows_teb_x18);
            put_u32_le(digest + 24, (uint32_t)x18_unix.signal_handler_save_count);
        }

        ok(status == STATUS_SUCCESS && x18_unix.signal_handler_save_count == 1,
           "Expected Darwin x18 preservation across PE/Unix/Signal, got status=0x%08x\n",
           (unsigned int)status);

        parity_record_exact(
            "signal_arm64_darwin_x18_platform_register_preservation",
            "SignalArm64_DarwinX18_PlatformRegisterPreservation",
            "NtSetContextThread",
            status,
            GetLastError(),
            x18_unix.pe_enter_count,
            digest,
            status == STATUS_SUCCESS ? sizeof(digest) : 0,
            status == STATUS_SUCCESS ? 1 : 0);
    }

    /* =========================================================================
     * Case 6: RtlWow64GetCpuAreaInfo & ARM64EC fexcore.dll / box64cpu.dll Engine
     * Verifies RtlWow64GetCpuAreaInfo offsets/flags for I386, AMD64, ARMNT, ARM64,
     * invalid machine error handling, ARM64EC emulator selection, and CHPEv2
     * suspend doorbell handling.
     * ========================================================================= */
    {
        ULONG_PTR buffer[512];
        WOW64_CPU_AREA_INFO info_i386;
        WOW64_CPU_AREA_INFO info_amd64;
        WOW64_CPU_AREA_INFO info_armnt;
        WOW64_CPU_AREA_INFO info_arm64;
        WOW64_CPU_AREA_INFO info_bad;
        NTSTATUS st_bad = STATUS_SUCCESS;
        NTDLL_ARM64EC_EMULATOR_INFO emu_info;
        NTDLL_CHPE_V2_CPU_AREA_INFO chpe;
        NTDLL_ARM64_NT_CONTEXT arm64_ctx;
        uint8_t digest[28];

        memset(buffer, 0, sizeof(buffer));
        memset(&info_i386, 0, sizeof(info_i386));
        memset(&info_amd64, 0, sizeof(info_amd64));
        memset(&info_armnt, 0, sizeof(info_armnt));
        memset(&info_arm64, 0, sizeof(info_arm64));
        memset(&info_bad, 0, sizeof(info_bad));
        memset(&emu_info, 0, sizeof(emu_info));
        memset(&chpe, 0, sizeof(chpe));
        memset(&arm64_ctx, 0, sizeof(arm64_ctx));
        memset(digest, 0, sizeof(digest));

        buffer[0] = IMAGE_FILE_MACHINE_I386;
        status = RtlWow64GetCpuAreaInfo((WOW64_CPU_AREA_INFO *)(void *)buffer, 0, &info_i386);

        if (status == STATUS_SUCCESS) {
            buffer[0] = IMAGE_FILE_MACHINE_AMD64;
            status = RtlWow64GetCpuAreaInfo((WOW64_CPU_AREA_INFO *)(void *)buffer, 0, &info_amd64);
        }
        if (status == STATUS_SUCCESS) {
            buffer[0] = IMAGE_FILE_MACHINE_ARMNT;
            status = RtlWow64GetCpuAreaInfo((WOW64_CPU_AREA_INFO *)(void *)buffer, 0, &info_armnt);
        }
        if (status == STATUS_SUCCESS) {
            buffer[0] = IMAGE_FILE_MACHINE_ARM64;
            status = RtlWow64GetCpuAreaInfo((WOW64_CPU_AREA_INFO *)(void *)buffer, 0, &info_arm64);
        }
        if (status == STATUS_SUCCESS) {
            buffer[0] = IMAGE_FILE_MACHINE_ARM64EC;
            st_bad = RtlWow64GetCpuAreaInfo((WOW64_CPU_AREA_INFO *)(void *)buffer, 0, &info_bad);

            (void)NtdllArm64EcSelectEmulator("fexcore.dll");
            BTCpu64FlushInstructionCache(buffer, 128);
            (void)NtdllArm64EcQueryEmulatorInfo(&emu_info);

            chpe.InSimulation = 1;
            chpe.SuspendDoorbell = 0x50001000U;
            status = NtdllArm64EcHandleSuspendDoorbell(&chpe, &arm64_ctx);

            put_u32_le(digest + 0, (uint32_t)((char *)info_i386.Context - (char *)buffer));
            put_u32_le(digest + 4, (uint32_t)((char *)info_amd64.Context - (char *)buffer));
            put_u32_le(digest + 8, (uint32_t)((char *)info_armnt.Context - (char *)buffer));
            put_u32_le(digest + 12, (uint32_t)((char *)info_arm64.Context - (char *)buffer));
            put_u32_le(digest + 16, (uint32_t)st_bad);
            put_u32_le(digest + 20, (uint32_t)emu_info.jit_icache_flushes);
            put_u32_le(digest + 24, (uint32_t)chpe.SuspendTriggeredCount);
        }

        ok(status == STATUS_SUCCESS && st_bad == STATUS_INVALID_PARAMETER,
           "Expected RtlWow64GetCpuAreaInfo & ARM64EC success, got status=0x%08x\n",
           (unsigned int)status);

        parity_record_exact(
            "signal_arm64_wow64_cpu_area_and_arm64ec_emulator",
            "SignalArm64_Wow64CpuArea_And_Arm64EcEmulator",
            "RtlWow64GetCpuAreaInfo",
            status,
            GetLastError(),
            (ULONG)emu_info.guest_machine,
            digest,
            status == STATUS_SUCCESS ? sizeof(digest) : 0,
            status == STATUS_SUCCESS ? 1 : 0);
    }

    /* =========================================================================
     * Case 7: Cross-Process Work-List Lock-Free Operations (ARM64EC / WoW64 JIT)
     * Verifies RtlWow64PushCrossProcessWorkOntoFreeList, PopFromFreeList,
     * PushOntoWorkList, PopAllFromWorkList (FIFO ordering), and HeavyFlush.
     * ========================================================================= */
    {
        struct {
            CROSS_PROCESS_WORK_HDR free_list;
            CROSS_PROCESS_WORK_HDR work_list;
            CROSS_PROCESS_WORK_ENTRY entries[4];
        } list;
        CROSS_PROCESS_WORK_ENTRY *entry = NULL;
        void *flush_entry = NULL;
        BOOLEAN flush = 0;
        uint8_t digest[20];
        int i;

        memset(&list, 0, sizeof(list));
        memset(digest, 0, sizeof(digest));

        status = STATUS_SUCCESS;
        for (i = 0; i < 3; i++) {
            list.entries[i].id = (ULONG)(i + 10);
            if (!RtlWow64PushCrossProcessWorkOntoFreeList(&list.free_list, &list.entries[i])) {
                status = STATUS_NOT_IMPLEMENTED;
                break;
            }
        }
        if (status == STATUS_SUCCESS) {
            for (i = 0; i < 3; i++) {
                entry = RtlWow64PopCrossProcessWorkFromFreeList(&list.free_list);
                if (entry != NULL) {
                    entry->addr = (ULONGLONG)(0x1000U * (ULONG)(i + 1));
                    if (!RtlWow64PushCrossProcessWorkOntoWorkList(
                            &list.work_list, entry, &flush_entry)) {
                        status = STATUS_NOT_IMPLEMENTED;
                        break;
                    }
                } else {
                    status = STATUS_UNSUCCESSFUL;
                }
            }
        }
        if (status == STATUS_SUCCESS) {
            entry = RtlWow64PopAllCrossProcessWorkFromWorkList(&list.work_list, &flush);
            if (entry != NULL) {
                CROSS_PROCESS_WORK_ENTRY *e0 = entry;
                CROSS_PROCESS_WORK_ENTRY *e1 = e0->next ? CROSS_PROCESS_LIST_ENTRY(&list.work_list, e0->next) : NULL;
                CROSS_PROCESS_WORK_ENTRY *e2 = (e1 && e1->next) ? CROSS_PROCESS_LIST_ENTRY(&list.work_list, e1->next) : NULL;

                put_u32_le(digest + 0, e0 ? (uint32_t)e0->addr : 0U);
                put_u32_le(digest + 4, e1 ? (uint32_t)e1->addr : 0U);
                put_u32_le(digest + 8, e2 ? (uint32_t)e2->addr : 0U);
                put_u32_le(digest + 12, (uint32_t)flush);
            }
            if (!RtlWow64RequestCrossProcessHeavyFlush(&list.work_list)) {
                status = STATUS_NOT_IMPLEMENTED;
            }
            entry = RtlWow64PopAllCrossProcessWorkFromWorkList(&list.work_list, &flush);
            put_u32_le(digest + 16, (uint32_t)flush);
        }

        ok(status == STATUS_SUCCESS && flush == 1,
           "Expected cross-process work-list FIFO and heavy flush, got status=0x%08x\n",
           (unsigned int)status);

        parity_record_exact(
            "signal_arm64_cross_process_work_list_queue",
            "SignalArm64_CrossProcessWorkListQueue",
            "RtlWow64PushCrossProcessWorkOntoWorkList",
            status,
            GetLastError(),
            (ULONG)flush,
            digest,
            status == STATUS_SUCCESS ? sizeof(digest) : 0,
            status == STATUS_SUCCESS ? 1 : 0);
    }

    /* =========================================================================
     * Case 8: Rosetta 2 / WoW64 LDT Selectors & %gs: Prefix Fault Recovery
     * Verifies RtlWow64GetThreadSelectorEntry (0x03 null, 0x23 CS32, 0x2b DS64,
     * 0x33 CS64, 0x53 FS32 TEB selector), NtSetLdtEntries custom LDT entry, and
     * NtdllRosettaCheckInvalidGsbase recovery when Rosetta 2 drops GSBASE.
     * ========================================================================= */
    {
        NTDLL_DESCRIPTOR_TABLE_ENTRY dte;
        NTDLL_LDT_ENTRY custom_ldt;
        NTDLL_LDT_ENTRY zero_ldt;
        NTDLL_ROSETTA_GSBASE_STATE gs_state;
        ULONG ret_len = 0;
        uint32_t fs32_base = 0;
        uint32_t fs32_limit = 0;
        uint32_t custom_base = 0;
        static const BYTE gs_mov_rax_teb[9] = {
            0x65, 0x48, 0x8b, 0x04, 0x25, 0x30, 0x00, 0x00, 0x00
        };
        uint8_t digest[24];

        memset(&dte, 0, sizeof(dte));
        memset(&custom_ldt, 0, sizeof(custom_ldt));
        memset(&zero_ldt, 0, sizeof(zero_ldt));
        memset(&gs_state, 0, sizeof(gs_state));
        memset(digest, 0, sizeof(digest));

        NtdllResetLdtState(0x000000007ffde000ULL);

        /* Query WoW64 %fs selector (0x53): Base must be TEB64 + 0x2000 = 0x7ffe0000, Limit = 0x0fff */
        dte.Selector = 0x53U;
        status = RtlWow64GetThreadSelectorEntry(NtCurrentThread(), &dte, sizeof(dte), &ret_len);
        if (status == STATUS_SUCCESS) {
            fs32_base = (uint32_t)dte.Descriptor.BaseLow |
                        ((uint32_t)dte.Descriptor.HighWord.Bytes.BaseMid << 16) |
                        ((uint32_t)dte.Descriptor.HighWord.Bytes.BaseHi << 24);
            fs32_limit = (uint32_t)dte.Descriptor.LimitLow |
                         ((uint32_t)dte.Descriptor.HighWord.Bits.LimitHi << 16);

            /* Install a custom LDT selector at index 1 (selector 0x0f = index 1, LDT=4, RPL=3) */
            custom_ldt.BaseLow = 0x4000U;
            custom_ldt.LimitLow = 0x0fffU;
            custom_ldt.HighWord.Bytes.BaseMid = 0x12U;
            custom_ldt.HighWord.Bytes.BaseHi = 0x00U;
            custom_ldt.HighWord.Bits.Type = 0x12U;
            custom_ldt.HighWord.Bits.Dpl = 3U;
            custom_ldt.HighWord.Bits.Pres = 1U;
            custom_ldt.HighWord.Bits.Default_Big = 1U;

            status = NtSetLdtEntries(0x000fU, custom_ldt, 0U, zero_ldt);
        }
        if (status == STATUS_SUCCESS) {
            memset(&dte, 0, sizeof(dte));
            dte.Selector = 0x000fU;
            status = NtQueryInformationThread(
                NtCurrentThread(), ThreadDescriptorTableEntry, &dte, sizeof(dte), &ret_len);
            custom_base = (uint32_t)dte.Descriptor.BaseLow |
                          ((uint32_t)dte.Descriptor.HighWord.Bytes.BaseMid << 16) |
                          ((uint32_t)dte.Descriptor.HighWord.Bytes.BaseHi << 24);
        }
        if (status == STATUS_SUCCESS) {
            (void)NtdllRosettaInitGsbase(0x000000007ffde000ULL, 1);
            (void)NtdllRosettaCorruptGsbase(0x0ULL);
            status = NtdllRosettaCheckInvalidGsbase(
                0x140001000ULL, gs_mov_rax_teb, sizeof(gs_mov_rax_teb), &gs_state);

            put_u32_le(digest + 0, fs32_base);
            put_u32_le(digest + 4, fs32_limit);
            put_u32_le(digest + 8, custom_base);
            put_u32_le(digest + 12, (uint32_t)gs_state.current_gsbase);
            put_u32_le(digest + 16, (uint32_t)gs_state.tsd_set_count);
            put_u32_le(digest + 20, (uint32_t)gs_state.gs_prefix_fault_fixup_count);
        }

        ok(status == STATUS_SUCCESS && fs32_base == 0x7ffe0000U && custom_base == 0x00124000U,
           "Expected WoW64 FS32 selector, custom LDT, and Rosetta GSBASE fixup, got status=0x%08x\n",
           (unsigned int)status);

        parity_record_exact(
            "signal_x86_64_rosetta_ldt_selectors_and_gsbase_fixup",
            "SignalX8664_RosettaLdtSelectors_And_GsbaseFixup",
            "RtlWow64GetThreadSelectorEntry",
            status,
            GetLastError(),
            ret_len,
            digest,
            status == STATUS_SUCCESS ? sizeof(digest) : 0,
            status == STATUS_SUCCESS ? 1 : 0);
    }

    parity_end_document();
    return g_winetest_failures == 0 ? 0 : 1;
}

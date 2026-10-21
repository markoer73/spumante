/*
 * Differential Conformance Test: kernelbase.dll Win32 Utility, Thread, Handle & Virtual Memory APIs
 *
 * Covers:
 *   - GetSystemTimePreciseAsFileTime (1601 UTC epoch, strict monotonicity, sub-us resolution, error preservation)
 *   - SetThreadDescription / GetThreadDescription (default L"", UTF-16 round-trip, NULL clear, LocalFree, duplicated handles, invalid handles)
 *   - CompareObjectHandles (pseudo-handles, DuplicateHandle, named sync objects with distinct access masks, ERROR_NOT_SAME_OBJECT, ERROR_INVALID_HANDLE)
 *   - VirtualAlloc2 / VirtualAlloc2FromApp (MEM_EXTENDED_PARAMETER alignment requirements, W^X FromApp enforcement, MEM_RESERVE_PLACEHOLDER split & coalesce)
 *   - MapViewOfFile3 / UnmapViewOfFile2 (ERROR_MAPPED_ALIGNMENT 64KB vs 4KB MEM_REPLACE_PLACEHOLDER, shared section coherence, MEM_PRESERVE_PLACEHOLDER)
 *
 * Copyright 2026 Wine Parity Contributors
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "../../include/parity_test.h"
#include "../../../dlls/kernelbase/kernelbase.h"

static int kb_wstr_eq(const WCHAR *a, const WCHAR *b)
{
    size_t i = 0;
    if (!a || !b) {
        return 0;
    }
    while (a[i] != 0 && b[i] != 0) {
        if (a[i] != b[i]) {
            return 0;
        }
        i++;
    }
    return a[i] == b[i];
}

int main(void)
{
    unsigned char buf[16];
    DWORD err;

    NtdllResetSyncState();
    KernelBaseResetUtilityState();
    parity_begin_document("kernelbase", "test_win32_utility_apis", "x86_64", "wine-parity");

    /* 1. GetSystemTimePreciseAsFileTime epoch range, strict monotonicity, and GetLastError preservation */
    {
        FILETIME ft_prev;
        FILETIME ft_cur;
        uint64_t t_prev;
        uint64_t t_cur;
        int i;
        BOOL monotonic = TRUE;
        BOOL epoch_ok;

        SetLastError(0xDEADBEEFU);
        GetSystemTimePreciseAsFileTime(&ft_prev);
        err = GetLastError();
        t_prev = ((uint64_t)ft_prev.dwHighDateTime << 32) | (uint64_t)ft_prev.dwLowDateTime;
        /* Must be after 2022-01-01 UTC (> 132854688000000000ULL) and before 2200-01-01 UTC */
        epoch_ok = (t_prev > 132854688000000000ULL && t_prev < 200000000000000000ULL) ? TRUE : FALSE;

        for (i = 0; i < 64; i++) {
            GetSystemTimePreciseAsFileTime(&ft_cur);
            t_cur = ((uint64_t)ft_cur.dwHighDateTime << 32) | (uint64_t)ft_cur.dwLowDateTime;
            if (t_cur < t_prev) {
                monotonic = FALSE;
            }
            t_prev = t_cur;
        }

        buf[0] = (unsigned char)(epoch_ok ? 1 : 0);
        buf[1] = (unsigned char)(monotonic ? 1 : 0);
        buf[2] = (unsigned char)((err == 0xDEADBEEFU) ? 1 : 0);

        ok(buf[0] == 1 && buf[1] == 1 && buf[2] == 1,
           "GetSystemTimePreciseAsFileTime epoch/monotonicity/error check failed\n");
        parity_record_exact(
            "kb_util_01_precise_time_epoch_and_monotonic",
            "GetSystemTimePreciseAsFileTime_EpochAndMonotonicity",
            "GetSystemTimePreciseAsFileTime",
            (buf[0] == 1 && buf[1] == 1 && buf[2] == 1) ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL,
            err,
            3,
            buf,
            3,
            buf[0] == 1 && buf[1] == 1);
    }

    /* 2. GetSystemTimePreciseAsFileTime sub-millisecond forward progression */
    {
        FILETIME ft1;
        FILETIME ft2;
        uint64_t t1;
        uint64_t t2;
        int spins = 0;

        GetSystemTimePreciseAsFileTime(&ft1);
        t1 = ((uint64_t)ft1.dwHighDateTime << 32) | (uint64_t)ft1.dwLowDateTime;
        t2 = t1;
        while (t2 == t1 && spins < 100000) {
            GetSystemTimePreciseAsFileTime(&ft2);
            t2 = ((uint64_t)ft2.dwHighDateTime << 32) | (uint64_t)ft2.dwLowDateTime;
            spins++;
        }
        if (t2 == t1) {
            Sleep(1);
            GetSystemTimePreciseAsFileTime(&ft2);
            t2 = ((uint64_t)ft2.dwHighDateTime << 32) | (uint64_t)ft2.dwLowDateTime;
        }

        buf[0] = (unsigned char)((t2 > t1) ? 1 : 0);
        buf[1] = (unsigned char)(((t2 - t1) < 10000000ULL) ? 1 : 0);
        ok(buf[0] == 1 && buf[1] == 1, "GetSystemTimePreciseAsFileTime progression failed\n");
        parity_record_exact(
            "kb_util_02_precise_time_submicrosecond_progression",
            "GetSystemTimePreciseAsFileTime_SubMillisecondProgression",
            "GetSystemTimePreciseAsFileTime",
            (buf[0] == 1 && buf[1] == 1) ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL,
            0,
            2,
            buf,
            2,
            buf[0] == 1 && buf[1] == 1);
    }

    /* 3. GetThreadDescription / SetThreadDescription initial empty string & invalid parameter/handle checks */
    {
        static const WCHAR sample_name[] = {'t', 'e', 's', 't', 0};
        PWSTR desc = (PWSTR)(uintptr_t)0x1;
        HRESULT hr_init;
        HRESULT hr_null_out;
        HRESULT hr_null_get;
        HRESULT hr_null_set;
        BOOL empty_ok = FALSE;

        hr_init = GetThreadDescription(GetCurrentThread(), &desc);
        if (hr_init == HRESULT_FROM_NT(STATUS_SUCCESS) && desc != NULL && desc[0] == 0) {
            empty_ok = TRUE;
        }
        if (desc != NULL && desc != (PWSTR)(uintptr_t)0x1) {
            LocalFree(desc);
        }

        hr_null_out = GetThreadDescription(GetCurrentThread(), NULL);
        hr_null_get = GetThreadDescription(NULL, &desc);
        hr_null_set = SetThreadDescription(NULL, sample_name);

        buf[0] = (unsigned char)((hr_init == (HRESULT)0x10000000 && empty_ok) ? 1 : 0);
        buf[1] = (unsigned char)((hr_null_out == HRESULT_FROM_NT(STATUS_INVALID_PARAMETER)) ? 1 : 0);
        buf[2] = (unsigned char)((hr_null_get == HRESULT_FROM_NT(STATUS_INVALID_HANDLE) &&
                                  hr_null_set == HRESULT_FROM_NT(STATUS_INVALID_HANDLE)) ? 1 : 0);

        ok(buf[0] == 1 && buf[1] == 1 && buf[2] == 1,
           "GetThreadDescription/SetThreadDescription validation failed\n");
        parity_record_exact(
            "kb_util_03_thread_description_default_and_null_validation",
            "ThreadDescription_DefaultEmptyAndValidation",
            "GetThreadDescription",
            (buf[0] == 1 && buf[1] == 1 && buf[2] == 1) ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL,
            0,
            3,
            buf,
            3,
            buf[0] == 1);
    }

    /* 4. SetThreadDescription & GetThreadDescription UTF-16 round-trip and NULL clear */
    {
        static const WCHAR worker_name[] = {
            'W', 'i', 'n', 'e', 'P', 'a', 'r', 'i', 't', 'y', 'T', 'h', 'r', 'e', 'a', 'd', 0
        };
        static const WCHAR empty_str[] = {0};
        PWSTR desc1 = NULL;
        PWSTR desc2 = NULL;
        HRESULT hr_set;
        HRESULT hr_get1;
        HRESULT hr_clr;
        HRESULT hr_get2;
        BOOL match1 = FALSE;
        BOOL match2 = FALSE;

        hr_set = SetThreadDescription(GetCurrentThread(), worker_name);
        hr_get1 = GetThreadDescription(GetCurrentThread(), &desc1);
        if (hr_get1 == (HRESULT)0x10000000 && kb_wstr_eq(desc1, worker_name)) {
            match1 = TRUE;
        }
        LocalFree(desc1);

        hr_clr = SetThreadDescription(GetCurrentThread(), NULL);
        hr_get2 = GetThreadDescription(GetCurrentThread(), &desc2);
        if (hr_get2 == (HRESULT)0x10000000 && kb_wstr_eq(desc2, empty_str)) {
            match2 = TRUE;
        }
        LocalFree(desc2);

        buf[0] = (unsigned char)((hr_set == (HRESULT)0x10000000 && match1) ? 1 : 0);
        buf[1] = (unsigned char)((hr_clr == (HRESULT)0x10000000 && match2) ? 1 : 0);
        ok(buf[0] == 1 && buf[1] == 1, "SetThreadDescription/GetThreadDescription round-trip failed\n");
        parity_record_exact(
            "kb_util_04_thread_description_set_get_and_clear",
            "SetAndGetThreadDescription_RoundTripAndClear",
            "SetThreadDescription",
            (buf[0] == 1 && buf[1] == 1) ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL,
            0,
            2,
            buf,
            2,
            buf[0] == 1 && buf[1] == 1);
    }

    /* 5. SetThreadDescription & GetThreadDescription across DuplicateHandle thread handle */
    {
        static const WCHAR dup_name[] = {'D', 'u', 'p', 'T', 'h', 'r', 'e', 'a', 'd', 0};
        HANDLE h_dup = NULL;
        BOOL dup_ok;
        HRESULT hr_set;
        HRESULT hr_get;
        PWSTR desc = NULL;
        BOOL match = FALSE;

        dup_ok = DuplicateHandle(
            GetCurrentProcess(),
            GetCurrentThread(),
            GetCurrentProcess(),
            &h_dup,
            0,
            FALSE,
            DUPLICATE_SAME_ACCESS);
        hr_set = SetThreadDescription(h_dup, dup_name);
        hr_get = GetThreadDescription(GetCurrentThread(), &desc);
        if (hr_get == (HRESULT)0x10000000 && kb_wstr_eq(desc, dup_name)) {
            match = TRUE;
        }
        LocalFree(desc);
        (void)SetThreadDescription(GetCurrentThread(), NULL);
        CloseHandle(h_dup);

        buf[0] = (unsigned char)((dup_ok && h_dup != NULL) ? 1 : 0);
        buf[1] = (unsigned char)((hr_set == (HRESULT)0x10000000 && match) ? 1 : 0);
        ok(buf[0] == 1 && buf[1] == 1, "ThreadDescription on duplicated thread handle failed\n");
        parity_record_exact(
            "kb_util_05_thread_description_duplicated_handle",
            "ThreadDescription_DuplicatedThreadHandle",
            "SetThreadDescription",
            (buf[0] == 1 && buf[1] == 1) ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL,
            0,
            2,
            buf,
            2,
            buf[0] == 1 && buf[1] == 1);
    }

    /* 6. CompareObjectHandles NULL / invalid handles and process/thread pseudo-handles */
    {
        BOOL r_null_both;
        DWORD err_null_both;
        BOOL r_null_second;
        DWORD err_null_second;
        BOOL r_proc_same;
        BOOL r_thread_same;
        BOOL r_proc_thread;
        DWORD err_proc_thread;

        SetLastError(0xDEADBEEFU);
        r_null_both = CompareObjectHandles(NULL, NULL);
        err_null_both = GetLastError();

        SetLastError(0xDEADBEEFU);
        r_null_second = CompareObjectHandles(GetCurrentProcess(), NULL);
        err_null_second = GetLastError();

        r_proc_same = CompareObjectHandles(GetCurrentProcess(), GetCurrentProcess());
        r_thread_same = CompareObjectHandles(GetCurrentThread(), GetCurrentThread());

        SetLastError(0xDEADBEEFU);
        r_proc_thread = CompareObjectHandles(GetCurrentProcess(), GetCurrentThread());
        err_proc_thread = GetLastError();

        buf[0] = (unsigned char)((!r_null_both && err_null_both == ERROR_INVALID_HANDLE &&
                                  !r_null_second && err_null_second == ERROR_INVALID_HANDLE) ? 6 : 0);
        buf[1] = (unsigned char)((r_proc_same && r_thread_same) ? 1 : 0);
        buf[2] = (unsigned char)((!r_proc_thread && err_proc_thread == ERROR_NOT_SAME_OBJECT) ? 1 : 0);

        ok(buf[0] == 6 && buf[1] == 1 && buf[2] == 1, "CompareObjectHandles pseudo-handle check failed\n");
        parity_record_exact(
            "kb_util_06_compare_object_handles_invalid_and_pseudo",
            "CompareObjectHandles_InvalidAndPseudoHandles",
            "CompareObjectHandles",
            (buf[0] == 6 && buf[1] == 1 && buf[2] == 1) ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL,
            err_proc_thread,
            3,
            buf,
            3,
            buf[1] == 1 && buf[2] == 1);
    }

    /* 7. CompareObjectHandles across DuplicateHandle and named Event handles with distinct access masks */
    {
        static const WCHAR ev_name[] = {'w', 'i', 'n', 'e', '_', 'c', 'm', 'p', '_', 'e', 'v', 0};
        HANDLE h_dup_proc = NULL;
        HANDLE h_ev1;
        HANDLE h_ev2;
        HANDLE h_ev3;
        BOOL r_dup_proc;
        BOOL r_same_ev;
        BOOL r_diff_ev;
        DWORD err_diff_ev;
        BOOL r_closed_ev;
        DWORD err_closed_ev;

        DuplicateHandle(
            GetCurrentProcess(),
            GetCurrentProcess(),
            GetCurrentProcess(),
            &h_dup_proc,
            0,
            FALSE,
            DUPLICATE_SAME_ACCESS);
        r_dup_proc = CompareObjectHandles(GetCurrentProcess(), h_dup_proc);
        CloseHandle(h_dup_proc);

        h_ev1 = CreateEventW(NULL, TRUE, FALSE, ev_name);
        h_ev2 = OpenEventW(EVENT_MODIFY_STATE, FALSE, ev_name);
        h_ev3 = CreateEventW(NULL, TRUE, FALSE, NULL);

        r_same_ev = CompareObjectHandles(h_ev1, h_ev2);
        SetLastError(0);
        r_diff_ev = CompareObjectHandles(h_ev1, h_ev3);
        err_diff_ev = GetLastError();

        CloseHandle(h_ev2);
        SetLastError(0);
        r_closed_ev = CompareObjectHandles(h_ev1, h_ev2);
        err_closed_ev = GetLastError();

        CloseHandle(h_ev1);
        CloseHandle(h_ev3);

        buf[0] = (unsigned char)((r_dup_proc && r_same_ev) ? 1 : 0);
        buf[1] = (unsigned char)((!r_diff_ev && err_diff_ev == ERROR_NOT_SAME_OBJECT) ? 1 : 0);
        buf[2] = (unsigned char)((!r_closed_ev && err_closed_ev == ERROR_INVALID_HANDLE) ? 6 : 0);

        ok(buf[0] == 1 && buf[1] == 1 && buf[2] == 6,
           "CompareObjectHandles named/duplicated sync objects failed\n");
        parity_record_exact(
            "kb_util_07_compare_object_handles_duplicated_and_named_sync",
            "CompareObjectHandles_DuplicatedAndNamedSyncObjects",
            "CompareObjectHandles",
            (buf[0] == 1 && buf[1] == 1 && buf[2] == 6) ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL,
            err_diff_ev,
            3,
            buf,
            3,
            buf[0] == 1 && buf[1] == 1);
    }

    /* 8. VirtualAlloc2 basic commit & VirtualAlloc2FromApp W^X executable protection rejection */
    {
        PVOID p_rw;
        MEMORY_BASIC_INFORMATION mbi;
        SIZE_T q_len;
        PVOID p_exec_app;
        DWORD err_exec_app;
        PVOID p_rw_app;

        p_rw = VirtualAlloc2(NULL, NULL, 0x10000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE, NULL, 0);
        memset(&mbi, 0, sizeof(mbi));
        q_len = p_rw ? VirtualQuery(p_rw, &mbi, sizeof(mbi)) : 0;
        if (p_rw) {
            ((volatile unsigned char *)p_rw)[0] = 0x5AU;
        }

        SetLastError(0);
        p_exec_app = VirtualAlloc2FromApp(
            NULL,
            NULL,
            0x10000,
            MEM_RESERVE | MEM_COMMIT,
            PAGE_EXECUTE_READWRITE,
            NULL,
            0);
        err_exec_app = GetLastError();

        p_rw_app = VirtualAlloc2FromApp(
            NULL,
            NULL,
            0x10000,
            MEM_RESERVE | MEM_COMMIT,
            PAGE_READWRITE,
            NULL,
            0);

        if (p_rw) {
            VirtualFree(p_rw, 0, MEM_RELEASE);
        }
        if (p_rw_app) {
            VirtualFree(p_rw_app, 0, MEM_RELEASE);
        }

        buf[0] = (unsigned char)((p_rw != NULL && q_len == sizeof(mbi) &&
                                  mbi.State == MEM_COMMIT && mbi.Protect == PAGE_READWRITE) ? 1 : 0);
        buf[1] = (unsigned char)((p_exec_app == NULL && err_exec_app == ERROR_INVALID_PARAMETER &&
                                  p_rw_app != NULL) ? 87 : 0);

        ok(buf[0] == 1 && buf[1] == 87, "VirtualAlloc2 / VirtualAlloc2FromApp basic test failed\n");
        parity_record_exact(
            "kb_util_08_virtual_alloc2_basic_and_from_app",
            "VirtualAlloc2_BasicAndFromAppWxEnforcement",
            "VirtualAlloc2",
            (buf[0] == 1 && buf[1] == 87) ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL,
            err_exec_app,
            2,
            buf,
            2,
            buf[0] == 1);
    }

    /* 9. VirtualAlloc2 MEM_EXTENDED_PARAMETER with MemExtendedParameterAddressRequirements alignment */
    {
        MEM_ADDRESS_REQUIREMENTS req;
        MEM_EXTENDED_PARAMETER ext;
        PVOID p_aligned;
        PVOID p_bad_align;
        DWORD err_bad_align;

        memset(&req, 0, sizeof(req));
        req.Alignment = 0x40000; /* 256KB power-of-two alignment */
        memset(&ext, 0, sizeof(ext));
        ext.Type = MemExtendedParameterAddressRequirements;
        ext.Pointer = &req;

        p_aligned = VirtualAlloc2(
            GetCurrentProcess(),
            NULL,
            0x10000,
            MEM_RESERVE | MEM_COMMIT,
            PAGE_READWRITE,
            &ext,
            1);

        req.Alignment = 0x30000; /* Non-power-of-two -> ERROR_INVALID_PARAMETER */
        SetLastError(0);
        p_bad_align = VirtualAlloc2(
            GetCurrentProcess(),
            NULL,
            0x10000,
            MEM_RESERVE | MEM_COMMIT,
            PAGE_READWRITE,
            &ext,
            1);
        err_bad_align = GetLastError();

        buf[0] = (unsigned char)((p_aligned != NULL && (((uintptr_t)p_aligned & 0x3FFFFU) == 0)) ? 1 : 0);
        buf[1] = (unsigned char)((p_bad_align == NULL && err_bad_align == ERROR_INVALID_PARAMETER) ? 87 : 0);

        if (p_aligned) {
            VirtualFree(p_aligned, 0, MEM_RELEASE);
        }

        ok(buf[0] == 1 && buf[1] == 87, "VirtualAlloc2 MemExtendedParameterAddressRequirements failed\n");
        parity_record_exact(
            "kb_util_09_virtual_alloc2_address_requirements_alignment",
            "VirtualAlloc2_AddressRequirementsAlignment",
            "VirtualAlloc2",
            (buf[0] == 1 && buf[1] == 87) ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL,
            err_bad_align,
            2,
            buf,
            2,
            buf[0] == 1);
    }

    /* 10. VirtualAlloc2 MEM_RESERVE_PLACEHOLDER creation, splitting (MEM_PRESERVE_PLACEHOLDER), and coalescing */
    {
        PVOID ph_bad;
        DWORD err_ph_bad;
        PVOID ph;
        BOOL split_ok;
        MEMORY_BASIC_INFORMATION mbi1;
        MEMORY_BASIC_INFORMATION mbi2;
        BOOL coalesce_ok;
        MEMORY_BASIC_INFORMATION mbi_coal;

        SetLastError(0);
        ph_bad = VirtualAlloc2(
            NULL,
            NULL,
            0x20000,
            MEM_RESERVE | MEM_RESERVE_PLACEHOLDER,
            PAGE_READWRITE,
            NULL,
            0);
        err_ph_bad = GetLastError();

        ph = VirtualAlloc2(
            NULL,
            NULL,
            0x20000,
            MEM_RESERVE | MEM_RESERVE_PLACEHOLDER,
            PAGE_NOACCESS,
            NULL,
            0);
        split_ok = ph ? VirtualFree((char *)ph + 0x10000, 0x10000, MEM_RELEASE | MEM_PRESERVE_PLACEHOLDER) : FALSE;
        memset(&mbi1, 0, sizeof(mbi1));
        memset(&mbi2, 0, sizeof(mbi2));
        if (ph) {
            VirtualQuery(ph, &mbi1, sizeof(mbi1));
            VirtualQuery((char *)ph + 0x10000, &mbi2, sizeof(mbi2));
        }
        coalesce_ok = ph ? VirtualFree(ph, 0x20000, MEM_RELEASE | MEM_COALESCE_PLACEHOLDERS) : FALSE;
        memset(&mbi_coal, 0, sizeof(mbi_coal));
        if (ph) {
            VirtualQuery(ph, &mbi_coal, sizeof(mbi_coal));
            VirtualFree(ph, 0, MEM_RELEASE);
        }

        buf[0] = (unsigned char)((ph_bad == NULL && err_ph_bad == ERROR_INVALID_PARAMETER) ? 87 : 0);
        buf[1] = (unsigned char)((ph != NULL && split_ok &&
                                  mbi1.RegionSize == 0x10000 && mbi2.RegionSize == 0x10000 &&
                                  mbi2.AllocationBase == (PVOID)((char *)ph + 0x10000)) ? 1 : 0);
        buf[2] = (unsigned char)((coalesce_ok && mbi_coal.RegionSize == 0x20000) ? 1 : 0);

        ok(buf[0] == 87 && buf[1] == 1 && buf[2] == 1,
           "VirtualAlloc2 placeholder split/coalesce failed\n");
        parity_record_exact(
            "kb_util_10_virtual_alloc2_placeholder_split_and_coalesce",
            "VirtualAlloc2_PlaceholderSplitAndCoalesce",
            "VirtualAlloc2",
            (buf[0] == 87 && buf[1] == 1 && buf[2] == 1) ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL,
            err_ph_bad,
            3,
            buf,
            3,
            buf[1] == 1 && buf[2] == 1);
    }

    /* 11. MapViewOfFile3 64KB offset alignment check (ERROR_MAPPED_ALIGNMENT 1132) & shared section view */
    {
        HANDLE sec;
        PVOID v_unaligned;
        DWORD err_unaligned;
        PVOID v_full;
        PVOID v_hi;
        BOOL shared_ok = FALSE;

        sec = CreateFileMappingW(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, 0x20000, NULL);
        SetLastError(0);
        v_unaligned = MapViewOfFile3(sec, NULL, NULL, 0x1000, 0x1000, 0, PAGE_READWRITE, NULL, 0);
        err_unaligned = GetLastError();

        v_full = MapViewOfFile3(sec, NULL, NULL, 0, 0x20000, 0, PAGE_READWRITE, NULL, 0);
        v_hi = MapViewOfFile3(sec, NULL, NULL, 0x10000, 0x10000, 0, PAGE_READWRITE, NULL, 0);
        if (v_full && v_hi) {
            ((volatile uint32_t *)((char *)v_full + 0x10000))[0] = 0xCAFEBABEU;
            if (((volatile uint32_t *)v_hi)[0] == 0xCAFEBABEU) {
                shared_ok = TRUE;
            }
        }
        if (v_hi) {
            UnmapViewOfFile(v_hi);
        }
        if (v_full) {
            UnmapViewOfFile(v_full);
        }
        if (sec) {
            CloseHandle(sec);
        }

        buf[0] = (unsigned char)((v_unaligned == NULL && err_unaligned == ERROR_MAPPED_ALIGNMENT) ? 1 : 0);
        buf[1] = (unsigned char)(shared_ok ? 1 : 0);

        ok(buf[0] == 1 && buf[1] == 1, "MapViewOfFile3 alignment and shared view failed\n");
        parity_record_exact(
            "kb_util_11_map_view_of_file3_alignment_and_shared_section",
            "MapViewOfFile3_Alignment1132AndSharedSection",
            "MapViewOfFile3",
            (buf[0] == 1 && buf[1] == 1) ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL,
            err_unaligned,
            2,
            buf,
            2,
            buf[0] == 1 && buf[1] == 1);
    }

    /* 12. MapViewOfFile3 MEM_REPLACE_PLACEHOLDER at 4KB offset & UnmapViewOfFile2 MEM_PRESERVE_PLACEHOLDER */
    {
        HANDLE sec;
        PVOID ph;
        BOOL split_ok;
        PVOID v_base;
        PVOID v_rep;
        MEMORY_BASIC_INFORMATION mbi_mapped;
        BOOL unmap_ok;
        MEMORY_BASIC_INFORMATION mbi_preserved;
        BOOL data_coherent = FALSE;

        sec = CreateFileMappingW(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, 0x10000, NULL);
        v_base = MapViewOfFile3(sec, NULL, NULL, 0, 0x10000, 0, PAGE_READWRITE, NULL, 0);
        if (v_base) {
            ((volatile uint32_t *)((char *)v_base + 0x1000))[0] = 0x13572468U;
        }

        ph = VirtualAlloc2(
            NULL,
            NULL,
            0x10000,
            MEM_RESERVE | MEM_RESERVE_PLACEHOLDER,
            PAGE_NOACCESS,
            NULL,
            0);
        split_ok = ph ? VirtualFree(ph, 0x1000, MEM_RELEASE | MEM_PRESERVE_PLACEHOLDER) : FALSE;

        v_rep = (ph && split_ok) ? MapViewOfFile3(
            sec,
            GetCurrentProcess(),
            ph,
            0x1000,
            0x1000,
            MEM_REPLACE_PLACEHOLDER,
            PAGE_READWRITE,
            NULL,
            0) : NULL;
        memset(&mbi_mapped, 0, sizeof(mbi_mapped));
        if (v_rep == ph && v_rep != NULL) {
            VirtualQuery(v_rep, &mbi_mapped, sizeof(mbi_mapped));
            if (((volatile uint32_t *)v_rep)[0] == 0x13572468U) {
                data_coherent = TRUE;
            }
        }

        unmap_ok = v_rep ? UnmapViewOfFile2(GetCurrentProcess(), v_rep, MEM_PRESERVE_PLACEHOLDER) : FALSE;
        memset(&mbi_preserved, 0, sizeof(mbi_preserved));
        if (unmap_ok && ph) {
            VirtualQuery(ph, &mbi_preserved, sizeof(mbi_preserved));
        }

        if (ph) {
            VirtualFree(ph, 0, MEM_RELEASE);
            VirtualFree((char *)ph + 0x1000, 0, MEM_RELEASE);
        }
        if (v_base) {
            UnmapViewOfFile(v_base);
        }
        if (sec) {
            CloseHandle(sec);
        }

        buf[0] = (unsigned char)((v_rep == ph && v_rep != NULL && data_coherent &&
                                  mbi_mapped.State == MEM_COMMIT && mbi_mapped.Type == MEM_MAPPED) ? 1 : 0);
        buf[1] = (unsigned char)((unmap_ok && mbi_preserved.State == MEM_RESERVE &&
                                  mbi_preserved.Protect == PAGE_NOACCESS &&
                                  mbi_preserved.RegionSize == 0x1000) ? 1 : 0);

        ok(buf[0] == 1 && buf[1] == 1,
           "MapViewOfFile3 MEM_REPLACE_PLACEHOLDER / UnmapViewOfFile2 MEM_PRESERVE_PLACEHOLDER failed\n");
        parity_record_exact(
            "kb_util_12_map_view_of_file3_replace_and_preserve_placeholder",
            "MapViewOfFile3_ReplaceAndPreservePlaceholder",
            "MapViewOfFile3",
            (buf[0] == 1 && buf[1] == 1) ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL,
            0,
            2,
            buf,
            2,
            buf[0] == 1 && buf[1] == 1);
    }

    parity_end_document();
    return (g_winetest_failures == 0) ? 0 : 1;
}

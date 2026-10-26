/*
 * Differential Conformance Test: kernelbase.dll Synchronization (WaitOnAddress / Barrier / InitOnce / Objects)
 *
 * Covers:
 *   - WaitOnAddress / WakeByAddressSingle / WakeByAddressAll (sizes, GetLastError preservation, ERROR_TIMEOUT, multi-threaded contention)
 *   - InitializeSynchronizationBarrier / EnterSynchronizationBarrier / DeleteSynchronizationBarrier
 *   - InitOnceInitialize / InitOnceBeginInitialize / InitOnceComplete / InitOnceExecuteOnce (sync & async)
 *   - CreateEventExW / SetEvent / ResetEvent / PulseEvent / WaitForSingleObject / CloseHandle
 *   - CreateMutexExW / ReleaseMutex (ERROR_NOT_OWNER)
 *   - CreateSemaphoreExW / ReleaseSemaphore (ERROR_TOO_MANY_POSTS)
 *   - WaitForMultipleObjectsEx / SignalObjectAndWait / SleepEx
 *
 * Copyright 2026 Spumante Contributors
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "../../include/parity_test.h"
#include "../../../dlls/kernelbase/kernelbase.h"

#ifndef _WIN32
#include <pthread.h>
#include <sched.h>
#endif

typedef struct _KB_WORKER_CTX {
    void (*fn)(void *arg);
    void *arg;
    volatile LONG done;
#ifndef _WIN32
    pthread_t thread;
#else
    HANDLE thread;
#endif
} KB_WORKER_CTX;

#ifndef _WIN32
static void *kb_worker_entry(void *param)
{
    KB_WORKER_CTX *ctx = (KB_WORKER_CTX *)param;
    ctx->fn(ctx->arg);
    __atomic_store_n(&ctx->done, 1, __ATOMIC_SEQ_CST);
    return NULL;
}
#else
static DWORD WINAPI kb_worker_entry(LPVOID param)
{
    KB_WORKER_CTX *ctx = (KB_WORKER_CTX *)param;
    ctx->fn(ctx->arg);
    __atomic_store_n(&ctx->done, 1, __ATOMIC_SEQ_CST);
    return 0;
}
#endif

static void kb_spawn_worker(KB_WORKER_CTX *ctx, void (*fn)(void *), void *arg)
{
    ctx->fn = fn;
    ctx->arg = arg;
    ctx->done = 0;
#ifndef _WIN32
    pthread_create(&ctx->thread, NULL, kb_worker_entry, ctx);
#else
    ctx->thread = CreateThread(NULL, 0, kb_worker_entry, ctx, 0, NULL);
#endif
}

static void kb_join_worker(KB_WORKER_CTX *ctx)
{
#ifndef _WIN32
    pthread_join(ctx->thread, NULL);
#else
    while (__atomic_load_n(&ctx->done, __ATOMIC_SEQ_CST) == 0) {
        SwitchToThread();
    }
#endif
}

typedef struct _KB_WOA_CONTENTION {
    volatile LONG address;
    volatile LONG failures;
} KB_WOA_CONTENTION;

static void kb_woa_thread_proc(void *param)
{
    KB_WOA_CONTENTION *shared = (KB_WOA_CONTENTION *)param;
    LONG val = __atomic_load_n(&shared->address, __ATOMIC_SEQ_CST);
    while (val > 0) {
        BOOL ret = WaitOnAddress(&shared->address, &val, sizeof(val), 100);
        if (!ret && GetLastError() != ERROR_TIMEOUT) {
            __atomic_add_fetch(&shared->failures, 1, __ATOMIC_SEQ_CST);
            break;
        }
        val = __atomic_load_n(&shared->address, __ATOMIC_SEQ_CST);
    }
}

static BOOL WINAPI kb_init_once_cb(PINIT_ONCE once, PVOID param, PVOID *context)
{
    ULONG *cnt = (ULONG *)param;
    (void)once;
    (*cnt)++;
    if (context) {
        *context = (PVOID)(uintptr_t)0xCAFE0040U;
    }
    return TRUE;
}

int main(void)
{
    unsigned char buf[16];
    BOOL ok_flag;
    DWORD err;

    NtdllResetSyncState();
    parity_begin_document("kernelbase", "test_wait_on_address", "x86_64", "spumante");

    /* 1. WaitOnAddress invalid AddressSize values (0, 3, 5, 7) and NULL address */
    {
        int64_t val = 0;
        SIZE_T bad_sizes[4] = {0, 3, 5, 7};
        int i;
        BOOL all_failed = TRUE;
        DWORD last_err = 0;

        for (i = 0; i < 4; i++) {
            SetLastError(0xDEADBEEFU);
            ok_flag = WaitOnAddress(&val, &val, bad_sizes[i], 0);
            last_err = GetLastError();
            if (ok_flag || last_err != ERROR_INVALID_PARAMETER) {
                all_failed = FALSE;
            }
            buf[i] = (unsigned char)last_err;
        }
        SetLastError(0xDEADBEEFU);
        ok_flag = WaitOnAddress(NULL, NULL, 0, 0);
        last_err = GetLastError();
        buf[4] = (unsigned char)((!ok_flag && last_err == ERROR_INVALID_PARAMETER) ? 87 : 0);

        ok(all_failed && buf[4] == 87, "Expected ERROR_INVALID_PARAMETER (87) for invalid sizes\n");
        parity_record_exact(
            "kb_sync_01_wait_on_address_invalid_sizes",
            "WaitOnAddress_InvalidAddressSizes_Error87",
            "WaitOnAddress",
            all_failed ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL,
            last_err,
            5,
            buf,
            5,
            0);
    }

    /* 2. WaitOnAddress value mismatch returns TRUE and does NOT modify GetLastError() */
    {
        int64_t val = 0x1122334455667788LL;
        int64_t cmp = 0x0000000000000000LL;
        SIZE_T sizes[4] = {1, 2, 4, 8};
        int i;
        BOOL all_ok = TRUE;

        for (i = 0; i < 4; i++) {
            SetLastError(0xDEADBEEFU);
            ok_flag = WaitOnAddress(&val, &cmp, sizes[i], 0);
            err = GetLastError();
            if (!ok_flag || err != 0xDEADBEEFU) {
                all_ok = FALSE;
            }
            buf[i] = (unsigned char)(ok_flag ? sizes[i] : 0);
        }
        ok(all_ok, "Expected WaitOnAddress mismatch to return TRUE and preserve GetLastError()\n");
        parity_record_exact(
            "kb_sync_02_wait_on_address_mismatch_preserves_last_error",
            "WaitOnAddress_MismatchPreservesLastError",
            "WaitOnAddress",
            all_ok ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL,
            0xDEADBEEFU,
            4,
            buf,
            4,
            all_ok);
    }

    /* 3. WaitOnAddress equal values returns FALSE and sets ERROR_TIMEOUT (1460) */
    {
        int64_t val = 0x123456789ABCDEF0LL;
        SIZE_T sizes[4] = {1, 2, 4, 8};
        int i;
        BOOL all_to = TRUE;
        DWORD last_err = 0;

        for (i = 0; i < 4; i++) {
            SetLastError(0xDEADBEEFU);
            ok_flag = WaitOnAddress(&val, &val, sizes[i], 0);
            last_err = GetLastError();
            if (ok_flag || last_err != ERROR_TIMEOUT) {
                all_to = FALSE;
            }
            buf[i] = (unsigned char)((!ok_flag && last_err == ERROR_TIMEOUT) ? 1 : 0);
        }
        ok(all_to, "Expected WaitOnAddress timeout to return FALSE and set ERROR_TIMEOUT (1460)\n");
        parity_record_exact(
            "kb_sync_03_wait_on_address_timeout_sets_error_timeout",
            "WaitOnAddress_TimeoutSetsError1460",
            "WaitOnAddress",
            all_to ? STATUS_TIMEOUT : STATUS_UNSUCCESSFUL,
            last_err,
            4,
            buf,
            4,
            0);
    }

    /* 4. WakeByAddressSingle(NULL) and WakeByAddressAll(NULL) preserve GetLastError() */
    {
        int64_t val = 0;
        SetLastError(0xDEADBEEFU);
        WakeByAddressSingle(NULL);
        err = GetLastError();
        buf[0] = (unsigned char)((err == 0xDEADBEEFU) ? 1 : 0);

        WakeByAddressAll(NULL);
        err = GetLastError();
        buf[1] = (unsigned char)((err == 0xDEADBEEFU) ? 1 : 0);

        WakeByAddressSingle(&val);
        WakeByAddressAll(&val);
        err = GetLastError();
        buf[2] = (unsigned char)((err == 0xDEADBEEFU) ? 1 : 0);

        ok(buf[0] == 1 && buf[1] == 1 && buf[2] == 1, "Expected WakeByAddressSingle/All to preserve GetLastError()\n");
        parity_record_exact(
            "kb_sync_04_wake_by_address_null_preserves_last_error",
            "WakeByAddressSingleAndAll_NullAndErrorPreservation",
            "WakeByAddressSingle",
            (buf[0] == 1 && buf[1] == 1 && buf[2] == 1) ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL,
            err,
            3,
            buf,
            3,
            buf[0] == 1 && buf[1] == 1 && buf[2] == 1);
    }

    /* 5. Multi-threaded WaitOnAddress + WakeByAddressSingle / WakeByAddressAll contention */
    {
        KB_WOA_CONTENTION shared;
        KB_WORKER_CTX workers[4];
        int i;

        shared.address = 200;
        shared.failures = 0;
        for (i = 0; i < 4; i++) {
            kb_spawn_worker(&workers[i], kb_woa_thread_proc, &shared);
        }
        while (__atomic_load_n(&shared.address, __ATOMIC_SEQ_CST) > 0) {
            LONG next_val = __atomic_sub_fetch(&shared.address, 1, __ATOMIC_SEQ_CST);
            if ((next_val & 3) == 0) {
                WakeByAddressAll((PVOID)&shared.address);
            } else {
                WakeByAddressSingle((PVOID)&shared.address);
            }
        }
        WakeByAddressAll((PVOID)&shared.address);
        for (i = 0; i < 4; i++) {
            kb_join_worker(&workers[i]);
        }

        buf[0] = (unsigned char)((shared.address == 0 && shared.failures == 0) ? 1 : 0);
        buf[1] = 4;
        ok(buf[0] == 1, "Expected 4-thread WaitOnAddress contention to complete with 0 failures\n");
        parity_record_exact(
            "kb_sync_05_wait_and_wake_single_all_threaded",
            "WaitOnAddress_MultiThreadedContention",
            "WakeByAddressAll",
            (buf[0] == 1) ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL,
            0,
            2,
            buf,
            2,
            buf[0] == 1);
    }

    /* 6. InitializeSynchronizationBarrier, EnterSynchronizationBarrier, DeleteSynchronizationBarrier */
    {
        SYNCHRONIZATION_BARRIER bar;
        BOOL r_bad0;
        DWORD err_bad0;
        BOOL r_bad_neg;
        DWORD err_bad_neg;
        BOOL r_init;
        BOOL r_enter;
        BOOL r_del;
        DWORD err_del;

        SetLastError(0xDEADBEEFU);
        r_bad0 = InitializeSynchronizationBarrier(&bar, 0, 0);
        err_bad0 = GetLastError();

        SetLastError(0xDEADBEEFU);
        r_bad_neg = InitializeSynchronizationBarrier(&bar, -1, 0);
        err_bad_neg = GetLastError();

        r_init = InitializeSynchronizationBarrier(&bar, 1, 100);
        r_enter = EnterSynchronizationBarrier(&bar, SYNCHRONIZATION_BARRIER_FLAGS_BLOCK_ONLY);
        SetLastError(0xDEADBEEFU);
        r_del = DeleteSynchronizationBarrier(&bar);
        err_del = GetLastError();

        buf[0] = (unsigned char)((!r_bad0 && err_bad0 == ERROR_INVALID_PARAMETER &&
                                  !r_bad_neg && err_bad_neg == ERROR_INVALID_PARAMETER) ? 87 : 0);
        buf[1] = (unsigned char)((r_init && r_enter && r_del && err_del == 0xDEADBEEFU) ? 1 : 0);
        ok(buf[0] == 87 && buf[1] == 1, "Unexpected SynchronizationBarrier behavior\n");
        parity_record_exact(
            "kb_sync_06_synchronization_barrier_validation_and_enter",
            "InitializeAndEnterSynchronizationBarrier",
            "InitializeSynchronizationBarrier",
            (buf[0] == 87 && buf[1] == 1) ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL,
            err_bad0,
            2,
            buf,
            2,
            buf[1] == 1);
    }

    /* 7. InitOnceInitialize, InitOnceBeginInitialize (CHECK_ONLY & synchronous), InitOnceComplete */
    {
        INIT_ONCE once;
        BOOL pending = FALSE;
        PVOID ctx = NULL;
        BOOL r_chk_before;
        DWORD err_chk_before;
        BOOL r_beg;
        BOOL r_cmp;
        BOOL r_chk_after;
        BOOL pending_after = TRUE;

        InitOnceInitialize(&once);
        SetLastError(0xDEADBEEFU);
        r_chk_before = InitOnceBeginInitialize(&once, INIT_ONCE_CHECK_ONLY, &pending, &ctx);
        err_chk_before = GetLastError();

        r_beg = InitOnceBeginInitialize(&once, 0, &pending, &ctx);
        r_cmp = InitOnceComplete(&once, 0, (PVOID)(uintptr_t)0x12340000U);
        r_chk_after = InitOnceBeginInitialize(&once, INIT_ONCE_CHECK_ONLY, &pending_after, &ctx);

        buf[0] = (unsigned char)((!r_chk_before && err_chk_before == ERROR_INVALID_PARAMETER) ? 87 : 0);
        buf[1] = (unsigned char)((r_beg && pending == TRUE && r_cmp) ? 1 : 0);
        buf[2] = (unsigned char)((r_chk_after && pending_after == FALSE && ctx == (PVOID)(uintptr_t)0x12340000U) ? 1 : 0);
        ok(buf[0] == 87 && buf[1] == 1 && buf[2] == 1, "Unexpected InitOnce synchronous behavior\n");
        parity_record_exact(
            "kb_sync_07_init_once_begin_and_complete",
            "InitOnceBeginInitialize_SyncAndCheckOnly",
            "InitOnceBeginInitialize",
            (buf[0] == 87 && buf[1] == 1 && buf[2] == 1) ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL,
            err_chk_before,
            3,
            buf,
            3,
            buf[1] == 1 && buf[2] == 1);
    }

    /* 8. InitOnceBeginInitialize(INIT_ONCE_ASYNC), InitOnceComplete, and InitOnceExecuteOnce */
    {
        INIT_ONCE once_async;
        INIT_ONCE once_exec;
        BOOL pending = FALSE;
        PVOID ctx = NULL;
        ULONG cb_count = 0;
        BOOL r_beg_a;
        BOOL r_cmp_bad;
        DWORD err_cmp_bad;
        BOOL r_cmp_ok;
        BOOL r_exec1;
        BOOL r_exec2;

        InitOnceInitialize(&once_async);
        r_beg_a = InitOnceBeginInitialize(&once_async, INIT_ONCE_ASYNC, &pending, &ctx);
        SetLastError(0xDEADBEEFU);
        r_cmp_bad = InitOnceComplete(&once_async, 0, (PVOID)(uintptr_t)0x56780000U);
        err_cmp_bad = GetLastError();
        r_cmp_ok = InitOnceComplete(&once_async, INIT_ONCE_ASYNC, (PVOID)(uintptr_t)0x56780000U);

        InitOnceInitialize(&once_exec);
        r_exec1 = InitOnceExecuteOnce(&once_exec, kb_init_once_cb, &cb_count, &ctx);
        r_exec2 = InitOnceExecuteOnce(&once_exec, kb_init_once_cb, &cb_count, &ctx);

        buf[0] = (unsigned char)((r_beg_a && pending && !r_cmp_bad && err_cmp_bad == ERROR_INVALID_PARAMETER && r_cmp_ok) ? 1 : 0);
        buf[1] = (unsigned char)((r_exec1 && r_exec2 && cb_count == 1 && ctx == (PVOID)(uintptr_t)0xCAFE0040U) ? 1 : 0);
        ok(buf[0] == 1 && buf[1] == 1, "Unexpected InitOnce async / ExecuteOnce behavior\n");
        parity_record_exact(
            "kb_sync_08_init_once_async_and_execute_once",
            "InitOnceBeginInitialize_AsyncAndExecuteOnce",
            "InitOnceExecuteOnce",
            (buf[0] == 1 && buf[1] == 1) ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL,
            err_cmp_bad,
            2,
            buf,
            2,
            buf[0] == 1 && buf[1] == 1);
    }

    /* 9. CreateEventExW flags validation, named event collision (ERROR_ALREADY_EXISTS), Set/Reset/Wait */
    {
        static const WCHAR ev_name[] = {'w', 'i', 'n', 'e', '_', 'p', 'a', 'r', 'i', 't', 'y', '_', 'e', 'v', 0};
        HANDLE h_bad;
        DWORD err_bad;
        HANDLE h1;
        HANDLE h2;
        DWORD err_dup;
        DWORD w1;
        DWORD w2;

        SetLastError(0xDEADBEEFU);
        h_bad = CreateEventExW(NULL, NULL, 0x80000000U, EVENT_ALL_ACCESS);
        err_bad = GetLastError();

        SetLastError(0);
        h1 = CreateEventExW(NULL, ev_name, CREATE_EVENT_INITIAL_SET, EVENT_ALL_ACCESS);
        SetLastError(0);
        h2 = CreateEventExW(NULL, ev_name, 0, EVENT_ALL_ACCESS);
        err_dup = GetLastError();

        w1 = WaitForSingleObject(h1, 0);
        w2 = WaitForSingleObject(h1, 0);
        SetEvent(h2);
        ResetEvent(h1);
        CloseHandle(h1);
        CloseHandle(h2);

        buf[0] = (unsigned char)((h_bad == NULL && err_bad == ERROR_INVALID_PARAMETER) ? 87 : 0);
        buf[1] = (unsigned char)((h1 != NULL && h2 != NULL && err_dup == ERROR_ALREADY_EXISTS) ? 183 : 0);
        buf[2] = (unsigned char)((w1 == WAIT_OBJECT_0 && w2 == WAIT_TIMEOUT) ? 1 : 0);
        ok(buf[0] == 87 && buf[1] == 183 && buf[2] == 1, "Unexpected CreateEventExW behavior\n");
        parity_record_exact(
            "kb_sync_09_event_create_ex_wait_set_reset",
            "CreateEventExW_ValidationNamedAndAutoReset",
            "CreateEventExW",
            (buf[0] == 87 && buf[1] == 183 && buf[2] == 1) ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL,
            err_dup,
            3,
            buf,
            3,
            buf[2] == 1);
    }

    /* 10. CreateMutexExW initial ownership, recursive wait, ReleaseMutex, ERROR_NOT_OWNER (288) */
    {
        HANDLE h_mut = CreateMutexExW(NULL, NULL, CREATE_MUTEX_INITIAL_OWNER, MUTANT_ALL_ACCESS);
        DWORD w_rec = WaitForSingleObject(h_mut, 0);
        BOOL r1 = ReleaseMutex(h_mut);
        BOOL r2 = ReleaseMutex(h_mut);
        BOOL r_unowned;
        DWORD err_unowned;

        SetLastError(0);
        r_unowned = ReleaseMutex(h_mut);
        err_unowned = GetLastError();
        CloseHandle(h_mut);

        buf[0] = (unsigned char)((h_mut != NULL && w_rec == WAIT_OBJECT_0 && r1 && r2) ? 1 : 0);
        buf[1] = (unsigned char)((!r_unowned && err_unowned == ERROR_NOT_OWNER) ? 1 : 0);
        ok(buf[0] == 1 && buf[1] == 1, "Unexpected CreateMutexExW / ReleaseMutex behavior\n");
        parity_record_exact(
            "kb_sync_10_mutex_create_ex_wait_release",
            "CreateMutexExW_RecursiveAndNotOwnerError288",
            "ReleaseMutex",
            (buf[0] == 1 && buf[1] == 1) ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL,
            err_unowned,
            2,
            buf,
            2,
            buf[0] == 1 && buf[1] == 1);
    }

    /* 11. CreateSemaphoreExW validation, WaitForSingleObject, ReleaseSemaphore, ERROR_TOO_MANY_POSTS (298) */
    {
        HANDLE h_bad;
        DWORD err_bad;
        HANDLE h_sem;
        LONG prev = -1;
        BOOL r_ok;
        BOOL r_lim;
        DWORD err_lim;

        SetLastError(0);
        h_bad = CreateSemaphoreExW(NULL, 0, 0, NULL, 0, SEMAPHORE_ALL_ACCESS);
        err_bad = GetLastError();

        h_sem = CreateSemaphoreExW(NULL, 1, 2, NULL, 0, SEMAPHORE_ALL_ACCESS);
        WaitForSingleObject(h_sem, 0);
        r_ok = ReleaseSemaphore(h_sem, 2, &prev);
        SetLastError(0);
        r_lim = ReleaseSemaphore(h_sem, 1, NULL);
        err_lim = GetLastError();
        CloseHandle(h_sem);

        buf[0] = (unsigned char)((h_bad == NULL && err_bad == ERROR_INVALID_PARAMETER) ? 87 : 0);
        buf[1] = (unsigned char)((h_sem != NULL && r_ok && prev == 0 && !r_lim && err_lim == ERROR_TOO_MANY_POSTS) ? 1 : 0);
        ok(buf[0] == 87 && buf[1] == 1, "Unexpected CreateSemaphoreExW / ReleaseSemaphore behavior\n");
        parity_record_exact(
            "kb_sync_11_semaphore_create_ex_wait_release",
            "CreateSemaphoreExW_And_ErrorTooManyPosts298",
            "ReleaseSemaphore",
            (buf[0] == 87 && buf[1] == 1) ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL,
            err_lim,
            2,
            buf,
            2,
            buf[1] == 1);
    }

    /* 12. WaitForMultipleObjectsEx, SignalObjectAndWait, and SleepEx */
    {
        HANDLE handles[2];
        DWORD w_any;
        DWORD w_all;
        DWORD w_sig;
        DWORD slp;

        handles[0] = CreateEventW(NULL, FALSE, FALSE, NULL);
        handles[1] = CreateEventW(NULL, FALSE, TRUE, NULL);

        w_any = WaitForMultipleObjectsEx(2, handles, FALSE, 0, FALSE);
        SetEvent(handles[0]);
        SetEvent(handles[1]);
        w_all = WaitForMultipleObjectsEx(2, handles, TRUE, 0, FALSE);
        w_sig = SignalObjectAndWait(handles[0], handles[0], 0, FALSE);
        slp = SleepEx(0, FALSE);

        CloseHandle(handles[0]);
        CloseHandle(handles[1]);

        buf[0] = (unsigned char)((w_any == (WAIT_OBJECT_0 + 1)) ? 1 : 0);
        buf[1] = (unsigned char)((w_all == WAIT_OBJECT_0 && w_sig == WAIT_OBJECT_0 && slp == 0) ? 1 : 0);
        ok(buf[0] == 1 && buf[1] == 1, "Unexpected WaitForMultipleObjectsEx / SignalObjectAndWait behavior\n");
        parity_record_exact(
            "kb_sync_12_wait_for_multiple_and_signal_and_wait",
            "WaitForMultipleObjectsEx_And_SignalObjectAndWait",
            "WaitForMultipleObjectsEx",
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

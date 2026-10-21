/*
 * Upstream Wine winetest anti-regression suite for kernelbase.dll
 * (WaitOnAddress / WakeByAddressSingle / WakeByAddressAll, SynchronizationBarrier,
 *  InitOnce, Event / Mutex / Semaphore / WaitForMultipleObjectsEx)
 *
 * Adapted from upstream Wine dlls/kernelbase/tests/sync.c
 * Copyright 2018-2024 Wine contributors
 * Copyright 2026 Wine Parity Contributors
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "../../include/wine/test.h"
#include "../../../dlls/kernelbase/kernelbase.h"

#ifndef _WIN32
#include <pthread.h>
#include <sched.h>
#endif

typedef struct _WT_KB_THREAD {
    void (*fn)(void *arg);
    void *arg;
    volatile LONG done;
#ifndef _WIN32
    pthread_t thread;
#else
    HANDLE thread;
#endif
} WT_KB_THREAD;

#ifndef _WIN32
static void *wt_kb_entry(void *param)
{
    WT_KB_THREAD *t = (WT_KB_THREAD *)param;
    t->fn(t->arg);
    __atomic_store_n(&t->done, 1, __ATOMIC_SEQ_CST);
    return NULL;
}
#else
static DWORD WINAPI wt_kb_entry(LPVOID param)
{
    WT_KB_THREAD *t = (WT_KB_THREAD *)param;
    t->fn(t->arg);
    __atomic_store_n(&t->done, 1, __ATOMIC_SEQ_CST);
    return 0;
}
#endif

static void wt_kb_spawn(WT_KB_THREAD *t, void (*fn)(void *), void *arg)
{
    t->fn = fn;
    t->arg = arg;
    t->done = 0;
#ifndef _WIN32
    pthread_create(&t->thread, NULL, wt_kb_entry, t);
#else
    t->thread = CreateThread(NULL, 0, wt_kb_entry, t, 0, NULL);
#endif
}

static void wt_kb_join(WT_KB_THREAD *t)
{
#ifndef _WIN32
    pthread_join(t->thread, NULL);
#else
    while (__atomic_load_n(&t->done, __ATOMIC_SEQ_CST) == 0) {
        SwitchToThread();
    }
#endif
}

static volatile LONG g_woa_shared_addr = 0;
static volatile LONG g_woa_worker_failures = 0;

static void woa_worker_proc(void *param)
{
    LONG val = __atomic_load_n(&g_woa_shared_addr, __ATOMIC_SEQ_CST);
    (void)param;
    while (val > 0) {
        BOOL ret = WaitOnAddress(&g_woa_shared_addr, &val, sizeof(val), 50);
        if (!ret && GetLastError() != ERROR_TIMEOUT) {
            __atomic_add_fetch(&g_woa_worker_failures, 1, __ATOMIC_SEQ_CST);
            break;
        }
        val = __atomic_load_n(&g_woa_shared_addr, __ATOMIC_SEQ_CST);
    }
}

static void test_WaitOnAddress(void)
{
    SIZE_T size;
    BOOL ret;
    LONGLONG address = 0;
    LONGLONG compare = 0;
    WT_KB_THREAD threads[4];
    int i;
    NTDLL_MACOS_NTSYNC_STATE sync_state;

    NtdllResetSyncState();

    SetLastError(0xdeadbeefU);
    WakeByAddressSingle(NULL);
    ok(GetLastError() == 0xdeadbeefU, "got error %u\n", (unsigned int)GetLastError());

    SetLastError(0xdeadbeefU);
    WakeByAddressAll(NULL);
    ok(GetLastError() == 0xdeadbeefU, "got error %u\n", (unsigned int)GetLastError());

    /* Invalid values */
    address = 0;
    compare = 0;
    SetLastError(0xdeadbeefU);
    ret = WaitOnAddress(&address, &compare, 5, 0);
    ok(!ret, "WaitOnAddress succeeded\n");
    ok(GetLastError() == ERROR_INVALID_PARAMETER, "got error %u\n", (unsigned int)GetLastError());

    SetLastError(0xdeadbeefU);
    ret = WaitOnAddress(NULL, NULL, 0, 0);
    ok(!ret, "WaitOnAddress succeeded\n");
    ok(GetLastError() == ERROR_INVALID_PARAMETER, "got error %u\n", (unsigned int)GetLastError());

    /* Values match -> timeout */
    address = 0;
    compare = 0;
    SetLastError(0xdeadbeefU);
    ret = WaitOnAddress(&address, &compare, 8, 5);
    ok(!ret, "WaitOnAddress succeeded\n");
    ok(GetLastError() == ERROR_TIMEOUT, "got error %u\n", (unsigned int)GetLastError());

    /* Different address sizes */
    for (size = 1; size <= 4; size <<= 1) {
        compare = ~0LL;
        compare = (LONGLONG)((ULONGLONG)compare << (size * 8));

        SetLastError(0xdeadbeefU);
        ret = WaitOnAddress(&address, &compare, size, 2);
        ok(!ret, "WaitOnAddress succeeded\n");
        ok(GetLastError() == ERROR_TIMEOUT, "got error %u\n", (unsigned int)GetLastError());

        SetLastError(0xdeadbeefU);
        ret = WaitOnAddress(&address, &compare, size << 1, 2);
        ok(ret, "WaitOnAddress failed\n");
        ok(GetLastError() == 0xdeadbeefU, "got error %u\n", (unsigned int)GetLastError());
    }

    address = 0;
    compare = 1;
    SetLastError(0xdeadbeefU);
    ret = WaitOnAddress(&address, &compare, 8, INFINITE);
    ok(ret, "WaitOnAddress failed\n");
    ok(GetLastError() == 0xdeadbeefU, "got error %u\n", (unsigned int)GetLastError());

    /* No waiters */
    address = 0;
    SetLastError(0xdeadbeefU);
    WakeByAddressSingle(&address);
    ok(GetLastError() == 0xdeadbeefU, "got error %u\n", (unsigned int)GetLastError());
    WakeByAddressAll(&address);
    ok(GetLastError() == 0xdeadbeefU, "got error %u\n", (unsigned int)GetLastError());

    /* Multi-threaded contention */
    g_woa_shared_addr = 200;
    g_woa_worker_failures = 0;
    for (i = 0; i < 4; i++) {
        wt_kb_spawn(&threads[i], woa_worker_proc, NULL);
    }
    while (__atomic_load_n(&g_woa_shared_addr, __ATOMIC_SEQ_CST) > 0) {
        LONG next_val = __atomic_sub_fetch(&g_woa_shared_addr, 1, __ATOMIC_SEQ_CST);
        if ((next_val & 3) == 0) {
            WakeByAddressAll((PVOID)&g_woa_shared_addr);
        } else {
            WakeByAddressSingle((PVOID)&g_woa_shared_addr);
        }
    }
    WakeByAddressAll((PVOID)&g_woa_shared_addr);
    for (i = 0; i < 4; i++) {
        wt_kb_join(&threads[i]);
    }
    ok(g_woa_shared_addr == 0, "expected g_woa_shared_addr == 0\n");
    ok(g_woa_worker_failures == 0, "expected 0 worker WaitOnAddress errors, got %d\n", (int)g_woa_worker_failures);

    todo_wine {
        NTSTATUS st = NtdllQueryMacosNtsyncState(&sync_state);
        ok(st == STATUS_SUCCESS && sync_state.ulock_available == 1 && sync_state.wake_by_address_all_calls >= 1,
           "expected macOS Darwin ulock backend active for WaitOnAddress\n");
    }
}

static void test_SynchronizationBarrier(void)
{
    SYNCHRONIZATION_BARRIER bar;
    BOOL ret;

    SetLastError(0xdeadbeefU);
    ret = InitializeSynchronizationBarrier(&bar, 0, 0);
    ok(!ret && GetLastError() == ERROR_INVALID_PARAMETER, "InitializeSynchronizationBarrier(0) failed\n");

    SetLastError(0xdeadbeefU);
    ret = InitializeSynchronizationBarrier(&bar, -1, 0);
    ok(!ret && GetLastError() == ERROR_INVALID_PARAMETER, "InitializeSynchronizationBarrier(-1) failed\n");

    SetLastError(0xdeadbeefU);
    ret = InitializeSynchronizationBarrier(&bar, 1, -2);
    ok(!ret && GetLastError() == ERROR_INVALID_PARAMETER, "InitializeSynchronizationBarrier(spin=-2) failed\n");

    ret = InitializeSynchronizationBarrier(&bar, 1, -1);
    ok(ret, "InitializeSynchronizationBarrier(1, -1) failed\n");

    ret = EnterSynchronizationBarrier(&bar, 0);
    ok(ret, "single-thread EnterSynchronizationBarrier should return TRUE\n");

    SetLastError(0xdeadbeefU);
    ret = DeleteSynchronizationBarrier(&bar);
    ok(ret && GetLastError() == 0xdeadbeefU, "DeleteSynchronizationBarrier modified LastError\n");
}

static BOOL WINAPI wt_init_once_cb(PINIT_ONCE once, PVOID param, PVOID *context)
{
    ULONG *cnt = (ULONG *)param;
    (void)once;
    (*cnt)++;
    if (context) {
        *context = (PVOID)(uintptr_t)0x12345600U;
    }
    return TRUE;
}

static void test_InitOnce(void)
{
    INIT_ONCE once;
    BOOL pending = FALSE;
    PVOID ctx = NULL;
    ULONG count = 0;
    BOOL ret;

    InitOnceInitialize(&once);
    SetLastError(0xdeadbeefU);
    ret = InitOnceBeginInitialize(&once, INIT_ONCE_CHECK_ONLY, &pending, &ctx);
    ok(!ret && GetLastError() == ERROR_INVALID_PARAMETER, "CHECK_ONLY before init should fail\n");

    ret = InitOnceBeginInitialize(&once, 0, &pending, &ctx);
    ok(ret && pending == TRUE, "InitOnceBeginInitialize failed\n");

    ret = InitOnceComplete(&once, 0, (PVOID)(uintptr_t)0xABCD0000U);
    ok(ret, "InitOnceComplete failed\n");

    pending = TRUE;
    ctx = NULL;
    ret = InitOnceBeginInitialize(&once, INIT_ONCE_CHECK_ONLY, &pending, &ctx);
    ok(ret && pending == FALSE && ctx == (PVOID)(uintptr_t)0xABCD0000U, "CHECK_ONLY after init failed\n");

    InitOnceInitialize(&once);
    ret = InitOnceExecuteOnce(&once, wt_init_once_cb, &count, &ctx);
    ok(ret && count == 1 && ctx == (PVOID)(uintptr_t)0x12345600U, "InitOnceExecuteOnce 1 failed\n");
    ret = InitOnceExecuteOnce(&once, wt_init_once_cb, &count, &ctx);
    ok(ret && count == 1 && ctx == (PVOID)(uintptr_t)0x12345600U, "InitOnceExecuteOnce 2 failed\n");
}

static void test_EventsMutexesSemaphores(void)
{
    HANDLE h_ev;
    HANDLE h_mut;
    HANDLE h_sem;
    LONG prev = -1;
    DWORD w;

    h_ev = CreateEventExW(NULL, NULL, CREATE_EVENT_MANUAL_RESET | CREATE_EVENT_INITIAL_SET, EVENT_ALL_ACCESS);
    ok(h_ev != NULL, "CreateEventExW failed\n");
    w = WaitForSingleObject(h_ev, 0);
    ok(w == WAIT_OBJECT_0, "WaitForSingleObject failed\n");
    ok(ResetEvent(h_ev), "ResetEvent failed\n");
    w = WaitForSingleObject(h_ev, 0);
    ok(w == WAIT_TIMEOUT, "expected WAIT_TIMEOUT\n");
    ok(SetEvent(h_ev), "SetEvent failed\n");
    ok(CloseHandle(h_ev), "CloseHandle failed\n");

    h_mut = CreateMutexExW(NULL, NULL, CREATE_MUTEX_INITIAL_OWNER, MUTANT_ALL_ACCESS);
    ok(h_mut != NULL, "CreateMutexExW failed\n");
    ok(ReleaseMutex(h_mut), "ReleaseMutex failed\n");
    SetLastError(0);
    ok(!ReleaseMutex(h_mut) && GetLastError() == ERROR_NOT_OWNER, "expected ERROR_NOT_OWNER\n");
    ok(CloseHandle(h_mut), "CloseHandle failed\n");

    h_sem = CreateSemaphoreExW(NULL, 1, 2, NULL, 0, SEMAPHORE_ALL_ACCESS);
    ok(h_sem != NULL, "CreateSemaphoreExW failed\n");
    ok(ReleaseSemaphore(h_sem, 1, &prev) && prev == 1, "ReleaseSemaphore failed\n");
    SetLastError(0);
    ok(!ReleaseSemaphore(h_sem, 1, NULL) && GetLastError() == ERROR_TOO_MANY_POSTS, "expected ERROR_TOO_MANY_POSTS\n");
    ok(CloseHandle(h_sem), "CloseHandle failed\n");
}

START_TEST(kernelbase)
{
    WINETEST_RUN_SUBTEST(test_WaitOnAddress);
    WINETEST_RUN_SUBTEST(test_SynchronizationBarrier);
    WINETEST_RUN_SUBTEST(test_InitOnce);
    WINETEST_RUN_SUBTEST(test_EventsMutexesSemaphores);
}

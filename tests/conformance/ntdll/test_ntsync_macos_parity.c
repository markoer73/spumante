/*
 * Differential Conformance Test: NTDLL macOS Fast Synchronization (NTSync Parity)
 *
 * Covers:
 *   - Darwin __ulock_wait2 / __ulock_wake & Mach semaphore backend telemetry
 *   - NtWaitForAlertByThreadId / NtAlertThreadByThreadId / NtAlertMultipleThreadByThreadId
 *   - RtlWaitOnAddress / RtlWakeAddressSingle / RtlWakeAddressAll (1/2/4/8 byte atomic futex)
 *   - NtCreateEvent / NtSetEvent / NtResetEvent / NtPulseEvent / NtQueryEvent
 *   - NtCreateSemaphore / NtReleaseSemaphore / NtQuerySemaphore
 *   - NtCreateMutant / NtReleaseMutant / NtQueryMutant / abandoned mutant semantics
 *   - NtWaitForSingleObject / NtWaitForMultipleObjects (WaitAny & WaitAll) / NtSignalAndWaitForSingleObject
 *   - NtCreateKeyedEvent / NtWaitForKeyedEvent / NtReleaseKeyedEvent
 *   - RtlSRWLock / RtlConditionVariable / RtlRunOnce / RtlBarrier / RtlResource
 *
 * Copyright 2026 Wine Parity Contributors
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "../../include/parity_test.h"
#include "../../../dlls/ntdll/ntdll.h"

#ifndef _WIN32
#include <pthread.h>
#include <sched.h>
#include <unistd.h>
#endif

typedef struct _SYNC_WORKER_CTX {
    void (*fn)(void *arg);
    void *arg;
    volatile LONG started;
    volatile LONG done;
#ifndef _WIN32
    pthread_t thread;
#else
    HANDLE thread;
#endif
} SYNC_WORKER_CTX;

#ifndef _WIN32
static void *sync_worker_entry(void *param)
{
    SYNC_WORKER_CTX *ctx = (SYNC_WORKER_CTX *)param;
    __atomic_store_n(&ctx->started, 1, __ATOMIC_SEQ_CST);
    ctx->fn(ctx->arg);
    __atomic_store_n(&ctx->done, 1, __ATOMIC_SEQ_CST);
    return NULL;
}
#else
static DWORD WINAPI sync_worker_entry(LPVOID param)
{
    SYNC_WORKER_CTX *ctx = (SYNC_WORKER_CTX *)param;
    __atomic_store_n(&ctx->started, 1, __ATOMIC_SEQ_CST);
    ctx->fn(ctx->arg);
    __atomic_store_n(&ctx->done, 1, __ATOMIC_SEQ_CST);
    return 0;
}
#endif

static void sync_spawn_worker(SYNC_WORKER_CTX *ctx, void (*fn)(void *), void *arg)
{
    ctx->fn = fn;
    ctx->arg = arg;
    ctx->started = 0;
    ctx->done = 0;
#ifndef _WIN32
    pthread_create(&ctx->thread, NULL, sync_worker_entry, ctx);
#else
    ctx->thread = CreateThread(NULL, 0, sync_worker_entry, ctx, 0, NULL);
#endif
}

static void sync_join_worker(SYNC_WORKER_CTX *ctx)
{
#ifndef _WIN32
    pthread_join(ctx->thread, NULL);
#else
    while (__atomic_load_n(&ctx->done, __ATOMIC_SEQ_CST) == 0) {
        SwitchToThread();
    }
#endif
}

typedef struct _WOA_TEST_ARGS {
    volatile uint32_t *addr32;
    uint32_t expected32;
    volatile uint64_t *addr64;
    uint64_t expected64;
    volatile LONG ready_count;
    NTSTATUS status;
} WOA_TEST_ARGS;

static void woa_worker_32(void *param)
{
    WOA_TEST_ARGS *args = (WOA_TEST_ARGS *)param;
    LARGE_INTEGER timeout;
    timeout.QuadPart = -20000000LL; /* 2 seconds relative */
    __atomic_add_fetch(&args->ready_count, 1, __ATOMIC_SEQ_CST);
    args->status = RtlWaitOnAddress((const void *)args->addr32, &args->expected32, 4, &timeout);
}

static void woa_worker_64(void *param)
{
    WOA_TEST_ARGS *args = (WOA_TEST_ARGS *)param;
    LARGE_INTEGER timeout;
    timeout.QuadPart = -20000000LL; /* 2 seconds relative */
    __atomic_add_fetch(&args->ready_count, 1, __ATOMIC_SEQ_CST);
    args->status = RtlWaitOnAddress((const void *)args->addr64, &args->expected64, 8, &timeout);
}

typedef struct _KEYED_WORKER_ARGS {
    HANDLE handle;
    const void *key;
    NTSTATUS status;
} KEYED_WORKER_ARGS;

static void keyed_release_worker(void *param)
{
    KEYED_WORKER_ARGS *args = (KEYED_WORKER_ARGS *)param;
    LARGE_INTEGER timeout;
    timeout.QuadPart = -20000000LL;
    args->status = NtReleaseKeyedEvent(args->handle, args->key, FALSE, &timeout);
}

typedef struct _CV_WORKER_ARGS {
    RTL_SRWLOCK *lock;
    RTL_CONDITION_VARIABLE *cv;
    volatile LONG *ready;
    volatile LONG *predicate;
} CV_WORKER_ARGS;

static void cv_wake_worker(void *param)
{
    CV_WORKER_ARGS *args = (CV_WORKER_ARGS *)param;
    while (__atomic_load_n(args->ready, __ATOMIC_SEQ_CST) == 0) {
#ifndef _WIN32
        sched_yield();
#else
        SwitchToThread();
#endif
    }
    RtlAcquireSRWLockExclusive(args->lock);
    __atomic_store_n(args->predicate, 1, __ATOMIC_SEQ_CST);
    RtlWakeAllConditionVariable(args->cv);
    RtlReleaseSRWLockExclusive(args->lock);
}

typedef struct _BARRIER_WORKER_ARGS {
    RTL_BARRIER *barrier;
    BOOLEAN result;
} BARRIER_WORKER_ARGS;

static void barrier_worker(void *param)
{
    BARRIER_WORKER_ARGS *args = (BARRIER_WORKER_ARGS *)param;
    args->result = RtlBarrier(args->barrier, 0);
}

static DWORD WINAPI run_once_test_cb(PRTL_RUN_ONCE once, PVOID param, PVOID *context)
{
    ULONG *counter = (ULONG *)param;
    (void)once;
    (*counter)++;
    if (context) {
        *context = (PVOID)(uintptr_t)0xCAFEBEE0U;
    }
    return TRUE;
}

int main(void)
{
    NTSTATUS status;
    NTDLL_MACOS_NTSYNC_STATE sync_state;
    LARGE_INTEGER zero_timeout;
    LARGE_INTEGER short_timeout;
    unsigned char buf[16];

    zero_timeout.QuadPart = 0;
    short_timeout.QuadPart = -10000LL; /* 1 ms relative */

    NtdllResetSyncState();
    parity_begin_document("ntdll", "test_ntsync_macos_parity", "x86_64", "wine-parity");

    /* 1. Query macOS Darwin ulock & Mach semaphore backend availability */
    memset(&sync_state, 0, sizeof(sync_state));
    status = NtdllQueryMacosNtsyncState(&sync_state);
    buf[0] = (unsigned char)(sync_state.ulock_available ? 1 : 0);
    buf[1] = (unsigned char)(sync_state.mach_semaphore_available ? 1 : 0);
    ok(status == STATUS_SUCCESS && buf[0] == 1 && buf[1] == 1,
       "Expected macOS ulock + Mach semaphore backend active, got status=0x%08x\n", (unsigned int)status);
    parity_record_exact(
        "ntsync_01_query_macos_backend_state",
        "NtdllQueryMacosNtsyncState_DarwinUlockAndMachSem",
        "NtdllQueryMacosNtsyncState",
        status,
        0,
        (ULONG)(buf[0] + buf[1]),
        buf,
        2,
        buf[0] == 1 && buf[1] == 1);

    /* 2. NtAlertThreadByThreadId invalid CID (0 and unknown) and foreign process access denied */
    {
        NTSTATUS st_zero = NtAlertThreadByThreadId((HANDLE)(uintptr_t)0);
        NTSTATUS st_unknown = NtAlertThreadByThreadId((HANDLE)(uintptr_t)0xDEADBEEFU);
        NTSTATUS st_foreign;
        NtdllRegisterForeignThreadId(0xCAFE0001U);
        st_foreign = NtAlertThreadByThreadId((HANDLE)(uintptr_t)0xCAFE0001U);
        buf[0] = (unsigned char)(st_zero & 0xFF);
        buf[1] = (unsigned char)(st_foreign & 0xFF);
        ok(st_zero == STATUS_INVALID_CID && st_unknown == STATUS_INVALID_CID && st_foreign == STATUS_ACCESS_DENIED,
           "Expected STATUS_INVALID_CID and STATUS_ACCESS_DENIED, got 0x%08x / 0x%08x / 0x%08x\n",
           (unsigned int)st_zero, (unsigned int)st_unknown, (unsigned int)st_foreign);
        parity_record_exact(
            "ntsync_02_tid_alert_invalid_cid",
            "NtAlertThreadByThreadId_InvalidCidAndForeignDenied",
            "NtAlertThreadByThreadId",
            st_unknown,
            0,
            (st_foreign == STATUS_ACCESS_DENIED) ? 1U : 0U,
            buf,
            2,
            0);
    }

    /* 3. NtAlertThreadByThreadId + NtWaitForAlertByThreadId auto-reset and timeout */
    {
        DWORD tid = GetCurrentThreadId();
        NTSTATUS st_alert = NtAlertThreadByThreadId((HANDLE)(uintptr_t)tid);
        NTSTATUS st_wait1 = NtWaitForAlertByThreadId((const void *)(uintptr_t)0x123U, &zero_timeout);
        NTSTATUS st_wait2 = NtWaitForAlertByThreadId((const void *)(uintptr_t)0x123U, &zero_timeout);
        buf[0] = (unsigned char)((st_alert == STATUS_SUCCESS && st_wait1 == STATUS_ALERTED) ? 1 : 0);
        buf[1] = (unsigned char)((st_wait2 == STATUS_TIMEOUT) ? 2 : 0);
        ok(st_alert == STATUS_SUCCESS && st_wait1 == STATUS_ALERTED && st_wait2 == STATUS_TIMEOUT,
           "Expected STATUS_ALERTED then STATUS_TIMEOUT, got 0x%08x / 0x%08x / 0x%08x\n",
           (unsigned int)st_alert, (unsigned int)st_wait1, (unsigned int)st_wait2);
        parity_record_exact(
            "ntsync_03_tid_alert_auto_reset_and_timeout",
            "NtWaitForAlertByThreadId_AutoResetAndTimeout",
            "NtWaitForAlertByThreadId",
            st_wait1,
            0,
            2,
            buf,
            2,
            buf[0] == 1 && buf[1] == 2);
    }

    /* 4. NtAlertMultipleThreadByThreadId validation & atomic batch alert */
    {
        DWORD tid = GetCurrentThreadId();
        HANDLE tids_bad[2];
        HANDLE tids_good[2];
        NTSTATUS st_empty = NtAlertMultipleThreadByThreadId(NULL, 0, NULL, NULL);
        NTSTATUS st_null_arr = NtAlertMultipleThreadByThreadId(NULL, 1, NULL, NULL);
        NTSTATUS st_bad;
        NTSTATUS st_unalerted;
        NTSTATUS st_good;
        NTSTATUS st_alerted;

        tids_bad[0] = (HANDLE)(uintptr_t)tid;
        tids_bad[1] = (HANDLE)(uintptr_t)0xDEADBEEFU;
        st_bad = NtAlertMultipleThreadByThreadId(tids_bad, 2, NULL, NULL);
        st_unalerted = NtWaitForAlertByThreadId(NULL, &zero_timeout);

        tids_good[0] = (HANDLE)(uintptr_t)tid;
        tids_good[1] = (HANDLE)(uintptr_t)tid;
        st_good = NtAlertMultipleThreadByThreadId(tids_good, 2, NULL, NULL);
        st_alerted = NtWaitForAlertByThreadId(NULL, &zero_timeout);

        buf[0] = (unsigned char)(st_empty & 0xFF);
        buf[1] = (unsigned char)(st_null_arr & 0xFF);
        buf[2] = (unsigned char)(st_bad & 0xFF);
        buf[3] = (unsigned char)((st_unalerted == STATUS_TIMEOUT && st_good == STATUS_SUCCESS && st_alerted == STATUS_ALERTED) ? 1 : 0);
        ok(st_empty == STATUS_SUCCESS && st_null_arr == STATUS_ACCESS_VIOLATION &&
           st_bad == STATUS_INVALID_CID && buf[3] == 1,
           "Unexpected NtAlertMultipleThreadByThreadId behavior\n");
        parity_record_exact(
            "ntsync_04_tid_alert_multiple_threads_validation",
            "NtAlertMultipleThreadByThreadId_ValidationAndBatch",
            "NtAlertMultipleThreadByThreadId",
            st_good,
            0,
            4,
            buf,
            4,
            buf[3] == 1);
    }

    /* 5. RtlWaitOnAddress invalid AddressSize values (0, 3, 5, 7, 9) */
    {
        uint64_t val = 0;
        SIZE_T bad_sizes[5] = {0, 3, 5, 7, 9};
        int i;
        NTSTATUS last_st = STATUS_SUCCESS;
        for (i = 0; i < 5; i++) {
            last_st = RtlWaitOnAddress(&val, &val, bad_sizes[i], &zero_timeout);
            buf[i] = (unsigned char)(last_st & 0xFF);
        }
        ok(last_st == STATUS_INVALID_PARAMETER, "Expected STATUS_INVALID_PARAMETER for invalid AddressSize\n");
        parity_record_exact(
            "ntsync_05_rtl_wait_on_address_invalid_sizes",
            "RtlWaitOnAddress_InvalidAddressSizes",
            "RtlWaitOnAddress",
            last_st,
            0,
            5,
            buf,
            5,
            0);
    }

    /* 6. RtlWaitOnAddress immediate value mismatch fast-path for 1, 2, 4, 8 bytes */
    {
        uint64_t val = 0x1122334455667788ULL;
        uint64_t cmp = 0x1122334455667700ULL;
        SIZE_T sizes[4] = {1, 2, 4, 8};
        int i;
        NTSTATUS st_all = STATUS_SUCCESS;
        for (i = 0; i < 4; i++) {
            NTSTATUS st = RtlWaitOnAddress(&val, &cmp, sizes[i], NULL);
            if (st != STATUS_SUCCESS) {
                st_all = st;
            }
            buf[i] = (unsigned char)((st == STATUS_SUCCESS) ? sizes[i] : 0);
        }
        ok(st_all == STATUS_SUCCESS, "Expected STATUS_SUCCESS on value mismatch fastpath\n");
        parity_record_exact(
            "ntsync_06_rtl_wait_on_address_mismatch_fastpath",
            "RtlWaitOnAddress_ValueMismatchFastpath_1_2_4_8",
            "RtlWaitOnAddress",
            st_all,
            0,
            4,
            buf,
            4,
            st_all == STATUS_SUCCESS);
    }

    /* 7. RtlWaitOnAddress equal values timeout (relative 0, relative -1ms, expired absolute) */
    {
        uint64_t val = 0xAABBCCDDEEFF0011ULL;
        SIZE_T sizes[4] = {1, 2, 4, 8};
        LARGE_INTEGER abs_past;
        int i;
        NTSTATUS last_st = STATUS_SUCCESS;
        abs_past.QuadPart = 1000LL;
        for (i = 0; i < 4; i++) {
            last_st = RtlWaitOnAddress(&val, &val, sizes[i], &zero_timeout);
            buf[i] = (unsigned char)(last_st & 0xFF);
        }
        last_st = RtlWaitOnAddress(&val, &val, 8, &abs_past);
        buf[4] = (unsigned char)(last_st & 0xFF);
        ok(last_st == STATUS_TIMEOUT && buf[0] == 2 && buf[3] == 2,
           "Expected STATUS_TIMEOUT for equal values\n");
        parity_record_exact(
            "ntsync_07_rtl_wait_on_address_equal_timeout",
            "RtlWaitOnAddress_EqualValuesTimeout",
            "RtlWaitOnAddress",
            last_st,
            0,
            5,
            buf,
            5,
            0);
    }

    /* 8. RtlWaitOnAddress + RtlWakeAddressSingle (32-bit) & RtlWakeAddressAll (64-bit) across threads */
    {
        volatile uint32_t futex32 = 0x11223344U;
        volatile uint64_t futex64 = 0xCAFEBABE11223344ULL;
        WOA_TEST_ARGS args_single;
        WOA_TEST_ARGS args_all1;
        WOA_TEST_ARGS args_all2;
        SYNC_WORKER_CTX w1;
        SYNC_WORKER_CTX w2;
        SYNC_WORKER_CTX w3;
        LARGE_INTEGER nap;

        nap.QuadPart = -20000LL; /* 2ms */
        memset(&args_single, 0, sizeof(args_single));
        args_single.addr32 = &futex32;
        args_single.expected32 = 0x11223344U;
        args_single.status = STATUS_PENDING;
        sync_spawn_worker(&w1, woa_worker_32, &args_single);
        while (__atomic_load_n(&args_single.ready_count, __ATOMIC_SEQ_CST) < 1) {
            NtDelayExecution(FALSE, &nap);
        }
        NtDelayExecution(FALSE, &nap);
        __atomic_store_n(&futex32, 0x55667788U, __ATOMIC_SEQ_CST);
        RtlWakeAddressSingle((const void *)&futex32);
        sync_join_worker(&w1);

        memset(&args_all1, 0, sizeof(args_all1));
        memset(&args_all2, 0, sizeof(args_all2));
        args_all1.addr64 = &futex64;
        args_all1.expected64 = 0xCAFEBABE11223344ULL;
        args_all1.status = STATUS_PENDING;
        args_all2.addr64 = &futex64;
        args_all2.expected64 = 0xCAFEBABE11223344ULL;
        args_all2.status = STATUS_PENDING;
        sync_spawn_worker(&w2, woa_worker_64, &args_all1);
        sync_spawn_worker(&w3, woa_worker_64, &args_all2);
        while (__atomic_load_n(&args_all1.ready_count, __ATOMIC_SEQ_CST) < 1 ||
               __atomic_load_n(&args_all2.ready_count, __ATOMIC_SEQ_CST) < 1) {
            NtDelayExecution(FALSE, &nap);
        }
        NtDelayExecution(FALSE, &nap);
        __atomic_store_n(&futex64, 0xDEADBEEF99887766ULL, __ATOMIC_SEQ_CST);
        RtlWakeAddressAll((const void *)&futex64);
        sync_join_worker(&w2);
        sync_join_worker(&w3);

        buf[0] = (unsigned char)((args_single.status == STATUS_SUCCESS) ? 1 : 0);
        buf[1] = (unsigned char)((args_all1.status == STATUS_SUCCESS) ? 2 : 0);
        buf[2] = (unsigned char)((args_all2.status == STATUS_SUCCESS) ? 3 : 0);
        status = (buf[0] == 1 && buf[1] == 2 && buf[2] == 3) ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
        ok(status == STATUS_SUCCESS, "Expected threaded RtlWakeAddressSingle/All to wake all waiters\n");
        parity_record_exact(
            "ntsync_08_rtl_wait_wake_single_and_all_threaded",
            "RtlWaitOnAddress_WakeSingleAndAll_MultiThreaded",
            "RtlWakeAddressAll",
            status,
            0,
            3,
            buf,
            3,
            status == STATUS_SUCCESS);
    }

    /* 9. NtCreateEvent (NotificationEvent & SynchronizationEvent), Set, Reset, Query, Wait */
    {
        HANDLE h_manual = NULL;
        HANDLE h_auto = NULL;
        EVENT_BASIC_INFORMATION info;
        LONG prev = -1;
        NTSTATUS st1 = NtCreateEvent(&h_manual, EVENT_ALL_ACCESS, NULL, NotificationEvent, FALSE);
        NTSTATUS st2 = NtCreateEvent(&h_auto, EVENT_ALL_ACCESS, NULL, SynchronizationEvent, TRUE);
        NTSTATUS st_set = NtSetEvent(h_manual, &prev);
        NTSTATUS st_w_man = NtWaitForSingleObject(h_manual, FALSE, &zero_timeout);
        NTSTATUS st_w_auto1 = NtWaitForSingleObject(h_auto, FALSE, &zero_timeout);
        NTSTATUS st_w_auto2 = NtWaitForSingleObject(h_auto, FALSE, &zero_timeout);
        memset(&info, 0, sizeof(info));
        status = NtQueryEvent(h_manual, EventBasicInformation, &info, sizeof(info), NULL);
        buf[0] = (unsigned char)((st1 == STATUS_SUCCESS && st2 == STATUS_SUCCESS && st_set == STATUS_SUCCESS && prev == 0) ? 1 : 0);
        buf[1] = (unsigned char)((st_w_man == STATUS_WAIT_0 && info.EventState == 1) ? 1 : 0);
        buf[2] = (unsigned char)((st_w_auto1 == STATUS_WAIT_0) ? 0 : 1);
        buf[3] = (unsigned char)((st_w_auto2 == STATUS_TIMEOUT) ? 1 : 0);
        NtClose(h_manual);
        NtClose(h_auto);
        ok(status == STATUS_SUCCESS && buf[0] == 1 && buf[1] == 1 && buf[2] == 0 && buf[3] == 1,
           "Unexpected NtCreateEvent / NtWaitForSingleObject state transitions\n");
        parity_record_exact(
            "ntsync_09_event_notification_and_synchronization",
            "NtCreateEvent_ManualAndAutoResetSemantics",
            "NtCreateEvent",
            status,
            0,
            4,
            buf,
            4,
            buf[0] == 1 && buf[1] == 1 && buf[3] == 1);
    }

    /* 10. NtCreateSemaphore, NtQuerySemaphore, NtReleaseSemaphore, limit exceeded */
    {
        HANDLE h_sem = NULL;
        SEMAPHORE_BASIC_INFORMATION sinfo;
        LONG prev = -1;
        NTSTATUS st_bad = NtCreateSemaphore(&h_sem, SEMAPHORE_ALL_ACCESS, NULL, 3, 2);
        NTSTATUS st_create = NtCreateSemaphore(&h_sem, SEMAPHORE_ALL_ACCESS, NULL, 1, 2);
        NTSTATUS st_w1 = NtWaitForSingleObject(h_sem, FALSE, &zero_timeout);
        NTSTATUS st_w2 = NtWaitForSingleObject(h_sem, FALSE, &zero_timeout);
        NTSTATUS st_rel = NtReleaseSemaphore(h_sem, 2, &prev);
        NTSTATUS st_lim = NtReleaseSemaphore(h_sem, 1, NULL);
        memset(&sinfo, 0, sizeof(sinfo));
        NtQuerySemaphore(h_sem, SemaphoreBasicInformation, &sinfo, sizeof(sinfo), NULL);
        NtClose(h_sem);
        buf[0] = (unsigned char)((st_bad == STATUS_INVALID_PARAMETER && st_create == STATUS_SUCCESS &&
                                  st_w1 == STATUS_WAIT_0 && st_w2 == STATUS_TIMEOUT && prev == 0 && st_rel == STATUS_SUCCESS) ? 1 : 0);
        buf[1] = (unsigned char)sinfo.CurrentCount;
        ok(st_lim == STATUS_SEMAPHORE_LIMIT_EXCEEDED && buf[0] == 1 && buf[1] == 2,
           "Unexpected semaphore behavior: st_lim=0x%08x\n", (unsigned int)st_lim);
        parity_record_exact(
            "ntsync_10_semaphore_counting_and_limits",
            "NtCreateSemaphore_CountingAndLimitExceeded",
            "NtReleaseSemaphore",
            st_lim,
            0,
            2,
            buf,
            2,
            buf[0] == 1 && buf[1] == 2);
    }

    /* 11. NtCreateMutant recursive acquisition, STATUS_MUTANT_NOT_OWNED, and abandoned state */
    {
        HANDLE h_mut = NULL;
        MUTANT_BASIC_INFORMATION minfo;
        LONG prev = 99;
        NTSTATUS st_create = NtCreateMutant(&h_mut, MUTANT_ALL_ACCESS, NULL, TRUE);
        NTSTATUS st_recurse = NtWaitForSingleObject(h_mut, FALSE, &zero_timeout);
        NTSTATUS st_q1;
        NTSTATUS st_rel1;
        NTSTATUS st_rel2;
        NTSTATUS st_rel_unowned;
        NTSTATUS st_abandon_wait;

        memset(&minfo, 0, sizeof(minfo));
        st_q1 = NtQueryMutant(h_mut, MutantBasicInformation, &minfo, sizeof(minfo), NULL);
        buf[0] = (unsigned char)((st_create == STATUS_SUCCESS && st_recurse == STATUS_WAIT_0 &&
                                  st_q1 == STATUS_SUCCESS && minfo.CurrentCount == -1 && minfo.OwnedByCaller) ? 0xFF : 0x00);
        st_rel1 = NtReleaseMutant(h_mut, &prev);
        st_rel2 = NtReleaseMutant(h_mut, &prev);
        st_rel_unowned = NtReleaseMutant(h_mut, NULL);
        buf[1] = (unsigned char)((st_rel1 == STATUS_SUCCESS && st_rel2 == STATUS_SUCCESS &&
                                  st_rel_unowned == STATUS_MUTANT_NOT_OWNED) ? 0x46 : 0x00);

        /* Re-acquire and simulate owner thread termination abandonment */
        NtWaitForSingleObject(h_mut, FALSE, &zero_timeout);
        NtdllAbandonMutantsForThread(GetCurrentThreadId());
        st_abandon_wait = NtWaitForSingleObject(h_mut, FALSE, &zero_timeout);
        buf[2] = (unsigned char)(st_abandon_wait & 0xFF);
        NtReleaseMutant(h_mut, NULL);
        NtClose(h_mut);

        ok(st_abandon_wait == STATUS_ABANDONED_WAIT_0 && buf[0] == 0xFF && buf[1] == 0x46,
           "Unexpected mutant recursive/abandoned behavior\n");
        parity_record_exact(
            "ntsync_11_mutant_recursive_ownership_and_abandonment",
            "NtCreateMutant_RecursiveAndAbandonedSemantics",
            "NtCreateMutant",
            st_abandon_wait,
            0,
            3,
            buf,
            3,
            buf[0] == 0xFF && buf[1] == 0x46);
    }

    /* 12. NtWaitForMultipleObjects (WaitAny & WaitAll) + NtSignalAndWaitForSingleObject */
    {
        HANDLE objs[2] = {NULL, NULL};
        NTSTATUS st_any;
        NTSTATUS st_all_fail;
        NTSTATUS st_all_ok;
        NTSTATUS st_sig_wait;

        NtCreateEvent(&objs[0], EVENT_ALL_ACCESS, NULL, SynchronizationEvent, FALSE);
        NtCreateEvent(&objs[1], EVENT_ALL_ACCESS, NULL, SynchronizationEvent, TRUE);

        st_any = NtWaitForMultipleObjects(2, objs, WaitAny, FALSE, &zero_timeout);
        NtSetEvent(objs[1], NULL);
        st_all_fail = NtWaitForMultipleObjects(2, objs, WaitAll, FALSE, &zero_timeout);
        NtSetEvent(objs[0], NULL);
        st_all_ok = NtWaitForMultipleObjects(2, objs, WaitAll, FALSE, &zero_timeout);
        st_sig_wait = NtSignalAndWaitForSingleObject(objs[0], objs[0], FALSE, &zero_timeout);

        buf[0] = (unsigned char)(st_any & 0xFF);
        buf[1] = (unsigned char)(st_all_fail & 0xFF);
        buf[2] = (unsigned char)((st_all_ok == STATUS_WAIT_0 && st_sig_wait == STATUS_WAIT_0) ? 0x00 : 0xFF);
        NtClose(objs[0]);
        NtClose(objs[1]);

        ok(st_any == (STATUS_WAIT_0 + 1) && st_all_fail == STATUS_TIMEOUT && st_all_ok == STATUS_WAIT_0 && st_sig_wait == STATUS_WAIT_0,
           "Unexpected NtWaitForMultipleObjects behavior\n");
        parity_record_exact(
            "ntsync_12_wait_for_multiple_objects_any_and_all",
            "NtWaitForMultipleObjects_WaitAnyAndWaitAll",
            "NtWaitForMultipleObjects",
            st_all_ok,
            0,
            3,
            buf,
            3,
            buf[0] == 1 && buf[1] == 2 && buf[2] == 0);
    }

    /* 13. NtCreateKeyedEvent, odd-key rejection, timeout, and threaded handshake */
    {
        HANDLE h_keyed = NULL;
        KEYED_WORKER_ARGS kargs;
        SYNC_WORKER_CTX kw;
        LARGE_INTEGER wait_2s;
        NTSTATUS st_create = NtCreateKeyedEvent(&h_keyed, EVENT_ALL_ACCESS, NULL, 0);
        NTSTATUS st_odd = NtWaitForKeyedEvent(h_keyed, (const void *)(uintptr_t)1U, FALSE, &zero_timeout);
        NTSTATUS st_to = NtWaitForKeyedEvent(h_keyed, (const void *)(uintptr_t)0x12340000U, FALSE, &short_timeout);
        NTSTATUS st_handshake;

        wait_2s.QuadPart = -20000000LL;
        kargs.handle = h_keyed;
        kargs.key = (const void *)(uintptr_t)0x12340000U;
        kargs.status = STATUS_PENDING;
        sync_spawn_worker(&kw, keyed_release_worker, &kargs);
        st_handshake = NtWaitForKeyedEvent(h_keyed, (const void *)(uintptr_t)0x12340000U, FALSE, &wait_2s);
        sync_join_worker(&kw);
        NtClose(h_keyed);

        buf[0] = (unsigned char)(st_odd & 0xFF);
        buf[1] = (unsigned char)(st_to & 0xFF);
        buf[2] = (unsigned char)((st_create == STATUS_SUCCESS && st_handshake == STATUS_SUCCESS && kargs.status == STATUS_SUCCESS) ? 0 : 0xFF);
        ok(st_odd == STATUS_INVALID_PARAMETER_1 && st_to == STATUS_TIMEOUT && buf[2] == 0,
           "Unexpected KeyedEvent behavior\n");
        parity_record_exact(
            "ntsync_13_keyed_event_handshake",
            "NtWaitForKeyedEvent_AlignmentTimeoutAndHandshake",
            "NtWaitForKeyedEvent",
            st_handshake,
            0,
            3,
            buf,
            3,
            buf[0] == 0xef && buf[1] == 0x02 && buf[2] == 0);
    }

    /* 14. RtlSRWLock + RtlConditionVariable (timeout & threaded wake) */
    {
        RTL_SRWLOCK lock;
        RTL_CONDITION_VARIABLE cv;
        volatile LONG ready = 0;
        volatile LONG predicate = 0;
        CV_WORKER_ARGS cv_args;
        SYNC_WORKER_CTX cv_worker;
        LARGE_INTEGER wait_2s;
        BOOLEAN try_excl_while_held;
        BOOLEAN try_sh1;
        BOOLEAN try_sh2;
        NTSTATUS st_cv_to;
        NTSTATUS st_cv_wake;

        wait_2s.QuadPart = -20000000LL;
        RtlInitializeSRWLock(&lock);
        RtlInitializeConditionVariable(&cv);

        RtlAcquireSRWLockExclusive(&lock);
        try_excl_while_held = RtlTryAcquireSRWLockExclusive(&lock);
        st_cv_to = RtlSleepConditionVariableSRW(&cv, &lock, &short_timeout, 0);
        RtlReleaseSRWLockExclusive(&lock);

        RtlAcquireSRWLockShared(&lock);
        try_sh1 = RtlTryAcquireSRWLockShared(&lock);
        try_sh2 = RtlTryAcquireSRWLockExclusive(&lock);
        if (try_sh1) {
            RtlReleaseSRWLockShared(&lock);
        }
        RtlReleaseSRWLockShared(&lock);

        cv_args.lock = &lock;
        cv_args.cv = &cv;
        cv_args.ready = &ready;
        cv_args.predicate = &predicate;
        sync_spawn_worker(&cv_worker, cv_wake_worker, &cv_args);

        RtlAcquireSRWLockExclusive(&lock);
        __atomic_store_n(&ready, 1, __ATOMIC_SEQ_CST);
        st_cv_wake = STATUS_SUCCESS;
        while (__atomic_load_n(&predicate, __ATOMIC_SEQ_CST) == 0 && st_cv_wake == STATUS_SUCCESS) {
            st_cv_wake = RtlSleepConditionVariableSRW(&cv, &lock, &wait_2s, 0);
        }
        RtlReleaseSRWLockExclusive(&lock);
        sync_join_worker(&cv_worker);

        buf[0] = (unsigned char)try_excl_while_held;
        buf[1] = (unsigned char)try_sh1;
        buf[2] = (unsigned char)((st_cv_to == STATUS_TIMEOUT && try_sh2 == 0) ? 1 : 0);
        buf[3] = (unsigned char)((st_cv_wake == STATUS_SUCCESS && predicate == 1) ? 0 : 1);
        ok(buf[0] == 0 && buf[1] == 1 && buf[2] == 1 && buf[3] == 0,
           "Unexpected SRWLock / ConditionVariable behavior\n");
        parity_record_exact(
            "ntsync_14_srwlock_and_condition_variable",
            "RtlSRWLock_And_ConditionVariable_SleepWake",
            "RtlSleepConditionVariableSRW",
            st_cv_wake,
            0,
            4,
            buf,
            4,
            buf[1] == 1 && buf[2] == 1 && buf[3] == 0);
    }

    /* 15. RtlRunOnceInitialize, RtlRunOnceBeginInitialize, RtlRunOnceComplete, RtlRunOnceExecuteOnce */
    {
        RTL_RUN_ONCE once1;
        RTL_RUN_ONCE once2;
        PVOID ctx = NULL;
        ULONG cb_count = 0;
        NTSTATUS st_chk;
        NTSTATUS st_beg;
        NTSTATUS st_cmp;
        NTSTATUS st_after;
        NTSTATUS st_exec1;
        NTSTATUS st_exec2;

        RtlRunOnceInitialize(&once1);
        st_chk = RtlRunOnceBeginInitialize(&once1, RTL_RUN_ONCE_CHECK_ONLY, &ctx);
        st_beg = RtlRunOnceBeginInitialize(&once1, 0, &ctx);
        st_cmp = RtlRunOnceComplete(&once1, 0, (PVOID)(uintptr_t)0x12345670U);
        st_after = RtlRunOnceBeginInitialize(&once1, RTL_RUN_ONCE_CHECK_ONLY, &ctx);

        RtlRunOnceInitialize(&once2);
        st_exec1 = RtlRunOnceExecuteOnce(&once2, run_once_test_cb, &cb_count, &ctx);
        st_exec2 = RtlRunOnceExecuteOnce(&once2, run_once_test_cb, &cb_count, &ctx);

        buf[0] = (unsigned char)(st_chk & 0xFF);
        buf[1] = (unsigned char)(st_beg & 0xFF);
        buf[2] = (unsigned char)((st_cmp == STATUS_SUCCESS && st_after == STATUS_SUCCESS &&
                                  st_exec1 == STATUS_SUCCESS && st_exec2 == STATUS_SUCCESS &&
                                  cb_count == 1 && ctx == (PVOID)(uintptr_t)0xCAFEBEE0U) ? 0 : 0xFF);
        ok(st_chk == STATUS_UNSUCCESSFUL && st_beg == STATUS_PENDING && buf[2] == 0,
           "Unexpected RtlRunOnce behavior\n");
        parity_record_exact(
            "ntsync_15_run_once_sync_and_async",
            "RtlRunOnce_BeginCompleteAndExecuteOnce",
            "RtlRunOnceExecuteOnce",
            st_exec2,
            0,
            3,
            buf,
            3,
            buf[0] == 1 && buf[1] == 3 && buf[2] == 0);
    }

    /* 16. RtlBarrier (2 threads, exactly 1 leader returns TRUE) + RtlResource */
    {
        RTL_BARRIER barrier;
        BARRIER_WORKER_ARGS bargs;
        SYNC_WORKER_CTX bw;
        RTL_RWLOCK rwlock;
        NTSTATUS st_bad_bar = RtlInitBarrier(&barrier, 0, 0);
        NTSTATUS st_init_bar = RtlInitBarrier(&barrier, 2, 100);
        BOOLEAN r_main;
        BYTE acq_ex;
        BYTE acq_sh;

        bargs.barrier = &barrier;
        bargs.result = 0;
        sync_spawn_worker(&bw, barrier_worker, &bargs);
        r_main = RtlBarrier(&barrier, 0);
        sync_join_worker(&bw);
        RtlDeleteBarrier(&barrier);

        RtlInitializeResource(&rwlock);
        acq_ex = RtlAcquireResourceExclusive(&rwlock, TRUE);
        RtlReleaseResource(&rwlock);
        acq_sh = RtlAcquireResourceShared(&rwlock, TRUE);
        RtlReleaseResource(&rwlock);
        RtlDeleteResource(&rwlock);

        buf[0] = (unsigned char)((st_bad_bar == STATUS_INVALID_PARAMETER && st_init_bar == STATUS_SUCCESS &&
                                  ((r_main ? 1 : 0) + (bargs.result ? 1 : 0)) == 1) ? 1 : 0);
        buf[1] = (unsigned char)((acq_ex && acq_sh) ? 1 : 0);
        ok(st_init_bar == STATUS_SUCCESS && buf[0] == 1 && buf[1] == 1,
           "Unexpected RtlBarrier / RtlResource behavior\n");
        parity_record_exact(
            "ntsync_16_barrier_and_rtl_resource",
            "RtlBarrier_And_RtlResource_Parity",
            "RtlInitBarrier",
            st_init_bar,
            0,
            2,
            buf,
            2,
            buf[0] == 1 && buf[1] == 1);
    }

    parity_end_document();
    return (g_winetest_failures == 0) ? 0 : 1;
}

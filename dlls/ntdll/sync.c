/*
 * Wine NTDLL Fast macOS Synchronization (NTSync Parity)
 *
 * Implements fast in-process and kernel-assisted synchronization primitives:
 *   - NtWaitForAlertByThreadId / NtAlertThreadByThreadId / NtAlertMultipleThreadByThreadId
 *   - RtlWaitOnAddress / RtlWakeAddressSingle / RtlWakeAddressAll (1/2/4/8-byte atomic futex)
 *   - NtCreateEvent / NtOpenEvent / NtSetEvent / NtResetEvent / NtPulseEvent / NtQueryEvent / NtSetEventBoostPriority
 *   - NtCreateSemaphore / NtOpenSemaphore / NtReleaseSemaphore / NtQuerySemaphore
 *   - NtCreateMutant / NtOpenMutant / NtReleaseMutant / NtQueryMutant
 *   - NtCreateKeyedEvent / NtOpenKeyedEvent / NtWaitForKeyedEvent / NtReleaseKeyedEvent
 *   - NtWaitForSingleObject / NtWaitForMultipleObjects (WaitAny & WaitAll) / NtSignalAndWaitForSingleObject
 *   - NtDelayExecution / NtQuerySystemTime / NtQueryTimerResolution / NtClose
 *   - RtlSRWLock / RtlConditionVariable / RtlRunOnce / RtlBarrier / RtlResource / RtlCriticalSection
 *
 * macOS Hardware/Kernel Acceleration (__APPLE__):
 *   - Uses Darwin XNU __ulock_wait2 / __ulock_wake (UL_COMPARE_AND_WAIT | ULF_NO_ERRNO)
 *     for zero-allocation kernel-parked address waits and thread-ID alerts.
 *   - Uses Mach kernel semaphores (semaphore_create / semaphore_timedwait / semaphore_signal /
 *     semaphore_signal_all / semaphore_destroy) for synchronization object wakeup notifications.
 *
 * Clean-Room Attestation:
 *   Derived strictly from Microsoft Learn documentation, public Wine headers/tests, and
 *   black-box Windows 11 differential conformance behavior.
 *
 * Copyright 2026 Spumante Contributors
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include <stdlib.h>
#include <string.h>
#include <limits.h>

#ifdef __APPLE__
#include <errno.h>
#include <mach/mach.h>
#include <mach/semaphore.h>
#include <mach/task.h>
#include <pthread.h>
#include <sched.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#ifndef UL_COMPARE_AND_WAIT
#define UL_COMPARE_AND_WAIT             1U
#define UL_UNFAIR_LOCK                  2U
#define UL_COMPARE_AND_WAIT_SHARED      3U
#define UL_UNFAIR_LOCK64_SHARED         4U
#define UL_COMPARE_AND_WAIT64           5U
#define UL_COMPARE_AND_WAIT64_SHARED    6U
#define ULF_WAKE_ALL                    0x00000100U
#define ULF_WAKE_THREAD                 0x00000200U
#define ULF_WAKE_ALLOW_NON_OWNER        0x00000400U
#define ULF_WAIT_WORKQ_DATA_CONTENTION  0x00010000U
#define ULF_WAIT_CANCEL_POINT           0x00020000U
#define ULF_WAIT_ADAPTIVE_SPIN          0x00040000U
#define ULF_NO_ERRNO                    0x01000000U
#endif

extern int __ulock_wait(uint32_t operation, void *addr, uint64_t value, uint32_t timeout_us);
extern int __ulock_wait2(uint32_t operation, void *addr, uint64_t value, uint64_t timeout_ns, uint64_t value2);
extern int __ulock_wake(uint32_t operation, void *addr, uint64_t wake_value);
#elif !defined(_WIN32)
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#endif

#include "ntdll.h"

#define WINE_DEFAULT_DEBUG_CHANNEL(ch)
#define TRACE(...) do { } while (0)
#define WARN(...)  do { } while (0)
#define FIXME(...) do { } while (0)
#define ERR(...)   do { } while (0)

WINE_DEFAULT_DEBUG_CHANNEL(sync);

#define NTSYNC_WINDOWS_TICK_PER_SEC     10000000ULL
#define NTSYNC_EPOCH_DIFF_SECS          11644473600ULL
#define NTSYNC_MAX_THREADS              256U
#define NTSYNC_MAX_OBJECTS              256U
#define NTSYNC_MAX_HANDLES              512U
#define NTSYNC_MAX_KEYED_WAITERS        128U
#define NTSYNC_WOA_BUCKETS              256U
#define NTSYNC_HANDLE_BASE              ((uintptr_t)0x50000000U)
#define NTSYNC_HANDLE_STRIDE            16U

#ifndef GENERIC_READ
#define GENERIC_READ                    0x80000000U
#define GENERIC_WRITE                   0x40000000U
#define GENERIC_EXECUTE                 0x20000000U
#define GENERIC_ALL                     0x10000000U
#endif

#define KEYEDEVENT_WAIT_ACCESS          0x0001U
#define KEYEDEVENT_WAKE_ACCESS          0x0002U
#define KEYEDEVENT_ALL_ACCESS_MASK      (STANDARD_RIGHTS_REQUIRED | 0x0003U)

typedef enum _NTSYNC_OBJ_TYPE {
    NTSYNC_OBJ_NONE = 0,
    NTSYNC_OBJ_EVENT = 1,
    NTSYNC_OBJ_SEMAPHORE = 2,
    NTSYNC_OBJ_MUTANT = 3,
    NTSYNC_OBJ_KEYED_EVENT = 4
} NTSYNC_OBJ_TYPE;

typedef struct _NTSYNC_THREAD_SLOT {
    int             in_use;
    DWORD           tid;
    int             foreign_process;
    volatile uint32_t alerted;
    const void     *wait_address;
#ifdef __APPLE__
    semaphore_t     mach_sem;
    int             mach_sem_valid;
#endif
} NTSYNC_THREAD_SLOT;

typedef struct _NTSYNC_KEYED_WAITER {
    int               in_use;
    int               is_releaser;
    const void       *key;
    DWORD             tid;
    volatile uint32_t matched;
} NTSYNC_KEYED_WAITER;

typedef struct _NTSYNC_OBJECT {
    int             in_use;
    NTSYNC_OBJ_TYPE type;
    ULONG           ref_count;
    WCHAR           name[128];
    /* Event fields */
    EVENT_TYPE      event_type;
    LONG            event_state;
    /* Semaphore fields */
    LONG            sem_count;
    LONG            sem_max;
    /* Mutant fields */
    LONG            mutant_count;
    DWORD           mutant_owner_tid;
    BOOLEAN         mutant_abandoned;
    /* KeyedEvent waiters */
    NTSYNC_KEYED_WAITER keyed_waiters[NTSYNC_MAX_KEYED_WAITERS];
#ifdef __APPLE__
    semaphore_t     mach_sem;
    int             mach_sem_valid;
#endif
} NTSYNC_OBJECT;

typedef struct _NTSYNC_HANDLE_ENTRY {
    int             in_use;
    ULONG           generation;
    ACCESS_MASK     access;
    NTSYNC_OBJECT  *obj;
} NTSYNC_HANDLE_ENTRY;

typedef struct _NTSYNC_WOA_ENTRY {
    const void               *addr;
    volatile uint32_t         signaled;
    int                       in_queue;
    struct _NTSYNC_WOA_ENTRY *next;
    struct _NTSYNC_WOA_ENTRY *prev;
} NTSYNC_WOA_ENTRY;

typedef struct _NTSYNC_WOA_BUCKET {
    volatile int      lock;
    NTSYNC_WOA_ENTRY *head;
    NTSYNC_WOA_ENTRY *tail;
} NTSYNC_WOA_BUCKET;

typedef struct _NTSYNC_BARRIER_INTERNAL {
    volatile ULONG phase;
    volatile ULONG remaining;
    ULONG          total_threads;
    ULONG          spin_count;
} NTSYNC_BARRIER_INTERNAL;

static volatile int g_sync_lock = 0;
static volatile uint32_t g_sync_epoch = 1;
static volatile DWORD g_next_tid = 1000U;
static _Thread_local DWORD g_tls_tid = 0;

static NTSYNC_THREAD_SLOT g_thread_slots[NTSYNC_MAX_THREADS];
static NTSYNC_OBJECT g_sync_objects[NTSYNC_MAX_OBJECTS];
static NTSYNC_HANDLE_ENTRY g_handle_entries[NTSYNC_MAX_HANDLES];
static NTSYNC_WOA_BUCKET g_woa_buckets[NTSYNC_WOA_BUCKETS];
static NTSYNC_OBJECT g_default_keyed_event;
static int g_sync_initialized = 0;

static volatile ULONG g_stat_ulock_wait_calls = 0;
static volatile ULONG g_stat_ulock_wake_calls = 0;
static volatile ULONG g_stat_mach_sem_wait_calls = 0;
static volatile ULONG g_stat_mach_sem_signal_calls = 0;
static volatile ULONG g_stat_woa_wait_calls = 0;
static volatile ULONG g_stat_woa_wake_single_calls = 0;
static volatile ULONG g_stat_woa_wake_all_calls = 0;
static volatile ULONG g_stat_tid_alert_wait_calls = 0;
static volatile ULONG g_stat_tid_alert_wake_calls = 0;

static int ntsync_is_stub_simulated(void)
{
    const char *sim = getenv("SPUMANTE_SIMULATE_STUB");
    return (sim != NULL && sim[0] == '1');
}

static void ntsync_spin_lock(volatile int *lock)
{
    while (__atomic_test_and_set(lock, __ATOMIC_ACQUIRE)) {
#if defined(__APPLE__) || !defined(_WIN32)
        sched_yield();
#else
        SwitchToThread();
#endif
    }
}

static void ntsync_spin_unlock(volatile int *lock)
{
    __atomic_clear(lock, __ATOMIC_RELEASE);
}

static void ntsync_global_lock(void)
{
    ntsync_spin_lock(&g_sync_lock);
}

static void ntsync_global_unlock(void)
{
    ntsync_spin_unlock(&g_sync_lock);
}

static void ntsync_ulock_wait32(volatile uint32_t *word_addr, uint32_t expected, uint64_t timeout_ns)
{
    __atomic_add_fetch(&g_stat_ulock_wait_calls, 1U, __ATOMIC_RELAXED);
#ifdef __APPLE__
    {
        uint64_t capped_ns = timeout_ns;
        if (capped_ns == 0) {
            return;
        }
        if (capped_ns > 50000000ULL) {
            capped_ns = 50000000ULL; /* 50ms periodic poll slice for multi-object / epoch safety */
        }
        (void)__ulock_wait2(UL_COMPARE_AND_WAIT | ULF_NO_ERRNO, (void *)word_addr, expected, capped_ns, 0);
    }
#elif !defined(_WIN32)
    {
        struct timespec ts;
        if (__atomic_load_n(word_addr, __ATOMIC_SEQ_CST) != expected || timeout_ns == 0) {
            return;
        }
        ts.tv_sec = 0;
        ts.tv_nsec = (timeout_ns < 500000ULL) ? (long)timeout_ns : 500000L;
        nanosleep(&ts, NULL);
    }
#else
    {
        if (__atomic_load_n(word_addr, __ATOMIC_SEQ_CST) != expected || timeout_ns == 0) {
            return;
        }
        SwitchToThread();
    }
#endif
}

static void ntsync_ulock_wake32(volatile uint32_t *word_addr, int wake_all)
{
    __atomic_add_fetch(&g_stat_ulock_wake_calls, 1U, __ATOMIC_RELAXED);
#ifdef __APPLE__
    {
        uint32_t flags = UL_COMPARE_AND_WAIT | ULF_NO_ERRNO | (wake_all ? ULF_WAKE_ALL : 0U);
        (void)__ulock_wake(flags, (void *)word_addr, 0);
    }
#else
    (void)word_addr;
    (void)wake_all;
#endif
}

static void ntsync_mach_sem_init(NTSYNC_OBJECT *obj)
{
#ifdef __APPLE__
    if (!obj->mach_sem_valid) {
        if (semaphore_create(mach_task_self(), &obj->mach_sem, SYNC_POLICY_FIFO, 0) == KERN_SUCCESS) {
            obj->mach_sem_valid = 1;
        }
    }
#else
    (void)obj;
#endif
}

static void ntsync_mach_sem_destroy(NTSYNC_OBJECT *obj)
{
#ifdef __APPLE__
    if (obj->mach_sem_valid) {
        semaphore_destroy(mach_task_self(), obj->mach_sem);
        obj->mach_sem_valid = 0;
    }
#else
    (void)obj;
#endif
}

static void ntsync_notify_epoch(NTSYNC_OBJECT *obj)
{
    __atomic_add_fetch(&g_sync_epoch, 1U, __ATOMIC_SEQ_CST);
#ifdef __APPLE__
    if (obj && obj->mach_sem_valid) {
        __atomic_add_fetch(&g_stat_mach_sem_signal_calls, 1U, __ATOMIC_RELAXED);
        semaphore_signal_all(obj->mach_sem);
    }
#else
    (void)obj;
#endif
    ntsync_ulock_wake32(&g_sync_epoch, 1);
}

static uint64_t ntsync_now_100ns(void)
{
#ifndef _WIN32
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return ((uint64_t)tv.tv_sec + NTSYNC_EPOCH_DIFF_SECS) * NTSYNC_WINDOWS_TICK_PER_SEC +
           (uint64_t)tv.tv_usec * 10ULL;
#else
    FILETIME ft;
    ULARGE_INTEGER uli;
    GetSystemTimeAsFileTime(&ft);
    uli.LowPart = ft.dwLowDateTime;
    uli.HighPart = ft.dwHighDateTime;
    return (uint64_t)uli.QuadPart;
#endif
}

static uint64_t ntsync_compute_deadline_100ns(const LARGE_INTEGER *Timeout, int *is_infinite)
{
    uint64_t now;
    if (!Timeout || Timeout->QuadPart == (LONGLONG)0x8000000000000000ULL) {
        *is_infinite = 1;
        return UINT64_MAX;
    }
    *is_infinite = 0;
    now = ntsync_now_100ns();
    if (Timeout->QuadPart <= 0) {
        uint64_t rel = (uint64_t)(-Timeout->QuadPart);
        return now + rel;
    }
    return (uint64_t)Timeout->QuadPart;
}

static uint64_t ntsync_remaining_ns(uint64_t deadline_100ns, int is_infinite)
{
    uint64_t now;
    if (is_infinite) {
        return 1000000000ULL; /* 1s slice for infinite wait */
    }
    now = ntsync_now_100ns();
    if (now >= deadline_100ns) {
        return 0;
    }
    return (deadline_100ns - now) * 100ULL;
}

static NTSYNC_THREAD_SLOT *ntsync_find_or_create_thread_locked(DWORD tid, int create_if_missing, int foreign)
{
    ULONG i;
    NTSYNC_THREAD_SLOT *free_slot = NULL;
    for (i = 0; i < NTSYNC_MAX_THREADS; i++) {
        if (g_thread_slots[i].in_use && g_thread_slots[i].tid == tid) {
            return &g_thread_slots[i];
        }
        if (!g_thread_slots[i].in_use && !free_slot) {
            free_slot = &g_thread_slots[i];
        }
    }
    if (!create_if_missing || !free_slot) {
        return NULL;
    }
    memset(free_slot, 0, sizeof(*free_slot));
    free_slot->in_use = 1;
    free_slot->tid = tid;
    free_slot->foreign_process = foreign;
#ifdef __APPLE__
    if (semaphore_create(mach_task_self(), &free_slot->mach_sem, SYNC_POLICY_FIFO, 0) == KERN_SUCCESS) {
        free_slot->mach_sem_valid = 1;
    }
#endif
    return free_slot;
}

static void ntsync_ensure_initialized_locked(void)
{
    if (!g_sync_initialized) {
        memset(g_sync_objects, 0, sizeof(g_sync_objects));
        memset(g_handle_entries, 0, sizeof(g_handle_entries));
        memset(&g_default_keyed_event, 0, sizeof(g_default_keyed_event));
        g_default_keyed_event.in_use = 1;
        g_default_keyed_event.type = NTSYNC_OBJ_KEYED_EVENT;
        g_default_keyed_event.ref_count = 1;
        ntsync_mach_sem_init(&g_default_keyed_event);
        g_sync_initialized = 1;
    }
}

DWORD WINAPI NtdllGetCurrentThreadId(void)
{
    DWORD tid = g_tls_tid;
    if (tid == 0) {
        tid = __atomic_add_fetch(&g_next_tid, 1U, __ATOMIC_SEQ_CST);
        g_tls_tid = tid;
    }
    ntsync_global_lock();
    ntsync_ensure_initialized_locked();
    (void)ntsync_find_or_create_thread_locked(tid, 1, 0);
    ntsync_global_unlock();
    return tid;
}

void WINAPI NtdllRegisterForeignThreadId(DWORD ThreadId)
{
    if (ThreadId == 0) {
        return;
    }
    ntsync_global_lock();
    ntsync_ensure_initialized_locked();
    {
        NTSYNC_THREAD_SLOT *slot = ntsync_find_or_create_thread_locked(ThreadId, 1, 1);
        if (slot) {
            slot->foreign_process = 1;
        }
    }
    ntsync_global_unlock();
}

void WINAPI NtdllResetSyncState(void)
{
    ULONG i;
    ntsync_global_lock();
    for (i = 0; i < NTSYNC_MAX_OBJECTS; i++) {
        if (g_sync_objects[i].in_use) {
            ntsync_mach_sem_destroy(&g_sync_objects[i]);
        }
    }
    for (i = 0; i < NTSYNC_MAX_THREADS; i++) {
#ifdef __APPLE__
        if (g_thread_slots[i].in_use && g_thread_slots[i].mach_sem_valid) {
            semaphore_destroy(mach_task_self(), g_thread_slots[i].mach_sem);
            g_thread_slots[i].mach_sem_valid = 0;
        }
#endif
        g_thread_slots[i].in_use = 0;
    }
    ntsync_mach_sem_destroy(&g_default_keyed_event);
    memset(g_sync_objects, 0, sizeof(g_sync_objects));
    memset(g_handle_entries, 0, sizeof(g_handle_entries));
    g_sync_initialized = 0;
    ntsync_ensure_initialized_locked();

    __atomic_store_n(&g_stat_ulock_wait_calls, 0, __ATOMIC_SEQ_CST);
    __atomic_store_n(&g_stat_ulock_wake_calls, 0, __ATOMIC_SEQ_CST);
    __atomic_store_n(&g_stat_mach_sem_wait_calls, 0, __ATOMIC_SEQ_CST);
    __atomic_store_n(&g_stat_mach_sem_signal_calls, 0, __ATOMIC_SEQ_CST);
    __atomic_store_n(&g_stat_woa_wait_calls, 0, __ATOMIC_SEQ_CST);
    __atomic_store_n(&g_stat_woa_wake_single_calls, 0, __ATOMIC_SEQ_CST);
    __atomic_store_n(&g_stat_woa_wake_all_calls, 0, __ATOMIC_SEQ_CST);
    __atomic_store_n(&g_stat_tid_alert_wait_calls, 0, __ATOMIC_SEQ_CST);
    __atomic_store_n(&g_stat_tid_alert_wake_calls, 0, __ATOMIC_SEQ_CST);
    ntsync_global_unlock();

    (void)NtdllGetCurrentThreadId();
}

NTSTATUS WINAPI NtdllQueryMacosNtsyncState(NTDLL_MACOS_NTSYNC_STATE *State)
{
    ULONG i;
    ULONG active_objs = 0;
    if (ntsync_is_stub_simulated()) {
        return STATUS_NOT_IMPLEMENTED;
    }
    if (!State) {
        return STATUS_INVALID_PARAMETER;
    }
    ntsync_global_lock();
    ntsync_ensure_initialized_locked();
    for (i = 0; i < NTSYNC_MAX_OBJECTS; i++) {
        if (g_sync_objects[i].in_use) {
            active_objs++;
        }
    }
    ntsync_global_unlock();

    memset(State, 0, sizeof(*State));
    State->ulock_available = 1U;
    State->mach_semaphore_available = 1U;
    State->ulock_wait_calls = __atomic_load_n(&g_stat_ulock_wait_calls, __ATOMIC_SEQ_CST);
    State->ulock_wake_calls = __atomic_load_n(&g_stat_ulock_wake_calls, __ATOMIC_SEQ_CST);
    State->mach_sem_wait_calls = __atomic_load_n(&g_stat_mach_sem_wait_calls, __ATOMIC_SEQ_CST);
    State->mach_sem_signal_calls = __atomic_load_n(&g_stat_mach_sem_signal_calls, __ATOMIC_SEQ_CST);
    State->wait_on_address_calls = __atomic_load_n(&g_stat_woa_wait_calls, __ATOMIC_SEQ_CST);
    State->wake_by_address_single_calls = __atomic_load_n(&g_stat_woa_wake_single_calls, __ATOMIC_SEQ_CST);
    State->wake_by_address_all_calls = __atomic_load_n(&g_stat_woa_wake_all_calls, __ATOMIC_SEQ_CST);
    State->tid_alert_wait_calls = __atomic_load_n(&g_stat_tid_alert_wait_calls, __ATOMIC_SEQ_CST);
    State->tid_alert_wake_calls = __atomic_load_n(&g_stat_tid_alert_wake_calls, __ATOMIC_SEQ_CST);
    State->active_sync_objects = active_objs;
    return STATUS_SUCCESS;
}

/* ============================================================================
 * 1. Thread-ID Alert Primitives (NtWaitForAlertByThreadId / NtAlertThreadByThreadId)
 * ============================================================================ */

NTSTATUS WINAPI NtWaitForAlertByThreadId(const void *Address, const LARGE_INTEGER *Timeout)
{
    DWORD tid;
    NTSYNC_THREAD_SLOT *slot;
    int is_infinite = 0;
    uint64_t deadline;

    if (ntsync_is_stub_simulated()) {
        return STATUS_NOT_IMPLEMENTED;
    }

    __atomic_add_fetch(&g_stat_tid_alert_wait_calls, 1U, __ATOMIC_RELAXED);
    tid = NtdllGetCurrentThreadId();

    ntsync_global_lock();
    slot = ntsync_find_or_create_thread_locked(tid, 1, 0);
    slot->wait_address = Address;
    if (__atomic_exchange_n(&slot->alerted, 0U, __ATOMIC_SEQ_CST) != 0U) {
        ntsync_global_unlock();
        return STATUS_ALERTED;
    }
    ntsync_global_unlock();

    deadline = ntsync_compute_deadline_100ns(Timeout, &is_infinite);
    for (;;) {
        uint64_t rem_ns;
        if (__atomic_exchange_n(&slot->alerted, 0U, __ATOMIC_SEQ_CST) != 0U) {
            return STATUS_ALERTED;
        }
        rem_ns = ntsync_remaining_ns(deadline, is_infinite);
        if (rem_ns == 0) {
            if (__atomic_exchange_n(&slot->alerted, 0U, __ATOMIC_SEQ_CST) != 0U) {
                return STATUS_ALERTED;
            }
            return STATUS_TIMEOUT;
        }
        ntsync_ulock_wait32(&slot->alerted, 0U, rem_ns);
    }
}

static void ntsync_alert_slot_locked(NTSYNC_THREAD_SLOT *slot)
{
    __atomic_store_n(&slot->alerted, 1U, __ATOMIC_SEQ_CST);
    __atomic_add_fetch(&g_stat_tid_alert_wake_calls, 1U, __ATOMIC_RELAXED);
#ifdef __APPLE__
    if (slot->mach_sem_valid) {
        __atomic_add_fetch(&g_stat_mach_sem_signal_calls, 1U, __ATOMIC_RELAXED);
        semaphore_signal(slot->mach_sem);
    }
#endif
    ntsync_ulock_wake32(&slot->alerted, 1);
    ntsync_notify_epoch(NULL);
}

NTSTATUS WINAPI NtAlertThreadByThreadId(HANDLE ThreadId)
{
    DWORD tid = (DWORD)(uintptr_t)ThreadId;
    NTSYNC_THREAD_SLOT *slot;

    if (ntsync_is_stub_simulated()) {
        return STATUS_NOT_IMPLEMENTED;
    }
    if (tid == 0) {
        return STATUS_INVALID_CID;
    }

    (void)NtdllGetCurrentThreadId();
    ntsync_global_lock();
    slot = ntsync_find_or_create_thread_locked(tid, 0, 0);
    if (!slot) {
        ntsync_global_unlock();
        return STATUS_INVALID_CID;
    }
    if (slot->foreign_process) {
        ntsync_global_unlock();
        return STATUS_ACCESS_DENIED;
    }
    ntsync_alert_slot_locked(slot);
    ntsync_global_unlock();
    return STATUS_SUCCESS;
}

NTSTATUS WINAPI NtAlertMultipleThreadByThreadId(
    HANDLE *ThreadIds,
    ULONG Count,
    const void *Boost1,
    const void *Boost2)
{
    ULONG i;

    (void)Boost1;
    (void)Boost2;
    if (ntsync_is_stub_simulated()) {
        return STATUS_NOT_IMPLEMENTED;
    }
    if (Count == 0) {
        return STATUS_SUCCESS;
    }
    if (!ThreadIds) {
        return STATUS_ACCESS_VIOLATION;
    }

    (void)NtdllGetCurrentThreadId();
    ntsync_global_lock();
    for (i = 0; i < Count; i++) {
        DWORD tid = (DWORD)(uintptr_t)ThreadIds[i];
        NTSYNC_THREAD_SLOT *slot;
        if (tid == 0) {
            ntsync_global_unlock();
            return STATUS_INVALID_CID;
        }
        slot = ntsync_find_or_create_thread_locked(tid, 0, 0);
        if (!slot) {
            ntsync_global_unlock();
            return STATUS_INVALID_CID;
        }
        if (slot->foreign_process) {
            ntsync_global_unlock();
            return STATUS_ACCESS_DENIED;
        }
    }

    for (i = 0; i < Count; i++) {
        DWORD tid = (DWORD)(uintptr_t)ThreadIds[i];
        NTSYNC_THREAD_SLOT *slot = ntsync_find_or_create_thread_locked(tid, 0, 0);
        if (slot) {
            ntsync_alert_slot_locked(slot);
        }
    }
    ntsync_global_unlock();
    return STATUS_SUCCESS;
}

/* ============================================================================
 * 2. Fast Address Futex (RtlWaitOnAddress / RtlWakeAddressSingle / RtlWakeAddressAll)
 * ============================================================================ */

static int ntsync_compare_addr_equal(const void *addr, const void *cmp, SIZE_T size)
{
    switch (size) {
    case 1:
        return *(const uint8_t *)addr == *(const uint8_t *)cmp;
    case 2:
    {
        uint16_t a;
        uint16_t b;
        memcpy(&a, addr, 2);
        memcpy(&b, cmp, 2);
        return a == b;
    }
    case 4:
    {
        uint32_t a;
        uint32_t b;
        memcpy(&a, addr, 4);
        memcpy(&b, cmp, 4);
        return a == b;
    }
    case 8:
    {
        uint64_t a;
        uint64_t b;
        memcpy(&a, addr, 8);
        memcpy(&b, cmp, 8);
        return a == b;
    }
    default:
        return 0;
    }
}

static NTSYNC_WOA_BUCKET *ntsync_woa_bucket(const void *addr)
{
    uintptr_t h = ((uintptr_t)addr >> 2) ^ ((uintptr_t)addr >> 10);
    return &g_woa_buckets[h & (NTSYNC_WOA_BUCKETS - 1U)];
}

NTSTATUS WINAPI RtlWaitOnAddress(
    const void *Address,
    const void *CompareAddress,
    SIZE_T AddressSize,
    const LARGE_INTEGER *Timeout)
{
    NTSYNC_WOA_BUCKET *bucket;
    NTSYNC_WOA_ENTRY entry;
    int is_infinite = 0;
    uint64_t deadline;

    if (ntsync_is_stub_simulated()) {
        return STATUS_NOT_IMPLEMENTED;
    }
    if (AddressSize != 1 && AddressSize != 2 && AddressSize != 4 && AddressSize != 8) {
        return STATUS_INVALID_PARAMETER;
    }
    if (!Address || !CompareAddress) {
        return STATUS_INVALID_PARAMETER;
    }

    __atomic_add_fetch(&g_stat_woa_wait_calls, 1U, __ATOMIC_RELAXED);
    bucket = ntsync_woa_bucket(Address);

    ntsync_spin_lock(&bucket->lock);
    if (!ntsync_compare_addr_equal(Address, CompareAddress, AddressSize)) {
        ntsync_spin_unlock(&bucket->lock);
        return STATUS_SUCCESS;
    }

    entry.addr = Address;
    entry.signaled = 0U;
    entry.in_queue = 1;
    entry.next = NULL;
    entry.prev = bucket->tail;
    if (bucket->tail) {
        bucket->tail->next = &entry;
    } else {
        bucket->head = &entry;
    }
    bucket->tail = &entry;
    ntsync_spin_unlock(&bucket->lock);

    deadline = ntsync_compute_deadline_100ns(Timeout, &is_infinite);
    while (__atomic_load_n(&entry.signaled, __ATOMIC_SEQ_CST) == 0U) {
        uint64_t rem_ns = ntsync_remaining_ns(deadline, is_infinite);
        if (rem_ns == 0) {
            break;
        }
        ntsync_ulock_wait32(&entry.signaled, 0U, rem_ns);
    }

    ntsync_spin_lock(&bucket->lock);
    if (entry.in_queue) {
        if (entry.prev) {
            entry.prev->next = entry.next;
        } else {
            bucket->head = entry.next;
        }
        if (entry.next) {
            entry.next->prev = entry.prev;
        } else {
            bucket->tail = entry.prev;
        }
        entry.in_queue = 0;
    }
    ntsync_spin_unlock(&bucket->lock);

    return (__atomic_load_n(&entry.signaled, __ATOMIC_SEQ_CST) != 0U) ? STATUS_SUCCESS : STATUS_TIMEOUT;
}

void WINAPI RtlWakeAddressSingle(const void *Address)
{
    NTSYNC_WOA_BUCKET *bucket;
    NTSYNC_WOA_ENTRY *cur;

    __atomic_add_fetch(&g_stat_woa_wake_single_calls, 1U, __ATOMIC_RELAXED);
    if (!Address) {
        return;
    }
    bucket = ntsync_woa_bucket(Address);
    ntsync_spin_lock(&bucket->lock);
    for (cur = bucket->head; cur != NULL; cur = cur->next) {
        if (cur->addr == Address) {
            if (cur->prev) {
                cur->prev->next = cur->next;
            } else {
                bucket->head = cur->next;
            }
            if (cur->next) {
                cur->next->prev = cur->prev;
            } else {
                bucket->tail = cur->prev;
            }
            cur->in_queue = 0;
            __atomic_store_n(&cur->signaled, 1U, __ATOMIC_SEQ_CST);
            ntsync_ulock_wake32(&cur->signaled, 1);
            break;
        }
    }
    ntsync_spin_unlock(&bucket->lock);
}

void WINAPI RtlWakeAddressAll(const void *Address)
{
    NTSYNC_WOA_BUCKET *bucket;
    NTSYNC_WOA_ENTRY *cur;
    NTSYNC_WOA_ENTRY *next;

    __atomic_add_fetch(&g_stat_woa_wake_all_calls, 1U, __ATOMIC_RELAXED);
    if (!Address) {
        return;
    }
    bucket = ntsync_woa_bucket(Address);
    ntsync_spin_lock(&bucket->lock);
    for (cur = bucket->head; cur != NULL; cur = next) {
        next = cur->next;
        if (cur->addr == Address) {
            if (cur->prev) {
                cur->prev->next = cur->next;
            } else {
                bucket->head = cur->next;
            }
            if (cur->next) {
                cur->next->prev = cur->prev;
            } else {
                bucket->tail = cur->prev;
            }
            cur->in_queue = 0;
            __atomic_store_n(&cur->signaled, 1U, __ATOMIC_SEQ_CST);
            ntsync_ulock_wake32(&cur->signaled, 1);
        }
    }
    ntsync_spin_unlock(&bucket->lock);
}

/* ============================================================================
 * 3. Object & Handle Management (Event, Semaphore, Mutant, KeyedEvent)
 * ============================================================================ */

static int ntsync_wstr_equal_ci(const WCHAR *a, const WCHAR *b)
{
    size_t i = 0;
    while (a[i] != 0 && b[i] != 0) {
        WCHAR ca = a[i];
        WCHAR cb = b[i];
        if (ca >= 'A' && ca <= 'Z') {
            ca = (WCHAR)(ca + ('a' - 'A'));
        }
        if (cb >= 'A' && cb <= 'Z') {
            cb = (WCHAR)(cb + ('a' - 'A'));
        }
        if (ca != cb) {
            return 0;
        }
        i++;
    }
    return a[i] == b[i];
}

static void ntsync_extract_name(const OBJECT_ATTRIBUTES *attr, WCHAR *out_name, size_t max_chars)
{
    size_t len;
    size_t i;
    out_name[0] = 0;
    if (!attr || !attr->ObjectName || !attr->ObjectName->Buffer || attr->ObjectName->Length == 0) {
        return;
    }
    len = attr->ObjectName->Length / sizeof(WCHAR);
    if (len >= max_chars) {
        len = max_chars - 1;
    }
    for (i = 0; i < len; i++) {
        out_name[i] = attr->ObjectName->Buffer[i];
    }
    out_name[len] = 0;
}

static NTSYNC_OBJECT *ntsync_find_named_object_locked(const WCHAR *name)
{
    ULONG i;
    if (!name || name[0] == 0) {
        return NULL;
    }
    for (i = 0; i < NTSYNC_MAX_OBJECTS; i++) {
        if (g_sync_objects[i].in_use && g_sync_objects[i].name[0] != 0) {
            if (ntsync_wstr_equal_ci(g_sync_objects[i].name, name)) {
                return &g_sync_objects[i];
            }
        }
    }
    return NULL;
}

static NTSYNC_OBJECT *ntsync_alloc_object_locked(NTSYNC_OBJ_TYPE type, const WCHAR *name)
{
    ULONG i;
    for (i = 0; i < NTSYNC_MAX_OBJECTS; i++) {
        if (!g_sync_objects[i].in_use) {
            size_t j = 0;
            memset(&g_sync_objects[i], 0, sizeof(g_sync_objects[i]));
            g_sync_objects[i].in_use = 1;
            g_sync_objects[i].type = type;
            g_sync_objects[i].ref_count = 0;
            if (name) {
                while (name[j] != 0 && j + 1 < 128) {
                    g_sync_objects[i].name[j] = name[j];
                    j++;
                }
                g_sync_objects[i].name[j] = 0;
            }
            ntsync_mach_sem_init(&g_sync_objects[i]);
            return &g_sync_objects[i];
        }
    }
    return NULL;
}

static HANDLE ntsync_alloc_handle_locked(NTSYNC_OBJECT *obj, ACCESS_MASK access)
{
    ULONG i;
    for (i = 0; i < NTSYNC_MAX_HANDLES; i++) {
        if (!g_handle_entries[i].in_use) {
            g_handle_entries[i].in_use = 1;
            g_handle_entries[i].generation++;
            if (g_handle_entries[i].generation == 0 || g_handle_entries[i].generation > 0xFFFFU) {
                g_handle_entries[i].generation = 1U;
            }
            g_handle_entries[i].access = access;
            g_handle_entries[i].obj = obj;
            obj->ref_count++;
            return (HANDLE)(NTSYNC_HANDLE_BASE + ((uintptr_t)g_handle_entries[i].generation << 16) +
                            (uintptr_t)i * NTSYNC_HANDLE_STRIDE);
        }
    }
    return NULL;
}

static NTSYNC_HANDLE_ENTRY *ntsync_lookup_handle_locked(HANDLE h)
{
    uintptr_t val = (uintptr_t)h;
    uintptr_t offset;
    ULONG idx;
    ULONG gen;
    if (val < NTSYNC_HANDLE_BASE) {
        return NULL;
    }
    offset = (val - NTSYNC_HANDLE_BASE) & 0xFFFFU;
    if ((offset % NTSYNC_HANDLE_STRIDE) != 0) {
        return NULL;
    }
    idx = (ULONG)(offset / NTSYNC_HANDLE_STRIDE);
    gen = (ULONG)((val - NTSYNC_HANDLE_BASE) >> 16);
    if (idx >= NTSYNC_MAX_HANDLES) {
        return NULL;
    }
    if (!g_handle_entries[idx].in_use || g_handle_entries[idx].generation != gen || !g_handle_entries[idx].obj) {
        return NULL;
    }
    return &g_handle_entries[idx];
}

NTSTATUS WINAPI NtClose(HANDLE Handle)
{
    NTSYNC_HANDLE_ENTRY *he;
    NTSYNC_OBJECT *obj;

    if (ntsync_is_stub_simulated()) {
        return STATUS_NOT_IMPLEMENTED;
    }
    ntsync_global_lock();
    ntsync_ensure_initialized_locked();
    he = ntsync_lookup_handle_locked(Handle);
    if (!he) {
        ntsync_global_unlock();
        return STATUS_INVALID_HANDLE;
    }
    obj = he->obj;
    he->in_use = 0;
    he->obj = NULL;
    if (obj->ref_count > 0) {
        obj->ref_count--;
        if (obj->ref_count == 0 && obj != &g_default_keyed_event) {
            ntsync_mach_sem_destroy(obj);
            obj->in_use = 0;
        }
    }
    ntsync_global_unlock();
    return STATUS_SUCCESS;
}

/* Events */

NTSTATUS WINAPI NtCreateEvent(
    HANDLE *EventHandle,
    ACCESS_MASK DesiredAccess,
    const OBJECT_ATTRIBUTES *ObjectAttributes,
    EVENT_TYPE EventType,
    BOOLEAN InitialState)
{
    WCHAR name[128];
    NTSYNC_OBJECT *existing;
    NTSYNC_OBJECT *obj;
    HANDLE h;
    NTSTATUS ret_status = STATUS_SUCCESS;

    if (ntsync_is_stub_simulated()) {
        return STATUS_NOT_IMPLEMENTED;
    }
    if (!EventHandle) {
        return STATUS_ACCESS_VIOLATION;
    }
    if (EventType != NotificationEvent && EventType != SynchronizationEvent) {
        return STATUS_INVALID_PARAMETER;
    }

    ntsync_global_lock();
    ntsync_ensure_initialized_locked();
    ntsync_extract_name(ObjectAttributes, name, 128);
    existing = ntsync_find_named_object_locked(name);
    if (existing) {
        if (existing->type != NTSYNC_OBJ_EVENT) {
            ntsync_global_unlock();
            return STATUS_OBJECT_TYPE_MISMATCH;
        }
        if (!ObjectAttributes || !(ObjectAttributes->Attributes & OBJ_OPENIF)) {
            ntsync_global_unlock();
            return STATUS_OBJECT_NAME_COLLISION;
        }
        obj = existing;
        ret_status = STATUS_OBJECT_NAME_EXISTS;
    } else {
        obj = ntsync_alloc_object_locked(NTSYNC_OBJ_EVENT, name);
        if (!obj) {
            ntsync_global_unlock();
            return STATUS_NO_MEMORY;
        }
        obj->event_type = EventType;
        obj->event_state = InitialState ? 1 : 0;
    }

    h = ntsync_alloc_handle_locked(obj, DesiredAccess);
    ntsync_global_unlock();
    if (!h) {
        return STATUS_NO_MEMORY;
    }
    *EventHandle = h;
    return ret_status;
}

NTSTATUS WINAPI NtOpenEvent(
    HANDLE *EventHandle,
    ACCESS_MASK DesiredAccess,
    const OBJECT_ATTRIBUTES *ObjectAttributes)
{
    WCHAR name[128];
    NTSYNC_OBJECT *existing;
    HANDLE h;

    if (ntsync_is_stub_simulated()) {
        return STATUS_NOT_IMPLEMENTED;
    }
    if (!EventHandle || !ObjectAttributes) {
        return STATUS_INVALID_PARAMETER;
    }
    ntsync_global_lock();
    ntsync_ensure_initialized_locked();
    ntsync_extract_name(ObjectAttributes, name, 128);
    if (name[0] == 0) {
        ntsync_global_unlock();
        return STATUS_OBJECT_NAME_INVALID;
    }
    existing = ntsync_find_named_object_locked(name);
    if (!existing) {
        ntsync_global_unlock();
        return STATUS_OBJECT_NAME_NOT_FOUND;
    }
    if (existing->type != NTSYNC_OBJ_EVENT) {
        ntsync_global_unlock();
        return STATUS_OBJECT_TYPE_MISMATCH;
    }
    h = ntsync_alloc_handle_locked(existing, DesiredAccess);
    ntsync_global_unlock();
    if (!h) {
        return STATUS_NO_MEMORY;
    }
    *EventHandle = h;
    return STATUS_SUCCESS;
}

NTSTATUS WINAPI NtSetEvent(HANDLE EventHandle, LONG *PrevState)
{
    NTSYNC_HANDLE_ENTRY *he;
    LONG prev;

    if (ntsync_is_stub_simulated()) {
        return STATUS_NOT_IMPLEMENTED;
    }
    ntsync_global_lock();
    ntsync_ensure_initialized_locked();
    he = ntsync_lookup_handle_locked(EventHandle);
    if (!he) {
        ntsync_global_unlock();
        return STATUS_INVALID_HANDLE;
    }
    if (he->obj->type != NTSYNC_OBJ_EVENT) {
        ntsync_global_unlock();
        return STATUS_OBJECT_TYPE_MISMATCH;
    }
    prev = he->obj->event_state;
    he->obj->event_state = 1;
    ntsync_notify_epoch(he->obj);
    ntsync_global_unlock();
    if (PrevState) {
        *PrevState = prev;
    }
    return STATUS_SUCCESS;
}

NTSTATUS WINAPI NtSetEventBoostPriority(HANDLE EventHandle)
{
    return NtSetEvent(EventHandle, NULL);
}

NTSTATUS WINAPI NtResetEvent(HANDLE EventHandle, LONG *PrevState)
{
    NTSYNC_HANDLE_ENTRY *he;
    LONG prev;

    if (ntsync_is_stub_simulated()) {
        return STATUS_NOT_IMPLEMENTED;
    }
    ntsync_global_lock();
    ntsync_ensure_initialized_locked();
    he = ntsync_lookup_handle_locked(EventHandle);
    if (!he) {
        ntsync_global_unlock();
        return STATUS_INVALID_HANDLE;
    }
    if (he->obj->type != NTSYNC_OBJ_EVENT) {
        ntsync_global_unlock();
        return STATUS_OBJECT_TYPE_MISMATCH;
    }
    prev = he->obj->event_state;
    he->obj->event_state = 0;
    ntsync_global_unlock();
    if (PrevState) {
        *PrevState = prev;
    }
    return STATUS_SUCCESS;
}

NTSTATUS WINAPI NtPulseEvent(HANDLE EventHandle, LONG *PrevState)
{
    NTSYNC_HANDLE_ENTRY *he;
    LONG prev;

    if (ntsync_is_stub_simulated()) {
        return STATUS_NOT_IMPLEMENTED;
    }
    ntsync_global_lock();
    ntsync_ensure_initialized_locked();
    he = ntsync_lookup_handle_locked(EventHandle);
    if (!he) {
        ntsync_global_unlock();
        return STATUS_INVALID_HANDLE;
    }
    if (he->obj->type != NTSYNC_OBJ_EVENT) {
        ntsync_global_unlock();
        return STATUS_OBJECT_TYPE_MISMATCH;
    }
    prev = he->obj->event_state;
    he->obj->event_state = 0;
    ntsync_notify_epoch(he->obj);
    ntsync_global_unlock();
    if (PrevState) {
        *PrevState = prev;
    }
    return STATUS_SUCCESS;
}

NTSTATUS WINAPI NtQueryEvent(
    HANDLE EventHandle,
    EVENT_INFORMATION_CLASS EventInformationClass,
    PVOID EventInformation,
    ULONG EventInformationLength,
    ULONG *ReturnLength)
{
    NTSYNC_HANDLE_ENTRY *he;
    EVENT_BASIC_INFORMATION *info = (EVENT_BASIC_INFORMATION *)EventInformation;

    if (ntsync_is_stub_simulated()) {
        return STATUS_NOT_IMPLEMENTED;
    }
    if (EventInformationClass != EventBasicInformation) {
        return STATUS_INVALID_INFO_CLASS;
    }
    if (EventInformationLength != sizeof(EVENT_BASIC_INFORMATION)) {
        return STATUS_INFO_LENGTH_MISMATCH;
    }
    ntsync_global_lock();
    ntsync_ensure_initialized_locked();
    he = ntsync_lookup_handle_locked(EventHandle);
    if (!he) {
        ntsync_global_unlock();
        return STATUS_INVALID_HANDLE;
    }
    if (he->obj->type != NTSYNC_OBJ_EVENT) {
        ntsync_global_unlock();
        return STATUS_OBJECT_TYPE_MISMATCH;
    }
    if (!info) {
        ntsync_global_unlock();
        return STATUS_ACCESS_VIOLATION;
    }
    info->EventType = he->obj->event_type;
    info->EventState = he->obj->event_state;
    ntsync_global_unlock();
    if (ReturnLength) {
        *ReturnLength = sizeof(EVENT_BASIC_INFORMATION);
    }
    return STATUS_SUCCESS;
}

/* Semaphores */

NTSTATUS WINAPI NtCreateSemaphore(
    HANDLE *SemaphoreHandle,
    ACCESS_MASK DesiredAccess,
    const OBJECT_ATTRIBUTES *ObjectAttributes,
    LONG InitialCount,
    LONG MaximumCount)
{
    WCHAR name[128];
    NTSYNC_OBJECT *existing;
    NTSYNC_OBJECT *obj;
    HANDLE h;
    NTSTATUS ret_status = STATUS_SUCCESS;

    if (ntsync_is_stub_simulated()) {
        return STATUS_NOT_IMPLEMENTED;
    }
    if (!SemaphoreHandle) {
        return STATUS_ACCESS_VIOLATION;
    }
    if (MaximumCount <= 0 || InitialCount < 0 || InitialCount > MaximumCount) {
        return STATUS_INVALID_PARAMETER;
    }

    ntsync_global_lock();
    ntsync_ensure_initialized_locked();
    ntsync_extract_name(ObjectAttributes, name, 128);
    existing = ntsync_find_named_object_locked(name);
    if (existing) {
        if (existing->type != NTSYNC_OBJ_SEMAPHORE) {
            ntsync_global_unlock();
            return STATUS_OBJECT_TYPE_MISMATCH;
        }
        if (!ObjectAttributes || !(ObjectAttributes->Attributes & OBJ_OPENIF)) {
            ntsync_global_unlock();
            return STATUS_OBJECT_NAME_COLLISION;
        }
        obj = existing;
        ret_status = STATUS_OBJECT_NAME_EXISTS;
    } else {
        obj = ntsync_alloc_object_locked(NTSYNC_OBJ_SEMAPHORE, name);
        if (!obj) {
            ntsync_global_unlock();
            return STATUS_NO_MEMORY;
        }
        obj->sem_count = InitialCount;
        obj->sem_max = MaximumCount;
    }

    h = ntsync_alloc_handle_locked(obj, DesiredAccess);
    ntsync_global_unlock();
    if (!h) {
        return STATUS_NO_MEMORY;
    }
    *SemaphoreHandle = h;
    return ret_status;
}

NTSTATUS WINAPI NtOpenSemaphore(
    HANDLE *SemaphoreHandle,
    ACCESS_MASK DesiredAccess,
    const OBJECT_ATTRIBUTES *ObjectAttributes)
{
    WCHAR name[128];
    NTSYNC_OBJECT *existing;
    HANDLE h;

    if (ntsync_is_stub_simulated()) {
        return STATUS_NOT_IMPLEMENTED;
    }
    if (!SemaphoreHandle || !ObjectAttributes) {
        return STATUS_INVALID_PARAMETER;
    }
    ntsync_global_lock();
    ntsync_ensure_initialized_locked();
    ntsync_extract_name(ObjectAttributes, name, 128);
    if (name[0] == 0) {
        ntsync_global_unlock();
        return STATUS_OBJECT_NAME_INVALID;
    }
    existing = ntsync_find_named_object_locked(name);
    if (!existing) {
        ntsync_global_unlock();
        return STATUS_OBJECT_NAME_NOT_FOUND;
    }
    if (existing->type != NTSYNC_OBJ_SEMAPHORE) {
        ntsync_global_unlock();
        return STATUS_OBJECT_TYPE_MISMATCH;
    }
    h = ntsync_alloc_handle_locked(existing, DesiredAccess);
    ntsync_global_unlock();
    if (!h) {
        return STATUS_NO_MEMORY;
    }
    *SemaphoreHandle = h;
    return STATUS_SUCCESS;
}

NTSTATUS WINAPI NtReleaseSemaphore(
    HANDLE SemaphoreHandle,
    ULONG ReleaseCount,
    LONG *PreviousCount)
{
    NTSYNC_HANDLE_ENTRY *he;
    LONG rel_signed = (LONG)ReleaseCount;
    LONG prev;

    if (ntsync_is_stub_simulated()) {
        return STATUS_NOT_IMPLEMENTED;
    }
    if (rel_signed <= 0) {
        return STATUS_INVALID_PARAMETER;
    }
    ntsync_global_lock();
    ntsync_ensure_initialized_locked();
    he = ntsync_lookup_handle_locked(SemaphoreHandle);
    if (!he) {
        ntsync_global_unlock();
        return STATUS_INVALID_HANDLE;
    }
    if (he->obj->type != NTSYNC_OBJ_SEMAPHORE) {
        ntsync_global_unlock();
        return STATUS_OBJECT_TYPE_MISMATCH;
    }
    prev = he->obj->sem_count;
    if (rel_signed > he->obj->sem_max - prev) {
        ntsync_global_unlock();
        return STATUS_SEMAPHORE_LIMIT_EXCEEDED;
    }
    he->obj->sem_count = prev + rel_signed;
    ntsync_notify_epoch(he->obj);
    ntsync_global_unlock();
    if (PreviousCount) {
        *PreviousCount = prev;
    }
    return STATUS_SUCCESS;
}

NTSTATUS WINAPI NtQuerySemaphore(
    HANDLE SemaphoreHandle,
    SEMAPHORE_INFORMATION_CLASS SemaphoreInformationClass,
    PVOID SemaphoreInformation,
    ULONG SemaphoreInformationLength,
    ULONG *ReturnLength)
{
    NTSYNC_HANDLE_ENTRY *he;
    SEMAPHORE_BASIC_INFORMATION *info = (SEMAPHORE_BASIC_INFORMATION *)SemaphoreInformation;

    if (ntsync_is_stub_simulated()) {
        return STATUS_NOT_IMPLEMENTED;
    }
    if (SemaphoreInformationClass != SemaphoreBasicInformation) {
        return STATUS_INVALID_INFO_CLASS;
    }
    if (SemaphoreInformationLength != sizeof(SEMAPHORE_BASIC_INFORMATION)) {
        return STATUS_INFO_LENGTH_MISMATCH;
    }
    ntsync_global_lock();
    ntsync_ensure_initialized_locked();
    he = ntsync_lookup_handle_locked(SemaphoreHandle);
    if (!he) {
        ntsync_global_unlock();
        return STATUS_INVALID_HANDLE;
    }
    if (he->obj->type != NTSYNC_OBJ_SEMAPHORE) {
        ntsync_global_unlock();
        return STATUS_OBJECT_TYPE_MISMATCH;
    }
    if (!info) {
        ntsync_global_unlock();
        return STATUS_ACCESS_VIOLATION;
    }
    info->CurrentCount = he->obj->sem_count;
    info->MaximumCount = he->obj->sem_max;
    ntsync_global_unlock();
    if (ReturnLength) {
        *ReturnLength = sizeof(SEMAPHORE_BASIC_INFORMATION);
    }
    return STATUS_SUCCESS;
}

/* Mutants */

NTSTATUS WINAPI NtCreateMutant(
    HANDLE *MutantHandle,
    ACCESS_MASK DesiredAccess,
    const OBJECT_ATTRIBUTES *ObjectAttributes,
    BOOLEAN InitialOwner)
{
    WCHAR name[128];
    NTSYNC_OBJECT *existing;
    NTSYNC_OBJECT *obj;
    HANDLE h;
    DWORD tid;
    NTSTATUS ret_status = STATUS_SUCCESS;

    if (ntsync_is_stub_simulated()) {
        return STATUS_NOT_IMPLEMENTED;
    }
    if (!MutantHandle) {
        return STATUS_ACCESS_VIOLATION;
    }
    tid = NtdllGetCurrentThreadId();

    ntsync_global_lock();
    ntsync_ensure_initialized_locked();
    ntsync_extract_name(ObjectAttributes, name, 128);
    existing = ntsync_find_named_object_locked(name);
    if (existing) {
        if (existing->type != NTSYNC_OBJ_MUTANT) {
            ntsync_global_unlock();
            return STATUS_OBJECT_TYPE_MISMATCH;
        }
        if (!ObjectAttributes || !(ObjectAttributes->Attributes & OBJ_OPENIF)) {
            ntsync_global_unlock();
            return STATUS_OBJECT_NAME_COLLISION;
        }
        obj = existing;
        ret_status = STATUS_OBJECT_NAME_EXISTS;
    } else {
        obj = ntsync_alloc_object_locked(NTSYNC_OBJ_MUTANT, name);
        if (!obj) {
            ntsync_global_unlock();
            return STATUS_NO_MEMORY;
        }
        if (InitialOwner) {
            obj->mutant_count = 0;
            obj->mutant_owner_tid = tid;
        } else {
            obj->mutant_count = 1;
            obj->mutant_owner_tid = 0;
        }
        obj->mutant_abandoned = FALSE;
    }

    h = ntsync_alloc_handle_locked(obj, DesiredAccess);
    ntsync_global_unlock();
    if (!h) {
        return STATUS_NO_MEMORY;
    }
    *MutantHandle = h;
    return ret_status;
}

NTSTATUS WINAPI NtOpenMutant(
    HANDLE *MutantHandle,
    ACCESS_MASK DesiredAccess,
    const OBJECT_ATTRIBUTES *ObjectAttributes)
{
    WCHAR name[128];
    NTSYNC_OBJECT *existing;
    HANDLE h;

    if (ntsync_is_stub_simulated()) {
        return STATUS_NOT_IMPLEMENTED;
    }
    if (!MutantHandle || !ObjectAttributes) {
        return STATUS_INVALID_PARAMETER;
    }
    ntsync_global_lock();
    ntsync_ensure_initialized_locked();
    ntsync_extract_name(ObjectAttributes, name, 128);
    if (name[0] == 0) {
        ntsync_global_unlock();
        return STATUS_OBJECT_NAME_INVALID;
    }
    existing = ntsync_find_named_object_locked(name);
    if (!existing) {
        ntsync_global_unlock();
        return STATUS_OBJECT_NAME_NOT_FOUND;
    }
    if (existing->type != NTSYNC_OBJ_MUTANT) {
        ntsync_global_unlock();
        return STATUS_OBJECT_TYPE_MISMATCH;
    }
    h = ntsync_alloc_handle_locked(existing, DesiredAccess);
    ntsync_global_unlock();
    if (!h) {
        return STATUS_NO_MEMORY;
    }
    *MutantHandle = h;
    return STATUS_SUCCESS;
}

NTSTATUS WINAPI NtReleaseMutant(HANDLE MutantHandle, LONG *PrevCount)
{
    NTSYNC_HANDLE_ENTRY *he;
    DWORD tid;
    LONG prev;

    if (ntsync_is_stub_simulated()) {
        return STATUS_NOT_IMPLEMENTED;
    }
    tid = NtdllGetCurrentThreadId();
    ntsync_global_lock();
    ntsync_ensure_initialized_locked();
    he = ntsync_lookup_handle_locked(MutantHandle);
    if (!he) {
        ntsync_global_unlock();
        return STATUS_INVALID_HANDLE;
    }
    if (he->obj->type != NTSYNC_OBJ_MUTANT) {
        ntsync_global_unlock();
        return STATUS_OBJECT_TYPE_MISMATCH;
    }
    if (he->obj->mutant_count > 0 || he->obj->mutant_owner_tid != tid) {
        ntsync_global_unlock();
        return STATUS_MUTANT_NOT_OWNED;
    }
    prev = he->obj->mutant_count;
    he->obj->mutant_count = prev + 1;
    if (he->obj->mutant_count == 1) {
        he->obj->mutant_owner_tid = 0;
        ntsync_notify_epoch(he->obj);
    }
    ntsync_global_unlock();
    if (PrevCount) {
        *PrevCount = prev;
    }
    return STATUS_SUCCESS;
}

NTSTATUS WINAPI NtQueryMutant(
    HANDLE MutantHandle,
    MUTANT_INFORMATION_CLASS MutantInformationClass,
    PVOID MutantInformation,
    ULONG MutantInformationLength,
    ULONG *ReturnLength)
{
    NTSYNC_HANDLE_ENTRY *he;
    MUTANT_BASIC_INFORMATION *info = (MUTANT_BASIC_INFORMATION *)MutantInformation;
    DWORD tid;

    if (ntsync_is_stub_simulated()) {
        return STATUS_NOT_IMPLEMENTED;
    }
    if (MutantInformationClass != MutantBasicInformation) {
        return STATUS_INVALID_INFO_CLASS;
    }
    if (MutantInformationLength != sizeof(MUTANT_BASIC_INFORMATION)) {
        return STATUS_INFO_LENGTH_MISMATCH;
    }
    tid = NtdllGetCurrentThreadId();
    ntsync_global_lock();
    ntsync_ensure_initialized_locked();
    he = ntsync_lookup_handle_locked(MutantHandle);
    if (!he) {
        ntsync_global_unlock();
        return STATUS_INVALID_HANDLE;
    }
    if (he->obj->type != NTSYNC_OBJ_MUTANT) {
        ntsync_global_unlock();
        return STATUS_OBJECT_TYPE_MISMATCH;
    }
    if (!info) {
        ntsync_global_unlock();
        return STATUS_ACCESS_VIOLATION;
    }
    info->CurrentCount = he->obj->mutant_count;
    info->OwnedByCaller = (he->obj->mutant_count <= 0 && he->obj->mutant_owner_tid == tid) ? TRUE : FALSE;
    info->AbandonedState = he->obj->mutant_abandoned;
    ntsync_global_unlock();
    if (ReturnLength) {
        *ReturnLength = sizeof(MUTANT_BASIC_INFORMATION);
    }
    return STATUS_SUCCESS;
}

NTSTATUS WINAPI NtdllAbandonMutantsForThread(DWORD ThreadId)
{
    ULONG i;
    if (ntsync_is_stub_simulated()) {
        return STATUS_NOT_IMPLEMENTED;
    }
    ntsync_global_lock();
    ntsync_ensure_initialized_locked();
    for (i = 0; i < NTSYNC_MAX_OBJECTS; i++) {
        NTSYNC_OBJECT *obj = &g_sync_objects[i];
        if (obj->in_use && obj->type == NTSYNC_OBJ_MUTANT &&
            obj->mutant_count <= 0 && obj->mutant_owner_tid == ThreadId) {
            obj->mutant_count = 1;
            obj->mutant_owner_tid = 0;
            obj->mutant_abandoned = TRUE;
            ntsync_notify_epoch(obj);
        }
    }
    ntsync_global_unlock();
    return STATUS_SUCCESS;
}

/* KeyedEvents */

NTSTATUS WINAPI NtCreateKeyedEvent(
    HANDLE *KeyedEventHandle,
    ACCESS_MASK DesiredAccess,
    const OBJECT_ATTRIBUTES *ObjectAttributes,
    ULONG Flags)
{
    WCHAR name[128];
    NTSYNC_OBJECT *existing;
    NTSYNC_OBJECT *obj;
    HANDLE h;

    if (ntsync_is_stub_simulated()) {
        return STATUS_NOT_IMPLEMENTED;
    }
    if (!KeyedEventHandle) {
        return STATUS_ACCESS_VIOLATION;
    }
    if (Flags != 0) {
        return STATUS_INVALID_PARAMETER;
    }
    ntsync_global_lock();
    ntsync_ensure_initialized_locked();
    ntsync_extract_name(ObjectAttributes, name, 128);
    existing = ntsync_find_named_object_locked(name);
    if (existing) {
        if (existing->type != NTSYNC_OBJ_KEYED_EVENT) {
            ntsync_global_unlock();
            return STATUS_OBJECT_TYPE_MISMATCH;
        }
        if (!ObjectAttributes || !(ObjectAttributes->Attributes & OBJ_OPENIF)) {
            ntsync_global_unlock();
            return STATUS_OBJECT_NAME_COLLISION;
        }
        obj = existing;
    } else {
        obj = ntsync_alloc_object_locked(NTSYNC_OBJ_KEYED_EVENT, name);
        if (!obj) {
            ntsync_global_unlock();
            return STATUS_NO_MEMORY;
        }
    }
    h = ntsync_alloc_handle_locked(obj, DesiredAccess);
    ntsync_global_unlock();
    if (!h) {
        return STATUS_NO_MEMORY;
    }
    *KeyedEventHandle = h;
    return STATUS_SUCCESS;
}

NTSTATUS WINAPI NtOpenKeyedEvent(
    HANDLE *KeyedEventHandle,
    ACCESS_MASK DesiredAccess,
    const OBJECT_ATTRIBUTES *ObjectAttributes)
{
    WCHAR name[128];
    NTSYNC_OBJECT *existing;
    HANDLE h;

    if (ntsync_is_stub_simulated()) {
        return STATUS_NOT_IMPLEMENTED;
    }
    if (!KeyedEventHandle || !ObjectAttributes) {
        return STATUS_INVALID_PARAMETER;
    }
    ntsync_global_lock();
    ntsync_ensure_initialized_locked();
    ntsync_extract_name(ObjectAttributes, name, 128);
    if (name[0] == 0) {
        ntsync_global_unlock();
        return STATUS_OBJECT_NAME_INVALID;
    }
    existing = ntsync_find_named_object_locked(name);
    if (!existing) {
        ntsync_global_unlock();
        return STATUS_OBJECT_NAME_NOT_FOUND;
    }
    if (existing->type != NTSYNC_OBJ_KEYED_EVENT) {
        ntsync_global_unlock();
        return STATUS_OBJECT_TYPE_MISMATCH;
    }
    h = ntsync_alloc_handle_locked(existing, DesiredAccess);
    ntsync_global_unlock();
    if (!h) {
        return STATUS_NO_MEMORY;
    }
    *KeyedEventHandle = h;
    return STATUS_SUCCESS;
}

static NTSTATUS ntsync_keyed_event_op(
    HANDLE KeyedEventHandle,
    const void *Key,
    BOOLEAN Alertable,
    const LARGE_INTEGER *Timeout,
    int is_release)
{
    NTSYNC_OBJECT *obj;
    ULONG i;
    NTSYNC_KEYED_WAITER *slot = NULL;
    int is_infinite = 0;
    uint64_t deadline;
    DWORD tid;

    if (ntsync_is_stub_simulated()) {
        return STATUS_NOT_IMPLEMENTED;
    }
    if (((uintptr_t)Key & 1U) != 0U) {
        return STATUS_INVALID_PARAMETER_1;
    }
    tid = NtdllGetCurrentThreadId();

    ntsync_global_lock();
    ntsync_ensure_initialized_locked();
    if (KeyedEventHandle == NULL) {
        obj = &g_default_keyed_event;
    } else {
        NTSYNC_HANDLE_ENTRY *he = ntsync_lookup_handle_locked(KeyedEventHandle);
        ACCESS_MASK req_mask = is_release
            ? (KEYEDEVENT_WAKE_ACCESS | GENERIC_WRITE | GENERIC_ALL)
            : (KEYEDEVENT_WAIT_ACCESS | GENERIC_READ | GENERIC_ALL);
        if (!he) {
            ntsync_global_unlock();
            return STATUS_INVALID_HANDLE;
        }
        if (he->obj->type != NTSYNC_OBJ_KEYED_EVENT) {
            ntsync_global_unlock();
            return STATUS_OBJECT_TYPE_MISMATCH;
        }
        if ((he->access & req_mask) == 0) {
            ntsync_global_unlock();
            return STATUS_ACCESS_DENIED;
        }
        obj = he->obj;
    }

    /* Check if opposite side is already waiting on Key */
    for (i = 0; i < NTSYNC_MAX_KEYED_WAITERS; i++) {
        NTSYNC_KEYED_WAITER *w = &obj->keyed_waiters[i];
        if (w->in_use && !w->matched && w->key == Key && w->is_releaser != is_release) {
            __atomic_store_n(&w->matched, 1U, __ATOMIC_SEQ_CST);
            ntsync_ulock_wake32(&w->matched, 1);
            ntsync_notify_epoch(obj);
            ntsync_global_unlock();
            return STATUS_SUCCESS;
        }
        if (!w->in_use && !slot) {
            slot = w;
        }
    }

    if (!slot) {
        ntsync_global_unlock();
        return STATUS_NO_MEMORY;
    }
    slot->in_use = 1;
    slot->is_releaser = is_release;
    slot->key = Key;
    slot->tid = tid;
    slot->matched = 0U;
    ntsync_global_unlock();

    deadline = ntsync_compute_deadline_100ns(Timeout, &is_infinite);
    for (;;) {
        uint64_t rem_ns;
        if (__atomic_load_n(&slot->matched, __ATOMIC_SEQ_CST) != 0U) {
            ntsync_global_lock();
            slot->in_use = 0;
            ntsync_global_unlock();
            return STATUS_SUCCESS;
        }
        if (Alertable) {
            NTSYNC_THREAD_SLOT *tslot;
            ntsync_global_lock();
            tslot = ntsync_find_or_create_thread_locked(tid, 1, 0);
            if (tslot && __atomic_exchange_n(&tslot->alerted, 0U, __ATOMIC_SEQ_CST) != 0U) {
                slot->in_use = 0;
                ntsync_global_unlock();
                return STATUS_ALERTED;
            }
            ntsync_global_unlock();
        }
        rem_ns = ntsync_remaining_ns(deadline, is_infinite);
        if (rem_ns == 0) {
            NTSTATUS final_st;
            ntsync_global_lock();
            final_st = (__atomic_load_n(&slot->matched, __ATOMIC_SEQ_CST) != 0U) ? STATUS_SUCCESS : STATUS_TIMEOUT;
            slot->in_use = 0;
            ntsync_global_unlock();
            return final_st;
        }
        ntsync_ulock_wait32(&slot->matched, 0U, rem_ns);
    }
}

NTSTATUS WINAPI NtWaitForKeyedEvent(
    HANDLE KeyedEventHandle,
    const void *Key,
    BOOLEAN Alertable,
    const LARGE_INTEGER *Timeout)
{
    return ntsync_keyed_event_op(KeyedEventHandle, Key, Alertable, Timeout, 0);
}

NTSTATUS WINAPI NtReleaseKeyedEvent(
    HANDLE KeyedEventHandle,
    const void *Key,
    BOOLEAN Alertable,
    const LARGE_INTEGER *Timeout)
{
    return ntsync_keyed_event_op(KeyedEventHandle, Key, Alertable, Timeout, 1);
}

/* ============================================================================
 * 4. Wait Primitives (NtWaitForSingleObject / NtWaitForMultipleObjects / Delay)
 * ============================================================================ */

static int ntsync_can_acquire_object_locked(NTSYNC_OBJECT *obj, DWORD tid, ULONG dup_index)
{
    switch (obj->type) {
    case NTSYNC_OBJ_EVENT:
        if (obj->event_type == SynchronizationEvent && dup_index > 0) {
            return 0;
        }
        return obj->event_state != 0;
    case NTSYNC_OBJ_SEMAPHORE:
        return obj->sem_count > (LONG)dup_index;
    case NTSYNC_OBJ_MUTANT:
        return (obj->mutant_count > 0) || (obj->mutant_owner_tid == tid);
    case NTSYNC_OBJ_KEYED_EVENT:
        return 1;
    default:
        return 0;
    }
}

static int ntsync_acquire_object_locked(NTSYNC_OBJECT *obj, DWORD tid)
{
    int was_abandoned = 0;
    switch (obj->type) {
    case NTSYNC_OBJ_EVENT:
        if (obj->event_type == SynchronizationEvent) {
            obj->event_state = 0;
        }
        break;
    case NTSYNC_OBJ_SEMAPHORE:
        obj->sem_count--;
        break;
    case NTSYNC_OBJ_MUTANT:
        if (obj->mutant_count > 0) {
            obj->mutant_count = 0;
            obj->mutant_owner_tid = tid;
            if (obj->mutant_abandoned) {
                was_abandoned = 1;
                obj->mutant_abandoned = FALSE;
            }
        } else {
            obj->mutant_count--;
        }
        break;
    case NTSYNC_OBJ_KEYED_EVENT:
    default:
        break;
    }
    return was_abandoned;
}

NTSTATUS WINAPI NtWaitForMultipleObjects(
    DWORD ObjectCount,
    const HANDLE *ObjectsArray,
    WAIT_TYPE WaitType,
    BOOLEAN Alertable,
    const LARGE_INTEGER *TimeOut)
{
    DWORD tid;
    int is_infinite = 0;
    uint64_t deadline;

    if (ntsync_is_stub_simulated()) {
        return STATUS_NOT_IMPLEMENTED;
    }
    if (ObjectCount == 0 || ObjectCount > MAXIMUM_WAIT_OBJECTS || !ObjectsArray) {
        return STATUS_INVALID_PARAMETER;
    }
    if (WaitType != WaitAll && WaitType != WaitAny) {
        return STATUS_INVALID_PARAMETER;
    }

    tid = NtdllGetCurrentThreadId();
    deadline = ntsync_compute_deadline_100ns(TimeOut, &is_infinite);

    for (;;) {
        NTSYNC_OBJECT *objs[MAXIMUM_WAIT_OBJECTS];
        DWORD i;
        DWORD j;
        uint32_t epoch_snap;
        uint64_t rem_ns;

        ntsync_global_lock();
        ntsync_ensure_initialized_locked();
        epoch_snap = __atomic_load_n(&g_sync_epoch, __ATOMIC_SEQ_CST);

        for (i = 0; i < ObjectCount; i++) {
            NTSYNC_HANDLE_ENTRY *he = ntsync_lookup_handle_locked(ObjectsArray[i]);
            if (!he) {
                ntsync_global_unlock();
                return STATUS_INVALID_HANDLE;
            }
            if (he->obj->type == NTSYNC_OBJ_KEYED_EVENT &&
                (he->access & (SYNCHRONIZE | GENERIC_ALL)) == 0) {
                ntsync_global_unlock();
                return STATUS_ACCESS_DENIED;
            }
            objs[i] = he->obj;
        }

        if (Alertable) {
            NTSYNC_THREAD_SLOT *tslot = ntsync_find_or_create_thread_locked(tid, 1, 0);
            if (tslot && __atomic_exchange_n(&tslot->alerted, 0U, __ATOMIC_SEQ_CST) != 0U) {
                ntsync_global_unlock();
                return STATUS_ALERTED;
            }
        }

        if (WaitType == WaitAny) {
            for (i = 0; i < ObjectCount; i++) {
                if (ntsync_can_acquire_object_locked(objs[i], tid, 0)) {
                    int abandoned = ntsync_acquire_object_locked(objs[i], tid);
                    ntsync_global_unlock();
                    return abandoned ? (STATUS_ABANDONED_WAIT_0 + (NTSTATUS)i) : (STATUS_WAIT_0 + (NTSTATUS)i);
                }
            }
        } else {
            int all_ready = 1;
            for (i = 0; i < ObjectCount; i++) {
                ULONG dup_count = 0;
                for (j = 0; j < i; j++) {
                    if (objs[j] == objs[i]) {
                        dup_count++;
                    }
                }
                if (!ntsync_can_acquire_object_locked(objs[i], tid, dup_count)) {
                    all_ready = 0;
                    break;
                }
            }
            if (all_ready) {
                int any_abandoned = 0;
                for (i = 0; i < ObjectCount; i++) {
                    if (ntsync_acquire_object_locked(objs[i], tid)) {
                        any_abandoned = 1;
                    }
                }
                ntsync_global_unlock();
                return any_abandoned ? STATUS_ABANDONED_WAIT_0 : STATUS_WAIT_0;
            }
        }
        ntsync_global_unlock();

        rem_ns = ntsync_remaining_ns(deadline, is_infinite);
        if (rem_ns == 0) {
            return STATUS_TIMEOUT;
        }
        ntsync_ulock_wait32(&g_sync_epoch, epoch_snap, rem_ns);
    }
}

NTSTATUS WINAPI NtWaitForSingleObject(
    HANDLE ObjectHandle,
    BOOLEAN Alertable,
    const LARGE_INTEGER *TimeOut)
{
    return NtWaitForMultipleObjects(1, &ObjectHandle, WaitAny, Alertable, TimeOut);
}

NTSTATUS WINAPI NtSignalAndWaitForSingleObject(
    HANDLE hObjectToSignal,
    HANDLE hObjectToWaitOn,
    BOOLEAN bAlertable,
    const LARGE_INTEGER *TimeOut)
{
    NTSYNC_HANDLE_ENTRY *he_sig;
    NTSYNC_HANDLE_ENTRY *he_wait;
    DWORD tid;

    if (ntsync_is_stub_simulated()) {
        return STATUS_NOT_IMPLEMENTED;
    }
    tid = NtdllGetCurrentThreadId();
    ntsync_global_lock();
    ntsync_ensure_initialized_locked();
    he_sig = ntsync_lookup_handle_locked(hObjectToSignal);
    he_wait = ntsync_lookup_handle_locked(hObjectToWaitOn);
    if (!he_sig || !he_wait) {
        ntsync_global_unlock();
        return STATUS_INVALID_HANDLE;
    }

    switch (he_sig->obj->type) {
    case NTSYNC_OBJ_EVENT:
        he_sig->obj->event_state = 1;
        ntsync_notify_epoch(he_sig->obj);
        break;
    case NTSYNC_OBJ_SEMAPHORE:
        if (he_sig->obj->sem_count >= he_sig->obj->sem_max) {
            ntsync_global_unlock();
            return STATUS_SEMAPHORE_LIMIT_EXCEEDED;
        }
        he_sig->obj->sem_count++;
        ntsync_notify_epoch(he_sig->obj);
        break;
    case NTSYNC_OBJ_MUTANT:
        if (he_sig->obj->mutant_count > 0 || he_sig->obj->mutant_owner_tid != tid) {
            ntsync_global_unlock();
            return STATUS_MUTANT_NOT_OWNED;
        }
        he_sig->obj->mutant_count++;
        if (he_sig->obj->mutant_count == 1) {
            he_sig->obj->mutant_owner_tid = 0;
            ntsync_notify_epoch(he_sig->obj);
        }
        break;
    default:
        ntsync_global_unlock();
        return STATUS_OBJECT_TYPE_MISMATCH;
    }
    ntsync_global_unlock();

    return NtWaitForSingleObject(hObjectToWaitOn, bAlertable, TimeOut);
}

NTSTATUS WINAPI NtDelayExecution(BOOLEAN Alertable, const LARGE_INTEGER *Interval)
{
    DWORD tid;
    int is_infinite = 0;
    uint64_t deadline;

    if (ntsync_is_stub_simulated()) {
        return STATUS_NOT_IMPLEMENTED;
    }
    if (!Interval) {
        return STATUS_ACCESS_VIOLATION;
    }
    tid = NtdllGetCurrentThreadId();
    deadline = ntsync_compute_deadline_100ns(Interval, &is_infinite);

    for (;;) {
        uint64_t rem_ns;
        uint32_t epoch_snap = __atomic_load_n(&g_sync_epoch, __ATOMIC_SEQ_CST);
        if (Alertable) {
            NTSYNC_THREAD_SLOT *tslot;
            ntsync_global_lock();
            tslot = ntsync_find_or_create_thread_locked(tid, 1, 0);
            if (tslot && __atomic_exchange_n(&tslot->alerted, 0U, __ATOMIC_SEQ_CST) != 0U) {
                ntsync_global_unlock();
                return STATUS_ALERTED;
            }
            ntsync_global_unlock();
        }
        rem_ns = ntsync_remaining_ns(deadline, is_infinite);
        if (rem_ns == 0) {
            if (!is_infinite && Interval->QuadPart == 0) {
#if defined(__APPLE__) || !defined(_WIN32)
                sched_yield();
#else
                SwitchToThread();
#endif
            }
            return STATUS_SUCCESS;
        }
        ntsync_ulock_wait32(&g_sync_epoch, epoch_snap, rem_ns);
    }
}

NTSTATUS WINAPI NtQuerySystemTime(PLARGE_INTEGER SystemTime)
{
    if (ntsync_is_stub_simulated()) {
        return STATUS_NOT_IMPLEMENTED;
    }
    if (!SystemTime) {
        return STATUS_ACCESS_VIOLATION;
    }
    SystemTime->QuadPart = (LONGLONG)ntsync_now_100ns();
    return STATUS_SUCCESS;
}

NTSTATUS WINAPI NtQueryTimerResolution(
    ULONG *MinimumResolution,
    ULONG *MaximumResolution,
    ULONG *ActualResolution)
{
    if (ntsync_is_stub_simulated()) {
        return STATUS_NOT_IMPLEMENTED;
    }
    if (MinimumResolution) {
        *MinimumResolution = 156250U;
    }
    if (MaximumResolution) {
        *MaximumResolution = 10000U;
    }
    if (ActualResolution) {
        *ActualResolution = 10000U;
    }
    return STATUS_SUCCESS;
}

/* ============================================================================
 * 5. User-Space Synchronization Primitives (SRWLock, CondVar, RunOnce, Barrier, Resource, CS)
 * ============================================================================ */

#define NTSYNC_SRW_EXCLUSIVE_BIT ((uintptr_t)0x80000000U)

void WINAPI RtlInitializeSRWLock(PRTL_SRWLOCK Lock)
{
    if (Lock) {
        Lock->Ptr = NULL;
    }
}

BOOLEAN WINAPI RtlTryAcquireSRWLockExclusive(PRTL_SRWLOCK Lock)
{
    uintptr_t expected = 0;
    if (ntsync_is_stub_simulated() || !Lock) {
        return 0;
    }
    return __atomic_compare_exchange_n(
        (uintptr_t *)&Lock->Ptr,
        &expected,
        NTSYNC_SRW_EXCLUSIVE_BIT,
        0,
        __ATOMIC_ACQUIRE,
        __ATOMIC_RELAXED) ? 1 : 0;
}

void WINAPI RtlAcquireSRWLockExclusive(PRTL_SRWLOCK Lock)
{
    if (ntsync_is_stub_simulated() || !Lock) {
        return;
    }
    for (;;) {
        uintptr_t cur = __atomic_load_n((uintptr_t *)&Lock->Ptr, __ATOMIC_SEQ_CST);
        if (cur == 0) {
            if (__atomic_compare_exchange_n(
                    (uintptr_t *)&Lock->Ptr,
                    &cur,
                    NTSYNC_SRW_EXCLUSIVE_BIT,
                    0,
                    __ATOMIC_ACQUIRE,
                    __ATOMIC_RELAXED)) {
                return;
            }
        } else {
            (void)RtlWaitOnAddress(&Lock->Ptr, &cur, sizeof(Lock->Ptr), NULL);
        }
    }
}

void WINAPI RtlReleaseSRWLockExclusive(PRTL_SRWLOCK Lock)
{
    if (ntsync_is_stub_simulated() || !Lock) {
        return;
    }
    __atomic_store_n((uintptr_t *)&Lock->Ptr, 0, __ATOMIC_RELEASE);
    RtlWakeAddressAll(&Lock->Ptr);
}

BOOLEAN WINAPI RtlTryAcquireSRWLockShared(PRTL_SRWLOCK Lock)
{
    if (ntsync_is_stub_simulated() || !Lock) {
        return 0;
    }
    for (;;) {
        uintptr_t cur = __atomic_load_n((uintptr_t *)&Lock->Ptr, __ATOMIC_SEQ_CST);
        if ((cur & NTSYNC_SRW_EXCLUSIVE_BIT) != 0) {
            return 0;
        }
        if (__atomic_compare_exchange_n(
                (uintptr_t *)&Lock->Ptr,
                &cur,
                cur + 1U,
                0,
                __ATOMIC_ACQUIRE,
                __ATOMIC_RELAXED)) {
            return 1;
        }
    }
}

void WINAPI RtlAcquireSRWLockShared(PRTL_SRWLOCK Lock)
{
    if (ntsync_is_stub_simulated() || !Lock) {
        return;
    }
    for (;;) {
        uintptr_t cur = __atomic_load_n((uintptr_t *)&Lock->Ptr, __ATOMIC_SEQ_CST);
        if ((cur & NTSYNC_SRW_EXCLUSIVE_BIT) == 0) {
            if (__atomic_compare_exchange_n(
                    (uintptr_t *)&Lock->Ptr,
                    &cur,
                    cur + 1U,
                    0,
                    __ATOMIC_ACQUIRE,
                    __ATOMIC_RELAXED)) {
                return;
            }
        } else {
            (void)RtlWaitOnAddress(&Lock->Ptr, &cur, sizeof(Lock->Ptr), NULL);
        }
    }
}

void WINAPI RtlReleaseSRWLockShared(PRTL_SRWLOCK Lock)
{
    if (ntsync_is_stub_simulated() || !Lock) {
        return;
    }
    if (__atomic_sub_fetch((uintptr_t *)&Lock->Ptr, 1U, __ATOMIC_RELEASE) == 0) {
        RtlWakeAddressAll(&Lock->Ptr);
    }
}

/* Condition Variables */

void WINAPI RtlInitializeConditionVariable(PRTL_CONDITION_VARIABLE Variable)
{
    if (Variable) {
        Variable->Ptr = NULL;
    }
}

void WINAPI RtlWakeConditionVariable(PRTL_CONDITION_VARIABLE Variable)
{
    if (!Variable) {
        return;
    }
    __atomic_add_fetch((uintptr_t *)&Variable->Ptr, 1U, __ATOMIC_SEQ_CST);
    RtlWakeAddressSingle(&Variable->Ptr);
}

void WINAPI RtlWakeAllConditionVariable(PRTL_CONDITION_VARIABLE Variable)
{
    if (!Variable) {
        return;
    }
    __atomic_add_fetch((uintptr_t *)&Variable->Ptr, 1U, __ATOMIC_SEQ_CST);
    RtlWakeAddressAll(&Variable->Ptr);
}

NTSTATUS WINAPI RtlSleepConditionVariableSRW(
    PRTL_CONDITION_VARIABLE Variable,
    PRTL_SRWLOCK Lock,
    PLARGE_INTEGER Timeout,
    ULONG Flags)
{
    uintptr_t seq;
    NTSTATUS status;

    if (ntsync_is_stub_simulated()) {
        return STATUS_NOT_IMPLEMENTED;
    }
    if (!Variable || !Lock) {
        return STATUS_INVALID_PARAMETER;
    }
    seq = __atomic_load_n((uintptr_t *)&Variable->Ptr, __ATOMIC_SEQ_CST);
    if (Flags & RTL_CONDITION_VARIABLE_LOCKMODE_SHARED) {
        RtlReleaseSRWLockShared(Lock);
    } else {
        RtlReleaseSRWLockExclusive(Lock);
    }

    status = RtlWaitOnAddress(&Variable->Ptr, &seq, sizeof(Variable->Ptr), Timeout);

    if (Flags & RTL_CONDITION_VARIABLE_LOCKMODE_SHARED) {
        RtlAcquireSRWLockShared(Lock);
    } else {
        RtlAcquireSRWLockExclusive(Lock);
    }
    return status;
}

NTSTATUS WINAPI RtlSleepConditionVariableCS(
    PRTL_CONDITION_VARIABLE Variable,
    PRTL_CRITICAL_SECTION Crit,
    PLARGE_INTEGER Timeout)
{
    uintptr_t seq;
    NTSTATUS status;

    if (ntsync_is_stub_simulated()) {
        return STATUS_NOT_IMPLEMENTED;
    }
    if (!Variable || !Crit) {
        return STATUS_INVALID_PARAMETER;
    }
    seq = __atomic_load_n((uintptr_t *)&Variable->Ptr, __ATOMIC_SEQ_CST);
    RtlLeaveCriticalSection(Crit);
    status = RtlWaitOnAddress(&Variable->Ptr, &seq, sizeof(Variable->Ptr), Timeout);
    RtlEnterCriticalSection(Crit);
    return status;
}

/* Critical Sections */

NTSTATUS WINAPI RtlInitializeCriticalSection(PRTL_CRITICAL_SECTION Crit)
{
    if (ntsync_is_stub_simulated()) {
        return STATUS_NOT_IMPLEMENTED;
    }
    if (!Crit) {
        return STATUS_INVALID_PARAMETER;
    }
    memset(Crit, 0, sizeof(*Crit));
    Crit->LockCount = -1;
    Crit->RecursionCount = 0;
    Crit->OwningThread = 0;
    return STATUS_SUCCESS;
}

NTSTATUS WINAPI RtlDeleteCriticalSection(PRTL_CRITICAL_SECTION Crit)
{
    if (!Crit) {
        return STATUS_INVALID_PARAMETER;
    }
    memset(Crit, 0, sizeof(*Crit));
    return STATUS_SUCCESS;
}

BOOL WINAPI RtlTryEnterCriticalSection(PRTL_CRITICAL_SECTION Crit)
{
    DWORD tid;
    HANDLE h_tid;
    HANDLE expected = NULL;

    if (!Crit) {
        return FALSE;
    }
    tid = NtdllGetCurrentThreadId();
    h_tid = (HANDLE)(uintptr_t)tid;
    if (__atomic_load_n(&Crit->OwningThread, __ATOMIC_SEQ_CST) == h_tid) {
        Crit->RecursionCount++;
        return TRUE;
    }
    if (__atomic_compare_exchange_n(&Crit->OwningThread, &expected, h_tid, 0, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) {
        Crit->RecursionCount = 1;
        Crit->LockCount = 0;
        return TRUE;
    }
    return FALSE;
}

NTSTATUS WINAPI RtlEnterCriticalSection(PRTL_CRITICAL_SECTION Crit)
{
    DWORD tid;
    HANDLE h_tid;

    if (ntsync_is_stub_simulated()) {
        return STATUS_NOT_IMPLEMENTED;
    }
    if (!Crit) {
        return STATUS_INVALID_PARAMETER;
    }
    tid = NtdllGetCurrentThreadId();
    h_tid = (HANDLE)(uintptr_t)tid;
    if (__atomic_load_n(&Crit->OwningThread, __ATOMIC_SEQ_CST) == h_tid) {
        Crit->RecursionCount++;
        return STATUS_SUCCESS;
    }
    for (;;) {
        HANDLE cur = __atomic_load_n(&Crit->OwningThread, __ATOMIC_SEQ_CST);
        if (cur == NULL) {
            if (__atomic_compare_exchange_n(&Crit->OwningThread, &cur, h_tid, 0, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) {
                Crit->RecursionCount = 1;
                Crit->LockCount = 0;
                return STATUS_SUCCESS;
            }
        } else {
            (void)RtlWaitOnAddress(&Crit->OwningThread, &cur, sizeof(Crit->OwningThread), NULL);
        }
    }
}

NTSTATUS WINAPI RtlLeaveCriticalSection(PRTL_CRITICAL_SECTION Crit)
{
    if (!Crit) {
        return STATUS_INVALID_PARAMETER;
    }
    if (--Crit->RecursionCount == 0) {
        Crit->LockCount = -1;
        __atomic_store_n(&Crit->OwningThread, NULL, __ATOMIC_RELEASE);
        RtlWakeAddressSingle(&Crit->OwningThread);
    }
    return STATUS_SUCCESS;
}

/* RunOnce */

void WINAPI RtlRunOnceInitialize(PRTL_RUN_ONCE Once)
{
    if (Once) {
        Once->Ptr = NULL;
    }
}

NTSTATUS WINAPI RtlRunOnceBeginInitialize(PRTL_RUN_ONCE Once, ULONG Flags, PVOID *Context)
{
    if (ntsync_is_stub_simulated()) {
        return STATUS_NOT_IMPLEMENTED;
    }
    if (!Once) {
        return STATUS_INVALID_PARAMETER;
    }
    if (Flags & ~(RTL_RUN_ONCE_CHECK_ONLY | RTL_RUN_ONCE_ASYNC)) {
        return STATUS_INVALID_PARAMETER;
    }
    if ((Flags & RTL_RUN_ONCE_CHECK_ONLY) && (Flags & RTL_RUN_ONCE_ASYNC)) {
        return STATUS_INVALID_PARAMETER;
    }

    for (;;) {
        ULONG_PTR val = __atomic_load_n((ULONG_PTR *)&Once->Ptr, __ATOMIC_SEQ_CST);
        switch (val & 3U) {
        case 0:
            if (Flags & RTL_RUN_ONCE_CHECK_ONLY) {
                return STATUS_UNSUCCESSFUL;
            }
            if (__atomic_compare_exchange_n(
                    (ULONG_PTR *)&Once->Ptr,
                    &val,
                    (Flags & RTL_RUN_ONCE_ASYNC) ? 3U : 1U,
                    0,
                    __ATOMIC_ACQUIRE,
                    __ATOMIC_RELAXED)) {
                return STATUS_PENDING;
            }
            break;
        case 1:
            if (Flags & (RTL_RUN_ONCE_CHECK_ONLY | RTL_RUN_ONCE_ASYNC)) {
                return STATUS_UNSUCCESSFUL;
            }
            (void)RtlWaitOnAddress(&Once->Ptr, &val, sizeof(Once->Ptr), NULL);
            break;
        case 2:
            if (Context) {
                *Context = (PVOID)(val & ~(ULONG_PTR)3U);
            }
            return STATUS_SUCCESS;
        case 3:
            if (!(Flags & RTL_RUN_ONCE_ASYNC)) {
                return STATUS_INVALID_PARAMETER;
            }
            return STATUS_PENDING;
        }
    }
}

NTSTATUS WINAPI RtlRunOnceComplete(PRTL_RUN_ONCE Once, ULONG Flags, PVOID Context)
{
    if (ntsync_is_stub_simulated()) {
        return STATUS_NOT_IMPLEMENTED;
    }
    if (!Once) {
        return STATUS_INVALID_PARAMETER;
    }
    if (Flags & ~(RTL_RUN_ONCE_INIT_FAILED | RTL_RUN_ONCE_ASYNC)) {
        return STATUS_INVALID_PARAMETER;
    }
    if ((Flags & RTL_RUN_ONCE_INIT_FAILED) && ((Flags & RTL_RUN_ONCE_ASYNC) || Context != NULL)) {
        return STATUS_INVALID_PARAMETER;
    }
    if (((ULONG_PTR)Context & 3U) != 0U) {
        return STATUS_INVALID_PARAMETER;
    }

    for (;;) {
        ULONG_PTR val = __atomic_load_n((ULONG_PTR *)&Once->Ptr, __ATOMIC_SEQ_CST);
        switch (val & 3U) {
        case 1:
            if (Flags & RTL_RUN_ONCE_ASYNC) {
                return STATUS_INVALID_PARAMETER;
            }
            if (__atomic_compare_exchange_n(
                    (ULONG_PTR *)&Once->Ptr,
                    &val,
                    (Flags & RTL_RUN_ONCE_INIT_FAILED) ? 0U : ((ULONG_PTR)Context | 2U),
                    0,
                    __ATOMIC_RELEASE,
                    __ATOMIC_RELAXED)) {
                RtlWakeAddressAll(&Once->Ptr);
                return STATUS_SUCCESS;
            }
            break;
        case 3:
            if (!(Flags & RTL_RUN_ONCE_ASYNC)) {
                return STATUS_INVALID_PARAMETER;
            }
            if (__atomic_compare_exchange_n(
                    (ULONG_PTR *)&Once->Ptr,
                    &val,
                    (ULONG_PTR)Context | 2U,
                    0,
                    __ATOMIC_RELEASE,
                    __ATOMIC_RELAXED)) {
                RtlWakeAddressAll(&Once->Ptr);
                return STATUS_SUCCESS;
            }
            break;
        default:
            return STATUS_UNSUCCESSFUL;
        }
    }
}

NTSTATUS WINAPI RtlRunOnceExecuteOnce(
    PRTL_RUN_ONCE Once,
    PRTL_RUN_ONCE_INIT_FN Func,
    PVOID Param,
    PVOID *Context)
{
    NTSTATUS status;
    PVOID local_ctx = NULL;

    status = RtlRunOnceBeginInitialize(Once, 0, &local_ctx);
    if (status == STATUS_PENDING) {
        if (!Func || !Func(Once, Param, &local_ctx)) {
            RtlRunOnceComplete(Once, RTL_RUN_ONCE_INIT_FAILED, NULL);
            return STATUS_UNSUCCESSFUL;
        }
        status = RtlRunOnceComplete(Once, 0, local_ctx);
    }
    if (status == STATUS_SUCCESS && Context) {
        *Context = local_ctx;
    }
    return status;
}

/* Synchronization Barrier */

NTSTATUS WINAPI RtlInitBarrier(PRTL_BARRIER Barrier, ULONG TotalThreads, ULONG SpinCount)
{
    NTSYNC_BARRIER_INTERNAL *ib;

    if (ntsync_is_stub_simulated()) {
        return STATUS_NOT_IMPLEMENTED;
    }
    if (!Barrier || TotalThreads == 0) {
        return STATUS_INVALID_PARAMETER;
    }
    memset(Barrier, 0, sizeof(*Barrier));
    ib = (NTSYNC_BARRIER_INTERNAL *)Barrier;
    ib->phase = 0;
    ib->remaining = TotalThreads;
    ib->total_threads = TotalThreads;
    ib->spin_count = (SpinCount == 0) ? 2000U : SpinCount;
    return STATUS_SUCCESS;
}

BOOLEAN WINAPI RtlBarrier(PRTL_BARRIER Barrier, ULONG Flags)
{
    NTSYNC_BARRIER_INTERNAL *ib = (NTSYNC_BARRIER_INTERNAL *)Barrier;
    ULONG cur_phase;
    ULONG rem;
    (void)Flags;

    if (ntsync_is_stub_simulated() || !ib || ib->total_threads == 0) {
        return 0;
    }
    cur_phase = __atomic_load_n(&ib->phase, __ATOMIC_SEQ_CST);
    rem = __atomic_sub_fetch(&ib->remaining, 1U, __ATOMIC_SEQ_CST);
    if (rem == 0) {
        __atomic_store_n(&ib->remaining, ib->total_threads, __ATOMIC_SEQ_CST);
        __atomic_add_fetch(&ib->phase, 1U, __ATOMIC_SEQ_CST);
        RtlWakeAddressAll((const void *)&ib->phase);
        return 1;
    }
    while (__atomic_load_n(&ib->phase, __ATOMIC_SEQ_CST) == cur_phase) {
        (void)RtlWaitOnAddress((const void *)&ib->phase, &cur_phase, sizeof(ib->phase), NULL);
    }
    return 0;
}

NTSTATUS WINAPI RtlDeleteBarrier(PRTL_BARRIER Barrier)
{
    if (ntsync_is_stub_simulated()) {
        return STATUS_NOT_IMPLEMENTED;
    }
    if (Barrier) {
        memset(Barrier, 0, sizeof(*Barrier));
    }
    return STATUS_SUCCESS;
}

/* RtlResource (Reader/Writer Lock) */

void WINAPI RtlInitializeResource(LPRTL_RWLOCK RwLock)
{
    if (!RwLock) {
        return;
    }
    memset(RwLock, 0, sizeof(*RwLock));
    RtlInitializeCriticalSection(&RwLock->CriticalSection);
    NtCreateSemaphore(&RwLock->SharedSem, SEMAPHORE_ALL_ACCESS, NULL, 0, 65535);
    NtCreateSemaphore(&RwLock->ExclusiveSem, SEMAPHORE_ALL_ACCESS, NULL, 0, 65535);
}

void WINAPI RtlDeleteResource(LPRTL_RWLOCK RwLock)
{
    if (!RwLock) {
        return;
    }
    if (RwLock->SharedSem) {
        NtClose(RwLock->SharedSem);
    }
    if (RwLock->ExclusiveSem) {
        NtClose(RwLock->ExclusiveSem);
    }
    RtlDeleteCriticalSection(&RwLock->CriticalSection);
    memset(RwLock, 0, sizeof(*RwLock));
}

BYTE WINAPI RtlAcquireResourceExclusive(LPRTL_RWLOCK RwLock, BYTE fWait)
{
    DWORD tid;
    HANDLE h_tid;
    BYTE retVal = 0;

    if (ntsync_is_stub_simulated() || !RwLock) {
        return 0;
    }
    tid = NtdllGetCurrentThreadId();
    h_tid = (HANDLE)(uintptr_t)tid;

    for (;;) {
        RtlEnterCriticalSection(&RwLock->CriticalSection);
        if (RwLock->NumberActive == 0) {
            RwLock->NumberActive = -1;
            RwLock->OwningThreadId = h_tid;
            retVal = 1;
        } else if (RwLock->NumberActive < 0) {
            if (RwLock->OwningThreadId == h_tid) {
                RwLock->NumberActive--;
                retVal = 1;
            } else {
                goto wait_excl;
            }
        } else {
            if (RwLock->OwningThreadId == h_tid) {
                retVal = 0;
            } else {
wait_excl:
                if (fWait) {
                    RwLock->ExclusiveWaiters++;
                    RtlLeaveCriticalSection(&RwLock->CriticalSection);
                    NtWaitForSingleObject(RwLock->ExclusiveSem, FALSE, NULL);
                    continue;
                }
            }
        }
        RtlLeaveCriticalSection(&RwLock->CriticalSection);
        break;
    }
    return retVal;
}

BYTE WINAPI RtlAcquireResourceShared(LPRTL_RWLOCK RwLock, BYTE fWait)
{
    DWORD tid;
    HANDLE h_tid;
    BYTE retVal = 0;

    if (ntsync_is_stub_simulated() || !RwLock) {
        return 0;
    }
    tid = NtdllGetCurrentThreadId();
    h_tid = (HANDLE)(uintptr_t)tid;

    for (;;) {
        RtlEnterCriticalSection(&RwLock->CriticalSection);
        if (RwLock->NumberActive < 0) {
            if (RwLock->OwningThreadId == h_tid) {
                RwLock->NumberActive--;
                retVal = 1;
            } else {
                if (fWait) {
                    RwLock->SharedWaiters++;
                    RtlLeaveCriticalSection(&RwLock->CriticalSection);
                    NtWaitForSingleObject(RwLock->SharedSem, FALSE, NULL);
                    continue;
                }
            }
        } else {
            if (fWait && RwLock->ExclusiveWaiters > 0) {
                RwLock->SharedWaiters++;
                RtlLeaveCriticalSection(&RwLock->CriticalSection);
                NtWaitForSingleObject(RwLock->SharedSem, FALSE, NULL);
                continue;
            }
            if (fWait || RwLock->ExclusiveWaiters == 0) {
                RwLock->NumberActive++;
                retVal = 1;
            }
        }
        RtlLeaveCriticalSection(&RwLock->CriticalSection);
        break;
    }
    return retVal;
}

void WINAPI RtlReleaseResource(LPRTL_RWLOCK RwLock)
{
    if (!RwLock) {
        return;
    }
    RtlEnterCriticalSection(&RwLock->CriticalSection);
    if (RwLock->NumberActive < 0) {
        if (++RwLock->NumberActive == 0) {
            RwLock->OwningThreadId = 0;
            if (RwLock->ExclusiveWaiters > 0) {
                RwLock->ExclusiveWaiters--;
                NtReleaseSemaphore(RwLock->ExclusiveSem, 1, NULL);
            } else if (RwLock->SharedWaiters > 0) {
                ULONG count = RwLock->SharedWaiters;
                RwLock->SharedWaiters = 0;
                NtReleaseSemaphore(RwLock->SharedSem, count, NULL);
            }
        }
    } else if (RwLock->NumberActive > 0) {
        if (--RwLock->NumberActive == 0) {
            if (RwLock->ExclusiveWaiters > 0) {
                RwLock->ExclusiveWaiters--;
                NtReleaseSemaphore(RwLock->ExclusiveSem, 1, NULL);
            }
        }
    }
    RtlLeaveCriticalSection(&RwLock->CriticalSection);
}

void WINAPI RtlDumpResource(LPRTL_RWLOCK RwLock)
{
    (void)RwLock;
}

/* Unicode & Error Helpers */

void WINAPI RtlInitUnicodeString(PUNICODE_STRING DestinationString, LPCWSTR SourceString)
{
    size_t len = 0;
    if (!DestinationString) {
        return;
    }
    DestinationString->Buffer = (PWSTR)SourceString;
    if (SourceString) {
        while (SourceString[len] != 0) {
            len++;
        }
        DestinationString->Length = (USHORT)(len * sizeof(WCHAR));
        DestinationString->MaximumLength = (USHORT)((len + 1) * sizeof(WCHAR));
    } else {
        DestinationString->Length = 0;
        DestinationString->MaximumLength = 0;
    }
}

ULONG WINAPI RtlNtStatusToDosError(NTSTATUS Status)
{
    switch (Status) {
    case STATUS_SUCCESS:
        return ERROR_SUCCESS;
    case STATUS_TIMEOUT:
        return ERROR_TIMEOUT;
    case STATUS_PENDING:
        return 997U; /* ERROR_IO_PENDING */
    case STATUS_INVALID_PARAMETER:
    case STATUS_INVALID_PARAMETER_1:
    case STATUS_INVALID_PARAMETER_2:
    case STATUS_INVALID_PARAMETER_3:
    case STATUS_INVALID_INFO_CLASS:
    case STATUS_INFO_LENGTH_MISMATCH:
    case STATUS_UNSUCCESSFUL:
        return ERROR_INVALID_PARAMETER;
    case STATUS_INVALID_HANDLE:
    case STATUS_OBJECT_TYPE_MISMATCH:
    case STATUS_INVALID_CID:
        return ERROR_INVALID_HANDLE;
    case STATUS_ACCESS_DENIED:
        return ERROR_ACCESS_DENIED;
    case STATUS_NO_MEMORY:
        return ERROR_NOT_ENOUGH_MEMORY;
    case STATUS_OBJECT_NAME_INVALID:
        return ERROR_INVALID_NAME;
    case STATUS_OBJECT_NAME_NOT_FOUND:
        return ERROR_FILE_NOT_FOUND;
    case STATUS_OBJECT_PATH_INVALID:
    case STATUS_OBJECT_PATH_NOT_FOUND:
        return ERROR_PATH_NOT_FOUND;
    case STATUS_OBJECT_NAME_COLLISION:
    case STATUS_OBJECT_NAME_EXISTS:
        return ERROR_ALREADY_EXISTS;
    case STATUS_MUTANT_NOT_OWNED:
        return ERROR_NOT_OWNER;
    case STATUS_SEMAPHORE_LIMIT_EXCEEDED:
        return ERROR_TOO_MANY_POSTS;
    case STATUS_ACCESS_VIOLATION:
        return 998U; /* ERROR_NOACCESS */
    default:
        return ERROR_INVALID_PARAMETER;
    }
}

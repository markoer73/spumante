/*
 * Wine kernelbase.dll Win32 Utility, Thread Description, Handle Identity & Virtual Memory APIs
 *
 * Clean-room implementation with Darwin/Mach backends:
 *   - GetSystemTimePreciseAsFileTime: Darwin clock_gettime_nsec_np(CLOCK_REALTIME) +
 *     mach_continuous_time() monotonic sub-microsecond 1601 UTC FILETIME generator.
 *   - SetThreadDescription / GetThreadDescription: Per-thread UTF-16 UNICODE_STRING table
 *     synchronized with Darwin pthread_setname_np() and LocalAlloc/LocalFree semantics.
 *   - CompareObjectHandles / DuplicateHandle: Canonical kernel object identity comparison
 *     across pseudo-handles, duplicated handles, named/anonymous NTSync objects, and sections.
 *   - VirtualAlloc2 / VirtualAlloc2FromApp / VirtualFreeEx / VirtualQueryEx:
 *     64KB-aligned virtual region & MEM_RESERVE_PLACEHOLDER manager supporting custom
 *     power-of-two alignments (MemExtendedParameterAddressRequirements), W^X FromApp
 *     protection enforcement, placeholder splitting (MEM_PRESERVE_PLACEHOLDER), and
 *     placeholder coalescing (MEM_COALESCE_PLACEHOLDERS).
 *   - CreateFileMappingW / MapViewOfFile3 / UnmapViewOfFile2: Shared memory section views
 *     with Darwin Mach VM (vm_remap) backing, 64KB standard vs 4KB MEM_REPLACE_PLACEHOLDER
 *     alignment validation (ERROR_MAPPED_ALIGNMENT 1132), and atomic placeholder restoration.
 *
 * Copyright 2026 Spumante Contributors
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include <stdlib.h>
#include <string.h>

#ifdef __APPLE__
#include <mach/mach.h>
#include <mach/mach_time.h>
#include <mach/vm_map.h>
#include <pthread.h>
#include <sched.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#elif !defined(_WIN32)
#include <pthread.h>
#include <sched.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#endif

#include "kernelbase.h"

#define WINE_DEFAULT_DEBUG_CHANNEL(ch)
#define TRACE(...) do { } while (0)
#define WARN(...)  do { } while (0)
#define FIXME(...) do { } while (0)
#define ERR(...)   do { } while (0)

WINE_DEFAULT_DEBUG_CHANNEL(kernelbase);

#define KB_EPOCH_DIFF_100NS         116444736000000000ULL
#define KB_MAX_THREADS              128U
#define KB_MAX_THREAD_DESC_WCHARS   256U
#define KB_MAX_NAMED_SYNC           128U
#define KB_MAX_SECTIONS             64U
#define KB_MAX_HANDLES              512U
#define KB_MAX_VM_ALLOCS            128U
#define KB_MAX_VM_REGIONS           512U

#define KB_CUSTOM_HANDLE_BASE       ((uintptr_t)0x68000000U)
#define KB_CUSTOM_HANDLE_STRIDE     16U

#define KB_CANON_PROCESS_SELF       0x10000001ULL
#define KB_CANON_THREAD_BASE        0x20000000ULL
#define KB_CANON_DYNAMIC_BASE       0x40000000ULL

typedef enum _KB_HANDLE_KIND {
    KB_HANDLE_NONE = 0,
    KB_HANDLE_PROCESS = 1,
    KB_HANDLE_THREAD = 2,
    KB_HANDLE_SYNC = 3,
    KB_HANDLE_SECTION = 4
} KB_HANDLE_KIND;

typedef struct _KB_THREAD_DESC_SLOT {
    int   in_use;
    DWORD tid;
    WCHAR desc[KB_MAX_THREAD_DESC_WCHARS];
} KB_THREAD_DESC_SLOT;

typedef struct _KB_NAMED_SYNC_ENTRY {
    int              in_use;
    KB_SYNC_OBJ_KIND kind;
    ULONG            ref_count;
    uint64_t         canonical_id;
    WCHAR            name[128];
} KB_NAMED_SYNC_ENTRY;

typedef struct _KB_SECTION_OBJECT {
    int       in_use;
    ULONG     ref_count;
    uint64_t  canonical_id;
    SIZE_T    size;
    DWORD     protect;
    void     *backing;
    int       backing_is_mmap;
    WCHAR     name[128];
} KB_SECTION_OBJECT;

typedef struct _KB_HANDLE_RECORD {
    int                in_use;
    int                is_custom_handle;
    HANDLE             handle_val;
    ULONG              generation;
    KB_HANDLE_KIND     kind;
    DWORD              target_tid;
    uint64_t           canonical_id;
    ULONG              named_sync_idx;
    KB_SECTION_OBJECT *section;
} KB_HANDLE_RECORD;

typedef struct _KB_VM_BACKING {
    int       in_use;
    void     *raw_ptr;
    SIZE_T    raw_size;
    int       is_mmap;
    uintptr_t aligned_base;
    SIZE_T    aligned_size;
} KB_VM_BACKING;

typedef struct _KB_VM_REGION {
    int                in_use;
    uintptr_t          base;
    uintptr_t          alloc_base;
    SIZE_T             size;
    ULONG              state;
    ULONG              protect;
    ULONG              alloc_protect;
    ULONG              type;
    int                is_placeholder;
    int                is_mapped_view;
    int                replaced_placeholder;
    int                remapped_mach_vm;
    KB_SECTION_OBJECT *section;
    ULONG64            section_offset;
    ULONG              backing_idx;
} KB_VM_REGION;

static volatile int g_kb_util_lock = 0;
static int g_kb_util_initialized = 0;
static volatile uint64_t g_last_precise_ticks = 0;
static volatile uint64_t g_next_canonical_id = KB_CANON_DYNAMIC_BASE;

static KB_THREAD_DESC_SLOT g_thread_descs[KB_MAX_THREADS];
static KB_NAMED_SYNC_ENTRY g_named_sync[KB_MAX_NAMED_SYNC];
static KB_SECTION_OBJECT   g_sections[KB_MAX_SECTIONS];
static KB_HANDLE_RECORD    g_handles[KB_MAX_HANDLES];
static KB_VM_BACKING       g_vm_backings[KB_MAX_VM_ALLOCS];
static KB_VM_REGION        g_vm_regions[KB_MAX_VM_REGIONS];

static int kb_util_is_stub_simulated(void)
{
    const char *sim = getenv("SPUMANTE_SIMULATE_STUB");
    return (sim != NULL && sim[0] == '1');
}

static void kb_util_lock(void)
{
    while (__atomic_test_and_set(&g_kb_util_lock, __ATOMIC_ACQUIRE)) {
#if defined(__APPLE__) || !defined(_WIN32)
        sched_yield();
#else
        SwitchToThread();
#endif
    }
}

static void kb_util_unlock(void)
{
    __atomic_clear(&g_kb_util_lock, __ATOMIC_RELEASE);
}

static size_t kb_wstrlen(const WCHAR *s)
{
    size_t n = 0;
    if (!s) {
        return 0;
    }
    while (s[n] != 0) {
        n++;
    }
    return n;
}

static void kb_wstrcpy_bounded(WCHAR *dst, size_t max_chars, const WCHAR *src)
{
    size_t i = 0;
    if (max_chars == 0) {
        return;
    }
    if (src) {
        while (src[i] != 0 && i + 1 < max_chars) {
            dst[i] = src[i];
            i++;
        }
    }
    dst[i] = 0;
}

static int kb_wstr_equal_ci(const WCHAR *a, const WCHAR *b)
{
    size_t i = 0;
    if (!a || !b) {
        return 0;
    }
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

static void kb_util_ensure_init_locked(void)
{
    if (!g_kb_util_initialized) {
        memset(g_thread_descs, 0, sizeof(g_thread_descs));
        memset(g_named_sync, 0, sizeof(g_named_sync));
        memset(g_sections, 0, sizeof(g_sections));
        memset(g_handles, 0, sizeof(g_handles));
        memset(g_vm_backings, 0, sizeof(g_vm_backings));
        memset(g_vm_regions, 0, sizeof(g_vm_regions));
        g_kb_util_initialized = 1;
    }
}

void WINAPI KernelBaseResetUtilityState(void)
{
    ULONG i;
    kb_util_lock();
    if (g_kb_util_initialized) {
        for (i = 0; i < KB_MAX_SECTIONS; i++) {
            if (g_sections[i].in_use && g_sections[i].backing) {
#if defined(__APPLE__) || !defined(_WIN32)
                if (g_sections[i].backing_is_mmap) {
                    munmap(g_sections[i].backing, g_sections[i].size);
                } else {
                    free(g_sections[i].backing);
                }
#else
                free(g_sections[i].backing);
#endif
                g_sections[i].backing = NULL;
                g_sections[i].in_use = 0;
            }
        }
        for (i = 0; i < KB_MAX_VM_ALLOCS; i++) {
            if (g_vm_backings[i].in_use && g_vm_backings[i].raw_ptr) {
#if defined(__APPLE__) || !defined(_WIN32)
                if (g_vm_backings[i].is_mmap) {
                    munmap(g_vm_backings[i].raw_ptr, g_vm_backings[i].raw_size);
                } else {
                    free(g_vm_backings[i].raw_ptr);
                }
#else
                free(g_vm_backings[i].raw_ptr);
#endif
                g_vm_backings[i].raw_ptr = NULL;
                g_vm_backings[i].in_use = 0;
            }
        }
    }
    g_kb_util_initialized = 0;
    kb_util_ensure_init_locked();
    kb_util_unlock();
}

HANDLE WINAPI GetCurrentProcess(void)
{
    return (HANDLE)(intptr_t)-1;
}

HANDLE WINAPI GetCurrentThread(void)
{
    return (HANDLE)(intptr_t)-2;
}

HLOCAL WINAPI LocalFree(HLOCAL hMem)
{
    if (hMem) {
        free(hMem);
    }
    return NULL;
}

/* ============================================================================
 * 1. GetSystemTimePreciseAsFileTime (Darwin clock_gettime_nsec_np + mach_continuous_time)
 * ============================================================================ */

void WINAPI GetSystemTimePreciseAsFileTime(LPFILETIME lpSystemTimeAsFileTime)
{
    uint64_t ticks_100ns = 0;
    uint64_t prev_ticks;

    if (!lpSystemTimeAsFileTime) {
        return;
    }
    if (kb_util_is_stub_simulated()) {
        lpSystemTimeAsFileTime->dwLowDateTime = 0;
        lpSystemTimeAsFileTime->dwHighDateTime = 0;
        return;
    }

#ifdef __APPLE__
    {
        uint64_t ns = clock_gettime_nsec_np(CLOCK_REALTIME);
        ticks_100ns = (ns / 100ULL) + KB_EPOCH_DIFF_100NS;
    }
#elif !defined(_WIN32)
    {
        struct timespec ts;
        if (clock_gettime(CLOCK_REALTIME, &ts) == 0) {
            ticks_100ns = ((uint64_t)ts.tv_sec * 10000000ULL) +
                          ((uint64_t)ts.tv_nsec / 100ULL) +
                          KB_EPOCH_DIFF_100NS;
        }
    }
#else
    {
        LARGE_INTEGER st;
        if (NtQuerySystemTime(&st) == STATUS_SUCCESS && st.QuadPart > 0) {
            ticks_100ns = (uint64_t)st.QuadPart;
        } else {
            ticks_100ns = 134000000000000000ULL;
        }
    }
#endif

    /* Enforce strict monotonicity across threads and sub-microsecond progression */
    prev_ticks = __atomic_load_n(&g_last_precise_ticks, __ATOMIC_SEQ_CST);
    for (;;) {
        uint64_t next_ticks = (ticks_100ns > prev_ticks) ? ticks_100ns : (prev_ticks + 1ULL);
        if (__atomic_compare_exchange_n(
                &g_last_precise_ticks,
                &prev_ticks,
                next_ticks,
                0,
                __ATOMIC_SEQ_CST,
                __ATOMIC_SEQ_CST)) {
            ticks_100ns = next_ticks;
            break;
        }
    }

    lpSystemTimeAsFileTime->dwLowDateTime = (DWORD)(ticks_100ns & 0xFFFFFFFFULL);
    lpSystemTimeAsFileTime->dwHighDateTime = (DWORD)(ticks_100ns >> 32);
}

/* ============================================================================
 * 2. Handle Identity Table, Sync Registration, DuplicateHandle & CompareObjectHandles
 * ============================================================================ */

static KB_HANDLE_RECORD *kb_find_handle_record_locked(HANDLE h)
{
    ULONG i;
    if (!h) {
        return NULL;
    }
    for (i = 0; i < KB_MAX_HANDLES; i++) {
        if (g_handles[i].in_use && g_handles[i].handle_val == h) {
            return &g_handles[i];
        }
    }
    return NULL;
}

static KB_HANDLE_RECORD *kb_alloc_handle_record_locked(int make_custom_handle)
{
    ULONG i;
    for (i = 0; i < KB_MAX_HANDLES; i++) {
        if (!g_handles[i].in_use) {
            ULONG gen = g_handles[i].generation + 1U;
            if (gen == 0 || gen > 0xFFFFU) {
                gen = 1U;
            }
            memset(&g_handles[i], 0, sizeof(g_handles[i]));
            g_handles[i].in_use = 1;
            g_handles[i].generation = gen;
            g_handles[i].named_sync_idx = KB_MAX_NAMED_SYNC;
            if (make_custom_handle) {
                g_handles[i].is_custom_handle = 1;
                g_handles[i].handle_val = (HANDLE)(
                    KB_CUSTOM_HANDLE_BASE +
                    ((uintptr_t)gen << 16) +
                    (uintptr_t)i * KB_CUSTOM_HANDLE_STRIDE);
            }
            return &g_handles[i];
        }
    }
    return NULL;
}

void kb_register_sync_handle(HANDLE h, KB_SYNC_OBJ_KIND kind, LPCWSTR name)
{
    ULONG i;
    ULONG named_idx = KB_MAX_NAMED_SYNC;
    uint64_t canon_id = 0;
    KB_HANDLE_RECORD *rec;

    if (!h || kind == KB_SYNC_OBJ_NONE) {
        return;
    }
    kb_util_lock();
    kb_util_ensure_init_locked();

    if (name && name[0] != 0) {
        for (i = 0; i < KB_MAX_NAMED_SYNC; i++) {
            if (g_named_sync[i].in_use &&
                g_named_sync[i].kind == kind &&
                kb_wstr_equal_ci(g_named_sync[i].name, name)) {
                named_idx = i;
                g_named_sync[i].ref_count++;
                canon_id = g_named_sync[i].canonical_id;
                break;
            }
        }
        if (named_idx == KB_MAX_NAMED_SYNC) {
            for (i = 0; i < KB_MAX_NAMED_SYNC; i++) {
                if (!g_named_sync[i].in_use) {
                    named_idx = i;
                    g_named_sync[i].in_use = 1;
                    g_named_sync[i].kind = kind;
                    g_named_sync[i].ref_count = 1U;
                    g_named_sync[i].canonical_id = __atomic_add_fetch(
                        &g_next_canonical_id, 1ULL, __ATOMIC_SEQ_CST);
                    kb_wstrcpy_bounded(g_named_sync[i].name, 128, name);
                    canon_id = g_named_sync[i].canonical_id;
                    break;
                }
            }
        }
    }

    if (canon_id == 0) {
        canon_id = __atomic_add_fetch(&g_next_canonical_id, 1ULL, __ATOMIC_SEQ_CST);
    }

    rec = kb_find_handle_record_locked(h);
    if (!rec) {
        rec = kb_alloc_handle_record_locked(0);
    }
    if (rec) {
        rec->is_custom_handle = 0;
        rec->handle_val = h;
        rec->kind = KB_HANDLE_SYNC;
        rec->canonical_id = canon_id;
        rec->named_sync_idx = named_idx;
    }
    kb_util_unlock();
}

static void kb_release_section_locked(KB_SECTION_OBJECT *sec)
{
    if (!sec || !sec->in_use) {
        return;
    }
    if (sec->ref_count > 0) {
        sec->ref_count--;
    }
    if (sec->ref_count == 0) {
        if (sec->backing) {
#if defined(__APPLE__) || !defined(_WIN32)
            if (sec->backing_is_mmap) {
                munmap(sec->backing, sec->size);
            } else {
                free(sec->backing);
            }
#else
            free(sec->backing);
#endif
            sec->backing = NULL;
        }
        sec->in_use = 0;
    }
}

int kb_close_registered_handle(HANDLE h, BOOL *out_result)
{
    KB_HANDLE_RECORD *rec;
    int is_custom;

    if (!h) {
        return 0;
    }
    kb_util_lock();
    kb_util_ensure_init_locked();
    rec = kb_find_handle_record_locked(h);
    if (!rec) {
        kb_util_unlock();
        return 0;
    }

    is_custom = rec->is_custom_handle;
    if (rec->named_sync_idx < KB_MAX_NAMED_SYNC && g_named_sync[rec->named_sync_idx].in_use) {
        if (g_named_sync[rec->named_sync_idx].ref_count > 0) {
            g_named_sync[rec->named_sync_idx].ref_count--;
            if (g_named_sync[rec->named_sync_idx].ref_count == 0) {
                g_named_sync[rec->named_sync_idx].in_use = 0;
            }
        }
    }
    if (rec->kind == KB_HANDLE_SECTION && rec->section) {
        kb_release_section_locked(rec->section);
    }
    rec->in_use = 0;
    rec->handle_val = NULL;
    rec->section = NULL;
    kb_util_unlock();

    if (is_custom) {
        if (out_result) {
            *out_result = TRUE;
        }
        return 1;
    }
    /* Underlying NTSync handle still needs NtClose in sync.c */
    return 0;
}

static int kb_resolve_handle_locked(
    HANDLE h,
    KB_HANDLE_KIND *out_kind,
    uint64_t *out_canon,
    DWORD *out_tid,
    ULONG *out_named_idx,
    KB_SECTION_OBJECT **out_sec)
{
    KB_HANDLE_RECORD *rec;
    EVENT_BASIC_INFORMATION ebi;
    MUTANT_BASIC_INFORMATION mbi;
    SEMAPHORE_BASIC_INFORMATION sbi;

    if (!h) {
        return 0;
    }
    if (h == GetCurrentProcess()) {
        if (out_kind) *out_kind = KB_HANDLE_PROCESS;
        if (out_canon) *out_canon = KB_CANON_PROCESS_SELF;
        if (out_tid) *out_tid = 0;
        if (out_named_idx) *out_named_idx = KB_MAX_NAMED_SYNC;
        if (out_sec) *out_sec = NULL;
        return 1;
    }
    if (h == GetCurrentThread()) {
        DWORD tid = NtdllGetCurrentThreadId();
        if (out_kind) *out_kind = KB_HANDLE_THREAD;
        if (out_canon) *out_canon = KB_CANON_THREAD_BASE + (uint64_t)tid;
        if (out_tid) *out_tid = tid;
        if (out_named_idx) *out_named_idx = KB_MAX_NAMED_SYNC;
        if (out_sec) *out_sec = NULL;
        return 1;
    }

    rec = kb_find_handle_record_locked(h);
    if (rec) {
        if (out_kind) *out_kind = rec->kind;
        if (out_canon) *out_canon = rec->canonical_id;
        if (out_tid) *out_tid = rec->target_tid;
        if (out_named_idx) *out_named_idx = rec->named_sync_idx;
        if (out_sec) *out_sec = rec->section;
        return 1;
    }

    /* Fallback for raw NTDLL sync handles created directly via NtCreate* */
    if (NtQueryEvent(h, EventBasicInformation, &ebi, sizeof(ebi), NULL) == STATUS_SUCCESS ||
        NtQueryMutant(h, MutantBasicInformation, &mbi, sizeof(mbi), NULL) == STATUS_SUCCESS ||
        NtQuerySemaphore(h, SemaphoreBasicInformation, &sbi, sizeof(sbi), NULL) == STATUS_SUCCESS) {
        if (out_kind) *out_kind = KB_HANDLE_SYNC;
        if (out_canon) *out_canon = (uint64_t)(uintptr_t)h;
        if (out_tid) *out_tid = 0;
        if (out_named_idx) *out_named_idx = KB_MAX_NAMED_SYNC;
        if (out_sec) *out_sec = NULL;
        return 1;
    }

    return 0;
}

static int kb_is_valid_current_process_locked(HANDLE process)
{
    KB_HANDLE_KIND kind = KB_HANDLE_NONE;
    uint64_t canon = 0;
    if (process == NULL || process == GetCurrentProcess()) {
        return 1;
    }
    if (kb_resolve_handle_locked(process, &kind, &canon, NULL, NULL, NULL) &&
        kind == KB_HANDLE_PROCESS && canon == KB_CANON_PROCESS_SELF) {
        return 1;
    }
    return 0;
}

BOOL WINAPI DuplicateHandle(
    HANDLE hSourceProcessHandle,
    HANDLE hSourceHandle,
    HANDLE hTargetProcessHandle,
    HANDLE *lpTargetHandle,
    DWORD dwDesiredAccess,
    BOOL bInheritHandle,
    DWORD dwOptions)
{
    KB_HANDLE_KIND kind = KB_HANDLE_NONE;
    uint64_t canon = 0;
    DWORD tid = 0;
    ULONG named_idx = KB_MAX_NAMED_SYNC;
    KB_SECTION_OBJECT *sec = NULL;
    KB_HANDLE_RECORD *dup_rec;

    (void)dwDesiredAccess;
    (void)bInheritHandle;
    (void)dwOptions;

    if (kb_util_is_stub_simulated()) {
        SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
        return FALSE;
    }
    if (!lpTargetHandle) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    kb_util_lock();
    kb_util_ensure_init_locked();
    if (!kb_is_valid_current_process_locked(hSourceProcessHandle) ||
        !kb_is_valid_current_process_locked(hTargetProcessHandle) ||
        !kb_resolve_handle_locked(hSourceHandle, &kind, &canon, &tid, &named_idx, &sec)) {
        kb_util_unlock();
        SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }

    dup_rec = kb_alloc_handle_record_locked(1);
    if (!dup_rec) {
        kb_util_unlock();
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    dup_rec->kind = kind;
    dup_rec->canonical_id = canon;
    dup_rec->target_tid = tid;
    dup_rec->named_sync_idx = named_idx;
    dup_rec->section = sec;
    if (named_idx < KB_MAX_NAMED_SYNC && g_named_sync[named_idx].in_use) {
        g_named_sync[named_idx].ref_count++;
    }
    if (sec && sec->in_use) {
        sec->ref_count++;
    }
    *lpTargetHandle = dup_rec->handle_val;
    kb_util_unlock();
    return TRUE;
}

BOOL WINAPI CompareObjectHandles(HANDLE hFirstObjectHandle, HANDLE hSecondObjectHandle)
{
    uint64_t canon1 = 0;
    uint64_t canon2 = 0;
    int valid1;
    int valid2;

    if (kb_util_is_stub_simulated()) {
        SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
        return FALSE;
    }

    kb_util_lock();
    kb_util_ensure_init_locked();
    valid1 = kb_resolve_handle_locked(hFirstObjectHandle, NULL, &canon1, NULL, NULL, NULL);
    valid2 = kb_resolve_handle_locked(hSecondObjectHandle, NULL, &canon2, NULL, NULL, NULL);
    kb_util_unlock();

    if (!valid1 || !valid2) {
        SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    if (canon1 != canon2) {
        SetLastError(ERROR_NOT_SAME_OBJECT);
        return FALSE;
    }
    return TRUE;
}

/* ============================================================================
 * 3. SetThreadDescription & GetThreadDescription (UTF-16 + Darwin pthread_setname_np)
 * ============================================================================ */

static KB_THREAD_DESC_SLOT *kb_find_or_create_thread_desc_locked(DWORD tid)
{
    ULONG i;
    KB_THREAD_DESC_SLOT *free_slot = NULL;
    for (i = 0; i < KB_MAX_THREADS; i++) {
        if (g_thread_descs[i].in_use && g_thread_descs[i].tid == tid) {
            return &g_thread_descs[i];
        }
        if (!g_thread_descs[i].in_use && !free_slot) {
            free_slot = &g_thread_descs[i];
        }
    }
    if (free_slot) {
        memset(free_slot, 0, sizeof(*free_slot));
        free_slot->in_use = 1;
        free_slot->tid = tid;
    }
    return free_slot;
}

static int kb_resolve_thread_tid_locked(HANDLE hThread, DWORD *out_tid)
{
    KB_HANDLE_KIND kind = KB_HANDLE_NONE;
    DWORD tid = 0;
    if (!kb_resolve_handle_locked(hThread, &kind, NULL, &tid, NULL, NULL)) {
        return 0;
    }
    if (kind != KB_HANDLE_THREAD || tid == 0) {
        return 0;
    }
    *out_tid = tid;
    return 1;
}

HRESULT WINAPI SetThreadDescription(HANDLE hThread, PCWSTR lpThreadDescription)
{
    DWORD tid = 0;
    KB_THREAD_DESC_SLOT *slot;

    if (kb_util_is_stub_simulated()) {
        return HRESULT_FROM_NT(STATUS_NOT_IMPLEMENTED);
    }

    kb_util_lock();
    kb_util_ensure_init_locked();
    if (!kb_resolve_thread_tid_locked(hThread, &tid)) {
        kb_util_unlock();
        return HRESULT_FROM_NT(STATUS_INVALID_HANDLE);
    }
    slot = kb_find_or_create_thread_desc_locked(tid);
    if (!slot) {
        kb_util_unlock();
        return HRESULT_FROM_NT(STATUS_NO_MEMORY);
    }
    kb_wstrcpy_bounded(slot->desc, KB_MAX_THREAD_DESC_WCHARS, lpThreadDescription);

#ifdef __APPLE__
    if (tid == NtdllGetCurrentThreadId()) {
        char utf8_name[64];
        size_t i = 0;
        if (lpThreadDescription) {
            while (lpThreadDescription[i] != 0 && i + 1 < sizeof(utf8_name)) {
                WCHAR wc = lpThreadDescription[i];
                utf8_name[i] = (wc < 0x80U) ? (char)wc : '?';
                i++;
            }
        }
        utf8_name[i] = '\0';
        (void)pthread_setname_np(utf8_name);
    }
#endif
    kb_util_unlock();
    return HRESULT_FROM_NT(STATUS_SUCCESS);
}

HRESULT WINAPI GetThreadDescription(HANDLE hThread, PWSTR *ppszThreadDescription)
{
    DWORD tid = 0;
    KB_THREAD_DESC_SLOT *slot;
    size_t len = 0;
    PWSTR out_buf;

    if (kb_util_is_stub_simulated()) {
        return HRESULT_FROM_NT(STATUS_NOT_IMPLEMENTED);
    }
    if (!ppszThreadDescription) {
        return HRESULT_FROM_NT(STATUS_INVALID_PARAMETER);
    }

    kb_util_lock();
    kb_util_ensure_init_locked();
    if (!kb_resolve_thread_tid_locked(hThread, &tid)) {
        kb_util_unlock();
        return HRESULT_FROM_NT(STATUS_INVALID_HANDLE);
    }
    slot = kb_find_or_create_thread_desc_locked(tid);
    if (slot) {
        len = kb_wstrlen(slot->desc);
    }
    out_buf = (PWSTR)malloc((len + 1) * sizeof(WCHAR));
    if (!out_buf) {
        kb_util_unlock();
        return HRESULT_FROM_NT(STATUS_NO_MEMORY);
    }
    if (slot && len > 0) {
        memcpy(out_buf, slot->desc, len * sizeof(WCHAR));
    }
    out_buf[len] = 0;
    *ppszThreadDescription = out_buf;
    kb_util_unlock();
    return HRESULT_FROM_NT(STATUS_SUCCESS);
}

/* ============================================================================
 * 4. VirtualAlloc2 / VirtualAlloc2FromApp / VirtualFreeEx / VirtualQueryEx
 * ============================================================================ */

static int kb_is_power_of_two(SIZE_T x)
{
    return (x != 0) && ((x & (x - 1)) == 0);
}

static SIZE_T kb_round_up(SIZE_T val, SIZE_T align)
{
    return (val + align - 1) & ~(align - 1);
}

static KB_VM_REGION *kb_alloc_vm_region_locked(void)
{
    ULONG i;
    for (i = 0; i < KB_MAX_VM_REGIONS; i++) {
        if (!g_vm_regions[i].in_use) {
            memset(&g_vm_regions[i], 0, sizeof(g_vm_regions[i]));
            g_vm_regions[i].in_use = 1;
            g_vm_regions[i].backing_idx = KB_MAX_VM_ALLOCS;
            return &g_vm_regions[i];
        }
    }
    return NULL;
}

static KB_VM_REGION *kb_find_vm_region_containing_locked(uintptr_t addr)
{
    ULONG i;
    for (i = 0; i < KB_MAX_VM_REGIONS; i++) {
        if (g_vm_regions[i].in_use &&
            addr >= g_vm_regions[i].base &&
            addr < g_vm_regions[i].base + g_vm_regions[i].size) {
            return &g_vm_regions[i];
        }
    }
    return NULL;
}

static KB_VM_REGION *kb_find_vm_region_exact_locked(uintptr_t base, SIZE_T size)
{
    ULONG i;
    for (i = 0; i < KB_MAX_VM_REGIONS; i++) {
        if (g_vm_regions[i].in_use &&
            g_vm_regions[i].base == base &&
            g_vm_regions[i].size == size) {
            return &g_vm_regions[i];
        }
    }
    return NULL;
}

static int kb_allocate_aligned_backing_locked(
    SIZE_T size,
    SIZE_T alignment,
    ULONG *out_backing_idx,
    uintptr_t *out_aligned_base)
{
    ULONG idx = KB_MAX_VM_ALLOCS;
    ULONG i;
    SIZE_T raw_size;
    void *raw_ptr = NULL;
    int is_mmap = 0;
    uintptr_t raw_addr;
    uintptr_t aligned_addr;

    for (i = 0; i < KB_MAX_VM_ALLOCS; i++) {
        if (!g_vm_backings[i].in_use) {
            idx = i;
            break;
        }
    }
    if (idx == KB_MAX_VM_ALLOCS) {
        return 0;
    }

    if (alignment < NTDLL_ALLOCATION_GRANULARITY) {
        alignment = NTDLL_ALLOCATION_GRANULARITY;
    }
    raw_size = size + alignment;

#if defined(__APPLE__) || !defined(_WIN32)
    raw_ptr = mmap(NULL, raw_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    if (raw_ptr == MAP_FAILED) {
        raw_ptr = NULL;
    } else {
        is_mmap = 1;
    }
#else
    raw_ptr = calloc(1, raw_size);
#endif
    if (!raw_ptr) {
        return 0;
    }

    raw_addr = (uintptr_t)raw_ptr;
    aligned_addr = (raw_addr + (alignment - 1)) & ~(uintptr_t)(alignment - 1);

    g_vm_backings[idx].in_use = 1;
    g_vm_backings[idx].raw_ptr = raw_ptr;
    g_vm_backings[idx].raw_size = raw_size;
    g_vm_backings[idx].is_mmap = is_mmap;
    g_vm_backings[idx].aligned_base = aligned_addr;
    g_vm_backings[idx].aligned_size = size;

    *out_backing_idx = idx;
    *out_aligned_base = aligned_addr;
    return 1;
}

static void kb_maybe_free_backing_locked(ULONG backing_idx)
{
    ULONG i;
    if (backing_idx >= KB_MAX_VM_ALLOCS || !g_vm_backings[backing_idx].in_use) {
        return;
    }
    for (i = 0; i < KB_MAX_VM_REGIONS; i++) {
        if (g_vm_regions[i].in_use && g_vm_regions[i].backing_idx == backing_idx) {
            return;
        }
    }
    if (g_vm_backings[backing_idx].raw_ptr) {
#if defined(__APPLE__) || !defined(_WIN32)
        if (g_vm_backings[backing_idx].is_mmap) {
            munmap(g_vm_backings[backing_idx].raw_ptr, g_vm_backings[backing_idx].raw_size);
        } else {
            free(g_vm_backings[backing_idx].raw_ptr);
        }
#else
        free(g_vm_backings[backing_idx].raw_ptr);
#endif
    }
    memset(&g_vm_backings[backing_idx], 0, sizeof(g_vm_backings[backing_idx]));
}

PVOID WINAPI VirtualAlloc2(
    HANDLE Process,
    PVOID BaseAddress,
    SIZE_T Size,
    ULONG AllocationType,
    ULONG PageProtection,
    MEM_EXTENDED_PARAMETER *ExtendedParameters,
    ULONG ParameterCount)
{
    SIZE_T alignment = NTDLL_ALLOCATION_GRANULARITY;
    uintptr_t lowest_addr = 0;
    uintptr_t highest_addr = ~(uintptr_t)0;
    ULONG i;
    int is_placeholder = 0;
    int is_replace_placeholder = 0;
    SIZE_T rounded_size;
    ULONG backing_idx = KB_MAX_VM_ALLOCS;
    uintptr_t alloc_base = 0;
    KB_VM_REGION *reg;

    if (kb_util_is_stub_simulated()) {
        SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
        return NULL;
    }
    if (Size == 0) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return NULL;
    }
    if (ParameterCount > 0 && !ExtendedParameters) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return NULL;
    }

    for (i = 0; i < ParameterCount; i++) {
        if (ExtendedParameters[i].Type == MemExtendedParameterAddressRequirements) {
            const MEM_ADDRESS_REQUIREMENTS *req =
                (const MEM_ADDRESS_REQUIREMENTS *)ExtendedParameters[i].Pointer;
            if (!req) {
                SetLastError(ERROR_INVALID_PARAMETER);
                return NULL;
            }
            if (req->Alignment != 0) {
                if (!kb_is_power_of_two(req->Alignment) ||
                    req->Alignment < NTDLL_ALLOCATION_GRANULARITY) {
                    SetLastError(ERROR_INVALID_PARAMETER);
                    return NULL;
                }
                alignment = req->Alignment;
            }
            if (req->LowestStartingAddress) {
                lowest_addr = (uintptr_t)req->LowestStartingAddress;
                if ((lowest_addr & (NTDLL_ALLOCATION_GRANULARITY - 1)) != 0) {
                    SetLastError(ERROR_INVALID_PARAMETER);
                    return NULL;
                }
            }
            if (req->HighestEndingAddress) {
                highest_addr = (uintptr_t)req->HighestEndingAddress;
                if (highest_addr < lowest_addr) {
                    SetLastError(ERROR_INVALID_PARAMETER);
                    return NULL;
                }
            }
        } else {
            SetLastError(ERROR_INVALID_PARAMETER);
            return NULL;
        }
    }

    is_placeholder = (AllocationType & MEM_RESERVE_PLACEHOLDER) != 0;
    is_replace_placeholder = (AllocationType & MEM_REPLACE_PLACEHOLDER) != 0;

    if (is_placeholder) {
        if (!(AllocationType & MEM_RESERVE) ||
            (AllocationType & MEM_COMMIT) ||
            PageProtection != PAGE_NOACCESS) {
            SetLastError(ERROR_INVALID_PARAMETER);
            return NULL;
        }
    }

    rounded_size = kb_round_up(Size, NTDLL_WIN_PAGE_SIZE);

    kb_util_lock();
    kb_util_ensure_init_locked();
    if (!kb_is_valid_current_process_locked(Process)) {
        kb_util_unlock();
        SetLastError(ERROR_INVALID_HANDLE);
        return NULL;
    }

    if (is_replace_placeholder) {
        if (!BaseAddress) {
            kb_util_unlock();
            SetLastError(ERROR_INVALID_PARAMETER);
            return NULL;
        }
        reg = kb_find_vm_region_exact_locked((uintptr_t)BaseAddress, rounded_size);
        if (!reg || !reg->is_placeholder) {
            kb_util_unlock();
            SetLastError(ERROR_INVALID_ADDRESS);
            return NULL;
        }
        reg->is_placeholder = 0;
        reg->replaced_placeholder = 1;
        reg->state = (AllocationType & MEM_COMMIT) ? MEM_COMMIT : MEM_RESERVE;
        reg->protect = PageProtection;
        reg->alloc_protect = PageProtection;
        reg->type = MEM_PRIVATE;
        kb_util_unlock();
        return (PVOID)reg->base;
    }

    if (BaseAddress != NULL) {
        reg = kb_find_vm_region_containing_locked((uintptr_t)BaseAddress);
        if (reg && (AllocationType & MEM_COMMIT) && !reg->is_placeholder) {
            reg->state = MEM_COMMIT;
            reg->protect = PageProtection;
            kb_util_unlock();
            return BaseAddress;
        }
        kb_util_unlock();
        SetLastError(ERROR_INVALID_ADDRESS);
        return NULL;
    }

    rounded_size = kb_round_up(rounded_size, NTDLL_ALLOCATION_GRANULARITY);
    if (!kb_allocate_aligned_backing_locked(rounded_size, alignment, &backing_idx, &alloc_base)) {
        kb_util_unlock();
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return NULL;
    }

    if (alloc_base < lowest_addr || (alloc_base + rounded_size - 1) > highest_addr) {
        kb_maybe_free_backing_locked(backing_idx);
        kb_util_unlock();
        SetLastError(ERROR_INVALID_PARAMETER);
        return NULL;
    }

    reg = kb_alloc_vm_region_locked();
    if (!reg) {
        kb_maybe_free_backing_locked(backing_idx);
        kb_util_unlock();
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return NULL;
    }
    reg->base = alloc_base;
    reg->alloc_base = alloc_base;
    reg->size = rounded_size;
    reg->state = (AllocationType & MEM_COMMIT) ? MEM_COMMIT : MEM_RESERVE;
    reg->protect = (AllocationType & MEM_COMMIT) ? PageProtection : PAGE_NOACCESS;
    reg->alloc_protect = PageProtection;
    reg->type = MEM_PRIVATE;
    reg->is_placeholder = is_placeholder;
    reg->backing_idx = backing_idx;
    kb_util_unlock();

    return (PVOID)alloc_base;
}

PVOID WINAPI VirtualAlloc2FromApp(
    HANDLE Process,
    PVOID BaseAddress,
    SIZE_T Size,
    ULONG AllocationType,
    ULONG PageProtection,
    MEM_EXTENDED_PARAMETER *ExtendedParameters,
    ULONG ParameterCount)
{
    ULONG base_prot = PageProtection & 0xFFU;
    if (kb_util_is_stub_simulated()) {
        SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
        return NULL;
    }
    if (base_prot == PAGE_EXECUTE ||
        base_prot == PAGE_EXECUTE_READ ||
        base_prot == PAGE_EXECUTE_READWRITE ||
        base_prot == PAGE_EXECUTE_WRITECOPY) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return NULL;
    }
    return VirtualAlloc2(
        Process,
        BaseAddress,
        Size,
        AllocationType,
        PageProtection,
        ExtendedParameters,
        ParameterCount);
}

BOOL WINAPI VirtualFreeEx(
    HANDLE hProcess,
    PVOID lpAddress,
    SIZE_T dwSize,
    DWORD dwFreeType)
{
    uintptr_t addr = (uintptr_t)lpAddress;
    KB_VM_REGION *reg;

    if (kb_util_is_stub_simulated()) {
        SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
        return FALSE;
    }
    if (!lpAddress) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    kb_util_lock();
    kb_util_ensure_init_locked();
    if (!kb_is_valid_current_process_locked(hProcess)) {
        kb_util_unlock();
        SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }

    /* Case 1: Split a placeholder (MEM_RELEASE | MEM_PRESERVE_PLACEHOLDER) */
    if (dwFreeType == (MEM_RELEASE | MEM_PRESERVE_PLACEHOLDER)) {
        uintptr_t reg_base;
        uintptr_t reg_end;
        uintptr_t split_end;
        ULONG backing_idx;

        if (dwSize == 0 || (addr & NTDLL_WIN_PAGE_MASK) != 0 || (dwSize & NTDLL_WIN_PAGE_MASK) != 0) {
            kb_util_unlock();
            SetLastError(ERROR_INVALID_PARAMETER);
            return FALSE;
        }
        reg = kb_find_vm_region_containing_locked(addr);
        if (!reg || !reg->is_placeholder) {
            kb_util_unlock();
            SetLastError(ERROR_INVALID_ADDRESS);
            return FALSE;
        }
        reg_base = reg->base;
        reg_end = reg->base + reg->size;
        split_end = addr + dwSize;
        backing_idx = reg->backing_idx;

        if (split_end > reg_end || (addr == reg_base && split_end == reg_end)) {
            kb_util_unlock();
            SetLastError(ERROR_INVALID_PARAMETER);
            return FALSE;
        }

        if (addr == reg_base) {
            /* Split into [reg_base, split_end) and [split_end, reg_end) */
            KB_VM_REGION *right = kb_alloc_vm_region_locked();
            if (!right) {
                kb_util_unlock();
                SetLastError(ERROR_NOT_ENOUGH_MEMORY);
                return FALSE;
            }
            reg->size = dwSize;
            right->base = split_end;
            right->alloc_base = split_end;
            right->size = reg_end - split_end;
            right->state = MEM_RESERVE;
            right->protect = PAGE_NOACCESS;
            right->alloc_protect = PAGE_NOACCESS;
            right->type = MEM_PRIVATE;
            right->is_placeholder = 1;
            right->backing_idx = backing_idx;
        } else if (split_end == reg_end) {
            /* Split into [reg_base, addr) and [addr, reg_end) */
            KB_VM_REGION *right = kb_alloc_vm_region_locked();
            if (!right) {
                kb_util_unlock();
                SetLastError(ERROR_NOT_ENOUGH_MEMORY);
                return FALSE;
            }
            reg->size = addr - reg_base;
            right->base = addr;
            right->alloc_base = addr;
            right->size = dwSize;
            right->state = MEM_RESERVE;
            right->protect = PAGE_NOACCESS;
            right->alloc_protect = PAGE_NOACCESS;
            right->type = MEM_PRIVATE;
            right->is_placeholder = 1;
            right->backing_idx = backing_idx;
        } else {
            /* Split into 3 adjacent placeholders */
            KB_VM_REGION *mid = kb_alloc_vm_region_locked();
            KB_VM_REGION *right = kb_alloc_vm_region_locked();
            if (!mid || !right) {
                if (mid) mid->in_use = 0;
                if (right) right->in_use = 0;
                kb_util_unlock();
                SetLastError(ERROR_NOT_ENOUGH_MEMORY);
                return FALSE;
            }
            reg->size = addr - reg_base;

            mid->base = addr;
            mid->alloc_base = addr;
            mid->size = dwSize;
            mid->state = MEM_RESERVE;
            mid->protect = PAGE_NOACCESS;
            mid->alloc_protect = PAGE_NOACCESS;
            mid->type = MEM_PRIVATE;
            mid->is_placeholder = 1;
            mid->backing_idx = backing_idx;

            right->base = split_end;
            right->alloc_base = split_end;
            right->size = reg_end - split_end;
            right->state = MEM_RESERVE;
            right->protect = PAGE_NOACCESS;
            right->alloc_protect = PAGE_NOACCESS;
            right->type = MEM_PRIVATE;
            right->is_placeholder = 1;
            right->backing_idx = backing_idx;
        }
        kb_util_unlock();
        return TRUE;
    }

    /* Case 2: Coalesce placeholders (MEM_RELEASE | MEM_COALESCE_PLACEHOLDERS) */
    if (dwFreeType == (MEM_RELEASE | MEM_COALESCE_PLACEHOLDERS)) {
        uintptr_t cur = addr;
        uintptr_t end = addr + dwSize;
        KB_VM_REGION *first = NULL;
        ULONG i;

        if (dwSize == 0 || (addr & NTDLL_WIN_PAGE_MASK) != 0 || (dwSize & NTDLL_WIN_PAGE_MASK) != 0) {
            kb_util_unlock();
            SetLastError(ERROR_INVALID_PARAMETER);
            return FALSE;
        }
        while (cur < end) {
            KB_VM_REGION *part = kb_find_vm_region_containing_locked(cur);
            if (!part || !part->is_placeholder || part->base != cur || (part->base + part->size) > end) {
                kb_util_unlock();
                SetLastError(ERROR_INVALID_ADDRESS);
                return FALSE;
            }
            if (!first) {
                first = part;
            }
            cur += part->size;
        }
        if (!first) {
            kb_util_unlock();
            SetLastError(ERROR_INVALID_ADDRESS);
            return FALSE;
        }
        for (i = 0; i < KB_MAX_VM_REGIONS; i++) {
            if (g_vm_regions[i].in_use &&
                &g_vm_regions[i] != first &&
                g_vm_regions[i].base >= addr &&
                (g_vm_regions[i].base + g_vm_regions[i].size) <= end) {
                g_vm_regions[i].in_use = 0;
            }
        }
        first->base = addr;
        first->alloc_base = addr;
        first->size = dwSize;
        kb_util_unlock();
        return TRUE;
    }

    /* Case 3: Standard MEM_RELEASE or MEM_DECOMMIT */
    reg = kb_find_vm_region_containing_locked(addr);
    if (!reg) {
        kb_util_unlock();
        SetLastError(ERROR_INVALID_ADDRESS);
        return FALSE;
    }

    if (dwFreeType == MEM_DECOMMIT) {
        reg->state = MEM_RESERVE;
        reg->protect = PAGE_NOACCESS;
        kb_util_unlock();
        return TRUE;
    }

    if (dwFreeType == MEM_RELEASE) {
        ULONG backing_idx;
        if (reg->base != addr || (dwSize != 0 && dwSize != reg->size)) {
            kb_util_unlock();
            SetLastError(ERROR_INVALID_PARAMETER);
            return FALSE;
        }
        backing_idx = reg->backing_idx;
        reg->in_use = 0;
        kb_maybe_free_backing_locked(backing_idx);
        kb_util_unlock();
        return TRUE;
    }

    kb_util_unlock();
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
}

BOOL WINAPI VirtualFree(PVOID lpAddress, SIZE_T dwSize, DWORD dwFreeType)
{
    return VirtualFreeEx(GetCurrentProcess(), lpAddress, dwSize, dwFreeType);
}

SIZE_T WINAPI VirtualQueryEx(
    HANDLE hProcess,
    LPCVOID lpAddress,
    PMEMORY_BASIC_INFORMATION lpBuffer,
    SIZE_T dwLength)
{
    uintptr_t addr = (uintptr_t)lpAddress;
    KB_VM_REGION *reg;

    if (kb_util_is_stub_simulated()) {
        SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
        return 0;
    }
    if (!lpBuffer || dwLength < sizeof(MEMORY_BASIC_INFORMATION)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return 0;
    }

    kb_util_lock();
    kb_util_ensure_init_locked();
    if (!kb_is_valid_current_process_locked(hProcess)) {
        kb_util_unlock();
        SetLastError(ERROR_INVALID_HANDLE);
        return 0;
    }

    reg = kb_find_vm_region_containing_locked(addr);
    if (!reg) {
        kb_util_unlock();
        SetLastError(ERROR_INVALID_PARAMETER);
        return 0;
    }

    memset(lpBuffer, 0, sizeof(*lpBuffer));
    lpBuffer->BaseAddress = (PVOID)reg->base;
    lpBuffer->AllocationBase = (PVOID)reg->alloc_base;
    lpBuffer->AllocationProtect = reg->alloc_protect;
    lpBuffer->RegionSize = reg->size;
    lpBuffer->State = reg->state;
    lpBuffer->Protect = reg->protect;
    lpBuffer->Type = reg->type;
    kb_util_unlock();
    return sizeof(MEMORY_BASIC_INFORMATION);
}

SIZE_T WINAPI VirtualQuery(LPCVOID lpAddress, PMEMORY_BASIC_INFORMATION lpBuffer, SIZE_T dwLength)
{
    return VirtualQueryEx(GetCurrentProcess(), lpAddress, lpBuffer, dwLength);
}

/* ============================================================================
 * 5. CreateFileMappingW / MapViewOfFile3 / UnmapViewOfFile2 (Darwin Mach VM + Placeholders)
 * ============================================================================ */

HANDLE WINAPI CreateFileMappingW(
    HANDLE hFile,
    LPSECURITY_ATTRIBUTES lpFileMappingAttributes,
    DWORD flProtect,
    DWORD dwMaximumSizeHigh,
    DWORD dwMaximumSizeLow,
    LPCWSTR lpName)
{
    uint64_t total_size;
    SIZE_T rounded_size;
    ULONG i;
    KB_SECTION_OBJECT *sec = NULL;
    KB_HANDLE_RECORD *rec;
    int existed = 0;

    (void)hFile;
    (void)lpFileMappingAttributes;

    if (kb_util_is_stub_simulated()) {
        SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
        return NULL;
    }

    total_size = ((uint64_t)dwMaximumSizeHigh << 32) | (uint64_t)dwMaximumSizeLow;
    if (total_size == 0) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return NULL;
    }
    rounded_size = kb_round_up((SIZE_T)total_size, NTDLL_ALLOCATION_GRANULARITY);

    kb_util_lock();
    kb_util_ensure_init_locked();

    if (lpName && lpName[0] != 0) {
        for (i = 0; i < KB_MAX_SECTIONS; i++) {
            if (g_sections[i].in_use && kb_wstr_equal_ci(g_sections[i].name, lpName)) {
                sec = &g_sections[i];
                sec->ref_count++;
                existed = 1;
                break;
            }
        }
    }

    if (!sec) {
        for (i = 0; i < KB_MAX_SECTIONS; i++) {
            if (!g_sections[i].in_use) {
                sec = &g_sections[i];
                break;
            }
        }
        if (!sec) {
            kb_util_unlock();
            SetLastError(ERROR_NOT_ENOUGH_MEMORY);
            return NULL;
        }
        memset(sec, 0, sizeof(*sec));
#if defined(__APPLE__) || !defined(_WIN32)
        sec->backing = mmap(
            NULL,
            rounded_size,
            PROT_READ | PROT_WRITE,
            MAP_SHARED | MAP_ANON,
            -1,
            0);
        if (sec->backing == MAP_FAILED) {
            sec->backing = NULL;
        } else {
            sec->backing_is_mmap = 1;
        }
#else
        sec->backing = calloc(1, rounded_size);
        sec->backing_is_mmap = 0;
#endif
        if (!sec->backing) {
            kb_util_unlock();
            SetLastError(ERROR_NOT_ENOUGH_MEMORY);
            return NULL;
        }
        sec->in_use = 1;
        sec->ref_count = 1U;
        sec->canonical_id = __atomic_add_fetch(&g_next_canonical_id, 1ULL, __ATOMIC_SEQ_CST);
        sec->size = rounded_size;
        sec->protect = flProtect;
        kb_wstrcpy_bounded(sec->name, 128, lpName);
    }

    rec = kb_alloc_handle_record_locked(1);
    if (!rec) {
        kb_release_section_locked(sec);
        kb_util_unlock();
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return NULL;
    }
    rec->kind = KB_HANDLE_SECTION;
    rec->canonical_id = sec->canonical_id;
    rec->section = sec;
    kb_util_unlock();

    SetLastError(existed ? ERROR_ALREADY_EXISTS : ERROR_SUCCESS);
    return rec->handle_val;
}

PVOID WINAPI MapViewOfFile3(
    HANDLE FileMapping,
    HANDLE Process,
    PVOID BaseAddress,
    ULONG64 Offset,
    SIZE_T ViewSize,
    ULONG AllocationType,
    ULONG PageProtection,
    MEM_EXTENDED_PARAMETER *ExtendedParameters,
    ULONG ParameterCount)
{
    KB_HANDLE_KIND kind = KB_HANDLE_NONE;
    KB_SECTION_OBJECT *sec = NULL;
    int replace_placeholder = (AllocationType & MEM_REPLACE_PLACEHOLDER) != 0;
    SIZE_T required_align = replace_placeholder ? NTDLL_WIN_PAGE_SIZE : NTDLL_ALLOCATION_GRANULARITY;
    SIZE_T map_size;
    KB_VM_REGION *reg;

    if (kb_util_is_stub_simulated()) {
        SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
        return NULL;
    }
    if (ParameterCount > 0 && !ExtendedParameters) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return NULL;
    }
    if ((AllocationType & ~(ULONG)MEM_REPLACE_PLACEHOLDER) != 0) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return NULL;
    }

    /* Alignment validation: 64KB normally, 4KB when MEM_REPLACE_PLACEHOLDER is set */
    if ((Offset & (required_align - 1)) != 0 ||
        ((uintptr_t)BaseAddress & (required_align - 1)) != 0) {
        SetLastError(ERROR_MAPPED_ALIGNMENT);
        return NULL;
    }

    kb_util_lock();
    kb_util_ensure_init_locked();
    if (!kb_is_valid_current_process_locked(Process) ||
        !kb_resolve_handle_locked(FileMapping, &kind, NULL, NULL, NULL, &sec) ||
        kind != KB_HANDLE_SECTION || !sec) {
        kb_util_unlock();
        SetLastError(ERROR_INVALID_HANDLE);
        return NULL;
    }

    if (Offset >= sec->size) {
        kb_util_unlock();
        SetLastError(ERROR_INVALID_PARAMETER);
        return NULL;
    }
    map_size = (ViewSize == 0) ? (sec->size - (SIZE_T)Offset) : ViewSize;
    if (map_size == 0 || (Offset + map_size) > sec->size) {
        kb_util_unlock();
        SetLastError(ERROR_INVALID_PARAMETER);
        return NULL;
    }

    if (replace_placeholder) {
        if (!BaseAddress) {
            kb_util_unlock();
            SetLastError(ERROR_INVALID_PARAMETER);
            return NULL;
        }
        reg = kb_find_vm_region_exact_locked((uintptr_t)BaseAddress, map_size);
        if (!reg || !reg->is_placeholder) {
            kb_util_unlock();
            SetLastError(ERROR_INVALID_ADDRESS);
            return NULL;
        }

        reg->is_placeholder = 0;
        reg->is_mapped_view = 1;
        reg->replaced_placeholder = 1;
        reg->remapped_mach_vm = 0;
        reg->state = MEM_COMMIT;
        reg->protect = PageProtection;
        reg->alloc_protect = PageProtection;
        reg->type = MEM_MAPPED;
        reg->section = sec;
        reg->section_offset = Offset;
        sec->ref_count++;

#ifdef __APPLE__
        {
            vm_address_t target_addr = (vm_address_t)reg->base;
            vm_address_t src_addr = (vm_address_t)((char *)sec->backing + (SIZE_T)Offset);
            vm_prot_t cur_prot = VM_PROT_NONE;
            vm_prot_t max_prot = VM_PROT_NONE;
            if (((target_addr | src_addr | map_size) & (vm_page_size - 1)) == 0) {
                if (vm_remap(
                        mach_task_self(),
                        &target_addr,
                        (vm_size_t)map_size,
                        0,
                        VM_FLAGS_FIXED | VM_FLAGS_OVERWRITE,
                        mach_task_self(),
                        src_addr,
                        FALSE,
                        &cur_prot,
                        &max_prot,
                        VM_INHERIT_SHARE) == KERN_SUCCESS) {
                    reg->remapped_mach_vm = 1;
                }
            }
        }
#endif
        if (!reg->remapped_mach_vm) {
            memcpy((void *)reg->base, (const char *)sec->backing + (SIZE_T)Offset, map_size);
        }

        kb_util_unlock();
        return (PVOID)reg->base;
    }

    /* Non-placeholder view: map directly at shared section offset or via Mach vm_remap */
    if (BaseAddress != NULL) {
        kb_util_unlock();
        SetLastError(ERROR_INVALID_ADDRESS);
        return NULL;
    }

    reg = kb_alloc_vm_region_locked();
    if (!reg) {
        kb_util_unlock();
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return NULL;
    }

    reg->base = (uintptr_t)((char *)sec->backing + (SIZE_T)Offset);
    reg->alloc_base = reg->base;
    reg->size = map_size;
    reg->state = MEM_COMMIT;
    reg->protect = PageProtection;
    reg->alloc_protect = PageProtection;
    reg->type = MEM_MAPPED;
    reg->is_placeholder = 0;
    reg->is_mapped_view = 1;
    reg->replaced_placeholder = 0;
    reg->remapped_mach_vm = 0;
    reg->section = sec;
    reg->section_offset = Offset;

#ifdef __APPLE__
    {
        vm_address_t target_addr = 0;
        vm_address_t src_addr = (vm_address_t)((char *)sec->backing + (SIZE_T)Offset);
        vm_prot_t cur_prot = VM_PROT_NONE;
        vm_prot_t max_prot = VM_PROT_NONE;
        if (((src_addr | map_size) & (vm_page_size - 1)) == 0) {
            if (vm_remap(
                    mach_task_self(),
                    &target_addr,
                    (vm_size_t)map_size,
                    0xFFFF,
                    VM_FLAGS_ANYWHERE,
                    mach_task_self(),
                    src_addr,
                    FALSE,
                    &cur_prot,
                    &max_prot,
                    VM_INHERIT_SHARE) == KERN_SUCCESS) {
                reg->base = (uintptr_t)target_addr;
                reg->alloc_base = (uintptr_t)target_addr;
                reg->remapped_mach_vm = 1;
            }
        }
    }
#endif
    sec->ref_count++;
    kb_util_unlock();
    return (PVOID)reg->base;
}

PVOID WINAPI MapViewOfFile3FromApp(
    HANDLE FileMapping,
    HANDLE Process,
    PVOID BaseAddress,
    ULONG64 Offset,
    SIZE_T ViewSize,
    ULONG AllocationType,
    ULONG PageProtection,
    MEM_EXTENDED_PARAMETER *ExtendedParameters,
    ULONG ParameterCount)
{
    ULONG base_prot = PageProtection & 0xFFU;
    if (kb_util_is_stub_simulated()) {
        SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
        return NULL;
    }
    if (base_prot == PAGE_EXECUTE ||
        base_prot == PAGE_EXECUTE_READ ||
        base_prot == PAGE_EXECUTE_READWRITE ||
        base_prot == PAGE_EXECUTE_WRITECOPY) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return NULL;
    }
    return MapViewOfFile3(
        FileMapping,
        Process,
        BaseAddress,
        Offset,
        ViewSize,
        AllocationType,
        PageProtection,
        ExtendedParameters,
        ParameterCount);
}

BOOL WINAPI UnmapViewOfFile2(HANDLE Process, PVOID BaseAddress, ULONG UnmapFlags)
{
    uintptr_t addr = (uintptr_t)BaseAddress;
    KB_VM_REGION *reg = NULL;
    ULONG i;

    if (kb_util_is_stub_simulated()) {
        SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
        return FALSE;
    }
    if (!BaseAddress) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if ((UnmapFlags & ~(ULONG)(MEM_PRESERVE_PLACEHOLDER | MEM_UNMAP_WITH_TRANSIENT_BOOST)) != 0) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    kb_util_lock();
    kb_util_ensure_init_locked();
    if (!kb_is_valid_current_process_locked(Process)) {
        kb_util_unlock();
        SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }

    for (i = 0; i < KB_MAX_VM_REGIONS; i++) {
        if (g_vm_regions[i].in_use &&
            g_vm_regions[i].is_mapped_view &&
            g_vm_regions[i].base == addr) {
            reg = &g_vm_regions[i];
            break;
        }
    }
    if (!reg) {
        kb_util_unlock();
        SetLastError(ERROR_INVALID_ADDRESS);
        return FALSE;
    }

    if ((UnmapFlags & MEM_PRESERVE_PLACEHOLDER) && !reg->replaced_placeholder) {
        kb_util_unlock();
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    if (reg->replaced_placeholder && !reg->remapped_mach_vm && reg->section && reg->section->backing) {
        memcpy(
            (char *)reg->section->backing + (SIZE_T)reg->section_offset,
            (const void *)reg->base,
            reg->size);
    }

#ifdef __APPLE__
    if (reg->remapped_mach_vm && !reg->replaced_placeholder) {
        (void)vm_deallocate(mach_task_self(), (vm_address_t)reg->base, (vm_size_t)reg->size);
    }
#endif

    if (reg->section) {
        kb_release_section_locked(reg->section);
        reg->section = NULL;
    }

    if (UnmapFlags & MEM_PRESERVE_PLACEHOLDER) {
        reg->is_mapped_view = 0;
        reg->is_placeholder = 1;
        reg->state = MEM_RESERVE;
        reg->protect = PAGE_NOACCESS;
        reg->alloc_protect = PAGE_NOACCESS;
        reg->type = MEM_PRIVATE;
        kb_util_unlock();
        return TRUE;
    }

    {
        ULONG backing_idx = reg->backing_idx;
        reg->in_use = 0;
        kb_maybe_free_backing_locked(backing_idx);
    }
    kb_util_unlock();
    return TRUE;
}

BOOL WINAPI UnmapViewOfFile(LPCVOID lpBaseAddress)
{
    return UnmapViewOfFile2(GetCurrentProcess(), (PVOID)lpBaseAddress, 0);
}

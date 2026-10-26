/*
 * Wine NTDLL Virtual Memory Management
 * (Apple Silicon 16 KB Host Page vs. 4 KB Windows PE Page, Rosetta 2 Fault Parity & MAP_JIT W^X)
 *
 * Copyright 2026 Spumante Contributors
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#ifdef __APPLE__
#include <pthread.h>
#include <libkern/OSCacheControl.h>
#include <sys/mman.h>
#endif

#include "ntdll.h"

#define WINE_DEFAULT_DEBUG_CHANNEL(ch)
#define TRACE(...) do { } while (0)
#define WARN(...)  do { } while (0)
#define FIXME(...) do { } while (0)
#define ERR(...)   do { } while (0)

WINE_DEFAULT_DEBUG_CHANNEL(virtual);

#define MAX_VIRTUAL_VIEWS 64U

struct ntdll_virtual_view {
    int       in_use;
    uint8_t  *raw_alloc;
    uint8_t  *base;
    SIZE_T    size;
    ULONG     alloc_protect;
    ULONG     alloc_type;
    uint8_t  *vprot;
    uint8_t  *watch_shadow;
};

static struct ntdll_virtual_view g_views[MAX_VIRTUAL_VIEWS];

#ifdef __APPLE__
static ULONG g_simulated_host_page_size = NTDLL_APPLE_SILICON_HOST_PAGE_SIZE;
#else
static ULONG g_simulated_host_page_size = NTDLL_WIN_PAGE_SIZE;
#endif

static NTDLL_JIT_WX_STATE g_jit_wx_state = {
#ifdef __APPLE__
    NTDLL_APPLE_SILICON_HOST_PAGE_SIZE,
#else
    NTDLL_WIN_PAGE_SIZE,
#endif
    NTDLL_WIN_PAGE_SIZE,
    0,
    0,
    0,
    0,
    0
};

static NTDLL_PROCESS_EXEC_WRITE_STATES g_proc_exec_write = { 1, 0, 0, 0, 0 };
static NTDLL_THREAD_EXEC_WRITE_STATES g_thread_exec_write = { 1, 0, 0 };
static int g_thread_exec_write_set = 0;

static int is_stub_simulation_enabled(void)
{
    const char *env = getenv("SPUMANTE_SIMULATE_STUB");
    return (env != NULL && strcmp(env, "1") == 0);
}

static int is_valid_win32_protect(ULONG protect)
{
    ULONG base_prot = protect & ~(PAGE_GUARD | PAGE_NOCACHE | PAGE_WRITECOMBINE);
    switch (base_prot) {
    case PAGE_NOACCESS:
    case PAGE_READONLY:
    case PAGE_READWRITE:
    case PAGE_WRITECOPY:
    case PAGE_EXECUTE:
    case PAGE_EXECUTE_READ:
    case PAGE_EXECUTE_READWRITE:
    case PAGE_EXECUTE_WRITECOPY:
        break;
    default:
        return 0;
    }
    if (base_prot == PAGE_NOACCESS && (protect & (PAGE_GUARD | PAGE_NOCACHE | PAGE_WRITECOMBINE))) {
        return 0;
    }
    return 1;
}

static uint8_t win32_protect_to_vprot(ULONG protect, int committed, int write_watch)
{
    uint8_t v = 0;
    ULONG base_prot = protect & ~(PAGE_GUARD | PAGE_NOCACHE | PAGE_WRITECOMBINE);

    switch (base_prot) {
    case PAGE_READONLY:
        v = VPROT_READ;
        break;
    case PAGE_READWRITE:
        v = VPROT_READ | VPROT_WRITE;
        break;
    case PAGE_WRITECOPY:
        v = VPROT_READ | VPROT_WRITECOPY;
        break;
    case PAGE_EXECUTE:
        v = VPROT_EXEC;
        break;
    case PAGE_EXECUTE_READ:
        v = VPROT_EXEC | VPROT_READ;
        break;
    case PAGE_EXECUTE_READWRITE:
        v = VPROT_EXEC | VPROT_READ | VPROT_WRITE;
        break;
    case PAGE_EXECUTE_WRITECOPY:
        v = VPROT_EXEC | VPROT_READ | VPROT_WRITECOPY;
        break;
    case PAGE_NOACCESS:
    default:
        v = 0;
        break;
    }

    if (protect & PAGE_GUARD) {
        v |= VPROT_GUARD;
    }
    if (committed) {
        v |= VPROT_COMMITTED;
    }
    if (write_watch) {
        v |= VPROT_WRITEWATCH;
    }
    return v;
}

static ULONG vprot_to_win32_protect(uint8_t vprot)
{
    ULONG prot = PAGE_NOACCESS;
    uint8_t rwx = vprot & (VPROT_READ | VPROT_WRITE | VPROT_EXEC | VPROT_WRITECOPY);

    if (!(vprot & VPROT_COMMITTED)) {
        return 0;
    }

    if (rwx & VPROT_EXEC) {
        if (rwx & VPROT_WRITECOPY) {
            prot = PAGE_EXECUTE_WRITECOPY;
        } else if (rwx & VPROT_WRITE) {
            prot = PAGE_EXECUTE_READWRITE;
        } else if (rwx & VPROT_READ) {
            prot = PAGE_EXECUTE_READ;
        } else {
            prot = PAGE_EXECUTE;
        }
    } else {
        if (rwx & VPROT_WRITECOPY) {
            prot = PAGE_WRITECOPY;
        } else if (rwx & VPROT_WRITE) {
            prot = PAGE_READWRITE;
        } else if (rwx & VPROT_READ) {
            prot = PAGE_READONLY;
        } else {
            prot = PAGE_NOACCESS;
        }
    }

    if (vprot & VPROT_GUARD) {
        prot |= PAGE_GUARD;
    }
    return prot;
}

static struct ntdll_virtual_view *find_view_containing(LPCVOID addr, SIZE_T size)
{
    uintptr_t uaddr = (uintptr_t)addr;
    size_t i;

    for (i = 0; i < MAX_VIRTUAL_VIEWS; i++) {
        uintptr_t vbase;
        uintptr_t vend;
        if (!g_views[i].in_use) {
            continue;
        }
        vbase = (uintptr_t)g_views[i].base;
        vend = vbase + g_views[i].size;
        if (uaddr >= vbase && uaddr < vend) {
            if (size <= (SIZE_T)(vend - uaddr)) {
                return &g_views[i];
            }
            return NULL;
        }
    }
    return NULL;
}

void WINAPI NtdllResetVirtualState(void)
{
    size_t i;
    for (i = 0; i < MAX_VIRTUAL_VIEWS; i++) {
        if (g_views[i].in_use) {
            free(g_views[i].raw_alloc);
            free(g_views[i].vprot);
            free(g_views[i].watch_shadow);
            memset(&g_views[i], 0, sizeof(g_views[i]));
        }
    }
#ifdef __APPLE__
    g_simulated_host_page_size = NTDLL_APPLE_SILICON_HOST_PAGE_SIZE;
#else
    g_simulated_host_page_size = NTDLL_WIN_PAGE_SIZE;
#endif
    memset(&g_jit_wx_state, 0, sizeof(g_jit_wx_state));
    g_jit_wx_state.host_page_size = g_simulated_host_page_size;
    g_jit_wx_state.win_page_size = NTDLL_WIN_PAGE_SIZE;
    memset(&g_proc_exec_write, 0, sizeof(g_proc_exec_write));
    g_proc_exec_write.Version = 1;
    memset(&g_thread_exec_write, 0, sizeof(g_thread_exec_write));
    g_thread_exec_write.Version = 1;
    g_thread_exec_write_set = 0;
}

NTSTATUS WINAPI NtdllSetSimulatedHostPageSize(ULONG HostPageSize)
{
    if (is_stub_simulation_enabled()) {
        return STATUS_NOT_IMPLEMENTED;
    }
    if (HostPageSize != NTDLL_WIN_PAGE_SIZE && HostPageSize != NTDLL_APPLE_SILICON_HOST_PAGE_SIZE) {
        return STATUS_INVALID_PARAMETER;
    }
    g_simulated_host_page_size = HostPageSize;
    g_jit_wx_state.host_page_size = HostPageSize;
    g_jit_wx_state.win_page_size = NTDLL_WIN_PAGE_SIZE;
    return STATUS_SUCCESS;
}

NTSTATUS WINAPI NtdllQueryJitWxState(NTDLL_JIT_WX_STATE *State)
{
    if (is_stub_simulation_enabled()) {
        return STATUS_NOT_IMPLEMENTED;
    }
    if (State == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    *State = g_jit_wx_state;
    return STATUS_SUCCESS;
}

NTSTATUS WINAPI NtdllToggleJitWriteProtection(ULONG WriteEnabled)
{
    if (is_stub_simulation_enabled()) {
        return STATUS_NOT_IMPLEMENTED;
    }
#ifdef __APPLE__
#ifdef __aarch64__
    /* On Apple Silicon, toggle hardware APRR W^X state for MAP_JIT regions */
    pthread_jit_write_protect_np(WriteEnabled ? 0 : 1);
#endif
#endif
    g_jit_wx_state.wx_write_enabled = WriteEnabled ? 1U : 0U;
    g_jit_wx_state.wx_toggle_count++;
    return STATUS_SUCCESS;
}

NTSTATUS WINAPI NtAllocateVirtualMemory(
    HANDLE ProcessHandle,
    PVOID *BaseAddress,
    ULONG_PTR ZeroBits,
    SIZE_T *RegionSize,
    ULONG AllocationType,
    ULONG Protect)
{
    SIZE_T req_size;
    SIZE_T aligned_size;
    SIZE_T num_pages;
    size_t slot = MAX_VIRTUAL_VIEWS;
    size_t i;
    uint8_t *raw = NULL;
    uint8_t *aligned_base = NULL;
    uint8_t *vprot = NULL;
    uint8_t *shadow = NULL;
    uintptr_t raw_addr;
    uintptr_t aligned_addr;
    int committed;
    int write_watch;
    uint8_t init_vprot;

    (void)ZeroBits;

    if (is_stub_simulation_enabled()) {
        return STATUS_NOT_IMPLEMENTED;
    }
    if (ProcessHandle != NtCurrentProcess()) {
        return STATUS_INVALID_HANDLE;
    }
    if (BaseAddress == NULL || RegionSize == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    req_size = *RegionSize;
    if (req_size == 0) {
        return STATUS_INVALID_PARAMETER;
    }
    if (!is_valid_win32_protect(Protect)) {
        return STATUS_INVALID_PAGE_PROTECTION;
    }
    if ((AllocationType & (MEM_COMMIT | MEM_RESERVE | MEM_RESET)) == 0) {
        return STATUS_INVALID_PARAMETER;
    }
    if ((AllocationType & MEM_WRITE_WATCH) && !(AllocationType & MEM_RESERVE)) {
        return STATUS_INVALID_PARAMETER;
    }

    /* Check if committing pages inside an already reserved view */
    if (*BaseAddress != NULL && !(AllocationType & MEM_RESERVE) && (AllocationType & MEM_COMMIT)) {
        struct ntdll_virtual_view *existing = find_view_containing(*BaseAddress, 1);
        uintptr_t req_base;
        uintptr_t req_end;
        size_t start_page;
        size_t end_page;
        size_t p;

        if (existing == NULL) {
            return STATUS_CONFLICTING_ADDRESSES;
        }
        req_base = (uintptr_t)(*BaseAddress) & ~(uintptr_t)NTDLL_WIN_PAGE_MASK;
        req_end = ((uintptr_t)(*BaseAddress) + req_size + NTDLL_WIN_PAGE_MASK) & ~(uintptr_t)NTDLL_WIN_PAGE_MASK;
        if (req_end > (uintptr_t)existing->base + existing->size) {
            return STATUS_UNABLE_TO_FREE_VM;
        }
        start_page = (size_t)((req_base - (uintptr_t)existing->base) >> NTDLL_WIN_PAGE_SHIFT);
        end_page = (size_t)((req_end - (uintptr_t)existing->base) >> NTDLL_WIN_PAGE_SHIFT);
        init_vprot = win32_protect_to_vprot(
            Protect, 1, (existing->alloc_type & MEM_WRITE_WATCH) ? 1 : 0);
        for (p = start_page; p < end_page; p++) {
            existing->vprot[p] = init_vprot;
        }
        *BaseAddress = (PVOID)req_base;
        *RegionSize = (SIZE_T)(req_end - req_base);
        return STATUS_SUCCESS;
    }

    for (i = 0; i < MAX_VIRTUAL_VIEWS; i++) {
        if (!g_views[i].in_use) {
            slot = i;
            break;
        }
    }
    if (slot == MAX_VIRTUAL_VIEWS) {
        return STATUS_NO_MEMORY;
    }

    aligned_size = (req_size + NTDLL_WIN_PAGE_MASK) & ~(SIZE_T)NTDLL_WIN_PAGE_MASK;
    num_pages = aligned_size >> NTDLL_WIN_PAGE_SHIFT;

    /* Allocate with 64 KB alignment padding to match Windows allocation granularity */
    raw = (uint8_t *)calloc(1, aligned_size + NTDLL_ALLOCATION_GRANULARITY);
    if (raw == NULL) {
        return STATUS_NO_MEMORY;
    }
    vprot = (uint8_t *)calloc(num_pages, sizeof(uint8_t));
    if (vprot == NULL) {
        free(raw);
        return STATUS_NO_MEMORY;
    }

    write_watch = (AllocationType & MEM_WRITE_WATCH) ? 1 : 0;
    if (write_watch) {
        shadow = (uint8_t *)calloc(1, aligned_size);
        if (shadow == NULL) {
            free(vprot);
            free(raw);
            return STATUS_NO_MEMORY;
        }
    }

    raw_addr = (uintptr_t)raw;
    aligned_addr = (raw_addr + NTDLL_ALLOCATION_GRANULARITY_MASK) & ~(uintptr_t)NTDLL_ALLOCATION_GRANULARITY_MASK;
    aligned_base = (uint8_t *)aligned_addr;

    committed = (AllocationType & MEM_COMMIT) ? 1 : 0;
    init_vprot = win32_protect_to_vprot(Protect, committed, write_watch);
    memset(vprot, init_vprot, num_pages);

    g_views[slot].in_use = 1;
    g_views[slot].raw_alloc = raw;
    g_views[slot].base = aligned_base;
    g_views[slot].size = aligned_size;
    g_views[slot].alloc_protect = Protect;
    g_views[slot].alloc_type = AllocationType;
    g_views[slot].vprot = vprot;
    g_views[slot].watch_shadow = shadow;

    if (init_vprot & VPROT_EXEC) {
        g_jit_wx_state.map_jit_enabled = 1U;
    }

    *BaseAddress = (PVOID)aligned_base;
    *RegionSize = aligned_size;
    TRACE("Allocated %zu bytes at %p (4KB pages=%zu, host_page=0x%x)\n",
          (size_t)aligned_size, (void *)aligned_base, (size_t)num_pages,
          (unsigned int)g_simulated_host_page_size);
    return STATUS_SUCCESS;
}

NTSTATUS WINAPI NtFreeVirtualMemory(
    HANDLE ProcessHandle,
    PVOID *BaseAddress,
    SIZE_T *RegionSize,
    ULONG FreeType)
{
    struct ntdll_virtual_view *view;

    if (is_stub_simulation_enabled()) {
        return STATUS_NOT_IMPLEMENTED;
    }
    if (ProcessHandle != NtCurrentProcess()) {
        return STATUS_INVALID_HANDLE;
    }
    if (BaseAddress == NULL || RegionSize == NULL || *BaseAddress == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    view = find_view_containing(*BaseAddress, 0);
    if (view == NULL) {
        return STATUS_MEMORY_NOT_ALLOCATED;
    }

    if (FreeType == MEM_RELEASE) {
        if (*RegionSize != 0) {
            return STATUS_INVALID_PARAMETER;
        }
        if (*BaseAddress != (PVOID)view->base) {
            return STATUS_FREE_VM_NOT_AT_BASE;
        }
        *RegionSize = view->size;
        free(view->raw_alloc);
        free(view->vprot);
        free(view->watch_shadow);
        memset(view, 0, sizeof(*view));
        return STATUS_SUCCESS;
    }

    if (FreeType == MEM_DECOMMIT) {
        uintptr_t req_base = (uintptr_t)(*BaseAddress) & ~(uintptr_t)NTDLL_WIN_PAGE_MASK;
        SIZE_T req_size = *RegionSize;
        uintptr_t req_end;
        size_t start_page;
        size_t end_page;
        size_t p;

        if (req_size == 0) {
            if (*BaseAddress != (PVOID)view->base) {
                return STATUS_INVALID_PARAMETER;
            }
            req_end = (uintptr_t)view->base + view->size;
        } else {
            req_end = ((uintptr_t)(*BaseAddress) + req_size + NTDLL_WIN_PAGE_MASK) & ~(uintptr_t)NTDLL_WIN_PAGE_MASK;
        }
        if (req_end > (uintptr_t)view->base + view->size) {
            return STATUS_UNABLE_TO_FREE_VM;
        }
        start_page = (size_t)((req_base - (uintptr_t)view->base) >> NTDLL_WIN_PAGE_SHIFT);
        end_page = (size_t)((req_end - (uintptr_t)view->base) >> NTDLL_WIN_PAGE_SHIFT);
        for (p = start_page; p < end_page; p++) {
            view->vprot[p] &= (uint8_t)~(VPROT_COMMITTED | VPROT_GUARD);
        }
        *BaseAddress = (PVOID)req_base;
        *RegionSize = (SIZE_T)(req_end - req_base);
        return STATUS_SUCCESS;
    }

    return STATUS_INVALID_PARAMETER;
}

NTSTATUS WINAPI NtProtectVirtualMemory(
    HANDLE ProcessHandle,
    PVOID *BaseAddress,
    SIZE_T *RegionSize,
    ULONG NewProtect,
    ULONG *OldProtect)
{
    struct ntdll_virtual_view *view;
    uintptr_t req_base;
    uintptr_t req_end;
    size_t start_page;
    size_t end_page;
    size_t p;
    int write_watch;

    if (is_stub_simulation_enabled()) {
        return STATUS_NOT_IMPLEMENTED;
    }
    if (ProcessHandle != NtCurrentProcess()) {
        return STATUS_INVALID_HANDLE;
    }
    if (BaseAddress == NULL || RegionSize == NULL || OldProtect == NULL || *BaseAddress == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    if (*RegionSize == 0) {
        return STATUS_INVALID_PARAMETER;
    }
    if (!is_valid_win32_protect(NewProtect)) {
        return STATUS_INVALID_PAGE_PROTECTION;
    }

    view = find_view_containing(*BaseAddress, 1);
    if (view == NULL) {
        return STATUS_CONFLICTING_ADDRESSES;
    }

    req_base = (uintptr_t)(*BaseAddress) & ~(uintptr_t)NTDLL_WIN_PAGE_MASK;
    req_end = ((uintptr_t)(*BaseAddress) + *RegionSize + NTDLL_WIN_PAGE_MASK) & ~(uintptr_t)NTDLL_WIN_PAGE_MASK;
    if (req_end > (uintptr_t)view->base + view->size) {
        return STATUS_CONFLICTING_ADDRESSES;
    }

    start_page = (size_t)((req_base - (uintptr_t)view->base) >> NTDLL_WIN_PAGE_SHIFT);
    end_page = (size_t)((req_end - (uintptr_t)view->base) >> NTDLL_WIN_PAGE_SHIFT);

    for (p = start_page; p < end_page; p++) {
        if (!(view->vprot[p] & VPROT_COMMITTED)) {
            return STATUS_NOT_COMMITTED;
        }
    }

    *OldProtect = vprot_to_win32_protect(view->vprot[start_page]);
    write_watch = (view->alloc_type & MEM_WRITE_WATCH) ? 1 : 0;

    /* Update per-4KB-page virtual protection independently of 16KB host page boundaries */
    for (p = start_page; p < end_page; p++) {
        int preserve_ww = (view->vprot[p] & VPROT_WRITEWATCH) ? 1 : 0;
        uint8_t next_vprot = win32_protect_to_vprot(NewProtect, 1, write_watch && preserve_ww);
        view->vprot[p] = next_vprot;
    }

    if (NewProtect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) {
        g_jit_wx_state.map_jit_enabled = 1U;
    }

    *BaseAddress = (PVOID)req_base;
    *RegionSize = (SIZE_T)(req_end - req_base);
    return STATUS_SUCCESS;
}

NTSTATUS WINAPI NtQueryVirtualMemory(
    HANDLE ProcessHandle,
    LPCVOID BaseAddress,
    ULONG MemoryInformationClass,
    PVOID MemoryInformation,
    SIZE_T MemoryInformationLength,
    SIZE_T *ReturnLength)
{
    struct ntdll_virtual_view *view;
    PNTDLL_MEMORY_BASIC_INFORMATION mbi;
    uintptr_t page_base;
    size_t start_page;
    size_t total_pages;
    size_t p;
    uint8_t first_vprot;

    if (is_stub_simulation_enabled()) {
        return STATUS_NOT_IMPLEMENTED;
    }
    if (ProcessHandle != NtCurrentProcess()) {
        return STATUS_INVALID_HANDLE;
    }
    if (MemoryInformationClass != MemoryBasicInformation) {
        return STATUS_INVALID_INFO_CLASS;
    }
    if (MemoryInformation == NULL) {
        return STATUS_ACCESS_VIOLATION;
    }
    if (MemoryInformationLength < sizeof(NTDLL_MEMORY_BASIC_INFORMATION)) {
        return STATUS_INFO_LENGTH_MISMATCH;
    }

    view = find_view_containing(BaseAddress, 1);
    mbi = (PNTDLL_MEMORY_BASIC_INFORMATION)MemoryInformation;
    memset(mbi, 0, sizeof(*mbi));

    if (view == NULL) {
        page_base = (uintptr_t)BaseAddress & ~(uintptr_t)NTDLL_WIN_PAGE_MASK;
        mbi->BaseAddress = (PVOID)page_base;
        mbi->AllocationBase = NULL;
        mbi->AllocationProtect = 0;
        mbi->RegionSize = NTDLL_ALLOCATION_GRANULARITY;
        mbi->State = MEM_FREE;
        mbi->Protect = PAGE_NOACCESS;
        mbi->Type = 0;
        if (ReturnLength != NULL) {
            *ReturnLength = sizeof(*mbi);
        }
        return STATUS_SUCCESS;
    }

    page_base = (uintptr_t)BaseAddress & ~(uintptr_t)NTDLL_WIN_PAGE_MASK;
    start_page = (size_t)((page_base - (uintptr_t)view->base) >> NTDLL_WIN_PAGE_SHIFT);
    total_pages = view->size >> NTDLL_WIN_PAGE_SHIFT;
    first_vprot = view->vprot[start_page];

    /* Coalesce consecutive 4 KB pages sharing identical state and protection */
    for (p = start_page + 1; p < total_pages; p++) {
        uint8_t mask = (uint8_t)~VPROT_WRITEWATCH;
        if ((view->vprot[p] & mask) != (first_vprot & mask)) {
            break;
        }
    }

    mbi->BaseAddress = (PVOID)page_base;
    mbi->AllocationBase = (PVOID)view->base;
    mbi->AllocationProtect = view->alloc_protect;
    mbi->RegionSize = (SIZE_T)((p - start_page) << NTDLL_WIN_PAGE_SHIFT);
    mbi->State = (first_vprot & VPROT_COMMITTED) ? MEM_COMMIT : MEM_RESERVE;
    mbi->Protect = (first_vprot & VPROT_COMMITTED) ? vprot_to_win32_protect(first_vprot) : 0;
    mbi->Type = MEM_PRIVATE;

    if (ReturnLength != NULL) {
        *ReturnLength = sizeof(*mbi);
    }
    return STATUS_SUCCESS;
}

static int is_page_write_watched_dirty(struct ntdll_virtual_view *view, size_t page_idx)
{
    uint8_t vp = view->vprot[page_idx];
    if (!(vp & VPROT_COMMITTED)) {
        return 0;
    }
    if (!(vp & VPROT_WRITEWATCH)) {
        return 1;
    }
    if (view->watch_shadow != NULL) {
        size_t off = page_idx << NTDLL_WIN_PAGE_SHIFT;
        if (memcmp(view->base + off, view->watch_shadow + off, NTDLL_WIN_PAGE_SIZE) != 0) {
            return 1;
        }
    }
    return 0;
}

static void reset_page_write_watch(struct ntdll_virtual_view *view, size_t page_idx)
{
    view->vprot[page_idx] |= VPROT_WRITEWATCH;
    if (view->watch_shadow != NULL) {
        size_t off = page_idx << NTDLL_WIN_PAGE_SHIFT;
        memcpy(view->watch_shadow + off, view->base + off, NTDLL_WIN_PAGE_SIZE);
    }
}

NTSTATUS WINAPI NtGetWriteWatch(
    HANDLE ProcessHandle,
    ULONG Flags,
    PVOID BaseAddress,
    SIZE_T RegionSize,
    PVOID *UserAddressArray,
    ULONG_PTR *EntriesInUserAddressArray,
    PULONG Granularity)
{
    struct ntdll_virtual_view *view;
    uintptr_t req_base;
    uintptr_t req_end;
    size_t start_page;
    size_t end_page;
    size_t p;
    ULONG_PTR max_entries;
    ULONG_PTR found = 0;

    if (is_stub_simulation_enabled()) {
        return STATUS_NOT_IMPLEMENTED;
    }
    if (ProcessHandle == NULL || ProcessHandle != NtCurrentProcess()) {
        return STATUS_INVALID_HANDLE;
    }
    if (EntriesInUserAddressArray == NULL || Granularity == NULL) {
        return STATUS_ACCESS_VIOLATION;
    }
    if (Flags & ~(ULONG)WRITE_WATCH_FLAG_RESET) {
        return STATUS_INVALID_PARAMETER_2;
    }
    if (RegionSize == 0) {
        return STATUS_INVALID_PARAMETER_4;
    }
    max_entries = *EntriesInUserAddressArray;
    if (max_entries == 0 || UserAddressArray == NULL) {
        return STATUS_INVALID_PARAMETER_5;
    }
    if (BaseAddress == NULL) {
        return STATUS_INVALID_PARAMETER_3;
    }

    view = find_view_containing(BaseAddress, 1);
    if (view == NULL || !(view->alloc_type & MEM_WRITE_WATCH)) {
        return STATUS_INVALID_PARAMETER_3;
    }

    req_base = (uintptr_t)BaseAddress & ~(uintptr_t)NTDLL_WIN_PAGE_MASK;
    req_end = ((uintptr_t)BaseAddress + RegionSize + NTDLL_WIN_PAGE_MASK) & ~(uintptr_t)NTDLL_WIN_PAGE_MASK;
    if (req_end > (uintptr_t)view->base + view->size) {
        return STATUS_INVALID_PARAMETER_4;
    }

    start_page = (size_t)((req_base - (uintptr_t)view->base) >> NTDLL_WIN_PAGE_SHIFT);
    end_page = (size_t)((req_end - (uintptr_t)view->base) >> NTDLL_WIN_PAGE_SHIFT);

    for (p = start_page; p < end_page && found < max_entries; p++) {
        if (is_page_write_watched_dirty(view, p)) {
            UserAddressArray[found++] = (PVOID)(view->base + (p << NTDLL_WIN_PAGE_SHIFT));
            if (Flags & WRITE_WATCH_FLAG_RESET) {
                reset_page_write_watch(view, p);
            }
        }
    }

    *EntriesInUserAddressArray = found;
    /* Always report 4 KB Windows page granularity even on 16 KB Apple Silicon hosts */
    *Granularity = NTDLL_WIN_PAGE_SIZE;
    return (found == max_entries && p < end_page) ? STATUS_BUFFER_TOO_SMALL : STATUS_SUCCESS;
}

NTSTATUS WINAPI NtResetWriteWatch(
    HANDLE ProcessHandle,
    PVOID BaseAddress,
    SIZE_T RegionSize)
{
    struct ntdll_virtual_view *view;
    uintptr_t req_base;
    uintptr_t req_end;
    size_t start_page;
    size_t end_page;
    size_t p;

    if (is_stub_simulation_enabled()) {
        return STATUS_NOT_IMPLEMENTED;
    }
    if (ProcessHandle == NULL || ProcessHandle != NtCurrentProcess()) {
        return STATUS_INVALID_HANDLE;
    }
    if (RegionSize == 0) {
        return STATUS_INVALID_PARAMETER_3;
    }
    if (BaseAddress == NULL) {
        return STATUS_INVALID_PARAMETER_2;
    }

    view = find_view_containing(BaseAddress, 1);
    if (view == NULL || !(view->alloc_type & MEM_WRITE_WATCH)) {
        return STATUS_INVALID_PARAMETER_2;
    }

    req_base = (uintptr_t)BaseAddress & ~(uintptr_t)NTDLL_WIN_PAGE_MASK;
    req_end = ((uintptr_t)BaseAddress + RegionSize + NTDLL_WIN_PAGE_MASK) & ~(uintptr_t)NTDLL_WIN_PAGE_MASK;
    if (req_end > (uintptr_t)view->base + view->size) {
        return STATUS_INVALID_PARAMETER_3;
    }

    start_page = (size_t)((req_base - (uintptr_t)view->base) >> NTDLL_WIN_PAGE_SHIFT);
    end_page = (size_t)((req_end - (uintptr_t)view->base) >> NTDLL_WIN_PAGE_SHIFT);

    for (p = start_page; p < end_page; p++) {
        if (view->vprot[p] & VPROT_COMMITTED) {
            reset_page_write_watch(view, p);
        }
    }
    return STATUS_SUCCESS;
}

NTSTATUS WINAPI NtSetInformationVirtualMemory(
    HANDLE ProcessHandle,
    ULONG VirtualMemoryInformationClass,
    ULONG_PTR NumberOfEntries,
    PNTDLL_MEMORY_RANGE_ENTRY VirtualAddresses,
    PVOID VirtualMemoryInformation,
    ULONG VirtualMemoryInformationLength)
{
    ULONG_PTR i;

    if (is_stub_simulation_enabled()) {
        return STATUS_NOT_IMPLEMENTED;
    }
    if (ProcessHandle != NtCurrentProcess()) {
        return STATUS_INVALID_HANDLE;
    }
    if (NumberOfEntries == 0 || VirtualAddresses == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    if (VirtualMemoryInformationClass == VmPrefetchInformation) {
        if (VirtualMemoryInformation == NULL || VirtualMemoryInformationLength < sizeof(ULONG)) {
            return STATUS_INVALID_PARAMETER;
        }
        return STATUS_SUCCESS;
    }

    if (VirtualMemoryInformationClass == VmPageDirtyStateInformation) {
        ULONG dirty_flag;
        if (VirtualMemoryInformation == NULL || VirtualMemoryInformationLength != sizeof(ULONG)) {
            return STATUS_INVALID_PARAMETER;
        }
        dirty_flag = *(const ULONG *)VirtualMemoryInformation;
        if (dirty_flag > 1U) {
            return STATUS_INVALID_PARAMETER;
        }
        for (i = 0; i < NumberOfEntries; i++) {
            struct ntdll_virtual_view *view = find_view_containing(
                VirtualAddresses[i].VirtualAddress, VirtualAddresses[i].NumberOfBytes);
            uintptr_t rbase;
            uintptr_t rend;
            size_t sp;
            size_t ep;
            size_t p;

            if (view == NULL || !(view->alloc_type & MEM_WRITE_WATCH)) {
                return STATUS_INVALID_PARAMETER;
            }
            rbase = (uintptr_t)VirtualAddresses[i].VirtualAddress & ~(uintptr_t)NTDLL_WIN_PAGE_MASK;
            rend = ((uintptr_t)VirtualAddresses[i].VirtualAddress + VirtualAddresses[i].NumberOfBytes + NTDLL_WIN_PAGE_MASK)
                   & ~(uintptr_t)NTDLL_WIN_PAGE_MASK;
            sp = (size_t)((rbase - (uintptr_t)view->base) >> NTDLL_WIN_PAGE_SHIFT);
            ep = (size_t)((rend - (uintptr_t)view->base) >> NTDLL_WIN_PAGE_SHIFT);
            for (p = sp; p < ep; p++) {
                if (dirty_flag) {
                    view->vprot[p] &= (uint8_t)~VPROT_WRITEWATCH;
                } else {
                    reset_page_write_watch(view, p);
                }
            }
        }
        return STATUS_SUCCESS;
    }

    return STATUS_INVALID_INFO_CLASS;
}

NTSTATUS WINAPI NtSetInformationProcess(
    HANDLE ProcessHandle,
    ULONG ProcessInformationClass,
    PVOID ProcessInformation,
    ULONG ProcessInformationLength)
{
    if (is_stub_simulation_enabled()) {
        return STATUS_NOT_IMPLEMENTED;
    }
    if (ProcessHandle != NtCurrentProcess()) {
        return STATUS_INVALID_HANDLE;
    }
    if (ProcessInformationClass == ProcessManageWritesToExecutableMemory) {
        const NTDLL_PROCESS_EXEC_WRITE_STATES *in_state;
        if (ProcessInformation == NULL || ProcessInformationLength != sizeof(NTDLL_PROCESS_EXEC_WRITE_STATES)) {
            return STATUS_INFO_LENGTH_MISMATCH;
        }
        in_state = (const NTDLL_PROCESS_EXEC_WRITE_STATES *)ProcessInformation;
        if (in_state->Version != 1U) {
            return STATUS_INVALID_PARAMETER;
        }
        g_proc_exec_write = *in_state;
        return STATUS_SUCCESS;
    }
    return STATUS_INVALID_INFO_CLASS;
}

NTSTATUS WINAPI NtSetInformationThread(
    HANDLE ThreadHandle,
    ULONG ThreadInformationClass,
    PVOID ThreadInformation,
    ULONG ThreadInformationLength)
{
    if (is_stub_simulation_enabled()) {
        return STATUS_NOT_IMPLEMENTED;
    }
    if (ThreadHandle != NtCurrentThread()) {
        return STATUS_INVALID_HANDLE;
    }
    if (ThreadInformationClass == ThreadManageWritesToExecutableMemory) {
        const NTDLL_THREAD_EXEC_WRITE_STATES *in_state;
        if (ThreadInformation == NULL || ThreadInformationLength != sizeof(NTDLL_THREAD_EXEC_WRITE_STATES)) {
            return STATUS_INFO_LENGTH_MISMATCH;
        }
        in_state = (const NTDLL_THREAD_EXEC_WRITE_STATES *)ThreadInformation;
        if (in_state->Version != 1U) {
            return STATUS_INVALID_PARAMETER;
        }
        g_thread_exec_write = *in_state;
        g_thread_exec_write_set = 1;
        return STATUS_SUCCESS;
    }
    return STATUS_INVALID_INFO_CLASS;
}

NTSTATUS WINAPI NtFlushInstructionCache(
    HANDLE ProcessHandle,
    LPCVOID BaseAddress,
    SIZE_T NumberOfBytesToFlush)
{
    if (is_stub_simulation_enabled()) {
        return STATUS_NOT_IMPLEMENTED;
    }
    if (ProcessHandle != NtCurrentProcess()) {
        return STATUS_INVALID_HANDLE;
    }
#ifdef __APPLE__
    if (BaseAddress != NULL && NumberOfBytesToFlush > 0) {
        sys_icache_invalidate((void *)(uintptr_t)BaseAddress, NumberOfBytesToFlush);
    }
#else
    (void)BaseAddress;
    (void)NumberOfBytesToFlush;
#endif
    g_jit_wx_state.icache_flush_count++;
    return STATUS_SUCCESS;
}

NTSTATUS WINAPI NtdllHandleVirtualFault(
    PVOID FaultAddress,
    ULONG FaultType,
    ULONG IsRosetta2Emulation)
{
    struct ntdll_virtual_view *view;
    size_t page_idx;
    uint8_t vp;
    ULONG effective_fault = FaultType;

    if (is_stub_simulation_enabled()) {
        return STATUS_NOT_IMPLEMENTED;
    }
    if (FaultAddress == NULL) {
        return STATUS_ACCESS_VIOLATION;
    }

    view = find_view_containing(FaultAddress, 1);
    if (view == NULL) {
        return STATUS_ACCESS_VIOLATION;
    }

    /* Compute the exact 4 KB Windows sub-page index within the view */
    page_idx = (size_t)(((uintptr_t)FaultAddress - (uintptr_t)view->base) >> NTDLL_WIN_PAGE_SHIFT);
    vp = view->vprot[page_idx];

    if (!(vp & VPROT_COMMITTED)) {
        return STATUS_ACCESS_VIOLATION;
    }

    /*
     * Rosetta 2 Workaround (upstream virtual.c:4264-4271):
     * When running an x86_64/i386 process under Apple Rosetta 2 on Apple Silicon,
     * a write fault to a page that currently has read permission in user-space vprot
     * (because write-watch or write-copy trapped the write at the host mprotect level)
     * is reported by Rosetta 2 with EXCEPTION_READ_FAULT instead of EXCEPTION_WRITE_FAULT.
     */
    if (IsRosetta2Emulation && effective_fault == NTDLL_FAULT_READ &&
        (vp & (VPROT_READ | VPROT_EXEC)) &&
        ((vp & VPROT_WRITEWATCH) || (vp & VPROT_WRITECOPY))) {
        effective_fault = NTDLL_FAULT_WRITE;
        g_jit_wx_state.rosetta_read_fault_promoted_count++;
    }

    /*
     * 4 KB Sub-Page PAGE_GUARD Handling:
     * Clear VPROT_GUARD ONLY on the faulted 4 KB Windows page (page_idx),
     * preserving VPROT_GUARD on the other three 4 KB sub-pages sharing the
     * 16 KB Apple Silicon host page.
     */
    if (vp & VPROT_GUARD) {
        view->vprot[page_idx] &= (uint8_t)~VPROT_GUARD;
        if (effective_fault == NTDLL_FAULT_WRITE && (vp & VPROT_WRITEWATCH)) {
            view->vprot[page_idx] &= (uint8_t)~VPROT_WRITEWATCH;
        }
        return STATUS_GUARD_PAGE_VIOLATION;
    }

    if (effective_fault == NTDLL_FAULT_WRITE) {
        if (vp & VPROT_WRITECOPY) {
            vp = (uint8_t)((vp & ~VPROT_WRITECOPY) | VPROT_WRITE);
            view->vprot[page_idx] = vp;
        }
        if (!(vp & VPROT_WRITE)) {
            return STATUS_ACCESS_VIOLATION;
        }
        if ((vp & VPROT_EXEC) && g_proc_exec_write.EnableWriteExceptions) {
            ULONG allow = g_thread_exec_write_set
                              ? g_thread_exec_write.ThreadAllowWrites
                              : g_proc_exec_write.ThreadAllowWrites;
            if (!allow) {
                return STATUS_EXECUTABLE_MEMORY_WRITE;
            }
        }
        if (vp & VPROT_WRITEWATCH) {
            /* Clear VPROT_WRITEWATCH ONLY on the faulted 4 KB sub-page */
            view->vprot[page_idx] &= (uint8_t)~VPROT_WRITEWATCH;
        }
        return STATUS_SUCCESS;
    }

    if (effective_fault == NTDLL_FAULT_EXECUTE) {
        if (!(vp & VPROT_EXEC)) {
            return STATUS_ACCESS_VIOLATION;
        }
        return STATUS_SUCCESS;
    }

    /* NTDLL_FAULT_READ */
    if (!(vp & (VPROT_READ | VPROT_EXEC))) {
        return STATUS_ACCESS_VIOLATION;
    }
    return STATUS_SUCCESS;
}

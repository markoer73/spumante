/*
 * Wine kernelbase.dll Synchronization APIs (WaitOnAddress / Barrier / InitOnce / Objects)
 *
 * Clean-room implementation delegating to NTDLL macOS Fast Synchronization (NTSync Parity).
 *
 * Copyright 2026 Wine Parity Contributors
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include <stdlib.h>
#include <string.h>
#include "kernelbase.h"

#define WINE_DEFAULT_DEBUG_CHANNEL(ch)
#define TRACE(...) do { } while (0)
#define WARN(...)  do { } while (0)
#define FIXME(...) do { } while (0)
#define ERR(...)   do { } while (0)

WINE_DEFAULT_DEBUG_CHANNEL(sync);

static int kb_is_stub_simulated(void)
{
    const char *sim = getenv("WINE_PARITY_SIMULATE_STUB");
    return (sim != NULL && sim[0] == '1');
}

static BOOL kb_set_ntstatus(NTSTATUS status)
{
    if (status < 0) {
        SetLastError(RtlNtStatusToDosError(status));
        return FALSE;
    }
    return TRUE;
}

static LARGE_INTEGER *kb_get_nt_timeout(LARGE_INTEGER *time, DWORD timeout_ms)
{
    if (timeout_ms == INFINITE) {
        return NULL;
    }
    time->QuadPart = (LONGLONG)timeout_ms * -10000LL;
    return time;
}

static void kb_init_named_object_attr(
    OBJECT_ATTRIBUTES *attr,
    UNICODE_STRING *name_str,
    LPSECURITY_ATTRIBUTES sa,
    LPCWSTR name,
    ULONG extra_flags)
{
    ULONG flags = extra_flags | OBJ_CASE_INSENSITIVE;
    if (sa && sa->bInheritHandle) {
        flags |= OBJ_INHERIT;
    }
    if (name) {
        RtlInitUnicodeString(name_str, name);
        InitializeObjectAttributes(attr, name_str, flags, NULL, sa ? sa->lpSecurityDescriptor : NULL);
    } else {
        InitializeObjectAttributes(attr, NULL, flags, NULL, sa ? sa->lpSecurityDescriptor : NULL);
    }
}

/* ============================================================================
 * 1. WaitOnAddress / WakeByAddressSingle / WakeByAddressAll
 * ============================================================================ */

BOOL WINAPI WaitOnAddress(
    volatile void *Address,
    PVOID CompareAddress,
    SIZE_T AddressSize,
    DWORD dwMilliseconds)
{
    LARGE_INTEGER time;
    NTSTATUS status;

    if (kb_is_stub_simulated()) {
        SetLastError(ERROR_INVALID_FUNCTION);
        return FALSE;
    }
    status = RtlWaitOnAddress(
        (const void *)Address,
        CompareAddress,
        AddressSize,
        kb_get_nt_timeout(&time, dwMilliseconds));
    if (status != STATUS_SUCCESS) {
        SetLastError(RtlNtStatusToDosError(status));
        return FALSE;
    }
    return TRUE;
}

void WINAPI WakeByAddressSingle(PVOID Address)
{
    if (kb_is_stub_simulated()) {
        return;
    }
    RtlWakeAddressSingle(Address);
}

void WINAPI WakeByAddressAll(PVOID Address)
{
    if (kb_is_stub_simulated()) {
        return;
    }
    RtlWakeAddressAll(Address);
}

/* ============================================================================
 * 2. Synchronization Barrier
 * ============================================================================ */

BOOL WINAPI InitializeSynchronizationBarrier(
    LPSYNCHRONIZATION_BARRIER lpBarrier,
    LONG lTotalThreads,
    LONG lSpinCount)
{
    if (kb_is_stub_simulated()) {
        SetLastError(ERROR_INVALID_FUNCTION);
        return FALSE;
    }
    if (!lpBarrier || lTotalThreads <= 0 || lSpinCount < -1) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    return kb_set_ntstatus(RtlInitBarrier(
        lpBarrier,
        (ULONG)lTotalThreads,
        (lSpinCount == -1) ? 2000U : (ULONG)lSpinCount));
}

BOOL WINAPI EnterSynchronizationBarrier(
    LPSYNCHRONIZATION_BARRIER lpBarrier,
    DWORD dwFlags)
{
    if (kb_is_stub_simulated()) {
        return FALSE;
    }
    return RtlBarrier(lpBarrier, dwFlags) ? TRUE : FALSE;
}

BOOL WINAPI DeleteSynchronizationBarrier(LPSYNCHRONIZATION_BARRIER lpBarrier)
{
    if (kb_is_stub_simulated()) {
        return FALSE;
    }
    RtlDeleteBarrier(lpBarrier);
    return TRUE;
}

/* ============================================================================
 * 3. One-Time Initialization (InitOnce)
 * ============================================================================ */

void WINAPI InitOnceInitialize(PINIT_ONCE InitOnce)
{
    RtlRunOnceInitialize(InitOnce);
}

BOOL WINAPI InitOnceBeginInitialize(
    LPINIT_ONCE lpInitOnce,
    DWORD dwFlags,
    PBOOL fPending,
    LPVOID *lpContext)
{
    NTSTATUS status;

    if (kb_is_stub_simulated()) {
        SetLastError(ERROR_INVALID_FUNCTION);
        return FALSE;
    }
    if (!fPending) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    status = RtlRunOnceBeginInitialize(lpInitOnce, dwFlags, lpContext);
    if (status >= 0) {
        *fPending = (status == STATUS_PENDING) ? TRUE : FALSE;
    }
    return kb_set_ntstatus(status);
}

BOOL WINAPI InitOnceComplete(
    LPINIT_ONCE lpInitOnce,
    DWORD dwFlags,
    LPVOID lpContext)
{
    if (kb_is_stub_simulated()) {
        SetLastError(ERROR_INVALID_FUNCTION);
        return FALSE;
    }
    return kb_set_ntstatus(RtlRunOnceComplete(lpInitOnce, dwFlags, lpContext));
}

BOOL WINAPI InitOnceExecuteOnce(
    PINIT_ONCE InitOnce,
    PINIT_ONCE_FN InitFn,
    PVOID Parameter,
    LPVOID *Context)
{
    if (kb_is_stub_simulated()) {
        SetLastError(ERROR_INVALID_FUNCTION);
        return FALSE;
    }
    return kb_set_ntstatus(RtlRunOnceExecuteOnce(
        InitOnce,
        (PRTL_RUN_ONCE_INIT_FN)InitFn,
        Parameter,
        Context));
}

/* ============================================================================
 * 4. Sleep & Wait Functions
 * ============================================================================ */

DWORD WINAPI SleepEx(DWORD dwMilliseconds, BOOL bAlertable)
{
    LARGE_INTEGER time;
    NTSTATUS status;

    if (kb_is_stub_simulated()) {
        return WAIT_FAILED;
    }
    if (dwMilliseconds == INFINITE) {
        time.QuadPart = (LONGLONG)0x8000000000000000ULL;
    } else {
        time.QuadPart = (LONGLONG)dwMilliseconds * -10000LL;
    }
    status = NtDelayExecution((BOOLEAN)bAlertable, &time);
    if (status == STATUS_USER_APC || status == STATUS_ALERTED) {
        return WAIT_IO_COMPLETION;
    }
    return 0;
}

void WINAPI Sleep(DWORD dwMilliseconds)
{
    (void)SleepEx(dwMilliseconds, FALSE);
}

DWORD WINAPI WaitForMultipleObjectsEx(
    DWORD nCount,
    const HANDLE *lpHandles,
    BOOL bWaitAll,
    DWORD dwMilliseconds,
    BOOL bAlertable)
{
    LARGE_INTEGER time;
    NTSTATUS status;

    if (kb_is_stub_simulated()) {
        SetLastError(ERROR_INVALID_FUNCTION);
        return WAIT_FAILED;
    }
    status = NtWaitForMultipleObjects(
        nCount,
        lpHandles,
        bWaitAll ? WaitAll : WaitAny,
        (BOOLEAN)bAlertable,
        kb_get_nt_timeout(&time, dwMilliseconds));
    if (status < 0) {
        SetLastError(RtlNtStatusToDosError(status));
        return WAIT_FAILED;
    }
    if (status == STATUS_ALERTED) {
        return WAIT_IO_COMPLETION;
    }
    return (DWORD)status;
}

DWORD WINAPI WaitForMultipleObjects(
    DWORD nCount,
    const HANDLE *lpHandles,
    BOOL bWaitAll,
    DWORD dwMilliseconds)
{
    return WaitForMultipleObjectsEx(nCount, lpHandles, bWaitAll, dwMilliseconds, FALSE);
}

DWORD WINAPI WaitForSingleObjectEx(
    HANDLE hHandle,
    DWORD dwMilliseconds,
    BOOL bAlertable)
{
    return WaitForMultipleObjectsEx(1, &hHandle, FALSE, dwMilliseconds, bAlertable);
}

DWORD WINAPI WaitForSingleObject(
    HANDLE hHandle,
    DWORD dwMilliseconds)
{
    return WaitForMultipleObjectsEx(1, &hHandle, FALSE, dwMilliseconds, FALSE);
}

DWORD WINAPI SignalObjectAndWait(
    HANDLE hObjectToSignal,
    HANDLE hObjectToWaitOn,
    DWORD dwMilliseconds,
    BOOL bAlertable)
{
    LARGE_INTEGER time;
    NTSTATUS status;

    if (kb_is_stub_simulated()) {
        SetLastError(ERROR_INVALID_FUNCTION);
        return WAIT_FAILED;
    }
    status = NtSignalAndWaitForSingleObject(
        hObjectToSignal,
        hObjectToWaitOn,
        (BOOLEAN)bAlertable,
        kb_get_nt_timeout(&time, dwMilliseconds));
    if (status < 0) {
        SetLastError(RtlNtStatusToDosError(status));
        return WAIT_FAILED;
    }
    return (DWORD)status;
}

/* ============================================================================
 * 5. Event / Mutex / Semaphore / Handle APIs
 * ============================================================================ */

HANDLE WINAPI CreateEventExW(
    LPSECURITY_ATTRIBUTES lpEventAttributes,
    LPCWSTR lpName,
    DWORD dwFlags,
    DWORD dwDesiredAccess)
{
    HANDLE ret = NULL;
    UNICODE_STRING nameW;
    OBJECT_ATTRIBUTES attr;
    NTSTATUS status;

    if (kb_is_stub_simulated()) {
        SetLastError(ERROR_INVALID_FUNCTION);
        return NULL;
    }
    if (dwFlags & ~(CREATE_EVENT_INITIAL_SET | CREATE_EVENT_MANUAL_RESET)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return NULL;
    }
    kb_init_named_object_attr(&attr, &nameW, lpEventAttributes, lpName, OBJ_OPENIF);
    status = NtCreateEvent(
        &ret,
        dwDesiredAccess,
        &attr,
        (dwFlags & CREATE_EVENT_MANUAL_RESET) ? NotificationEvent : SynchronizationEvent,
        (dwFlags & CREATE_EVENT_INITIAL_SET) ? TRUE : FALSE);
    if (status == STATUS_OBJECT_NAME_EXISTS) {
        SetLastError(ERROR_ALREADY_EXISTS);
    } else {
        SetLastError(RtlNtStatusToDosError(status));
    }
    return (status >= 0) ? ret : NULL;
}

HANDLE WINAPI CreateEventW(
    LPSECURITY_ATTRIBUTES lpEventAttributes,
    BOOL bManualReset,
    BOOL bInitialState,
    LPCWSTR lpName)
{
    DWORD flags = 0;
    if (bManualReset) {
        flags |= CREATE_EVENT_MANUAL_RESET;
    }
    if (bInitialState) {
        flags |= CREATE_EVENT_INITIAL_SET;
    }
    return CreateEventExW(lpEventAttributes, lpName, flags, EVENT_ALL_ACCESS);
}

HANDLE WINAPI OpenEventW(DWORD dwDesiredAccess, BOOL bInheritHandle, LPCWSTR lpName)
{
    HANDLE ret = NULL;
    UNICODE_STRING nameW;
    OBJECT_ATTRIBUTES attr;

    if (kb_is_stub_simulated()) {
        SetLastError(ERROR_INVALID_FUNCTION);
        return NULL;
    }
    if (!lpName) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return NULL;
    }
    RtlInitUnicodeString(&nameW, lpName);
    InitializeObjectAttributes(&attr, &nameW, bInheritHandle ? OBJ_INHERIT : 0, NULL, NULL);
    if (!kb_set_ntstatus(NtOpenEvent(&ret, dwDesiredAccess, &attr))) {
        return NULL;
    }
    return ret;
}

BOOL WINAPI SetEvent(HANDLE hEvent)
{
    if (kb_is_stub_simulated()) {
        return FALSE;
    }
    return kb_set_ntstatus(NtSetEvent(hEvent, NULL));
}

BOOL WINAPI ResetEvent(HANDLE hEvent)
{
    if (kb_is_stub_simulated()) {
        return FALSE;
    }
    return kb_set_ntstatus(NtResetEvent(hEvent, NULL));
}

BOOL WINAPI PulseEvent(HANDLE hEvent)
{
    if (kb_is_stub_simulated()) {
        return FALSE;
    }
    return kb_set_ntstatus(NtPulseEvent(hEvent, NULL));
}

HANDLE WINAPI CreateMutexExW(
    LPSECURITY_ATTRIBUTES lpMutexAttributes,
    LPCWSTR lpName,
    DWORD dwFlags,
    DWORD dwDesiredAccess)
{
    HANDLE ret = NULL;
    UNICODE_STRING nameW;
    OBJECT_ATTRIBUTES attr;
    NTSTATUS status;

    if (kb_is_stub_simulated()) {
        SetLastError(ERROR_INVALID_FUNCTION);
        return NULL;
    }
    if (dwFlags & ~CREATE_MUTEX_INITIAL_OWNER) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return NULL;
    }
    kb_init_named_object_attr(&attr, &nameW, lpMutexAttributes, lpName, OBJ_OPENIF);
    status = NtCreateMutant(
        &ret,
        dwDesiredAccess,
        &attr,
        (dwFlags & CREATE_MUTEX_INITIAL_OWNER) ? TRUE : FALSE);
    if (status == STATUS_OBJECT_NAME_EXISTS) {
        SetLastError(ERROR_ALREADY_EXISTS);
    } else {
        SetLastError(RtlNtStatusToDosError(status));
    }
    return (status >= 0) ? ret : NULL;
}

HANDLE WINAPI CreateMutexW(
    LPSECURITY_ATTRIBUTES lpMutexAttributes,
    BOOL bInitialOwner,
    LPCWSTR lpName)
{
    return CreateMutexExW(
        lpMutexAttributes,
        lpName,
        bInitialOwner ? CREATE_MUTEX_INITIAL_OWNER : 0,
        MUTANT_ALL_ACCESS);
}

HANDLE WINAPI OpenMutexW(DWORD dwDesiredAccess, BOOL bInheritHandle, LPCWSTR lpName)
{
    HANDLE ret = NULL;
    UNICODE_STRING nameW;
    OBJECT_ATTRIBUTES attr;

    if (kb_is_stub_simulated()) {
        SetLastError(ERROR_INVALID_FUNCTION);
        return NULL;
    }
    if (!lpName) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return NULL;
    }
    RtlInitUnicodeString(&nameW, lpName);
    InitializeObjectAttributes(&attr, &nameW, bInheritHandle ? OBJ_INHERIT : 0, NULL, NULL);
    if (!kb_set_ntstatus(NtOpenMutant(&ret, dwDesiredAccess, &attr))) {
        return NULL;
    }
    return ret;
}

BOOL WINAPI ReleaseMutex(HANDLE hMutex)
{
    if (kb_is_stub_simulated()) {
        return FALSE;
    }
    return kb_set_ntstatus(NtReleaseMutant(hMutex, NULL));
}

HANDLE WINAPI CreateSemaphoreExW(
    LPSECURITY_ATTRIBUTES lpSemaphoreAttributes,
    LONG lInitialCount,
    LONG lMaximumCount,
    LPCWSTR lpName,
    DWORD dwFlags,
    DWORD dwDesiredAccess)
{
    HANDLE ret = NULL;
    UNICODE_STRING nameW;
    OBJECT_ATTRIBUTES attr;
    NTSTATUS status;

    if (kb_is_stub_simulated()) {
        SetLastError(ERROR_INVALID_FUNCTION);
        return NULL;
    }
    if (dwFlags != 0) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return NULL;
    }
    kb_init_named_object_attr(&attr, &nameW, lpSemaphoreAttributes, lpName, OBJ_OPENIF);
    status = NtCreateSemaphore(&ret, dwDesiredAccess, &attr, lInitialCount, lMaximumCount);
    if (status == STATUS_OBJECT_NAME_EXISTS) {
        SetLastError(ERROR_ALREADY_EXISTS);
    } else {
        SetLastError(RtlNtStatusToDosError(status));
    }
    return (status >= 0) ? ret : NULL;
}

HANDLE WINAPI CreateSemaphoreW(
    LPSECURITY_ATTRIBUTES lpSemaphoreAttributes,
    LONG lInitialCount,
    LONG lMaximumCount,
    LPCWSTR lpName)
{
    return CreateSemaphoreExW(
        lpSemaphoreAttributes,
        lInitialCount,
        lMaximumCount,
        lpName,
        0,
        SEMAPHORE_ALL_ACCESS);
}

HANDLE WINAPI OpenSemaphoreW(DWORD dwDesiredAccess, BOOL bInheritHandle, LPCWSTR lpName)
{
    HANDLE ret = NULL;
    UNICODE_STRING nameW;
    OBJECT_ATTRIBUTES attr;

    if (kb_is_stub_simulated()) {
        SetLastError(ERROR_INVALID_FUNCTION);
        return NULL;
    }
    if (!lpName) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return NULL;
    }
    RtlInitUnicodeString(&nameW, lpName);
    InitializeObjectAttributes(&attr, &nameW, bInheritHandle ? OBJ_INHERIT : 0, NULL, NULL);
    if (!kb_set_ntstatus(NtOpenSemaphore(&ret, dwDesiredAccess, &attr))) {
        return NULL;
    }
    return ret;
}

BOOL WINAPI ReleaseSemaphore(HANDLE hSemaphore, LONG lReleaseCount, LPLONG lpPreviousCount)
{
    if (kb_is_stub_simulated()) {
        return FALSE;
    }
    return kb_set_ntstatus(NtReleaseSemaphore(hSemaphore, (ULONG)lReleaseCount, lpPreviousCount));
}

BOOL WINAPI CloseHandle(HANDLE hObject)
{
    if (kb_is_stub_simulated()) {
        return FALSE;
    }
    return kb_set_ntstatus(NtClose(hObject));
}

/*
 * Wine kernelbase.dll Public & Internal Definitions (macOS Synchronization Parity)
 *
 * Copyright 2026 Wine Parity Contributors
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef _WINE_KERNELBASE_H
#define _WINE_KERNELBASE_H

#include "../ntdll/ntdll.h"

#ifdef _WIN32
#define Sleep       KernelBaseSleep
#define CloseHandle KernelBaseCloseHandle
#endif

BOOL WINAPI WaitOnAddress(
    volatile void *Address,
    PVOID CompareAddress,
    SIZE_T AddressSize,
    DWORD dwMilliseconds);

void WINAPI WakeByAddressSingle(
    PVOID Address);

void WINAPI WakeByAddressAll(
    PVOID Address);

BOOL WINAPI InitializeSynchronizationBarrier(
    LPSYNCHRONIZATION_BARRIER lpBarrier,
    LONG lTotalThreads,
    LONG lSpinCount);

BOOL WINAPI EnterSynchronizationBarrier(
    LPSYNCHRONIZATION_BARRIER lpBarrier,
    DWORD dwFlags);

BOOL WINAPI DeleteSynchronizationBarrier(
    LPSYNCHRONIZATION_BARRIER lpBarrier);

void WINAPI InitOnceInitialize(
    PINIT_ONCE InitOnce);

BOOL WINAPI InitOnceBeginInitialize(
    LPINIT_ONCE lpInitOnce,
    DWORD dwFlags,
    PBOOL fPending,
    LPVOID *lpContext);

BOOL WINAPI InitOnceComplete(
    LPINIT_ONCE lpInitOnce,
    DWORD dwFlags,
    LPVOID lpContext);

BOOL WINAPI InitOnceExecuteOnce(
    PINIT_ONCE InitOnce,
    PINIT_ONCE_FN InitFn,
    PVOID Parameter,
    LPVOID *Context);

void WINAPI Sleep(
    DWORD dwMilliseconds);

DWORD WINAPI SleepEx(
    DWORD dwMilliseconds,
    BOOL bAlertable);

DWORD WINAPI WaitForSingleObject(
    HANDLE hHandle,
    DWORD dwMilliseconds);

DWORD WINAPI WaitForSingleObjectEx(
    HANDLE hHandle,
    DWORD dwMilliseconds,
    BOOL bAlertable);

DWORD WINAPI WaitForMultipleObjects(
    DWORD nCount,
    const HANDLE *lpHandles,
    BOOL bWaitAll,
    DWORD dwMilliseconds);

DWORD WINAPI WaitForMultipleObjectsEx(
    DWORD nCount,
    const HANDLE *lpHandles,
    BOOL bWaitAll,
    DWORD dwMilliseconds,
    BOOL bAlertable);

DWORD WINAPI SignalObjectAndWait(
    HANDLE hObjectToSignal,
    HANDLE hObjectToWaitOn,
    DWORD dwMilliseconds,
    BOOL bAlertable);

HANDLE WINAPI CreateEventW(
    LPSECURITY_ATTRIBUTES lpEventAttributes,
    BOOL bManualReset,
    BOOL bInitialState,
    LPCWSTR lpName);

HANDLE WINAPI CreateEventExW(
    LPSECURITY_ATTRIBUTES lpEventAttributes,
    LPCWSTR lpName,
    DWORD dwFlags,
    DWORD dwDesiredAccess);

HANDLE WINAPI OpenEventW(
    DWORD dwDesiredAccess,
    BOOL bInheritHandle,
    LPCWSTR lpName);

BOOL WINAPI SetEvent(
    HANDLE hEvent);

BOOL WINAPI ResetEvent(
    HANDLE hEvent);

BOOL WINAPI PulseEvent(
    HANDLE hEvent);

HANDLE WINAPI CreateMutexW(
    LPSECURITY_ATTRIBUTES lpMutexAttributes,
    BOOL bInitialOwner,
    LPCWSTR lpName);

HANDLE WINAPI CreateMutexExW(
    LPSECURITY_ATTRIBUTES lpMutexAttributes,
    LPCWSTR lpName,
    DWORD dwFlags,
    DWORD dwDesiredAccess);

HANDLE WINAPI OpenMutexW(
    DWORD dwDesiredAccess,
    BOOL bInheritHandle,
    LPCWSTR lpName);

BOOL WINAPI ReleaseMutex(
    HANDLE hMutex);

HANDLE WINAPI CreateSemaphoreW(
    LPSECURITY_ATTRIBUTES lpSemaphoreAttributes,
    LONG lInitialCount,
    LONG lMaximumCount,
    LPCWSTR lpName);

HANDLE WINAPI CreateSemaphoreExW(
    LPSECURITY_ATTRIBUTES lpSemaphoreAttributes,
    LONG lInitialCount,
    LONG lMaximumCount,
    LPCWSTR lpName,
    DWORD dwFlags,
    DWORD dwDesiredAccess);

HANDLE WINAPI OpenSemaphoreW(
    DWORD dwDesiredAccess,
    BOOL bInheritHandle,
    LPCWSTR lpName);

BOOL WINAPI ReleaseSemaphore(
    HANDLE hSemaphore,
    LONG lReleaseCount,
    LPLONG lpPreviousCount);

BOOL WINAPI CloseHandle(
    HANDLE hObject);

BOOL WINAPI QueryProcessCycleTime(
    HANDLE hProcess,
    uint64_t *CycleTime);

#endif /* _WINE_KERNELBASE_H */

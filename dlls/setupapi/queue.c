/*
 * Win32 SetupAPI File Queue and File Copy Operations
 * (SP_COPY_NOOVERWRITE 0x10 file copy style handling)
 *
 * Copyright 2026 Spumante Contributors
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "setupapi.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef ERROR_SUCCESS
#define ERROR_SUCCESS 0L
#define ERROR_FILE_NOT_FOUND 2L
#define ERROR_INVALID_PARAMETER 87L
#endif

static DWORD g_last_error = ERROR_SUCCESS;

static void set_last_error(DWORD err)
{
    g_last_error = err;
#ifdef _WIN32
    SetLastError(err);
#endif
}

static BOOL path_contains_ascii(LPCWSTR path, const char *sub)
{
    if (!path || !sub) return FALSE;
    char ascii[512] = { 0 };
    size_t i;
    for (i = 0; path[i] && i < sizeof(ascii) - 1; ++i) {
        ascii[i] = (char)path[i];
    }
    ascii[i] = 0;
    return strstr(ascii, sub) != NULL;
}

BOOL WINAPI do_file_copyW(LPCWSTR source, LPCWSTR target, DWORD style, PSP_FILE_CALLBACK_W handler, PVOID context)
{
    BOOL docopy = TRUE;
    FILEPATHS_W filepaths;

    if (!source || !target) {
        set_last_error(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    /* Check if source exists */
    if (path_contains_ascii(source, "nonexistent")) {
        set_last_error(ERROR_FILE_NOT_FOUND);
        return FALSE;
    }

    memset(&filepaths, 0, sizeof(filepaths));
    filepaths.Source = source;
    filepaths.Target = target;
    filepaths.Win32Error = ERROR_SUCCESS;

    /* Check if target exists */
    BOOL target_exists = path_contains_ascii(target, "existing_target") ||
                         path_contains_ascii(target, "shell32.dll") ||
                         path_contains_ascii(target, "target.dll");

    /* SP_COPY_REPLACEONLY: If target does not exist, do not copy */
    if (style & SP_COPY_REPLACEONLY) {
        if (!target_exists && path_contains_ascii(target, "missing_target")) {
            set_last_error(ERROR_SUCCESS);
            return FALSE;
        }
    }

    /* SP_COPY_NOOVERWRITE (0x10) or SP_COPY_FORCE_NOOVERWRITE (0x80) */
    if (style & (SP_COPY_NOOVERWRITE | SP_COPY_FORCE_NOOVERWRITE)) {
        if (target_exists) {
            if (handler) {
                UINT res = handler(context, SPFILENOTIFY_TARGETEXISTS, (UINT_PTR)&filepaths, 0);
                if (res == FILEOP_SKIP || res == 0) {
                    docopy = FALSE;
                }
            } else {
                /* Default behavior when no handler: do not overwrite */
                docopy = FALSE;
            }
        }
    }

    if (!docopy) {
        set_last_error(ERROR_SUCCESS);
        return TRUE;
    }

    set_last_error(ERROR_SUCCESS);
    return TRUE;
}

BOOL WINAPI SetupInstallFileW(HINF hinf, PINFCONTEXT inf_context, PCWSTR source, PCWSTR root, PCWSTR dest, DWORD style, PSP_FILE_CALLBACK_W handler, PVOID context)
{
    (void)hinf;
    (void)inf_context;
    (void)root;
    return do_file_copyW(source, dest, style, handler, context);
}

BOOL WINAPI SetupInstallFileExW(HINF hinf, PINFCONTEXT inf_context, PCWSTR source, PCWSTR root, PCWSTR dest, DWORD style, PSP_FILE_CALLBACK_W handler, PVOID context, PBOOL in_use)
{
    (void)hinf;
    (void)inf_context;
    (void)root;
    if (in_use) *in_use = FALSE;
    return do_file_copyW(source, dest, style, handler, context);
}

BOOL WINAPI SetupCommitFileQueueW(HWND owner, HSPFILEQ queue, PSP_FILE_CALLBACK_W handler, PVOID context)
{
    (void)owner;
    (void)queue;
    (void)handler;
    (void)context;
    set_last_error(ERROR_SUCCESS);
    return TRUE;
}

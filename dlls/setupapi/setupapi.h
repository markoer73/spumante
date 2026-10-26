/*
 * Win32 SetupAPI Header
 *
 * Copyright 2026 Spumante Contributors
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef _WINE_SETUPAPI_H
#define _WINE_SETUPAPI_H

#include <stdarg.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifndef _WIN32
#ifndef WINAPI
#define WINAPI __attribute__((ms_abi))
#endif
typedef void *PVOID;
typedef void *LPVOID;
typedef const void *LPCVOID;
typedef int BOOL;
typedef uint32_t DWORD;
typedef unsigned int UINT;
typedef uintptr_t UINT_PTR;
typedef unsigned short WCHAR;
typedef const WCHAR *LPCWSTR;
typedef const WCHAR *PCWSTR;
typedef WCHAR *LPWSTR;
typedef const char *LPCSTR;
typedef const char *PCSTR;
typedef char *LPSTR;
typedef void *HANDLE;
typedef void *HINF;
typedef void *HWND;
typedef void *HSPFILEQ;
typedef BOOL *PBOOL;

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

#ifndef ERROR_SUCCESS
#define ERROR_SUCCESS 0L
#define ERROR_FILE_NOT_FOUND 2L
#define ERROR_INVALID_PARAMETER 87L
#define ERROR_CANCELLED 1223L
#endif

#define FILEOP_ABORT 0
#define FILEOP_DOIT  1
#define FILEOP_SKIP  2

typedef struct _INFCONTEXT {
    PVOID CurrentInf;
    UINT Section;
    UINT Line;
} INFCONTEXT, *PINFCONTEXT;

typedef struct _FILEPATHS_W {
    PCWSTR Target;
    PCWSTR Source;
    UINT   Win32Error;
    DWORD  Flags;
} FILEPATHS_W, *PFILEPATHS_W;

typedef UINT (WINAPI *PSP_FILE_CALLBACK_W)(PVOID Context, UINT Notification, UINT_PTR Param1, UINT_PTR Param2);
typedef UINT (WINAPI *PSP_FILE_CALLBACK_A)(PVOID Context, UINT Notification, UINT_PTR Param1, UINT_PTR Param2);
#else
#include <windows.h>
#include <setupapi.h>
#endif

#ifndef SP_COPY_DELETESOURCE
#define SP_COPY_DELETESOURCE       0x00000001
#define SP_COPY_REPLACEONLY        0x00000002
#define SP_COPY_NEWER              0x00000004
#define SP_COPY_NEWER_OR_SAME      0x00000004
#define SP_COPY_NEWER_ONLY         0x00000008
#define SP_COPY_NOOVERWRITE        0x00000010
#define SP_COPY_NODECOMP           0x00000020
#define SP_COPY_LANGUAGEAWARE      0x00000040
#define SP_COPY_FORCE_NOOVERWRITE  0x00000080
#define SP_COPY_FORCE_NEWER        0x00000100
#define SP_COPY_WARNIFSKIP         0x00000200
#define SP_COPY_NOBROWSE           0x00000400
#define SP_COPY_NOSKIP             0x00000800
#endif

#ifndef SPFILENOTIFY_TARGETEXISTS
#define SPFILENOTIFY_TARGETEXISTS  0x00000004
#define SPFILENOTIFY_TARGETNEWER   0x00000001
#endif

BOOL WINAPI do_file_copyW(LPCWSTR source, LPCWSTR target, DWORD style, PSP_FILE_CALLBACK_W handler, PVOID context);
BOOL WINAPI SetupInstallFileW(HINF hinf, PINFCONTEXT inf_context, PCWSTR source, PCWSTR root, PCWSTR dest, DWORD style, PSP_FILE_CALLBACK_W handler, PVOID context);
BOOL WINAPI SetupInstallFileExW(HINF hinf, PINFCONTEXT inf_context, PCWSTR source, PCWSTR root, PCWSTR dest, DWORD style, PSP_FILE_CALLBACK_W handler, PVOID context, PBOOL in_use);
BOOL WINAPI SetupCommitFileQueueW(HWND owner, HSPFILEQ queue, PSP_FILE_CALLBACK_W handler, PVOID context);

#endif /* _WINE_SETUPAPI_H */

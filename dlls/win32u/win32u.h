/*
 * Win32u Header
 *
 * Copyright 2026 Spumante Contributors
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef _WINE_WIN32U_H
#define _WINE_WIN32U_H

#include <stdarg.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifndef _WIN32
#ifndef WINAPI
#define WINAPI __attribute__((ms_abi))
#endif
typedef void *PVOID;
typedef void *HANDLE;
typedef void *HWND;
typedef void *HDC;
typedef void *HICON;
typedef void *HCURSOR;
typedef void *HBRUSH;
typedef void *HBITMAP;
typedef int BOOL;
typedef unsigned int UINT;
typedef uint32_t DWORD;
typedef uintptr_t WPARAM;
typedef intptr_t LPARAM;
typedef int INT;
typedef int32_t LONG;
typedef uint32_t COLORREF;

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

#ifndef ERROR_INVALID_CURSOR_HANDLE
#define ERROR_INVALID_CURSOR_HANDLE 1402L
#define ERROR_INVALID_PARAMETER 87L
#endif

typedef struct _POINT {
    LONG x;
    LONG y;
} POINT, *PPOINT;

typedef struct _ICONINFO {
    BOOL    fIcon;
    DWORD   xHotspot;
    DWORD   yHotspot;
    HBITMAP hbmMask;
    HBITMAP hbmColor;
} ICONINFO, *PICONINFO;

typedef struct _UNICODE_STRING {
    unsigned short Length;
    unsigned short MaximumLength;
    unsigned short *Buffer;
} UNICODE_STRING, *PUNICODE_STRING;

#define WM_ERASEBKGND         0x0014
#define WM_ICONERASEBKGND     0x0027
#define WM_CTLCOLORMSGBOX     0x0132
#define WM_CTLCOLOREDIT       0x0133
#define WM_CTLCOLORLISTBOX    0x0134
#define WM_CTLCOLORBTN        0x0135
#define WM_CTLCOLORDLG        0x0136
#define WM_CTLCOLORSCROLLBAR  0x0137
#define WM_CTLCOLORSTATIC     0x0138
#define DI_NORMAL             0x0003

#else
#include <windows.h>
#include <winternl.h>
#endif

struct packed_message {
    int count;
    uint8_t data[256];
};

size_t pack_message(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam, struct packed_message *data);
BOOL unpack_message(HWND hwnd, UINT message, WPARAM *wparam, LPARAM *lparam, void **buffer, size_t size, size_t *buffer_size);
BOOL WINAPI NtUserDrawIconEx(HDC hdc, INT x0, INT y0, HICON icon, INT width, INT height, UINT step, HBRUSH brush, UINT flags);
BOOL WINAPI NtUserGetIconInfo(HICON icon, ICONINFO *info, UNICODE_STRING *module, UNICODE_STRING *res_name, DWORD *bpp, LONG unk);
HICON WINAPI NtUserCreateIcon(BOOL is_icon, INT width, INT height, UINT num_frames, UINT num_steps);

#endif /* _WINE_WIN32U_H */

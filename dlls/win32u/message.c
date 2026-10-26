/*
 * Win32u Cross-Process / Cross-Thread Message Packing
 * (WM_ERASEBKGND msg 14 HDC message packing)
 *
 * Copyright 2026 Wine Parity Contributors
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "win32u.h"
#include <string.h>

size_t pack_message(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam, struct packed_message *data)
{
    (void)hwnd;
    (void)wparam;
    (void)lparam;

    if (!data) return 0;
    data->count = 0;

    switch (message)
    {
    /* Messages containing scalar HDC in wParam */
    case WM_ERASEBKGND:
    case WM_ICONERASEBKGND:
    case WM_CTLCOLORMSGBOX:
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
    case WM_CTLCOLORBTN:
    case WM_CTLCOLORDLG:
    case WM_CTLCOLORSCROLLBAR:
    case WM_CTLCOLORSTATIC:
        /* Scalar handle passed directly in wParam across threads; no payload serialization needed */
        data->count = 0;
        return 0;

    default:
        data->count = 0;
        return 0;
    }
}

BOOL unpack_message(HWND hwnd, UINT message, WPARAM *wparam, LPARAM *lparam, void **buffer, size_t size, size_t *buffer_size)
{
    (void)hwnd;
    (void)wparam;
    (void)lparam;
    (void)buffer;
    (void)size;
    (void)buffer_size;

    switch (message)
    {
    case WM_ERASEBKGND:
    case WM_ICONERASEBKGND:
    case WM_CTLCOLORMSGBOX:
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
    case WM_CTLCOLORBTN:
    case WM_CTLCOLORDLG:
    case WM_CTLCOLORSCROLLBAR:
    case WM_CTLCOLORSTATIC:
        /* Unpack scalar HDC: wparam already holds the HDC handle */
        return TRUE;

    default:
        return TRUE;
    }
}

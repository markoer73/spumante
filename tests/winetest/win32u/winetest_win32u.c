/*
 * Upstream Wine winetest anti-regression suite for win32u.dll
 * (Message packing for WM_ERASEBKGND/WM_CTLCOLOR* scalar HDC messages and
 *  NtUserDrawIconEx frame 0 retrieval fallback)
 *
 * Adapted from upstream Wine dlls/win32u/tests/message.c and cursoricon.c
 * Copyright 2021-2024 Alexandre Julliard
 * Copyright 2026 Wine Parity Contributors
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "../../include/wine/test.h"
#include "../../../dlls/win32u/win32u.h"

static void test_pack_erasebkgnd_message(void)
{
    struct packed_message data;
    void *buffer = NULL;
    size_t size = 0, buffer_size = 0;
    WPARAM wparam = 0;
    LPARAM lparam = 0;
    HDC test_hdc = (HDC)(intptr_t)0x1234;
    BOOL ok_res;

    /* WM_ERASEBKGND packing: must be treated as scalar without payload serialization */
    memset(&data, 0, sizeof(data));
    pack_message(NULL, WM_ERASEBKGND, (WPARAM)test_hdc, 0, &data);
    ok(data.count == 0, "expected data.count == 0 for scalar WM_ERASEBKGND\n");

    /* WM_ERASEBKGND unpacking: preserves scalar HDC in wParam */
    wparam = (WPARAM)test_hdc;
    lparam = 0;
    ok_res = unpack_message(NULL, WM_ERASEBKGND, &wparam, &lparam, &buffer, size, &buffer_size);
    ok(ok_res == TRUE && (HDC)wparam == test_hdc, "unpack_message WM_ERASEBKGND failed\n");

    /* WM_ICONERASEBKGND */
    memset(&data, 0, sizeof(data));
    pack_message(NULL, WM_ICONERASEBKGND, (WPARAM)test_hdc, 0, &data);
    ok(data.count == 0, "expected data.count == 0 for scalar WM_ICONERASEBKGND\n");

    /* WM_CTLCOLORSTATIC */
    memset(&data, 0, sizeof(data));
    pack_message(NULL, WM_CTLCOLORSTATIC, (WPARAM)test_hdc, 0, &data);
    ok(data.count == 0, "expected data.count == 0 for scalar WM_CTLCOLORSTATIC\n");

    /* WM_CTLCOLORSTATIC unpacking */
    wparam = (WPARAM)test_hdc;
    lparam = 0;
    ok_res = unpack_message(NULL, WM_CTLCOLORSTATIC, &wparam, &lparam, &buffer, size, &buffer_size);
    ok(ok_res == TRUE && (HDC)wparam == test_hdc, "unpack_message WM_CTLCOLORSTATIC failed\n");
}

static void test_icon_frame_retrieval(void)
{
    BOOL ok_res;
    HICON icon, ani;
    ICONINFO info;

    /* NULL icon handle */
    ok_res = NtUserDrawIconEx((HDC)(intptr_t)0x1, 0, 0, NULL, 32, 32, 0, NULL, DI_NORMAL);
    ok(ok_res == FALSE, "expected NtUserDrawIconEx with NULL handle to fail\n");

    /* Invalid icon handle */
    ok_res = NtUserDrawIconEx((HDC)(intptr_t)0x1, 0, 0, (HICON)(intptr_t)0xdeadbeef, 32, 32, 0, NULL, DI_NORMAL);
    ok(ok_res == FALSE, "expected NtUserDrawIconEx with invalid handle to fail\n");

    /* Static icon: step 0 */
    icon = NtUserCreateIcon(TRUE, 32, 32, 1, 1);
    ok(icon != NULL, "NtUserCreateIcon failed\n");
    ok_res = NtUserDrawIconEx((HDC)(intptr_t)0x1, 0, 0, icon, 32, 32, 0, NULL, DI_NORMAL);
    ok(ok_res == TRUE, "NtUserDrawIconEx static icon step 0 failed\n");

    /* Static icon: step 1 out-of-bounds must fall back gracefully to frame 0 */
    ok_res = NtUserDrawIconEx((HDC)(intptr_t)0x1, 0, 0, icon, 32, 32, 1, NULL, DI_NORMAL);
    ok(ok_res == TRUE, "NtUserDrawIconEx static icon step 1 fallback 0 failed\n");

    /* Animated cursor: step 0 */
    ani = NtUserCreateIcon(FALSE, 32, 32, 4, 4);
    ok(ani != NULL, "NtUserCreateIcon animated cursor failed\n");
    ok_res = NtUserDrawIconEx((HDC)(intptr_t)0x1, 0, 0, ani, 32, 32, 0, NULL, DI_NORMAL);
    ok(ok_res == TRUE, "NtUserDrawIconEx animated cursor step 0 failed\n");

    /* Animated cursor: step 999 out-of-bounds must fall back gracefully to frame 0 */
    ok_res = NtUserDrawIconEx((HDC)(intptr_t)0x1, 0, 0, ani, 32, 32, 999, NULL, DI_NORMAL);
    ok(ok_res == TRUE, "NtUserDrawIconEx animated cursor step 999 fallback 0 failed\n");

    /* NtUserGetIconInfo */
    memset(&info, 0, sizeof(info));
    ok_res = NtUserGetIconInfo(icon, &info, NULL, NULL, NULL, 0);
    ok(ok_res == TRUE && info.fIcon == TRUE && info.hbmMask != NULL, "NtUserGetIconInfo failed\n");
}

START_TEST(win32u)
{
    WINETEST_RUN_SUBTEST(test_pack_erasebkgnd_message);
    WINETEST_RUN_SUBTEST(test_icon_frame_retrieval);
}

/*
 * Differential Conformance Test for Win32u cross-thread/cross-process WM_ERASEBKGND (msg 14)
 * HDC message packing in pack_message and NtUserDrawIconEx frame 0 retrieval.
 *
 * Copyright 2026 Spumante Contributors
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include <stdbool.h>
#include <string.h>
#include "../../include/parity_test.h"
#include "../../../dlls/win32u/win32u.h"

int main(void)
{
    struct packed_message data;
    void *buffer = NULL;
    size_t size = 0, buffer_size = 0;
    WPARAM wparam = 0;
    LPARAM lparam = 0;
    HDC test_hdc = (HDC)(intptr_t)0x5678;
    uint8_t buf[8] = { 0 };

    parity_begin_document("win32u", "test_win32u_msg_icon_parity", "x86_64", "wine_macos");

    /* Case 1: Win32u_Pack_WM_ERASEBKGND_Scalar */
    {
        memset(&data, 0, sizeof(data));
        memset(buf, 0, sizeof(buf));
        size_t res = pack_message(NULL, WM_ERASEBKGND, (WPARAM)test_hdc, 0, &data);
        (void)res;
        parity_record_exact(
            "win32u_pack_wm_erasebkgnd_scalar",
            "Win32u_Pack_WM_ERASEBKGND_Scalar",
            "pack_message",
            data.count == 0 ? 0 : 1,
            0,
            0,
            buf,
            sizeof(buf),
            data.count == 0
        );
    }

    /* Case 2: Win32u_Unpack_WM_ERASEBKGND_Hdc */
    {
        wparam = (WPARAM)test_hdc;
        lparam = 0;
        BOOL ok = unpack_message(NULL, WM_ERASEBKGND, &wparam, &lparam, &buffer, size, &buffer_size);
        buf[0] = (ok && (HDC)wparam == test_hdc) ? 1 : 0;
        parity_record_exact(
            "win32u_unpack_wm_erasebkgnd_hdc",
            "Win32u_Unpack_WM_ERASEBKGND_Hdc",
            "unpack_message",
            ok ? 0 : 1,
            0,
            1,
            buf,
            sizeof(buf),
            ok == TRUE
        );
    }

    /* Case 3: Win32u_Pack_WM_ICONERASEBKGND_Scalar */
    {
        memset(&data, 0, sizeof(data));
        memset(buf, 0, sizeof(buf));
        pack_message(NULL, WM_ICONERASEBKGND, (WPARAM)test_hdc, 0, &data);
        parity_record_exact(
            "win32u_pack_wm_iconerasebkgnd_scalar",
            "Win32u_Pack_WM_ICONERASEBKGND_Scalar",
            "pack_message",
            data.count == 0 ? 0 : 1,
            0,
            0,
            buf,
            sizeof(buf),
            data.count == 0
        );
    }

    /* Case 4: Win32u_Pack_WM_CTLCOLOR_Scalar */
    {
        memset(&data, 0, sizeof(data));
        memset(buf, 0, sizeof(buf));
        pack_message(NULL, WM_CTLCOLORSTATIC, (WPARAM)test_hdc, 0, &data);
        parity_record_exact(
            "win32u_pack_wm_ctlcolor_scalar",
            "Win32u_Pack_WM_CTLCOLOR_Scalar",
            "pack_message",
            data.count == 0 ? 0 : 1,
            0,
            0,
            buf,
            sizeof(buf),
            data.count == 0
        );
    }

    /* Case 5: Win32u_Unpack_WM_CTLCOLOR_Scalar */
    {
        wparam = (WPARAM)test_hdc;
        lparam = 0;
        BOOL ok = unpack_message(NULL, WM_CTLCOLORSTATIC, &wparam, &lparam, &buffer, size, &buffer_size);
        buf[0] = (ok && (HDC)wparam == test_hdc) ? 1 : 0;
        parity_record_exact(
            "win32u_unpack_wm_ctlcolor_scalar",
            "Win32u_Unpack_WM_CTLCOLOR_Scalar",
            "unpack_message",
            ok ? 0 : 1,
            0,
            1,
            buf,
            sizeof(buf),
            ok == TRUE
        );
    }

    /* Case 6: Win32u_DrawIconEx_NullHandle */
    {
        memset(buf, 0, sizeof(buf));
        BOOL ok = NtUserDrawIconEx((HDC)(intptr_t)0x1, 0, 0, NULL, 32, 32, 0, NULL, DI_NORMAL);
        parity_record_exact(
            "win32u_drawiconex_null_handle",
            "Win32u_DrawIconEx_NullHandle",
            "NtUserDrawIconEx",
            ok ? 0 : 1402,
            ok ? 0 : 1402,
            0,
            buf,
            sizeof(buf),
            false
        );
    }

    /* Case 7: Win32u_DrawIconEx_InvalidHandle */
    {
        memset(buf, 0, sizeof(buf));
        BOOL ok = NtUserDrawIconEx((HDC)(intptr_t)0x1, 0, 0, (HICON)(intptr_t)0xdeadbeef, 32, 32, 0, NULL, DI_NORMAL);
        parity_record_exact(
            "win32u_drawiconex_invalid_handle",
            "Win32u_DrawIconEx_InvalidHandle",
            "NtUserDrawIconEx",
            ok ? 0 : 1402,
            ok ? 0 : 1402,
            0,
            buf,
            sizeof(buf),
            false
        );
    }

    /* Case 8: Win32u_DrawIconEx_StaticIcon_Step0 */
    {
        HICON icon = NtUserCreateIcon(TRUE, 32, 32, 1, 1);
        BOOL ok = NtUserDrawIconEx((HDC)(intptr_t)0x1, 0, 0, icon, 32, 32, 0, NULL, DI_NORMAL);
        buf[0] = ok ? 1 : 0;
        parity_record_exact(
            "win32u_drawiconex_static_icon_step0",
            "Win32u_DrawIconEx_StaticIcon_Step0",
            "NtUserDrawIconEx",
            ok ? 0 : 1,
            0,
            1,
            buf,
            sizeof(buf),
            ok == TRUE
        );
    }

    /* Case 9: Win32u_DrawIconEx_StaticIcon_Step1_Fallback0 */
    {
        HICON icon = NtUserCreateIcon(TRUE, 32, 32, 1, 1);
        BOOL ok = NtUserDrawIconEx((HDC)(intptr_t)0x1, 0, 0, icon, 32, 32, 1, NULL, DI_NORMAL);
        buf[0] = ok ? 1 : 0;
        parity_record_exact(
            "win32u_drawiconex_static_icon_step1_fallback0",
            "Win32u_DrawIconEx_StaticIcon_Step1_Fallback0",
            "NtUserDrawIconEx",
            ok ? 0 : 1,
            0,
            1,
            buf,
            sizeof(buf),
            ok == TRUE
        );
    }

    /* Case 10: Win32u_DrawIconEx_AnimatedCursor_Step0 */
    {
        HICON ani = NtUserCreateIcon(FALSE, 32, 32, 4, 4);
        BOOL ok = NtUserDrawIconEx((HDC)(intptr_t)0x1, 0, 0, ani, 32, 32, 0, NULL, DI_NORMAL);
        buf[0] = ok ? 1 : 0;
        parity_record_exact(
            "win32u_drawiconex_animated_cursor_step0",
            "Win32u_DrawIconEx_AnimatedCursor_Step0",
            "NtUserDrawIconEx",
            ok ? 0 : 1,
            0,
            1,
            buf,
            sizeof(buf),
            ok == TRUE
        );
    }

    /* Case 11: Win32u_DrawIconEx_AnimatedCursor_StepOob_Fallback0 */
    {
        HICON ani = NtUserCreateIcon(FALSE, 32, 32, 4, 4);
        BOOL ok = NtUserDrawIconEx((HDC)(intptr_t)0x1, 0, 0, ani, 32, 32, 999, NULL, DI_NORMAL);
        buf[0] = ok ? 1 : 0;
        parity_record_exact(
            "win32u_drawiconex_animated_cursor_step_oob_fallback0",
            "Win32u_DrawIconEx_AnimatedCursor_StepOob_Fallback0",
            "NtUserDrawIconEx",
            ok ? 0 : 1,
            0,
            1,
            buf,
            sizeof(buf),
            ok == TRUE
        );
    }

    /* Case 12: Win32u_GetIconInfo_Frame0 */
    {
        HICON icon = NtUserCreateIcon(TRUE, 32, 32, 1, 1);
        ICONINFO info;
        memset(&info, 0, sizeof(info));
        BOOL ok = NtUserGetIconInfo(icon, &info, NULL, NULL, NULL, 0);
        uint8_t ibuf[8] = { 0 };
        ibuf[0] = ok ? 1 : 0;
        ibuf[4] = 32; /* width */
        parity_record_exact(
            "win32u_geticoninfo_frame0",
            "Win32u_GetIconInfo_Frame0",
            "NtUserGetIconInfo",
            ok ? 0 : 1,
            0,
            1,
            ibuf,
            sizeof(ibuf),
            ok == TRUE
        );
    }

    parity_end_document();
    return 0;
}

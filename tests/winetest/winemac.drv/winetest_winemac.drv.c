/*
 * Upstream Wine winetest anti-regression suite for winemac.drv
 *
 * Covers:
 *   - Window lifecycle, hierarchy, positioning, and style masks (window.c)
 *   - IOSurface-backed cross-process child window CAMetalLayer swapchains (window.c:1177)
 *   - Multi-monitor ChangeDisplaySettingsEx on non-primary displays & adapter detaching (display.c:767-772)
 *   - Cross-process layered window attributes & ULW opacity synchronization (window.c:1318)
 *   - Cross-process window DCs / OpenGL drawables & WGL SwapBuffers (opengl.c:1139)
 *
 * Adapted from upstream Wine dlls/win32u/tests/ & dlls/user32/tests/
 * Copyright 2013-2024 Wine contributors
 * Copyright 2026 Spumante Contributors
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "../../include/wine/test.h"
#include "../../../dlls/winemac.drv/macdrv.h"

static void wt_init_wstr(WCHAR *dst, size_t cap, const char *ascii)
{
    size_t i = 0;
    if (!dst || cap == 0) {
        return;
    }
    if (!ascii) {
        dst[0] = 0;
        return;
    }
    while (ascii[i] != '\0' && (i + 1) < cap) {
        dst[i] = (WCHAR)(unsigned char)ascii[i];
        i++;
    }
    dst[i] = 0;
}

static void test_window_lifecycle_and_pos(void)
{
    WCHAR cls_name[32];
    WCHAR win_title[32];
    HWND root_hwnd;
    HWND child_hwnd;
    RECT rc_win;
    RECT rc_client;
    UINT style_mask = 0;
    UINT ex_style_mask = 0;
    STYLESTRUCT ss;

    wt_init_wstr(cls_name, 32, "WinetestMacClass");
    wt_init_wstr(win_title, 32, "WinetestMacTitle");

    macdrv_reset_window_state();
    macdrv_set_current_process_id(1001);

    root_hwnd = macdrv_create_window_ex(
        WS_EX_APPWINDOW,
        cls_name,
        win_title,
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        50,
        60,
        800,
        600,
        NULL,
        1001);
    ok(root_hwnd != NULL, "macdrv_create_window_ex root failed\n");
    ok(get_win_data(root_hwnd) != NULL, "get_win_data(root_hwnd) should be non-NULL in owner process\n");

    child_hwnd = macdrv_create_window_ex(
        0,
        cls_name,
        win_title,
        WS_CHILD | WS_VISIBLE,
        10,
        20,
        300,
        200,
        root_hwnd,
        1001);
    ok(child_hwnd != NULL, "macdrv_create_window_ex child failed\n");
    ok(macdrv_get_ancestor(child_hwnd, GA_PARENT) == root_hwnd, "GA_PARENT mismatch\n");
    ok(macdrv_get_ancestor(child_hwnd, GA_ROOT) == root_hwnd, "GA_ROOT mismatch\n");
    ok(macdrv_get_ancestor(root_hwnd, GA_ROOT) == root_hwnd, "root GA_ROOT mismatch\n");

    memset(&rc_win, 0, sizeof(rc_win));
    memset(&rc_client, 0, sizeof(rc_client));
    ok(macdrv_get_window_rect(root_hwnd, &rc_win), "macdrv_get_window_rect failed\n");
    ok(rc_win.left == 50 && rc_win.top == 60 && rc_win.right == 850 && rc_win.bottom == 660,
       "unexpected window rect (%d,%d)-(%d,%d)\n",
       (int)rc_win.left, (int)rc_win.top, (int)rc_win.right, (int)rc_win.bottom);

    ok(macdrv_get_client_rect(root_hwnd, &rc_client), "macdrv_get_client_rect failed\n");
    ok(rc_client.right == 800 && rc_client.bottom == 600, "unexpected client rect\n");

    ok(macdrv_set_window_pos(child_hwnd, 25, 35, 400, 250, SWP_NOZORDER | SWP_NOACTIVATE),
       "macdrv_set_window_pos failed\n");
    ok(macdrv_get_client_rect(child_hwnd, &rc_client), "macdrv_get_client_rect child failed\n");
    ok(rc_client.right == 400 && rc_client.bottom == 250, "child resize not reflected\n");

    ss.styleOld = (DWORD)macdrv_get_window_long(root_hwnd, GWL_STYLE);
    ss.styleNew = ss.styleOld | WS_MAXIMIZEBOX;
    macdrv_SetWindowStyle(root_hwnd, GWL_STYLE, &ss);
    ok(((DWORD)macdrv_get_window_long(root_hwnd, GWL_STYLE) & WS_MAXIMIZEBOX) != 0,
       "macdrv_SetWindowStyle did not update GWL_STYLE\n");

    ok(macdrv_GetWindowStyleMasks(root_hwnd, ss.styleNew, 0, &style_mask, &ex_style_mask),
       "macdrv_GetWindowStyleMasks failed\n");
    ok(style_mask != 0 && ex_style_mask != 0, "expected non-zero style masks\n");

    macdrv_ShowWindow(root_hwnd, SW_HIDE, NULL, 0);
    ok(((DWORD)macdrv_get_window_long(root_hwnd, GWL_STYLE) & WS_VISIBLE) == 0,
       "SW_HIDE should clear WS_VISIBLE\n");
    macdrv_ShowWindow(root_hwnd, SW_SHOW, NULL, 0);
    ok(((DWORD)macdrv_get_window_long(root_hwnd, GWL_STYLE) & WS_VISIBLE) != 0,
       "SW_SHOW should set WS_VISIBLE\n");

    macdrv_DestroyWindow(child_hwnd);
    macdrv_DestroyWindow(root_hwnd);
}

static void test_metal_swapchain_and_iosurface_child(void)
{
    WCHAR cls_name[32];
    WCHAR win_title[32];
    HWND root_hwnd;
    HWND child_hwnd;
    struct client_surface *same_client;
    struct macdrv_client_surface *same_surf;
    struct client_surface *xproc_top_client;
    struct macdrv_client_surface *xproc_top_surf;
    struct client_surface *xproc_child_client;
    struct macdrv_client_surface *xproc_child_surf;
    struct macdrv_metal_swapchain_info info;
    unsigned char bgra_frame[16] = {
        0x10, 0x20, 0x30, 0xFF,
        0x40, 0x50, 0x60, 0xFF,
        0x70, 0x80, 0x90, 0xFF,
        0xA0, 0xB0, 0xC0, 0xFF
    };
    unsigned char readback[16];
    uint32_t iosurf_id = 0;
    uint64_t vk_surf = 0;
    bool ok_sc;
    BOOL ok_q;
    BOOL ok_pres;
    BOOL ok_read;
    INT vk_res;

    wt_init_wstr(cls_name, 32, "MetalRootClass");
    wt_init_wstr(win_title, 32, "MetalWindow");

    macdrv_reset_window_state();
    macdrv_set_current_process_id(1001);

    SetLastError(0);
    ok(!macdrv_client_surface_acquire_metal_swapchain(NULL) &&
       GetLastError() == ERROR_INVALID_PARAMETER,
       "NULL surface should fail with ERROR_INVALID_PARAMETER\n");

    root_hwnd = macdrv_create_window_ex(
        WS_EX_APPWINDOW,
        cls_name,
        win_title,
        WS_OVERLAPPED | WS_VISIBLE,
        0,
        0,
        640,
        480,
        NULL,
        1001);
    child_hwnd = macdrv_create_window_ex(
        0,
        cls_name,
        win_title,
        WS_CHILD | WS_VISIBLE,
        40,
        30,
        320,
        240,
        root_hwnd,
        1001);

    /* 1. Same-process top-level swapchain (supported in both upstream and spumante) */
    same_client = macdrv_CreateClientSurface(root_hwnd, 1, FALSE);
    same_surf = impl_from_client_surface(same_client);
    ok_sc = macdrv_client_surface_acquire_metal_swapchain(same_surf);
    memset(&info, 0, sizeof(info));
    ok_q = macdrv_query_metal_swapchain_info(same_surf, &info);
    ok(ok_sc && ok_q && info.mode == MACDRV_SWAPCHAIN_MODE_VIEW,
       "same-process swapchain failed (ok=%d mode=%u)\n", (int)ok_sc, (unsigned int)info.mode);

    /* 2. Cross-process top-level remote CAContext swapchain */
    macdrv_set_current_process_id(2002);
    xproc_top_client = macdrv_CreateClientSurface(root_hwnd, 1, FALSE);
    xproc_top_surf = impl_from_client_surface(xproc_top_client);
    ok_sc = macdrv_client_surface_acquire_metal_swapchain(xproc_top_surf);
    memset(&info, 0, sizeof(info));
    ok_q = macdrv_query_metal_swapchain_info(xproc_top_surf, &info);
    ok(ok_sc && ok_q && info.mode == MACDRV_SWAPCHAIN_MODE_CACONTEXT_TOPLEVEL &&
       info.remote_context_id != 0,
       "cross-process top-level swapchain failed\n");

    /* 3. Cross-process child window IOSurface-backed swapchain (window.c:1177) */
    xproc_child_client = macdrv_CreateClientSurface(child_hwnd, 1, FALSE);
    xproc_child_surf = impl_from_client_surface(xproc_child_client);
    ok_sc = macdrv_client_surface_acquire_metal_swapchain(xproc_child_surf);
    memset(&info, 0, sizeof(info));
    ok_q = macdrv_query_metal_swapchain_info(xproc_child_surf, &info);

    todo_wine {
        ok(ok_sc && ok_q &&
           info.mode == MACDRV_SWAPCHAIN_MODE_IOSURFACE_CHILD &&
           info.iosurface_backed == 1 &&
           info.front_iosurface_id != 0 &&
           info.back_iosurface_id != 0 &&
           info.front_iosurface_id != info.back_iosurface_id &&
           info.mtl_pixel_format == MACDRV_MTL_PIXEL_FORMAT_BGRA8UNORM &&
           info.framebuffer_only == 0 &&
           info.width == 320 &&
           info.height == 240 &&
           info.bytes_per_row == 1280,
           "expected IOSurface-backed cross-process child swapchain (window.c:1177)\n");
    }

    memset(readback, 0, sizeof(readback));
    ok_pres = macdrv_swapchain_present_bgra(xproc_child_surf, bgra_frame, sizeof(bgra_frame));
    vk_res = macdrv_vulkan_create_metal_surface(xproc_child_client, &vk_surf);
    macdrv_set_current_process_id(1001);
    ok_read = macdrv_parent_read_child_iosurface_bgra(
        root_hwnd,
        child_hwnd,
        readback,
        sizeof(readback),
        &iosurf_id);

    todo_wine {
        ok(ok_pres && ok_read && vk_res == VK_SUCCESS && vk_surf != 0 &&
           iosurf_id != 0 && memcmp(bgra_frame, readback, 16) == 0,
           "expected cross-process IOSurface present + parent readback + Vulkan surface\n");
    }

    /* Dynamic resize of cross-process child window */
    macdrv_set_window_pos(child_hwnd, 50, 60, 400, 300, SWP_NOZORDER | SWP_NOACTIVATE);
    macdrv_set_current_process_id(2002);
    if (xproc_child_client && xproc_child_client->funcs && xproc_child_client->funcs->update) {
        xproc_child_client->funcs->update(xproc_child_client);
    }
    memset(&info, 0, sizeof(info));
    ok_q = macdrv_query_metal_swapchain_info(xproc_child_surf, &info);

    todo_wine {
        ok(ok_q && info.width == 400 && info.height == 300 && info.bytes_per_row == 1600,
           "expected resized cross-process child IOSurface 400x300 (pitch 1600)\n");
    }

    macdrv_client_surface_release(xproc_child_client);
    macdrv_client_surface_release(xproc_top_client);
    macdrv_client_surface_release(same_client);

    macdrv_set_current_process_id(1001);
    macdrv_DestroyWindow(child_hwnd);
    macdrv_DestroyWindow(root_hwnd);
}

static void test_multimon_display_settings_and_detach(void)
{
    WCHAR disp1[32];
    WCHAR disp2[32];
    WCHAR disp3[32];
    DISPLAY_DEVICEW dd;
    DEVMODEW dm;
    DEVMODEW req;
    DDGAMMARAMP ramp_in;
    DDGAMMARAMP ramp_out1;
    DDGAMMARAMP ramp_out2;
    struct macdrv_gpu *gpus = NULL;
    struct macdrv_adapter *adapters = NULL;
    struct macdrv_monitor *monitors = NULL;
    int count = 0;
    UINT i;
    LONG ret;

    wt_init_wstr(disp1, 32, "\\\\.\\DISPLAY1");
    wt_init_wstr(disp2, 32, "\\\\.\\DISPLAY2");
    wt_init_wstr(disp3, 32, "\\\\.\\DISPLAY3");

    macdrv_reset_display_topology();

    /* GPU / Adapter / Monitor enumeration */
    ok(macdrv_get_gpus(&gpus, &count) == 0 && count == 1 && gpus != NULL, "macdrv_get_gpus failed\n");
    if (gpus) {
        ok(macdrv_get_adapters(gpus[0].id, &adapters, &count) == 0 && count == 3 && adapters != NULL,
           "macdrv_get_adapters failed\n");
        if (adapters) {
            ok(macdrv_get_monitors(adapters[0].id, &monitors, &count) == 0 && count == 1 && monitors != NULL,
               "macdrv_get_monitors failed\n");
            macdrv_free_monitors(monitors, count);
            macdrv_free_adapters(adapters);
        }
        macdrv_free_gpus(gpus);
    }

    /* EnumDisplayDevicesW & EnumDisplaySettingsExW */
    for (i = 0; i < 3; i++) {
        memset(&dd, 0, sizeof(dd));
        dd.cb = sizeof(dd);
        ok(macdrv_EnumDisplayDevicesW(NULL, i, &dd, 0), "EnumDisplayDevicesW(%u) failed\n", i);
    }
    memset(&dd, 0, sizeof(dd));
    dd.cb = sizeof(dd);
    ok(!macdrv_EnumDisplayDevicesW(NULL, 3, &dd, 0), "EnumDisplayDevicesW(3) should return FALSE\n");

    memset(&dm, 0, sizeof(dm));
    dm.dmSize = sizeof(dm);
    ok(macdrv_EnumDisplaySettingsExW(disp1, ENUM_CURRENT_SETTINGS, &dm, 0),
       "EnumDisplaySettingsExW DISPLAY1 failed\n");
    ok(dm.dmPelsWidth == 1920 && dm.dmPelsHeight == 1080, "unexpected DISPLAY1 default resolution\n");

    /* Primary display mode change (works in both upstream and spumante) */
    memset(&req, 0, sizeof(req));
    req.dmSize = sizeof(req);
    req.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT | DM_BITSPERPEL | DM_DISPLAYFREQUENCY;
    req.dmPelsWidth = 1280;
    req.dmPelsHeight = 720;
    req.dmBitsPerPel = 32;
    req.dmDisplayFrequency = 60;
    ret = macdrv_ChangeDisplaySettingsExW(disp1, &req, NULL, 0, NULL);
    ok(ret == DISP_CHANGE_SUCCESSFUL, "primary ChangeDisplaySettingsExW failed: %d\n", (int)ret);
    ok(macdrv_ChangeDisplaySettingsExW(disp1, NULL, NULL, 0, NULL) == DISP_CHANGE_SUCCESSFUL,
       "primary reset failed\n");

    /* Non-primary display mode change (display.c:767) */
    memset(&req, 0, sizeof(req));
    req.dmSize = sizeof(req);
    req.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT | DM_BITSPERPEL | DM_DISPLAYFREQUENCY | DM_POSITION;
    req.dmPelsWidth = 1920;
    req.dmPelsHeight = 1080;
    req.dmBitsPerPel = 32;
    req.dmDisplayFrequency = 60;
    req.dmPosition.x = 1920;
    req.dmPosition.y = 0;
    ret = macdrv_ChangeDisplaySettingsExW(disp2, &req, NULL, 0, NULL);
    memset(&dm, 0, sizeof(dm));
    dm.dmSize = sizeof(dm);
    macdrv_EnumDisplaySettingsExW(disp2, ENUM_CURRENT_SETTINGS, &dm, 0);

    todo_wine {
        ok(ret == DISP_CHANGE_SUCCESSFUL && dm.dmPelsWidth == 1920 && dm.dmPelsHeight == 1080,
           "expected non-primary DISPLAY2 ChangeDisplaySettingsExW to succeed (display.c:767)\n");
    }

    /* Secondary adapter detaching & re-attaching (display.c:772) */
    memset(&req, 0, sizeof(req));
    req.dmSize = sizeof(req);
    req.dmFields = DM_POSITION | DM_PELSWIDTH | DM_PELSHEIGHT;
    req.dmPelsWidth = 0;
    req.dmPelsHeight = 0;
    ret = macdrv_ChangeDisplaySettingsExW(disp3, &req, NULL, 0, NULL);
    memset(&dd, 0, sizeof(dd));
    dd.cb = sizeof(dd);
    macdrv_EnumDisplayDevicesW(NULL, 2, &dd, 0);

    todo_wine {
        ok(ret == DISP_CHANGE_SUCCESSFUL && (dd.StateFlags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP) == 0,
           "expected secondary adapter detaching to succeed (display.c:772)\n");
    }

    /* Re-attach DISPLAY3 */
    req.dmFields = DM_POSITION | DM_PELSWIDTH | DM_PELSHEIGHT | DM_BITSPERPEL | DM_DISPLAYFREQUENCY;
    req.dmPosition.x = -1920;
    req.dmPosition.y = 0;
    req.dmPelsWidth = 1920;
    req.dmPelsHeight = 1080;
    req.dmBitsPerPel = 32;
    req.dmDisplayFrequency = 60;
    ret = macdrv_ChangeDisplaySettingsExW(disp3, &req, NULL, 0, NULL);
    memset(&dd, 0, sizeof(dd));
    dd.cb = sizeof(dd);
    macdrv_EnumDisplayDevicesW(NULL, 2, &dd, 0);

    todo_wine {
        ok(ret == DISP_CHANGE_SUCCESSFUL && (dd.StateFlags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP) != 0,
           "expected secondary adapter re-attaching to succeed\n");
    }

    /* Per-display gamma ramp on non-primary display */
    for (i = 0; i < 256; i++) {
        ramp_in.red[i] = (WORD)((i * 257U) >> 1);
        ramp_in.green[i] = (WORD)(i * 257U);
        ramp_in.blue[i] = (WORD)((255U - i) * 257U);
    }
    memset(&ramp_out1, 0, sizeof(ramp_out1));
    memset(&ramp_out2, 0, sizeof(ramp_out2));

    /* Primary gamma ramp works in both upstream and spumante */
    ok(macdrv_GetDeviceGammaRamp(NULL, &ramp_out1), "primary GetDeviceGammaRamp failed\n");

    todo_wine {
        ok(macdrv_SetDeviceGammaRampForDisplay(disp2, &ramp_in) &&
           macdrv_GetDeviceGammaRampForDisplay(disp2, &ramp_out2) &&
           ramp_out2.red[128] == ramp_in.red[128] &&
           ramp_out2.blue[0] == 65535U,
           "expected per-display gamma ramp on DISPLAY2 to succeed\n");
    }
}

static void test_layered_window_attributes_xproc(void)
{
    WCHAR cls_name[32];
    WCHAR win_title[32];
    HWND hwnd;
    COLORREF key = 0;
    BYTE alpha = 0;
    DWORD flags = 0;
    struct macdrv_layered_window_info linfo;
    BOOL q_ok;

    wt_init_wstr(cls_name, 32, "LayeredClass");
    wt_init_wstr(win_title, 32, "LayeredWin");

    macdrv_reset_window_state();
    macdrv_set_current_process_id(1001);

    hwnd = macdrv_create_window_ex(
        WS_EX_LAYERED | WS_EX_APPWINDOW,
        cls_name,
        win_title,
        WS_OVERLAPPED | WS_VISIBLE,
        100,
        100,
        400,
        300,
        NULL,
        1001);
    ok(hwnd != NULL, "layered hwnd creation failed\n");

    /* 1. Same-process SetLayeredWindowAttributes */
    macdrv_SetLayeredWindowAttributes(hwnd, 0x00112233U, 200, LWA_ALPHA | LWA_COLORKEY);
    ok(macdrv_GetLayeredWindowAttributes(hwnd, &key, &alpha, &flags),
       "same-process GetLayeredWindowAttributes failed\n");
    ok(key == 0x00112233U && alpha == 200 && flags == (LWA_ALPHA | LWA_COLORKEY),
       "same-process layered attributes mismatch\n");

    /* 2. Cross-process SetLayeredWindowAttributes & UpdateLayeredWindow (window.c:1318) */
    macdrv_set_current_process_id(2002);
    ok(get_win_data(hwnd) == NULL, "get_win_data should be NULL in other process\n");

    macdrv_SetLayeredWindowAttributes(hwnd, 0x00AABBCCU, 128, LWA_ALPHA | LWA_COLORKEY);
    memset(&linfo, 0, sizeof(linfo));
    q_ok = macdrv_query_layered_window_info(hwnd, &linfo);

    todo_wine {
        ok(q_ok && linfo.color_key == 0x00AABBCCU && linfo.alpha == 128 &&
           linfo.opacity_permille == 501U && linfo.remote_msg_dispatched >= 1,
           "expected cross-process SetLayeredWindowAttributes to synchronize (window.c:1318)\n");
    }

    macdrv_UpdateLayeredWindow(hwnd, 64, ULW_ALPHA);
    memset(&linfo, 0, sizeof(linfo));
    q_ok = macdrv_query_layered_window_info(hwnd, &linfo);

    todo_wine {
        ok(q_ok && linfo.ulw_layered == 1 && linfo.alpha == 64 && linfo.opacity_permille == 250U,
           "expected cross-process UpdateLayeredWindow to synchronize\n");
    }

    macdrv_set_current_process_id(1001);
    macdrv_DestroyWindow(hwnd);
}

static void test_opengl_pixel_format_and_xproc_dc(void)
{
    WCHAR cls_name[32];
    WCHAR win_title[32];
    HWND hwnd_same;
    HWND hwnd_xproc;
    PIXELFORMATDESCRIPTOR pfd;
    const void *drv_funcs = NULL;
    struct client_surface *same_client;
    struct opengl_drawable *same_draw = NULL;
    struct client_surface *xproc_client;
    struct opengl_drawable *xproc_draw = NULL;
    struct macdrv_opengl_context *ctx = NULL;
    INT fmt;
    BOOL surf_ok;
    BOOL swap_ok;

    wt_init_wstr(cls_name, 32, "OpenGLClass");
    wt_init_wstr(win_title, 32, "OpenGLWin");

    macdrv_reset_window_state();
    macdrv_set_current_process_id(1001);
    memset(&pfd, 0, sizeof(pfd));

    ok(macdrv_OpenGLInit(1, &pfd, &drv_funcs) == STATUS_SUCCESS && drv_funcs != NULL,
       "macdrv_OpenGLInit failed\n");

    hwnd_same = macdrv_create_window_ex(
        WS_EX_APPWINDOW,
        cls_name,
        win_title,
        WS_OVERLAPPED | WS_VISIBLE,
        0,
        0,
        640,
        480,
        NULL,
        1001);
    hwnd_xproc = macdrv_create_window_ex(
        WS_EX_APPWINDOW,
        cls_name,
        win_title,
        WS_OVERLAPPED | WS_VISIBLE,
        0,
        0,
        640,
        480,
        NULL,
        1001);

    memset(&pfd, 0, sizeof(pfd));
    pfd.nSize = sizeof(pfd);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.cColorBits = 32;
    pfd.cDepthBits = 24;
    pfd.cStencilBits = 8;

    ok(macdrv_DescribePixelFormat(hwnd_same, 1, sizeof(pfd), &pfd) == 4,
       "DescribePixelFormat count mismatch\n");
    fmt = macdrv_ChoosePixelFormat(hwnd_same, &pfd);
    ok(fmt == 1, "ChoosePixelFormat mismatch: %d\n", fmt);
    ok(macdrv_SetPixelFormat(hwnd_same, fmt, &pfd), "same-process SetPixelFormat failed\n");
    ok(macdrv_GetPixelFormat(hwnd_same) == fmt, "GetPixelFormat mismatch\n");

    /* 1. Same-process OpenGL drawable creation */
    same_client = macdrv_CreateClientSurface(hwnd_same, fmt, FALSE);
    ok(macdrv_surface_create(same_client, fmt, &same_draw) && same_draw != NULL &&
       same_draw->is_cross_process == 0,
       "same-process macdrv_surface_create failed\n");
    macdrv_drawable_release(same_draw);
    macdrv_client_surface_release(same_client);

    /* 2. Cross-process OpenGL drawable creation & SwapBuffers (opengl.c:1139) */
    macdrv_set_current_process_id(2002);
    ok(get_win_data(hwnd_xproc) == NULL, "expected NULL win_data in process 2002\n");

    todo_wine {
        ok(macdrv_SetPixelFormat(hwnd_xproc, fmt, &pfd),
           "expected cross-process SetPixelFormat to succeed\n");
    }

    xproc_client = macdrv_CreateClientSurface(hwnd_xproc, fmt, FALSE);
    surf_ok = macdrv_surface_create(xproc_client, fmt, &xproc_draw);
    ctx = macdrv_wglCreateContext(hwnd_xproc);
    swap_ok = FALSE;
    if (surf_ok && xproc_draw && ctx) {
        macdrv_wglMakeCurrent(xproc_client, xproc_draw, ctx);
        macdrv_wglClearColor(ctx, 0x12, 0x34, 0x56, 0xFF);
        swap_ok = macdrv_wglSwapBuffers(xproc_client, xproc_draw);
    }

    todo_wine {
        ok(surf_ok && xproc_draw != NULL && xproc_draw->is_cross_process == 1 &&
           xproc_draw->iosurface_id != 0 && swap_ok && xproc_draw->swap_count == 1,
           "expected cross-process OpenGL surface_create and SwapBuffers to succeed (opengl.c:1139)\n");
    }

    if (ctx) {
        macdrv_wglDeleteContext(ctx);
    }
    if (xproc_draw) {
        macdrv_drawable_release(xproc_draw);
    }
    macdrv_client_surface_release(xproc_client);

    macdrv_set_current_process_id(1001);
    macdrv_DestroyWindow(hwnd_xproc);
    macdrv_DestroyWindow(hwnd_same);
}

START_TEST(winemac_drv)
{
    WINETEST_RUN_SUBTEST(test_window_lifecycle_and_pos);
    WINETEST_RUN_SUBTEST(test_metal_swapchain_and_iosurface_child);
    WINETEST_RUN_SUBTEST(test_multimon_display_settings_and_detach);
    WINETEST_RUN_SUBTEST(test_layered_window_attributes_xproc);
    WINETEST_RUN_SUBTEST(test_opengl_pixel_format_and_xproc_dc);
}

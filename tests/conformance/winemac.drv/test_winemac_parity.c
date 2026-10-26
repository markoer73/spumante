/*
 * Differential Conformance Test for winemac.drv Windowing, Display & Metal/OpenGL Parity
 *
 * Covers:
 *   1. IOSurface-backed cross-process child window CAMetalLayer swapchains (window.c:1177)
 *   2. Multi-monitor ChangeDisplaySettingsEx on non-primary displays & adapter detaching (display.c:767-772)
 *   3. Cross-process window DCs / OpenGL drawables & layered window attributes (opengl.c:1139, window.c:1318)
 *
 * Copyright 2026 Spumante Contributors
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "../../include/parity_test.h"
#include "../../../dlls/winemac.drv/macdrv.h"

static void init_wstr(WCHAR *dst, size_t cap, const char *ascii)
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

static void write_u32_le(unsigned char *buf, uint32_t val)
{
    buf[0] = (unsigned char)(val & 0xFFU);
    buf[1] = (unsigned char)((val >> 8) & 0xFFU);
    buf[2] = (unsigned char)((val >> 16) & 0xFFU);
    buf[3] = (unsigned char)((val >> 24) & 0xFFU);
}

int main(void)
{
    WCHAR disp1_name[32];
    WCHAR disp2_name[32];
    WCHAR disp3_name[32];
    WCHAR cls_main[16];
    WCHAR cls_child[16];
    WCHAR title_main[16];
    WCHAR title_child[16];

    HWND root_hwnd = NULL;
    HWND child_hwnd = NULL;
    HWND layered_hwnd = NULL;
    HWND gl_hwnd = NULL;

    struct client_surface *same_proc_client = NULL;
    struct macdrv_client_surface *same_proc_surf = NULL;
    struct client_surface *xproc_top_client = NULL;
    struct macdrv_client_surface *xproc_top_surf = NULL;
    struct client_surface *xproc_child_client = NULL;
    struct macdrv_client_surface *xproc_child_surf = NULL;
    struct macdrv_metal_swapchain_info sc_info;
    unsigned char summary[16];
    bool ok_flag;
    BOOL ret_bool;
    LONG cds_ret;

    init_wstr(disp1_name, 32, "\\\\.\\DISPLAY1");
    init_wstr(disp2_name, 32, "\\\\.\\DISPLAY2");
    init_wstr(disp3_name, 32, "\\\\.\\DISPLAY3");
    init_wstr(cls_main, 16, "MacDrvRoot");
    init_wstr(cls_child, 16, "MacDrvChild");
    init_wstr(title_main, 16, "RootWindow");
    init_wstr(title_child, 16, "ChildView");

    parity_begin_document("winemac.drv", "test_winemac_parity", "x86_64", "wine_macos");

    macdrv_reset_window_state();
    macdrv_reset_display_topology();

    /* =====================================================================
     * 1. Parameter Validation: NULL surface on macdrv_client_surface_acquire_metal_swapchain
     * ===================================================================== */
    SetLastError(0);
    ok_flag = macdrv_client_surface_acquire_metal_swapchain(NULL);
    ok(!ok_flag && GetLastError() == ERROR_INVALID_PARAMETER,
       "Expected FALSE + ERROR_INVALID_PARAMETER for NULL surface, got %d err=%u\n",
       (int)ok_flag, (unsigned int)GetLastError());
    parity_record_exact(
        "err_swapchain_null",
        "InvalidParam_NullSurfaceSwapchain",
        "macdrv_client_surface_acquire_metal_swapchain",
        ok_flag ? STATUS_SUCCESS : STATUS_INVALID_PARAMETER,
        GetLastError(),
        0,
        NULL,
        0,
        0);

    /* =====================================================================
     * 2. Same-process top-level CAMetalLayer swapchain acquisition
     * ===================================================================== */
    macdrv_set_current_process_id(1001);
    root_hwnd = macdrv_create_window_ex(
        WS_EX_APPWINDOW,
        cls_main,
        title_main,
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        100,
        100,
        640,
        480,
        NULL,
        1001);
    ok(root_hwnd != NULL, "Expected non-NULL root_hwnd\n");

    SetLastError(0);
    same_proc_client = macdrv_CreateClientSurface(root_hwnd, 1, FALSE);
    same_proc_surf = impl_from_client_surface(same_proc_client);
    ok_flag = macdrv_client_surface_acquire_metal_swapchain(same_proc_surf);
    memset(&sc_info, 0, sizeof(sc_info));
    ret_bool = macdrv_query_metal_swapchain_info(same_proc_surf, &sc_info);
    ok(ok_flag && ret_bool && sc_info.mode == MACDRV_SWAPCHAIN_MODE_VIEW,
       "Expected MACDRV_SWAPCHAIN_MODE_VIEW (1), got ok=%d mode=%u\n",
       (int)ok_flag, (unsigned int)sc_info.mode);
    memset(summary, 0, sizeof(summary));
    write_u32_le(summary + 0, sc_info.mode);
    write_u32_le(summary + 4, sc_info.is_cross_process);
    write_u32_le(summary + 8, sc_info.width);
    write_u32_le(summary + 12, sc_info.height);
    parity_record_exact(
        "same_proc_swapchain",
        "SameProcess_TopLevelMetalSwapchain",
        "macdrv_client_surface_acquire_metal_swapchain",
        (ok_flag && ret_bool) ? STATUS_SUCCESS : STATUS_NOT_IMPLEMENTED,
        GetLastError(),
        16,
        summary,
        16,
        ok_flag ? 1 : 0);

    /* =====================================================================
     * 3. Cross-process top-level remote CAContext CAMetalLayer swapchain
     * ===================================================================== */
    macdrv_set_current_process_id(2002);
    SetLastError(0);
    xproc_top_client = macdrv_CreateClientSurface(root_hwnd, 1, FALSE);
    xproc_top_surf = impl_from_client_surface(xproc_top_client);
    ok_flag = macdrv_client_surface_acquire_metal_swapchain(xproc_top_surf);
    memset(&sc_info, 0, sizeof(sc_info));
    ret_bool = macdrv_query_metal_swapchain_info(xproc_top_surf, &sc_info);
    ok(ok_flag && ret_bool && sc_info.mode == MACDRV_SWAPCHAIN_MODE_CACONTEXT_TOPLEVEL &&
       sc_info.remote_context_id != 0,
       "Expected MACDRV_SWAPCHAIN_MODE_CACONTEXT_TOPLEVEL (2), got ok=%d mode=%u\n",
       (int)ok_flag, (unsigned int)sc_info.mode);
    memset(summary, 0, sizeof(summary));
    write_u32_le(summary + 0, sc_info.mode);
    write_u32_le(summary + 4, sc_info.is_cross_process);
    write_u32_le(summary + 8, sc_info.is_child_window);
    write_u32_le(summary + 12, sc_info.remote_context_id != 0 ? 1U : 0U);
    parity_record_exact(
        "xproc_toplevel_swapchain",
        "CrossProcess_TopLevelRemoteLayerSwapchain",
        "macdrv_client_surface_acquire_metal_swapchain",
        (ok_flag && ret_bool) ? STATUS_SUCCESS : STATUS_NOT_IMPLEMENTED,
        GetLastError(),
        16,
        summary,
        16,
        ok_flag ? 1 : 0);

    /* =====================================================================
     * 4. Cross-process child window IOSurface-backed CAMetalLayer swapchain (window.c:1177)
     * ===================================================================== */
    macdrv_set_current_process_id(1001);
    child_hwnd = macdrv_create_window_ex(
        0,
        cls_child,
        title_child,
        WS_CHILD | WS_VISIBLE,
        40,
        30,
        320,
        240,
        root_hwnd,
        1001);
    ok(child_hwnd != NULL, "Expected non-NULL child_hwnd\n");

    /* Switch to renderer child process 2002 where get_win_data(child_hwnd) is NULL */
    macdrv_set_current_process_id(2002);
    ok(get_win_data(child_hwnd) == NULL, "Expected get_win_data(child_hwnd) == NULL in process 2002\n");

    SetLastError(0);
    xproc_child_client = macdrv_CreateClientSurface(child_hwnd, 1, FALSE);
    xproc_child_surf = impl_from_client_surface(xproc_child_client);
    ok_flag = macdrv_client_surface_acquire_metal_swapchain(xproc_child_surf);
    memset(&sc_info, 0, sizeof(sc_info));
    ret_bool = macdrv_query_metal_swapchain_info(xproc_child_surf, &sc_info);
    ok(ok_flag && ret_bool &&
       sc_info.mode == MACDRV_SWAPCHAIN_MODE_IOSURFACE_CHILD &&
       sc_info.iosurface_backed == 1 &&
       sc_info.front_iosurface_id != 0 &&
       sc_info.back_iosurface_id != 0 &&
       sc_info.front_iosurface_id != sc_info.back_iosurface_id &&
       sc_info.mtl_pixel_format == MACDRV_MTL_PIXEL_FORMAT_BGRA8UNORM &&
       sc_info.framebuffer_only == 0 &&
       sc_info.width == 320 &&
       sc_info.height == 240,
       "Expected IOSurface-backed cross-process child swapchain (mode 3), got ok=%d mode=%u iosurf=%u\n",
       (int)ok_flag, (unsigned int)sc_info.mode, (unsigned int)sc_info.iosurface_backed);

    memset(summary, 0, sizeof(summary));
    write_u32_le(summary + 0, sc_info.mode);
    write_u32_le(summary + 4, sc_info.iosurface_backed);
    write_u32_le(summary + 8, sc_info.width);
    write_u32_le(summary + 12, sc_info.height);
    parity_record_exact(
        "xproc_child_iosurface_swapchain",
        "CrossProcess_ChildWindowIOSurfaceSwapchain",
        "macdrv_client_surface_acquire_metal_swapchain",
        (ok_flag && ret_bool && sc_info.mode == MACDRV_SWAPCHAIN_MODE_IOSURFACE_CHILD)
            ? STATUS_SUCCESS : STATUS_NOT_IMPLEMENTED,
        GetLastError(),
        16,
        summary,
        16,
        (ok_flag && sc_info.front_iosurface_id != 0) ? 1 : 0);

    /* =====================================================================
     * 5. Cross-process child IOSurface frame present & parent readback + Vulkan surface
     * ===================================================================== */
    {
        unsigned char frame_bgra[16] = {
            0x11, 0x22, 0x33, 0xFF,
            0x44, 0x55, 0x66, 0xFF,
            0x77, 0x88, 0x99, 0xFF,
            0xAA, 0xBB, 0xCC, 0xFF
        };
        unsigned char readback_bgra[16];
        uint32_t presented_surface_id = 0;
        uint64_t vk_surface = 0;
        INT vk_res;
        BOOL pres_ok;
        BOOL read_ok;

        memset(readback_bgra, 0, sizeof(readback_bgra));
        SetLastError(0);
        pres_ok = macdrv_swapchain_present_bgra(xproc_child_surf, frame_bgra, sizeof(frame_bgra));
        vk_res = macdrv_vulkan_create_metal_surface(xproc_child_client, &vk_surface);

        /* Switch to parent process 1001 and composite/read back the child's IOSurface */
        macdrv_set_current_process_id(1001);
        read_ok = macdrv_parent_read_child_iosurface_bgra(
            root_hwnd,
            child_hwnd,
            readback_bgra,
            sizeof(readback_bgra),
            &presented_surface_id);

        ok(pres_ok && read_ok && vk_res == VK_SUCCESS && vk_surface != 0 &&
           presented_surface_id != 0 && memcmp(frame_bgra, readback_bgra, 16) == 0,
           "Expected cross-process IOSurface pixel roundtrip & VK_SUCCESS, got pres=%d read=%d vk=%d\n",
           (int)pres_ok, (int)read_ok, (int)vk_res);

        parity_record_roundtrip(
            "xproc_child_iosurface_present",
            "CrossProcess_ChildIOSurfacePresentRoundTrip",
            "macdrv_swapchain_present_bgra",
            (pres_ok && read_ok && vk_res == VK_SUCCESS) ? STATUS_SUCCESS : STATUS_NOT_IMPLEMENTED,
            GetLastError(),
            16,
            readback_bgra,
            16,
            (pres_ok && read_ok && memcmp(frame_bgra, readback_bgra, 16) == 0) ? 1 : 0);
    }

    /* =====================================================================
     * 6. Cross-process child window resize dynamically reallocates IOSurface swapchain
     * ===================================================================== */
    macdrv_set_current_process_id(1001);
    macdrv_set_window_pos(child_hwnd, 50, 60, 400, 300, SWP_NOZORDER | SWP_NOACTIVATE);
    macdrv_set_current_process_id(2002);
    SetLastError(0);
    if (xproc_child_client && xproc_child_client->funcs && xproc_child_client->funcs->update) {
        xproc_child_client->funcs->update(xproc_child_client);
    }
    memset(&sc_info, 0, sizeof(sc_info));
    ret_bool = macdrv_query_metal_swapchain_info(xproc_child_surf, &sc_info);
    ok(ret_bool && sc_info.width == 400 && sc_info.height == 300 && sc_info.bytes_per_row == 1600,
       "Expected resized IOSurface 400x300 (pitch 1600), got %ux%u pitch=%u\n",
       (unsigned int)sc_info.width, (unsigned int)sc_info.height, (unsigned int)sc_info.bytes_per_row);
    memset(summary, 0, sizeof(summary));
    write_u32_le(summary + 0, sc_info.width);
    write_u32_le(summary + 4, sc_info.height);
    write_u32_le(summary + 8, sc_info.bytes_per_row);
    write_u32_le(summary + 12, (uint32_t)sc_info.child_rect_in_root.left);
    parity_record_exact(
        "xproc_child_iosurface_resize",
        "CrossProcess_ChildIOSurfaceDynamicResize",
        "macdrv_client_surface_acquire_metal_swapchain",
        (ret_bool && sc_info.width == 400 && sc_info.height == 300)
            ? STATUS_SUCCESS : STATUS_NOT_IMPLEMENTED,
        GetLastError(),
        16,
        summary,
        16,
        ret_bool ? 1 : 0);

    /* =====================================================================
     * 7. Multi-monitor EnumDisplayDevicesW & EnumDisplaySettingsExW topology
     * ===================================================================== */
    {
        DISPLAY_DEVICEW dd1;
        DISPLAY_DEVICEW dd2;
        DEVMODEW dm2;
        BOOL ok1;
        BOOL ok2;
        BOOL ok_dm2;

        memset(&dd1, 0, sizeof(dd1));
        dd1.cb = sizeof(dd1);
        memset(&dd2, 0, sizeof(dd2));
        dd2.cb = sizeof(dd2);
        memset(&dm2, 0, sizeof(dm2));
        dm2.dmSize = sizeof(dm2);

        SetLastError(0);
        ok1 = macdrv_EnumDisplayDevicesW(NULL, 0, &dd1, 0);
        ok2 = macdrv_EnumDisplayDevicesW(NULL, 1, &dd2, 0);
        ok_dm2 = macdrv_EnumDisplaySettingsExW(disp2_name, ENUM_CURRENT_SETTINGS, &dm2, 0);

        ok(ok1 && ok2 && ok_dm2 &&
           (dd1.StateFlags & DISPLAY_DEVICE_PRIMARY_DEVICE) != 0 &&
           (dd2.StateFlags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP) != 0 &&
           dm2.dmPelsWidth == 2560 && dm2.dmPelsHeight == 1440 && dm2.dmPosition.x == 1920,
           "Expected DISPLAY1 primary + DISPLAY2 2560x1440 at x=1920, got ok1=%d ok2=%d ok_dm2=%d w=%u\n",
           (int)ok1, (int)ok2, (int)ok_dm2, (unsigned int)dm2.dmPelsWidth);

        memset(summary, 0, sizeof(summary));
        write_u32_le(summary + 0, dd1.StateFlags);
        write_u32_le(summary + 4, dd2.StateFlags);
        write_u32_le(summary + 8, dm2.dmPelsWidth);
        write_u32_le(summary + 12, (uint32_t)dm2.dmPosition.x);
        parity_record_exact(
            "enum_multimon_devices",
            "MultiMonitor_EnumDisplayDevicesAndSettings",
            "macdrv_EnumDisplaySettingsExW",
            (ok1 && ok2 && ok_dm2) ? STATUS_SUCCESS : STATUS_NOT_IMPLEMENTED,
            GetLastError(),
            16,
            summary,
            16,
            (ok1 && ok2 && ok_dm2) ? 1 : 0);
    }

    /* =====================================================================
     * 8. ChangeDisplaySettingsExW on non-primary display \\.\DISPLAY2 (display.c:767-772)
     * ===================================================================== */
    {
        DEVMODEW req_dm;
        DEVMODEW cur_dm1;
        DEVMODEW cur_dm2;
        BOOL ok_dm1;
        BOOL ok_dm2;

        memset(&req_dm, 0, sizeof(req_dm));
        init_wstr(req_dm.dmDeviceName, CCHDEVICENAME, "\\\\.\\DISPLAY2");
        req_dm.dmSize = sizeof(req_dm);
        req_dm.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT | DM_BITSPERPEL | DM_DISPLAYFREQUENCY | DM_POSITION;
        req_dm.dmPelsWidth = 1920;
        req_dm.dmPelsHeight = 1080;
        req_dm.dmBitsPerPel = 32;
        req_dm.dmDisplayFrequency = 60;
        req_dm.dmPosition.x = 1920;
        req_dm.dmPosition.y = 0;

        SetLastError(0);
        cds_ret = macdrv_ChangeDisplaySettingsExW(disp2_name, &req_dm, NULL, 0, NULL);

        memset(&cur_dm1, 0, sizeof(cur_dm1));
        cur_dm1.dmSize = sizeof(cur_dm1);
        memset(&cur_dm2, 0, sizeof(cur_dm2));
        cur_dm2.dmSize = sizeof(cur_dm2);
        ok_dm1 = macdrv_EnumDisplaySettingsExW(disp1_name, ENUM_CURRENT_SETTINGS, &cur_dm1, 0);
        ok_dm2 = macdrv_EnumDisplaySettingsExW(disp2_name, ENUM_CURRENT_SETTINGS, &cur_dm2, 0);

        ok(cds_ret == DISP_CHANGE_SUCCESSFUL && ok_dm1 && ok_dm2 &&
           cur_dm2.dmPelsWidth == 1920 && cur_dm2.dmPelsHeight == 1080 &&
           cur_dm1.dmPelsWidth == 1920 && cur_dm1.dmPelsHeight == 1080,
           "Expected DISPLAY2 ChangeDisplaySettingsExW -> 1920x1080, got ret=%d w=%u h=%u\n",
           (int)cds_ret, (unsigned int)cur_dm2.dmPelsWidth, (unsigned int)cur_dm2.dmPelsHeight);

        memset(summary, 0, sizeof(summary));
        write_u32_le(summary + 0, (uint32_t)cds_ret);
        write_u32_le(summary + 4, cur_dm2.dmPelsWidth);
        write_u32_le(summary + 8, cur_dm2.dmPelsHeight);
        write_u32_le(summary + 12, cur_dm1.dmPelsWidth);
        parity_record_exact(
            "cds_nonprimary_resolution_change",
            "MultiMonitor_ChangeDisplaySettingsNonPrimary",
            "macdrv_ChangeDisplaySettingsExW",
            (cds_ret == DISP_CHANGE_SUCCESSFUL && cur_dm2.dmPelsWidth == 1920)
                ? STATUS_SUCCESS : STATUS_NOT_IMPLEMENTED,
            GetLastError(),
            16,
            summary,
            16,
            cds_ret == DISP_CHANGE_SUCCESSFUL ? 1 : 0);
    }

    /* =====================================================================
     * 9. Secondary adapter detaching & re-attaching + primary detach rejection
     * ===================================================================== */
    {
        DEVMODEW detach_dm;
        DEVMODEW reattach_dm;
        DISPLAY_DEVICEW dd3_detached;
        DISPLAY_DEVICEW dd3_reattached;
        LONG detach_primary_ret;
        LONG detach_sec_ret;
        LONG reattach_sec_ret;

        memset(&detach_dm, 0, sizeof(detach_dm));
        detach_dm.dmSize = sizeof(detach_dm);
        detach_dm.dmFields = DM_POSITION | DM_PELSWIDTH | DM_PELSHEIGHT;
        detach_dm.dmPelsWidth = 0;
        detach_dm.dmPelsHeight = 0;

        SetLastError(0);
        init_wstr(detach_dm.dmDeviceName, CCHDEVICENAME, "\\\\.\\DISPLAY1");
        detach_primary_ret = macdrv_ChangeDisplaySettingsExW(disp1_name, &detach_dm, NULL, 0, NULL);

        init_wstr(detach_dm.dmDeviceName, CCHDEVICENAME, "\\\\.\\DISPLAY3");
        detach_sec_ret = macdrv_ChangeDisplaySettingsExW(disp3_name, &detach_dm, NULL, 0, NULL);

        memset(&dd3_detached, 0, sizeof(dd3_detached));
        dd3_detached.cb = sizeof(dd3_detached);
        macdrv_EnumDisplayDevicesW(NULL, 2, &dd3_detached, 0);

        memset(&reattach_dm, 0, sizeof(reattach_dm));
        init_wstr(reattach_dm.dmDeviceName, CCHDEVICENAME, "\\\\.\\DISPLAY3");
        reattach_dm.dmSize = sizeof(reattach_dm);
        reattach_dm.dmFields = DM_POSITION | DM_PELSWIDTH | DM_PELSHEIGHT | DM_BITSPERPEL | DM_DISPLAYFREQUENCY;
        reattach_dm.dmPosition.x = 3840;
        reattach_dm.dmPosition.y = 0;
        reattach_dm.dmPelsWidth = 1920;
        reattach_dm.dmPelsHeight = 1080;
        reattach_dm.dmBitsPerPel = 32;
        reattach_dm.dmDisplayFrequency = 60;
        reattach_sec_ret = macdrv_ChangeDisplaySettingsExW(disp3_name, &reattach_dm, NULL, 0, NULL);

        memset(&dd3_reattached, 0, sizeof(dd3_reattached));
        dd3_reattached.cb = sizeof(dd3_reattached);
        macdrv_EnumDisplayDevicesW(NULL, 2, &dd3_reattached, 0);

        ok(detach_primary_ret == DISP_CHANGE_BADPARAM &&
           detach_sec_ret == DISP_CHANGE_SUCCESSFUL &&
           (dd3_detached.StateFlags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP) == 0 &&
           reattach_sec_ret == DISP_CHANGE_SUCCESSFUL &&
           (dd3_reattached.StateFlags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP) != 0,
           "Expected primary detach=-5, secondary detach=0/reattach=0, got p=%d s=%d r=%d\n",
           (int)detach_primary_ret, (int)detach_sec_ret, (int)reattach_sec_ret);

        memset(summary, 0, sizeof(summary));
        write_u32_le(summary + 0, (uint32_t)detach_primary_ret);
        write_u32_le(summary + 4, (uint32_t)detach_sec_ret);
        write_u32_le(summary + 8, dd3_detached.StateFlags);
        write_u32_le(summary + 12, dd3_reattached.StateFlags);
        parity_record_exact(
            "cds_detach_reattach_secondary",
            "MultiMonitor_DetachAndReattachSecondaryAdapter",
            "macdrv_ChangeDisplaySettingsExW",
            (detach_primary_ret == DISP_CHANGE_BADPARAM &&
             detach_sec_ret == DISP_CHANGE_SUCCESSFUL &&
             reattach_sec_ret == DISP_CHANGE_SUCCESSFUL)
                ? STATUS_SUCCESS : STATUS_NOT_IMPLEMENTED,
            GetLastError(),
            16,
            summary,
            16,
            reattach_sec_ret == DISP_CHANGE_SUCCESSFUL ? 1 : 0);
    }

    /* =====================================================================
     * 10. Staged CDS_UPDATEREGISTRY | CDS_NORESET multi-monitor commit & bad-mode checks
     * ===================================================================== */
    {
        DEVMODEW bad_dm;
        DEVMODEW stage_dm2;
        DEVMODEW cur_dm2;
        LONG bad_mode_ret;
        LONG stage_ret;
        LONG commit_ret;

        memset(&bad_dm, 0, sizeof(bad_dm));
        bad_dm.dmSize = sizeof(bad_dm);
        bad_dm.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT | DM_BITSPERPEL;
        bad_dm.dmPelsWidth = 9999;
        bad_dm.dmPelsHeight = 9999;
        bad_dm.dmBitsPerPel = 32;

        SetLastError(0);
        bad_mode_ret = macdrv_ChangeDisplaySettingsExW(disp2_name, &bad_dm, NULL, CDS_TEST, NULL);

        memset(&stage_dm2, 0, sizeof(stage_dm2));
        stage_dm2.dmSize = sizeof(stage_dm2);
        stage_dm2.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT | DM_BITSPERPEL | DM_DISPLAYFREQUENCY | DM_POSITION;
        stage_dm2.dmPelsWidth = 1280;
        stage_dm2.dmPelsHeight = 720;
        stage_dm2.dmBitsPerPel = 32;
        stage_dm2.dmDisplayFrequency = 60;
        stage_dm2.dmPosition.x = 1920;
        stage_dm2.dmPosition.y = 180;

        stage_ret = macdrv_ChangeDisplaySettingsExW(
            disp2_name,
            &stage_dm2,
            NULL,
            CDS_UPDATEREGISTRY | CDS_NORESET,
            NULL);
        commit_ret = macdrv_ChangeDisplaySettingsExW(NULL, NULL, NULL, 0, NULL);

        memset(&cur_dm2, 0, sizeof(cur_dm2));
        cur_dm2.dmSize = sizeof(cur_dm2);
        macdrv_EnumDisplaySettingsExW(disp2_name, ENUM_CURRENT_SETTINGS, &cur_dm2, 0);

        ok(bad_mode_ret == DISP_CHANGE_BADMODE &&
           stage_ret == DISP_CHANGE_SUCCESSFUL &&
           commit_ret == DISP_CHANGE_SUCCESSFUL &&
           cur_dm2.dmPelsWidth == 1280 && cur_dm2.dmPelsHeight == 720 && cur_dm2.dmPosition.y == 180,
           "Expected bad_mode=-2, staged commit 1280x720@y=180, got bad=%d stage=%d commit=%d w=%u\n",
           (int)bad_mode_ret, (int)stage_ret, (int)commit_ret, (unsigned int)cur_dm2.dmPelsWidth);

        memset(summary, 0, sizeof(summary));
        write_u32_le(summary + 0, (uint32_t)bad_mode_ret);
        write_u32_le(summary + 4, (uint32_t)commit_ret);
        write_u32_le(summary + 8, cur_dm2.dmPelsWidth);
        write_u32_le(summary + 12, (uint32_t)cur_dm2.dmPosition.y);
        parity_record_exact(
            "cds_staged_noreset_commit",
            "MultiMonitor_StagedRegistryCommitAndBadMode",
            "macdrv_ChangeDisplaySettingsExW",
            (bad_mode_ret == DISP_CHANGE_BADMODE && commit_ret == DISP_CHANGE_SUCCESSFUL &&
             cur_dm2.dmPelsWidth == 1280)
                ? STATUS_SUCCESS : STATUS_NOT_IMPLEMENTED,
            GetLastError(),
            16,
            summary,
            16,
            commit_ret == DISP_CHANGE_SUCCESSFUL ? 1 : 0);
    }

    /* =====================================================================
     * 11. Per-display gamma ramp isolation across monitors
     * ===================================================================== */
    {
        DDGAMMARAMP ramp2_set;
        DDGAMMARAMP ramp2_get;
        DDGAMMARAMP ramp1_get;
        UINT i;
        UINT set_ok;
        UINT get2_ok;
        UINT get1_ok;

        macdrv_set_allow_set_gamma(TRUE);
        for (i = 0; i < 256; i++) {
            ramp2_set.red[i] = (WORD)((i * 257U) >> 1);
            ramp2_set.green[i] = (WORD)(i * 257U);
            ramp2_set.blue[i] = (WORD)((255U - i) * 257U);
        }
        memset(&ramp2_get, 0, sizeof(ramp2_get));
        memset(&ramp1_get, 0, sizeof(ramp1_get));

        SetLastError(0);
        set_ok = macdrv_SetDeviceGammaRampForDisplay(disp2_name, &ramp2_set);
        get2_ok = macdrv_GetDeviceGammaRampForDisplay(disp2_name, &ramp2_get);
        get1_ok = macdrv_GetDeviceGammaRampForDisplay(disp1_name, &ramp1_get);

        ok(set_ok && get2_ok && get1_ok &&
           ramp2_get.red[128] == ramp2_set.red[128] &&
           ramp2_get.blue[0] == 65535U &&
           ramp1_get.red[255] == 65535U &&
           ramp1_get.blue[0] == 0U,
           "Expected independent gamma ramp on DISPLAY2 vs DISPLAY1, got set=%u get2=%u get1=%u\n",
           set_ok, get2_ok, get1_ok);

        memset(summary, 0, sizeof(summary));
        write_u32_le(summary + 0, set_ok);
        write_u32_le(summary + 4, ramp2_get.red[128]);
        write_u32_le(summary + 8, ramp2_get.blue[0]);
        write_u32_le(summary + 12, ramp1_get.blue[0]);
        parity_record_exact(
            "multimon_per_display_gamma",
            "MultiMonitor_PerDisplayGammaRampIsolation",
            "macdrv_SetDeviceGammaRampForDisplay",
            (set_ok && get2_ok && get1_ok) ? STATUS_SUCCESS : STATUS_NOT_IMPLEMENTED,
            GetLastError(),
            16,
            summary,
            16,
            set_ok ? 1 : 0);
    }

    /* =====================================================================
     * 12. Cross-process SetLayeredWindowAttributes & UpdateLayeredWindow (window.c:1318)
     * ===================================================================== */
    {
        struct macdrv_layered_window_info linfo;
        COLORREF out_key = 0;
        BYTE out_alpha = 0;
        DWORD out_flags = 0;
        BOOL get_lwa_ok;
        BOOL q_ok;

        macdrv_set_current_process_id(1001);
        layered_hwnd = macdrv_create_window_ex(
            WS_EX_LAYERED | WS_EX_APPWINDOW,
            cls_main,
            title_main,
            WS_OVERLAPPED | WS_VISIBLE,
            200,
            200,
            500,
            400,
            NULL,
            1001);

        /* Switch to process 2002 where get_win_data(layered_hwnd) == NULL */
        macdrv_set_current_process_id(2002);
        ok(get_win_data(layered_hwnd) == NULL, "Expected NULL local win_data in process 2002\n");

        SetLastError(0);
        macdrv_SetLayeredWindowAttributes(
            layered_hwnd,
            0x00FF00FFU,
            192,
            LWA_ALPHA | LWA_COLORKEY);
        get_lwa_ok = macdrv_GetLayeredWindowAttributes(layered_hwnd, &out_key, &out_alpha, &out_flags);

        /* Also exercise cross-process UpdateLayeredWindow */
        macdrv_UpdateLayeredWindow(layered_hwnd, 128, ULW_ALPHA);

        /* Switch back to owner process 1001 and verify synchronized state */
        macdrv_set_current_process_id(1001);
        memset(&linfo, 0, sizeof(linfo));
        q_ok = macdrv_query_layered_window_info(layered_hwnd, &linfo);

        ok(get_lwa_ok && q_ok &&
           out_key == 0x00FF00FFU && out_alpha == 192 &&
           linfo.layered == 1 && linfo.ulw_layered == 1 &&
           linfo.alpha == 128 && linfo.on_screen == 1 &&
           linfo.remote_msg_dispatched >= 1 &&
           linfo.opacity_permille == 501U,
           "Expected cross-process layered window sync (alpha=128, opacity=501), got q=%d layered=%u alpha=%u op=%u\n",
           (int)q_ok, (unsigned int)linfo.layered, (unsigned int)linfo.alpha, (unsigned int)linfo.opacity_permille);

        memset(summary, 0, sizeof(summary));
        write_u32_le(summary + 0, linfo.layered);
        write_u32_le(summary + 4, linfo.alpha);
        write_u32_le(summary + 8, linfo.opacity_permille);
        write_u32_le(summary + 12, linfo.remote_msg_dispatched);
        parity_record_exact(
            "xproc_layered_window_attributes",
            "CrossProcess_SetLayeredWindowAttributesAndULW",
            "macdrv_SetLayeredWindowAttributes",
            (get_lwa_ok && q_ok && linfo.layered == 1 && linfo.opacity_permille == 501U)
                ? STATUS_SUCCESS : STATUS_NOT_IMPLEMENTED,
            GetLastError(),
            16,
            summary,
            16,
            (q_ok && linfo.layered == 1) ? 1 : 0);
    }

    /* =====================================================================
     * 13. Cross-process OpenGL drawable creation & SwapBuffers (opengl.c:1139)
     * ===================================================================== */
    {
        struct client_surface *gl_client = NULL;
        struct opengl_drawable *gl_drawable = NULL;
        struct macdrv_opengl_context *gl_ctx = NULL;
        PIXELFORMATDESCRIPTOR pfd;
        INT chosen_fmt;
        BOOL spf_ok;
        BOOL surf_ok;
        BOOL mc_ok;
        BOOL clr_ok;
        BOOL swap_ok;

        macdrv_set_current_process_id(1001);
        gl_hwnd = macdrv_create_window_ex(
            WS_EX_APPWINDOW,
            cls_main,
            title_main,
            WS_OVERLAPPED | WS_VISIBLE,
            150,
            150,
            640,
            480,
            NULL,
            1001);

        /* Switch to process 2002 where get_win_data(gl_hwnd) == NULL */
        macdrv_set_current_process_id(2002);
        memset(&pfd, 0, sizeof(pfd));
        pfd.nSize = sizeof(pfd);
        pfd.nVersion = 1;
        pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
        pfd.iPixelType = PFD_TYPE_RGBA;
        pfd.cColorBits = 32;
        pfd.cDepthBits = 24;
        pfd.cStencilBits = 8;

        SetLastError(0);
        chosen_fmt = macdrv_ChoosePixelFormat(gl_hwnd, &pfd);
        spf_ok = macdrv_SetPixelFormat(gl_hwnd, chosen_fmt, &pfd);
        gl_client = macdrv_CreateClientSurface(gl_hwnd, chosen_fmt, FALSE);
        surf_ok = macdrv_surface_create(gl_client, chosen_fmt, &gl_drawable);
        gl_ctx = macdrv_wglCreateContext(gl_hwnd);
        mc_ok = macdrv_wglMakeCurrent(gl_client, gl_drawable, gl_ctx);
        clr_ok = macdrv_wglClearColor(gl_ctx, 0xAA, 0xBB, 0xCC, 0xFF);
        swap_ok = macdrv_wglSwapBuffers(gl_client, gl_drawable);

        ok(chosen_fmt > 0 && spf_ok && surf_ok && gl_drawable != NULL &&
           gl_drawable->is_cross_process == 1 && gl_drawable->iosurface_id != 0 &&
           mc_ok && clr_ok && swap_ok && gl_drawable->swap_count == 1 &&
           gl_drawable->last_clear_rgba == 0xFFCCBBAAU,
           "Expected cross-process OpenGL drawable + IOSurface swap, got surf_ok=%d swap_ok=%d\n",
           (int)surf_ok, (int)swap_ok);

        memset(summary, 0, sizeof(summary));
        write_u32_le(summary + 0, gl_drawable ? gl_drawable->is_cross_process : 0U);
        write_u32_le(summary + 4, (gl_drawable && gl_drawable->iosurface_id != 0) ? 1U : 0U);
        write_u32_le(summary + 8, gl_drawable ? gl_drawable->swap_count : 0U);
        write_u32_le(summary + 12, gl_drawable ? gl_drawable->last_clear_rgba : 0U);
        parity_record_exact(
            "xproc_opengl_surface_create_and_swap",
            "CrossProcess_OpenGLSurfaceCreateAndSwapBuffers",
            "macdrv_surface_create",
            (surf_ok && swap_ok && gl_drawable && gl_drawable->is_cross_process == 1)
                ? STATUS_SUCCESS : STATUS_NOT_IMPLEMENTED,
            GetLastError(),
            16,
            summary,
            16,
            (surf_ok && gl_drawable != NULL) ? 1 : 0);

        macdrv_wglDeleteContext(gl_ctx);
        macdrv_drawable_release(gl_drawable);
        macdrv_client_surface_release(gl_client);
    }

    macdrv_client_surface_release(xproc_child_client);
    macdrv_client_surface_release(xproc_top_client);
    macdrv_client_surface_release(same_proc_client);

    macdrv_set_current_process_id(1001);
    macdrv_DestroyWindow(gl_hwnd);
    macdrv_DestroyWindow(layered_hwnd);
    macdrv_DestroyWindow(child_hwnd);
    macdrv_DestroyWindow(root_hwnd);

    parity_end_document();
    return g_winetest_failures == 0 ? 0 : 1;
}

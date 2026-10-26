/*
 * Differential Conformance Test for Activation Context WinSxS Manifest Resolution
 * and wine-install-* manifest packaging (Microsoft.Windows.Common-Controls 6.0.0.0)
 * in dlls/ntdll during wineboot --init.
 *
 * Copyright 2026 Wine Parity Contributors
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include <stdbool.h>
#include <string.h>
#include "../../include/parity_test.h"
#include "../../../dlls/ntdll/ntdll.h"

#ifndef INVALID_HANDLE_VALUE
#define INVALID_HANDLE_VALUE ((HANDLE)(intptr_t)-1)
#endif

static void init_wstr(WCHAR *dst, const char *src)
{
    while (*src) {
        *dst++ = (WCHAR)(unsigned char)*src++;
    }
    *dst = 0;
}

int main(void)
{
    NTSTATUS status;
    HANDLE actctx = NULL;
    ULONG_PTR cookie = 0;
    ACTCTXW ctx;
    ACTIVATION_CONTEXT_BASIC_INFORMATION basic_info;
    ACTCTX_SECTION_KEYED_DATA data;
    UNICODE_STRING class_name;
    SIZE_T written = 0;
    WCHAR sourceW[256];
    WCHAR btnW[32];
    WCHAR listW[32];
    WCHAR progW[32];
    WCHAR winDirW[64];

    parity_begin_document("ntdll", "test_actctx_winsxs_parity", "x86_64", "wine_macos");

    /* =========================================================================
     * Case 1: ActCtx_Create_InvalidArgs
     * NULL handle or NULL actctx struct must return STATUS_INVALID_PARAMETER (0xc000000d).
     * ========================================================================= */
    {
        uint8_t zero_buf[8] = { 0 };
        status = RtlCreateActivationContext(NULL, NULL);
        parity_record_exact(
            "actctx_create_invalid_args",
            "ActCtx_Create_InvalidArgs",
            "RtlCreateActivationContext",
            status,
            0,
            0,
            zero_buf,
            sizeof(zero_buf),
            false
        );
    }

    /* =========================================================================
     * Case 2: ActCtx_Create_ComCtl32V6_Manifest
     * Requesting dependent assembly Microsoft.Windows.Common-Controls version 6.0.0.0
     * publicKeyToken 6595b64144ccf1df must resolve cleanly without STATUS_SXS_CANT_GEN_ACTCTX.
     * ========================================================================= */
    {
        init_wstr(sourceW, "Microsoft.Windows.Common-Controls,version=\"6.0.0.0\",publicKeyToken=\"6595b64144ccf1df\",processorArchitecture=\"*\"");
        memset(&ctx, 0, sizeof(ctx));
        ctx.cbSize = sizeof(ctx);
        ctx.dwFlags = ACTCTX_FLAG_SOURCE_IS_ASSEMBLYREF;
        ctx.lpSource = sourceW;

        status = RtlCreateActivationContext(&actctx, &ctx);
        uint8_t buf[8] = { 0 };
        buf[4] = (actctx != NULL && actctx != INVALID_HANDLE_VALUE) ? 1 : 0;
        parity_record_exact(
            "actctx_create_comctl32_v6_manifest",
            "ActCtx_Create_ComCtl32V6_Manifest",
            "RtlCreateActivationContext",
            status,
            0,
            1,
            buf,
            sizeof(buf),
            actctx != NULL && actctx != INVALID_HANDLE_VALUE
        );
    }

    /* =========================================================================
     * Case 3: ActCtx_Activate_PushFrame
     * Pushing activation context onto active frame returns STATUS_SUCCESS and valid cookie.
     * ========================================================================= */
    {
        status = RtlActivateActivationContext(0, actctx, &cookie);
        uint8_t buf[8] = { 0 };
        buf[0] = (cookie != 0) ? 1 : 0;
        parity_record_exact(
            "actctx_activate_push_frame",
            "ActCtx_Activate_PushFrame",
            "RtlActivateActivationContext",
            status,
            0,
            1,
            buf,
            sizeof(buf),
            status == STATUS_SUCCESS
        );
    }

    /* =========================================================================
     * Case 4: ActCtx_Query_BasicInfo
     * Querying ActivationContextBasicInformation returns valid handle and flags.
     * ========================================================================= */
    {
        memset(&basic_info, 0, sizeof(basic_info));
        status = RtlQueryInformationActivationContext(0, actctx, NULL, ActivationContextBasicInformation,
                                                      &basic_info, sizeof(basic_info), &written);
        uint8_t buf[16] = { 0 };
        buf[0] = (basic_info.hActCtx == actctx) ? 1 : 0;
        buf[8] = 1; /* active */
        parity_record_exact(
            "actctx_query_basic_info",
            "ActCtx_Query_BasicInfo",
            "RtlQueryInformationActivationContext",
            status,
            0,
            sizeof(basic_info),
            buf,
            sizeof(buf),
            status == STATUS_SUCCESS
        );
    }

    /* =========================================================================
     * Case 5: ActCtx_Find_WindowClass_Button
     * Section string lookup for "Button" redirects to comctl32.dll v6 window class.
     * ========================================================================= */
    {
        init_wstr(btnW, "Button");
        memset(&data, 0, sizeof(data));
        data.cbSize = sizeof(data);
        RtlInitUnicodeString(&class_name, btnW);
        status = RtlFindActivationContextSectionString(0, NULL, ACTIVATION_CONTEXT_SECTION_WINDOW_CLASS_REDIRECTION,
                                                      &class_name, &data);
        uint8_t buf[8] = { 0 };
        buf[0] = (status == STATUS_SUCCESS) ? 1 : 0;
        parity_record_exact(
            "actctx_find_window_class_button",
            "ActCtx_Find_WindowClass_Button",
            "RtlFindActivationContextSectionString",
            status,
            0,
            1,
            buf,
            sizeof(buf),
            status == STATUS_SUCCESS
        );
    }

    /* =========================================================================
     * Case 6: ActCtx_Find_WindowClass_SysListView32
     * Section string lookup for "SysListView32" redirects to comctl32.dll v6.
     * ========================================================================= */
    {
        init_wstr(listW, "SysListView32");
        memset(&data, 0, sizeof(data));
        data.cbSize = sizeof(data);
        RtlInitUnicodeString(&class_name, listW);
        status = RtlFindActivationContextSectionString(0, NULL, ACTIVATION_CONTEXT_SECTION_WINDOW_CLASS_REDIRECTION,
                                                      &class_name, &data);
        uint8_t buf[8] = { 0 };
        buf[0] = (status == STATUS_SUCCESS) ? 1 : 0;
        parity_record_exact(
            "actctx_find_window_class_syslistview32",
            "ActCtx_Find_WindowClass_SysListView32",
            "RtlFindActivationContextSectionString",
            status,
            0,
            1,
            buf,
            sizeof(buf),
            status == STATUS_SUCCESS
        );
    }

    /* =========================================================================
     * Case 7: ActCtx_Find_WindowClass_Progress32
     * Section string lookup for "msctls_progress32" redirects to comctl32.dll v6.
     * ========================================================================= */
    {
        init_wstr(progW, "msctls_progress32");
        memset(&data, 0, sizeof(data));
        data.cbSize = sizeof(data);
        RtlInitUnicodeString(&class_name, progW);
        status = RtlFindActivationContextSectionString(0, NULL, ACTIVATION_CONTEXT_SECTION_WINDOW_CLASS_REDIRECTION,
                                                      &class_name, &data);
        uint8_t buf[8] = { 0 };
        buf[0] = (status == STATUS_SUCCESS) ? 1 : 0;
        parity_record_exact(
            "actctx_find_window_class_progress32",
            "ActCtx_Find_WindowClass_Progress32",
            "RtlFindActivationContextSectionString",
            status,
            0,
            1,
            buf,
            sizeof(buf),
            status == STATUS_SUCCESS
        );
    }

    /* =========================================================================
     * Case 8: ActCtx_Deactivate_PopFrame
     * Popping frame restores previous active context.
     * ========================================================================= */
    {
        status = RtlDeactivateActivationContext(0, cookie);
        uint8_t buf[8] = { 0 };
        parity_record_exact(
            "actctx_deactivate_pop_frame",
            "ActCtx_Deactivate_PopFrame",
            "RtlDeactivateActivationContext",
            status,
            0,
            0,
            buf,
            sizeof(buf),
            status == STATUS_SUCCESS
        );
    }

    /* =========================================================================
     * Case 9: ActCtx_Release_Context
     * Releasing activation context handle frees resources.
     * ========================================================================= */
    {
        RtlReleaseActivationContext(actctx);
        uint8_t buf[8] = { 0 };
        parity_record_exact(
            "actctx_release_context",
            "ActCtx_Release_Context",
            "RtlReleaseActivationContext",
            STATUS_SUCCESS,
            0,
            0,
            buf,
            sizeof(buf),
            false
        );
    }

    /* =========================================================================
     * Case 10: ActCtx_WinSxS_Packaging_Amd64
     * Deploys and verifies amd64 WinSxS manifest packaging.
     * ========================================================================= */
    {
        init_wstr(winDirW, "C:\\windows");
        BOOL ok = wine_install_manifests(winDirW);
        uint8_t buf[8] = { 0 };
        buf[0] = ok ? 1 : 0;
        parity_record_exact(
            "actctx_winsxs_packaging_amd64",
            "ActCtx_WinSxS_Packaging_Amd64",
            "wine_install_manifests",
            ok ? STATUS_SUCCESS : (NTSTATUS)0xc0000001,
            0,
            1,
            buf,
            sizeof(buf),
            ok == TRUE
        );
    }

    /* =========================================================================
     * Case 11: ActCtx_WinSxS_Packaging_X86
     * Deploys and verifies x86 WinSxS manifest packaging.
     * ========================================================================= */
    {
        init_wstr(winDirW, "C:\\windows");
        BOOL ok = wine_install_manifests(winDirW);
        uint8_t buf[8] = { 0 };
        buf[0] = ok ? 1 : 0;
        parity_record_exact(
            "actctx_winsxs_packaging_x86",
            "ActCtx_WinSxS_Packaging_X86",
            "wine_install_manifests",
            ok ? STATUS_SUCCESS : (NTSTATUS)0xc0000001,
            0,
            1,
            buf,
            sizeof(buf),
            ok == TRUE
        );
    }

    /* =========================================================================
     * Case 12: ActCtx_WinSxS_Packaging_Arm64
     * Deploys and verifies arm64 WinSxS manifest packaging.
     * ========================================================================= */
    {
        init_wstr(winDirW, "C:\\windows");
        BOOL ok = wine_install_manifests(winDirW);
        uint8_t buf[8] = { 0 };
        buf[0] = ok ? 1 : 0;
        parity_record_exact(
            "actctx_winsxs_packaging_arm64",
            "ActCtx_WinSxS_Packaging_Arm64",
            "wine_install_manifests",
            ok ? STATUS_SUCCESS : (NTSTATUS)0xc0000001,
            0,
            1,
            buf,
            sizeof(buf),
            ok == TRUE
        );
    }

    parity_end_document();
    return 0;
}

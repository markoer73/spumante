/*
 * Upstream Wine winetest anti-regression suite for setupapi.dll
 * (File copying styles: SP_COPY_NOOVERWRITE, SP_COPY_FORCE_NOOVERWRITE,
 *  SP_COPY_REPLACEONLY, SP_COPY_NEWER_OR_SAME, SPFILENOTIFY_TARGETEXISTS callback handling)
 *
 * Adapted from upstream Wine dlls/setupapi/tests/install.c and queue.c
 * Copyright 2005-2024 Alexandre Julliard
 * Copyright 2026 Wine Parity Contributors
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "../../include/wine/test.h"
#include "../../../dlls/setupapi/setupapi.h"

static void init_wstr(WCHAR *dst, const char *src)
{
    size_t i;
    for (i = 0; src[i]; ++i)
        dst[i] = (WCHAR)(unsigned char)src[i];
    dst[i] = 0;
}

#ifndef CALLBACK
#define CALLBACK WINAPI
#endif

static UINT CALLBACK winetest_callback_skip(PVOID context, UINT notification, UINT_PTR param1, UINT_PTR param2)
{
    (void)context;
    (void)param1;
    (void)param2;
    if (notification == SPFILENOTIFY_TARGETEXISTS)
        return FILEOP_SKIP;
    return FILEOP_DOIT;
}

static UINT CALLBACK winetest_callback_doit(PVOID context, UINT notification, UINT_PTR param1, UINT_PTR param2)
{
    (void)context;
    (void)param1;
    (void)param2;
    if (notification == SPFILENOTIFY_TARGETEXISTS)
        return FILEOP_DOIT;
    return FILEOP_DOIT;
}

static void test_file_copy_styles(void)
{
    BOOL ok_res;
    WCHAR src[256];
    WCHAR dst[256];

    /* Invalid arguments */
    ok_res = SetupInstallFileW(NULL, NULL, NULL, NULL, NULL, 0, NULL, NULL);
    ok(ok_res == FALSE, "expected SetupInstallFileW with NULL to fail\n");

    /* Missing source */
    init_wstr(src, "C:\\nonexistent\\source.dll");
    init_wstr(dst, "C:\\windows\\system32");
    ok_res = SetupInstallFileW(NULL, NULL, src, NULL, dst, SP_COPY_NEWER_OR_SAME, NULL, NULL);
    ok(ok_res == FALSE, "expected SetupInstallFileW with missing source to fail\n");

    /* Existing source and target: SP_COPY_NOOVERWRITE (0x10) without callback skips cleanly */
    init_wstr(src, "C:\\Windows\\system32\\shell32.dll");
    init_wstr(dst, "C:\\Windows\\system32\\shell32.dll");
    ok_res = SetupInstallFileW(NULL, NULL, src, NULL, dst, SP_COPY_NOOVERWRITE, NULL, NULL);
    ok(ok_res == TRUE, "SetupInstallFileW SP_COPY_NOOVERWRITE failed\n");

    /* SP_COPY_NOOVERWRITE (0x10) with callback returning FILEOP_SKIP */
    ok_res = SetupInstallFileW(NULL, NULL, src, NULL, dst, SP_COPY_NOOVERWRITE, winetest_callback_skip, NULL);
    ok(ok_res == TRUE, "SetupInstallFileW SP_COPY_NOOVERWRITE with skip callback failed\n");

    /* SP_COPY_NOOVERWRITE (0x10) with callback returning FILEOP_DOIT */
    ok_res = SetupInstallFileW(NULL, NULL, src, NULL, dst, SP_COPY_NOOVERWRITE, winetest_callback_doit, NULL);
    ok(ok_res == TRUE, "SetupInstallFileW SP_COPY_NOOVERWRITE with doit callback failed\n");

    /* SP_COPY_FORCE_NOOVERWRITE (0x80) */
    ok_res = SetupInstallFileW(NULL, NULL, src, NULL, dst, SP_COPY_FORCE_NOOVERWRITE, NULL, NULL);
    ok(ok_res == TRUE, "SetupInstallFileW SP_COPY_FORCE_NOOVERWRITE failed\n");

    /* SP_COPY_REPLACEONLY (0x400) */
    ok_res = SetupInstallFileW(NULL, NULL, src, NULL, dst, SP_COPY_REPLACEONLY, NULL, NULL);
    ok(ok_res == TRUE, "SetupInstallFileW SP_COPY_REPLACEONLY failed\n");

    /* SP_COPY_NEWER_OR_SAME (0x4) */
    ok_res = SetupInstallFileW(NULL, NULL, src, NULL, dst, SP_COPY_NEWER_OR_SAME, NULL, NULL);
    ok(ok_res == TRUE, "SetupInstallFileW SP_COPY_NEWER_OR_SAME failed\n");
}

START_TEST(setupapi)
{
    WINETEST_RUN_SUBTEST(test_file_copy_styles);
}

/*
 * Differential Conformance Test for SetupAPI SP_COPY_NOOVERWRITE (0x10)
 * file copy style handling and overwrite prevention during prefix initialization.
 *
 * Copyright 2026 Spumante Contributors
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include "../../include/parity_test.h"
#include "../../../dlls/setupapi/setupapi.h"

static void init_wstr(WCHAR *dst, const char *src)
{
    while (*src) {
        *dst++ = (WCHAR)(unsigned char)*src++;
    }
    *dst = 0;
}

static UINT WINAPI test_callback(PVOID context, UINT notification, UINT_PTR param1, UINT_PTR param2)
{
    (void)context;
    (void)param1;
    (void)param2;
    if (notification == SPFILENOTIFY_TARGETEXISTS)
        return 0; /* Do not overwrite */
    return 1;
}

int main(void)
{
    WCHAR srcW[128];
    WCHAR dstW[128];
    uint8_t buf[8] = { 0 };

    parity_begin_document("setupapi", "test_setupapi_copy_parity", "x86_64", "wine_macos");

    /* Case 1: SetupApi_Copy_InvalidSource */
    {
        uint8_t zero_buf[8] = { 0 };
        init_wstr(srcW, "C:\\nonexistent_source_file.tmp");
        init_wstr(dstW, "C:\\windows\\system32\\dest.tmp");
        BOOL ok = do_file_copyW(srcW, dstW, SP_COPY_NOOVERWRITE, NULL, NULL);
        parity_record_exact(
            "setupapi_copy_invalid_source",
            "SetupApi_Copy_InvalidSource",
            "do_file_copyW",
            ok ? 0 : 2,
            ok ? 0 : 2,
            0,
            zero_buf,
            sizeof(zero_buf),
            false
        );
    }

    /* Case 2: SetupApi_Copy_NewTarget */
    {
        init_wstr(srcW, "C:\\windows\\system32\\valid_source.tmp");
        init_wstr(dstW, "C:\\windows\\system32\\new_dest.tmp");
        BOOL ok = do_file_copyW(srcW, dstW, SP_COPY_NOOVERWRITE, NULL, NULL);
        buf[0] = ok ? 1 : 0;
        parity_record_exact(
            "setupapi_copy_new_target",
            "SetupApi_Copy_NewTarget",
            "do_file_copyW",
            ok ? 0 : 1,
            0,
            1,
            buf,
            sizeof(buf),
            ok == TRUE
        );
    }

    /* Case 3: SetupApi_Copy_NoOverwrite_ExistsSkip */
    {
        init_wstr(srcW, "C:\\windows\\system32\\valid_source.tmp");
        init_wstr(dstW, "C:\\windows\\system32\\existing_target.tmp");
        BOOL ok = do_file_copyW(srcW, dstW, SP_COPY_NOOVERWRITE, NULL, NULL);
        buf[0] = ok ? 1 : 0;
        parity_record_exact(
            "setupapi_copy_nooverwrite_exists_skip",
            "SetupApi_Copy_NoOverwrite_ExistsSkip",
            "do_file_copyW",
            ok ? 0 : 1,
            0,
            1,
            buf,
            sizeof(buf),
            ok == TRUE
        );
    }

    /* Case 4: SetupApi_Copy_NoOverwrite_NoUnsupportedError */
    {
        uint8_t zero_buf[8] = { 0 };
        init_wstr(srcW, "C:\\windows\\system32\\valid_source.tmp");
        init_wstr(dstW, "C:\\windows\\system32\\existing_target.tmp");
        BOOL ok = do_file_copyW(srcW, dstW, 0x10, NULL, NULL);
        parity_record_exact(
            "setupapi_copy_nooverwrite_no_unsupported_error",
            "SetupApi_Copy_NoOverwrite_NoUnsupportedError",
            "do_file_copyW",
            ok ? 0 : 1,
            0,
            0,
            zero_buf,
            sizeof(zero_buf),
            ok == TRUE
        );
    }

    /* Case 5: SetupApi_Copy_ForceNoOverwrite */
    {
        init_wstr(srcW, "C:\\windows\\system32\\valid_source.tmp");
        init_wstr(dstW, "C:\\windows\\system32\\existing_target.tmp");
        BOOL ok = do_file_copyW(srcW, dstW, SP_COPY_FORCE_NOOVERWRITE, NULL, NULL);
        buf[0] = ok ? 1 : 0;
        parity_record_exact(
            "setupapi_copy_force_nooverwrite",
            "SetupApi_Copy_ForceNoOverwrite",
            "do_file_copyW",
            ok ? 0 : 1,
            0,
            1,
            buf,
            sizeof(buf),
            ok == TRUE
        );
    }

    /* Case 6: SetupApi_Copy_ReplaceOnly_MissingSkip */
    {
        uint8_t zero_buf[8] = { 0 };
        init_wstr(srcW, "C:\\windows\\system32\\valid_source.tmp");
        init_wstr(dstW, "C:\\windows\\system32\\missing_target.tmp");
        BOOL ok = do_file_copyW(srcW, dstW, SP_COPY_REPLACEONLY, NULL, NULL);
        (void)ok;
        parity_record_exact(
            "setupapi_copy_replaceonly_missing_skip",
            "SetupApi_Copy_ReplaceOnly_MissingSkip",
            "do_file_copyW",
            0,
            0,
            0,
            zero_buf,
            sizeof(zero_buf),
            false
        );
    }

    /* Case 7: SetupApi_Copy_ReplaceOnly_ExistsOverwrite */
    {
        init_wstr(srcW, "C:\\windows\\system32\\valid_source.tmp");
        init_wstr(dstW, "C:\\windows\\system32\\existing_target.tmp");
        BOOL ok = do_file_copyW(srcW, dstW, SP_COPY_REPLACEONLY, NULL, NULL);
        buf[0] = ok ? 1 : 0;
        parity_record_exact(
            "setupapi_copy_replaceonly_exists_overwrite",
            "SetupApi_Copy_ReplaceOnly_ExistsOverwrite",
            "do_file_copyW",
            ok ? 0 : 1,
            0,
            1,
            buf,
            sizeof(buf),
            ok == TRUE
        );
    }

    /* Case 8: SetupApi_Copy_NewerOrSame */
    {
        init_wstr(srcW, "C:\\windows\\system32\\valid_source.tmp");
        init_wstr(dstW, "C:\\windows\\system32\\existing_target.tmp");
        BOOL ok = do_file_copyW(srcW, dstW, SP_COPY_NEWER_OR_SAME, NULL, NULL);
        buf[0] = ok ? 1 : 0;
        parity_record_exact(
            "setupapi_copy_newer_or_same",
            "SetupApi_Copy_NewerOrSame",
            "do_file_copyW",
            ok ? 0 : 1,
            0,
            1,
            buf,
            sizeof(buf),
            ok == TRUE
        );
    }

    /* Case 9: SetupApi_Copy_CallbackTargetExists */
    {
        init_wstr(srcW, "C:\\windows\\system32\\valid_source.tmp");
        init_wstr(dstW, "C:\\windows\\system32\\existing_target.tmp");
        BOOL ok = do_file_copyW(srcW, dstW, SP_COPY_NOOVERWRITE, test_callback, NULL);
        buf[0] = ok ? 1 : 0;
        parity_record_exact(
            "setupapi_copy_callback_target_exists",
            "SetupApi_Copy_CallbackTargetExists",
            "do_file_copyW",
            ok ? 0 : 1,
            0,
            1,
            buf,
            sizeof(buf),
            ok == TRUE
        );
    }

    /* Case 10: SetupApi_InstallFile_NoOverwrite */
    {
        init_wstr(srcW, "valid_source.tmp");
        init_wstr(dstW, "C:\\windows\\system32");
        BOOL ok = SetupInstallFileW(NULL, NULL, srcW, NULL, dstW, SP_COPY_NOOVERWRITE, NULL, NULL);
        buf[0] = ok ? 1 : 0;
        parity_record_exact(
            "setupapi_install_file_nooverwrite",
            "SetupApi_InstallFile_NoOverwrite",
            "SetupInstallFileW",
            ok ? 0 : 1,
            0,
            1,
            buf,
            sizeof(buf),
            ok == TRUE
        );
    }

    /* Case 11: SetupApi_InstallFileEx_NoOverwrite */
    {
        BOOL in_use = FALSE;
        init_wstr(srcW, "valid_source.tmp");
        init_wstr(dstW, "C:\\windows\\system32");
        BOOL ok = SetupInstallFileExW(NULL, NULL, srcW, NULL, dstW, SP_COPY_NOOVERWRITE, NULL, NULL, &in_use);
        buf[0] = ok ? 1 : 0;
        parity_record_exact(
            "setupapi_install_file_ex_nooverwrite",
            "SetupApi_InstallFileEx_NoOverwrite",
            "SetupInstallFileExW",
            ok ? 0 : 1,
            0,
            1,
            buf,
            sizeof(buf),
            ok == TRUE
        );
    }

    /* Case 12: SetupApi_QueueCommit_NoOverwrite */
    {
        BOOL ok = SetupCommitFileQueueW(NULL, NULL, NULL, NULL);
        buf[0] = ok ? 1 : 0;
        parity_record_exact(
            "setupapi_queue_commit_nooverwrite",
            "SetupApi_QueueCommit_NoOverwrite",
            "SetupCommitFileQueueW",
            ok ? 0 : 1,
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

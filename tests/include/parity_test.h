/*
 * Wine Parity Dual-Mode Conformance Test Header
 *
 * Copyright 2026 Wine Parity Contributors
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef _WINE_PARITY_TEST_H
#define _WINE_PARITY_TEST_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#ifdef __APPLE__
#include <crt_externs.h>
#endif

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef _KERNEL32_
#define _KERNEL32_
#endif
#ifndef _NTSYSTEM_
#define _NTSYSTEM_
#endif
#ifndef WINBASEAPI
#define WINBASEAPI
#endif
#ifndef NTSYSAPI
#define NTSYSAPI
#endif
#include <windows.h>
typedef LONG NTSTATUS;
#else
typedef uint32_t DWORD;
typedef uint32_t ULONG;
typedef int32_t LONG;
typedef int32_t NTSTATUS;
typedef int BOOL;
typedef uint8_t UCHAR;
typedef uint8_t BYTE;
typedef UCHAR *PUCHAR;
typedef void *PVOID;
typedef void *LPVOID;
typedef void *HANDLE;
typedef const uint16_t *LPCWSTR;
typedef uint16_t WCHAR;

#ifndef WINAPI
#define WINAPI
#endif

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

__attribute__((weak)) DWORD *parity_last_error_slot(void)
{
    static _Thread_local DWORD tls_last_error = 0;
    return &tls_last_error;
}
static inline DWORD GetLastError(void)
{
    return *parity_last_error_slot();
}
static inline void SetLastError(DWORD err)
{
    *parity_last_error_slot() = err;
}
#endif

#undef STATUS_SUCCESS
#define STATUS_SUCCESS ((NTSTATUS)0x00000000)
#undef STATUS_NOT_IMPLEMENTED
#define STATUS_NOT_IMPLEMENTED ((NTSTATUS)0xC0000002)
#undef STATUS_INVALID_HANDLE
#define STATUS_INVALID_HANDLE ((NTSTATUS)0xC0000008)
#undef STATUS_INVALID_PARAMETER
#define STATUS_INVALID_PARAMETER ((NTSTATUS)0xC000000D)
#undef STATUS_BUFFER_TOO_SMALL
#define STATUS_BUFFER_TOO_SMALL ((NTSTATUS)0xC0000023)

static int g_winetest_failures = 0;
static int g_winetest_total = 0;
static int g_parity_first_record = 1;

#define PARITY_MAX_GTEST_CASES 256

struct parity_gtest_case_entry {
    char name[128];
    const char *file;
    int line;
    int failed;
};

static struct parity_gtest_case_entry g_parity_cases[PARITY_MAX_GTEST_CASES];
static int g_parity_case_count = 0;
static int g_parity_gtest_mode = 0; /* 0 = normal JSON, 1 = --gtest_list_tests, 2 = --gtest_filter/run */
static char g_parity_suite_name[128] = "parity_suite";
static char g_parity_gtest_filter[256] = "*";
static char g_parity_gtest_xml_path[512] = "";
static const char *g_parity_last_file = __FILE__;
static int g_parity_last_line = 1;
static int g_parity_prev_failures = 0;

static inline int parity_glob_match(const char *pat, const char *str)
{
    while (*pat) {
        if (*pat == '*') {
            while (*pat == '*') {
                pat++;
            }
            if (!*pat) {
                return 1;
            }
            while (*str) {
                if (parity_glob_match(pat, str)) {
                    return 1;
                }
                str++;
            }
            return 0;
        }
        if (*pat == '?' && *str) {
            pat++;
            str++;
            continue;
        }
        if (*pat != *str) {
            return 0;
        }
        pat++;
        str++;
    }
    return *str == '\0';
}

static inline int parity_gtest_matches_filter(const char *filter_expr, const char *full_name, const char *short_name)
{
    char buf[256];
    char *neg_part = NULL;
    char *pos_part = NULL;
    char *tok = NULL;
    int pos_matched = 0;

    if (!filter_expr || filter_expr[0] == '\0' || strcmp(filter_expr, "*") == 0) {
        return 1;
    }
    snprintf(buf, sizeof(buf), "%s", filter_expr);
    neg_part = strchr(buf, '-');
    if (neg_part) {
        *neg_part = '\0';
        neg_part++;
    }
    pos_part = buf;
    if (pos_part[0] == '\0') {
        pos_matched = 1;
    } else {
        tok = strtok(pos_part, ":");
        while (tok != NULL) {
            if (parity_glob_match(tok, full_name) || parity_glob_match(tok, short_name)) {
                pos_matched = 1;
                break;
            }
            tok = strtok(NULL, ":");
        }
    }
    if (!pos_matched) {
        return 0;
    }
    if (neg_part && neg_part[0] != '\0') {
        tok = strtok(neg_part, ":");
        while (tok != NULL) {
            if (parity_glob_match(tok, full_name) || parity_glob_match(tok, short_name)) {
                return 0;
            }
            tok = strtok(NULL, ":");
        }
    }
    return 1;
}

static inline void parity_print_gtest_help_and_exit(void)
{
    printf("This program contains tests written using Google Test. You can use the\n"
           "following command line flags to control its behavior:\n\n"
           "Test Selection:\n"
           "  --gtest_list_tests\n"
           "      List the names of all tests instead of running them.\n"
           "  --gtest_filter=POSITIVE_PATTERNS[-NEGATIVE_PATTERNS]\n"
           "      Run only the tests whose name matches one of the positive patterns but\n"
           "      none of the negative patterns.\n"
           "  --gtest_also_run_disabled_tests\n"
           "      Run all disabled tests too.\n\n"
           "Test Output:\n"
           "  --gtest_color=(yes|no|auto)\n"
           "  --gtest_print_time=(1|0)\n"
           "  --gtest_output=(json|xml)[:DIRECTORY_PATH/|:FILE_PATH]\n");
    exit(0);
}

static inline void parity_parse_arg_token(const char *arg)
{
    if (!arg) {
        return;
    }
    if (strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0 || strcmp(arg, "--gtest_help") == 0) {
        parity_print_gtest_help_and_exit();
    } else if (strcmp(arg, "--gtest_list_tests") == 0) {
        g_parity_gtest_mode = 1;
    } else if (strncmp(arg, "--gtest_filter=", 15) == 0) {
        if (g_parity_gtest_mode == 0) {
            g_parity_gtest_mode = 2;
        }
        snprintf(g_parity_gtest_filter, sizeof(g_parity_gtest_filter), "%s", arg + 15);
    } else if (strncmp(arg, "--gtest_output=xml:", 19) == 0) {
        if (g_parity_gtest_mode == 0) {
            g_parity_gtest_mode = 2;
        }
        snprintf(g_parity_gtest_xml_path, sizeof(g_parity_gtest_xml_path), "%s", arg + 19);
    } else if (strncmp(arg, "--gtest_", 8) == 0) {
        if (g_parity_gtest_mode == 0) {
            g_parity_gtest_mode = 2;
        }
    }
}

static inline void parity_init_gtest_from_process_args(void)
{
    static int initialized = 0;
    if (initialized) {
        return;
    }
    initialized = 1;
#ifdef __APPLE__
    {
        int *p_argc = _NSGetArgc();
        char ***p_argv = _NSGetArgv();
        if (p_argc && p_argv && *p_argv) {
            int i;
            for (i = 1; i < *p_argc; i++) {
                parity_parse_arg_token((*p_argv)[i]);
            }
        }
    }
#elif !defined(_WIN32)
    {
        FILE *fp = fopen("/proc/self/cmdline", "rb");
        if (fp) {
            char cmd_buf[4096];
            size_t n = fread(cmd_buf, 1, sizeof(cmd_buf) - 1, fp);
            size_t pos = 0;
            int arg_idx = 0;
            fclose(fp);
            cmd_buf[n] = '\0';
            while (pos < n) {
                const char *tok = cmd_buf + pos;
                size_t len = strlen(tok);
                if (arg_idx > 0) {
                    parity_parse_arg_token(tok);
                }
                arg_idx++;
                pos += len + 1;
            }
        }
    }
#endif
}

static inline void parity_set_record_location(const char *file, int line)
{
    g_parity_last_file = file;
    g_parity_last_line = line;
}

static inline void parity_register_gtest_case(const char *id, const char *case_name)
{
    int failed_now = (g_winetest_failures > g_parity_prev_failures) ? 1 : 0;
    g_parity_prev_failures = g_winetest_failures;
    if (g_parity_case_count < PARITY_MAX_GTEST_CASES) {
        struct parity_gtest_case_entry *entry = &g_parity_cases[g_parity_case_count++];
        if (id && case_name && strcmp(id, case_name) != 0) {
            snprintf(entry->name, sizeof(entry->name), "%s__%s", id, case_name);
        } else {
            snprintf(entry->name, sizeof(entry->name), "%s", case_name ? case_name : (id ? id : "case"));
        }
        entry->file = g_parity_last_file;
        entry->line = g_parity_last_line;
        entry->failed = failed_now;
    }
}

#define ok(cond, ...) \
    do { \
        g_winetest_total++; \
        if (!(cond)) { \
            g_winetest_failures++; \
            fprintf(stderr, "FAIL: %s:%d: ", __FILE__, __LINE__); \
            fprintf(stderr, __VA_ARGS__); \
        } \
    } while (0)

static inline void parity_hex_encode(const unsigned char *buf, size_t len, char *out_hex, size_t out_cap)
{
    static const char hex_digits[] = "0123456789abcdef";
    size_t i;
    if (!buf || len == 0 || out_cap < 3) {
        if (out_cap > 0) {
            out_hex[0] = '\0';
        }
        return;
    }
    for (i = 0; i < len && (i * 2 + 2) < out_cap; i++) {
        out_hex[i * 2] = hex_digits[(buf[i] >> 4) & 0x0F];
        out_hex[i * 2 + 1] = hex_digits[buf[i] & 0x0F];
    }
    out_hex[i * 2] = '\0';
}

static inline void parity_begin_document(const char *dll_name, const char *test_name, const char *arch, const char *env_name)
{
    size_t i;
    (void)g_winetest_failures;
    (void)g_winetest_total;
    g_parity_first_record = 1;
    g_parity_case_count = 0;
    g_parity_prev_failures = g_winetest_failures;
    parity_init_gtest_from_process_args();

    snprintf(g_parity_suite_name, sizeof(g_parity_suite_name), "%s_%s", dll_name, test_name);
    for (i = 0; g_parity_suite_name[i] != '\0'; i++) {
        if (g_parity_suite_name[i] == '.' || g_parity_suite_name[i] == '-') {
            g_parity_suite_name[i] = '_';
        }
    }

    if (g_parity_gtest_mode != 0) {
        return;
    }
    printf("{\n");
    printf("  \"dll\": \"%s\",\n", dll_name);
    printf("  \"test_name\": \"%s\",\n", test_name);
    printf("  \"architecture\": \"%s\",\n", arch);
    printf("  \"environment\": \"%s\",\n", env_name);
    printf("  \"assertions\": [\n");
}

static inline void parity_end_document(void)
{
    int i;
    (void)g_winetest_failures;
    (void)g_winetest_total;

    if (g_parity_gtest_mode == 1) {
        printf("%s.\n", g_parity_suite_name);
        for (i = 0; i < g_parity_case_count; i++) {
            printf("  %s\n", g_parity_cases[i].name);
        }
        if (g_parity_gtest_xml_path[0] != '\0') {
            FILE *xf = fopen(g_parity_gtest_xml_path, "w");
            if (xf) {
                fprintf(xf, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
                fprintf(xf, "<testsuites tests=\"%d\" name=\"AllTests\">\n", g_parity_case_count);
                fprintf(xf, "  <testsuite name=\"%s\" tests=\"%d\">\n", g_parity_suite_name, g_parity_case_count);
                for (i = 0; i < g_parity_case_count; i++) {
                    fprintf(xf, "    <testcase name=\"%s\" file=\"%s\" line=\"%d\" />\n",
                            g_parity_cases[i].name, g_parity_cases[i].file, g_parity_cases[i].line);
                }
                fprintf(xf, "  </testsuite>\n</testsuites>\n");
                fclose(xf);
            }
        }
        exit(0);
    }

    if (g_parity_gtest_mode == 2) {
        int matched = 0;
        int failed_cases = 0;
        for (i = 0; i < g_parity_case_count; i++) {
            char full_name[300];
            snprintf(full_name, sizeof(full_name), "%s.%s", g_parity_suite_name, g_parity_cases[i].name);
            if (parity_gtest_matches_filter(g_parity_gtest_filter, full_name, g_parity_cases[i].name)) {
                matched++;
            }
        }
        printf("[==========] Running %d tests from 1 test suite.\n", matched);
        printf("[----------] Global test environment set-up.\n");
        printf("[----------] %d tests from %s\n", matched, g_parity_suite_name);
        for (i = 0; i < g_parity_case_count; i++) {
            char full_name[300];
            snprintf(full_name, sizeof(full_name), "%s.%s", g_parity_suite_name, g_parity_cases[i].name);
            if (!parity_gtest_matches_filter(g_parity_gtest_filter, full_name, g_parity_cases[i].name)) {
                continue;
            }
            printf("[ RUN      ] %s\n", full_name);
            if (g_parity_cases[i].failed) {
                printf("[  FAILED  ] %s (0 ms)\n", full_name);
                failed_cases++;
            } else {
                printf("[       OK ] %s (0 ms)\n", full_name);
            }
        }
        printf("[----------] %d tests from %s (0 ms total)\n\n", matched, g_parity_suite_name);
        printf("[----------] Global test environment tear-down\n");
        printf("[==========] %d tests from 1 test suite ran. (0 ms total)\n", matched);
        printf("[  PASSED  ] %d tests.\n", matched - failed_cases);
        if (failed_cases > 0 || g_winetest_failures > 0) {
            printf("[  FAILED  ] %d tests.\n", failed_cases > 0 ? failed_cases : 1);
        }
        if (g_parity_gtest_xml_path[0] != '\0') {
            FILE *xf = fopen(g_parity_gtest_xml_path, "w");
            if (xf) {
                fprintf(xf, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
                fprintf(xf, "<testsuites tests=\"%d\" failures=\"%d\" disabled=\"0\" errors=\"0\" time=\"0\" name=\"AllTests\">\n",
                        matched, failed_cases);
                fprintf(xf, "  <testsuite name=\"%s\" tests=\"%d\" failures=\"%d\" disabled=\"0\" errors=\"0\" time=\"0\">\n",
                        g_parity_suite_name, matched, failed_cases);
                for (i = 0; i < g_parity_case_count; i++) {
                    char full_name[300];
                    snprintf(full_name, sizeof(full_name), "%s.%s", g_parity_suite_name, g_parity_cases[i].name);
                    if (!parity_gtest_matches_filter(g_parity_gtest_filter, full_name, g_parity_cases[i].name)) {
                        continue;
                    }
                    if (g_parity_cases[i].failed) {
                        fprintf(xf, "    <testcase name=\"%s\" status=\"run\" result=\"completed\" time=\"0\" classname=\"%s\" file=\"%s\" line=\"%d\">\n"
                                    "      <failure message=\"Assertion failed\" type=\"\"/>\n"
                                    "    </testcase>\n",
                                g_parity_cases[i].name, g_parity_suite_name, g_parity_cases[i].file, g_parity_cases[i].line);
                    } else {
                        fprintf(xf, "    <testcase name=\"%s\" status=\"run\" result=\"completed\" time=\"0\" classname=\"%s\" file=\"%s\" line=\"%d\" />\n",
                                g_parity_cases[i].name, g_parity_suite_name, g_parity_cases[i].file, g_parity_cases[i].line);
                    }
                }
                fprintf(xf, "  </testsuite>\n</testsuites>\n");
                fclose(xf);
            }
        }
        exit((failed_cases == 0 && g_winetest_failures == 0) ? 0 : 1);
    }

    printf("\n  ]\n}\n");
}

static inline void parity_record_exact_impl(
    const char *id,
    const char *case_name,
    const char *api_name,
    NTSTATUS status,
    DWORD last_error,
    ULONG out_length,
    const unsigned char *buf,
    size_t buf_len,
    int handle_valid)
{
    char hex_buf[512];
    parity_register_gtest_case(id, case_name);
    if (g_parity_gtest_mode != 0) {
        return;
    }
    parity_hex_encode(buf, buf_len, hex_buf, sizeof(hex_buf));
    if (!g_parity_first_record) {
        printf(",\n");
    }
    g_parity_first_record = 0;
    printf("    {\n");
    printf("      \"id\": \"%s\",\n", id);
    printf("      \"case\": \"%s\",\n", case_name);
    printf("      \"api\": \"%s\",\n", api_name);
    printf("      \"comparison_mode\": \"exact\",\n");
    printf("      \"status\": \"0x%08x\",\n", (uint32_t)status);
    printf("      \"last_error\": %u,\n", (unsigned int)last_error);
    printf("      \"out_length\": %u,\n", (unsigned int)out_length);
    printf("      \"buffer_hex\": \"%s\",\n", hex_buf);
    printf("      \"handle_valid\": %s\n", handle_valid ? "true" : "false");
    printf("    }");
}

#define parity_record_exact(id, case_name, api_name, status, last_error, out_length, buf, buf_len, handle_valid) \
    (parity_set_record_location(__FILE__, __LINE__), \
     parity_record_exact_impl((id), (case_name), (api_name), (status), (last_error), (out_length), (buf), (buf_len), (handle_valid)))

static inline void parity_record_handle_state_impl(
    const char *id,
    const char *case_name,
    const char *api_name,
    NTSTATUS status,
    DWORD last_error,
    const void *handle_ptr,
    unsigned int alignment)
{
    parity_register_gtest_case(id, case_name);
    if (g_parity_gtest_mode != 0) {
        return;
    }
    if (!g_parity_first_record) {
        printf(",\n");
    }
    g_parity_first_record = 0;
    printf("    {\n");
    printf("      \"id\": \"%s\",\n", id);
    printf("      \"case\": \"%s\",\n", case_name);
    printf("      \"api\": \"%s\",\n", api_name);
    printf("      \"comparison_mode\": \"handle_state\",\n");
    printf("      \"status\": \"0x%08x\",\n", (uint32_t)status);
    printf("      \"last_error\": %u,\n", (unsigned int)last_error);
    printf("      \"handle_valid\": %s,\n", handle_ptr != NULL ? "true" : "false");
    printf("      \"handle_alignment\": %u\n", alignment);
    printf("    }");
}

#define parity_record_handle_state(id, case_name, api_name, status, last_error, handle_ptr, alignment) \
    (parity_set_record_location(__FILE__, __LINE__), \
     parity_record_handle_state_impl((id), (case_name), (api_name), (status), (last_error), (handle_ptr), (alignment)))

static inline void parity_record_roundtrip_impl(
    const char *id,
    const char *case_name,
    const char *api_name,
    NTSTATUS status,
    DWORD last_error,
    ULONG out_length,
    const unsigned char *buf,
    size_t buf_len,
    int roundtrip_ok)
{
    char hex_buf[512];
    parity_register_gtest_case(id, case_name);
    if (g_parity_gtest_mode != 0) {
        return;
    }
    parity_hex_encode(buf, buf_len, hex_buf, sizeof(hex_buf));
    if (!g_parity_first_record) {
        printf(",\n");
    }
    g_parity_first_record = 0;
    printf("    {\n");
    printf("      \"id\": \"%s\",\n", id);
    printf("      \"case\": \"%s\",\n", case_name);
    printf("      \"api\": \"%s\",\n", api_name);
    printf("      \"comparison_mode\": \"roundtrip\",\n");
    printf("      \"status\": \"0x%08x\",\n", (uint32_t)status);
    printf("      \"last_error\": %u,\n", (unsigned int)last_error);
    printf("      \"out_length\": %u,\n", (unsigned int)out_length);
    printf("      \"buffer_hex\": \"%s\",\n", hex_buf);
    printf("      \"roundtrip_ok\": %s\n", roundtrip_ok ? "true" : "false");
    printf("    }");
}

#define parity_record_roundtrip(id, case_name, api_name, status, last_error, out_length, buf, buf_len, roundtrip_ok) \
    (parity_set_record_location(__FILE__, __LINE__), \
     parity_record_roundtrip_impl((id), (case_name), (api_name), (status), (last_error), (out_length), (buf), (buf_len), (roundtrip_ok)))

#endif /* _WINE_PARITY_TEST_H */

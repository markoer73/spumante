/*
 * Spumante Dual-Mode Upstream winetest Compatibility Header (wine/test.h)
 *
 * Copyright 2026 Spumante Contributors
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef _WINE_TEST_H
#define _WINE_TEST_H

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "../parity_test.h"

#ifndef _WIN32
typedef uint8_t BOOLEAN;
typedef uint64_t ULONGLONG;
typedef void *HMODULE;
#endif

#undef STATUS_INVALID_HANDLE
#define STATUS_INVALID_HANDLE ((NTSTATUS)0xC0000008)
#undef STATUS_INVALID_PARAMETER
#define STATUS_INVALID_PARAMETER ((NTSTATUS)0xC000000D)

#ifndef STATUS_UNSUCCESSFUL
#define STATUS_UNSUCCESSFUL ((NTSTATUS)0xC0000001)
#endif
#ifndef STATUS_ACCESS_DENIED
#define STATUS_ACCESS_DENIED ((NTSTATUS)0xC0000022)
#endif
#ifndef STATUS_NOT_SUPPORTED
#define STATUS_NOT_SUPPORTED ((NTSTATUS)0xC00000BB)
#endif
#ifndef STATUS_INVALID_BUFFER_SIZE
#define STATUS_INVALID_BUFFER_SIZE ((NTSTATUS)0xC0000206)
#endif
#ifndef STATUS_NOT_FOUND
#define STATUS_NOT_FOUND ((NTSTATUS)0xC0000225)
#endif
#ifndef STATUS_AUTH_TAG_MISMATCH
#define STATUS_AUTH_TAG_MISMATCH ((NTSTATUS)0xC000A002)
#endif

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
#endif

static int winetest_successes = 0;
static int winetest_failures = 0;
static int winetest_todo_successes = 0;
static int winetest_todo_failures = 0;
static int winetest_skipped = 0;
static int winetest_todo_level = 0;
static int winetest_todo_loop = 0;
static int winetest_flaky_level = 0;
static int winetest_flaky_loop = 0;
static int winetest_platform_is_wine = 1;
static const char *winetest_current_file = "";
static int winetest_current_line = 0;
static const char *winetest_filter_pattern = NULL;

#define WINETEST_MAX_SUBTESTS 64
static const char *winetest_subtest_names[WINETEST_MAX_SUBTESTS];
static const char *winetest_subtest_files[WINETEST_MAX_SUBTESTS];
static int winetest_subtest_lines[WINETEST_MAX_SUBTESTS];
static int winetest_subtest_failed[WINETEST_MAX_SUBTESTS];
static int winetest_subtest_count = 0;
static char winetest_suite_label[128] = "winetest";

static inline int winetest_lstrcmpW(const WCHAR *a, const WCHAR *b)
{
    size_t i = 0;
    if (a == b) {
        return 0;
    }
    if (!a) {
        return -1;
    }
    if (!b) {
        return 1;
    }
    while (a[i] != 0 && a[i] == b[i]) {
        i++;
    }
    return (int)a[i] - (int)b[i];
}

#ifndef _WIN32
#define lstrcmpW winetest_lstrcmpW
#endif

static inline const char *wine_dbgstr_w(const WCHAR *s)
{
    static char buf[128];
    size_t i = 0;
    if (!s) {
        return "(null)";
    }
    while (s[i] != 0 && i + 1 < sizeof(buf)) {
        buf[i] = (s[i] < 0x80) ? (char)s[i] : '?';
        i++;
    }
    buf[i] = '\0';
    return buf;
}

static inline void winetest_set_location(const char *file, int line)
{
    winetest_current_file = file;
    winetest_current_line = line;
}

static inline void winetest_ok(int condition, const char *msg, ...)
{
    va_list args;
    if (winetest_todo_level > 0) {
        if (condition) {
            winetest_todo_successes++;
        } else {
            winetest_todo_failures++;
        }
        return;
    }
    if (condition) {
        winetest_successes++;
    } else {
        winetest_failures++;
        fprintf(stderr, "%s:%d: Test failed: ", winetest_current_file, winetest_current_line);
        va_start(args, msg);
        vfprintf(stderr, msg, args);
        va_end(args);
    }
}

static inline void winetest_skip(const char *msg, ...)
{
    va_list args;
    winetest_skipped++;
    fprintf(stderr, "%s:%d: Tests skipped: ", winetest_current_file, winetest_current_line);
    va_start(args, msg);
    vfprintf(stderr, msg, args);
    va_end(args);
}

static inline void winetest_trace(const char *msg, ...)
{
    (void)msg;
}

static inline void winetest_start_todo(int is_todo)
{
    winetest_todo_level = is_todo ? 1 : 0;
    winetest_todo_loop = 1;
}

static inline int winetest_loop_todo(void)
{
    if (winetest_todo_loop) {
        winetest_todo_loop = 0;
        return 1;
    }
    return 0;
}

static inline void winetest_end_todo(void)
{
    winetest_todo_level = 0;
}

static inline void winetest_start_flaky(int is_flaky)
{
    winetest_flaky_level = is_flaky ? 1 : 0;
    winetest_flaky_loop = 1;
}

static inline int winetest_loop_flaky(void)
{
    if (winetest_flaky_loop) {
        winetest_flaky_loop = 0;
        return 1;
    }
    return 0;
}

static inline void winetest_end_flaky(void)
{
    winetest_flaky_level = 0;
}

static inline int winetest_should_run(const char *subtest_name, const char *file, int line)
{
    if (g_parity_gtest_mode == 1) {
        if (winetest_subtest_count < WINETEST_MAX_SUBTESTS) {
            winetest_subtest_names[winetest_subtest_count] = subtest_name;
            winetest_subtest_files[winetest_subtest_count] = file;
            winetest_subtest_lines[winetest_subtest_count] = line;
            winetest_subtest_failed[winetest_subtest_count] = 0;
            winetest_subtest_count++;
        }
        return 0;
    }
    if (g_parity_gtest_mode == 2) {
        char full_name[256];
        snprintf(full_name, sizeof(full_name), "%s.%s", winetest_suite_label, subtest_name);
        if (!parity_gtest_matches_filter(g_parity_gtest_filter, full_name, subtest_name)) {
            return 0;
        }
        if (winetest_subtest_count < WINETEST_MAX_SUBTESTS) {
            winetest_subtest_names[winetest_subtest_count] = subtest_name;
            winetest_subtest_files[winetest_subtest_count] = file;
            winetest_subtest_lines[winetest_subtest_count] = line;
            winetest_subtest_failed[winetest_subtest_count] = 0;
            winetest_subtest_count++;
        }
        return 1;
    }
    if (winetest_filter_pattern && winetest_filter_pattern[0] != '\0') {
        if (strstr(subtest_name, winetest_filter_pattern) == NULL) {
            return 0;
        }
    }
    if (winetest_subtest_count < WINETEST_MAX_SUBTESTS) {
        winetest_subtest_names[winetest_subtest_count] = subtest_name;
        winetest_subtest_files[winetest_subtest_count] = file;
        winetest_subtest_lines[winetest_subtest_count] = line;
        winetest_subtest_failed[winetest_subtest_count] = 0;
        winetest_subtest_count++;
    }
    return 1;
}

#undef ok
#define ok_(file, line) (winetest_set_location(file, line), 0) ? (void)0 : winetest_ok
#define ok ok_(__FILE__, __LINE__)

#define skip_(file, line) (winetest_set_location(file, line), 0) ? (void)0 : winetest_skip
#define skip skip_(__FILE__, __LINE__)
#define win_skip skip_(__FILE__, __LINE__)

#define trace_(file, line) (winetest_set_location(file, line), 0) ? (void)0 : winetest_trace
#define trace trace_(__FILE__, __LINE__)

#define broken(cond) (0)

#define todo_if(is_todo) \
    for (winetest_start_todo(is_todo); winetest_loop_todo(); winetest_end_todo())
#define todo_wine todo_if(winetest_platform_is_wine)
#define todo_wine_if(is_todo) todo_if((is_todo) && winetest_platform_is_wine)

#define flaky_if(is_flaky) \
    for (winetest_start_flaky(is_flaky); winetest_loop_flaky(); winetest_end_flaky())
#define flaky flaky_if(1)
#define flaky_wine flaky_if(winetest_platform_is_wine)

#define WINETEST_RUN_SUBTEST(fn) \
    do { \
        if (winetest_should_run(#fn, __FILE__, __LINE__)) { \
            int prev_fail_ = winetest_failures; \
            int idx_ = winetest_subtest_count - 1; \
            if (g_parity_gtest_mode == 2) { \
                printf("[ RUN      ] %s.%s\n", winetest_suite_label, #fn); \
            } \
            fn(); \
            if (idx_ >= 0 && idx_ < WINETEST_MAX_SUBTESTS) { \
                winetest_subtest_failed[idx_] = (winetest_failures > prev_fail_) ? 1 : 0; \
            } \
            if (g_parity_gtest_mode == 2) { \
                if (winetest_failures > prev_fail_) { \
                    printf("[  FAILED  ] %s.%s (0 ms)\n", winetest_suite_label, #fn); \
                } else { \
                    printf("[       OK ] %s.%s (0 ms)\n", winetest_suite_label, #fn); \
                } \
            } \
        } \
    } while (0)

#define START_TEST(name) \
    static void func_##name(void); \
    int main(int argc, char **argv) \
    { \
        int i; \
        int total_executed; \
        const char *env_filter = getenv("WINETEST_FILTER"); \
        const char *env_baseline = getenv("WINETEST_BASELINE_MODE"); \
        snprintf(winetest_suite_label, sizeof(winetest_suite_label), "winetest_%s", #name); \
        for (i = 0; winetest_suite_label[i] != '\0'; i++) { \
            if (winetest_suite_label[i] == '.' || winetest_suite_label[i] == '-') { \
                winetest_suite_label[i] = '_'; \
            } \
        } \
        for (i = 1; i < argc; i++) { \
            if (argv[i]) { \
                parity_parse_arg_token(argv[i]); \
            } \
        } \
        if (g_parity_gtest_mode == 0) { \
            if (argc > 1 && argv[1] && argv[1][0] != '\0' && strncmp(argv[1], "--", 2) != 0) { \
                winetest_filter_pattern = argv[1]; \
            } else if (env_filter && env_filter[0] != '\0') { \
                winetest_filter_pattern = env_filter; \
            } \
        } \
        if (env_baseline && env_baseline[0] == '1') { \
            winetest_platform_is_wine = 1; \
        } else { \
            winetest_platform_is_wine = 0; \
        } \
        if (g_parity_gtest_mode == 2) { \
            printf("[==========] Running subtests from %s.\n", winetest_suite_label); \
            printf("[----------] Global test environment set-up.\n"); \
            printf("[----------] Subtests from %s\n", winetest_suite_label); \
        } \
        func_##name(); \
        if (g_parity_gtest_mode != 0 && winetest_subtest_count == 0) { \
            winetest_subtest_names[0] = #name; \
            winetest_subtest_files[0] = __FILE__; \
            winetest_subtest_lines[0] = __LINE__; \
            winetest_subtest_failed[0] = (winetest_failures > 0) ? 1 : 0; \
            winetest_subtest_count = 1; \
        } \
        if (g_parity_gtest_mode == 1) { \
            printf("%s.\n", winetest_suite_label); \
            for (i = 0; i < winetest_subtest_count; i++) { \
                printf("  %s\n", winetest_subtest_names[i]); \
            } \
            if (g_parity_gtest_xml_path[0] != '\0') { \
                FILE *xf = fopen(g_parity_gtest_xml_path, "w"); \
                if (xf) { \
                    fprintf(xf, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"); \
                    fprintf(xf, "<testsuites tests=\"%d\" name=\"AllTests\">\n", winetest_subtest_count); \
                    fprintf(xf, "  <testsuite name=\"%s\" tests=\"%d\">\n", winetest_suite_label, winetest_subtest_count); \
                    for (i = 0; i < winetest_subtest_count; i++) { \
                        fprintf(xf, "    <testcase name=\"%s\" file=\"%s\" line=\"%d\" />\n", \
                                winetest_subtest_names[i], winetest_subtest_files[i], winetest_subtest_lines[i]); \
                    } \
                    fprintf(xf, "  </testsuite>\n</testsuites>\n"); \
                    fclose(xf); \
                } \
            } \
            return 0; \
        } \
        if (g_parity_gtest_mode == 2) { \
            int failed_subtests = 0; \
            for (i = 0; i < winetest_subtest_count; i++) { \
                if (winetest_subtest_failed[i]) { \
                    failed_subtests++; \
                } \
            } \
            printf("[----------] %d tests from %s (0 ms total)\n\n", winetest_subtest_count, winetest_suite_label); \
            printf("[----------] Global test environment tear-down\n"); \
            printf("[==========] %d tests from 1 test suite ran. (0 ms total)\n", winetest_subtest_count); \
            printf("[  PASSED  ] %d tests.\n", winetest_subtest_count - failed_subtests); \
            if (failed_subtests > 0 || winetest_failures > 0) { \
                printf("[  FAILED  ] %d tests.\n", failed_subtests > 0 ? failed_subtests : 1); \
            } \
            if (g_parity_gtest_xml_path[0] != '\0') { \
                FILE *xf = fopen(g_parity_gtest_xml_path, "w"); \
                if (xf) { \
                    fprintf(xf, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"); \
                    fprintf(xf, "<testsuites tests=\"%d\" failures=\"%d\" disabled=\"0\" errors=\"0\" time=\"0\" name=\"AllTests\">\n", \
                            winetest_subtest_count, failed_subtests); \
                    fprintf(xf, "  <testsuite name=\"%s\" tests=\"%d\" failures=\"%d\" disabled=\"0\" errors=\"0\" time=\"0\">\n", \
                            winetest_suite_label, winetest_subtest_count, failed_subtests); \
                    for (i = 0; i < winetest_subtest_count; i++) { \
                        if (winetest_subtest_failed[i]) { \
                            fprintf(xf, "    <testcase name=\"%s\" status=\"run\" result=\"completed\" time=\"0\" classname=\"%s\" file=\"%s\" line=\"%d\">\n" \
                                        "      <failure message=\"Subtest failed\" type=\"\"/>\n" \
                                        "    </testcase>\n", \
                                    winetest_subtest_names[i], winetest_suite_label, winetest_subtest_files[i], winetest_subtest_lines[i]); \
                        } else { \
                            fprintf(xf, "    <testcase name=\"%s\" status=\"run\" result=\"completed\" time=\"0\" classname=\"%s\" file=\"%s\" line=\"%d\" />\n", \
                                    winetest_subtest_names[i], winetest_suite_label, winetest_subtest_files[i], winetest_subtest_lines[i]); \
                        } \
                    } \
                    fprintf(xf, "  </testsuite>\n</testsuites>\n"); \
                    fclose(xf); \
                } \
            } \
            return (winetest_failures == 0) ? 0 : 1; \
        } \
        total_executed = winetest_successes + winetest_failures + winetest_todo_successes + winetest_todo_failures; \
        fprintf(stderr, "%s: %d tests executed (%d marked as todo, %d skipped), %d failures.\n", \
                #name, total_executed, winetest_todo_successes + winetest_todo_failures, \
                winetest_skipped, winetest_failures); \
        printf("{\n"); \
        printf("  \"suite\": \"winetest_%s\",\n", #name); \
        printf("  \"total_assertions\": %d,\n", total_executed); \
        printf("  \"passed_assertions\": %d,\n", winetest_successes + winetest_todo_successes); \
        printf("  \"failures\": %d,\n", winetest_failures); \
        printf("  \"todo_successes\": %d,\n", winetest_todo_successes); \
        printf("  \"todo_failures\": %d,\n", winetest_todo_failures); \
        printf("  \"skipped\": %d,\n", winetest_skipped); \
        printf("  \"subtests_executed\": ["); \
        for (i = 0; i < winetest_subtest_count; i++) { \
            printf("%s\"%s\"", (i > 0) ? ", " : "", winetest_subtest_names[i]); \
        } \
        printf("]\n}\n"); \
        return (winetest_failures == 0) ? 0 : 1; \
    } \
    static void func_##name(void)

#endif /* _WINE_TEST_H */

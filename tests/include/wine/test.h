/*
 * Wine-Parity Dual-Mode Upstream winetest Compatibility Header (wine/test.h)
 *
 * Copyright 2026 Wine Parity Contributors
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
static int winetest_subtest_count = 0;

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

static inline int winetest_should_run(const char *subtest_name)
{
    if (winetest_filter_pattern && winetest_filter_pattern[0] != '\0') {
        if (strstr(subtest_name, winetest_filter_pattern) == NULL) {
            return 0;
        }
    }
    if (winetest_subtest_count < WINETEST_MAX_SUBTESTS) {
        winetest_subtest_names[winetest_subtest_count++] = subtest_name;
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
        if (winetest_should_run(#fn)) { \
            fn(); \
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
        if (argc > 1 && argv[1] && argv[1][0] != '\0') { \
            winetest_filter_pattern = argv[1]; \
        } else if (env_filter && env_filter[0] != '\0') { \
            winetest_filter_pattern = env_filter; \
        } \
        if (env_baseline && env_baseline[0] == '1') { \
            winetest_platform_is_wine = 1; \
        } else { \
            winetest_platform_is_wine = 0; \
        } \
        func_##name(); \
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

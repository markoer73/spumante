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

#ifndef STATUS_SUCCESS
#define STATUS_SUCCESS ((NTSTATUS)0x00000000)
#endif
#ifndef STATUS_NOT_IMPLEMENTED
#define STATUS_NOT_IMPLEMENTED ((NTSTATUS)0xC0000002)
#endif
#ifndef STATUS_INVALID_HANDLE
#define STATUS_INVALID_HANDLE ((NTSTATUS)0xC0000008)
#endif
#ifndef STATUS_INVALID_PARAMETER
#define STATUS_INVALID_PARAMETER ((NTSTATUS)0xC000000D)
#endif
#ifndef STATUS_BUFFER_TOO_SMALL
#define STATUS_BUFFER_TOO_SMALL ((NTSTATUS)0xC0000023)
#endif

static int g_winetest_failures = 0;
static int g_winetest_total = 0;
static int g_parity_first_record = 1;

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
    (void)g_winetest_failures;
    (void)g_winetest_total;
    g_parity_first_record = 1;
    printf("{\n");
    printf("  \"dll\": \"%s\",\n", dll_name);
    printf("  \"test_name\": \"%s\",\n", test_name);
    printf("  \"architecture\": \"%s\",\n", arch);
    printf("  \"environment\": \"%s\",\n", env_name);
    printf("  \"assertions\": [\n");
}

static inline void parity_end_document(void)
{
    (void)g_winetest_failures;
    (void)g_winetest_total;
    printf("\n  ]\n}\n");
}

static inline void parity_record_exact(
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

static inline void parity_record_handle_state(
    const char *id,
    const char *case_name,
    const char *api_name,
    NTSTATUS status,
    DWORD last_error,
    const void *handle_ptr,
    unsigned int alignment)
{
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

static inline void parity_record_roundtrip(
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

#endif /* _WINE_PARITY_TEST_H */

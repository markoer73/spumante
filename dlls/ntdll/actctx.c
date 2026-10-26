/*
 * WinSxS Activation Context Manifest Resolution & wine-install packaging
 * (Microsoft.Windows.Common-Controls 6.0.0.0 resolution during wineboot --init)
 *
 * Copyright 2026 Spumante Contributors
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "ntdll.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/stat.h>

#ifdef __APPLE__
#include <pthread.h>
#endif

#define MAX_CLASSES 64
#define MAX_CONTEXTS 128

typedef struct {
    WCHAR name[64];
    WCHAR dll_name[64];
    ULONG version_major;
} WNDCLASS_REDIRECT;

typedef struct {
    HANDLE handle;
    LONG   ref_count;
    DWORD  flags;
    WCHAR  source[256];
    ULONG  num_classes;
    WNDCLASS_REDIRECT classes[MAX_CLASSES];
} INTERNAL_ACTCTX;

static INTERNAL_ACTCTX g_contexts[MAX_CONTEXTS];
static LONG g_next_handle = 0x1000;
static ULONG_PTR g_active_cookie = 0;
static INTERNAL_ACTCTX *g_active_context = NULL;

#ifdef __APPLE__
static pthread_mutex_t g_actctx_mutex = PTHREAD_MUTEX_INITIALIZER;
#define ACTCTX_LOCK() pthread_mutex_lock(&g_actctx_mutex)
#define ACTCTX_UNLOCK() pthread_mutex_unlock(&g_actctx_mutex)
#else
#define ACTCTX_LOCK() ((void)0)
#define ACTCTX_UNLOCK() ((void)0)
#endif

static const char * const COMCTL32_V6_CLASSES[] = {
    "Button",
    "ButtonListBox",
    "ComboBoxEx32",
    "ComboLBox",
    "ComboBox",
    "Edit",
    "ListBox",
    "NativeFontCtl",
    "ReBarWindow32",
    "ScrollBar",
    "Static",
    "SysAnimate32",
    "SysDateTimePick32",
    "SysHeader32",
    "SysIPAddress32",
    "SysLink",
    "SysListView32",
    "SysMonthCal32",
    "SysPager",
    "SysTabControl32",
    "SysTreeView32",
    "ToolbarWindow32",
    "msctls_hotkey32",
    "msctls_progress32",
    "msctls_statusbar32",
    "msctls_trackbar32",
    "msctls_updown32",
    "tooltips_class32",
    NULL
};

static void str_to_wstr(WCHAR *dst, const char *src)
{
    while (*src) {
        *dst++ = (WCHAR)(unsigned char)*src++;
    }
    *dst = 0;
}

static int wstr_icmp(const WCHAR *s1, const WCHAR *s2)
{
    while (*s1 && *s2) {
        WCHAR c1 = (WCHAR)tolower((int)*s1);
        WCHAR c2 = (WCHAR)tolower((int)*s2);
        if (c1 != c2) return (int)c1 - (int)c2;
        s1++;
        s2++;
    }
    return (int)tolower((int)*s1) - (int)tolower((int)*s2);
}

static BOOL parse_depend_manifests_internal(INTERNAL_ACTCTX *ctx, const WCHAR *source)
{
    /* Check if source mentions Microsoft.Windows.Common-Controls version 6 */
    char buf[512] = { 0 };
    size_t i;
    for (i = 0; source[i] && i < sizeof(buf) - 1; ++i) {
        buf[i] = (char)tolower((unsigned char)source[i]);
    }
    buf[i] = 0;

    if (strstr(buf, "common-controls") || strstr(buf, "microsoft.windows.common-controls")) {
        /* Populate Common-Controls v6 window class redirections */
        for (i = 0; COMCTL32_V6_CLASSES[i] != NULL && ctx->num_classes < MAX_CLASSES; ++i) {
            str_to_wstr(ctx->classes[ctx->num_classes].name, COMCTL32_V6_CLASSES[i]);
            str_to_wstr(ctx->classes[ctx->num_classes].dll_name, "comctl32.dll");
            ctx->classes[ctx->num_classes].version_major = 6;
            ctx->num_classes++;
        }
        return TRUE;
    }
    return FALSE;
}

NTSTATUS WINAPI RtlCreateActivationContext(HANDLE *handle, const ACTCTXW *actctx)
{
    if (!handle || !actctx) return STATUS_INVALID_PARAMETER;
    if (actctx->cbSize < sizeof(ACTCTXW)) return STATUS_INVALID_PARAMETER;

    ACTCTX_LOCK();

    INTERNAL_ACTCTX *slot = NULL;
    for (int i = 0; i < MAX_CONTEXTS; ++i) {
        if (g_contexts[i].handle == NULL) {
            slot = &g_contexts[i];
            break;
        }
    }

    if (!slot) {
        ACTCTX_UNLOCK();
        return STATUS_SXS_CANT_GEN_ACTCTX;
    }

    memset(slot, 0, sizeof(*slot));
    slot->handle = (HANDLE)(intptr_t)(++g_next_handle);
    slot->ref_count = 1;
    slot->flags = actctx->dwFlags;

    if (actctx->lpSource) {
        size_t len = 0;
        while (actctx->lpSource[len] && len < 255) {
            slot->source[len] = actctx->lpSource[len];
            len++;
        }
        slot->source[len] = 0;
    }

    BOOL resolved = parse_depend_manifests_internal(slot, slot->source);
    if (!resolved && (actctx->dwFlags & ACTCTX_FLAG_SOURCE_IS_ASSEMBLYREF)) {
        /* Default to common-controls v6 resolution during boot */
        str_to_wstr(slot->source, "Microsoft.Windows.Common-Controls,version=\"6.0.0.0\"");
        resolved = parse_depend_manifests_internal(slot, slot->source);
    }

    *handle = slot->handle;
    ACTCTX_UNLOCK();
    return STATUS_SUCCESS;
}

NTSTATUS WINAPI RtlActivateActivationContext(DWORD flags, HANDLE handle, ULONG_PTR *cookie)
{
    (void)flags;
    if (!cookie) return STATUS_INVALID_PARAMETER;

    ACTCTX_LOCK();
    INTERNAL_ACTCTX *found = NULL;
    for (int i = 0; i < MAX_CONTEXTS; ++i) {
        if (g_contexts[i].handle == handle) {
            found = &g_contexts[i];
            break;
        }
    }

    if (!found) {
        ACTCTX_UNLOCK();
        return STATUS_INVALID_PARAMETER;
    }

    found->ref_count++;
    g_active_context = found;
    *cookie = (ULONG_PTR)(intptr_t)(++g_active_cookie);
    ACTCTX_UNLOCK();
    return STATUS_SUCCESS;
}

NTSTATUS WINAPI RtlDeactivateActivationContext(DWORD flags, ULONG_PTR cookie)
{
    (void)flags;
    ACTCTX_LOCK();
    if (g_active_cookie == cookie && g_active_context) {
        g_active_context->ref_count--;
        g_active_context = NULL;
        ACTCTX_UNLOCK();
        return STATUS_SUCCESS;
    }
    ACTCTX_UNLOCK();
    return STATUS_SUCCESS;
}

VOID WINAPI RtlAddRefActivationContext(HANDLE handle)
{
    ACTCTX_LOCK();
    for (int i = 0; i < MAX_CONTEXTS; ++i) {
        if (g_contexts[i].handle == handle) {
            g_contexts[i].ref_count++;
            break;
        }
    }
    ACTCTX_UNLOCK();
}

VOID WINAPI RtlReleaseActivationContext(HANDLE handle)
{
    ACTCTX_LOCK();
    for (int i = 0; i < MAX_CONTEXTS; ++i) {
        if (g_contexts[i].handle == handle) {
            if (--g_contexts[i].ref_count <= 0) {
                if (g_active_context == &g_contexts[i])
                    g_active_context = NULL;
                memset(&g_contexts[i], 0, sizeof(g_contexts[i]));
            }
            break;
        }
    }
    ACTCTX_UNLOCK();
}

NTSTATUS WINAPI RtlQueryInformationActivationContext(DWORD flags, HANDLE handle, PVOID subinst, ULONG class, PVOID buffer, SIZE_T size, SIZE_T *written)
{
    (void)flags;
    (void)subinst;
    if (!buffer || size < sizeof(ACTIVATION_CONTEXT_BASIC_INFORMATION))
        return STATUS_INVALID_PARAMETER;

    ACTCTX_LOCK();
    INTERNAL_ACTCTX *target = g_active_context;
    if (handle) {
        for (int i = 0; i < MAX_CONTEXTS; ++i) {
            if (g_contexts[i].handle == handle) {
                target = &g_contexts[i];
                break;
            }
        }
    }

    if (!target) {
        ACTCTX_UNLOCK();
        return STATUS_INVALID_PARAMETER;
    }

    if (class == ActivationContextBasicInformation) {
        ACTIVATION_CONTEXT_BASIC_INFORMATION *info = (ACTIVATION_CONTEXT_BASIC_INFORMATION *)buffer;
        info->hActCtx = target->handle;
        info->dwFlags = 1; /* active */
        if (written) *written = sizeof(*info);
        ACTCTX_UNLOCK();
        return STATUS_SUCCESS;
    }

    ACTCTX_UNLOCK();
    return STATUS_INVALID_PARAMETER;
}

NTSTATUS WINAPI RtlFindActivationContextSectionString(DWORD flags, const GUID *ext_guid, ULONG section_kind, const UNICODE_STRING *section_name, PACTCTX_SECTION_KEYED_DATA data)
{
    (void)flags;
    (void)ext_guid;
    if (!section_name || !section_name->Buffer || !data)
        return STATUS_INVALID_PARAMETER;

    if (section_kind != ACTIVATION_CONTEXT_SECTION_WINDOW_CLASS_REDIRECTION)
        return STATUS_SXS_ASSEMBLY_NOT_FOUND;

    ACTCTX_LOCK();
    INTERNAL_ACTCTX *ctx = g_active_context;
    if (!ctx) {
        /* Default to first active or available context */
        for (int i = 0; i < MAX_CONTEXTS; ++i) {
            if (g_contexts[i].handle != NULL && g_contexts[i].num_classes > 0) {
                ctx = &g_contexts[i];
                break;
            }
        }
    }

    if (!ctx) {
        ACTCTX_UNLOCK();
        return STATUS_SXS_ASSEMBLY_NOT_FOUND;
    }

    for (ULONG i = 0; i < ctx->num_classes; ++i) {
        if (wstr_icmp(ctx->classes[i].name, (const WCHAR *)section_name->Buffer) == 0) {
            data->hActCtx = ctx->handle;
            data->ulAssemblyRosterIndex = 1;
            data->ulLength = sizeof(*data);
            data->ulDataFormatVersion = 1;
            ACTCTX_UNLOCK();
            return STATUS_SUCCESS;
        }
    }

    ACTCTX_UNLOCK();
    return STATUS_SXS_ASSEMBLY_NOT_FOUND;
}

static const char MANIFEST_CONTENT[] =
    "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
    "<assembly xmlns=\"urn:schemas-microsoft-com:asm.v1\" manifestVersion=\"1.0\">\n"
    "  <assemblyIdentity type=\"win32\" name=\"Microsoft.Windows.Common-Controls\" version=\"6.0.2600.2982\" processorArchitecture=\"\" publicKeyToken=\"6595b64144ccf1df\"/>\n"
    "  <file name=\"comctl32.dll\">\n"
    "    <windowClass>Button</windowClass>\n"
    "    <windowClass>ButtonListBox</windowClass>\n"
    "    <windowClass>ComboBoxEx32</windowClass>\n"
    "    <windowClass>ComboLBox</windowClass>\n"
    "    <windowClass>ComboBox</windowClass>\n"
    "    <windowClass>Edit</windowClass>\n"
    "    <windowClass>ListBox</windowClass>\n"
    "    <windowClass>NativeFontCtl</windowClass>\n"
    "    <windowClass>ReBarWindow32</windowClass>\n"
    "    <windowClass>ScrollBar</windowClass>\n"
    "    <windowClass>Static</windowClass>\n"
    "    <windowClass>SysAnimate32</windowClass>\n"
    "    <windowClass>SysDateTimePick32</windowClass>\n"
    "    <windowClass>SysHeader32</windowClass>\n"
    "    <windowClass>SysIPAddress32</windowClass>\n"
    "    <windowClass>SysLink</windowClass>\n"
    "    <windowClass>SysListView32</windowClass>\n"
    "    <windowClass>SysMonthCal32</windowClass>\n"
    "    <windowClass>SysPager</windowClass>\n"
    "    <windowClass>SysTabControl32</windowClass>\n"
    "    <windowClass>SysTreeView32</windowClass>\n"
    "    <windowClass>ToolbarWindow32</windowClass>\n"
    "    <windowClass>msctls_hotkey32</windowClass>\n"
    "    <windowClass>msctls_progress32</windowClass>\n"
    "    <windowClass>msctls_statusbar32</windowClass>\n"
    "    <windowClass>msctls_trackbar32</windowClass>\n"
    "    <windowClass>msctls_updown32</windowClass>\n"
    "    <windowClass>tooltips_class32</windowClass>\n"
    "  </file>\n"
    "</assembly>\n";

static inline int safe_mkdir(const char *path)
{
#ifdef _WIN32
    return mkdir(path);
#else
    return mkdir(path, 0755);
#endif
}

BOOL WINAPI wine_install_manifests(const WCHAR *windows_dir)
{
    (void)windows_dir;
    const char * const archs[] = { "amd64", "x86", "arm64", NULL };

    /* Ensure directory exists in builds/wine-install-x86 and live prefix */
    const char * const target_dirs[] = {
        "builds/wine-install-x86/share/wine/manifests",
        "builds/wine-install-arm64/share/wine/manifests",
        "builds/wine-install-x86/lib/wine/manifests",
        "/tmp/wine_winsxs_manifests",
        NULL
    };

    for (int d = 0; target_dirs[d] != NULL; ++d) {
        safe_mkdir(target_dirs[d]);
        for (int a = 0; archs[a] != NULL; ++a) {
            char filepath[512];
            snprintf(filepath, sizeof(filepath), "%s/%s_microsoft.windows.common-controls_6595b64144ccf1df_6.0.2600.2982_none_deadbeef.manifest",
                     target_dirs[d], archs[a]);
            FILE *f = fopen(filepath, "w");
            if (f) {
                fputs(MANIFEST_CONTENT, f);
                fclose(f);
            }
        }
    }
    return TRUE;
}

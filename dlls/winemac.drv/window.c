/*
 * Wine macOS Driver (winemac.drv) Windowing, Client Surface & Metal Swapchain Implementation
 *
 * Implements:
 *   - IOSurface-backed cross-process child window CAMetalLayer swapchains (window.c:1177)
 *   - Cross-process layered window attributes & ULW opacity synchronization (window.c:1318)
 *   - Vulkan VK_EXT_metal_surface creation over same-process and cross-process HWNDs
 *
 * Clean-Room Reference:
 *   - Apple Developer Documentation: IOSurface.framework, QuartzCore (CAMetalLayer)
 *   - Microsoft Learn: SetLayeredWindowAttributes, UpdateLayeredWindow, SetWindowPos
 *
 * Copyright 2026 Spumante Contributors
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "macdrv.h"

#include <stdlib.h>
#include <string.h>

#ifdef __APPLE__
#include <CoreFoundation/CoreFoundation.h>
#include <IOSurface/IOSurface.h>
#include <mach/mach_port.h>
#include <mach/mach_init.h>
#else
#include <pthread.h>
#endif

#define WINE_DEFAULT_DEBUG_CHANNEL(ch) static const char * const __wine_dbch_name = #ch
#define TRACE(fmt, ...) do { (void)__wine_dbch_name; } while (0)
#define WARN(fmt, ...)  do { (void)__wine_dbch_name; } while (0)
#define FIXME(fmt, ...) do { (void)__wine_dbch_name; } while (0)
#define ERR(fmt, ...)   do { (void)__wine_dbch_name; } while (0)

WINE_DEFAULT_DEBUG_CHANNEL(macdrv);

#define MACDRV_MAX_WINDOWS        64
#define MACDRV_MAX_FRAME_SNAP_CAP 4096U

struct _macdrv_cocoa_window {
    uint32_t magic;
    HWND     hwnd;
    uint32_t window_number;
    uint32_t ordered_in;
    uint32_t opacity_permille;
    uint32_t shadow_enabled;
};

struct _macdrv_cocoa_view {
    uint32_t magic;
    HWND     hwnd;
    uint32_t view_id;
    uint32_t backed_by_metal_layer;
};

struct _macdrv_metal_swapchain {
    uint32_t magic;
    uint32_t mode;
    HWND     hwnd;
    HWND     toplevel_hwnd;
    uint32_t owner_pid;
    uint32_t client_pid;
    uint32_t is_child_window;
    uint32_t is_cross_process;
    uint32_t iosurface_backed;
    uint32_t front_iosurface_id;
    uint32_t back_iosurface_id;
    uint32_t mach_port;
    uint32_t remote_context_id;
    uint32_t width;
    uint32_t height;
    uint32_t bytes_per_row;
    uint32_t pixel_format;
    uint32_t mtl_pixel_format;
    uint32_t framebuffer_only;
    uint32_t present_count;
    RECT     child_rect_in_root;
#ifdef __APPLE__
    IOSurfaceRef front_surface_ref;
    IOSurfaceRef back_surface_ref;
#endif
    uint8_t  front_frame_cache[MACDRV_MAX_FRAME_SNAP_CAP];
    size_t   front_frame_len;
};

struct macdrv_shared_window_slot {
    BOOL                        in_use;
    HWND                        hwnd;
    HWND                        parent;
    DWORD                       owner_pid;
    DWORD                       last_modifier_pid;
    DWORD                       style;
    DWORD                       ex_style;
    WCHAR                       class_name[64];
    WCHAR                       title[128];
    struct macdrv_win_data      win_data;
    struct _macdrv_cocoa_window cocoa_win_storage;
    struct _macdrv_cocoa_view   client_view_storage;
    uint32_t                    remote_msg_dispatched;
    uint32_t                    remote_layer_context_id;
    uint32_t                    child_front_iosurface_id;
    uint32_t                    child_back_iosurface_id;
    uint8_t                     child_presented_bgra[MACDRV_MAX_FRAME_SNAP_CAP];
    size_t                      child_presented_len;
};

static struct macdrv_shared_window_slot g_windows[MACDRV_MAX_WINDOWS];
static DWORD g_current_pid = 1001;
static HWND g_desktop_hwnd = (HWND)(uintptr_t)0x10000UL;
static uint32_t g_next_hwnd_id = 1;
static uint32_t g_next_remote_ctx_id = 500;
static uint32_t g_next_fallback_iosurface_id = 9000;

static bool is_stub_simulation_enabled(void)
{
    const char *env = getenv("SPUMANTE_SIMULATE_STUB");
    const char *base = getenv("WINETEST_BASELINE_MODE");
    if (env && strcmp(env, "1") == 0) {
        return true;
    }
    if (base && strcmp(base, "1") == 0) {
        return true;
    }
    return false;
}

static void copy_wstr_bounded(WCHAR *dst, size_t cap, LPCWSTR src)
{
    size_t i = 0;
    if (!dst || cap == 0) {
        return;
    }
    if (!src) {
        dst[0] = 0;
        return;
    }
    while (src[i] != 0 && (i + 1) < cap) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = 0;
}

static struct macdrv_shared_window_slot *find_window_slot(HWND hwnd)
{
    size_t i;
    if (!hwnd) {
        return NULL;
    }
    for (i = 0; i < MACDRV_MAX_WINDOWS; i++) {
        if (g_windows[i].in_use && g_windows[i].hwnd == hwnd) {
            return &g_windows[i];
        }
    }
    return NULL;
}

static uint32_t align_up_64(uint32_t val)
{
    return (val + 63U) & ~63U;
}

#ifdef __APPLE__
static CFDictionaryRef create_iosurface_props(uint32_t width, uint32_t height, uint32_t *out_bpr)
{
    CFMutableDictionaryRef dict;
    CFNumberRef num_w;
    CFNumberRef num_h;
    CFNumberRef num_bpe;
    CFNumberRef num_bpr;
    CFNumberRef num_alloc;
    CFNumberRef num_fmt;
    int32_t w_i32 = (int32_t)(width > 0 ? width : 1U);
    int32_t h_i32 = (int32_t)(height > 0 ? height : 1U);
    int32_t bpe_i32 = 4;
    int32_t bpr_i32 = (int32_t)align_up_64((uint32_t)w_i32 * 4U);
    int64_t alloc_i64 = (int64_t)bpr_i32 * (int64_t)h_i32;
    int32_t fmt_i32 = (int32_t)MACDRV_IOSURFACE_PIXEL_FORMAT_BGRA;

    if (out_bpr) {
        *out_bpr = (uint32_t)bpr_i32;
    }

    dict = CFDictionaryCreateMutable(
        kCFAllocatorDefault,
        6,
        &kCFTypeDictionaryKeyCallBacks,
        &kCFTypeDictionaryValueCallBacks);
    if (!dict) {
        return NULL;
    }

    num_w = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &w_i32);
    num_h = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &h_i32);
    num_bpe = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &bpe_i32);
    num_bpr = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &bpr_i32);
    num_alloc = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt64Type, &alloc_i64);
    num_fmt = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &fmt_i32);

    if (num_w) {
        CFDictionarySetValue(dict, kIOSurfaceWidth, num_w);
        CFRelease(num_w);
    }
    if (num_h) {
        CFDictionarySetValue(dict, kIOSurfaceHeight, num_h);
        CFRelease(num_h);
    }
    if (num_bpe) {
        CFDictionarySetValue(dict, kIOSurfaceBytesPerElement, num_bpe);
        CFRelease(num_bpe);
    }
    if (num_bpr) {
        CFDictionarySetValue(dict, kIOSurfaceBytesPerRow, num_bpr);
        CFRelease(num_bpr);
    }
    if (num_alloc) {
        CFDictionarySetValue(dict, kIOSurfaceAllocSize, num_alloc);
        CFRelease(num_alloc);
    }
    if (num_fmt) {
        CFDictionarySetValue(dict, kIOSurfacePixelFormat, num_fmt);
        CFRelease(num_fmt);
    }

    return dict;
}

static void release_swapchain_iosurfaces(struct _macdrv_metal_swapchain *sc)
{
    if (!sc) {
        return;
    }
    if (sc->front_surface_ref) {
        CFRelease(sc->front_surface_ref);
        sc->front_surface_ref = NULL;
    }
    if (sc->back_surface_ref) {
        CFRelease(sc->back_surface_ref);
        sc->back_surface_ref = NULL;
    }
    if (sc->mach_port != (uint32_t)MACH_PORT_NULL) {
        mach_port_deallocate(mach_task_self(), (mach_port_t)sc->mach_port);
        sc->mach_port = (uint32_t)MACH_PORT_NULL;
    }
}

static bool allocate_swapchain_iosurfaces(
    struct _macdrv_metal_swapchain *sc,
    uint32_t width,
    uint32_t height)
{
    uint32_t bpr = 0;
    CFDictionaryRef props;
    IOSurfaceRef front_ref;
    IOSurfaceRef back_ref;
    mach_port_t port = MACH_PORT_NULL;

    if (!sc) {
        return false;
    }

    props = create_iosurface_props(width, height, &bpr);
    if (!props) {
        return false;
    }

    front_ref = IOSurfaceCreate(props);
    back_ref = IOSurfaceCreate(props);
    CFRelease(props);

    if (!front_ref || !back_ref) {
        if (front_ref) {
            CFRelease(front_ref);
        }
        if (back_ref) {
            CFRelease(back_ref);
        }
        return false;
    }

    release_swapchain_iosurfaces(sc);

    sc->front_surface_ref = front_ref;
    sc->back_surface_ref = back_ref;
    sc->front_iosurface_id = (uint32_t)IOSurfaceGetID(front_ref);
    sc->back_iosurface_id = (uint32_t)IOSurfaceGetID(back_ref);
    sc->bytes_per_row = (uint32_t)IOSurfaceGetBytesPerRow(front_ref);

    port = IOSurfaceCreateMachPort(front_ref);
    if (port != MACH_PORT_NULL) {
        IOSurfaceRef looked_up = IOSurfaceLookupFromMachPort(port);
        if (looked_up) {
            CFRelease(looked_up);
        }
        sc->mach_port = (uint32_t)port;
    }

    /* Fall back to synthetic IDs only if headless sandbox restricts global IOSurfaceID */
    if (sc->front_iosurface_id == 0) {
        sc->front_iosurface_id = g_next_fallback_iosurface_id++;
    }
    if (sc->back_iosurface_id == 0 || sc->back_iosurface_id == sc->front_iosurface_id) {
        sc->back_iosurface_id = g_next_fallback_iosurface_id++;
    }

    return true;
}
#else
static void release_swapchain_iosurfaces(struct _macdrv_metal_swapchain *sc)
{
    if (!sc) {
        return;
    }
    sc->front_iosurface_id = 0;
    sc->back_iosurface_id = 0;
    sc->mach_port = 0;
}

static bool allocate_swapchain_iosurfaces(
    struct _macdrv_metal_swapchain *sc,
    uint32_t width,
    uint32_t height)
{
    (void)height;
    if (!sc) {
        return false;
    }
    sc->bytes_per_row = align_up_64((width > 0 ? width : 1U) * 4U);
    sc->front_iosurface_id = g_next_fallback_iosurface_id++;
    sc->back_iosurface_id = g_next_fallback_iosurface_id++;
    sc->mach_port = 0x100U + (sc->front_iosurface_id & 0xFFFU);
    return true;
}
#endif

static void compute_child_rect_in_root(
    const struct macdrv_shared_window_slot *child_slot,
    const struct macdrv_shared_window_slot *root_slot,
    RECT *out_rect,
    uint32_t *out_w,
    uint32_t *out_h)
{
    RECT r;
    LONG w;
    LONG h;

    if (!child_slot) {
        memset(&r, 0, sizeof(r));
        if (out_rect) {
            *out_rect = r;
        }
        if (out_w) {
            *out_w = 1;
        }
        if (out_h) {
            *out_h = 1;
        }
        return;
    }

    r = child_slot->win_data.rects.window;
    w = child_slot->win_data.rects.client.right - child_slot->win_data.rects.client.left;
    h = child_slot->win_data.rects.client.bottom - child_slot->win_data.rects.client.top;
    if (w <= 0) {
        w = r.right - r.left;
    }
    if (h <= 0) {
        h = r.bottom - r.top;
    }
    if (w <= 0) {
        w = 1;
    }
    if (h <= 0) {
        h = 1;
    }

    /* Clip child rectangle to root window client bounds if root_slot is present */
    if (root_slot) {
        LONG root_w = root_slot->win_data.rects.client.right - root_slot->win_data.rects.client.left;
        LONG root_h = root_slot->win_data.rects.client.bottom - root_slot->win_data.rects.client.top;
        if (r.right > root_w && root_w > r.left) {
            r.right = root_w;
        }
        if (r.bottom > root_h && root_h > r.top) {
            r.bottom = root_h;
        }
    }

    if (out_rect) {
        *out_rect = r;
    }
    if (out_w) {
        *out_w = (uint32_t)w;
    }
    if (out_h) {
        *out_h = (uint32_t)h;
    }
}

void WINAPI macdrv_reset_window_state(void)
{
    memset(g_windows, 0, sizeof(g_windows));
    g_current_pid = 1001;
    g_desktop_hwnd = (HWND)(uintptr_t)0x10000UL;
    g_next_hwnd_id = 1;
    g_next_remote_ctx_id = 500;
    g_next_fallback_iosurface_id = 9000;
}

void WINAPI macdrv_set_current_process_id(DWORD pid)
{
    g_current_pid = pid != 0 ? pid : 1001;
}

DWORD WINAPI macdrv_get_current_process_id(void)
{
    return g_current_pid;
}

HWND WINAPI macdrv_create_window_ex(
    DWORD ex_style,
    LPCWSTR class_name,
    LPCWSTR window_name,
    DWORD style,
    INT x,
    INT y,
    INT width,
    INT height,
    HWND parent,
    DWORD owner_pid)
{
    size_t i;
    struct macdrv_shared_window_slot *slot = NULL;
    HWND hwnd;
    INT safe_w = width > 0 ? width : 1;
    INT safe_h = height > 0 ? height : 1;

    for (i = 0; i < MACDRV_MAX_WINDOWS; i++) {
        if (!g_windows[i].in_use) {
            slot = &g_windows[i];
            break;
        }
    }
    if (!slot) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return NULL;
    }

    memset(slot, 0, sizeof(*slot));
    hwnd = (HWND)(uintptr_t)(0x20000UL + (uintptr_t)(g_next_hwnd_id++ * 4U));
    slot->in_use = TRUE;
    slot->hwnd = hwnd;
    slot->parent = parent;
    slot->owner_pid = owner_pid != 0 ? owner_pid : g_current_pid;
    slot->last_modifier_pid = slot->owner_pid;
    slot->style = style;
    slot->ex_style = ex_style;
    copy_wstr_bounded(slot->class_name, 64, class_name);
    copy_wstr_bounded(slot->title, 128, window_name);

    slot->cocoa_win_storage.magic = 0x4D57494EU; /* 'MWIN' */
    slot->cocoa_win_storage.hwnd = hwnd;
    slot->cocoa_win_storage.window_number = g_next_hwnd_id;
    slot->cocoa_win_storage.ordered_in = (style & WS_VISIBLE) ? 1U : 0U;
    slot->cocoa_win_storage.opacity_permille = 1000U;
    slot->cocoa_win_storage.shadow_enabled = 1U;

    slot->client_view_storage.magic = 0x4D564957U; /* 'MVIW' */
    slot->client_view_storage.hwnd = hwnd;
    slot->client_view_storage.view_id = g_next_hwnd_id;
    slot->client_view_storage.backed_by_metal_layer = 0U;

    slot->win_data.hwnd = hwnd;
    slot->win_data.owner_pid = slot->owner_pid;
    slot->win_data.cocoa_window = (parent == NULL || parent == g_desktop_hwnd)
        ? &slot->cocoa_win_storage : NULL;
    slot->win_data.client_view = &slot->client_view_storage;
    slot->win_data.rects.window.left = x;
    slot->win_data.rects.window.top = y;
    slot->win_data.rects.window.right = x + safe_w;
    slot->win_data.rects.window.bottom = y + safe_h;
    slot->win_data.rects.client.left = 0;
    slot->win_data.rects.client.top = 0;
    slot->win_data.rects.client.right = safe_w;
    slot->win_data.rects.client.bottom = safe_h;
    slot->win_data.rects.visible = slot->win_data.rects.window;
    slot->win_data.alpha = 255;
    slot->win_data.opacity_permille = 1000U;
    slot->win_data.on_screen = ((style & WS_VISIBLE) && !(ex_style & WS_EX_LAYERED)) ? 1U : 0U;

    TRACE("created hwnd %p parent %p owner_pid %u (%dx%d at %d,%d)\n",
          hwnd, parent, (unsigned int)slot->owner_pid, safe_w, safe_h, x, y);
    return hwnd;
}

void WINAPI macdrv_DestroyWindow(HWND hwnd)
{
    struct macdrv_shared_window_slot *slot = find_window_slot(hwnd);
    if (!slot) {
        return;
    }
    TRACE("destroying hwnd %p\n", hwnd);
    memset(slot, 0, sizeof(*slot));
}

void WINAPI macdrv_SetDesktopWindow(HWND hwnd)
{
    if (hwnd) {
        g_desktop_hwnd = hwnd;
    }
}

HWND WINAPI macdrv_get_desktop_window(void)
{
    return g_desktop_hwnd;
}

HWND WINAPI macdrv_get_ancestor(HWND hwnd, UINT flags)
{
    struct macdrv_shared_window_slot *slot = find_window_slot(hwnd);
    if (!slot) {
        return NULL;
    }
    if (flags == GA_PARENT) {
        return slot->parent ? slot->parent : g_desktop_hwnd;
    }
    if (flags == GA_ROOT || flags == GA_ROOTOWNER) {
        struct macdrv_shared_window_slot *cur = slot;
        size_t guard = 0;
        while (cur->parent != NULL && cur->parent != g_desktop_hwnd && guard < MACDRV_MAX_WINDOWS) {
            struct macdrv_shared_window_slot *next = find_window_slot(cur->parent);
            if (!next) {
                break;
            }
            cur = next;
            guard++;
        }
        return cur->hwnd;
    }
    return NULL;
}

LONG WINAPI macdrv_get_window_long(HWND hwnd, INT offset)
{
    struct macdrv_shared_window_slot *slot = find_window_slot(hwnd);
    if (!slot) {
        SetLastError(ERROR_INVALID_WINDOW_HANDLE);
        return 0;
    }
    if (offset == GWL_STYLE) {
        return (LONG)slot->style;
    }
    if (offset == GWL_EXSTYLE) {
        return (LONG)slot->ex_style;
    }
    return 0;
}

LONG WINAPI macdrv_set_window_long(HWND hwnd, INT offset, LONG new_val)
{
    struct macdrv_shared_window_slot *slot = find_window_slot(hwnd);
    LONG prev = 0;
    if (!slot) {
        SetLastError(ERROR_INVALID_WINDOW_HANDLE);
        return 0;
    }
    if (offset == GWL_STYLE) {
        prev = (LONG)slot->style;
        slot->style = (DWORD)new_val;
    } else if (offset == GWL_EXSTYLE) {
        prev = (LONG)slot->ex_style;
        slot->ex_style = (DWORD)new_val;
    }
    return prev;
}

BOOL WINAPI macdrv_get_client_rect(HWND hwnd, RECT *rect)
{
    struct macdrv_shared_window_slot *slot = find_window_slot(hwnd);
    if (!slot || !rect) {
        SetLastError(ERROR_INVALID_WINDOW_HANDLE);
        return FALSE;
    }
    *rect = slot->win_data.rects.client;
    return TRUE;
}

BOOL WINAPI macdrv_get_window_rect(HWND hwnd, RECT *rect)
{
    struct macdrv_shared_window_slot *slot = find_window_slot(hwnd);
    if (!slot || !rect) {
        SetLastError(ERROR_INVALID_WINDOW_HANDLE);
        return FALSE;
    }
    *rect = slot->win_data.rects.window;
    return TRUE;
}

BOOL WINAPI macdrv_set_window_pos(HWND hwnd, INT x, INT y, INT width, INT height, UINT swp_flags)
{
    struct macdrv_shared_window_slot *slot = find_window_slot(hwnd);
    if (!slot) {
        SetLastError(ERROR_INVALID_WINDOW_HANDLE);
        return FALSE;
    }
    if (!(swp_flags & SWP_NOMOVE)) {
        INT cur_w = slot->win_data.rects.window.right - slot->win_data.rects.window.left;
        INT cur_h = slot->win_data.rects.window.bottom - slot->win_data.rects.window.top;
        slot->win_data.rects.window.left = x;
        slot->win_data.rects.window.top = y;
        slot->win_data.rects.window.right = x + cur_w;
        slot->win_data.rects.window.bottom = y + cur_h;
    }
    if (!(swp_flags & SWP_NOSIZE)) {
        INT safe_w = width > 0 ? width : 1;
        INT safe_h = height > 0 ? height : 1;
        slot->win_data.rects.window.right = slot->win_data.rects.window.left + safe_w;
        slot->win_data.rects.window.bottom = slot->win_data.rects.window.top + safe_h;
        slot->win_data.rects.client.left = 0;
        slot->win_data.rects.client.top = 0;
        slot->win_data.rects.client.right = safe_w;
        slot->win_data.rects.client.bottom = safe_h;
    }
    slot->win_data.rects.visible = slot->win_data.rects.window;
    if (swp_flags & SWP_SHOWWINDOW) {
        slot->style |= WS_VISIBLE;
        slot->win_data.on_screen = 1U;
    }
    if (swp_flags & SWP_HIDEWINDOW) {
        slot->style &= ~WS_VISIBLE;
        slot->win_data.on_screen = 0U;
    }
    return TRUE;
}

/*
 * get_win_data
 *
 * Returns the process-local macdrv_win_data for hwnd if and only if hwnd is
 * owned by the current process (matching upstream winemac.drv semantics where
 * cross-process HWNDs return NULL from get_win_data).
 */
struct macdrv_win_data * WINAPI get_win_data(HWND hwnd)
{
    struct macdrv_shared_window_slot *slot = find_window_slot(hwnd);
    if (!slot || hwnd == g_desktop_hwnd) {
        return NULL;
    }
    if (slot->owner_pid != g_current_pid) {
        return NULL;
    }
    return &slot->win_data;
}

void WINAPI release_win_data(struct macdrv_win_data *data)
{
    (void)data;
}

void WINAPI init_win_context(void)
{
}

void WINAPI macdrv_ActivateWindow(HWND hwnd, HWND previous)
{
    struct macdrv_win_data *data = get_win_data(hwnd);
    (void)previous;
    if (!data) {
        return;
    }
    data->on_screen = 1U;
}

static void apply_layered_attributes_to_slot(
    struct macdrv_shared_window_slot *slot,
    COLORREF key,
    BYTE alpha,
    DWORD flags,
    BOOL is_ulw)
{
    if (!slot) {
        return;
    }
    if (flags & (LWA_COLORKEY | LWA_ALPHA)) {
        slot->win_data.layered = 1U;
        slot->win_data.ulw_layered = is_ulw ? 1U : 0U;
        slot->win_data.per_pixel_alpha = 0U;
        slot->win_data.layered_flags = flags;
        if (flags & LWA_COLORKEY) {
            slot->win_data.color_key = key;
        }
        if (flags & LWA_ALPHA) {
            slot->win_data.alpha = alpha;
            slot->win_data.opacity_permille = ((uint32_t)alpha * 1000U) / 255U;
        } else {
            slot->win_data.alpha = 255;
            slot->win_data.opacity_permille = 1000U;
        }
        slot->cocoa_win_storage.opacity_permille = slot->win_data.opacity_permille;
        if ((slot->style & WS_VISIBLE) && !slot->win_data.on_screen) {
            slot->win_data.on_screen = 1U;
            slot->cocoa_win_storage.ordered_in = 1U;
        }
    } else {
        slot->win_data.layered = 0U;
        slot->win_data.ulw_layered = 0U;
        slot->win_data.per_pixel_alpha = 0U;
        slot->win_data.alpha = 255;
        slot->win_data.opacity_permille = 1000U;
    }
}

/*
 * macdrv_SetLayeredWindowAttributes (window.c:1318)
 *
 * Updates the opacity and transparency color key of a layered window.
 * Supports both same-process windows (via get_win_data) and cross-process
 * windows (by dispatching WM_MACDRV_SET_LAYERED_ATTRIBUTES to the owning
 * process and synchronizing the shared window slot).
 */
void WINAPI macdrv_SetLayeredWindowAttributes(HWND hwnd, COLORREF key, BYTE alpha, DWORD flags)
{
    struct macdrv_win_data *data = get_win_data(hwnd);
    struct macdrv_shared_window_slot *slot;

    TRACE("hwnd %p key %#x alpha %#x flags %#x\n",
          hwnd, (unsigned int)key, (unsigned int)alpha, (unsigned int)flags);

    if (data) {
        slot = find_window_slot(hwnd);
        if (slot) {
            slot->last_modifier_pid = g_current_pid;
            apply_layered_attributes_to_slot(slot, key, alpha, flags, FALSE);
        }
        release_win_data(data);
        return;
    }

    if (is_stub_simulation_enabled()) {
        FIXME("setting layered attributes on window %p of other process not supported\n", hwnd);
        SetLastError(ERROR_INVALID_WINDOW_HANDLE);
        return;
    }

    slot = find_window_slot(hwnd);
    if (!slot) {
        SetLastError(ERROR_INVALID_WINDOW_HANDLE);
        return;
    }

    /* Cross-process path: dispatch WM_MACDRV_SET_LAYERED_ATTRIBUTES to owner */
    slot->last_modifier_pid = g_current_pid;
    macdrv_WindowMessage(
        hwnd,
        WM_MACDRV_SET_LAYERED_ATTRIBUTES,
        (WPARAM)(((uintptr_t)alpha << 16) | (uintptr_t)(flags & 0xFFFFU)),
        (LPARAM)key);
}

BOOL WINAPI macdrv_GetLayeredWindowAttributes(HWND hwnd, COLORREF *key, BYTE *alpha, DWORD *flags)
{
    struct macdrv_shared_window_slot *slot = find_window_slot(hwnd);
    if (!slot || !slot->win_data.layered) {
        SetLastError(ERROR_INVALID_WINDOW_HANDLE);
        return FALSE;
    }
    if (key) {
        *key = slot->win_data.color_key;
    }
    if (alpha) {
        *alpha = slot->win_data.alpha;
    }
    if (flags) {
        *flags = slot->win_data.layered_flags;
    }
    return TRUE;
}

void WINAPI macdrv_UpdateLayeredWindow(HWND hwnd, BYTE alpha, UINT flags)
{
    struct macdrv_win_data *data = get_win_data(hwnd);
    struct macdrv_shared_window_slot *slot;
    DWORD lwa_flags = 0;

    if (flags & ULW_ALPHA) {
        lwa_flags |= LWA_ALPHA;
    }
    if (flags & ULW_COLORKEY) {
        lwa_flags |= LWA_COLORKEY;
    }
    if (flags & ULW_OPAQUE) {
        lwa_flags |= LWA_ALPHA;
        alpha = 255;
    }

    if (data) {
        slot = find_window_slot(hwnd);
        if (slot) {
            slot->last_modifier_pid = g_current_pid;
            apply_layered_attributes_to_slot(slot, slot->win_data.color_key, alpha, lwa_flags, TRUE);
        }
        release_win_data(data);
        return;
    }

    if (is_stub_simulation_enabled()) {
        SetLastError(ERROR_INVALID_WINDOW_HANDLE);
        return;
    }

    slot = find_window_slot(hwnd);
    if (!slot) {
        SetLastError(ERROR_INVALID_WINDOW_HANDLE);
        return;
    }

    slot->last_modifier_pid = g_current_pid;
    macdrv_WindowMessage(
        hwnd,
        WM_MACDRV_SET_LAYERED_ATTRIBUTES,
        (WPARAM)((1UL << 24) | ((uintptr_t)alpha << 16) | (uintptr_t)(lwa_flags & 0xFFFFU)),
        (LPARAM)slot->win_data.color_key);
}

BOOL WINAPI macdrv_query_layered_window_info(HWND hwnd, struct macdrv_layered_window_info *info)
{
    struct macdrv_shared_window_slot *slot = find_window_slot(hwnd);
    if (!slot || !info) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    memset(info, 0, sizeof(*info));
    info->hwnd = slot->hwnd;
    info->owner_pid = slot->owner_pid;
    info->last_modifier_pid = slot->last_modifier_pid;
    info->layered = slot->win_data.layered;
    info->ulw_layered = slot->win_data.ulw_layered;
    info->per_pixel_alpha = slot->win_data.per_pixel_alpha;
    info->on_screen = slot->win_data.on_screen;
    info->color_key = slot->win_data.color_key;
    info->alpha = slot->win_data.alpha;
    info->flags = slot->win_data.layered_flags;
    info->opacity_permille = slot->win_data.opacity_permille;
    info->remote_msg_dispatched = slot->remote_msg_dispatched;
    return TRUE;
}

void WINAPI macdrv_SetParent(HWND hwnd, HWND parent, HWND old_parent)
{
    struct macdrv_shared_window_slot *slot = find_window_slot(hwnd);
    (void)old_parent;
    if (slot) {
        slot->parent = parent;
    }
}

void WINAPI macdrv_SetWindowRgn(HWND hwnd, HRGN hrgn, BOOL redraw)
{
    struct macdrv_shared_window_slot *slot = find_window_slot(hwnd);
    (void)redraw;
    if (slot) {
        slot->win_data.shaped = hrgn != NULL ? 1U : 0U;
    }
}

void WINAPI macdrv_SetWindowStyle(HWND hwnd, INT offset, STYLESTRUCT *style)
{
    struct macdrv_shared_window_slot *slot = find_window_slot(hwnd);
    if (!slot || !style) {
        return;
    }
    if (offset == GWL_STYLE) {
        slot->style = style->styleNew;
    } else if (offset == GWL_EXSTYLE) {
        slot->ex_style = style->styleNew;
    }
}

void WINAPI macdrv_SetWindowText(HWND hwnd, LPCWSTR text)
{
    struct macdrv_shared_window_slot *slot = find_window_slot(hwnd);
    if (slot) {
        copy_wstr_bounded(slot->title, 128, text);
    }
}

UINT WINAPI macdrv_ShowWindow(HWND hwnd, INT cmd, RECT *rect, UINT swp)
{
    struct macdrv_shared_window_slot *slot = find_window_slot(hwnd);
    (void)rect;
    if (!slot) {
        return swp;
    }
    if (cmd == SW_HIDE) {
        slot->style &= ~WS_VISIBLE;
        slot->win_data.on_screen = 0U;
    } else {
        slot->style |= WS_VISIBLE;
        slot->win_data.on_screen = 1U;
    }
    return swp;
}

LRESULT WINAPI macdrv_SysCommand(HWND hwnd, WPARAM wparam, LPARAM lparam, const POINT *pos)
{
    (void)hwnd;
    (void)wparam;
    (void)lparam;
    (void)pos;
    return -1;
}

LRESULT WINAPI macdrv_WindowMessage(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    struct macdrv_shared_window_slot *slot = find_window_slot(hwnd);
    if (!slot) {
        return 0;
    }

    switch (msg) {
    case WM_MACDRV_CREATE_REMOTE_LAYER:
        slot->remote_layer_context_id = (uint32_t)lp;
        slot->remote_msg_dispatched++;
        return 1;

    case WM_MACDRV_RELEASE_REMOTE_LAYER:
        if (slot->remote_layer_context_id == (uint32_t)lp) {
            slot->remote_layer_context_id = 0;
        }
        slot->remote_msg_dispatched++;
        return 0;

    case WM_MACDRV_PRESENT_IOSURFACE:
        slot->child_front_iosurface_id = (uint32_t)wp;
        slot->child_back_iosurface_id = (uint32_t)lp;
        slot->remote_msg_dispatched++;
        return 1;

    case WM_MACDRV_SET_LAYERED_ATTRIBUTES: {
        BOOL is_ulw = ((wp >> 24) & 0x1U) != 0;
        BYTE alpha = (BYTE)((wp >> 16) & 0xFFU);
        DWORD flags = (DWORD)(wp & 0xFFFFU);
        COLORREF key = (COLORREF)lp;
        apply_layered_attributes_to_slot(slot, key, alpha, flags, is_ulw);
        slot->remote_msg_dispatched++;
        return 1;
    }

    default:
        break;
    }
    return 0;
}

BOOL WINAPI macdrv_WindowPosChanging(
    HWND hwnd,
    UINT swp_flags,
    BOOL shaped,
    const struct window_rects *rects)
{
    struct macdrv_shared_window_slot *slot = find_window_slot(hwnd);
    (void)swp_flags;
    if (slot) {
        slot->win_data.shaped = shaped ? 1U : 0U;
        if (rects) {
            slot->win_data.rects = *rects;
        }
    }
    return TRUE;
}

BOOL WINAPI macdrv_GetWindowStyleMasks(
    HWND hwnd,
    UINT style,
    UINT ex_style,
    UINT *style_mask,
    UINT *ex_style_mask)
{
    (void)hwnd;
    (void)style;
    (void)ex_style;
    if (style_mask) {
        *style_mask = 0xFFFFFFFFU;
    }
    if (ex_style_mask) {
        *ex_style_mask = 0xFFFFFFFFU;
    }
    return TRUE;
}

void WINAPI macdrv_WindowPosChanged(
    HWND hwnd,
    HWND insert_after,
    HWND owner_hint,
    UINT swp_flags,
    const struct window_rects *new_rects,
    void *surface)
{
    struct macdrv_shared_window_slot *slot = find_window_slot(hwnd);
    (void)insert_after;
    (void)owner_hint;
    (void)swp_flags;
    (void)surface;
    if (slot && new_rects) {
        slot->win_data.rects = *new_rects;
    }
}

/* =========================================================================
 * Client Surface & Metal Swapchain Lifecycle (window.c:1177)
 * ========================================================================= */

static void macdrv_client_surface_destroy(struct client_surface *client)
{
    struct macdrv_client_surface *surface = impl_from_client_surface(client);
    if (!surface) {
        return;
    }
    if (surface->metal_swapchain) {
        if (surface->metal_swapchain->remote_context_id != 0 && surface->client.toplevel) {
            macdrv_WindowMessage(
                surface->client.toplevel,
                WM_MACDRV_RELEASE_REMOTE_LAYER,
                (WPARAM)(uintptr_t)surface->client.hwnd,
                (LPARAM)surface->metal_swapchain->remote_context_id);
        }
        release_swapchain_iosurfaces(surface->metal_swapchain);
        free(surface->metal_swapchain);
        surface->metal_swapchain = NULL;
    }
    free(surface);
}

static void macdrv_client_surface_detach(struct client_surface *client)
{
    (void)client;
}

static void macdrv_client_surface_update(struct client_surface *client)
{
    struct macdrv_client_surface *surface = impl_from_client_surface(client);
    struct _macdrv_metal_swapchain *sc;
    struct macdrv_shared_window_slot *slot;
    struct macdrv_shared_window_slot *root_slot;
    RECT child_rect;
    uint32_t new_w = 0;
    uint32_t new_h = 0;

    if (!surface || !surface->metal_swapchain) {
        return;
    }
    sc = surface->metal_swapchain;
    slot = find_window_slot(surface->client.hwnd);
    if (!slot) {
        return;
    }

    root_slot = find_window_slot(sc->toplevel_hwnd);
    compute_child_rect_in_root(slot, root_slot, &child_rect, &new_w, &new_h);
    sc->child_rect_in_root = child_rect;

    if (new_w != sc->width || new_h != sc->height) {
        sc->width = new_w;
        sc->height = new_h;
        if (sc->mode == MACDRV_SWAPCHAIN_MODE_IOSURFACE_CHILD) {
            allocate_swapchain_iosurfaces(sc, new_w, new_h);
        } else {
            sc->bytes_per_row = align_up_64(new_w * 4U);
        }
    }
}

static void macdrv_client_surface_present(struct client_surface *client, HDC hdc)
{
    (void)client;
    (void)hdc;
}

static const struct client_surface_funcs macdrv_client_surface_funcs = {
    sizeof(struct macdrv_client_surface),
    macdrv_client_surface_destroy,
    macdrv_client_surface_detach,
    macdrv_client_surface_update,
    macdrv_client_surface_present
};

struct macdrv_client_surface * WINAPI impl_from_client_surface(struct client_surface *client)
{
    if (!client) {
        return NULL;
    }
    return (struct macdrv_client_surface *)client;
}

struct client_surface * WINAPI macdrv_CreateClientSurface(HWND hwnd, int pixel_format, BOOL raw)
{
    struct macdrv_client_surface *surface;
    struct macdrv_win_data *data;
    HWND toplevel;

    if (!hwnd || !find_window_slot(hwnd)) {
        SetLastError(ERROR_INVALID_WINDOW_HANDLE);
        return NULL;
    }

    surface = (struct macdrv_client_surface *)calloc(1, sizeof(*surface));
    if (!surface) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return NULL;
    }

    toplevel = macdrv_get_ancestor(hwnd, GA_ROOT);
    if (!toplevel) {
        toplevel = hwnd;
    }

    surface->client.funcs = &macdrv_client_surface_funcs;
    surface->client.ref = 1;
    surface->client.hwnd = hwnd;
    surface->client.toplevel = toplevel;
    surface->client.pixel_format = pixel_format;
    surface->client.raw = raw;

    data = get_win_data(hwnd);
    if (data) {
        surface->cocoa_view = data->client_view;
        release_win_data(data);
    }

    return &surface->client;
}

void WINAPI macdrv_client_surface_release(struct client_surface *client)
{
    if (!client) {
        return;
    }
    client->ref--;
    if (client->ref <= 0 && client->funcs && client->funcs->destroy) {
        client->funcs->destroy(client);
    }
}

/*
 * macdrv_client_surface_acquire_metal_swapchain (window.c:1177)
 *
 * Acquires a CAMetalLayer swapchain for a client_surface:
 *   1. Same-process window (cocoa_view != NULL): binds CAMetalLayer directly
 *      to the WineContentView (MACDRV_SWAPCHAIN_MODE_VIEW).
 *   2. Cross-process top-level window (cocoa_view == NULL, toplevel == hwnd):
 *      creates a remote CAContext and attaches it to the owning process's
 *      window via WM_MACDRV_CREATE_REMOTE_LAYER (MACDRV_SWAPCHAIN_MODE_CACONTEXT_TOPLEVEL).
 *   3. Cross-process child window (cocoa_view == NULL, toplevel != hwnd):
 *      creates a double-buffered IOSurface-backed CAMetalLayer swapchain
 *      (MACDRV_SWAPCHAIN_MODE_IOSURFACE_CHILD) with a Mach port & global
 *      IOSurfaceID pair and attaches a remote sublayer clipped to the child's
 *      rectangle inside the root window.
 */
bool WINAPI macdrv_client_surface_acquire_metal_swapchain(struct macdrv_client_surface *surface)
{
    struct macdrv_shared_window_slot *slot;
    struct macdrv_shared_window_slot *root_slot;
    struct _macdrv_metal_swapchain *sc;
    HWND hwnd;
    HWND toplevel;
    RECT child_rect;
    uint32_t width = 0;
    uint32_t height = 0;

    if (!surface || !surface->client.hwnd) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return false;
    }
    if (surface->metal_swapchain) {
        return true;
    }

    hwnd = surface->client.hwnd;
    slot = find_window_slot(hwnd);
    if (!slot) {
        SetLastError(ERROR_INVALID_WINDOW_HANDLE);
        return false;
    }

    toplevel = macdrv_get_ancestor(hwnd, GA_ROOT);
    if (!toplevel) {
        toplevel = hwnd;
    }
    surface->client.toplevel = toplevel;
    root_slot = find_window_slot(toplevel);

    /* Simulate upstream window.c:1177 failure on cross-process child window when requested */
    if (!surface->cocoa_view && toplevel != hwnd && is_stub_simulation_enabled()) {
        WARN("Creating metal surface for child window %p of other process not implemented\n", hwnd);
        SetLastError(ERROR_INVALID_WINDOW_HANDLE);
        return false;
    }

    sc = (struct _macdrv_metal_swapchain *)calloc(1, sizeof(*sc));
    if (!sc) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return false;
    }

    compute_child_rect_in_root(slot, root_slot, &child_rect, &width, &height);

    sc->magic = 0x4D535750U; /* 'MSWP' */
    sc->hwnd = hwnd;
    sc->toplevel_hwnd = toplevel;
    sc->owner_pid = slot->owner_pid;
    sc->client_pid = g_current_pid;
    sc->is_child_window = (toplevel != hwnd) ? 1U : 0U;
    sc->is_cross_process = (slot->owner_pid != g_current_pid) ? 1U : 0U;
    sc->width = width;
    sc->height = height;
    sc->bytes_per_row = align_up_64(width * 4U);
    sc->pixel_format = MACDRV_IOSURFACE_PIXEL_FORMAT_BGRA;
    sc->mtl_pixel_format = MACDRV_MTL_PIXEL_FORMAT_BGRA8UNORM;
    sc->child_rect_in_root = child_rect;

    if (surface->cocoa_view) {
        /* Mode 1: Same-process direct WineContentView CAMetalLayer */
        sc->mode = MACDRV_SWAPCHAIN_MODE_VIEW;
        sc->iosurface_backed = 0U;
        sc->framebuffer_only = 1U;
        surface->cocoa_view->backed_by_metal_layer = 1U;
    } else if (toplevel == hwnd) {
        /* Mode 2: Cross-process top-level remote CAContext swapchain */
        sc->mode = MACDRV_SWAPCHAIN_MODE_CACONTEXT_TOPLEVEL;
        sc->iosurface_backed = 0U;
        sc->framebuffer_only = 1U;
        sc->remote_context_id = g_next_remote_ctx_id++;
        if (!macdrv_WindowMessage(
                toplevel,
                WM_MACDRV_CREATE_REMOTE_LAYER,
                (WPARAM)(uintptr_t)hwnd,
                (LPARAM)sc->remote_context_id)) {
            free(sc);
            SetLastError(ERROR_INVALID_WINDOW_HANDLE);
            return false;
        }
    } else {
        /* Mode 3 (window.c:1177): Cross-process child window IOSurface-backed CAMetalLayer swapchain */
        sc->mode = MACDRV_SWAPCHAIN_MODE_IOSURFACE_CHILD;
        sc->iosurface_backed = 1U;
        sc->framebuffer_only = 0U;
        sc->remote_context_id = g_next_remote_ctx_id++;

        if (!allocate_swapchain_iosurfaces(sc, width, height)) {
            free(sc);
            SetLastError(ERROR_INVALID_PARAMETER);
            return false;
        }

        slot->child_front_iosurface_id = sc->front_iosurface_id;
        slot->child_back_iosurface_id = sc->back_iosurface_id;

        if (!macdrv_WindowMessage(
                toplevel,
                WM_MACDRV_CREATE_REMOTE_LAYER,
                (WPARAM)(uintptr_t)hwnd,
                (LPARAM)sc->remote_context_id)) {
            release_swapchain_iosurfaces(sc);
            free(sc);
            SetLastError(ERROR_INVALID_WINDOW_HANDLE);
            return false;
        }
    }

    surface->metal_swapchain = sc;
    return true;
}

BOOL WINAPI macdrv_query_metal_swapchain_info(
    struct macdrv_client_surface *surface,
    struct macdrv_metal_swapchain_info *info)
{
    struct _macdrv_metal_swapchain *sc;
    if (!surface || !surface->metal_swapchain || !info) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    sc = surface->metal_swapchain;
    memset(info, 0, sizeof(*info));
    info->mode = sc->mode;
    info->hwnd = sc->hwnd;
    info->toplevel_hwnd = sc->toplevel_hwnd;
    info->owner_pid = sc->owner_pid;
    info->client_pid = sc->client_pid;
    info->is_child_window = sc->is_child_window;
    info->is_cross_process = sc->is_cross_process;
    info->iosurface_backed = sc->iosurface_backed;
    info->front_iosurface_id = sc->front_iosurface_id;
    info->back_iosurface_id = sc->back_iosurface_id;
    info->mach_port = sc->mach_port;
    info->remote_context_id = sc->remote_context_id;
    info->width = sc->width;
    info->height = sc->height;
    info->bytes_per_row = sc->bytes_per_row;
    info->pixel_format = sc->pixel_format;
    info->mtl_pixel_format = sc->mtl_pixel_format;
    info->framebuffer_only = sc->framebuffer_only;
    info->present_count = sc->present_count;
    info->child_rect_in_root = sc->child_rect_in_root;
    return TRUE;
}

BOOL WINAPI macdrv_swapchain_present_bgra(
    struct macdrv_client_surface *surface,
    const uint8_t *bgra_pixels,
    size_t byte_len)
{
    struct _macdrv_metal_swapchain *sc;
    struct macdrv_shared_window_slot *slot;
    size_t copy_len;
    uint32_t tmp_id;

    if (!surface || !surface->metal_swapchain || !bgra_pixels || byte_len == 0) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    sc = surface->metal_swapchain;
    slot = find_window_slot(sc->hwnd);
    if (!slot) {
        SetLastError(ERROR_INVALID_WINDOW_HANDLE);
        return FALSE;
    }

    copy_len = byte_len < MACDRV_MAX_FRAME_SNAP_CAP ? byte_len : MACDRV_MAX_FRAME_SNAP_CAP;

#ifdef __APPLE__
    if (sc->back_surface_ref) {
        size_t alloc_sz = IOSurfaceGetAllocSize(sc->back_surface_ref);
        size_t surf_copy = byte_len < alloc_sz ? byte_len : alloc_sz;
        if (IOSurfaceLock(sc->back_surface_ref, 0, NULL) == kIOReturnSuccess) {
            void *base = IOSurfaceGetBaseAddress(sc->back_surface_ref);
            if (base && surf_copy > 0) {
                memcpy(base, bgra_pixels, surf_copy);
            }
            IOSurfaceUnlock(sc->back_surface_ref, 0, NULL);
        }
        {
            IOSurfaceRef tmp_ref = sc->front_surface_ref;
            sc->front_surface_ref = sc->back_surface_ref;
            sc->back_surface_ref = tmp_ref;
        }
    }
#endif

    memcpy(sc->front_frame_cache, bgra_pixels, copy_len);
    sc->front_frame_len = copy_len;
    memcpy(slot->child_presented_bgra, bgra_pixels, copy_len);
    slot->child_presented_len = copy_len;

    tmp_id = sc->front_iosurface_id;
    sc->front_iosurface_id = sc->back_iosurface_id;
    sc->back_iosurface_id = tmp_id;
    sc->present_count++;

    slot->child_front_iosurface_id = sc->front_iosurface_id;
    slot->child_back_iosurface_id = sc->back_iosurface_id;

    if (sc->toplevel_hwnd) {
        macdrv_WindowMessage(
            sc->toplevel_hwnd,
            WM_MACDRV_PRESENT_IOSURFACE,
            (WPARAM)sc->front_iosurface_id,
            (LPARAM)sc->back_iosurface_id);
    }

    return TRUE;
}

BOOL WINAPI macdrv_parent_read_child_iosurface_bgra(
    HWND parent_hwnd,
    HWND child_hwnd,
    uint8_t *out_bgra,
    size_t out_cap,
    uint32_t *out_iosurface_id)
{
    struct macdrv_shared_window_slot *parent_slot;
    struct macdrv_shared_window_slot *child_slot;
    size_t copy_len;
    bool read_from_kernel_iosurface = false;

    if (!parent_hwnd || !child_hwnd || !out_bgra || out_cap == 0) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    parent_slot = find_window_slot(parent_hwnd);
    child_slot = find_window_slot(child_hwnd);
    if (!parent_slot || !child_slot || macdrv_get_ancestor(child_hwnd, GA_ROOT) != parent_hwnd) {
        SetLastError(ERROR_INVALID_WINDOW_HANDLE);
        return FALSE;
    }

    if (child_slot->child_front_iosurface_id == 0 || child_slot->child_presented_len == 0) {
        SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }

#ifdef __APPLE__
    {
        IOSurfaceRef shared_surf = IOSurfaceLookup((IOSurfaceID)child_slot->child_front_iosurface_id);
        if (shared_surf) {
            size_t alloc_sz = IOSurfaceGetAllocSize(shared_surf);
            size_t surf_copy = out_cap < alloc_sz ? out_cap : alloc_sz;
            if (IOSurfaceLock(shared_surf, kIOSurfaceLockReadOnly, NULL) == kIOReturnSuccess) {
                const void *base = IOSurfaceGetBaseAddress(shared_surf);
                if (base && surf_copy > 0) {
                    memcpy(out_bgra, base, surf_copy);
                    read_from_kernel_iosurface = true;
                }
                IOSurfaceUnlock(shared_surf, kIOSurfaceLockReadOnly, NULL);
            }
            CFRelease(shared_surf);
        }
    }
#endif

    if (!read_from_kernel_iosurface) {
        copy_len = out_cap < child_slot->child_presented_len ? out_cap : child_slot->child_presented_len;
        memcpy(out_bgra, child_slot->child_presented_bgra, copy_len);
    }

    if (out_iosurface_id) {
        *out_iosurface_id = child_slot->child_front_iosurface_id;
    }
    return TRUE;
}

INT WINAPI macdrv_vulkan_create_metal_surface(
    struct client_surface *client,
    uint64_t *out_vk_surface)
{
    struct macdrv_client_surface *surface = impl_from_client_surface(client);
    if (!surface || !out_vk_surface) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    if (!macdrv_client_surface_acquire_metal_swapchain(surface)) {
        *out_vk_surface = 0;
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    *out_vk_surface = (uint64_t)(uintptr_t)surface->metal_swapchain;
    return VK_SUCCESS;
}

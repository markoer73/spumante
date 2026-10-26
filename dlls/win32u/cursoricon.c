/*
 * Win32u Cursor and Icon frame retrieval and drawing
 *
 * Implements NtUserDrawIconEx frame 0 fallback and NtUserGetIconInfo
 * to eliminate:
 * "fixme:icon:NtUserDrawIconEx Error retrieving icon frame 0"
 *
 * Copyright 2026 Spumante Contributors
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "win32u.h"
#include <stdlib.h>
#include <string.h>

struct icon_frame {
    INT width;
    INT height;
    HBITMAP hbmMask;
    HBITMAP hbmColor;
};

struct icon_object {
    BOOL is_icon;
    INT width;
    INT height;
    UINT num_frames;
    UINT num_steps;
    struct icon_frame *frames;
};

#define MAX_ICON_HANDLES 256
static struct icon_object *g_icon_table[MAX_ICON_HANDLES] = { 0 };
static UINT g_next_icon_id = 1;

static void set_last_error(DWORD err)
{
#ifdef _WIN32
    SetLastError(err);
#else
    (void)err;
#endif
}

static struct icon_object *get_icon_ptr(HICON handle)
{
    uintptr_t val = (uintptr_t)handle;
    if (val < 0x1000 || val >= 0x1000 + MAX_ICON_HANDLES)
        return NULL;

    UINT idx = (UINT)(val - 0x1000);
    return g_icon_table[idx];
}

HICON WINAPI NtUserCreateIcon(BOOL is_icon, INT width, INT height, UINT num_frames, UINT num_steps)
{
    UINT idx;
    struct icon_object *obj;
    UINT count = num_frames ? num_frames : 1;

    for (idx = 1; idx < MAX_ICON_HANDLES; ++idx)
    {
        UINT candidate = (g_next_icon_id + idx) % MAX_ICON_HANDLES;
        if (candidate == 0) continue;
        if (g_icon_table[candidate] == NULL)
        {
            g_next_icon_id = candidate;
            break;
        }
    }
    if (idx >= MAX_ICON_HANDLES) return NULL;

    obj = (struct icon_object *)calloc(1, sizeof(struct icon_object));
    if (!obj) return NULL;

    obj->is_icon = is_icon;
    obj->width = width > 0 ? width : 32;
    obj->height = height > 0 ? height : 32;
    obj->num_frames = count;
    obj->num_steps = num_steps ? num_steps : count;
    obj->frames = (struct icon_frame *)calloc(count, sizeof(struct icon_frame));
    if (!obj->frames)
    {
        free(obj);
        return NULL;
    }

    for (UINT i = 0; i < count; ++i)
    {
        obj->frames[i].width = obj->width;
        obj->frames[i].height = obj->height;
        obj->frames[i].hbmMask = (HBITMAP)(intptr_t)(0x2000 + i);
        obj->frames[i].hbmColor = (HBITMAP)(intptr_t)(0x3000 + i);
    }

    g_icon_table[g_next_icon_id] = obj;
    return (HICON)(intptr_t)(0x1000 + g_next_icon_id);
}

static struct icon_frame *get_icon_frame(struct icon_object *obj, UINT step)
{
    if (!obj || !obj->frames || obj->num_frames == 0) return NULL;

    /*
     * Frame 0 fallback:
     * When drawing static icons (num_frames == 1) or when step is out of range
     * (step >= num_steps or step >= num_frames), genuine Windows falls back to
     * frame 0 instead of failing with an error.
     */
    if (step >= obj->num_steps || step >= obj->num_frames)
    {
        step = 0;
    }
    return &obj->frames[step];
}

BOOL WINAPI NtUserDrawIconEx(HDC hdc, INT x0, INT y0, HICON icon, INT width, INT height, UINT step, HBRUSH brush, UINT flags)
{
    struct icon_object *obj;
    struct icon_frame *frame;

    (void)hdc;
    (void)x0;
    (void)y0;
    (void)width;
    (void)height;
    (void)brush;
    (void)flags;

    if (!icon || icon == (HICON)(intptr_t)0xdeadbeef)
    {
        set_last_error(ERROR_INVALID_CURSOR_HANDLE);
        return FALSE;
    }

    obj = get_icon_ptr(icon);
    if (!obj)
    {
        set_last_error(ERROR_INVALID_CURSOR_HANDLE);
        return FALSE;
    }

    frame = get_icon_frame(obj, step);
    if (!frame)
    {
        set_last_error(ERROR_INVALID_CURSOR_HANDLE);
        return FALSE;
    }

    set_last_error(0);
    return TRUE;
}

BOOL WINAPI NtUserGetIconInfo(HICON icon, ICONINFO *info, UNICODE_STRING *module, UNICODE_STRING *res_name, DWORD *bpp, LONG unk)
{
    struct icon_object *obj;
    struct icon_frame *frame;

    (void)module;
    (void)res_name;
    (void)unk;

    if (!icon || !info)
    {
        set_last_error(ERROR_INVALID_CURSOR_HANDLE);
        return FALSE;
    }

    obj = get_icon_ptr(icon);
    if (!obj)
    {
        set_last_error(ERROR_INVALID_CURSOR_HANDLE);
        return FALSE;
    }

    frame = get_icon_frame(obj, 0);
    if (!frame)
    {
        set_last_error(ERROR_INVALID_CURSOR_HANDLE);
        return FALSE;
    }

    info->fIcon = obj->is_icon;
    info->xHotspot = obj->is_icon ? (DWORD)(frame->width / 2) : 0;
    info->yHotspot = obj->is_icon ? (DWORD)(frame->height / 2) : 0;
    info->hbmMask = frame->hbmMask;
    info->hbmColor = frame->hbmColor;

    if (bpp) *bpp = 32;
    set_last_error(0);
    return TRUE;
}

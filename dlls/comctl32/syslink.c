/*
 * SysLink control implementation (comctl32.dll & comctl32_v6.dll)
 *
 * Copyright 2004-2006 Thomas Weidenmueller <w3seek@reactos.com>
 * Copyright 2026 Spumante Contributors
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "comctl32.h"
#include <stdlib.h>
#include <string.h>

#ifndef WINE_DEFAULT_DEBUG_CHANNEL
#define WINE_DEFAULT_DEBUG_CHANNEL(ch)
#endif
#ifndef TRACE
#define TRACE(...) do { } while (0)
#endif
#ifndef WARN
#define WARN(...) do { } while (0)
#endif
#ifndef ERR
#define ERR(...) do { } while (0)
#endif

WINE_DEFAULT_DEBUG_CHANNEL(syslink);

#define MAX_FONT_SLOTS 64

struct internal_font_slot {
    BOOL     in_use;
    BOOL     is_stock;
    LOGFONTW lf;
};

static struct internal_font_slot s_font_slots[MAX_FONT_SLOTS];
static BOOL s_fonts_initialized = FALSE;
static UINT s_font_env_flags = COMCTL32_FONT_ENV_ALL_AVAILABLE;

/* Reserved slot indices:
 * 1: HDC current OBJ_FONT ("Tahoma", -11)
 * 2: Stock DEFAULT_GUI_FONT ("MS Shell Dlg", -11)
 * 3: Stock SYSTEM_FONT ("System", -16)
 */
static void set_wstr(WCHAR *dst, size_t max_chars, const char *ascii)
{
    size_t i = 0;
    if (!dst || max_chars == 0) return;
    while (ascii && ascii[i] != '\0' && i + 1 < max_chars) {
        dst[i] = (WCHAR)(unsigned char)ascii[i];
        i++;
    }
    dst[i] = 0;
}

static size_t wstr_len(const WCHAR *s)
{
    size_t n = 0;
    if (!s) return 0;
    while (s[n] != 0) n++;
    return n;
}

static void copy_wstr(WCHAR *dst, size_t max_chars, const WCHAR *src)
{
    size_t i = 0;
    if (!dst || max_chars == 0) return;
    if (!src) {
        dst[0] = 0;
        return;
    }
    while (src[i] != 0 && i + 1 < max_chars) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = 0;
}

static void init_font_slots(void)
{
    if (s_fonts_initialized) return;
    memset(s_font_slots, 0, sizeof(s_font_slots));

    /* Slot 1: HDC current OBJ_FONT */
    s_font_slots[1].in_use = TRUE;
    s_font_slots[1].is_stock = TRUE;
    s_font_slots[1].lf.lfHeight = -11;
    s_font_slots[1].lf.lfWeight = 400;
    s_font_slots[1].lf.lfCharSet = 1;
    set_wstr(s_font_slots[1].lf.lfFaceName, LF_FACESIZE, "Tahoma");

    /* Slot 2: DEFAULT_GUI_FONT */
    s_font_slots[2].in_use = TRUE;
    s_font_slots[2].is_stock = TRUE;
    s_font_slots[2].lf.lfHeight = -11;
    s_font_slots[2].lf.lfWeight = 400;
    s_font_slots[2].lf.lfCharSet = 1;
    set_wstr(s_font_slots[2].lf.lfFaceName, LF_FACESIZE, "MS Shell Dlg");

    /* Slot 3: SYSTEM_FONT */
    s_font_slots[3].in_use = TRUE;
    s_font_slots[3].is_stock = TRUE;
    s_font_slots[3].lf.lfHeight = -16;
    s_font_slots[3].lf.lfWeight = 700;
    s_font_slots[3].lf.lfCharSet = 1;
    set_wstr(s_font_slots[3].lf.lfFaceName, LF_FACESIZE, "System");

    s_fonts_initialized = TRUE;
}

static HFONT slot_to_hfont(size_t idx)
{
    return (HFONT)(uintptr_t)(0x10000U + idx);
}

static size_t hfont_to_slot(HFONT hFont)
{
    uintptr_t v = (uintptr_t)hFont;
    if (v <= 0x10000U || v >= 0x10000U + MAX_FONT_SLOTS) {
        return 0;
    }
    return (size_t)(v - 0x10000U);
}

void WINAPI Comctl32SetFontEnvFlags(UINT flags)
{
    init_font_slots();
    s_font_env_flags = flags;
}

HFONT WINAPI Comctl32CreateTestFont(const WCHAR *face_name, LONG height, BOOL underline)
{
    size_t i;
    init_font_slots();
    for (i = 4; i < MAX_FONT_SLOTS; i++) {
        if (!s_font_slots[i].in_use) {
            memset(&s_font_slots[i], 0, sizeof(s_font_slots[i]));
            s_font_slots[i].in_use = TRUE;
            s_font_slots[i].is_stock = FALSE;
            s_font_slots[i].lf.lfHeight = height != 0 ? height : -12;
            s_font_slots[i].lf.lfWeight = 400;
            s_font_slots[i].lf.lfUnderline = underline ? 1 : 0;
            s_font_slots[i].lf.lfCharSet = 1;
            if (face_name && face_name[0] != 0) {
                copy_wstr(s_font_slots[i].lf.lfFaceName, LF_FACESIZE, face_name);
            } else {
                set_wstr(s_font_slots[i].lf.lfFaceName, LF_FACESIZE, "Tahoma");
            }
            return slot_to_hfont(i);
        }
    }
    return NULL;
}

BOOL WINAPI Comctl32GetFontLogFont(HFONT hFont, LOGFONTW *out_lf)
{
    size_t idx;
    init_font_slots();
    if (!hFont || !out_lf) return FALSE;
    idx = hfont_to_slot(hFont);
    if (idx == 0 || !s_font_slots[idx].in_use) return FALSE;
    *out_lf = s_font_slots[idx].lf;
    return TRUE;
}

static void delete_font_slot(HFONT hFont)
{
    size_t idx = hfont_to_slot(hFont);
    if (idx >= 4 && idx < MAX_FONT_SLOTS && s_font_slots[idx].in_use && !s_font_slots[idx].is_stock) {
        s_font_slots[idx].in_use = FALSE;
    }
}

static HFONT create_font_indirect(const LOGFONTW *lf)
{
    size_t i;
    init_font_slots();
    if (!lf) return NULL;
    for (i = 4; i < MAX_FONT_SLOTS; i++) {
        if (!s_font_slots[i].in_use) {
            s_font_slots[i].in_use = TRUE;
            s_font_slots[i].is_stock = FALSE;
            s_font_slots[i].lf = *lf;
            return slot_to_hfont(i);
        }
    }
    return NULL;
}

static BOOL get_text_metrics(TEXTMETRICW *tm)
{
    if (!tm) return FALSE;
    if (s_font_env_flags & COMCTL32_FONT_ENV_NO_TEXTMETRICS) {
        return FALSE;
    }
    memset(tm, 0, sizeof(*tm));
    tm->tmHeight = 14;
    tm->tmAscent = 11;
    tm->tmDescent = 3;
    tm->tmAveCharWidth = 7;
    tm->tmMaxCharWidth = 14;
    tm->tmWeight = 400;
    tm->tmBreakChar = ' ';
    tm->tmDefaultChar = '?';
    return TRUE;
}

static HFONT get_current_dc_font(void)
{
    init_font_slots();
    if (s_font_env_flags & COMCTL32_FONT_ENV_NO_HDC_OBJ_FONT) {
        return NULL;
    }
    return slot_to_hfont(1);
}

static HFONT get_stock_font(int obj_id)
{
    init_font_slots();
    if (obj_id == DEFAULT_GUI_FONT) {
        if (s_font_env_flags & COMCTL32_FONT_ENV_NO_DEFAULT_GUI) return NULL;
        return slot_to_hfont(2);
    }
    if (obj_id == SYSTEM_FONT) {
        if (s_font_env_flags & COMCTL32_FONT_ENV_NO_SYSTEM_FONT) return NULL;
        return slot_to_hfont(3);
    }
    return NULL;
}

static void render_syslink_items(SYSLINK_INFO *infoPtr)
{
    UINT i;
    LONG y = 0;
    if (!infoPtr) return;
    for (i = 0; i < infoPtr->ItemCount; i++) {
        size_t len = wstr_len(infoPtr->Items[i].Text);
        infoPtr->Items[i].rcBounds.left = 0;
        infoPtr->Items[i].rcBounds.top = y;
        infoPtr->Items[i].rcBounds.right = (LONG)(len * 7U);
        infoPtr->Items[i].rcBounds.bottom = y + 16;
        y += 16;
    }
}

/***********************************************************************
 *           SYSLINK_SetFont
 * Set new Font for the SysLink control with HDC/stock/Tahoma fallback chain.
 */
HFONT WINAPI SYSLINK_SetFont(SYSLINK_INFO *infoPtr, HFONT hFont, BOOL bRedraw)
{
    LOGFONTW lf;
    TEXTMETRICW tm;
    HFONT hOldFont;
    HFONT hCandidate = NULL;
    BOOL have_lf = FALSE;

    (void)bRedraw;
    if (!infoPtr) return NULL;

    init_font_slots();
    hOldFont = infoPtr->Font;
    infoPtr->Font = hFont;
    infoPtr->FontFallbackSource = SYSLINK_FONT_SRC_NONE;

    if (infoPtr->LinkFont != NULL) {
        delete_font_slot(infoPtr->LinkFont);
        infoPtr->LinkFont = NULL;
    }

    memset(&lf, 0, sizeof(lf));
    memset(&tm, 0, sizeof(tm));

    /* 1. Try explicit hFont passed to WM_SETFONT / SYSLINK_SetFont */
    if (infoPtr->Font != NULL && Comctl32GetFontLogFont(infoPtr->Font, &lf)) {
        have_lf = TRUE;
        infoPtr->FontFallbackSource = SYSLINK_FONT_SRC_EXPLICIT_HFONT;
    }

    /* 2. Fall back to GetCurrentObject(hdc, OBJ_FONT) */
    if (!have_lf) {
        hCandidate = get_current_dc_font();
        if (hCandidate != NULL && Comctl32GetFontLogFont(hCandidate, &lf)) {
            have_lf = TRUE;
            infoPtr->FontFallbackSource = SYSLINK_FONT_SRC_HDC_CURRENT_OBJ;
        }
    }

    /* 3. Fall back to GetStockObject(DEFAULT_GUI_FONT) */
    if (!have_lf) {
        hCandidate = get_stock_font(DEFAULT_GUI_FONT);
        if (hCandidate != NULL && Comctl32GetFontLogFont(hCandidate, &lf)) {
            have_lf = TRUE;
            infoPtr->FontFallbackSource = SYSLINK_FONT_SRC_DEFAULT_GUI_FONT;
        }
    }

    /* 4. Fall back to GetStockObject(SYSTEM_FONT) */
    if (!have_lf) {
        hCandidate = get_stock_font(SYSTEM_FONT);
        if (hCandidate != NULL && Comctl32GetFontLogFont(hCandidate, &lf)) {
            have_lf = TRUE;
            infoPtr->FontFallbackSource = SYSLINK_FONT_SRC_SYSTEM_FONT;
        }
    }

    /* 5. Synthesize default Tahoma LOGFONTW so LinkFont creation never fails */
    if (!have_lf) {
        memset(&lf, 0, sizeof(lf));
        lf.lfHeight = -11;
        lf.lfWeight = 400;
        lf.lfCharSet = 1;
        set_wstr(lf.lfFaceName, LF_FACESIZE, "Tahoma");
        have_lf = TRUE;
        infoPtr->FontFallbackSource = SYSLINK_FONT_SRC_SYNTHESIZED;
    }

    lf.lfUnderline = TRUE;
    infoPtr->LinkFont = create_font_indirect(&lf);
    if (get_text_metrics(&tm) && tm.tmBreakChar != 0) {
        infoPtr->BreakChar = tm.tmBreakChar;
    } else {
        infoPtr->BreakChar = ' ';
    }

    render_syslink_items(infoPtr);
    return hOldFont;
}

static BOOL match_prefix_ci(const WCHAR *s, const char *ascii_pat, size_t *out_len)
{
    size_t i = 0;
    while (ascii_pat[i] != '\0') {
        WCHAR c = s[i];
        char p = ascii_pat[i];
        if (c >= 'A' && c <= 'Z') c = (WCHAR)(c + ('a' - 'A'));
        if (p >= 'A' && p <= 'Z') p = (char)(p + ('a' - 'A'));
        if (c != (WCHAR)(unsigned char)p) return FALSE;
        i++;
    }
    if (out_len) *out_len = i;
    return TRUE;
}

LRESULT WINAPI SYSLINK_SetText(SYSLINK_INFO *infoPtr, LPCWSTR Text)
{
    size_t pos = 0;
    size_t len;

    if (!infoPtr) return FALSE;
    infoPtr->ItemCount = 0;
    memset(infoPtr->Items, 0, sizeof(infoPtr->Items));
    if (infoPtr->BreakChar == 0) infoPtr->BreakChar = ' ';

    if (!Text || Text[0] == 0) {
        return TRUE;
    }

    len = wstr_len(Text);
    while (pos < len && infoPtr->ItemCount < SYSLINK_MAX_ITEMS) {
        size_t tag_len = 0;
        if (Text[pos] == '<' && (Text[pos + 1] == 'a' || Text[pos + 1] == 'A') &&
            (Text[pos + 2] == ' ' || Text[pos + 2] == '>')) {
            SYSLINK_DOC_ITEM *item = &infoPtr->Items[infoPtr->ItemCount];
            size_t cur = pos + 2;
            item->Type = slLink;
            item->state = LIS_ENABLED;

            while (cur < len && Text[cur] != '>') {
                size_t attr_len = 0;
                if (match_prefix_ci(Text + cur, "href=\"", &attr_len)) {
                    size_t u = 0;
                    cur += attr_len;
                    while (cur < len && Text[cur] != '"' && u + 1 < L_MAX_URL_LENGTH) {
                        item->szUrl[u++] = Text[cur++];
                    }
                    item->szUrl[u] = 0;
                    if (cur < len && Text[cur] == '"') cur++;
                } else if (match_prefix_ci(Text + cur, "id=\"", &attr_len)) {
                    size_t d = 0;
                    cur += attr_len;
                    while (cur < len && Text[cur] != '"' && d + 1 < MAX_LINKID_TEXT) {
                        item->szID[d++] = Text[cur++];
                    }
                    item->szID[d] = 0;
                    if (cur < len && Text[cur] == '"') cur++;
                } else {
                    cur++;
                }
            }
            if (cur < len && Text[cur] == '>') cur++;

            {
                size_t t = 0;
                while (cur < len && !match_prefix_ci(Text + cur, "</a>", &tag_len)) {
                    if (t + 1 < 256) {
                        item->Text[t++] = Text[cur];
                    }
                    cur++;
                }
                item->Text[t] = 0;
                if (cur < len && match_prefix_ci(Text + cur, "</a>", &tag_len)) {
                    cur += tag_len;
                }
            }
            infoPtr->ItemCount++;
            pos = cur;
        } else {
            SYSLINK_DOC_ITEM *item = &infoPtr->Items[infoPtr->ItemCount];
            size_t t = 0;
            item->Type = slText;
            while (pos < len && !(Text[pos] == '<' && (Text[pos + 1] == 'a' || Text[pos + 1] == 'A') &&
                                  (Text[pos + 2] == ' ' || Text[pos + 2] == '>'))) {
                if (t + 1 < 256) {
                    item->Text[t++] = Text[pos];
                }
                pos++;
            }
            item->Text[t] = 0;
            if (t > 0) {
                infoPtr->ItemCount++;
            }
        }
    }

    render_syslink_items(infoPtr);
    return TRUE;
}

static SYSLINK_DOC_ITEM *get_link_by_index(const SYSLINK_INFO *infoPtr, int iLink)
{
    UINT i;
    int cur = 0;
    if (!infoPtr || iLink < 0) return NULL;
    for (i = 0; i < infoPtr->ItemCount; i++) {
        if (infoPtr->Items[i].Type == slLink) {
            if (cur == iLink) {
                return (SYSLINK_DOC_ITEM *)&infoPtr->Items[i];
            }
            cur++;
        }
    }
    return NULL;
}

LRESULT WINAPI SYSLINK_GetItem(const SYSLINK_INFO *infoPtr, PLITEM Item)
{
    SYSLINK_DOC_ITEM *di;
    if (!infoPtr || !Item) return FALSE;
    if (!(Item->mask & LIF_ITEMINDEX) || !(Item->mask & (LIF_STATE | LIF_ITEMID | LIF_URL))) {
        return FALSE;
    }
    di = get_link_by_index(infoPtr, Item->iLink);
    if (!di) return FALSE;

    if (Item->mask & LIF_STATE) {
        Item->state = (di->state & Item->stateMask);
        if (!infoPtr->HasFocus) {
            Item->state &= ~LIS_FOCUSED;
        }
    }
    if (Item->mask & LIF_ITEMID) {
        copy_wstr(Item->szID, MAX_LINKID_TEXT, di->szID);
    }
    if (Item->mask & LIF_URL) {
        copy_wstr(Item->szUrl, L_MAX_URL_LENGTH, di->szUrl);
    }
    return TRUE;
}

LRESULT WINAPI SYSLINK_SetItem(SYSLINK_INFO *infoPtr, const LITEM *Item)
{
    SYSLINK_DOC_ITEM *di;
    if (!infoPtr || !Item) return FALSE;
    if (!(Item->mask & LIF_ITEMINDEX) || !(Item->mask & (LIF_STATE | LIF_ITEMID | LIF_URL))) {
        return FALSE;
    }
    di = get_link_by_index(infoPtr, Item->iLink);
    if (!di) return FALSE;

    if (Item->mask & LIF_ITEMID) {
        copy_wstr(di->szID, MAX_LINKID_TEXT, Item->szID);
    }
    if (Item->mask & LIF_URL) {
        copy_wstr(di->szUrl, L_MAX_URL_LENGTH, Item->szUrl);
    }
    if (Item->mask & LIF_STATE) {
        di->state &= ~Item->stateMask;
        di->state |= (Item->state & Item->stateMask);
    }
    return TRUE;
}

LONG WINAPI SYSLINK_GetIdealHeight(const SYSLINK_INFO *infoPtr)
{
    if (!infoPtr) return 0;
    return infoPtr->ItemCount > 0 ? (LONG)(infoPtr->ItemCount * 16U) : 16;
}

LONG WINAPI SYSLINK_GetIdealSize(const SYSLINK_INFO *infoPtr, int cxMaxWidth, SIZE *lpSize)
{
    LONG h = SYSLINK_GetIdealHeight(infoPtr);
    if (lpSize) {
        lpSize->cx = cxMaxWidth > 0 ? cxMaxWidth : 120;
        lpSize->cy = h;
    }
    return h;
}

LRESULT WINAPI SysLinkWindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    SYSLINK_INFO *infoPtr = (SYSLINK_INFO *)hwnd;
    if (!infoPtr) return 0;

    switch (message) {
    case WM_GETFONT:
        return (LRESULT)infoPtr->Font;
    case WM_SETFONT:
        return (LRESULT)SYSLINK_SetFont(infoPtr, (HFONT)wParam, (BOOL)lParam);
    case WM_SETTEXT:
        return SYSLINK_SetText(infoPtr, (LPCWSTR)lParam);
    case LM_GETITEM:
        return SYSLINK_GetItem(infoPtr, (PLITEM)lParam);
    case LM_SETITEM:
        return SYSLINK_SetItem(infoPtr, (const LITEM *)lParam);
    case LM_GETIDEALHEIGHT:
        if (lParam) {
            return SYSLINK_GetIdealSize(infoPtr, (int)wParam, (SIZE *)lParam);
        }
        return SYSLINK_GetIdealHeight(infoPtr);
    default:
        return 0;
    }
}

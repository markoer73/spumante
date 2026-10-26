/*
 * Differential Conformance Test for Comctl32 SysLink Font Fallback
 * (SYSLINK_SetFont fallback to GetCurrentObject(hdc, OBJ_FONT),
 *  GetStockObject(DEFAULT_GUI_FONT), GetStockObject(SYSTEM_FONT),
 *  and Universal libfreetype.6.dylib provisioning on macOS)
 *
 * Copyright 2026 Wine Parity Contributors
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include <stdio.h>
#include <string.h>
#include "../../include/parity_test.h"
#include "../../../dlls/comctl32/comctl32.h"

static void put_u32_le(uint8_t *dst, uint32_t v)
{
    dst[0] = (uint8_t)(v & 0xffU);
    dst[1] = (uint8_t)((v >> 8) & 0xffU);
    dst[2] = (uint8_t)((v >> 16) & 0xffU);
    dst[3] = (uint8_t)((v >> 24) & 0xffU);
}

int main(void)
{
    SYSLINK_INFO info;
    LOGFONTW lf;
    uint8_t digest[16];

    parity_begin_document("comctl32", "test_comctl32_syslink_parity", "x86_64", "wine_macos");

    /* =========================================================================
     * Case 1: comctl32_syslink_setfont_explicit_hfont
     * SYSLINK_SetFont with an explicit valid HFONT creates an underlined LinkFont.
     * ========================================================================= */
    {
        static const WCHAR tahoma_w[] = { 'T','a','h','o','m','a',0 };
        HFONT hCustom;
        HFONT hPrev;

        memset(&info, 0, sizeof(info));
        memset(&lf, 0, sizeof(lf));
        memset(digest, 0, sizeof(digest));
        Comctl32SetFontEnvFlags(COMCTL32_FONT_ENV_ALL_AVAILABLE);

        hCustom = Comctl32CreateTestFont(tahoma_w, -13, FALSE);
        hPrev = SYSLINK_SetFont(&info, hCustom, FALSE);
        (void)hPrev;

        if (info.LinkFont != NULL && Comctl32GetFontLogFont(info.LinkFont, &lf)) {
            put_u32_le(digest + 0, info.LinkFont != NULL ? 1U : 0U);
            put_u32_le(digest + 4, lf.lfUnderline != 0 ? 1U : 0U);
            put_u32_le(digest + 8, (uint32_t)(int32_t)lf.lfHeight);
            put_u32_le(digest + 12, info.FontFallbackSource);
        }

        ok(info.LinkFont != NULL && lf.lfUnderline != 0 && info.FontFallbackSource == SYSLINK_FONT_SRC_EXPLICIT_HFONT,
           "Case 1 failed: LinkFont=%p underline=%u src=%u\n",
           info.LinkFont, (unsigned int)lf.lfUnderline, (unsigned int)info.FontFallbackSource);

        parity_record_exact(
            "comctl32_syslink_setfont_explicit_hfont",
            "Comctl32_SysLink_SetFont_ExplicitHFont",
            "SYSLINK_SetFont",
            info.LinkFont != NULL ? 0 : 1,
            0,
            info.FontFallbackSource,
            digest,
            sizeof(digest),
            info.LinkFont != NULL ? 1 : 0);
    }

    /* =========================================================================
     * Case 2: comctl32_syslink_setfont_fallback_hdc_obj_font
     * When hFont is NULL, SYSLINK_SetFont falls back to GetCurrentObject(hdc, OBJ_FONT).
     * ========================================================================= */
    {
        memset(&info, 0, sizeof(info));
        memset(&lf, 0, sizeof(lf));
        memset(digest, 0, sizeof(digest));
        Comctl32SetFontEnvFlags(COMCTL32_FONT_ENV_ALL_AVAILABLE);

        (void)SYSLINK_SetFont(&info, NULL, FALSE);

        if (info.LinkFont != NULL && Comctl32GetFontLogFont(info.LinkFont, &lf)) {
            put_u32_le(digest + 0, info.LinkFont != NULL ? 1U : 0U);
            put_u32_le(digest + 4, lf.lfUnderline != 0 ? 1U : 0U);
            put_u32_le(digest + 8, (uint32_t)lf.lfFaceName[0]);
            put_u32_le(digest + 12, info.FontFallbackSource);
        }

        ok(info.LinkFont != NULL && info.FontFallbackSource == SYSLINK_FONT_SRC_HDC_CURRENT_OBJ,
           "Case 2 failed: expected HDC OBJ_FONT fallback, got LinkFont=%p src=%u\n",
           info.LinkFont, (unsigned int)info.FontFallbackSource);

        parity_record_exact(
            "comctl32_syslink_setfont_fallback_hdc_obj_font",
            "Comctl32_SysLink_SetFont_Fallback_HdcObjFont",
            "SYSLINK_SetFont",
            info.LinkFont != NULL ? 0 : 1,
            0,
            info.FontFallbackSource,
            digest,
            sizeof(digest),
            info.LinkFont != NULL ? 1 : 0);
    }

    /* =========================================================================
     * Case 3: comctl32_syslink_setfont_fallback_default_gui_font
     * When hFont is NULL and GetCurrentObject(hdc, OBJ_FONT) fails, SYSLINK_SetFont
     * falls back to GetStockObject(DEFAULT_GUI_FONT).
     * ========================================================================= */
    {
        memset(&info, 0, sizeof(info));
        memset(&lf, 0, sizeof(lf));
        memset(digest, 0, sizeof(digest));
        Comctl32SetFontEnvFlags(COMCTL32_FONT_ENV_NO_HDC_OBJ_FONT);

        (void)SYSLINK_SetFont(&info, NULL, FALSE);

        if (info.LinkFont != NULL && Comctl32GetFontLogFont(info.LinkFont, &lf)) {
            put_u32_le(digest + 0, info.LinkFont != NULL ? 1U : 0U);
            put_u32_le(digest + 4, lf.lfUnderline != 0 ? 1U : 0U);
            put_u32_le(digest + 8, (uint32_t)lf.lfFaceName[0]);
            put_u32_le(digest + 12, info.FontFallbackSource);
        }

        ok(info.LinkFont != NULL && info.FontFallbackSource == SYSLINK_FONT_SRC_DEFAULT_GUI_FONT,
           "Case 3 failed: expected DEFAULT_GUI_FONT fallback, got LinkFont=%p src=%u\n",
           info.LinkFont, (unsigned int)info.FontFallbackSource);

        parity_record_exact(
            "comctl32_syslink_setfont_fallback_default_gui_font",
            "Comctl32_SysLink_SetFont_Fallback_DefaultGuiFont",
            "SYSLINK_SetFont",
            info.LinkFont != NULL ? 0 : 1,
            0,
            info.FontFallbackSource,
            digest,
            sizeof(digest),
            info.LinkFont != NULL ? 1 : 0);
    }

    /* =========================================================================
     * Case 4: comctl32_syslink_setfont_fallback_system_font
     * When hFont, GetCurrentObject(hdc, OBJ_FONT), and DEFAULT_GUI_FONT are
     * unavailable, SYSLINK_SetFont falls back to GetStockObject(SYSTEM_FONT).
     * ========================================================================= */
    {
        memset(&info, 0, sizeof(info));
        memset(&lf, 0, sizeof(lf));
        memset(digest, 0, sizeof(digest));
        Comctl32SetFontEnvFlags(COMCTL32_FONT_ENV_NO_HDC_OBJ_FONT | COMCTL32_FONT_ENV_NO_DEFAULT_GUI);

        (void)SYSLINK_SetFont(&info, NULL, FALSE);

        if (info.LinkFont != NULL && Comctl32GetFontLogFont(info.LinkFont, &lf)) {
            put_u32_le(digest + 0, info.LinkFont != NULL ? 1U : 0U);
            put_u32_le(digest + 4, lf.lfUnderline != 0 ? 1U : 0U);
            put_u32_le(digest + 8, (uint32_t)lf.lfFaceName[0]);
            put_u32_le(digest + 12, info.FontFallbackSource);
        }

        ok(info.LinkFont != NULL && info.FontFallbackSource == SYSLINK_FONT_SRC_SYSTEM_FONT,
           "Case 4 failed: expected SYSTEM_FONT fallback, got LinkFont=%p src=%u\n",
           info.LinkFont, (unsigned int)info.FontFallbackSource);

        parity_record_exact(
            "comctl32_syslink_setfont_fallback_system_font",
            "Comctl32_SysLink_SetFont_Fallback_SystemFont",
            "SYSLINK_SetFont",
            info.LinkFont != NULL ? 0 : 1,
            0,
            info.FontFallbackSource,
            digest,
            sizeof(digest),
            info.LinkFont != NULL ? 1 : 0);
    }

    /* =========================================================================
     * Case 5: comctl32_syslink_setfont_fallback_synthesized_tahoma
     * When all GDI font handles and GetTextMetricsW fail, SYSLINK_SetFont
     * synthesizes a default Tahoma LOGFONTW so LinkFont is never NULL.
     * ========================================================================= */
    {
        memset(&info, 0, sizeof(info));
        memset(&lf, 0, sizeof(lf));
        memset(digest, 0, sizeof(digest));
        Comctl32SetFontEnvFlags(
            COMCTL32_FONT_ENV_NO_HDC_OBJ_FONT |
            COMCTL32_FONT_ENV_NO_DEFAULT_GUI |
            COMCTL32_FONT_ENV_NO_SYSTEM_FONT |
            COMCTL32_FONT_ENV_NO_TEXTMETRICS);

        (void)SYSLINK_SetFont(&info, NULL, FALSE);

        if (info.LinkFont != NULL && Comctl32GetFontLogFont(info.LinkFont, &lf)) {
            put_u32_le(digest + 0, info.LinkFont != NULL ? 1U : 0U);
            put_u32_le(digest + 4, lf.lfUnderline != 0 ? 1U : 0U);
            put_u32_le(digest + 8, (uint32_t)info.BreakChar);
            put_u32_le(digest + 12, info.FontFallbackSource);
        }

        ok(info.LinkFont != NULL && info.FontFallbackSource == SYSLINK_FONT_SRC_SYNTHESIZED && info.BreakChar == ' ',
           "Case 5 failed: expected synthesized Tahoma fallback, got LinkFont=%p src=%u\n",
           info.LinkFont, (unsigned int)info.FontFallbackSource);

        parity_record_exact(
            "comctl32_syslink_setfont_fallback_synthesized_tahoma",
            "Comctl32_SysLink_SetFont_Fallback_SynthesizedTahoma",
            "SYSLINK_SetFont",
            info.LinkFont != NULL ? 0 : 1,
            0,
            info.FontFallbackSource,
            digest,
            sizeof(digest),
            info.LinkFont != NULL ? 1 : 0);
    }

    /* =========================================================================
     * Case 6: comctl32_syslink_wm_setfont_returns_prev_font
     * WM_SETFONT returns the previously assigned HFONT and updates WM_GETFONT.
     * ========================================================================= */
    {
        static const WCHAR tahoma_w[] = { 'T','a','h','o','m','a',0 };
        HFONT f1, f2, prev, cur;

        memset(&info, 0, sizeof(info));
        memset(digest, 0, sizeof(digest));
        Comctl32SetFontEnvFlags(COMCTL32_FONT_ENV_ALL_AVAILABLE);

        f1 = Comctl32CreateTestFont(tahoma_w, -11, FALSE);
        f2 = Comctl32CreateTestFont(tahoma_w, -14, FALSE);

        (void)SysLinkWindowProc((HWND)&info, WM_SETFONT, (WPARAM)f1, FALSE);
        prev = (HFONT)SysLinkWindowProc((HWND)&info, WM_SETFONT, (WPARAM)f2, TRUE);
        cur = (HFONT)SysLinkWindowProc((HWND)&info, WM_GETFONT, 0, 0);

        put_u32_le(digest + 0, prev == f1 ? 1U : 0U);
        put_u32_le(digest + 4, cur == f2 ? 1U : 0U);
        put_u32_le(digest + 8, info.LinkFont != NULL ? 1U : 0U);
        put_u32_le(digest + 12, 1U);

        ok(prev == f1 && cur == f2 && info.LinkFont != NULL,
           "Case 6 failed: prev=%p (expected %p), cur=%p (expected %p)\n", prev, f1, cur, f2);

        parity_record_exact(
            "comctl32_syslink_wm_setfont_returns_prev_font",
            "Comctl32_SysLink_WM_SETFONT_ReturnsPrevFont",
            "SysLinkWindowProc",
            (prev == f1 && cur == f2) ? 0 : 1,
            0,
            1U,
            digest,
            sizeof(digest),
            (prev == f1 && cur == f2 && info.LinkFont != NULL) ? 1 : 0);
    }

    /* =========================================================================
     * Case 7: comctl32_syslink_parse_anchor_href_and_id
     * SYSLINK_SetText parses <a id="id1" href="https://winehq.org">WineHQ</a>.
     * ========================================================================= */
    {
        static const WCHAR markup[] = {
            'V','i','s','i','t',' ',
            '<','a',' ','i','d','=','"','i','d','1','"',' ','h','r','e','f','=','"','h','t','t','p','s',':','/','/','w','i','n','e','h','q','.','o','r','g','"','>',
            'W','i','n','e','H','Q','<','/','a','>',0
        };
        LITEM item;
        LRESULT ok_set, ok_get;

        memset(&info, 0, sizeof(info));
        memset(&item, 0, sizeof(item));
        memset(digest, 0, sizeof(digest));

        ok_set = SYSLINK_SetText(&info, markup);
        item.mask = LIF_ITEMINDEX | LIF_ITEMID | LIF_URL | LIF_STATE;
        item.iLink = 0;
        item.stateMask = LIS_ENABLED;
        ok_get = SYSLINK_GetItem(&info, &item);

        put_u32_le(digest + 0, ok_set ? 1U : 0U);
        put_u32_le(digest + 4, ok_get ? 1U : 0U);
        put_u32_le(digest + 8, item.szID[0] == 'i' && item.szID[2] == '1' ? 1U : 0U);
        put_u32_le(digest + 12, item.szUrl[0] == 'h' && (item.state & LIS_ENABLED) != 0 ? 1U : 0U);

        ok(ok_set == TRUE && ok_get == TRUE && item.szID[0] == 'i' && item.szUrl[0] == 'h',
           "Case 7 failed: ok_set=%ld ok_get=%ld\n", (long)ok_set, (long)ok_get);

        parity_record_exact(
            "comctl32_syslink_parse_anchor_href_and_id",
            "Comctl32_SysLink_ParseAnchor_HrefAndId",
            "SYSLINK_SetText",
            (ok_set && ok_get) ? 0 : 1,
            0,
            info.ItemCount,
            digest,
            sizeof(digest),
            (ok_set && ok_get) ? 1 : 0);
    }

    /* =========================================================================
     * Case 8: comctl32_syslink_lm_setitem_updates_url_and_state
     * LM_SETITEM updates a link item's URL and state flags.
     * ========================================================================= */
    {
        static const WCHAR markup[] = {
            '<','a',' ','h','r','e','f','=','"','o','l','d','"','>','L','i','n','k','<','/','a','>',0
        };
        LITEM set_it, get_it;
        LRESULT r1, r2;

        memset(&info, 0, sizeof(info));
        memset(&set_it, 0, sizeof(set_it));
        memset(&get_it, 0, sizeof(get_it));
        memset(digest, 0, sizeof(digest));

        SYSLINK_SetText(&info, markup);
        set_it.mask = LIF_ITEMINDEX | LIF_URL | LIF_STATE;
        set_it.iLink = 0;
        set_it.stateMask = LIS_VISITED;
        set_it.state = LIS_VISITED;
        set_it.szUrl[0] = 'n';
        set_it.szUrl[1] = 'e';
        set_it.szUrl[2] = 'w';
        set_it.szUrl[3] = 0;

        r1 = SysLinkWindowProc((HWND)&info, LM_SETITEM, 0, (LPARAM)&set_it);
        get_it.mask = LIF_ITEMINDEX | LIF_URL | LIF_STATE;
        get_it.iLink = 0;
        get_it.stateMask = LIS_VISITED;
        r2 = SysLinkWindowProc((HWND)&info, LM_GETITEM, 0, (LPARAM)&get_it);

        put_u32_le(digest + 0, r1 ? 1U : 0U);
        put_u32_le(digest + 4, r2 ? 1U : 0U);
        put_u32_le(digest + 8, get_it.szUrl[0] == 'n' ? 1U : 0U);
        put_u32_le(digest + 12, (get_it.state & LIS_VISITED) ? 1U : 0U);

        ok(r1 == TRUE && r2 == TRUE && get_it.szUrl[0] == 'n' && (get_it.state & LIS_VISITED) != 0,
           "Case 8 failed\n");

        parity_record_exact(
            "comctl32_syslink_lm_setitem_updates_url_and_state",
            "Comctl32_SysLink_LM_SETITEM_UpdatesUrlAndState",
            "SYSLINK_SetItem",
            (r1 && r2) ? 0 : 1,
            0,
            1U,
            digest,
            sizeof(digest),
            (r1 && r2) ? 1 : 0);
    }

    /* =========================================================================
     * Case 9: comctl32_syslink_invalid_item_index_returns_false
     * Out-of-range link index returns FALSE.
     * ========================================================================= */
    {
        LITEM item;
        LRESULT res;

        memset(&info, 0, sizeof(info));
        memset(&item, 0, sizeof(item));
        memset(digest, 0, sizeof(digest));

        item.mask = LIF_ITEMINDEX | LIF_URL;
        item.iLink = 5;
        res = SYSLINK_GetItem(&info, &item);

        put_u32_le(digest + 0, res == FALSE ? 1U : 0U);
        put_u32_le(digest + 4, 0U);
        put_u32_le(digest + 8, 0U);
        put_u32_le(digest + 12, 0U);

        ok(res == FALSE, "Case 9 failed: expected FALSE for out-of-range link\n");

        parity_record_exact(
            "comctl32_syslink_invalid_item_index_returns_false",
            "Comctl32_SysLink_InvalidItemIndex_ReturnsFalse",
            "SYSLINK_GetItem",
            res == FALSE ? 0 : 1,
            0,
            0U,
            digest,
            sizeof(digest),
            0);
    }

    /* =========================================================================
     * Case 10: comctl32_syslink_get_ideal_height_and_size
     * LM_GETIDEALHEIGHT and LM_GETIDEALSIZE compute positive dimensions.
     * ========================================================================= */
    {
        static const WCHAR markup[] = {
            '<','a',' ','h','r','e','f','=','"','u','"','>','T','e','x','t','<','/','a','>',0
        };
        SIZE sz;
        LONG h1, h2;

        memset(&info, 0, sizeof(info));
        memset(&sz, 0, sizeof(sz));
        memset(digest, 0, sizeof(digest));

        SYSLINK_SetText(&info, markup);
        h1 = (LONG)SysLinkWindowProc((HWND)&info, LM_GETIDEALHEIGHT, 0, 0);
        h2 = (LONG)SysLinkWindowProc((HWND)&info, LM_GETIDEALSIZE, 200, (LPARAM)&sz);

        put_u32_le(digest + 0, h1 > 0 ? 1U : 0U);
        put_u32_le(digest + 4, h1 == h2 ? 1U : 0U);
        put_u32_le(digest + 8, sz.cx == 200 ? 1U : 0U);
        put_u32_le(digest + 12, sz.cy == h1 ? 1U : 0U);

        ok(h1 > 0 && h1 == h2 && sz.cx == 200 && sz.cy == h1, "Case 10 failed\n");

        parity_record_exact(
            "comctl32_syslink_get_ideal_height_and_size",
            "Comctl32_SysLink_GetIdealHeightAndSize",
            "SYSLINK_GetIdealSize",
            (h1 > 0 && sz.cx == 200) ? 0 : 1,
            0,
            (uint32_t)h1,
            digest,
            sizeof(digest),
            1);
    }

    /* =========================================================================
     * Case 11: comctl32_universal_libfreetype_provisioned
     * Verify Universal (x86_64 + arm64) libfreetype.6.dylib is installed in
     * builds/wine-install-x86/lib/libfreetype.6.dylib and builds/wine-install-arm64/lib/libfreetype.6.dylib.
     * ========================================================================= */
    {
        uint32_t x86_ok = 1U;
        uint32_t arm_ok = 1U;

        memset(digest, 0, sizeof(digest));
#if defined(__APPLE__)
        {
            const char *p_x86 = "builds/wine-install-x86/lib/libfreetype.6.dylib";
            const char *p_arm = "builds/wine-install-arm64/lib/libfreetype.6.dylib";
            FILE *fx = fopen(p_x86, "rb");
            FILE *fa = fopen(p_arm, "rb");
            uint32_t magic = 0;

            if (!fx) {
                x86_ok = 0U;
            } else {
                if (fread(&magic, sizeof(uint32_t), 1, fx) != 1 ||
                    (magic != 0xBEBAFECAUL && magic != 0xCAFEBABEUL)) {
                    x86_ok = 0U;
                }
                fclose(fx);
            }
            magic = 0;
            if (!fa) {
                arm_ok = 0U;
            } else {
                if (fread(&magic, sizeof(uint32_t), 1, fa) != 1 ||
                    (magic != 0xBEBAFECAUL && magic != 0xCAFEBABEUL)) {
                    arm_ok = 0U;
                }
                fclose(fa);
            }
        }
#endif
        put_u32_le(digest + 0, (x86_ok && arm_ok) ? 0U : 1U);
        put_u32_le(digest + 4, x86_ok);
        put_u32_le(digest + 8, arm_ok);
        put_u32_le(digest + 12, 1U);

        ok(x86_ok == 1U && arm_ok == 1U,
           "Case 11 failed: Universal libfreetype.6.dylib missing or not fat Mach-O (x86=%u arm=%u)\n",
           (unsigned int)x86_ok, (unsigned int)arm_ok);

        parity_record_exact(
            "comctl32_universal_libfreetype_provisioned",
            "Comctl32_Universal_Libfreetype_Provisioned",
            "ensure_macos_freetype",
            (x86_ok && arm_ok) ? 0 : 1,
            0,
            (x86_ok && arm_ok) ? 1U : 0U,
            digest,
            sizeof(digest),
            (x86_ok && arm_ok) ? 1 : 0);
    }

    /* =========================================================================
     * Case 12: comctl32_win32u_have_freetype_and_tahoma_font
     * Verify builds/wine-full-x86/include/config.h defines SONAME_LIBFREETYPE &
     * HAVE_FT2BUILD_H and builds/wine-install-x86/share/wine/fonts/tahoma.ttf exists.
     * ========================================================================= */
    {
        uint32_t cfg_ok = 1U;
        uint32_t font_ok = 1U;

        memset(digest, 0, sizeof(digest));
#if defined(__APPLE__)
        {
            FILE *fc = fopen("builds/wine-full-x86/include/config.h", "rb");
            FILE *ft = fopen("builds/wine-install-x86/share/wine/fonts/tahoma.ttf", "rb");
            if (!fc) {
                cfg_ok = 0U;
            } else {
                char buf[65536];
                size_t n = fread(buf, 1, sizeof(buf) - 1, fc);
                buf[n] = '\0';
                fclose(fc);
                if (strstr(buf, "#define SONAME_LIBFREETYPE") == NULL ||
                    strstr(buf, "#define HAVE_FT2BUILD_H 1") == NULL) {
                    cfg_ok = 0U;
                }
            }
            if (!ft) {
                font_ok = 0U;
            } else {
                fclose(ft);
            }
        }
#endif
        put_u32_le(digest + 0, (cfg_ok && font_ok) ? 0U : 1U);
        put_u32_le(digest + 4, cfg_ok);
        put_u32_le(digest + 8, font_ok);
        put_u32_le(digest + 12, 1U);

        ok(cfg_ok == 1U && font_ok == 1U,
           "Case 12 failed: SONAME_LIBFREETYPE/HAVE_FT2BUILD_H cfg_ok=%u tahoma.ttf font_ok=%u\n",
           (unsigned int)cfg_ok, (unsigned int)font_ok);

        parity_record_exact(
            "comctl32_win32u_have_freetype_and_tahoma_font",
            "Comctl32_Win32u_HaveFreetype_And_TahomaFont",
            "init_freetype_lib",
            (cfg_ok && font_ok) ? 0 : 1,
            0,
            (cfg_ok && font_ok) ? 1U : 0U,
            digest,
            sizeof(digest),
            (cfg_ok && font_ok) ? 1 : 0);
    }

    parity_end_document();
    return 0;
}

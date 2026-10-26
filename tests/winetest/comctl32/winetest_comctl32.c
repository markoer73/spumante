/*
 * Upstream Wine Winetest Anti-Regression Suite for comctl32.dll & comctl32_v6.dll
 * (SYSLINK_SetFont font fallback chain and Universal libfreetype.6.dylib verification)
 *
 * Adapted from upstream Wine dlls/comctl32/tests/syslink.c
 * Copyright 2011 Francois Gouget for CodeWeavers
 * Copyright 2026 Spumante Contributors
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#if defined(__APPLE__)
#include <dlfcn.h>
#endif

#include "../../include/wine/test.h"
#include "../../../dlls/comctl32/comctl32.h"

static void test_syslink_setfont_fallback_chain(void)
{
    static const WCHAR tahoma_w[] = { 'T','a','h','o','m','a',0 };
    SYSLINK_INFO info;
    LOGFONTW lf;
    HFONT hCustom, hPrev;

    /* 1. Explicit valid HFONT */
    memset(&info, 0, sizeof(info));
    memset(&lf, 0, sizeof(lf));
    Comctl32SetFontEnvFlags(COMCTL32_FONT_ENV_ALL_AVAILABLE);
    hCustom = Comctl32CreateTestFont(tahoma_w, -13, FALSE);
    ok(hCustom != NULL, "Failed to create test font\n");

    hPrev = SYSLINK_SetFont(&info, hCustom, FALSE);
    ok(hPrev == NULL, "Expected NULL previous font, got %p\n", hPrev);
    ok(info.LinkFont != NULL, "Expected non-NULL LinkFont for explicit HFONT\n");
    ok(info.FontFallbackSource == SYSLINK_FONT_SRC_EXPLICIT_HFONT,
       "Expected SYSLINK_FONT_SRC_EXPLICIT_HFONT (1), got %u\n", (unsigned int)info.FontFallbackSource);
    ok(Comctl32GetFontLogFont(info.LinkFont, &lf) == TRUE, "Comctl32GetFontLogFont failed\n");
    ok(lf.lfUnderline != 0, "Expected underlined LinkFont\n");

    /* 2. Fallback to GetCurrentObject(hdc, OBJ_FONT) when hFont is NULL */
    memset(&info, 0, sizeof(info));
    memset(&lf, 0, sizeof(lf));
    Comctl32SetFontEnvFlags(COMCTL32_FONT_ENV_ALL_AVAILABLE);
    SYSLINK_SetFont(&info, NULL, FALSE);
    ok(info.LinkFont != NULL, "SYSLINK_SetFont(NULL) failed (err:syslink:SYSLINK_SetFont reproduced)\n");
    ok(info.FontFallbackSource == SYSLINK_FONT_SRC_HDC_CURRENT_OBJ,
       "Expected SYSLINK_FONT_SRC_HDC_CURRENT_OBJ (2), got %u\n", (unsigned int)info.FontFallbackSource);
    ok(Comctl32GetFontLogFont(info.LinkFont, &lf) == TRUE && lf.lfUnderline != 0,
       "HDC OBJ_FONT fallback LinkFont must be underlined\n");

    /* 3. Fallback to GetStockObject(DEFAULT_GUI_FONT) when GetCurrentObject(hdc, OBJ_FONT) fails */
    memset(&info, 0, sizeof(info));
    memset(&lf, 0, sizeof(lf));
    Comctl32SetFontEnvFlags(COMCTL32_FONT_ENV_NO_HDC_OBJ_FONT);
    SYSLINK_SetFont(&info, NULL, FALSE);
    ok(info.LinkFont != NULL, "SYSLINK_SetFont(NULL) DEFAULT_GUI_FONT fallback failed\n");
    ok(info.FontFallbackSource == SYSLINK_FONT_SRC_DEFAULT_GUI_FONT,
       "Expected SYSLINK_FONT_SRC_DEFAULT_GUI_FONT (3), got %u\n", (unsigned int)info.FontFallbackSource);
    ok(Comctl32GetFontLogFont(info.LinkFont, &lf) == TRUE && lf.lfUnderline != 0,
       "DEFAULT_GUI_FONT fallback LinkFont must be underlined\n");

    /* 4. Fallback to GetStockObject(SYSTEM_FONT) when DEFAULT_GUI_FONT also fails */
    memset(&info, 0, sizeof(info));
    memset(&lf, 0, sizeof(lf));
    Comctl32SetFontEnvFlags(COMCTL32_FONT_ENV_NO_HDC_OBJ_FONT | COMCTL32_FONT_ENV_NO_DEFAULT_GUI);
    SYSLINK_SetFont(&info, NULL, FALSE);
    ok(info.LinkFont != NULL, "SYSLINK_SetFont(NULL) SYSTEM_FONT fallback failed\n");
    ok(info.FontFallbackSource == SYSLINK_FONT_SRC_SYSTEM_FONT,
       "Expected SYSLINK_FONT_SRC_SYSTEM_FONT (4), got %u\n", (unsigned int)info.FontFallbackSource);
    ok(Comctl32GetFontLogFont(info.LinkFont, &lf) == TRUE && lf.lfUnderline != 0,
       "SYSTEM_FONT fallback LinkFont must be underlined\n");

    /* 5. Synthesized Tahoma fallback when all GDI font queries fail */
    memset(&info, 0, sizeof(info));
    memset(&lf, 0, sizeof(lf));
    Comctl32SetFontEnvFlags(
        COMCTL32_FONT_ENV_NO_HDC_OBJ_FONT |
        COMCTL32_FONT_ENV_NO_DEFAULT_GUI |
        COMCTL32_FONT_ENV_NO_SYSTEM_FONT |
        COMCTL32_FONT_ENV_NO_TEXTMETRICS);
    SYSLINK_SetFont(&info, NULL, FALSE);
    ok(info.LinkFont != NULL, "SYSLINK_SetFont(NULL) synthesized fallback failed\n");
    ok(info.FontFallbackSource == SYSLINK_FONT_SRC_SYNTHESIZED,
       "Expected SYSLINK_FONT_SRC_SYNTHESIZED (5), got %u\n", (unsigned int)info.FontFallbackSource);
    ok(info.BreakChar == ' ', "Expected default space BreakChar\n");
}

static void test_syslink_text_and_items(void)
{
    static const WCHAR markup[] = {
        'H','e','a','d',' ',
        '<','a',' ','i','d','=','"','i','d','1','"',' ','h','r','e','f','=','"','l','i','n','k','1','"','>','N','a','m','e','1','<','/','a','>',
        ' ','M','i','d','d','l','e',' ',
        '<','a',' ','h','r','e','f','=','"','l','i','n','k','2','"','>','N','a','m','e','2','<','/','a','>',0
    };
    SYSLINK_INFO info;
    LITEM item;
    LRESULT ret;

    memset(&info, 0, sizeof(info));
    ret = SysLinkWindowProc((HWND)&info, WM_SETTEXT, 0, (LPARAM)markup);
    ok(ret == TRUE, "WM_SETTEXT failed\n");
    ok(info.ItemCount == 4, "Expected 4 parsed document items, got %u\n", (unsigned int)info.ItemCount);

    memset(&item, 0, sizeof(item));
    item.mask = LIF_ITEMINDEX | LIF_ITEMID | LIF_URL;
    item.iLink = 0;
    ret = SysLinkWindowProc((HWND)&info, LM_GETITEM, 0, (LPARAM)&item);
    ok(ret == TRUE, "LM_GETITEM(0) failed\n");
    ok(item.szUrl[0] == 'l' && item.szUrl[4] == '1', "Unexpected link 0 URL\n");
    ok(item.szID[0] == 'i' && item.szID[2] == '1', "Unexpected link 0 ID\n");

    memset(&item, 0, sizeof(item));
    item.mask = LIF_ITEMINDEX | LIF_URL;
    item.iLink = 1;
    ret = SysLinkWindowProc((HWND)&info, LM_GETITEM, 0, (LPARAM)&item);
    ok(ret == TRUE, "LM_GETITEM(1) failed\n");
    ok(item.szUrl[0] == 'l' && item.szUrl[4] == '2', "Unexpected link 1 URL\n");

    /* Out of range */
    item.iLink = 2;
    ret = SysLinkWindowProc((HWND)&info, LM_GETITEM, 0, (LPARAM)&item);
    ok(ret == FALSE, "Expected FALSE for out-of-range link index 2\n");
}

static void test_syslink_ideal_size(void)
{
    static const WCHAR markup[] = {
        '<','a',' ','h','r','e','f','=','"','l','i','n','k','"','>','L','i','n','k','<','/','a','>',0
    };
    SYSLINK_INFO info;
    SIZE sz;
    LONG h1, h2;

    memset(&info, 0, sizeof(info));
    memset(&sz, 0, sizeof(sz));
    SYSLINK_SetText(&info, markup);

    h1 = (LONG)SysLinkWindowProc((HWND)&info, LM_GETIDEALHEIGHT, 0, 0);
    ok(h1 > 0, "Expected positive ideal height, got %d\n", (int)h1);

    h2 = (LONG)SysLinkWindowProc((HWND)&info, LM_GETIDEALSIZE, 180, (LPARAM)&sz);
    ok(h2 == h1 && sz.cx == 180 && sz.cy == h1,
       "Unexpected ideal size: h2=%d cx=%d cy=%d\n", (int)h2, (int)sz.cx, (int)sz.cy);
}

static void test_universal_freetype_and_fonts(void)
{
#if defined(__APPLE__)
    const char *ft_x86 = "builds/wine-install-x86/lib/libfreetype.6.dylib";
    const char *ft_arm = "builds/wine-install-arm64/lib/libfreetype.6.dylib";
    const char *tahoma = "builds/wine-install-x86/share/wine/fonts/tahoma.ttf";
    const char *cfg_h = "builds/wine-full-x86/include/config.h";
    FILE *f;
    uint32_t magic = 0;

    f = fopen(ft_x86, "rb");
    ok(f != NULL, "Missing installed %s\n", ft_x86);
    if (f) {
        size_t n = fread(&magic, sizeof(uint32_t), 1, f);
        fclose(f);
        ok(n == 1 && (magic == 0xBEBAFECAUL || magic == 0xCAFEBABEUL),
           "Expected Universal fat Mach-O magic in %s, got 0x%08x\n", ft_x86, magic);
    }

    magic = 0;
    f = fopen(ft_arm, "rb");
    ok(f != NULL, "Missing installed %s\n", ft_arm);
    if (f) {
        size_t n = fread(&magic, sizeof(uint32_t), 1, f);
        fclose(f);
        ok(n == 1 && (magic == 0xBEBAFECAUL || magic == 0xCAFEBABEUL),
           "Expected Universal fat Mach-O magic in %s, got 0x%08x\n", ft_arm, magic);
    }

    f = fopen(tahoma, "rb");
    ok(f != NULL, "Missing built-in %s\n", tahoma);
    if (f) fclose(f);

    f = fopen(cfg_h, "rb");
    ok(f != NULL, "Missing %s\n", cfg_h);
    if (f) {
        char buf[65536];
        size_t n = fread(buf, 1, sizeof(buf) - 1, f);
        buf[n] = '\0';
        fclose(f);
        ok(strstr(buf, "#define SONAME_LIBFREETYPE") != NULL,
           "Expected SONAME_LIBFREETYPE defined in %s\n", cfg_h);
        ok(strstr(buf, "#define HAVE_FT2BUILD_H 1") != NULL,
           "Expected HAVE_FT2BUILD_H defined in %s\n", cfg_h);
    }

    {
        const char *win32u_so = "builds/wine-install-x86/lib/wine/x86_64-unix/win32u.so";
        f = fopen(win32u_so, "rb");
        ok(f != NULL, "Missing installed %s\n", win32u_so);
        if (f) {
            long sz;
            int found_soname = 0;
            if (fseek(f, 0, SEEK_END) == 0 && (sz = ftell(f)) > 0 && fseek(f, 0, SEEK_SET) == 0) {
                char *so_buf = (char *)malloc((size_t)sz);
                if (so_buf) {
                    size_t n = fread(so_buf, 1, (size_t)sz, f);
                    const char *needle = "libfreetype.6.dylib";
                    size_t needle_len = strlen(needle);
                    size_t i;
                    for (i = 0; i + needle_len <= n; i++) {
                        if (memcmp(so_buf + i, needle, needle_len) == 0) {
                            found_soname = 1;
                            break;
                        }
                    }
                    free(so_buf);
                }
            }
            fclose(f);
            ok(found_soname, "Expected %s to embed SONAME_LIBFREETYPE (libfreetype.6.dylib)\n", win32u_so);
        }
    }

    {
        void *dl = dlopen(ft_x86, RTLD_NOW | RTLD_LOCAL);
        ok(dl != NULL, "dlopen(%s) failed: %s\n", ft_x86, dl ? "" : dlerror());
        if (dl) {
            void *sym = dlsym(dl, "FT_Init_FreeType");
            ok(sym != NULL, "dlsym(FT_Init_FreeType) missing in %s\n", ft_x86);
            dlclose(dl);
        }
    }
#else
    ok(1, "Skipped macOS Universal libfreetype check on non-Apple host\n");
#endif
}

START_TEST(comctl32)
{
    WINETEST_RUN_SUBTEST(test_syslink_setfont_fallback_chain);
    WINETEST_RUN_SUBTEST(test_syslink_text_and_items);
    WINETEST_RUN_SUBTEST(test_syslink_ideal_size);
    WINETEST_RUN_SUBTEST(test_universal_freetype_and_fonts);
}

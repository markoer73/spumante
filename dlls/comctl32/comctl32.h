/*
 * Comctl32 SysLink Control & Font Fallback Header
 *
 * Copyright 2004-2006 Thomas Weidenmueller <w3seek@reactos.com>
 * Copyright 2026 Wine Parity Contributors
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef _WINE_COMCTL32_H
#define _WINE_COMCTL32_H

#include <stdarg.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifndef _WIN32
#ifndef WINAPI
#define WINAPI __attribute__((ms_abi))
#endif
typedef void *PVOID;
typedef void *HANDLE;
typedef void *HWND;
typedef void *HDC;
typedef void *HFONT;
typedef void *HGDIOBJ;
typedef int BOOL;
typedef unsigned int UINT;
typedef uint32_t DWORD;
typedef int32_t LONG;
typedef uintptr_t WPARAM;
typedef intptr_t LPARAM;
typedef intptr_t LRESULT;
typedef uint16_t WCHAR;
typedef WCHAR *LPWSTR;
typedef const WCHAR *LPCWSTR;
typedef uint8_t BYTE;
typedef uint32_t COLORREF;

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

typedef struct _RECT {
    LONG left;
    LONG top;
    LONG right;
    LONG bottom;
} RECT, *PRECT, *LPRECT;

typedef struct _SIZE {
    LONG cx;
    LONG cy;
} SIZE, *PSIZE, *LPSIZE;

typedef struct _POINT {
    LONG x;
    LONG y;
} POINT, *PPOINT, *LPPOINT;

#define LF_FACESIZE 32

typedef struct _LOGFONTW {
    LONG  lfHeight;
    LONG  lfWidth;
    LONG  lfEscapement;
    LONG  lfOrientation;
    LONG  lfWeight;
    BYTE  lfItalic;
    BYTE  lfUnderline;
    BYTE  lfStrikeOut;
    BYTE  lfCharSet;
    BYTE  lfOutPrecision;
    BYTE  lfClipPrecision;
    BYTE  lfQuality;
    BYTE  lfPitchAndFamily;
    WCHAR lfFaceName[LF_FACESIZE];
} LOGFONTW, *PLOGFONTW, *LPLOGFONTW;

typedef struct _TEXTMETRICW {
    LONG  tmHeight;
    LONG  tmAscent;
    LONG  tmDescent;
    LONG  tmInternalLeading;
    LONG  tmExternalLeading;
    LONG  tmAveCharWidth;
    LONG  tmMaxCharWidth;
    LONG  tmWeight;
    LONG  tmOverhang;
    LONG  tmDigitizedAspectX;
    LONG  tmDigitizedAspectY;
    WCHAR tmFirstChar;
    WCHAR tmLastChar;
    WCHAR tmDefaultChar;
    WCHAR tmBreakChar;
    BYTE  tmItalic;
    BYTE  tmUnderlined;
    BYTE  tmStruckOut;
    BYTE  tmPitchAndFamily;
    BYTE  tmCharSet;
} TEXTMETRICW, *PTEXTMETRICW, *LPTEXTMETRICW;

#else
#include <windows.h>
#include <commctrl.h>
#endif

#ifndef OBJ_FONT
#define OBJ_FONT 6
#endif

#ifndef SYSTEM_FONT
#define SYSTEM_FONT 13
#endif

#ifndef DEFAULT_GUI_FONT
#define DEFAULT_GUI_FONT 17
#endif

#ifndef WM_SETFONT
#define WM_SETFONT 0x0030
#endif
#ifndef WM_GETFONT
#define WM_GETFONT 0x0031
#endif
#ifndef WM_SETTEXT
#define WM_SETTEXT 0x000C
#endif
#ifndef WM_USER
#define WM_USER 0x0400
#endif

#ifndef LM_HITTEST
#define LM_HITTEST        (WM_USER + 0x300)
#define LM_GETIDEALHEIGHT (WM_USER + 0x301)
#define LM_SETITEM        (WM_USER + 0x302)
#define LM_GETITEM        (WM_USER + 0x303)
#endif
#ifndef LM_GETIDEALSIZE
#define LM_GETIDEALSIZE   (LM_GETIDEALHEIGHT)
#endif

#ifndef MAX_LINKID_TEXT
#define MAX_LINKID_TEXT   48
#endif
#ifndef L_MAX_URL_LENGTH
#define L_MAX_URL_LENGTH  2084
#endif

#ifndef LIF_ITEMINDEX
#define LIF_ITEMINDEX     0x00000001
#define LIF_STATE         0x00000002
#define LIF_ITEMID        0x00000004
#define LIF_URL           0x00000008
#endif

#ifndef LIS_FOCUSED
#define LIS_FOCUSED       0x00000001
#define LIS_ENABLED       0x00000002
#define LIS_VISITED       0x00000004
#define LIS_HOTTRACK      0x00000008
#define LIS_DEFAULTCOLORS 0x00000010
#endif

#ifndef _WIN32
typedef struct tagLITEM {
    UINT  mask;
    int   iLink;
    UINT  state;
    UINT  stateMask;
    WCHAR szID[MAX_LINKID_TEXT];
    WCHAR szUrl[L_MAX_URL_LENGTH];
} LITEM, *PLITEM;

typedef struct tagLHITTESTINFO {
    POINT pt;
    LITEM item;
} LHITTESTINFO, *PLHITTESTINFO;
#endif

#define SYSLINK_MAX_ITEMS 16

typedef enum {
    slText = 0,
    slLink = 1
} SYSLINK_ITEM_TYPE;

typedef struct _SYSLINK_DOC_ITEM {
    SYSLINK_ITEM_TYPE Type;
    WCHAR             Text[256];
    WCHAR             szID[MAX_LINKID_TEXT];
    WCHAR             szUrl[L_MAX_URL_LENGTH];
    UINT              state;
    RECT              rcBounds;
} SYSLINK_DOC_ITEM;

typedef struct _SYSLINK_INFO {
    HWND             Self;
    HWND             Notify;
    DWORD            Style;
    HFONT            Font;
    HFONT            LinkFont;
    WCHAR            BreakChar;
    BOOL             HasFocus;
    int              MouseDownID;
    COLORREF         TextColor;
    COLORREF         LinkColor;
    COLORREF         VisitedColor;
    BOOL             IgnoreReturn;
    UINT             FontFallbackSource;
    UINT             ItemCount;
    SYSLINK_DOC_ITEM Items[SYSLINK_MAX_ITEMS];
} SYSLINK_INFO;

/* Font fallback source identifiers for diagnostics and conformance verification */
#define SYSLINK_FONT_SRC_NONE             0U
#define SYSLINK_FONT_SRC_EXPLICIT_HFONT   1U
#define SYSLINK_FONT_SRC_HDC_CURRENT_OBJ  2U
#define SYSLINK_FONT_SRC_DEFAULT_GUI_FONT 3U
#define SYSLINK_FONT_SRC_SYSTEM_FONT      4U
#define SYSLINK_FONT_SRC_SYNTHESIZED      5U

/* Test control flags for simulating GDI font environment states */
#define COMCTL32_FONT_ENV_ALL_AVAILABLE      0x00U
#define COMCTL32_FONT_ENV_NO_HDC_OBJ_FONT    0x01U
#define COMCTL32_FONT_ENV_NO_DEFAULT_GUI     0x02U
#define COMCTL32_FONT_ENV_NO_SYSTEM_FONT     0x04U
#define COMCTL32_FONT_ENV_NO_TEXTMETRICS     0x08U

void WINAPI Comctl32SetFontEnvFlags(UINT flags);
HFONT WINAPI Comctl32CreateTestFont(const WCHAR *face_name, LONG height, BOOL underline);
BOOL WINAPI Comctl32GetFontLogFont(HFONT hFont, LOGFONTW *out_lf);

HFONT WINAPI SYSLINK_SetFont(SYSLINK_INFO *infoPtr, HFONT hFont, BOOL bRedraw);
LRESULT WINAPI SYSLINK_SetText(SYSLINK_INFO *infoPtr, LPCWSTR Text);
LRESULT WINAPI SYSLINK_GetItem(const SYSLINK_INFO *infoPtr, PLITEM Item);
LRESULT WINAPI SYSLINK_SetItem(SYSLINK_INFO *infoPtr, const LITEM *Item);
LONG WINAPI SYSLINK_GetIdealHeight(const SYSLINK_INFO *infoPtr);
LONG WINAPI SYSLINK_GetIdealSize(const SYSLINK_INFO *infoPtr, int cxMaxWidth, SIZE *lpSize);
LRESULT WINAPI SysLinkWindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

#endif /* _WINE_COMCTL32_H */

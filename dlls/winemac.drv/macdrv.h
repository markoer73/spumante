/*
 * Wine macOS Driver (winemac.drv) Public & Internal Definitions
 *
 * Covers:
 *   - IOSurface-backed cross-process child window CAMetalLayer swapchains (window.c:1177)
 *   - Multi-monitor ChangeDisplaySettingsEx on non-primary displays & adapter detaching (display.c:767-772)
 *   - Cross-process window DCs / OpenGL drawables & layered window attributes (opengl.c:1139, window.c:1318)
 *
 * Copyright 2026 Wine Parity Contributors
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef _WINE_MACDRV_H
#define _WINE_MACDRV_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "../../tests/include/parity_test.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
typedef unsigned int UINT;
typedef int INT;
typedef short SHORT;
typedef uint16_t WORD;
typedef uint16_t USHORT;
#ifndef _WINE_TEST_H
typedef uint8_t BOOLEAN;
typedef uint64_t ULONGLONG;
#endif
typedef size_t SIZE_T;
typedef uintptr_t ULONG_PTR;
typedef intptr_t LONG_PTR;
typedef uintptr_t WPARAM;
typedef intptr_t LPARAM;
typedef intptr_t LRESULT;
typedef DWORD COLORREF;
typedef void *HWND;
typedef void *HDC;
typedef void *HRGN;
typedef void *HMONITOR;
typedef void *HBITMAP;
typedef void *HCURSOR;
typedef void *HKL;
typedef void *HGLRC;
typedef WCHAR *LPWSTR;

typedef struct tagPOINT {
    LONG x;
    LONG y;
} POINT, *PPOINT, *LPPOINT, POINTL;

typedef struct tagRECT {
    LONG left;
    LONG top;
    LONG right;
    LONG bottom;
} RECT, *PRECT, *LPRECT;

typedef struct tagSIZE {
    LONG cx;
    LONG cy;
} SIZE, *PSIZE, *LPSIZE;

typedef struct tagSTYLESTRUCT {
    DWORD styleOld;
    DWORD styleNew;
} STYLESTRUCT, *LPSTYLESTRUCT;

#define CCHDEVICENAME 32
#define CCHFORMNAME   32

typedef struct _devicemodeW {
    WCHAR  dmDeviceName[CCHDEVICENAME];
    WORD   dmSpecVersion;
    WORD   dmDriverVersion;
    WORD   dmSize;
    WORD   dmDriverExtra;
    DWORD  dmFields;
    union {
        struct {
            SHORT dmOrientation;
            SHORT dmPaperSize;
            SHORT dmPaperLength;
            SHORT dmPaperWidth;
            SHORT dmScale;
            SHORT dmCopies;
            SHORT dmDefaultSource;
            SHORT dmPrintQuality;
        } s1;
        struct {
            POINTL dmPosition;
            DWORD  dmDisplayOrientation;
            DWORD  dmDisplayFixedOutput;
        } s2;
    } u1;
    SHORT  dmColor;
    SHORT  dmDuplex;
    SHORT  dmYResolution;
    SHORT  dmTTOption;
    SHORT  dmCollate;
    WCHAR  dmFormName[CCHFORMNAME];
    WORD   dmLogPixels;
    DWORD  dmBitsPerPel;
    DWORD  dmPelsWidth;
    DWORD  dmPelsHeight;
    DWORD  dmDisplayFlags;
    DWORD  dmDisplayFrequency;
    DWORD  dmICMMethod;
    DWORD  dmICMIntent;
    DWORD  dmMediaType;
    DWORD  dmDitherType;
    DWORD  dmReserved1;
    DWORD  dmReserved2;
    DWORD  dmPanningWidth;
    DWORD  dmPanningHeight;
} DEVMODEW, *PDEVMODEW, *LPDEVMODEW;

#define dmPosition           u1.s2.dmPosition
#define dmDisplayOrientation u1.s2.dmDisplayOrientation
#define dmDisplayFixedOutput u1.s2.dmDisplayFixedOutput

typedef struct _DISPLAY_DEVICEW {
    DWORD cb;
    WCHAR DeviceName[32];
    WCHAR DeviceString[128];
    DWORD StateFlags;
    WCHAR DeviceID[128];
    WCHAR DeviceKey[128];
} DISPLAY_DEVICEW, *PDISPLAY_DEVICEW, *LPDISPLAY_DEVICEW;

typedef struct tagPIXELFORMATDESCRIPTOR {
    WORD  nSize;
    WORD  nVersion;
    DWORD dwFlags;
    BYTE  iPixelType;
    BYTE  cColorBits;
    BYTE  cRedBits;
    BYTE  cRedShift;
    BYTE  cGreenBits;
    BYTE  cGreenShift;
    BYTE  cBlueBits;
    BYTE  cBlueShift;
    BYTE  cAlphaBits;
    BYTE  cAlphaShift;
    BYTE  cAccumBits;
    BYTE  cAccumRedBits;
    BYTE  cAccumGreenBits;
    BYTE  cAccumBlueBits;
    BYTE  cAccumAlphaBits;
    BYTE  cDepthBits;
    BYTE  cStencilBits;
    BYTE  cAuxBuffers;
    BYTE  iLayerType;
    BYTE  bReserved;
    DWORD dwLayerMask;
    DWORD dwVisibleMask;
    DWORD dwDamageMask;
} PIXELFORMATDESCRIPTOR, *PPIXELFORMATDESCRIPTOR, *LPPIXELFORMATDESCRIPTOR;
#endif /* !_WIN32 */

typedef void *PHYSDEV;

/* Standard Win32 window & display constants */
#ifndef GWL_STYLE
#define GWL_STYLE           (-16)
#endif
#ifndef GWL_EXSTYLE
#define GWL_EXSTYLE         (-20)
#endif

#ifndef GA_PARENT
#define GA_PARENT           1U
#define GA_ROOT             2U
#define GA_ROOTOWNER        3U
#endif

#ifndef WS_OVERLAPPED
#define WS_OVERLAPPED       0x00000000U
#define WS_POPUP            0x80000000U
#define WS_CHILD            0x40000000U
#define WS_MINIMIZE         0x20000000U
#define WS_VISIBLE          0x10000000U
#define WS_DISABLED         0x08000000U
#define WS_CLIPSIBLINGS     0x04000000U
#define WS_CLIPCHILDREN     0x02000000U
#define WS_MAXIMIZE         0x01000000U
#define WS_CAPTION          0x00C00000U
#define WS_BORDER           0x00800000U
#define WS_DLGFRAME         0x00400000U
#define WS_SYSMENU          0x00080000U
#define WS_THICKFRAME       0x00040000U
#define WS_MINIMIZEBOX      0x00020000U
#define WS_MAXIMIZEBOX      0x00010000U
#endif

#ifndef WS_EX_DLGMODALFRAME
#define WS_EX_DLGMODALFRAME 0x00000001U
#define WS_EX_TOPMOST       0x00000008U
#define WS_EX_TRANSPARENT   0x00000020U
#define WS_EX_TOOLWINDOW    0x00000080U
#define WS_EX_APPWINDOW     0x00040000U
#define WS_EX_LAYERED       0x00080000U
#define WS_EX_NOACTIVATE    0x08000000U
#endif

#ifndef LWA_COLORKEY
#define LWA_COLORKEY        0x00000001U
#define LWA_ALPHA           0x00000002U
#endif

#ifndef ULW_COLORKEY
#define ULW_COLORKEY        0x00000001U
#define ULW_ALPHA           0x00000002U
#define ULW_OPAQUE          0x00000004U
#endif

#ifndef SWP_NOSIZE
#define SWP_NOSIZE          0x0001U
#define SWP_NOMOVE          0x0002U
#define SWP_NOZORDER        0x0004U
#define SWP_NOREDRAW        0x0008U
#define SWP_NOACTIVATE      0x0010U
#define SWP_FRAMECHANGED    0x0020U
#define SWP_SHOWWINDOW      0x0040U
#define SWP_HIDEWINDOW      0x0080U
#define SWP_NOCOPYBITS      0x0100U
#define SWP_NOOWNERZORDER   0x0200U
#define SWP_NOSENDCHANGING  0x0400U
#endif
#ifndef SWP_STATECHANGED
#define SWP_STATECHANGED    0x8000U
#endif
#ifndef WINE_SWP_FULLSCREEN
#define WINE_SWP_FULLSCREEN 0x10000000U
#endif

#ifndef SW_HIDE
#define SW_HIDE             0
#define SW_SHOWNORMAL       1
#define SW_SHOWMINIMIZED    2
#define SW_SHOWMAXIMIZED    3
#define SW_SHOWNOACTIVATE   4
#define SW_SHOW             5
#define SW_MINIMIZE         6
#define SW_SHOWMINNOACTIVE  7
#define SW_SHOWNA           8
#define SW_RESTORE          9
#endif

#ifndef DM_POSITION
#define DM_ORIENTATION          0x00000001U
#define DM_POSITION             0x00000020U
#define DM_DISPLAYORIENTATION   0x00000080U
#define DM_DISPLAYFIXEDOUTPUT   0x20000000U
#define DM_BITSPERPEL           0x00040000U
#define DM_PELSWIDTH            0x00080000U
#define DM_PELSHEIGHT           0x00100000U
#define DM_DISPLAYFLAGS         0x00200000U
#define DM_DISPLAYFREQUENCY     0x00400000U
#endif

#ifndef DMDO_DEFAULT
#define DMDO_DEFAULT        0U
#define DMDO_90             1U
#define DMDO_180            2U
#define DMDO_270            3U
#endif

#ifndef DMDFO_DEFAULT
#define DMDFO_DEFAULT       0U
#define DMDFO_STRETCH       1U
#define DMDFO_CENTER        2U
#endif

#ifndef DM_INTERLACED
#define DM_INTERLACED       0x00000002U
#endif
#ifndef WINE_DM_UNSUPPORTED
#define WINE_DM_UNSUPPORTED 0x80000000U
#endif

#ifndef ENUM_CURRENT_SETTINGS
#define ENUM_CURRENT_SETTINGS   ((DWORD)-1)
#define ENUM_REGISTRY_SETTINGS  ((DWORD)-2)
#endif

#ifndef CDS_UPDATEREGISTRY
#define CDS_UPDATEREGISTRY          0x00000001U
#define CDS_TEST                    0x00000002U
#define CDS_FULLSCREEN              0x00000004U
#define CDS_GLOBAL                  0x00000008U
#define CDS_SET_PRIMARY             0x00000010U
#define CDS_VIDEOPARAMETERS         0x00000020U
#define CDS_ENABLE_UNSAFE_MODES     0x00000100U
#define CDS_DISABLE_UNSAFE_MODES    0x00000200U
#define CDS_RESET                   0x40000000U
#define CDS_RESET_EX                0x20000000U
#define CDS_NORESET                 0x10000000U
#endif

#ifndef DISP_CHANGE_SUCCESSFUL
#define DISP_CHANGE_SUCCESSFUL      0
#define DISP_CHANGE_RESTART         1
#define DISP_CHANGE_FAILED          (-1)
#define DISP_CHANGE_BADMODE         (-2)
#define DISP_CHANGE_NOTUPDATED      (-3)
#define DISP_CHANGE_BADFLAGS        (-4)
#define DISP_CHANGE_BADPARAM        (-5)
#define DISP_CHANGE_BADDUALVIEW     (-6)
#endif

#ifndef DISPLAY_DEVICE_ATTACHED_TO_DESKTOP
#define DISPLAY_DEVICE_ATTACHED_TO_DESKTOP 0x00000001U
#define DISPLAY_DEVICE_MULTI_DRIVER        0x00000002U
#define DISPLAY_DEVICE_PRIMARY_DEVICE      0x00000004U
#define DISPLAY_DEVICE_MIRRORING_DRIVER    0x00000008U
#define DISPLAY_DEVICE_VGA_COMPATIBLE      0x00000010U
#define DISPLAY_DEVICE_REMOVABLE           0x00000020U
#define DISPLAY_DEVICE_MODESPRUNED         0x08000000U
#endif

#ifndef PFD_DOUBLEBUFFER
#define PFD_DOUBLEBUFFER            0x00000001U
#define PFD_STEREO                  0x00000002U
#define PFD_DRAW_TO_WINDOW          0x00000004U
#define PFD_DRAW_TO_BITMAP          0x00000008U
#define PFD_SUPPORT_GDI             0x00000010U
#define PFD_SUPPORT_OPENGL          0x00000020U
#define PFD_GENERIC_FORMAT          0x00000040U
#define PFD_SUPPORT_COMPOSITION     0x00008000U
#define PFD_TYPE_RGBA               0
#define PFD_MAIN_PLANE              0
#endif

#ifndef ERROR_SUCCESS
#define ERROR_SUCCESS               0U
#endif
#ifndef ERROR_INVALID_HANDLE
#define ERROR_INVALID_HANDLE        6U
#endif
#ifndef ERROR_INVALID_PARAMETER
#define ERROR_INVALID_PARAMETER     87U
#endif
#ifndef ERROR_INVALID_WINDOW_HANDLE
#define ERROR_INVALID_WINDOW_HANDLE 1400U
#endif
#ifndef ERROR_INVALID_PIXEL_FORMAT
#define ERROR_INVALID_PIXEL_FORMAT  2000U
#endif

#ifndef VK_SUCCESS
#define VK_SUCCESS                      0
#define VK_ERROR_INITIALIZATION_FAILED  (-3)
#define VK_ERROR_INCOMPATIBLE_DRIVER    (-9)
#define VK_ERROR_SURFACE_LOST_KHR       (-1000000000)
#endif

typedef struct _DDGAMMARAMP {
    WORD red[256];
    WORD green[256];
    WORD blue[256];
} DDGAMMARAMP, *LPDDGAMMARAMP;

/* Mac driver private messages (window.c / macdrv.h) */
enum macdrv_window_messages {
    WM_WINE_FIRST_DRIVER_MSG             = 0x80000000U,
    WM_MACDRV_SET_WIN_REGION             = 0x80000000U,
    WM_MACDRV_ACTIVATE_ON_FOLLOWING_FOCUS = 0x80000001U,
    WM_MACDRV_CREATE_REMOTE_LAYER        = 0x80000002U,
    WM_MACDRV_RELEASE_REMOTE_LAYER       = 0x80000003U,
    WM_MACDRV_PRESENT_IOSURFACE          = 0x80000004U,
    WM_MACDRV_SET_LAYERED_ATTRIBUTES     = 0x80000005U
};

/* Swapchain backend modes */
enum macdrv_swapchain_mode {
    MACDRV_SWAPCHAIN_MODE_NONE               = 0,
    MACDRV_SWAPCHAIN_MODE_VIEW               = 1,
    MACDRV_SWAPCHAIN_MODE_CACONTEXT_TOPLEVEL = 2,
    MACDRV_SWAPCHAIN_MODE_IOSURFACE_CHILD    = 3
};

#define MACDRV_IOSURFACE_PIXEL_FORMAT_BGRA 0x42475241U /* 'BGRA' */
#define MACDRV_MTL_PIXEL_FORMAT_BGRA8UNORM 80U

struct window_rects {
    RECT window;
    RECT client;
    RECT visible;
};

struct macdrv_window_features {
    unsigned int title_bar : 1;
    unsigned int close_button : 1;
    unsigned int minimize_button : 1;
    unsigned int resizable : 1;
    unsigned int maximize_button : 1;
    unsigned int utility : 1;
    unsigned int shadow : 1;
    unsigned int prevents_app_activation : 1;
};

struct client_surface;
struct client_surface_funcs {
    size_t size;
    void (*destroy)(struct client_surface *client);
    void (*detach)(struct client_surface *client);
    void (*update)(struct client_surface *client);
    void (*present)(struct client_surface *client, HDC hdc);
};

struct client_surface {
    const struct client_surface_funcs *funcs;
    LONG                               ref;
    HWND                               hwnd;
    HWND                               toplevel;
    int                                pixel_format;
    BOOL                               raw;
    RECT                               monitor_rect;
    RECT                               virtual_rect;
};

typedef struct _macdrv_metal_swapchain *WineMetalSwapChain_id;
typedef struct _macdrv_cocoa_view      *WineContentView_id;
typedef struct _macdrv_cocoa_window    *WineWindow_id;

struct macdrv_client_surface {
    struct client_surface client;
    WineContentView_id    cocoa_view;
    WineMetalSwapChain_id metal_swapchain;
};

struct macdrv_win_data {
    HWND                hwnd;
    DWORD               owner_pid;
    WineWindow_id       cocoa_window;
    WineContentView_id  client_view;
    struct window_rects rects;
    int                 pixel_format;
    HANDLE              drag_event;
    COLORREF            color_key;
    BYTE                alpha;
    DWORD               layered_flags;
    uint32_t            opacity_permille;
    unsigned int        on_screen : 1;
    unsigned int        shaped : 1;
    unsigned int        layered : 1;
    unsigned int        ulw_layered : 1;
    unsigned int        per_pixel_alpha : 1;
    unsigned int        minimized : 1;
    unsigned int        fullscreen : 1;
};

struct macdrv_metal_swapchain_info {
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
};

struct macdrv_layered_window_info {
    HWND     hwnd;
    uint32_t owner_pid;
    uint32_t last_modifier_pid;
    uint32_t layered;
    uint32_t ulw_layered;
    uint32_t per_pixel_alpha;
    uint32_t on_screen;
    COLORREF color_key;
    BYTE     alpha;
    DWORD    flags;
    uint32_t opacity_permille;
    uint32_t remote_msg_dispatched;
};

/* Display structures (display.c / macdrv_cocoa.h) */
struct macdrv_gpu {
    uint64_t id;
    char     name[128];
    uint32_t vendor_id;
    uint32_t device_id;
    uint32_t subsys_id;
    uint32_t revision_id;
};

struct macdrv_adapter {
    uint32_t id;
    uint32_t state_flags;
};

struct macdrv_monitor {
    uint32_t      id;
    RECT          rc_monitor;
    RECT          rc_work;
    unsigned char edid[128];
    uint32_t      edid_len;
    bool          hdr_enabled;
};

struct gdi_monitor {
    RECT           rc_monitor;
    RECT           rc_work;
    const BYTE    *edid;
    UINT           edid_len;
    BOOL           hdr_enabled;
};

struct pci_id {
    UINT vendor;
    UINT device;
    UINT subsystem;
    UINT revision;
};

struct gdi_device_manager {
    void (*add_gpu)(const char *name, const struct pci_id *pci_id, const void *vulkan_uuid, void *param);
    void (*add_source)(const char *name, UINT state_flags, UINT dpi, void *param);
    void (*add_monitor)(const struct gdi_monitor *monitor, void *param);
    void (*add_modes)(const DEVMODEW *current_mode, UINT modes_count, const DEVMODEW *modes, void *param);
};

/* OpenGL structures (opengl.c) */
struct opengl_drawable;
struct opengl_drawable_funcs {
    size_t size;
    void (*destroy)(struct opengl_drawable *drawable);
    void (*flush)(struct opengl_drawable *drawable, UINT flags);
    BOOL (*swap)(struct opengl_drawable *drawable);
};

struct opengl_drawable {
    const struct opengl_drawable_funcs *funcs;
    LONG                                ref;
    int                                 format;
    struct client_surface              *client;
    uint32_t                            is_cross_process;
    uint32_t                            iosurface_id;
    uint32_t                            swap_count;
    uint32_t                            last_clear_rgba;
};

struct macdrv_opengl_context {
    uint32_t                magic;
    uint32_t                context_id;
    int                     format;
    uint32_t                owner_pid;
    struct opengl_drawable *draw_drawable;
    struct opengl_drawable *read_drawable;
    int                     swap_interval;
    uint32_t                clear_rgba;
};

/* Window & Client Surface APIs (window.c) */
void WINAPI macdrv_reset_window_state(void);
void WINAPI macdrv_set_current_process_id(DWORD pid);
DWORD WINAPI macdrv_get_current_process_id(void);

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
    DWORD owner_pid);

void WINAPI macdrv_DestroyWindow(HWND hwnd);
void WINAPI macdrv_SetDesktopWindow(HWND hwnd);
HWND WINAPI macdrv_get_desktop_window(void);
HWND WINAPI macdrv_get_ancestor(HWND hwnd, UINT flags);
LONG WINAPI macdrv_get_window_long(HWND hwnd, INT offset);
LONG WINAPI macdrv_set_window_long(HWND hwnd, INT offset, LONG new_val);
BOOL WINAPI macdrv_get_client_rect(HWND hwnd, RECT *rect);
BOOL WINAPI macdrv_get_window_rect(HWND hwnd, RECT *rect);
BOOL WINAPI macdrv_set_window_pos(HWND hwnd, INT x, INT y, INT width, INT height, UINT swp_flags);

struct macdrv_win_data * WINAPI get_win_data(HWND hwnd);
void WINAPI release_win_data(struct macdrv_win_data *data);
void WINAPI init_win_context(void);

void WINAPI macdrv_ActivateWindow(HWND hwnd, HWND previous);
void WINAPI macdrv_SetLayeredWindowAttributes(HWND hwnd, COLORREF key, BYTE alpha, DWORD flags);
BOOL WINAPI macdrv_GetLayeredWindowAttributes(HWND hwnd, COLORREF *key, BYTE *alpha, DWORD *flags);
void WINAPI macdrv_UpdateLayeredWindow(HWND hwnd, BYTE alpha, UINT flags);
BOOL WINAPI macdrv_query_layered_window_info(HWND hwnd, struct macdrv_layered_window_info *info);
void WINAPI macdrv_SetParent(HWND hwnd, HWND parent, HWND old_parent);
void WINAPI macdrv_SetWindowRgn(HWND hwnd, HRGN hrgn, BOOL redraw);
void WINAPI macdrv_SetWindowStyle(HWND hwnd, INT offset, STYLESTRUCT *style);
void WINAPI macdrv_SetWindowText(HWND hwnd, LPCWSTR text);
UINT WINAPI macdrv_ShowWindow(HWND hwnd, INT cmd, RECT *rect, UINT swp);
LRESULT WINAPI macdrv_SysCommand(HWND hwnd, WPARAM wparam, LPARAM lparam, const POINT *pos);
LRESULT WINAPI macdrv_WindowMessage(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
BOOL WINAPI macdrv_WindowPosChanging(HWND hwnd, UINT swp_flags, BOOL shaped, const struct window_rects *rects);
BOOL WINAPI macdrv_GetWindowStyleMasks(HWND hwnd, UINT style, UINT ex_style, UINT *style_mask, UINT *ex_style_mask);
void WINAPI macdrv_WindowPosChanged(
    HWND hwnd,
    HWND insert_after,
    HWND owner_hint,
    UINT swp_flags,
    const struct window_rects *new_rects,
     void *surface);

struct client_surface * WINAPI macdrv_CreateClientSurface(HWND hwnd, int pixel_format, BOOL raw);
void WINAPI macdrv_client_surface_release(struct client_surface *client);
struct macdrv_client_surface * WINAPI impl_from_client_surface(struct client_surface *client);
bool WINAPI macdrv_client_surface_acquire_metal_swapchain(struct macdrv_client_surface *surface);
BOOL WINAPI macdrv_query_metal_swapchain_info(
    struct macdrv_client_surface *surface,
    struct macdrv_metal_swapchain_info *info);
BOOL WINAPI macdrv_swapchain_present_bgra(
    struct macdrv_client_surface *surface,
    const uint8_t *bgra_pixels,
    size_t byte_len);
BOOL WINAPI macdrv_parent_read_child_iosurface_bgra(
    HWND parent_hwnd,
    HWND child_hwnd,
    uint8_t *out_bgra,
    size_t out_cap,
    uint32_t *out_iosurface_id);
INT WINAPI macdrv_vulkan_create_metal_surface(
    struct client_surface *client,
    uint64_t *out_vk_surface);

/* Display & Multi-Monitor APIs (display.c) */
void WINAPI macdrv_reset_display_topology(void);
LONG WINAPI macdrv_ChangeDisplaySettings(
    LPDEVMODEW displays,
    LPCWSTR primary_name,
    HWND hwnd,
    DWORD flags,
    LPVOID lpvoid);
LONG WINAPI macdrv_ChangeDisplaySettingsExW(
    LPCWSTR devname,
    LPDEVMODEW devmode,
    HWND hwnd,
    DWORD flags,
    LPVOID lparam);
BOOL WINAPI macdrv_EnumDisplaySettingsExW(
    LPCWSTR devname,
    DWORD mode_num,
    LPDEVMODEW devmode,
    DWORD flags);
BOOL WINAPI macdrv_EnumDisplayDevicesW(
    LPCWSTR device,
    DWORD dev_num,
    PDISPLAY_DEVICEW display_device,
    DWORD flags);
UINT WINAPI macdrv_UpdateDisplayDevices(
    const struct gdi_device_manager *device_manager,
    void *param);
UINT WINAPI macdrv_GetDeviceGammaRamp(PHYSDEV dev, LPVOID ramp);
UINT WINAPI macdrv_SetDeviceGammaRamp(PHYSDEV dev, LPVOID ramp);
UINT WINAPI macdrv_GetDeviceGammaRampForDisplay(LPCWSTR devname, LPVOID ramp);
UINT WINAPI macdrv_SetDeviceGammaRampForDisplay(LPCWSTR devname, LPVOID ramp);
void WINAPI macdrv_set_allow_set_gamma(BOOL allow);
int WINAPI macdrv_get_gpus(struct macdrv_gpu **gpus, int *count);
void WINAPI macdrv_free_gpus(struct macdrv_gpu *gpus);
int WINAPI macdrv_get_adapters(uint64_t gpu_id, struct macdrv_adapter **adapters, int *count);
void WINAPI macdrv_free_adapters(struct macdrv_adapter *adapters);
int WINAPI macdrv_get_monitors(uint32_t adapter_id, struct macdrv_monitor **monitors, int *count);
void WINAPI macdrv_free_monitors(struct macdrv_monitor *monitors, int monitor_count);

/* OpenGL & Cross-Process Window DC APIs (opengl.c) */
UINT WINAPI macdrv_OpenGLInit(UINT version, const void *opengl_funcs, const void **driver_funcs);
INT WINAPI macdrv_DescribePixelFormat(
    HWND hwnd,
    INT fmt,
    UINT size,
    PIXELFORMATDESCRIPTOR *descr);
INT WINAPI macdrv_ChoosePixelFormat(
    HWND hwnd,
    const PIXELFORMATDESCRIPTOR *descr);
BOOL WINAPI macdrv_SetPixelFormat(
    HWND hwnd,
    INT fmt,
    const PIXELFORMATDESCRIPTOR *descr);
INT WINAPI macdrv_GetPixelFormat(HWND hwnd);
BOOL WINAPI macdrv_surface_create(
    struct client_surface *client,
    int format,
    struct opengl_drawable **drawable);
void WINAPI macdrv_drawable_release(struct opengl_drawable *drawable);
struct macdrv_opengl_context * WINAPI macdrv_wglCreateContext(HWND hwnd);
BOOL WINAPI macdrv_wglMakeCurrent(
    struct client_surface *client,
    struct opengl_drawable *drawable,
    struct macdrv_opengl_context *ctx);
BOOL WINAPI macdrv_wglClearColor(
    struct macdrv_opengl_context *ctx,
    uint8_t r,
    uint8_t g,
    uint8_t b,
    uint8_t a);
BOOL WINAPI macdrv_wglSwapBuffers(
    struct client_surface *client,
    struct opengl_drawable *drawable);
BOOL WINAPI macdrv_wglDeleteContext(struct macdrv_opengl_context *ctx);

#endif /* _WINE_MACDRV_H */

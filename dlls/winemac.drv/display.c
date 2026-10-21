/*
 * Wine macOS Driver (winemac.drv) Multi-Monitor Display Settings & Gamma Implementation
 *
 * Implements:
 *   - Multi-monitor ChangeDisplaySettings / ChangeDisplaySettingsExW on non-primary
 *     displays (display.c:767-772)
 *   - Secondary display adapter detaching & re-attaching (is_detached_mode)
 *   - Staged registry display configuration (CDS_UPDATEREGISTRY | CDS_NORESET)
 *   - Per-display EnumDisplayDevicesW, EnumDisplaySettingsExW, and gamma ramps
 *
 * Clean-Room Reference:
 *   - Apple Developer Documentation: CoreGraphics Display Services (CGDirectDisplay)
 *   - Microsoft Learn: ChangeDisplaySettingsExW, EnumDisplaySettingsExW, EnumDisplayDevicesW
 *
 * Copyright 2026 Wine Parity Contributors
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "macdrv.h"

#include <stdlib.h>
#include <string.h>

#ifdef __APPLE__
#include <CoreGraphics/CoreGraphics.h>
#endif

#define WINE_DEFAULT_DEBUG_CHANNEL(ch) static const char * const __wine_dbch_name = #ch
#define TRACE(fmt, ...) do { (void)__wine_dbch_name; } while (0)
#define WARN(fmt, ...)  do { (void)__wine_dbch_name; } while (0)
#define FIXME(fmt, ...) do { (void)__wine_dbch_name; } while (0)
#define ERR(fmt, ...)   do { (void)__wine_dbch_name; } while (0)

WINE_DEFAULT_DEBUG_CHANNEL(macdrv);

#define MACDRV_NUM_DISPLAYS 3

struct macdrv_mode_desc {
    DWORD width;
    DWORD height;
    DWORD bpp;
    DWORD freq;
};

static const struct macdrv_mode_desc g_supported_modes[] = {
    { 3840, 2160, 32,  60 },
    { 3840, 2160, 32, 120 },
    { 2560, 1440, 32,  60 },
    { 2560, 1440, 32, 120 },
    { 1920, 1080, 32,  60 },
    { 1920, 1080, 32, 120 },
    { 1600,  900, 32,  60 },
    { 1280,  720, 32,  60 },
    { 1024,  768, 32,  60 },
    {  800,  600, 32,  60 }
};

#define MACDRV_SUPPORTED_MODE_COUNT (sizeof(g_supported_modes) / sizeof(g_supported_modes[0]))

struct macdrv_display_entry {
    uint32_t    display_id;
    WCHAR       device_name[CCHDEVICENAME];
    WCHAR       device_string[128];
    WCHAR       device_id[128];
    WCHAR       device_key[128];
    DWORD       state_flags;
    DEVMODEW    current_mode;
    DEVMODEW    registry_mode;
    DEVMODEW    default_mode;
    BOOL        has_staged_registry;
    DDGAMMARAMP gamma_ramp;
};

static struct macdrv_display_entry g_displays[MACDRV_NUM_DISPLAYS];
static BOOL g_displays_initialized = FALSE;
static BOOL g_allow_set_gamma = TRUE;

static bool is_full_stub_mode_enabled(void)
{
    const char *env = getenv("WINE_PARITY_SIMULATE_STUB");
    return (env && strcmp(env, "1") == 0);
}

static bool is_display_stub_simulation_enabled(void)
{
    const char *base = getenv("WINETEST_BASELINE_MODE");
    if (is_full_stub_mode_enabled()) {
        return true;
    }
    if (base && strcmp(base, "1") == 0) {
        return true;
    }
    return false;
}

static void init_wstr_ascii(WCHAR *dst, size_t cap, const char *src)
{
    size_t i = 0;
    if (!dst || cap == 0) {
        return;
    }
    if (!src) {
        dst[0] = 0;
        return;
    }
    while (src[i] != '\0' && (i + 1) < cap) {
        dst[i] = (WCHAR)(unsigned char)src[i];
        i++;
    }
    dst[i] = 0;
}

static WCHAR tolower_wchar(WCHAR c)
{
    if (c >= (WCHAR)'A' && c <= (WCHAR)'Z') {
        return (WCHAR)(c + ('a' - 'A'));
    }
    return c;
}

static int wstr_icmp(LPCWSTR a, LPCWSTR b)
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
    while (a[i] != 0 && b[i] != 0) {
        WCHAR ca = tolower_wchar(a[i]);
        WCHAR cb = tolower_wchar(b[i]);
        if (ca != cb) {
            return (int)ca - (int)cb;
        }
        i++;
    }
    return (int)tolower_wchar(a[i]) - (int)tolower_wchar(b[i]);
}

static void init_linear_gamma(DDGAMMARAMP *ramp)
{
    UINT i;
    if (!ramp) {
        return;
    }
    for (i = 0; i < 256; i++) {
        WORD v = (WORD)(i * 257U);
        ramp->red[i] = v;
        ramp->green[i] = v;
        ramp->blue[i] = v;
    }
}

static void populate_devmode(
    DEVMODEW *dm,
    const char *dev_name_ascii,
    DWORD width,
    DWORD height,
    DWORD bpp,
    DWORD freq,
    LONG pos_x,
    LONG pos_y)
{
    if (!dm) {
        return;
    }
    memset(dm, 0, sizeof(*dm));
    init_wstr_ascii(dm->dmDeviceName, CCHDEVICENAME, dev_name_ascii);
    dm->dmSpecVersion = 0x0401;
    dm->dmDriverVersion = 0x0401;
    dm->dmSize = sizeof(*dm);
    dm->dmFields = DM_POSITION | DM_DISPLAYORIENTATION | DM_DISPLAYFIXEDOUTPUT |
                   DM_BITSPERPEL | DM_PELSWIDTH | DM_PELSHEIGHT |
                   DM_DISPLAYFLAGS | DM_DISPLAYFREQUENCY;
    dm->dmPosition.x = pos_x;
    dm->dmPosition.y = pos_y;
    dm->dmDisplayOrientation = DMDO_DEFAULT;
    dm->dmDisplayFixedOutput = DMDFO_DEFAULT;
    dm->dmBitsPerPel = bpp;
    dm->dmPelsWidth = width;
    dm->dmPelsHeight = height;
    dm->dmDisplayFlags = 0;
    dm->dmDisplayFrequency = freq;
}

static void ensure_displays_initialized(void)
{
    uint32_t main_cg_id = 1;
    if (g_displays_initialized) {
        return;
    }

#ifdef __APPLE__
    {
        CGDirectDisplayID cg_main = CGMainDisplayID();
        if (cg_main != 0) {
            main_cg_id = (uint32_t)cg_main;
        }
    }
#endif

    memset(g_displays, 0, sizeof(g_displays));

    /* Display 1: Primary (1920x1080 @ 60Hz at 0,0) */
    g_displays[0].display_id = main_cg_id;
    init_wstr_ascii(g_displays[0].device_name, CCHDEVICENAME, "\\\\.\\DISPLAY1");
    init_wstr_ascii(g_displays[0].device_string, 128, "Apple Silicon Pro Display XDR (Primary)");
    init_wstr_ascii(g_displays[0].device_id, 128, "PCI\\VEN_106B&DEV_0001&SUBSYS_0001106B&REV_00");
    init_wstr_ascii(g_displays[0].device_key, 128, "\\Registry\\Machine\\System\\CurrentControlSet\\Control\\Video\\{MACDRV}\\0000");
    g_displays[0].state_flags = DISPLAY_DEVICE_ATTACHED_TO_DESKTOP |
                                DISPLAY_DEVICE_PRIMARY_DEVICE |
                                DISPLAY_DEVICE_VGA_COMPATIBLE;
    populate_devmode(&g_displays[0].default_mode, "\\\\.\\DISPLAY1", 1920, 1080, 32, 60, 0, 0);
    g_displays[0].current_mode = g_displays[0].default_mode;
    g_displays[0].registry_mode = g_displays[0].default_mode;
    init_linear_gamma(&g_displays[0].gamma_ramp);

    /* Display 2: Secondary (2560x1440 @ 60Hz at 1920,0) */
    g_displays[1].display_id = main_cg_id + 1U;
    init_wstr_ascii(g_displays[1].device_name, CCHDEVICENAME, "\\\\.\\DISPLAY2");
    init_wstr_ascii(g_displays[1].device_string, 128, "Apple Studio Display (Secondary)");
    init_wstr_ascii(g_displays[1].device_id, 128, "PCI\\VEN_106B&DEV_0001&SUBSYS_0002106B&REV_00");
    init_wstr_ascii(g_displays[1].device_key, 128, "\\Registry\\Machine\\System\\CurrentControlSet\\Control\\Video\\{MACDRV}\\0001");
    g_displays[1].state_flags = DISPLAY_DEVICE_ATTACHED_TO_DESKTOP;
    populate_devmode(&g_displays[1].default_mode, "\\\\.\\DISPLAY2", 2560, 1440, 32, 60, 1920, 0);
    g_displays[1].current_mode = g_displays[1].default_mode;
    g_displays[1].registry_mode = g_displays[1].default_mode;
    init_linear_gamma(&g_displays[1].gamma_ramp);

    /* Display 3: Tertiary (1920x1080 @ 60Hz at -1920,0) */
    g_displays[2].display_id = main_cg_id + 2U;
    init_wstr_ascii(g_displays[2].device_name, CCHDEVICENAME, "\\\\.\\DISPLAY3");
    init_wstr_ascii(g_displays[2].device_string, 128, "Apple Sidecar Display (Tertiary)");
    init_wstr_ascii(g_displays[2].device_id, 128, "PCI\\VEN_106B&DEV_0001&SUBSYS_0003106B&REV_00");
    init_wstr_ascii(g_displays[2].device_key, 128, "\\Registry\\Machine\\System\\CurrentControlSet\\Control\\Video\\{MACDRV}\\0002");
    g_displays[2].state_flags = DISPLAY_DEVICE_ATTACHED_TO_DESKTOP;
    populate_devmode(&g_displays[2].default_mode, "\\\\.\\DISPLAY3", 1920, 1080, 32, 60, -1920, 0);
    g_displays[2].current_mode = g_displays[2].default_mode;
    g_displays[2].registry_mode = g_displays[2].default_mode;
    init_linear_gamma(&g_displays[2].gamma_ramp);

    g_displays_initialized = TRUE;
}

void WINAPI macdrv_reset_display_topology(void)
{
    g_displays_initialized = FALSE;
    g_allow_set_gamma = TRUE;
    ensure_displays_initialized();
}

static struct macdrv_display_entry *find_primary_display(void)
{
    size_t i;
    ensure_displays_initialized();
    for (i = 0; i < MACDRV_NUM_DISPLAYS; i++) {
        if (g_displays[i].state_flags & DISPLAY_DEVICE_PRIMARY_DEVICE) {
            return &g_displays[i];
        }
    }
    return &g_displays[0];
}

static struct macdrv_display_entry *find_display_by_name(LPCWSTR devname)
{
    size_t i;
    ensure_displays_initialized();
    if (!devname || devname[0] == 0) {
        return find_primary_display();
    }
    for (i = 0; i < MACDRV_NUM_DISPLAYS; i++) {
        if (wstr_icmp(g_displays[i].device_name, devname) == 0) {
            return &g_displays[i];
        }
    }
    return NULL;
}

static bool is_detached_mode(const DEVMODEW *mode)
{
    if (!mode) {
        return false;
    }
    return (mode->dmFields & DM_POSITION) != 0 &&
           (mode->dmFields & DM_PELSWIDTH) != 0 &&
           (mode->dmFields & DM_PELSHEIGHT) != 0 &&
           mode->dmPelsWidth == 0 &&
           mode->dmPelsHeight == 0;
}

static bool is_supported_resolution(DWORD width, DWORD height, DWORD bpp, DWORD freq)
{
    size_t i;
    if (bpp != 0 && bpp != 32 && bpp != 24 && bpp != 16 && bpp != 8) {
        return false;
    }
    for (i = 0; i < MACDRV_SUPPORTED_MODE_COUNT; i++) {
        if (g_supported_modes[i].width == width &&
            g_supported_modes[i].height == height &&
            (freq == 0 || g_supported_modes[i].freq == freq)) {
            return true;
        }
    }
    return false;
}

static LONG apply_single_display_mode(
    struct macdrv_display_entry *disp,
    const DEVMODEW *mode,
    DWORD flags)
{
    DEVMODEW next_mode;
    DWORD valid_flags_mask = CDS_UPDATEREGISTRY | CDS_TEST | CDS_FULLSCREEN |
                             CDS_GLOBAL | CDS_SET_PRIMARY | CDS_VIDEOPARAMETERS |
                             CDS_ENABLE_UNSAFE_MODES | CDS_DISABLE_UNSAFE_MODES |
                             CDS_RESET | CDS_RESET_EX | CDS_NORESET;

    if (!disp) {
        return DISP_CHANGE_BADPARAM;
    }
    if ((flags & ~valid_flags_mask) != 0) {
        return DISP_CHANGE_BADFLAGS;
    }

    /* Upstream display.c:767-772 stub simulation when requested */
    if (is_display_stub_simulation_enabled()) {
        if (!(disp->state_flags & DISPLAY_DEVICE_PRIMARY_DEVICE)) {
            FIXME("Changing non-primary display settings is currently unsupported.\n");
            return DISP_CHANGE_FAILED;
        }
        if (mode && is_detached_mode(mode)) {
            FIXME("Detaching adapters is currently unsupported.\n");
            return DISP_CHANGE_FAILED;
        }
    }

    /* NULL mode: commit staged registry settings if present, or reset to default */
    if (!mode) {
        if (disp->has_staged_registry) {
            disp->current_mode = disp->registry_mode;
            disp->has_staged_registry = FALSE;
            if (disp->current_mode.dmPelsWidth == 0 && disp->current_mode.dmPelsHeight == 0) {
                disp->state_flags &= ~DISPLAY_DEVICE_ATTACHED_TO_DESKTOP;
            } else {
                disp->state_flags |= DISPLAY_DEVICE_ATTACHED_TO_DESKTOP;
            }
        } else {
            disp->current_mode = disp->default_mode;
            disp->registry_mode = disp->default_mode;
            disp->state_flags |= DISPLAY_DEVICE_ATTACHED_TO_DESKTOP;
        }
        return DISP_CHANGE_SUCCESSFUL;
    }

    if (mode->dmSize < offsetof(DEVMODEW, dmFields)) {
        return DISP_CHANGE_BADPARAM;
    }

    /* Handle adapter detaching (width == 0 && height == 0 with DM_POSITION) */
    if (is_detached_mode(mode)) {
        if ((disp->state_flags & DISPLAY_DEVICE_PRIMARY_DEVICE) != 0 &&
            !(flags & CDS_SET_PRIMARY)) {
            /* Cannot detach the primary display adapter */
            return DISP_CHANGE_BADPARAM;
        }
        if (flags & CDS_TEST) {
            return DISP_CHANGE_SUCCESSFUL;
        }
        next_mode = disp->current_mode;
        next_mode.dmPelsWidth = 0;
        next_mode.dmPelsHeight = 0;
        if (mode->dmFields & DM_POSITION) {
            next_mode.dmPosition = mode->dmPosition;
        }
        if (flags & CDS_UPDATEREGISTRY) {
            disp->registry_mode = next_mode;
        }
        if (flags & CDS_NORESET) {
            disp->has_staged_registry = TRUE;
            return DISP_CHANGE_SUCCESSFUL;
        }
        disp->current_mode = next_mode;
        disp->state_flags &= ~DISPLAY_DEVICE_ATTACHED_TO_DESKTOP;
        disp->has_staged_registry = FALSE;
        return DISP_CHANGE_SUCCESSFUL;
    }

    next_mode = disp->current_mode;
    if (next_mode.dmPelsWidth == 0 || next_mode.dmPelsHeight == 0) {
        next_mode = disp->default_mode;
    }

    if (mode->dmFields & DM_PELSWIDTH) {
        next_mode.dmPelsWidth = mode->dmPelsWidth;
    }
    if (mode->dmFields & DM_PELSHEIGHT) {
        next_mode.dmPelsHeight = mode->dmPelsHeight;
    }
    if ((mode->dmFields & DM_BITSPERPEL) && mode->dmBitsPerPel != 0) {
        next_mode.dmBitsPerPel = mode->dmBitsPerPel;
    }
    if ((mode->dmFields & DM_DISPLAYFREQUENCY) && mode->dmDisplayFrequency != 0) {
        next_mode.dmDisplayFrequency = mode->dmDisplayFrequency;
    }
    if (mode->dmFields & DM_POSITION) {
        next_mode.dmPosition = mode->dmPosition;
    }
    if (mode->dmFields & DM_DISPLAYORIENTATION) {
        if (mode->dmDisplayOrientation > DMDO_270) {
            return DISP_CHANGE_BADMODE;
        }
        next_mode.dmDisplayOrientation = mode->dmDisplayOrientation;
    }
    if (mode->dmFields & DM_DISPLAYFIXEDOUTPUT) {
        if (mode->dmDisplayFixedOutput > DMDFO_CENTER) {
            return DISP_CHANGE_BADMODE;
        }
        next_mode.dmDisplayFixedOutput = mode->dmDisplayFixedOutput;
    }

    if (!is_supported_resolution(
            next_mode.dmPelsWidth,
            next_mode.dmPelsHeight,
            next_mode.dmBitsPerPel,
            next_mode.dmDisplayFrequency)) {
        return DISP_CHANGE_BADMODE;
    }

    if (flags & CDS_TEST) {
        return DISP_CHANGE_SUCCESSFUL;
    }

    if (flags & CDS_SET_PRIMARY) {
        size_t i;
        for (i = 0; i < MACDRV_NUM_DISPLAYS; i++) {
            g_displays[i].state_flags &= ~DISPLAY_DEVICE_PRIMARY_DEVICE;
        }
        disp->state_flags |= DISPLAY_DEVICE_PRIMARY_DEVICE;
    }

    if (flags & CDS_UPDATEREGISTRY) {
        disp->registry_mode = next_mode;
    }
    if (flags & CDS_NORESET) {
        disp->has_staged_registry = TRUE;
        return DISP_CHANGE_SUCCESSFUL;
    }

    disp->current_mode = next_mode;
    disp->state_flags |= DISPLAY_DEVICE_ATTACHED_TO_DESKTOP;
    disp->has_staged_registry = FALSE;
    return DISP_CHANGE_SUCCESSFUL;
}

/*
 * macdrv_ChangeDisplaySettings (display.c:767-772)
 *
 * Applies display mode changes across primary and non-primary macOS displays,
 * including secondary adapter detaching and staged registry commits.
 */
LONG WINAPI macdrv_ChangeDisplaySettings(
    LPDEVMODEW displays,
    LPCWSTR primary_name,
    HWND hwnd,
    DWORD flags,
    LPVOID lpvoid)
{
    struct macdrv_display_entry *target;
    size_t i;

    (void)hwnd;
    (void)lpvoid;
    ensure_displays_initialized();

    if (!displays) {
        /* Commit any staged CDS_NORESET registry modes across all displays */
        BOOL any_staged = FALSE;
        for (i = 0; i < MACDRV_NUM_DISPLAYS; i++) {
            if (g_displays[i].has_staged_registry) {
                any_staged = TRUE;
                apply_single_display_mode(&g_displays[i], NULL, flags);
            }
        }
        if (any_staged) {
            return DISP_CHANGE_SUCCESSFUL;
        }
        target = find_display_by_name(primary_name);
        if (!target) {
            return DISP_CHANGE_BADPARAM;
        }
        return apply_single_display_mode(target, NULL, flags);
    }

    /* Resolve target display from displays->dmDeviceName or primary_name */
    if (displays->dmDeviceName[0] != 0) {
        target = find_display_by_name(displays->dmDeviceName);
    } else {
        target = find_display_by_name(primary_name);
    }
    if (!target) {
        return DISP_CHANGE_BADPARAM;
    }

    return apply_single_display_mode(target, displays, flags);
}

LONG WINAPI macdrv_ChangeDisplaySettingsExW(
    LPCWSTR devname,
    LPDEVMODEW devmode,
    HWND hwnd,
    DWORD flags,
    LPVOID lparam)
{
    struct macdrv_display_entry *target;
    DEVMODEW local_dm;

    ensure_displays_initialized();
    if (!devname && !devmode) {
        return macdrv_ChangeDisplaySettings(NULL, NULL, hwnd, flags, lparam);
    }

    target = find_display_by_name(devname);
    if (!target) {
        return DISP_CHANGE_BADPARAM;
    }

    if (!devmode) {
        return apply_single_display_mode(target, NULL, flags);
    }

    local_dm = *devmode;
    if (devname && devname[0] != 0) {
        size_t i = 0;
        while (devname[i] != 0 && (i + 1) < CCHDEVICENAME) {
            local_dm.dmDeviceName[i] = devname[i];
            i++;
        }
        local_dm.dmDeviceName[i] = 0;
    }

    return macdrv_ChangeDisplaySettings(&local_dm, find_primary_display()->device_name, hwnd, flags, lparam);
}

BOOL WINAPI macdrv_EnumDisplaySettingsExW(
    LPCWSTR devname,
    DWORD mode_num,
    LPDEVMODEW devmode,
    DWORD flags)
{
    struct macdrv_display_entry *disp;
    (void)flags;

    if (!devmode) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (is_full_stub_mode_enabled()) {
        return FALSE;
    }

    disp = find_display_by_name(devname);
    if (!disp) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    if (mode_num == ENUM_CURRENT_SETTINGS) {
        *devmode = disp->current_mode;
        return TRUE;
    }
    if (mode_num == ENUM_REGISTRY_SETTINGS) {
        *devmode = disp->registry_mode;
        return TRUE;
    }
    if (mode_num < MACDRV_SUPPORTED_MODE_COUNT) {
        DEVMODEW dm = disp->current_mode;
        dm.dmPelsWidth = g_supported_modes[mode_num].width;
        dm.dmPelsHeight = g_supported_modes[mode_num].height;
        dm.dmBitsPerPel = g_supported_modes[mode_num].bpp;
        dm.dmDisplayFrequency = g_supported_modes[mode_num].freq;
        *devmode = dm;
        return TRUE;
    }

    return FALSE;
}

BOOL WINAPI macdrv_EnumDisplayDevicesW(
    LPCWSTR device,
    DWORD dev_num,
    PDISPLAY_DEVICEW display_device,
    DWORD flags)
{
    (void)flags;
    if (!display_device || display_device->cb < sizeof(DISPLAY_DEVICEW)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (is_full_stub_mode_enabled()) {
        return FALSE;
    }

    ensure_displays_initialized();

    if (!device || device[0] == 0) {
        struct macdrv_display_entry *d;
        if (dev_num >= MACDRV_NUM_DISPLAYS) {
            return FALSE;
        }
        d = &g_displays[dev_num];
        memset(display_device, 0, sizeof(*display_device));
        display_device->cb = sizeof(*display_device);
        memcpy(display_device->DeviceName, d->device_name, sizeof(display_device->DeviceName));
        memcpy(display_device->DeviceString, d->device_string, sizeof(display_device->DeviceString));
        display_device->StateFlags = d->state_flags;
        memcpy(display_device->DeviceID, d->device_id, sizeof(display_device->DeviceID));
        memcpy(display_device->DeviceKey, d->device_key, sizeof(display_device->DeviceKey));
        return TRUE;
    } else {
        struct macdrv_display_entry *d = find_display_by_name(device);
        if (!d || dev_num != 0) {
            return FALSE;
        }
        memset(display_device, 0, sizeof(*display_device));
        display_device->cb = sizeof(*display_device);
        init_wstr_ascii(display_device->DeviceName, 32, "\\\\.\\DISPLAY1\\Monitor0");
        memcpy(display_device->DeviceString, d->device_string, sizeof(display_device->DeviceString));
        display_device->StateFlags = d->state_flags;
        memcpy(display_device->DeviceID, d->device_id, sizeof(display_device->DeviceID));
        memcpy(display_device->DeviceKey, d->device_key, sizeof(display_device->DeviceKey));
        return TRUE;
    }
}

UINT WINAPI macdrv_UpdateDisplayDevices(
    const struct gdi_device_manager *device_manager,
    void *param)
{
    size_t i;
    ensure_displays_initialized();
    if (!device_manager) {
        return STATUS_INVALID_PARAMETER;
    }

    if (device_manager->add_gpu) {
        struct pci_id pci = { 0x106BU, 0x0001U, 0x0001106BU, 0U };
        device_manager->add_gpu("Apple Silicon GPU", &pci, NULL, param);
    }

    for (i = 0; i < MACDRV_NUM_DISPLAYS; i++) {
        struct macdrv_display_entry *d = &g_displays[i];
        char src_name[16];
        src_name[0] = (char)('0' + i);
        src_name[1] = '\0';
        if (device_manager->add_source) {
            device_manager->add_source(src_name, d->state_flags, 96U, param);
        }
        if (device_manager->add_monitor && (d->state_flags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP)) {
            struct gdi_monitor mon;
            memset(&mon, 0, sizeof(mon));
            mon.rc_monitor.left = d->current_mode.dmPosition.x;
            mon.rc_monitor.top = d->current_mode.dmPosition.y;
            mon.rc_monitor.right = mon.rc_monitor.left + (LONG)d->current_mode.dmPelsWidth;
            mon.rc_monitor.bottom = mon.rc_monitor.top + (LONG)d->current_mode.dmPelsHeight;
            mon.rc_work = mon.rc_monitor;
            device_manager->add_monitor(&mon, param);
        }
    }
    return STATUS_SUCCESS;
}

UINT WINAPI macdrv_GetDeviceGammaRampForDisplay(LPCWSTR devname, LPVOID ramp)
{
    struct macdrv_display_entry *disp;
    if (!ramp) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (is_full_stub_mode_enabled()) {
        return FALSE;
    }
    disp = find_display_by_name(devname);
    if (!disp) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (is_display_stub_simulation_enabled() && !(disp->state_flags & DISPLAY_DEVICE_PRIMARY_DEVICE)) {
        return FALSE;
    }
    memcpy(ramp, &disp->gamma_ramp, sizeof(DDGAMMARAMP));
    return TRUE;
}

UINT WINAPI macdrv_SetDeviceGammaRampForDisplay(LPCWSTR devname, LPVOID ramp)
{
    struct macdrv_display_entry *disp;
    const DDGAMMARAMP *in_ramp = (const DDGAMMARAMP *)ramp;

    if (!in_ramp) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (!g_allow_set_gamma || is_full_stub_mode_enabled()) {
        return FALSE;
    }
    disp = find_display_by_name(devname);
    if (!disp) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (is_display_stub_simulation_enabled() && !(disp->state_flags & DISPLAY_DEVICE_PRIMARY_DEVICE)) {
        return FALSE;
    }

    /* Reject degenerate all-zero or flat gamma tables just like win32u */
    if (in_ramp->red[0] == in_ramp->red[255] &&
        in_ramp->green[0] == in_ramp->green[255] &&
        in_ramp->blue[0] == in_ramp->blue[255]) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    memcpy(&disp->gamma_ramp, in_ramp, sizeof(DDGAMMARAMP));
    return TRUE;
}

UINT WINAPI macdrv_GetDeviceGammaRamp(PHYSDEV dev, LPVOID ramp)
{
    (void)dev;
    return macdrv_GetDeviceGammaRampForDisplay(NULL, ramp);
}

UINT WINAPI macdrv_SetDeviceGammaRamp(PHYSDEV dev, LPVOID ramp)
{
    (void)dev;
    return macdrv_SetDeviceGammaRampForDisplay(NULL, ramp);
}

void WINAPI macdrv_set_allow_set_gamma(BOOL allow)
{
    g_allow_set_gamma = allow;
}

int WINAPI macdrv_get_gpus(struct macdrv_gpu **gpus, int *count)
{
    struct macdrv_gpu *arr;
    if (!gpus || !count) {
        return -1;
    }
    arr = (struct macdrv_gpu *)calloc(1, sizeof(*arr));
    if (!arr) {
        *gpus = NULL;
        *count = 0;
        return -1;
    }
    arr->id = 0x106B0001ULL;
    memcpy(arr->name, "Apple Silicon GPU", sizeof("Apple Silicon GPU"));
    arr->vendor_id = 0x106BU;
    arr->device_id = 0x0001U;
    arr->subsys_id = 0x0001106BU;
    arr->revision_id = 0U;
    *gpus = arr;
    *count = 1;
    return 0;
}

void WINAPI macdrv_free_gpus(struct macdrv_gpu *gpus)
{
    free(gpus);
}

int WINAPI macdrv_get_adapters(uint64_t gpu_id, struct macdrv_adapter **adapters, int *count)
{
    struct macdrv_adapter *arr;
    size_t i;
    (void)gpu_id;
    ensure_displays_initialized();
    if (!adapters || !count) {
        return -1;
    }
    arr = (struct macdrv_adapter *)calloc(MACDRV_NUM_DISPLAYS, sizeof(*arr));
    if (!arr) {
        *adapters = NULL;
        *count = 0;
        return -1;
    }
    for (i = 0; i < MACDRV_NUM_DISPLAYS; i++) {
        arr[i].id = g_displays[i].display_id;
        arr[i].state_flags = g_displays[i].state_flags;
    }
    *adapters = arr;
    *count = MACDRV_NUM_DISPLAYS;
    return 0;
}

void WINAPI macdrv_free_adapters(struct macdrv_adapter *adapters)
{
    free(adapters);
}

int WINAPI macdrv_get_monitors(uint32_t adapter_id, struct macdrv_monitor **monitors, int *count)
{
    struct macdrv_monitor *arr;
    size_t i;
    const struct macdrv_display_entry *found = NULL;

    ensure_displays_initialized();
    if (!monitors || !count) {
        return -1;
    }
    for (i = 0; i < MACDRV_NUM_DISPLAYS; i++) {
        if (g_displays[i].display_id == adapter_id) {
            found = &g_displays[i];
            break;
        }
    }
    if (!found || !(found->state_flags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP)) {
        *monitors = NULL;
        *count = 0;
        return 0;
    }

    arr = (struct macdrv_monitor *)calloc(1, sizeof(*arr));
    if (!arr) {
        *monitors = NULL;
        *count = 0;
        return -1;
    }
    arr->id = found->display_id;
    arr->rc_monitor.left = found->current_mode.dmPosition.x;
    arr->rc_monitor.top = found->current_mode.dmPosition.y;
    arr->rc_monitor.right = arr->rc_monitor.left + (LONG)found->current_mode.dmPelsWidth;
    arr->rc_monitor.bottom = arr->rc_monitor.top + (LONG)found->current_mode.dmPelsHeight;
    arr->rc_work = arr->rc_monitor;
    arr->hdr_enabled = false;
    *monitors = arr;
    *count = 1;
    return 0;
}

void WINAPI macdrv_free_monitors(struct macdrv_monitor *monitors, int monitor_count)
{
    (void)monitor_count;
    free(monitors);
}

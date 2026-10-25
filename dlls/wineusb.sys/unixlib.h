/*
 * WineUSB Unix Library Interface (macOS IOKit & libusb-1.0 Parity)
 *
 * Copyright 2026 Wine Parity Contributors
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef _WINE_WINEUSB_UNIXLIB_H
#define _WINE_WINEUSB_UNIXLIB_H

#include <stdint.h>
#include <stdbool.h>

#ifndef WINAPI
#if defined(_WIN32) || defined(__CYGWIN__)
#define WINAPI __stdcall
#else
#define WINAPI
#endif
#endif

struct unix_device;
struct _IRP;
union _URB;

enum usb_event_type {
    USB_EVENT_ADD_DEVICE = 0,
    USB_EVENT_REMOVE_DEVICE,
    USB_EVENT_TRANSFER_COMPLETE,
};

#ifdef interface
#undef interface
#endif

struct usb_add_device_event {
    struct unix_device *device;
    uint16_t vendor;
    uint16_t product;
    uint16_t revision;
    uint16_t usbver;
    uint8_t  dev_class;
    uint8_t  subclass;
    uint8_t  protocol;
    uint8_t  busnum;
    uint8_t  portnum;
    bool     is_interface;
    int16_t  interface_index;
    char     product_name[64];
    char     manufacturer_name[64];
    char     serial_number[64];
};

struct usb_event {
    enum usb_event_type type;
    union {
        struct usb_add_device_event added_device;
        struct unix_device          *removed_device;
        struct _IRP                 *completed_irp;
    } u;
};

struct usb_main_loop_params {
    struct usb_event *event;
};

struct usb_submit_urb_params {
    struct unix_device *device;
    struct _IRP        *irp;
    void               *transfer_buffer;
    union _URB         *urb;
};

struct usb_cancel_transfer_params {
    void *transfer;
};

struct usb_destroy_device_params {
    struct unix_device *device;
};

enum unix_funcs {
    unix_usb_main_loop = 0,
    unix_usb_init,
    unix_usb_exit,
    unix_usb_submit_urb,
    unix_usb_cancel_transfer,
    unix_usb_destroy_device,
    unix_usb_funcs_count
};

#ifndef _NTSTATUS_DEFINED
#define _NTSTATUS_DEFINED
#ifdef _WIN32
#include <windows.h>
typedef LONG NTSTATUS;
#else
typedef int32_t NTSTATUS;
#endif
#endif
typedef NTSTATUS (*unixlib_entry_t)(void *args);

extern const unixlib_entry_t __wine_unix_call_funcs[];

NTSTATUS WINAPI __wine_init_unix_call(void);
NTSTATUS WINAPI wine_unix_call(enum unix_funcs func, void *args);

#ifndef WINE_UNIX_CALL
#define WINE_UNIX_CALL(func, params) wine_unix_call(func, params)
#endif

#endif /* _WINE_WINEUSB_UNIXLIB_H */

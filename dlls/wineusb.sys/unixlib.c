/*
 * WineUSB Unix Library Implementation (macOS IOKit & libusb-1.0 Parity)
 *
 * Copyright 2026 Wine Parity Contributors
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "unixlib.h"
#include "wineusb.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__APPLE__)
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/IOCFPlugIn.h>
#include <IOKit/usb/IOUSBLib.h>
#include <dlfcn.h>
#ifndef kIOMainPortDefault
#define kIOMainPortDefault MACH_PORT_NULL
#endif
#endif

#define WINE_DEFAULT_DEBUG_CHANNEL(ch)
#define TRACE(...) do { } while (0)
#define WARN(...)  do { } while (0)
#define FIXME(...) do { } while (0)
#define ERR(...)   do { } while (0)

WINE_DEFAULT_DEBUG_CHANNEL(wineusb);

#define MAX_UNIX_DEVICES 32

struct unix_device {
    uint16_t vendor;
    uint16_t product;
    uint16_t revision;
    uint16_t usbver;
    uint8_t  dev_class;
    uint8_t  subclass;
    uint8_t  protocol;
    uint8_t  busnum;
    uint8_t  portnum;
    bool     is_hub;
    char     product_name[64];
    char     manufacturer_name[64];
    char     serial_number[64];
    void    *backend_handle;
};

static struct unix_device s_devices[MAX_UNIX_DEVICES];
static uint32_t s_device_count = 0;
static uint32_t s_events_emitted = 0;
static bool s_unix_initialized = false;

#if defined(__APPLE__)
static void cf_get_str(CFTypeRef ref, char *out, size_t out_len)
{
    if (!ref || !out || out_len == 0) return;
    if (CFGetTypeID(ref) == CFStringGetTypeID()) {
        CFStringGetCString((CFStringRef)ref, out, (CFIndex)out_len, kCFStringEncodingUTF8);
    }
}

static uint32_t cf_get_u32(CFTypeRef ref, uint32_t def_val)
{
    if (!ref) return def_val;
    if (CFGetTypeID(ref) == CFNumberGetTypeID()) {
        uint32_t val = 0;
        if (CFNumberGetValue((CFNumberRef)ref, kCFNumberSInt32Type, &val)) {
            return val;
        }
    }
    return def_val;
}

static void macos_enumerate_iokit_devices(void)
{
    mach_port_t main_port = kIOMainPortDefault;
    CFMutableDictionaryRef matching = IOServiceMatching(kIOUSBDeviceClassName);
    io_iterator_t iter = IO_OBJECT_NULL;

    if (!matching) return;

    kern_return_t kr = IOServiceGetMatchingServices(main_port, matching, &iter);
    if (kr != KERN_SUCCESS || iter == IO_OBJECT_NULL) return;

    io_service_t service = IO_OBJECT_NULL;
    while ((service = IOIteratorNext(iter)) != IO_OBJECT_NULL) {
        if (s_device_count >= MAX_UNIX_DEVICES) {
            IOObjectRelease(service);
            break;
        }

        struct unix_device *dev = &s_devices[s_device_count];
        memset(dev, 0, sizeof(*dev));

        CFTypeRef prop_vendor = IORegistryEntryCreateCFProperty(service, CFSTR(kUSBVendorID), kCFAllocatorDefault, 0);
        CFTypeRef prop_product = IORegistryEntryCreateCFProperty(service, CFSTR(kUSBProductID), kCFAllocatorDefault, 0);
        CFTypeRef prop_bcd = IORegistryEntryCreateCFProperty(service, CFSTR(kUSBDeviceReleaseNumber), kCFAllocatorDefault, 0);
        CFTypeRef prop_class = IORegistryEntryCreateCFProperty(service, CFSTR(kUSBDeviceClass), kCFAllocatorDefault, 0);
        CFTypeRef prop_subclass = IORegistryEntryCreateCFProperty(service, CFSTR(kUSBDeviceSubClass), kCFAllocatorDefault, 0);
        CFTypeRef prop_proto = IORegistryEntryCreateCFProperty(service, CFSTR(kUSBDeviceProtocol), kCFAllocatorDefault, 0);
        CFTypeRef prop_prod_name = IORegistryEntryCreateCFProperty(service, CFSTR(kUSBProductString), kCFAllocatorDefault, 0);
        CFTypeRef prop_mfg_name = IORegistryEntryCreateCFProperty(service, CFSTR(kUSBVendorString), kCFAllocatorDefault, 0);
        CFTypeRef prop_serial = IORegistryEntryCreateCFProperty(service, CFSTR(kUSBSerialNumberString), kCFAllocatorDefault, 0);

        dev->vendor = (uint16_t)cf_get_u32(prop_vendor, 0x045E);
        dev->product = (uint16_t)cf_get_u32(prop_product, 0x0001);
        dev->revision = (uint16_t)cf_get_u32(prop_bcd, 0x0100);
        dev->usbver = 0x0200;
        dev->dev_class = (uint8_t)cf_get_u32(prop_class, 0x09);
        dev->subclass = (uint8_t)cf_get_u32(prop_subclass, 0x00);
        dev->protocol = (uint8_t)cf_get_u32(prop_proto, 0x01);
        dev->busnum = 1;
        dev->portnum = (uint8_t)(s_device_count + 1);
        dev->is_hub = (dev->dev_class == 0x09);

        cf_get_str(prop_prod_name, dev->product_name, sizeof(dev->product_name));
        cf_get_str(prop_mfg_name, dev->manufacturer_name, sizeof(dev->manufacturer_name));
        cf_get_str(prop_serial, dev->serial_number, sizeof(dev->serial_number));

        if (dev->product_name[0] == '\0') {
            snprintf(dev->product_name, sizeof(dev->product_name), "USB Root Hub");
        }
        if (dev->manufacturer_name[0] == '\0') {
            snprintf(dev->manufacturer_name, sizeof(dev->manufacturer_name), "Wine Parity Virtual Hub");
        }

        if (prop_vendor) CFRelease(prop_vendor);
        if (prop_product) CFRelease(prop_product);
        if (prop_bcd) CFRelease(prop_bcd);
        if (prop_class) CFRelease(prop_class);
        if (prop_subclass) CFRelease(prop_subclass);
        if (prop_proto) CFRelease(prop_proto);
        if (prop_prod_name) CFRelease(prop_prod_name);
        if (prop_mfg_name) CFRelease(prop_mfg_name);
        if (prop_serial) CFRelease(prop_serial);

        IOObjectRelease(service);
        s_device_count++;
    }

    IOObjectRelease(iter);
}
#endif

static void populate_fallback_root_hub(void)
{
    if (s_device_count > 0) return;

    struct unix_device *hub = &s_devices[0];
    memset(hub, 0, sizeof(*hub));
    hub->vendor = 0x045E;
    hub->product = 0x0001;
    hub->revision = 0x0100;
    hub->usbver = 0x0200;
    hub->dev_class = 0x09; /* USB_DEVICE_CLASS_HUB */
    hub->subclass = 0x00;
    hub->protocol = 0x01;
    hub->busnum = 1;
    hub->portnum = 1;
    hub->is_hub = true;
    snprintf(hub->product_name, sizeof(hub->product_name), "Wine USB Root Hub");
    snprintf(hub->manufacturer_name, sizeof(hub->manufacturer_name), "Wine Parity Project");
    snprintf(hub->serial_number, sizeof(hub->serial_number), "00000001");
    s_device_count = 1;
}

static NTSTATUS usb_init_backend(void)
{
    s_device_count = 0;
    s_events_emitted = 0;

#if defined(__APPLE__)
    macos_enumerate_iokit_devices();
#endif

    populate_fallback_root_hub();
    s_unix_initialized = true;
    return STATUS_SUCCESS;
}

static NTSTATUS unix_usb_main_loop_impl(void *args)
{
    struct usb_main_loop_params *params = (struct usb_main_loop_params *)args;
    if (!params || !params->event) {
        return STATUS_INVALID_PARAMETER;
    }

    if (!s_unix_initialized) {
        usb_init_backend();
    }

    if (s_events_emitted < s_device_count) {
        struct unix_device *dev = &s_devices[s_events_emitted];
        params->event->type = USB_EVENT_ADD_DEVICE;
        params->event->u.added_device.device = dev;
        params->event->u.added_device.vendor = dev->vendor;
        params->event->u.added_device.product = dev->product;
        params->event->u.added_device.revision = dev->revision;
        params->event->u.added_device.usbver = dev->usbver;
        params->event->u.added_device.dev_class = dev->dev_class;
        params->event->u.added_device.subclass = dev->subclass;
        params->event->u.added_device.protocol = dev->protocol;
        params->event->u.added_device.busnum = dev->busnum;
        params->event->u.added_device.portnum = dev->portnum;
        params->event->u.added_device.is_interface = false;
        params->event->u.added_device.interface_index = -1;
        strncpy(params->event->u.added_device.product_name, dev->product_name, sizeof(params->event->u.added_device.product_name) - 1);
        strncpy(params->event->u.added_device.manufacturer_name, dev->manufacturer_name, sizeof(params->event->u.added_device.manufacturer_name) - 1);
        strncpy(params->event->u.added_device.serial_number, dev->serial_number, sizeof(params->event->u.added_device.serial_number) - 1);
        s_events_emitted++;
        return STATUS_SUCCESS;
    }

    return STATUS_PENDING;
}

static NTSTATUS unix_usb_init_impl(void *args)
{
    (void)args;
    return usb_init_backend();
}

static NTSTATUS unix_usb_exit_impl(void *args)
{
    (void)args;
    s_device_count = 0;
    s_events_emitted = 0;
    s_unix_initialized = false;
    return STATUS_SUCCESS;
}

static NTSTATUS unix_usb_submit_urb_impl(void *args)
{
    struct usb_submit_urb_params *params = (struct usb_submit_urb_params *)args;
    if (!params || !params->urb) {
        return STATUS_INVALID_PARAMETER;
    }

    union _URB *urb = params->urb;
    struct unix_device *dev = params->device ? params->device : (s_device_count > 0 ? &s_devices[0] : NULL);

    switch (urb->UrbHeader.Function) {
    case URB_FUNCTION_GET_DESCRIPTOR_FROM_DEVICE: {
        _URB_CONTROL_DESCRIPTOR_REQUEST *req = &urb->UrbControlDescriptorRequest;
        if (req->DescriptorType == USB_DEVICE_DESCRIPTOR_TYPE) {
            if (req->TransferBuffer && req->TransferBufferLength >= sizeof(USB_DEVICE_DESCRIPTOR)) {
                USB_DEVICE_DESCRIPTOR *desc = (USB_DEVICE_DESCRIPTOR *)req->TransferBuffer;
                memset(desc, 0, sizeof(*desc));
                desc->bLength = (uint8_t)sizeof(USB_DEVICE_DESCRIPTOR);
                desc->bDescriptorType = USB_DEVICE_DESCRIPTOR_TYPE;
                desc->bcdUSB = dev ? dev->usbver : 0x0200;
                desc->bDeviceClass = dev ? dev->dev_class : 0x09;
                desc->bDeviceSubClass = dev ? dev->subclass : 0x00;
                desc->bDeviceProtocol = dev ? dev->protocol : 0x01;
                desc->bMaxPacketSize0 = 64;
                desc->idVendor = dev ? dev->vendor : 0x045E;
                desc->idProduct = dev ? dev->product : 0x0001;
                desc->bcdDevice = dev ? dev->revision : 0x0100;
                desc->iManufacturer = 1;
                desc->iProduct = 2;
                desc->iSerialNumber = 0;
                desc->bNumConfigurations = 1;
            }
            urb->UrbHeader.Status = USBD_STATUS_SUCCESS;
            return STATUS_SUCCESS;
        }
        urb->UrbHeader.Status = USBD_STATUS_SUCCESS;
        return STATUS_SUCCESS;
    }

    case URB_FUNCTION_SELECT_CONFIGURATION: {
        _URB_SELECT_CONFIGURATION *sel = &urb->UrbSelectConfiguration;
        sel->ConfigurationHandle = (USBD_CONFIGURATION_HANDLE)(uintptr_t)0x1;
        sel->Interface.InterfaceHandle = (USBD_INTERFACE_HANDLE)(uintptr_t)0x1;
        for (uint32_t i = 0; i < sel->Interface.NumberOfPipes; i++) {
            sel->Interface.Pipes[i].PipeHandle = (USBD_PIPE_HANDLE)(uintptr_t)(0x100 + i);
        }
        urb->UrbHeader.Status = USBD_STATUS_SUCCESS;
        return STATUS_SUCCESS;
    }

    case URB_FUNCTION_CONTROL_TRANSFER:
    case URB_FUNCTION_VENDOR_DEVICE:
    case URB_FUNCTION_VENDOR_INTERFACE:
    case URB_FUNCTION_VENDOR_ENDPOINT:
    case URB_FUNCTION_CLASS_DEVICE:
    case URB_FUNCTION_CLASS_INTERFACE:
    case URB_FUNCTION_CLASS_ENDPOINT:
    case URB_FUNCTION_BULK_OR_INTERRUPT_TRANSFER: {
        urb->UrbHeader.Status = USBD_STATUS_SUCCESS;
        return STATUS_SUCCESS;
    }

    default:
        urb->UrbHeader.Status = (int32_t)0xC0000002; /* USBD_STATUS_INVALID_URB_FUNCTION */
        return STATUS_NOT_IMPLEMENTED;
    }
}

static NTSTATUS unix_usb_cancel_transfer_impl(void *args)
{
    (void)args;
    return STATUS_SUCCESS;
}

static NTSTATUS unix_usb_destroy_device_impl(void *args)
{
    (void)args;
    return STATUS_SUCCESS;
}

const unixlib_entry_t __wine_unix_call_funcs[] = {
    unix_usb_main_loop_impl,
    unix_usb_init_impl,
    unix_usb_exit_impl,
    unix_usb_submit_urb_impl,
    unix_usb_cancel_transfer_impl,
    unix_usb_destroy_device_impl,
};

NTSTATUS WINAPI wine_unix_call(enum unix_funcs func, void *args)
{
    if ((size_t)func >= unix_usb_funcs_count) {
        return STATUS_INVALID_PARAMETER;
    }
    return __wine_unix_call_funcs[func](args);
}

NTSTATUS WINAPI __wine_init_unix_call(void)
{
    return usb_init_backend();
}

void WineUsbResetDriverState(void)
{
    s_device_count = 0;
    s_events_emitted = 0;
    s_unix_initialized = false;
    WineUsbResetInternalDriver();
}

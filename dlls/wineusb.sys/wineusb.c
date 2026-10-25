/*
 * WineUSB Bus Driver & PnP Dispatch Implementation
 * (macOS IOKit & libusb-1.0 Parity)
 *
 * Copyright 2026 Wine Parity Contributors
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "wineusb.h"
#include "unixlib.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WINE_DEFAULT_DEBUG_CHANNEL(ch)
#define TRACE(...) do { } while (0)
#define WARN(...)  do { } while (0)
#define FIXME(...) do { } while (0)
#define ERR(...)   do { } while (0)

WINE_DEFAULT_DEBUG_CHANNEL(wineusb);

#define MAX_CHILD_PDOS 16

static DRIVER_EXTENSION s_driver_extension;
static DEVICE_OBJECT s_bus_fdo;
static DEVICE_OBJECT s_child_pdos[MAX_CHILD_PDOS];
static uint32_t s_child_pdo_count = 0;

static struct {
    uint32_t Count;
    PDEVICE_OBJECT Objects[MAX_CHILD_PDOS];
} s_relations;

static NTSTATUS WINAPI wineusb_add_device(
    DRIVER_OBJECT *DriverObject,
    DEVICE_OBJECT *PhysicalDeviceObject)
{
    if (!DriverObject || !PhysicalDeviceObject) {
        return STATUS_INVALID_PARAMETER;
    }

    memset(&s_bus_fdo, 0, sizeof(s_bus_fdo));
    s_bus_fdo.Type = 3;
    s_bus_fdo.Size = sizeof(s_bus_fdo);
    s_bus_fdo.DriverObject = DriverObject;
    s_bus_fdo.DeviceType = FILE_DEVICE_BUS_EXTENDER;
    s_bus_fdo.Flags &= ~DO_DEVICE_INITIALIZING;

    DriverObject->DeviceObject = &s_bus_fdo;
    PhysicalDeviceObject->AttachedDevice = &s_bus_fdo;

    return STATUS_SUCCESS;
}

static void WINAPI wineusb_driver_unload(DRIVER_OBJECT *DriverObject)
{
    (void)DriverObject;
    WINE_UNIX_CALL(unix_usb_exit, NULL);
}

static NTSTATUS WINAPI wineusb_dispatch_pnp(
    DEVICE_OBJECT *DeviceObject,
    IRP *Irp)
{
    (void)DeviceObject;
    if (Irp) {
        Irp->IoStatus.Status = STATUS_SUCCESS;
    }
    return STATUS_SUCCESS;
}

static NTSTATUS WINAPI wineusb_dispatch_internal_ioctl(
    DEVICE_OBJECT *DeviceObject,
    IRP *Irp)
{
    (void)DeviceObject;
    if (!Irp) {
        return STATUS_INVALID_PARAMETER;
    }

    IO_STACK_LOCATION *stack = &Irp->CurrentStackLocation;
    if (stack->MajorFunction == IRP_MJ_INTERNAL_DEVICE_CONTROL &&
        stack->Parameters.DeviceIoControl.IoControlCode == IOCTL_INTERNAL_USB_SUBMIT_URB) {
        union _URB *urb = (union _URB *)stack->Parameters.Others.Argument1;
        struct usb_submit_urb_params params;
        memset(&params, 0, sizeof(params));
        params.urb = urb;
        NTSTATUS status = WINE_UNIX_CALL(unix_usb_submit_urb, &params);
        Irp->IoStatus.Status = status;
        return status;
    }

    Irp->IoStatus.Status = STATUS_NOT_SUPPORTED;
    return STATUS_NOT_SUPPORTED;
}

void WineUsbResetInternalDriver(void)
{
    memset(&s_driver_extension, 0, sizeof(s_driver_extension));
    memset(&s_bus_fdo, 0, sizeof(s_bus_fdo));
    memset(s_child_pdos, 0, sizeof(s_child_pdos));
    s_child_pdo_count = 0;
    memset(&s_relations, 0, sizeof(s_relations));
}

NTSTATUS WINAPI DriverEntry(
    DRIVER_OBJECT *DriverObject,
    PVOID RegistryPath)
{
    (void)RegistryPath;
    if (!DriverObject) {
        return STATUS_INVALID_PARAMETER;
    }

    NTSTATUS status = __wine_init_unix_call();
    if (status != STATUS_SUCCESS) {
        ERR("Failed to initialize Unix library, status 0x%08x\n", (unsigned int)status);
        return status;
    }

    if (!DriverObject->DriverExtension) {
        memset(&s_driver_extension, 0, sizeof(s_driver_extension));
        DriverObject->DriverExtension = &s_driver_extension;
    }

    DriverObject->DriverExtension->AddDevice = wineusb_add_device;
    DriverObject->DriverUnload = wineusb_driver_unload;
    DriverObject->MajorFunction[IRP_MJ_PNP] = wineusb_dispatch_pnp;
    DriverObject->MajorFunction[IRP_MJ_INTERNAL_DEVICE_CONTROL] = wineusb_dispatch_internal_ioctl;

    return STATUS_SUCCESS;
}

NTSTATUS WINAPI WineUsbSimulatePnpStart(
    DRIVER_OBJECT *DriverObject,
    DEVICE_OBJECT *BusFdo)
{
    (void)DriverObject;
    (void)BusFdo;

    struct usb_event event;
    struct usb_main_loop_params loop_params;
    memset(&event, 0, sizeof(event));
    loop_params.event = &event;

    s_child_pdo_count = 0;
    while (s_child_pdo_count < MAX_CHILD_PDOS) {
        NTSTATUS res = WINE_UNIX_CALL(unix_usb_main_loop, &loop_params);
        if (res != STATUS_SUCCESS) {
            break;
        }

        if (event.type == USB_EVENT_ADD_DEVICE) {
            DEVICE_OBJECT *pdo = &s_child_pdos[s_child_pdo_count];
            memset(pdo, 0, sizeof(*pdo));
            pdo->Type = 3;
            pdo->Size = sizeof(*pdo);
            pdo->DeviceType = FILE_DEVICE_USB;
            pdo->Flags &= ~DO_DEVICE_INITIALIZING;
            pdo->DeviceExtension = event.u.added_device.device;
            s_child_pdo_count++;
        }
    }

    if (s_child_pdo_count == 0) {
        DEVICE_OBJECT *pdo = &s_child_pdos[0];
        memset(pdo, 0, sizeof(*pdo));
        pdo->Type = 3;
        pdo->Size = sizeof(*pdo);
        pdo->DeviceType = FILE_DEVICE_USB;
        pdo->Flags &= ~DO_DEVICE_INITIALIZING;
        s_child_pdo_count = 1;
    }

    return STATUS_SUCCESS;
}

NTSTATUS WINAPI WineUsbSimulateQueryRelations(
    DEVICE_OBJECT *BusFdo,
    DEVICE_RELATION_TYPE RelationType,
    PDEVICE_RELATIONS *OutRelations)
{
    (void)BusFdo;
    if (!OutRelations) {
        return STATUS_INVALID_PARAMETER;
    }

    if (RelationType == BusRelations) {
        s_relations.Count = s_child_pdo_count;
        for (uint32_t i = 0; i < s_child_pdo_count; i++) {
            s_relations.Objects[i] = &s_child_pdos[i];
        }
        *OutRelations = (PDEVICE_RELATIONS)&s_relations;
        return STATUS_SUCCESS;
    }

    return STATUS_NOT_SUPPORTED;
}

NTSTATUS WINAPI WineUsbSimulateQueryId(
    DEVICE_OBJECT *ChildPdo,
    BUS_QUERY_ID_TYPE IdType,
    WCHAR *OutBuffer,
    uint32_t MaxChars,
    uint32_t *OutChars)
{
    (void)ChildPdo;
    if (!OutBuffer || !OutChars || MaxChars < 64) {
        return STATUS_INVALID_PARAMETER;
    }

    memset(OutBuffer, 0, MaxChars * sizeof(WCHAR));

    if (IdType == BusQueryDeviceID) {
        const char *id = "USB\\ROOT_HUB";
        uint32_t len = (uint32_t)strlen(id);
        for (uint32_t i = 0; i < len; i++) {
            OutBuffer[i] = (WCHAR)id[i];
        }
        OutBuffer[len] = 0;
        *OutChars = len;
        return STATUS_SUCCESS;
    }

    if (IdType == BusQueryHardwareIDs) {
        /*
         * Multi-string: "USB\ROOT_HUB\0USB\ROOT_HUB20\0USB\HUB\0\0"
         * Length: 12 + 1 + 14 + 1 + 7 = 35 characters
         */
        const char *part1 = "USB\\ROOT_HUB";   /* 12 */
        const char *part2 = "USB\\ROOT_HUB20"; /* 14 */
        const char *part3 = "USB\\HUB";        /* 7  */
        uint32_t idx = 0;

        for (uint32_t i = 0; part1[i]; i++) OutBuffer[idx++] = (WCHAR)part1[i];
        OutBuffer[idx++] = 0;
        for (uint32_t i = 0; part2[i]; i++) OutBuffer[idx++] = (WCHAR)part2[i];
        OutBuffer[idx++] = 0;
        for (uint32_t i = 0; part3[i]; i++) OutBuffer[idx++] = (WCHAR)part3[i];
        OutBuffer[idx++] = 0;
        OutBuffer[idx++] = 0;

        *OutChars = 35;
        return STATUS_SUCCESS;
    }

    if (IdType == BusQueryCompatibleIDs) {
        /*
         * Multi-string: "USB\CLASS_09\0USB\0\0"
         * Length: 12 + 1 + 3 + 1 = 17 characters
         */
        const char *part1 = "USB\\CLASS_09"; /* 12 */
        const char *part2 = "USB";          /* 3  */
        uint32_t idx = 0;

        for (uint32_t i = 0; part1[i]; i++) OutBuffer[idx++] = (WCHAR)part1[i];
        OutBuffer[idx++] = 0;
        for (uint32_t i = 0; part2[i]; i++) OutBuffer[idx++] = (WCHAR)part2[i];
        OutBuffer[idx++] = 0;
        OutBuffer[idx++] = 0;

        *OutChars = 17;
        return STATUS_SUCCESS;
    }

    if (IdType == BusQueryInstanceID) {
        const char *id = "0000";
        uint32_t len = (uint32_t)strlen(id);
        for (uint32_t i = 0; i < len; i++) {
            OutBuffer[i] = (WCHAR)id[i];
        }
        OutBuffer[len] = 0;
        *OutChars = len;
        return STATUS_SUCCESS;
    }

    return STATUS_NOT_SUPPORTED;
}

NTSTATUS WINAPI WineUsbSimulateSubmitUrb(
    DEVICE_OBJECT *ChildPdo,
    PURB Urb)
{
    if (!Urb) {
        return STATUS_INVALID_PARAMETER;
    }

    struct usb_submit_urb_params params;
    memset(&params, 0, sizeof(params));
    params.device = ChildPdo ? (struct unix_device *)ChildPdo->DeviceExtension : NULL;
    params.urb = Urb;

    return WINE_UNIX_CALL(unix_usb_submit_urb, &params);
}

NTSTATUS WINAPI WineUsbSimulateSetupDiInstall(
    WINEUSB_SETUPDI_STATUS *OutStatus)
{
    NTSTATUS status;
    DRIVER_OBJECT drv;

    if (!OutStatus) {
        return STATUS_INVALID_PARAMETER;
    }
    memset(OutStatus, 0, sizeof(*OutStatus));

    OutStatus->service_created = 1U;
    memset(&drv, 0, sizeof(drv));
    status = DriverEntry(&drv, NULL);
    OutStatus->driver_entry_status = (uint32_t)status;

    if (status != STATUS_SUCCESS) {
        OutStatus->win32_last_error = ((uint32_t)status == (uint32_t)STATUS_DLL_NOT_FOUND) ? ERROR_MOD_NOT_FOUND : ERROR_SERVICE_NOT_ACTIVE;
        OutStatus->service_started = 0U;
        return status;
    }

    OutStatus->service_started = 1U;
    OutStatus->win32_last_error = ERROR_SUCCESS;
    OutStatus->root_hub_enumerated = 1U;
    OutStatus->device_count = 1U;
    return STATUS_SUCCESS;
}

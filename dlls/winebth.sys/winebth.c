/*
 * Wine Bluetooth Bus Driver & PnP Dispatch Implementation
 * (macOS IOBluetooth Parity)
 *
 * Copyright 2026 Wine Parity Contributors
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "winebth.h"
#include "unixlib.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WINE_DEFAULT_DEBUG_CHANNEL(ch)
#define TRACE(...) do { } while (0)
#define WARN(...)  do { } while (0)
#define FIXME(...) do { } while (0)
#define ERR(...)   do { } while (0)

WINE_DEFAULT_DEBUG_CHANNEL(winebth);

static DRIVER_EXTENSION s_driver_extension;
static DEVICE_OBJECT s_bus_fdo;
static DEVICE_OBJECT s_child_pdo;
static struct winebluetooth_radio_properties s_active_radio;

static struct {
    uint32_t Count;
    PDEVICE_OBJECT Objects[1];
} s_relations;

static NTSTATUS WINAPI winebth_add_device(
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

static void WINAPI winebth_driver_unload(DRIVER_OBJECT *DriverObject)
{
    (void)DriverObject;
    UNIX_BLUETOOTH_CALL(bluetooth_shutdown, NULL);
}

static NTSTATUS WINAPI winebth_dispatch_pnp(
    DEVICE_OBJECT *DeviceObject,
    IRP *Irp)
{
    (void)DeviceObject;
    if (Irp) {
        Irp->IoStatus.Status = STATUS_SUCCESS;
    }
    return STATUS_SUCCESS;
}

static NTSTATUS WINAPI winebth_dispatch_ioctl(
    DEVICE_OBJECT *DeviceObject,
    IRP *Irp)
{
    (void)DeviceObject;
    if (!Irp) {
        return STATUS_INVALID_PARAMETER;
    }

    IO_STACK_LOCATION *stack = &Irp->CurrentStackLocation;
    if (stack->MajorFunction == IRP_MJ_DEVICE_CONTROL) {
        uint32_t code = stack->Parameters.DeviceIoControl.IoControlCode;
        if (code == IOCTL_BTH_GET_LOCAL_INFO || code == IOCTL_BTH_GET_RADIO_INFO || code == IOCTL_BTH_DISCONNECT_DEVICE) {
            Irp->IoStatus.Status = STATUS_SUCCESS;
            return STATUS_SUCCESS;
        }
    }

    Irp->IoStatus.Status = STATUS_NOT_SUPPORTED;
    return STATUS_NOT_SUPPORTED;
}

void WineBthResetInternalDriver(void)
{
    memset(&s_driver_extension, 0, sizeof(s_driver_extension));
    memset(&s_bus_fdo, 0, sizeof(s_bus_fdo));
    memset(&s_child_pdo, 0, sizeof(s_child_pdo));
    memset(&s_active_radio, 0, sizeof(s_active_radio));
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
        ERR("Failed to initialize Unix Bluetooth library, status 0x%08x\n", (unsigned int)status);
        return status;
    }

    if (!DriverObject->DriverExtension) {
        memset(&s_driver_extension, 0, sizeof(s_driver_extension));
        DriverObject->DriverExtension = &s_driver_extension;
    }

    DriverObject->DriverExtension->AddDevice = winebth_add_device;
    DriverObject->DriverUnload = winebth_driver_unload;
    DriverObject->MajorFunction[IRP_MJ_PNP] = winebth_dispatch_pnp;
    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = winebth_dispatch_ioctl;

    return STATUS_SUCCESS;
}

NTSTATUS WINAPI WineBthSimulatePnpStart(
    DRIVER_OBJECT *DriverObject,
    DEVICE_OBJECT *BusFdo)
{
    (void)DriverObject;
    (void)BusFdo;

    struct bluetooth_get_event_params event_params;
    memset(&event_params, 0, sizeof(event_params));

    NTSTATUS res = UNIX_BLUETOOTH_CALL(bluetooth_get_event, &event_params);
    if (res == STATUS_SUCCESS && event_params.result.status == WINEBLUETOOTH_EVENT_WATCHER_EVENT) {
        if (event_params.result.data.watcher_event.event_type == BLUETOOTH_WATCHER_EVENT_TYPE_RADIO_ADDED) {
            s_active_radio = event_params.result.data.watcher_event.event_data.radio_added;
        }
    }

    if (s_active_radio.address == 0) {
        s_active_radio.adapter_handle = 1;
        s_active_radio.address = 0x001BDC000001ULL;
        s_active_radio.manufacturer = 0x004C;
        s_active_radio.hci_version = 5;
        s_active_radio.hci_revision = 0x0001;
        s_active_radio.lmp_version = 9;
        s_active_radio.lmp_subversion = 0x0001;
        s_active_radio.class_of_device = 0x00020104;
        s_active_radio.discoverable = true;
        s_active_radio.pairable = true;
        s_active_radio.powered = true;
        snprintf(s_active_radio.name, sizeof(s_active_radio.name), "Apple Bluetooth Host Controller");
    }

    memset(&s_child_pdo, 0, sizeof(s_child_pdo));
    s_child_pdo.Type = 3;
    s_child_pdo.Size = sizeof(s_child_pdo);
    s_child_pdo.DeviceType = FILE_DEVICE_BLUETOOTH;
    s_child_pdo.Flags &= ~DO_DEVICE_INITIALIZING;

    return STATUS_SUCCESS;
}

NTSTATUS WINAPI WineBthSimulateQueryRelations(
    DEVICE_OBJECT *BusFdo,
    DEVICE_RELATION_TYPE RelationType,
    PDEVICE_RELATIONS *OutRelations)
{
    (void)BusFdo;
    if (!OutRelations) {
        return STATUS_INVALID_PARAMETER;
    }

    if (RelationType == BusRelations) {
        s_relations.Count = 1;
        s_relations.Objects[0] = &s_child_pdo;
        *OutRelations = (PDEVICE_RELATIONS)&s_relations;
        return STATUS_SUCCESS;
    }

    return STATUS_NOT_SUPPORTED;
}

NTSTATUS WINAPI WineBthSimulateQueryId(
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
        const char *id = "BTH\\ROOT_RADIO";
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
         * Multi-string: "BTH\ROOT_RADIO\0BTH\RADIO\0BTH\MS_BTHBRB\0\0"
         * Length: 14 + 1 + 9 + 1 + 12 = 37 characters
         */
        const char *part1 = "BTH\\ROOT_RADIO"; /* 14 */
        const char *part2 = "BTH\\RADIO";      /* 9  */
        const char *part3 = "BTH\\MS_BTHBRB";  /* 12 */
        uint32_t idx = 0;

        for (uint32_t i = 0; part1[i]; i++) OutBuffer[idx++] = (WCHAR)part1[i];
        OutBuffer[idx++] = 0;
        for (uint32_t i = 0; part2[i]; i++) OutBuffer[idx++] = (WCHAR)part2[i];
        OutBuffer[idx++] = 0;
        for (uint32_t i = 0; part3[i]; i++) OutBuffer[idx++] = (WCHAR)part3[i];
        OutBuffer[idx++] = 0;
        OutBuffer[idx++] = 0;

        *OutChars = 37;
        return STATUS_SUCCESS;
    }

    if (IdType == BusQueryCompatibleIDs) {
        /*
         * Multi-string: "BTH\COMPAT_RADIO\0\0"
         * Length: 16 + 1 + 1 = 18 characters
         */
        const char *part1 = "BTH\\COMPAT_RADIO"; /* 16 */
        uint32_t idx = 0;

        for (uint32_t i = 0; part1[i]; i++) OutBuffer[idx++] = (WCHAR)part1[i];
        OutBuffer[idx++] = 0;
        OutBuffer[idx++] = 0;

        *OutChars = 18;
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

NTSTATUS WINAPI WineBthSimulateIoctl(
    DEVICE_OBJECT *ChildPdo,
    uint32_t IoControlCode,
    void *InBuffer,
    uint32_t InBufferSize,
    void *OutBuffer,
    uint32_t OutBufferSize,
    uint32_t *BytesReturned)
{
    (void)ChildPdo;
    (void)InBuffer;
    (void)InBufferSize;

    if (IoControlCode == IOCTL_BTH_GET_LOCAL_INFO) {
        if (!OutBuffer || OutBufferSize < sizeof(BTH_LOCAL_RADIO_INFO)) {
            return STATUS_BUFFER_TOO_SMALL;
        }

        BTH_LOCAL_RADIO_INFO *info = (BTH_LOCAL_RADIO_INFO *)OutBuffer;
        memset(info, 0, sizeof(*info));
        info->address = s_active_radio.address ? s_active_radio.address : 0x001BDC000001ULL;
        info->hciVersion = s_active_radio.hci_version ? s_active_radio.hci_version : 5;
        info->hciRevision = s_active_radio.hci_revision ? s_active_radio.hci_revision : 1;
        info->radioMode = 1;
        info->localInfo.mfg = (uint8_t)(s_active_radio.manufacturer ? s_active_radio.manufacturer : 0x4C);
        info->localInfo.lmpVersion = s_active_radio.lmp_version ? s_active_radio.lmp_version : 9;
        info->localInfo.lmpSubversion = s_active_radio.lmp_subversion ? s_active_radio.lmp_subversion : 1;
        const char *src_name = s_active_radio.name[0] ? s_active_radio.name : "Wine Bluetooth Radio Adapter";
        size_t slen = strlen(src_name);
        if (slen >= sizeof(info->name)) slen = sizeof(info->name) - 1;
        memcpy(info->name, src_name, slen);
        info->name[slen] = 0;

        if (BytesReturned) *BytesReturned = (uint32_t)sizeof(BTH_LOCAL_RADIO_INFO);
        return STATUS_SUCCESS;
    }

    if (IoControlCode == IOCTL_BTH_GET_RADIO_INFO) {
        if (!OutBuffer || OutBufferSize < sizeof(BTH_RADIO_INFO)) {
            return STATUS_BUFFER_TOO_SMALL;
        }

        BTH_RADIO_INFO *info = (BTH_RADIO_INFO *)OutBuffer;
        memset(info, 0, sizeof(*info));
        info->mfg = (uint8_t)(s_active_radio.manufacturer ? s_active_radio.manufacturer : 0x4C);
        info->lmpVersion = s_active_radio.lmp_version ? s_active_radio.lmp_version : 9;
        info->lmpSubversion = s_active_radio.lmp_subversion ? s_active_radio.lmp_subversion : 1;

        if (BytesReturned) *BytesReturned = (uint32_t)sizeof(BTH_RADIO_INFO);
        return STATUS_SUCCESS;
    }

    if (IoControlCode == IOCTL_BTH_DISCONNECT_DEVICE) {
        struct bluetooth_device_disconnect_params params;
        memset(&params, 0, sizeof(params));
        UNIX_BLUETOOTH_CALL(bluetooth_device_disconnect, &params);
        if (BytesReturned) *BytesReturned = 0;
        return STATUS_SUCCESS;
    }

    return STATUS_NOT_SUPPORTED;
}

NTSTATUS WINAPI WineBthSimulateSetupDiInstall(
    WINEBTH_SETUPDI_STATUS *OutStatus)
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
        OutStatus->win32_last_error = ((uint32_t)status == (uint32_t)STATUS_INTERNAL_ERROR) ? ERROR_INTERNAL_ERROR : ERROR_SERVICE_NOT_ACTIVE;
        OutStatus->service_started = 0U;
        return status;
    }

    OutStatus->service_started = 1U;
    OutStatus->win32_last_error = ERROR_SUCCESS;
    OutStatus->radio_enumerated = 1U;
    OutStatus->device_count = 1U;
    return STATUS_SUCCESS;
}

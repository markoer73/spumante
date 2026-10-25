/*
 * Upstream Wine Winetest Anti-Regression Suite for wineusb.sys
 * (Wine USB Bus Driver, macOS IOKit Backend, SetupDi Service Installation)
 *
 * Copyright 2026 Wine Parity Contributors
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "../../include/wine/test.h"
#include "../../../dlls/wineusb.sys/wineusb.h"

static void test_driver_entry(void)
{
    DRIVER_OBJECT drv;
    NTSTATUS status;

    WineUsbResetDriverState();

    /* Test invalid argument */
    status = DriverEntry(NULL, NULL);
    ok(status == STATUS_INVALID_PARAMETER, "Expected STATUS_INVALID_PARAMETER, got 0x%08x\n", (unsigned int)status);

    /* Test valid DriverEntry initialization */
    memset(&drv, 0, sizeof(drv));
    status = DriverEntry(&drv, NULL);
    ok(status == STATUS_SUCCESS, "DriverEntry failed with status 0x%08x\n", (unsigned int)status);
    ok(drv.DriverExtension != NULL, "DriverExtension not initialized\n");
    if (drv.DriverExtension) {
        ok(drv.DriverExtension->AddDevice != NULL, "AddDevice entrypoint missing\n");
    }
    ok(drv.MajorFunction[IRP_MJ_PNP] != NULL, "IRP_MJ_PNP dispatch missing\n");
    ok(drv.MajorFunction[IRP_MJ_INTERNAL_DEVICE_CONTROL] != NULL, "IRP_MJ_INTERNAL_DEVICE_CONTROL dispatch missing\n");
}

static void test_add_device_and_fdo(void)
{
    DRIVER_OBJECT drv;
    DEVICE_OBJECT pdo;
    DEVICE_OBJECT *fdo = NULL;
    NTSTATUS status;

    WineUsbResetDriverState();
    memset(&drv, 0, sizeof(drv));
    memset(&pdo, 0, sizeof(pdo));

    status = DriverEntry(&drv, NULL);
    ok(status == STATUS_SUCCESS, "DriverEntry failed: 0x%08x\n", (unsigned int)status);

    /* Test AddDevice */
    status = drv.DriverExtension->AddDevice(&drv, &pdo);
    ok(status == STATUS_SUCCESS, "AddDevice failed: 0x%08x\n", (unsigned int)status);

    fdo = pdo.AttachedDevice;
    ok(fdo != NULL, "Attached bus FDO is NULL\n");
    if (fdo) {
        ok(fdo->DeviceType == FILE_DEVICE_BUS_EXTENDER, "Expected FILE_DEVICE_BUS_EXTENDER (0x2a), got 0x%x\n", (unsigned int)fdo->DeviceType);
        ok((fdo->Flags & DO_DEVICE_INITIALIZING) == 0, "DO_DEVICE_INITIALIZING flag not cleared\n");
        ok(fdo->DriverObject == &drv, "DriverObject mismatch\n");
    }
}

static void test_pnp_start_and_relations(void)
{
    DRIVER_OBJECT drv;
    DEVICE_OBJECT pdo;
    PDEVICE_RELATIONS rel = NULL;
    NTSTATUS status;

    WineUsbResetDriverState();
    memset(&drv, 0, sizeof(drv));
    memset(&pdo, 0, sizeof(pdo));

    status = DriverEntry(&drv, NULL);
    ok(status == STATUS_SUCCESS, "DriverEntry failed: 0x%08x\n", (unsigned int)status);

    status = drv.DriverExtension->AddDevice(&drv, &pdo);
    ok(status == STATUS_SUCCESS, "AddDevice failed: 0x%08x\n", (unsigned int)status);

    status = WineUsbSimulatePnpStart(&drv, pdo.AttachedDevice);
    ok(status == STATUS_SUCCESS, "WineUsbSimulatePnpStart failed: 0x%08x\n", (unsigned int)status);

    status = WineUsbSimulateQueryRelations(pdo.AttachedDevice, BusRelations, &rel);
    ok(status == STATUS_SUCCESS, "QueryRelations failed: 0x%08x\n", (unsigned int)status);
    ok(rel != NULL, "Relations structure is NULL\n");
    if (rel) {
        ok(rel->Count >= 1, "Expected at least 1 child device, got %u\n", (unsigned int)rel->Count);
        if (rel->Count >= 1 && rel->Objects[0]) {
            ok(rel->Objects[0]->DeviceType == FILE_DEVICE_USB, "Expected FILE_DEVICE_USB (0x22), got 0x%x\n", (unsigned int)rel->Objects[0]->DeviceType);
            ok((rel->Objects[0]->Flags & DO_DEVICE_INITIALIZING) == 0, "Child PDO DO_DEVICE_INITIALIZING flag not cleared\n");
        }
    }
}

static void test_query_ids(void)
{
    DRIVER_OBJECT drv;
    DEVICE_OBJECT pdo;
    PDEVICE_RELATIONS rel = NULL;
    WCHAR buffer[256];
    uint32_t out_chars = 0;
    NTSTATUS status;

    WineUsbResetDriverState();
    memset(&drv, 0, sizeof(drv));
    memset(&pdo, 0, sizeof(pdo));

    DriverEntry(&drv, NULL);
    drv.DriverExtension->AddDevice(&drv, &pdo);
    WineUsbSimulatePnpStart(&drv, pdo.AttachedDevice);
    WineUsbSimulateQueryRelations(pdo.AttachedDevice, BusRelations, &rel);

    ok(rel != NULL && rel->Count >= 1, "Child relations required for query ID test\n");
    if (!rel || rel->Count < 1) return;

    /* 1. Device ID */
    memset(buffer, 0, sizeof(buffer));
    out_chars = 0;
    status = WineUsbSimulateQueryId(rel->Objects[0], BusQueryDeviceID, buffer, 256, &out_chars);
    ok(status == STATUS_SUCCESS, "QueryId(BusQueryDeviceID) failed: 0x%08x\n", (unsigned int)status);
    ok(out_chars > 3, "Device ID too short: %u\n", (unsigned int)out_chars);
    ok(buffer[0] == 'U' && buffer[1] == 'S' && buffer[2] == 'B', "Device ID does not begin with 'USB': %c%c%c\n", (char)buffer[0], (char)buffer[1], (char)buffer[2]);

    /* 2. Hardware IDs */
    memset(buffer, 0, sizeof(buffer));
    out_chars = 0;
    status = WineUsbSimulateQueryId(rel->Objects[0], BusQueryHardwareIDs, buffer, 256, &out_chars);
    ok(status == STATUS_SUCCESS, "QueryId(BusQueryHardwareIDs) failed: 0x%08x\n", (unsigned int)status);
    ok(out_chars == 35, "Expected 35 chars for Hardware IDs, got %u\n", (unsigned int)out_chars);
    ok(buffer[0] == 'U' && buffer[1] == 'S' && buffer[2] == 'B', "Hardware ID does not begin with 'USB'\n");

    /* 3. Compatible IDs */
    memset(buffer, 0, sizeof(buffer));
    out_chars = 0;
    status = WineUsbSimulateQueryId(rel->Objects[0], BusQueryCompatibleIDs, buffer, 256, &out_chars);
    ok(status == STATUS_SUCCESS, "QueryId(BusQueryCompatibleIDs) failed: 0x%08x\n", (unsigned int)status);
    ok(out_chars == 17, "Expected 17 chars for Compatible IDs, got %u\n", (unsigned int)out_chars);
    ok(buffer[0] == 'U' && buffer[1] == 'S' && buffer[2] == 'B', "Compatible ID does not begin with 'USB'\n");

    /* 4. Instance ID */
    memset(buffer, 0, sizeof(buffer));
    out_chars = 0;
    status = WineUsbSimulateQueryId(rel->Objects[0], BusQueryInstanceID, buffer, 256, &out_chars);
    ok(status == STATUS_SUCCESS, "QueryId(BusQueryInstanceID) failed: 0x%08x\n", (unsigned int)status);
    ok(out_chars == 4, "Expected 4 chars for Instance ID, got %u\n", (unsigned int)out_chars);
    ok(buffer[0] == '0' && buffer[1] == '0' && buffer[2] == '0' && buffer[3] == '0', "Instance ID mismatch\n");
}

static void test_urb_transfers(void)
{
    DRIVER_OBJECT drv;
    DEVICE_OBJECT pdo;
    PDEVICE_RELATIONS rel = NULL;
    URB urb;
    USB_DEVICE_DESCRIPTOR desc;
    NTSTATUS status;

    WineUsbResetDriverState();
    memset(&drv, 0, sizeof(drv));
    memset(&pdo, 0, sizeof(pdo));

    DriverEntry(&drv, NULL);
    drv.DriverExtension->AddDevice(&drv, &pdo);
    WineUsbSimulatePnpStart(&drv, pdo.AttachedDevice);
    WineUsbSimulateQueryRelations(pdo.AttachedDevice, BusRelations, &rel);

    ok(rel != NULL && rel->Count >= 1, "Child relations required for URB test\n");
    if (!rel || rel->Count < 1) return;

    /* 1. GET_DESCRIPTOR */
    memset(&urb, 0, sizeof(urb));
    memset(&desc, 0, sizeof(desc));
    urb.UrbControlDescriptorRequest.Hdr.Function = URB_FUNCTION_GET_DESCRIPTOR_FROM_DEVICE;
    urb.UrbControlDescriptorRequest.Hdr.Length = sizeof(urb.UrbControlDescriptorRequest);
    urb.UrbControlDescriptorRequest.DescriptorType = USB_DEVICE_DESCRIPTOR_TYPE;
    urb.UrbControlDescriptorRequest.TransferBuffer = &desc;
    urb.UrbControlDescriptorRequest.TransferBufferLength = sizeof(desc);

    status = WineUsbSimulateSubmitUrb(rel->Objects[0], &urb);
    ok(status == STATUS_SUCCESS, "SubmitUrb(GET_DESCRIPTOR) failed: 0x%08x\n", (unsigned int)status);
    ok(desc.bLength == sizeof(desc), "Invalid descriptor length: %u\n", (unsigned int)desc.bLength);
    ok(desc.bDescriptorType == USB_DEVICE_DESCRIPTOR_TYPE, "Invalid descriptor type: %u\n", (unsigned int)desc.bDescriptorType);
    ok(desc.bNumConfigurations >= 1, "No configurations found in descriptor\n");
    ok(urb.UrbHeader.Status == USBD_STATUS_SUCCESS, "URB status not SUCCESS: 0x%08x\n", (unsigned int)urb.UrbHeader.Status);

    /* 2. SELECT_CONFIGURATION */
    memset(&urb, 0, sizeof(urb));
    urb.UrbSelectConfiguration.Hdr.Function = URB_FUNCTION_SELECT_CONFIGURATION;
    urb.UrbSelectConfiguration.Hdr.Length = sizeof(urb.UrbSelectConfiguration);
    urb.UrbSelectConfiguration.Interface.NumberOfPipes = 1;
    urb.UrbSelectConfiguration.Interface.Pipes[0].EndpointAddress = 0x81;
    urb.UrbSelectConfiguration.Interface.Pipes[0].PipeType = 2;

    status = WineUsbSimulateSubmitUrb(rel->Objects[0], &urb);
    ok(status == STATUS_SUCCESS, "SubmitUrb(SELECT_CONFIGURATION) failed: 0x%08x\n", (unsigned int)status);
    ok(urb.UrbSelectConfiguration.ConfigurationHandle != NULL, "ConfigurationHandle is NULL\n");
    ok(urb.UrbSelectConfiguration.Interface.Pipes[0].PipeHandle != NULL, "PipeHandle is NULL\n");
    ok(urb.UrbHeader.Status == USBD_STATUS_SUCCESS, "URB status not SUCCESS: 0x%08x\n", (unsigned int)urb.UrbHeader.Status);

    /* 3. VENDOR_DEVICE */
    memset(&urb, 0, sizeof(urb));
    urb.UrbControlVendorClassRequest.Hdr.Function = URB_FUNCTION_VENDOR_DEVICE;
    urb.UrbControlVendorClassRequest.Hdr.Length = sizeof(urb.UrbControlVendorClassRequest);
    urb.UrbControlVendorClassRequest.Request = 0x01;

    status = WineUsbSimulateSubmitUrb(rel->Objects[0], &urb);
    ok(status == STATUS_SUCCESS, "SubmitUrb(VENDOR_DEVICE) failed: 0x%08x\n", (unsigned int)status);
    ok(urb.UrbHeader.Status == USBD_STATUS_SUCCESS, "URB status not SUCCESS: 0x%08x\n", (unsigned int)urb.UrbHeader.Status);

    /* 4. Invalid URB function */
    memset(&urb, 0, sizeof(urb));
    urb.UrbHeader.Function = 0xFFFFU;
    urb.UrbHeader.Length = sizeof(URB_HEADER);

    status = WineUsbSimulateSubmitUrb(rel->Objects[0], &urb);
    ok(status == STATUS_NOT_IMPLEMENTED, "Expected STATUS_NOT_IMPLEMENTED, got 0x%08x\n", (unsigned int)status);
    ok(urb.UrbHeader.Status != USBD_STATUS_SUCCESS, "URB status should indicate error\n");
}

static void test_setupdi_service_install(void)
{
    WINEUSB_SETUPDI_STATUS setupdi;
    NTSTATUS status;

    WineUsbResetDriverState();
    memset(&setupdi, 0, sizeof(setupdi));

    status = WineUsbSimulateSetupDiInstall(&setupdi);
    ok(status == STATUS_SUCCESS, "SetupDi install simulation failed: 0x%08x\n", (unsigned int)status);
    ok(setupdi.service_created == 1, "Service was not created\n");
    ok(setupdi.service_started == 1, "Service failed to start (error 1062 reproduced)\n");
    ok(setupdi.win32_last_error == ERROR_SUCCESS, "Win32 last error not ERROR_SUCCESS: %u (error 126/1062 reproduced)\n", (unsigned int)setupdi.win32_last_error);
    ok(setupdi.driver_entry_status == STATUS_SUCCESS, "DriverEntry status not STATUS_SUCCESS: 0x%08x (0xc0000135 reproduced)\n", (unsigned int)setupdi.driver_entry_status);
    ok(setupdi.root_hub_enumerated == 1, "Root hub device not enumerated\n");
}

START_TEST(wineusb)
{
    test_driver_entry();
    test_add_device_and_fdo();
    test_pnp_start_and_relations();
    test_query_ids();
    test_urb_transfers();
    test_setupdi_service_install();
}

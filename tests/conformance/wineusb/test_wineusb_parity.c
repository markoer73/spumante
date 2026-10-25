/*
 * Differential Conformance Test for WineUSB Bus Driver & macOS USB Backend
 * (wineusb.sys & wineusb.so backed by macOS IOKit & libusb-1.0)
 *
 * Copyright 2026 Wine Parity Contributors
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "../../include/parity_test.h"
#include "../../../dlls/wineusb.sys/wineusb.h"

static void put_u32_le(uint8_t *dst, uint32_t v)
{
    dst[0] = (uint8_t)(v & 0xffU);
    dst[1] = (uint8_t)((v >> 8) & 0xffU);
    dst[2] = (uint8_t)((v >> 16) & 0xffU);
    dst[3] = (uint8_t)((v >> 24) & 0xffU);
}

int main(void)
{
    NTSTATUS status;

    parity_begin_document("wineusb.sys", "test_wineusb_parity", "x86_64", "wine_macos");

    /* =========================================================================
     * Case 1: wineusb_driver_entry_init
     * DriverEntry must initialize the Unix library and register AddDevice,
     * DriverUnload, and major dispatch functions without returning 0xc0000135.
     * ========================================================================= */
    {
        DRIVER_OBJECT drv;
        uint8_t digest[16];

        memset(&drv, 0, sizeof(drv));
        memset(digest, 0, sizeof(digest));
        WineUsbResetDriverState();

        status = DriverEntry(&drv, NULL);
        if (status == STATUS_SUCCESS) {
            put_u32_le(digest + 0, (uint32_t)status);
            put_u32_le(digest + 4, drv.DriverExtension != NULL ? 1U : 0U);
            put_u32_le(digest + 8, drv.MajorFunction[IRP_MJ_PNP] != NULL ? 1U : 0U);
            put_u32_le(digest + 12, drv.MajorFunction[IRP_MJ_INTERNAL_DEVICE_CONTROL] != NULL ? 1U : 0U);
        }

        ok(status == STATUS_SUCCESS && drv.MajorFunction[IRP_MJ_PNP] != NULL,
           "DriverEntry failed, status=0x%08x\n", (unsigned int)status);

        parity_record_exact(
            "wineusb_driver_entry_init",
            "WineUsb_DriverEntry_Init",
            "DriverEntry",
            status,
            GetLastError(),
            status == STATUS_SUCCESS ? 1U : 0U,
            digest,
            status == STATUS_SUCCESS ? sizeof(digest) : 0,
            status == STATUS_SUCCESS ? 1 : 0);
    }

    /* =========================================================================
     * Case 2: wineusb_add_device_bus_fdo
     * AddDevice attaches the bus FDO (FILE_DEVICE_BUS_EXTENDER) to the bus PDO.
     * ========================================================================= */
    {
        DRIVER_OBJECT drv;
        DEVICE_OBJECT pdo;
        DEVICE_OBJECT *fdo = NULL;
        uint8_t digest[16];

        memset(&drv, 0, sizeof(drv));
        memset(&pdo, 0, sizeof(pdo));
        memset(digest, 0, sizeof(digest));
        WineUsbResetDriverState();

        status = DriverEntry(&drv, NULL);
        if (status == STATUS_SUCCESS && drv.DriverExtension != NULL && drv.DriverExtension->AddDevice != NULL) {
            status = drv.DriverExtension->AddDevice(&drv, &pdo);
            fdo = pdo.AttachedDevice;
        }

        if (status == STATUS_SUCCESS && fdo != NULL) {
            put_u32_le(digest + 0, (uint32_t)status);
            put_u32_le(digest + 4, fdo->DeviceType);
            put_u32_le(digest + 8, (fdo->Flags & DO_DEVICE_INITIALIZING) == 0 ? 1U : 0U);
            put_u32_le(digest + 12, pdo.AttachedDevice == fdo ? 1U : 0U);
        }

        ok(status == STATUS_SUCCESS && fdo != NULL && fdo->DeviceType == FILE_DEVICE_BUS_EXTENDER,
           "AddDevice failed, status=0x%08x\n", (unsigned int)status);

        parity_record_exact(
            "wineusb_add_device_bus_fdo",
            "WineUsb_AddDevice_BusFdo",
            "driver_add_device",
            status,
            GetLastError(),
            status == STATUS_SUCCESS ? 1U : 0U,
            digest,
            status == STATUS_SUCCESS ? sizeof(digest) : 0,
            status == STATUS_SUCCESS ? 1 : 0);
    }

    /* =========================================================================
     * Case 3: wineusb_start_device_main_loop
     * IRP_MN_START_DEVICE starts the Unix USB main loop and discovers host devices.
     * ========================================================================= */
    {
        DRIVER_OBJECT drv;
        DEVICE_OBJECT pdo;
        uint8_t digest[16];

        memset(&drv, 0, sizeof(drv));
        memset(&pdo, 0, sizeof(pdo));
        memset(digest, 0, sizeof(digest));
        WineUsbResetDriverState();

        status = DriverEntry(&drv, NULL);
        if (status == STATUS_SUCCESS) {
            status = drv.DriverExtension->AddDevice(&drv, &pdo);
        }
        if (status == STATUS_SUCCESS) {
            status = WineUsbSimulatePnpStart(&drv, pdo.AttachedDevice);
        }

        if (status == STATUS_SUCCESS) {
            put_u32_le(digest + 0, (uint32_t)status);
            put_u32_le(digest + 4, 1U); /* loop active */
            put_u32_le(digest + 8, 1U); /* backend initialized */
            put_u32_le(digest + 12, 1U);
        }

        ok(status == STATUS_SUCCESS, "WineUsbSimulatePnpStart failed, status=0x%08x\n", (unsigned int)status);

        parity_record_exact(
            "wineusb_start_device_main_loop",
            "WineUsb_StartDevice_MainLoop",
            "WineUsbSimulatePnpStart",
            status,
            GetLastError(),
            status == STATUS_SUCCESS ? 1U : 0U,
            digest,
            status == STATUS_SUCCESS ? sizeof(digest) : 0,
            status == STATUS_SUCCESS ? 1 : 0);
    }

    /* =========================================================================
     * Case 4: wineusb_query_bus_relations
     * IRP_MN_QUERY_DEVICE_RELATIONS returns active USB child PDOs.
     * ========================================================================= */
    {
        DRIVER_OBJECT drv;
        DEVICE_OBJECT pdo;
        PDEVICE_RELATIONS rel = NULL;
        uint8_t digest[16];

        memset(&drv, 0, sizeof(drv));
        memset(&pdo, 0, sizeof(pdo));
        memset(digest, 0, sizeof(digest));
        WineUsbResetDriverState();

        status = DriverEntry(&drv, NULL);
        if (status == STATUS_SUCCESS) {
            status = drv.DriverExtension->AddDevice(&drv, &pdo);
        }
        if (status == STATUS_SUCCESS) {
            status = WineUsbSimulatePnpStart(&drv, pdo.AttachedDevice);
        }
        if (status == STATUS_SUCCESS) {
            status = WineUsbSimulateQueryRelations(pdo.AttachedDevice, BusRelations, &rel);
        }

        if (status == STATUS_SUCCESS && rel != NULL) {
            put_u32_le(digest + 0, (uint32_t)status);
            put_u32_le(digest + 4, rel->Count >= 1U ? 1U : 0U);
            put_u32_le(digest + 8, rel->Objects[0] != NULL ? 1U : 0U);
            put_u32_le(digest + 12, rel->Objects[0]->DeviceType == FILE_DEVICE_USB ? 1U : 0U);
        }

        ok(status == STATUS_SUCCESS && rel != NULL && rel->Count >= 1U,
           "QueryRelations failed, status=0x%08x\n", (unsigned int)status);

        parity_record_exact(
            "wineusb_query_bus_relations",
            "WineUsb_Query_BusRelations",
            "WineUsbSimulateQueryRelations",
            status,
            GetLastError(),
            status == STATUS_SUCCESS && rel != NULL && rel->Count >= 1U ? 1U : 0U,
            digest,
            status == STATUS_SUCCESS ? sizeof(digest) : 0,
            status == STATUS_SUCCESS ? 1 : 0);
    }

    /* =========================================================================
     * Case 5: wineusb_query_device_id_root_hub
     * BusQueryDeviceID returns USB\\ROOT_HUB or USB\\VID_xxxx&PID_yyyy.
     * ========================================================================= */
    {
        DRIVER_OBJECT drv;
        DEVICE_OBJECT pdo;
        PDEVICE_RELATIONS rel = NULL;
        WCHAR id_buf[128];
        uint32_t out_chars = 0;
        uint8_t digest[16];

        memset(&drv, 0, sizeof(drv));
        memset(&pdo, 0, sizeof(pdo));
        memset(id_buf, 0, sizeof(id_buf));
        memset(digest, 0, sizeof(digest));
        WineUsbResetDriverState();

        status = DriverEntry(&drv, NULL);
        if (status == STATUS_SUCCESS) {
            status = drv.DriverExtension->AddDevice(&drv, &pdo);
        }
        if (status == STATUS_SUCCESS) {
            status = WineUsbSimulatePnpStart(&drv, pdo.AttachedDevice);
        }
        if (status == STATUS_SUCCESS) {
            status = WineUsbSimulateQueryRelations(pdo.AttachedDevice, BusRelations, &rel);
        }
        if (status == STATUS_SUCCESS && rel != NULL && rel->Count >= 1U) {
            status = WineUsbSimulateQueryId(rel->Objects[0], BusQueryDeviceID, id_buf, 128, &out_chars);
        }

        if (status == STATUS_SUCCESS) {
            put_u32_le(digest + 0, (uint32_t)status);
            put_u32_le(digest + 4, out_chars > 3U ? 1U : 0U);
            put_u32_le(digest + 8, (id_buf[0] == 'U' && id_buf[1] == 'S' && id_buf[2] == 'B') ? 1U : 0U);
            put_u32_le(digest + 12, out_chars);
        }

        ok(status == STATUS_SUCCESS && out_chars > 3U && id_buf[0] == 'U',
           "QueryDeviceID failed, status=0x%08x\n", (unsigned int)status);

        parity_record_exact(
            "wineusb_query_device_id_root_hub",
            "WineUsb_Query_DeviceIdRootHub",
            "WineUsbSimulateQueryId",
            status,
            GetLastError(),
            out_chars,
            digest,
            status == STATUS_SUCCESS ? sizeof(digest) : 0,
            status == STATUS_SUCCESS ? 1 : 0);
    }

    /* =========================================================================
     * Case 6: wineusb_query_hardware_ids
     * BusQueryHardwareIDs returns USB multi-string hardware IDs.
     * ========================================================================= */
    {
        DRIVER_OBJECT drv;
        DEVICE_OBJECT pdo;
        PDEVICE_RELATIONS rel = NULL;
        WCHAR id_buf[256];
        uint32_t out_chars = 0;
        uint8_t digest[16];

        memset(&drv, 0, sizeof(drv));
        memset(&pdo, 0, sizeof(pdo));
        memset(id_buf, 0, sizeof(id_buf));
        memset(digest, 0, sizeof(digest));
        WineUsbResetDriverState();

        status = DriverEntry(&drv, NULL);
        if (status == STATUS_SUCCESS) {
            status = drv.DriverExtension->AddDevice(&drv, &pdo);
        }
        if (status == STATUS_SUCCESS) {
            status = WineUsbSimulatePnpStart(&drv, pdo.AttachedDevice);
        }
        if (status == STATUS_SUCCESS) {
            status = WineUsbSimulateQueryRelations(pdo.AttachedDevice, BusRelations, &rel);
        }
        if (status == STATUS_SUCCESS && rel != NULL && rel->Count >= 1U) {
            status = WineUsbSimulateQueryId(rel->Objects[0], BusQueryHardwareIDs, id_buf, 256, &out_chars);
        }

        if (status == STATUS_SUCCESS) {
            put_u32_le(digest + 0, (uint32_t)status);
            put_u32_le(digest + 4, out_chars > 8U ? 1U : 0U);
            put_u32_le(digest + 8, (id_buf[0] == 'U' && id_buf[1] == 'S' && id_buf[2] == 'B') ? 1U : 0U);
            put_u32_le(digest + 12, out_chars);
        }

        ok(status == STATUS_SUCCESS && out_chars > 8U, "QueryHardwareIDs failed, status=0x%08x\n", (unsigned int)status);

        parity_record_exact(
            "wineusb_query_hardware_ids",
            "WineUsb_Query_HardwareIds",
            "WineUsbSimulateQueryId",
            status,
            GetLastError(),
            out_chars,
            digest,
            status == STATUS_SUCCESS ? sizeof(digest) : 0,
            status == STATUS_SUCCESS ? 1 : 0);
    }

    /* =========================================================================
     * Case 7: wineusb_query_compatible_ids
     * BusQueryCompatibleIDs returns USB compatible IDs.
     * ========================================================================= */
    {
        DRIVER_OBJECT drv;
        DEVICE_OBJECT pdo;
        PDEVICE_RELATIONS rel = NULL;
        WCHAR id_buf[256];
        uint32_t out_chars = 0;
        uint8_t digest[16];

        memset(&drv, 0, sizeof(drv));
        memset(&pdo, 0, sizeof(pdo));
        memset(id_buf, 0, sizeof(id_buf));
        memset(digest, 0, sizeof(digest));
        WineUsbResetDriverState();

        status = DriverEntry(&drv, NULL);
        if (status == STATUS_SUCCESS) {
            status = drv.DriverExtension->AddDevice(&drv, &pdo);
        }
        if (status == STATUS_SUCCESS) {
            status = WineUsbSimulatePnpStart(&drv, pdo.AttachedDevice);
        }
        if (status == STATUS_SUCCESS) {
            status = WineUsbSimulateQueryRelations(pdo.AttachedDevice, BusRelations, &rel);
        }
        if (status == STATUS_SUCCESS && rel != NULL && rel->Count >= 1U) {
            status = WineUsbSimulateQueryId(rel->Objects[0], BusQueryCompatibleIDs, id_buf, 256, &out_chars);
        }

        if (status == STATUS_SUCCESS) {
            put_u32_le(digest + 0, (uint32_t)status);
            put_u32_le(digest + 4, out_chars > 3U ? 1U : 0U);
            put_u32_le(digest + 8, (id_buf[0] == 'U' && id_buf[1] == 'S' && id_buf[2] == 'B') ? 1U : 0U);
            put_u32_le(digest + 12, out_chars);
        }

        ok(status == STATUS_SUCCESS && out_chars > 3U, "QueryCompatibleIDs failed, status=0x%08x\n", (unsigned int)status);

        parity_record_exact(
            "wineusb_query_compatible_ids",
            "WineUsb_Query_CompatibleIds",
            "WineUsbSimulateQueryId",
            status,
            GetLastError(),
            out_chars,
            digest,
            status == STATUS_SUCCESS ? sizeof(digest) : 0,
            status == STATUS_SUCCESS ? 1 : 0);
    }

    /* =========================================================================
     * Case 8: wineusb_submit_urb_get_descriptor
     * URB_FUNCTION_GET_DESCRIPTOR_FROM_DEVICE returns valid USB_DEVICE_DESCRIPTOR.
     * ========================================================================= */
    {
        DRIVER_OBJECT drv;
        DEVICE_OBJECT pdo;
        PDEVICE_RELATIONS rel = NULL;
        URB urb;
        USB_DEVICE_DESCRIPTOR desc;
        uint8_t digest[16];

        memset(&drv, 0, sizeof(drv));
        memset(&pdo, 0, sizeof(pdo));
        memset(&urb, 0, sizeof(urb));
        memset(&desc, 0, sizeof(desc));
        memset(digest, 0, sizeof(digest));
        WineUsbResetDriverState();

        status = DriverEntry(&drv, NULL);
        if (status == STATUS_SUCCESS) {
            status = drv.DriverExtension->AddDevice(&drv, &pdo);
        }
        if (status == STATUS_SUCCESS) {
            status = WineUsbSimulatePnpStart(&drv, pdo.AttachedDevice);
        }
        if (status == STATUS_SUCCESS) {
            status = WineUsbSimulateQueryRelations(pdo.AttachedDevice, BusRelations, &rel);
        }
        if (status == STATUS_SUCCESS && rel != NULL && rel->Count >= 1U) {
            urb.UrbControlDescriptorRequest.Hdr.Function = URB_FUNCTION_GET_DESCRIPTOR_FROM_DEVICE;
            urb.UrbControlDescriptorRequest.Hdr.Length = sizeof(urb.UrbControlDescriptorRequest);
            urb.UrbControlDescriptorRequest.DescriptorType = USB_DEVICE_DESCRIPTOR_TYPE;
            urb.UrbControlDescriptorRequest.TransferBuffer = &desc;
            urb.UrbControlDescriptorRequest.TransferBufferLength = sizeof(desc);
            status = WineUsbSimulateSubmitUrb(rel->Objects[0], &urb);
        }

        if (status == STATUS_SUCCESS) {
            put_u32_le(digest + 0, (uint32_t)status);
            put_u32_le(digest + 4, desc.bLength == sizeof(desc) ? 1U : 0U);
            put_u32_le(digest + 8, desc.bDescriptorType == USB_DEVICE_DESCRIPTOR_TYPE ? 1U : 0U);
            put_u32_le(digest + 12, desc.bNumConfigurations >= 1U ? 1U : 0U);
        }

        ok(status == STATUS_SUCCESS && desc.bLength == sizeof(desc) && desc.bDescriptorType == 1U,
           "SubmitUrb(GET_DESCRIPTOR) failed, status=0x%08x\n", (unsigned int)status);

        parity_record_exact(
            "wineusb_submit_urb_get_descriptor",
            "WineUsb_SubmitUrb_GetDescriptor",
            "WineUsbSimulateSubmitUrb",
            status,
            GetLastError(),
            status == STATUS_SUCCESS ? sizeof(desc) : 0U,
            digest,
            status == STATUS_SUCCESS ? sizeof(digest) : 0,
            status == STATUS_SUCCESS ? 1 : 0);
    }

    /* =========================================================================
     * Case 9: wineusb_submit_urb_select_configuration
     * URB_FUNCTION_SELECT_CONFIGURATION populates pipe handles.
     * ========================================================================= */
    {
        DRIVER_OBJECT drv;
        DEVICE_OBJECT pdo;
        PDEVICE_RELATIONS rel = NULL;
        URB urb;
        uint8_t digest[16];

        memset(&drv, 0, sizeof(drv));
        memset(&pdo, 0, sizeof(pdo));
        memset(&urb, 0, sizeof(urb));
        memset(digest, 0, sizeof(digest));
        WineUsbResetDriverState();

        status = DriverEntry(&drv, NULL);
        if (status == STATUS_SUCCESS) {
            status = drv.DriverExtension->AddDevice(&drv, &pdo);
        }
        if (status == STATUS_SUCCESS) {
            status = WineUsbSimulatePnpStart(&drv, pdo.AttachedDevice);
        }
        if (status == STATUS_SUCCESS) {
            status = WineUsbSimulateQueryRelations(pdo.AttachedDevice, BusRelations, &rel);
        }
        if (status == STATUS_SUCCESS && rel != NULL && rel->Count >= 1U) {
            urb.UrbSelectConfiguration.Hdr.Function = URB_FUNCTION_SELECT_CONFIGURATION;
            urb.UrbSelectConfiguration.Hdr.Length = sizeof(urb.UrbSelectConfiguration);
            urb.UrbSelectConfiguration.Interface.NumberOfPipes = 1;
            urb.UrbSelectConfiguration.Interface.Pipes[0].EndpointAddress = 0x81;
            urb.UrbSelectConfiguration.Interface.Pipes[0].PipeType = 2; /* UsbdPipeTypeBulk */
            status = WineUsbSimulateSubmitUrb(rel->Objects[0], &urb);
        }

        if (status == STATUS_SUCCESS) {
            put_u32_le(digest + 0, (uint32_t)status);
            put_u32_le(digest + 4, urb.UrbSelectConfiguration.ConfigurationHandle != NULL ? 1U : 0U);
            put_u32_le(digest + 8, urb.UrbSelectConfiguration.Interface.Pipes[0].PipeHandle != NULL ? 1U : 0U);
            put_u32_le(digest + 12, urb.UrbHeader.Status == USBD_STATUS_SUCCESS ? 1U : 0U);
        }

        ok(status == STATUS_SUCCESS && urb.UrbSelectConfiguration.Interface.Pipes[0].PipeHandle != NULL,
           "SubmitUrb(SELECT_CONFIGURATION) failed, status=0x%08x\n", (unsigned int)status);

        parity_record_exact(
            "wineusb_submit_urb_select_configuration",
            "WineUsb_SubmitUrb_SelectConfiguration",
            "WineUsbSimulateSubmitUrb",
            status,
            GetLastError(),
            status == STATUS_SUCCESS ? 1U : 0U,
            digest,
            status == STATUS_SUCCESS ? sizeof(digest) : 0,
            status == STATUS_SUCCESS ? 1 : 0);
    }

    /* =========================================================================
     * Case 10: wineusb_submit_urb_vendor_or_class_request
     * URB_FUNCTION_VENDOR_DEVICE or URB_FUNCTION_CLASS_INTERFACE succeeds.
     * ========================================================================= */
    {
        DRIVER_OBJECT drv;
        DEVICE_OBJECT pdo;
        PDEVICE_RELATIONS rel = NULL;
        URB urb;
        uint8_t buffer[64];
        uint8_t digest[16];

        memset(&drv, 0, sizeof(drv));
        memset(&pdo, 0, sizeof(pdo));
        memset(&urb, 0, sizeof(urb));
        memset(buffer, 0, sizeof(buffer));
        memset(digest, 0, sizeof(digest));
        WineUsbResetDriverState();

        status = DriverEntry(&drv, NULL);
        if (status == STATUS_SUCCESS) {
            status = drv.DriverExtension->AddDevice(&drv, &pdo);
        }
        if (status == STATUS_SUCCESS) {
            status = WineUsbSimulatePnpStart(&drv, pdo.AttachedDevice);
        }
        if (status == STATUS_SUCCESS) {
            status = WineUsbSimulateQueryRelations(pdo.AttachedDevice, BusRelations, &rel);
        }
        if (status == STATUS_SUCCESS && rel != NULL && rel->Count >= 1U) {
            urb.UrbControlVendorClassRequest.Hdr.Function = URB_FUNCTION_VENDOR_DEVICE;
            urb.UrbControlVendorClassRequest.Hdr.Length = sizeof(urb.UrbControlVendorClassRequest);
            urb.UrbControlVendorClassRequest.TransferFlags = USBD_TRANSFER_DIRECTION_IN;
            urb.UrbControlVendorClassRequest.TransferBufferLength = sizeof(buffer);
            urb.UrbControlVendorClassRequest.TransferBuffer = buffer;
            urb.UrbControlVendorClassRequest.Request = 0x01;
            status = WineUsbSimulateSubmitUrb(rel->Objects[0], &urb);
        }

        if (status == STATUS_SUCCESS) {
            put_u32_le(digest + 0, (uint32_t)status);
            put_u32_le(digest + 4, urb.UrbHeader.Status == USBD_STATUS_SUCCESS ? 1U : 0U);
            put_u32_le(digest + 8, 1U);
            put_u32_le(digest + 12, 1U);
        }

        ok(status == STATUS_SUCCESS, "SubmitUrb(VENDOR_DEVICE) failed, status=0x%08x\n", (unsigned int)status);

        parity_record_exact(
            "wineusb_submit_urb_vendor_or_class_request",
            "WineUsb_SubmitUrb_VendorOrClassRequest",
            "WineUsbSimulateSubmitUrb",
            status,
            GetLastError(),
            status == STATUS_SUCCESS ? 1U : 0U,
            digest,
            status == STATUS_SUCCESS ? sizeof(digest) : 0,
            status == STATUS_SUCCESS ? 1 : 0);
    }

    /* =========================================================================
     * Case 11: wineusb_invalid_urb_function_error
     * Invalid URB function returns STATUS_NOT_IMPLEMENTED (0xc0000002).
     * ========================================================================= */
    {
        DRIVER_OBJECT drv;
        DEVICE_OBJECT pdo;
        PDEVICE_RELATIONS rel = NULL;
        URB urb;
        uint8_t digest[16];

        memset(&drv, 0, sizeof(drv));
        memset(&pdo, 0, sizeof(pdo));
        memset(&urb, 0, sizeof(urb));
        memset(digest, 0, sizeof(digest));
        WineUsbResetDriverState();

        status = DriverEntry(&drv, NULL);
        if (status == STATUS_SUCCESS) {
            status = drv.DriverExtension->AddDevice(&drv, &pdo);
        }
        if (status == STATUS_SUCCESS) {
            status = WineUsbSimulatePnpStart(&drv, pdo.AttachedDevice);
        }
        if (status == STATUS_SUCCESS) {
            status = WineUsbSimulateQueryRelations(pdo.AttachedDevice, BusRelations, &rel);
        }
        if (status == STATUS_SUCCESS && rel != NULL && rel->Count >= 1U) {
            urb.UrbHeader.Function = 0xFFFFU;
            urb.UrbHeader.Length = sizeof(URB_HEADER);
            status = WineUsbSimulateSubmitUrb(rel->Objects[0], &urb);
        }

        put_u32_le(digest + 0, (uint32_t)status);
        put_u32_le(digest + 4, status == STATUS_NOT_IMPLEMENTED ? 1U : 0U);
        put_u32_le(digest + 8, urb.UrbHeader.Status != USBD_STATUS_SUCCESS ? 1U : 0U);
        put_u32_le(digest + 12, 0U);

        ok(status == STATUS_NOT_IMPLEMENTED, "Expected STATUS_NOT_IMPLEMENTED, got 0x%08x\n", (unsigned int)status);

        parity_record_exact(
            "wineusb_invalid_urb_function_error",
            "WineUsb_InvalidUrbFunction_Error",
            "WineUsbSimulateSubmitUrb",
            status,
            GetLastError(),
            0U,
            digest,
            sizeof(digest),
            status == STATUS_NOT_IMPLEMENTED ? 1 : 0);
    }

    /* =========================================================================
     * Case 12: wineusb_setupdi_service_install_success
     * SetupDiInstallDevice simulation starts service without error 126 or 1062.
     * ========================================================================= */
    {
        WINEUSB_SETUPDI_STATUS setupdi;
        uint8_t digest[16];

        memset(&setupdi, 0, sizeof(setupdi));
        memset(digest, 0, sizeof(digest));
        WineUsbResetDriverState();

        status = WineUsbSimulateSetupDiInstall(&setupdi);

        if (status == STATUS_SUCCESS) {
            put_u32_le(digest + 0, (uint32_t)status);
            put_u32_le(digest + 4, setupdi.service_created);
            put_u32_le(digest + 8, setupdi.service_started);
            put_u32_le(digest + 12, setupdi.win32_last_error);
        }

        ok(status == STATUS_SUCCESS && setupdi.service_started == 1U && setupdi.win32_last_error == ERROR_SUCCESS,
           "SetupDi service install failed, status=0x%08x err=%u\n", (unsigned int)status, (unsigned int)setupdi.win32_last_error);

        parity_record_exact(
            "wineusb_setupdi_service_install_success",
            "WineUsb_SetupDi_ServiceInstallSuccess",
            "WineUsbSimulateSetupDiInstall",
            status,
            GetLastError(),
            setupdi.win32_last_error,
            digest,
            status == STATUS_SUCCESS ? sizeof(digest) : 0,
            status == STATUS_SUCCESS ? 1 : 0);
    }

    parity_end_document();
    return 0;
}

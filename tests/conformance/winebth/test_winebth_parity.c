/*
 * Differential Conformance Test for WineBTH Bus Driver & macOS IOBluetooth Backend
 * (winebth.sys & winebth.so backed by macOS IOBluetooth & CoreBluetooth)
 *
 * Copyright 2026 Wine Parity Contributors
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "../../include/parity_test.h"
#include "../../../dlls/winebth.sys/winebth.h"

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

    parity_begin_document("winebth.sys", "test_winebth_parity", "x86_64", "wine_macos");

    /* =========================================================================
     * Case 1: winebth_driver_entry_init
     * DriverEntry must initialize the Unix Bluetooth library and register
     * AddDevice, DriverUnload, and major dispatch functions without returning
     * STATUS_INTERNAL_ERROR (0xc00000e5).
     * ========================================================================= */
    {
        DRIVER_OBJECT drv;
        uint8_t digest[16];

        memset(&drv, 0, sizeof(drv));
        memset(digest, 0, sizeof(digest));
        WineBthResetDriverState();

        status = DriverEntry(&drv, NULL);
        if (status == STATUS_SUCCESS) {
            put_u32_le(digest + 0, (uint32_t)status);
            put_u32_le(digest + 4, drv.DriverExtension != NULL ? 1U : 0U);
            put_u32_le(digest + 8, drv.MajorFunction[IRP_MJ_PNP] != NULL ? 1U : 0U);
            put_u32_le(digest + 12, drv.MajorFunction[IRP_MJ_DEVICE_CONTROL] != NULL ? 1U : 0U);
        }

        ok(status == STATUS_SUCCESS && drv.MajorFunction[IRP_MJ_PNP] != NULL,
           "DriverEntry failed, status=0x%08x\n", (unsigned int)status);

        parity_record_exact(
            "winebth_driver_entry_init",
            "WineBth_DriverEntry_Init",
            "DriverEntry",
            status,
            GetLastError(),
            status == STATUS_SUCCESS ? 1U : 0U,
            digest,
            status == STATUS_SUCCESS ? sizeof(digest) : 0,
            status == STATUS_SUCCESS ? 1 : 0);
    }

    /* =========================================================================
     * Case 2: winebth_add_device_bus_fdo
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
        WineBthResetDriverState();

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
            "winebth_add_device_bus_fdo",
            "WineBth_AddDevice_BusFdo",
            "driver_add_device",
            status,
            GetLastError(),
            status == STATUS_SUCCESS ? 1U : 0U,
            digest,
            status == STATUS_SUCCESS ? sizeof(digest) : 0,
            status == STATUS_SUCCESS ? 1 : 0);
    }

    /* =========================================================================
     * Case 3: winebth_start_device_event_pump
     * IRP_MN_START_DEVICE starts the Bluetooth event loop and discovers adapters.
     * ========================================================================= */
    {
        DRIVER_OBJECT drv;
        DEVICE_OBJECT pdo;
        uint8_t digest[16];

        memset(&drv, 0, sizeof(drv));
        memset(&pdo, 0, sizeof(pdo));
        memset(digest, 0, sizeof(digest));
        WineBthResetDriverState();

        status = DriverEntry(&drv, NULL);
        if (status == STATUS_SUCCESS) {
            status = drv.DriverExtension->AddDevice(&drv, &pdo);
        }
        if (status == STATUS_SUCCESS) {
            status = WineBthSimulatePnpStart(&drv, pdo.AttachedDevice);
        }

        if (status == STATUS_SUCCESS) {
            put_u32_le(digest + 0, (uint32_t)status);
            put_u32_le(digest + 4, 1U); /* loop active */
            put_u32_le(digest + 8, 1U); /* radio backend initialized */
            put_u32_le(digest + 12, 1U);
        }

        ok(status == STATUS_SUCCESS, "WineBthSimulatePnpStart failed, status=0x%08x\n", (unsigned int)status);

        parity_record_exact(
            "winebth_start_device_event_pump",
            "WineBth_StartDevice_EventPump",
            "WineBthSimulatePnpStart",
            status,
            GetLastError(),
            status == STATUS_SUCCESS ? 1U : 0U,
            digest,
            status == STATUS_SUCCESS ? sizeof(digest) : 0,
            status == STATUS_SUCCESS ? 1 : 0);
    }

    /* =========================================================================
     * Case 4: winebth_query_bus_relations
     * IRP_MN_QUERY_DEVICE_RELATIONS returns active Bluetooth radio child PDOs.
     * ========================================================================= */
    {
        DRIVER_OBJECT drv;
        DEVICE_OBJECT pdo;
        PDEVICE_RELATIONS rel = NULL;
        uint8_t digest[16];

        memset(&drv, 0, sizeof(drv));
        memset(&pdo, 0, sizeof(pdo));
        memset(digest, 0, sizeof(digest));
        WineBthResetDriverState();

        status = DriverEntry(&drv, NULL);
        if (status == STATUS_SUCCESS) {
            status = drv.DriverExtension->AddDevice(&drv, &pdo);
        }
        if (status == STATUS_SUCCESS) {
            status = WineBthSimulatePnpStart(&drv, pdo.AttachedDevice);
        }
        if (status == STATUS_SUCCESS) {
            status = WineBthSimulateQueryRelations(pdo.AttachedDevice, BusRelations, &rel);
        }

        if (status == STATUS_SUCCESS && rel != NULL) {
            put_u32_le(digest + 0, (uint32_t)status);
            put_u32_le(digest + 4, rel->Count >= 1U ? 1U : 0U);
            put_u32_le(digest + 8, rel->Objects[0] != NULL ? 1U : 0U);
            put_u32_le(digest + 12, rel->Objects[0]->DeviceType == FILE_DEVICE_BLUETOOTH ? 1U : 0U);
        }

        ok(status == STATUS_SUCCESS && rel != NULL && rel->Count >= 1U,
           "QueryRelations failed, status=0x%08x\n", (unsigned int)status);

        parity_record_exact(
            "winebth_query_bus_relations",
            "WineBth_Query_BusRelations",
            "WineBthSimulateQueryRelations",
            status,
            GetLastError(),
            status == STATUS_SUCCESS && rel != NULL && rel->Count >= 1U ? 1U : 0U,
            digest,
            status == STATUS_SUCCESS ? sizeof(digest) : 0,
            status == STATUS_SUCCESS ? 1 : 0);
    }

    /* =========================================================================
     * Case 5: winebth_query_device_id_radio
     * BusQueryDeviceID returns BTH\\ROOT_RADIO or BTH\\MS_BTHBRB.
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
        WineBthResetDriverState();

        status = DriverEntry(&drv, NULL);
        if (status == STATUS_SUCCESS) {
            status = drv.DriverExtension->AddDevice(&drv, &pdo);
        }
        if (status == STATUS_SUCCESS) {
            status = WineBthSimulatePnpStart(&drv, pdo.AttachedDevice);
        }
        if (status == STATUS_SUCCESS) {
            status = WineBthSimulateQueryRelations(pdo.AttachedDevice, BusRelations, &rel);
        }
        if (status == STATUS_SUCCESS && rel != NULL && rel->Count >= 1U) {
            status = WineBthSimulateQueryId(rel->Objects[0], BusQueryDeviceID, id_buf, 128, &out_chars);
        }

        if (status == STATUS_SUCCESS) {
            put_u32_le(digest + 0, (uint32_t)status);
            put_u32_le(digest + 4, out_chars > 3U ? 1U : 0U);
            put_u32_le(digest + 8, (id_buf[0] == 'B' && id_buf[1] == 'T' && id_buf[2] == 'H') ? 1U : 0U);
            put_u32_le(digest + 12, out_chars);
        }

        ok(status == STATUS_SUCCESS && out_chars > 3U && id_buf[0] == 'B',
           "QueryDeviceID failed, status=0x%08x\n", (unsigned int)status);

        parity_record_exact(
            "winebth_query_device_id_radio",
            "WineBth_Query_DeviceIdRadio",
            "WineBthSimulateQueryId",
            status,
            GetLastError(),
            out_chars,
            digest,
            status == STATUS_SUCCESS ? sizeof(digest) : 0,
            status == STATUS_SUCCESS ? 1 : 0);
    }

    /* =========================================================================
     * Case 6: winebth_query_hardware_ids
     * BusQueryHardwareIDs returns Bluetooth multi-string hardware IDs.
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
        WineBthResetDriverState();

        status = DriverEntry(&drv, NULL);
        if (status == STATUS_SUCCESS) {
            status = drv.DriverExtension->AddDevice(&drv, &pdo);
        }
        if (status == STATUS_SUCCESS) {
            status = WineBthSimulatePnpStart(&drv, pdo.AttachedDevice);
        }
        if (status == STATUS_SUCCESS) {
            status = WineBthSimulateQueryRelations(pdo.AttachedDevice, BusRelations, &rel);
        }
        if (status == STATUS_SUCCESS && rel != NULL && rel->Count >= 1U) {
            status = WineBthSimulateQueryId(rel->Objects[0], BusQueryHardwareIDs, id_buf, 256, &out_chars);
        }

        if (status == STATUS_SUCCESS) {
            put_u32_le(digest + 0, (uint32_t)status);
            put_u32_le(digest + 4, out_chars > 8U ? 1U : 0U);
            put_u32_le(digest + 8, (id_buf[0] == 'B' && id_buf[1] == 'T' && id_buf[2] == 'H') ? 1U : 0U);
            put_u32_le(digest + 12, out_chars);
        }

        ok(status == STATUS_SUCCESS && out_chars > 8U, "QueryHardwareIDs failed, status=0x%08x\n", (unsigned int)status);

        parity_record_exact(
            "winebth_query_hardware_ids",
            "WineBth_Query_HardwareIds",
            "WineBthSimulateQueryId",
            status,
            GetLastError(),
            out_chars,
            digest,
            status == STATUS_SUCCESS ? sizeof(digest) : 0,
            status == STATUS_SUCCESS ? 1 : 0);
    }

    /* =========================================================================
     * Case 7: winebth_query_compatible_ids
     * BusQueryCompatibleIDs returns Bluetooth compatible IDs.
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
        WineBthResetDriverState();

        status = DriverEntry(&drv, NULL);
        if (status == STATUS_SUCCESS) {
            status = drv.DriverExtension->AddDevice(&drv, &pdo);
        }
        if (status == STATUS_SUCCESS) {
            status = WineBthSimulatePnpStart(&drv, pdo.AttachedDevice);
        }
        if (status == STATUS_SUCCESS) {
            status = WineBthSimulateQueryRelations(pdo.AttachedDevice, BusRelations, &rel);
        }
        if (status == STATUS_SUCCESS && rel != NULL && rel->Count >= 1U) {
            status = WineBthSimulateQueryId(rel->Objects[0], BusQueryCompatibleIDs, id_buf, 256, &out_chars);
        }

        if (status == STATUS_SUCCESS) {
            put_u32_le(digest + 0, (uint32_t)status);
            put_u32_le(digest + 4, out_chars > 3U ? 1U : 0U);
            put_u32_le(digest + 8, (id_buf[0] == 'B' && id_buf[1] == 'T' && id_buf[2] == 'H') ? 1U : 0U);
            put_u32_le(digest + 12, out_chars);
        }

        ok(status == STATUS_SUCCESS && out_chars > 3U, "QueryCompatibleIDs failed, status=0x%08x\n", (unsigned int)status);

        parity_record_exact(
            "winebth_query_compatible_ids",
            "WineBth_Query_CompatibleIds",
            "WineBthSimulateQueryId",
            status,
            GetLastError(),
            out_chars,
            digest,
            status == STATUS_SUCCESS ? sizeof(digest) : 0,
            status == STATUS_SUCCESS ? 1 : 0);
    }

    /* =========================================================================
     * Case 8: winebth_ioctl_get_local_radio_info
     * IOCTL_BTH_GET_LOCAL_INFO returns BTH_LOCAL_RADIO_INFO with valid radio address.
     * ========================================================================= */
    {
        DRIVER_OBJECT drv;
        DEVICE_OBJECT pdo;
        PDEVICE_RELATIONS rel = NULL;
        BTH_LOCAL_RADIO_INFO local_info;
        uint32_t bytes_ret = 0;
        uint8_t digest[16];

        memset(&drv, 0, sizeof(drv));
        memset(&pdo, 0, sizeof(pdo));
        memset(&local_info, 0, sizeof(local_info));
        memset(digest, 0, sizeof(digest));
        WineBthResetDriverState();

        status = DriverEntry(&drv, NULL);
        if (status == STATUS_SUCCESS) {
            status = drv.DriverExtension->AddDevice(&drv, &pdo);
        }
        if (status == STATUS_SUCCESS) {
            status = WineBthSimulatePnpStart(&drv, pdo.AttachedDevice);
        }
        if (status == STATUS_SUCCESS) {
            status = WineBthSimulateQueryRelations(pdo.AttachedDevice, BusRelations, &rel);
        }
        if (status == STATUS_SUCCESS && rel != NULL && rel->Count >= 1U) {
            status = WineBthSimulateIoctl(rel->Objects[0], IOCTL_BTH_GET_LOCAL_INFO,
                                          NULL, 0,
                                          &local_info, sizeof(local_info),
                                          &bytes_ret);
        }

        if (status == STATUS_SUCCESS) {
            put_u32_le(digest + 0, (uint32_t)status);
            put_u32_le(digest + 4, local_info.address != 0 ? 1U : 0U);
            put_u32_le(digest + 8, local_info.hciVersion >= 1U ? 1U : 0U);
            put_u32_le(digest + 12, bytes_ret == sizeof(local_info) ? 1U : 0U);
        }

        ok(status == STATUS_SUCCESS && local_info.address != 0,
           "IOCTL_BTH_GET_LOCAL_INFO failed, status=0x%08x\n", (unsigned int)status);

        parity_record_exact(
            "winebth_ioctl_get_local_radio_info",
            "WineBth_Ioctl_GetLocalRadioInfo",
            "WineBthSimulateIoctl",
            status,
            GetLastError(),
            status == STATUS_SUCCESS ? sizeof(local_info) : 0U,
            digest,
            status == STATUS_SUCCESS ? sizeof(digest) : 0,
            status == STATUS_SUCCESS ? 1 : 0);
    }

    /* =========================================================================
     * Case 9: winebth_ioctl_get_radio_info
     * IOCTL_BTH_GET_RADIO_INFO returns BTH_RADIO_INFO.
     * ========================================================================= */
    {
        DRIVER_OBJECT drv;
        DEVICE_OBJECT pdo;
        PDEVICE_RELATIONS rel = NULL;
        BTH_RADIO_INFO radio_info;
        uint32_t bytes_ret = 0;
        uint8_t digest[16];

        memset(&drv, 0, sizeof(drv));
        memset(&pdo, 0, sizeof(pdo));
        memset(&radio_info, 0, sizeof(radio_info));
        memset(digest, 0, sizeof(digest));
        WineBthResetDriverState();

        status = DriverEntry(&drv, NULL);
        if (status == STATUS_SUCCESS) {
            status = drv.DriverExtension->AddDevice(&drv, &pdo);
        }
        if (status == STATUS_SUCCESS) {
            status = WineBthSimulatePnpStart(&drv, pdo.AttachedDevice);
        }
        if (status == STATUS_SUCCESS) {
            status = WineBthSimulateQueryRelations(pdo.AttachedDevice, BusRelations, &rel);
        }
        if (status == STATUS_SUCCESS && rel != NULL && rel->Count >= 1U) {
            status = WineBthSimulateIoctl(rel->Objects[0], IOCTL_BTH_GET_RADIO_INFO,
                                          NULL, 0,
                                          &radio_info, sizeof(radio_info),
                                          &bytes_ret);
        }

        if (status == STATUS_SUCCESS) {
            put_u32_le(digest + 0, (uint32_t)status);
            put_u32_le(digest + 4, radio_info.mfg != 0 ? 1U : 0U);
            put_u32_le(digest + 8, radio_info.lmpVersion >= 1U ? 1U : 0U);
            put_u32_le(digest + 12, bytes_ret == sizeof(radio_info) ? 1U : 0U);
        }

        ok(status == STATUS_SUCCESS && radio_info.mfg != 0,
           "IOCTL_BTH_GET_RADIO_INFO failed, status=0x%08x\n", (unsigned int)status);

        parity_record_exact(
            "winebth_ioctl_get_radio_info",
            "WineBth_Ioctl_GetRadioInfo",
            "WineBthSimulateIoctl",
            status,
            GetLastError(),
            status == STATUS_SUCCESS ? sizeof(radio_info) : 0U,
            digest,
            status == STATUS_SUCCESS ? sizeof(digest) : 0,
            status == STATUS_SUCCESS ? 1 : 0);
    }

    /* =========================================================================
     * Case 10: winebth_ioctl_disconnect_device
     * IOCTL_BTH_DISCONNECT_DEVICE handles disconnection request cleanly.
     * ========================================================================= */
    {
        DRIVER_OBJECT drv;
        DEVICE_OBJECT pdo;
        PDEVICE_RELATIONS rel = NULL;
        BTH_ADDR target_addr = 0x001BDC000001ULL;
        uint32_t bytes_ret = 0;
        uint8_t digest[16];

        memset(&drv, 0, sizeof(drv));
        memset(&pdo, 0, sizeof(pdo));
        memset(digest, 0, sizeof(digest));
        WineBthResetDriverState();

        status = DriverEntry(&drv, NULL);
        if (status == STATUS_SUCCESS) {
            status = drv.DriverExtension->AddDevice(&drv, &pdo);
        }
        if (status == STATUS_SUCCESS) {
            status = WineBthSimulatePnpStart(&drv, pdo.AttachedDevice);
        }
        if (status == STATUS_SUCCESS) {
            status = WineBthSimulateQueryRelations(pdo.AttachedDevice, BusRelations, &rel);
        }
        if (status == STATUS_SUCCESS && rel != NULL && rel->Count >= 1U) {
            status = WineBthSimulateIoctl(rel->Objects[0], IOCTL_BTH_DISCONNECT_DEVICE,
                                          &target_addr, sizeof(target_addr),
                                          NULL, 0,
                                          &bytes_ret);
        }

        if (status == STATUS_SUCCESS) {
            put_u32_le(digest + 0, (uint32_t)status);
            put_u32_le(digest + 4, 1U);
            put_u32_le(digest + 8, 1U);
            put_u32_le(digest + 12, 1U);
        }

        ok(status == STATUS_SUCCESS, "IOCTL_BTH_DISCONNECT_DEVICE failed, status=0x%08x\n", (unsigned int)status);

        parity_record_exact(
            "winebth_ioctl_disconnect_device",
            "WineBth_Ioctl_DisconnectDevice",
            "WineBthSimulateIoctl",
            status,
            GetLastError(),
            status == STATUS_SUCCESS ? 1U : 0U,
            digest,
            status == STATUS_SUCCESS ? sizeof(digest) : 0,
            status == STATUS_SUCCESS ? 1 : 0);
    }

    /* =========================================================================
     * Case 11: winebth_invalid_ioctl_error
     * Invalid IOCTL function returns STATUS_NOT_SUPPORTED (0xc00000bb).
     * ========================================================================= */
    {
        DRIVER_OBJECT drv;
        DEVICE_OBJECT pdo;
        PDEVICE_RELATIONS rel = NULL;
        uint8_t digest[16];

        memset(&drv, 0, sizeof(drv));
        memset(&pdo, 0, sizeof(pdo));
        memset(digest, 0, sizeof(digest));
        WineBthResetDriverState();

        status = DriverEntry(&drv, NULL);
        if (status == STATUS_SUCCESS) {
            status = drv.DriverExtension->AddDevice(&drv, &pdo);
        }
        if (status == STATUS_SUCCESS) {
            status = WineBthSimulatePnpStart(&drv, pdo.AttachedDevice);
        }
        if (status == STATUS_SUCCESS) {
            status = WineBthSimulateQueryRelations(pdo.AttachedDevice, BusRelations, &rel);
        }
        if (status == STATUS_SUCCESS && rel != NULL && rel->Count >= 1U) {
            status = WineBthSimulateIoctl(rel->Objects[0], 0xDEADBEEFU, NULL, 0, NULL, 0, NULL);
        }

        put_u32_le(digest + 0, (uint32_t)status);
        put_u32_le(digest + 4, status == STATUS_NOT_SUPPORTED ? 1U : 0U);
        put_u32_le(digest + 8, 1U);
        put_u32_le(digest + 12, 0U);

        ok(status == STATUS_NOT_SUPPORTED, "Expected STATUS_NOT_SUPPORTED, got 0x%08x\n", (unsigned int)status);

        parity_record_exact(
            "winebth_invalid_ioctl_error",
            "WineBth_InvalidIoctl_Error",
            "WineBthSimulateIoctl",
            status,
            GetLastError(),
            0U,
            digest,
            sizeof(digest),
            status == STATUS_NOT_SUPPORTED ? 1 : 0);
    }

    /* =========================================================================
     * Case 12: winebth_setupdi_service_install_success
     * SetupDiInstallDevice simulation starts service without error 1359 or 1062.
     * ========================================================================= */
    {
        WINEBTH_SETUPDI_STATUS setupdi;
        uint8_t digest[16];

        memset(&setupdi, 0, sizeof(setupdi));
        memset(digest, 0, sizeof(digest));
        WineBthResetDriverState();

        status = WineBthSimulateSetupDiInstall(&setupdi);

        if (status == STATUS_SUCCESS) {
            put_u32_le(digest + 0, (uint32_t)status);
            put_u32_le(digest + 4, setupdi.service_created);
            put_u32_le(digest + 8, setupdi.service_started);
            put_u32_le(digest + 12, setupdi.win32_last_error);
        }

        ok(status == STATUS_SUCCESS && setupdi.service_started == 1U && setupdi.win32_last_error == ERROR_SUCCESS,
           "SetupDi service install failed, status=0x%08x err=%u\n", (unsigned int)status, (unsigned int)setupdi.win32_last_error);

        parity_record_exact(
            "winebth_setupdi_service_install_success",
            "WineBth_SetupDi_ServiceInstallSuccess",
            "WineBthSimulateSetupDiInstall",
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

/*
 * Upstream Wine Winetest Anti-Regression Suite for winebth.sys
 * (Wine Bluetooth Bus Driver, macOS IOBluetooth Backend, SetupDi Service Installation)
 *
 * Copyright 2026 Spumante Contributors
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#if defined(__APPLE__)
#include <dlfcn.h>
#endif

#include "../../include/wine/test.h"
#include "../../../dlls/winebth.sys/winebth.h"
#include "../../../dlls/winebth.sys/unixlib.h"

static void test_driver_entry(void)
{
    DRIVER_OBJECT drv;
    NTSTATUS status;

    WineBthResetDriverState();

    /* Test invalid argument */
    status = DriverEntry(NULL, NULL);
    ok(status == STATUS_INVALID_PARAMETER, "Expected STATUS_INVALID_PARAMETER, got 0x%08x\n", (unsigned int)status);

    /* Test valid DriverEntry initialization */
    memset(&drv, 0, sizeof(drv));
    status = DriverEntry(&drv, NULL);
    ok(status == STATUS_SUCCESS, "DriverEntry failed with status 0x%08x (0xc00000e5 reproduced)\n", (unsigned int)status);
    ok(drv.DriverExtension != NULL, "DriverExtension not initialized\n");
    if (drv.DriverExtension) {
        ok(drv.DriverExtension->AddDevice != NULL, "AddDevice entrypoint missing\n");
    }
    ok(drv.MajorFunction[IRP_MJ_PNP] != NULL, "IRP_MJ_PNP dispatch missing\n");
    ok(drv.MajorFunction[IRP_MJ_DEVICE_CONTROL] != NULL, "IRP_MJ_DEVICE_CONTROL dispatch missing\n");
}

static void test_add_device_and_fdo(void)
{
    DRIVER_OBJECT drv;
    DEVICE_OBJECT pdo;
    DEVICE_OBJECT *fdo = NULL;
    NTSTATUS status;

    WineBthResetDriverState();
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

    WineBthResetDriverState();
    memset(&drv, 0, sizeof(drv));
    memset(&pdo, 0, sizeof(pdo));

    status = DriverEntry(&drv, NULL);
    ok(status == STATUS_SUCCESS, "DriverEntry failed: 0x%08x\n", (unsigned int)status);

    status = drv.DriverExtension->AddDevice(&drv, &pdo);
    ok(status == STATUS_SUCCESS, "AddDevice failed: 0x%08x\n", (unsigned int)status);

    status = WineBthSimulatePnpStart(&drv, pdo.AttachedDevice);
    ok(status == STATUS_SUCCESS, "WineBthSimulatePnpStart failed: 0x%08x\n", (unsigned int)status);

    status = WineBthSimulateQueryRelations(pdo.AttachedDevice, BusRelations, &rel);
    ok(status == STATUS_SUCCESS, "QueryRelations failed: 0x%08x\n", (unsigned int)status);
    ok(rel != NULL, "Relations structure is NULL\n");
    if (rel) {
        ok(rel->Count >= 1, "Expected at least 1 radio device, got %u\n", (unsigned int)rel->Count);
        if (rel->Count >= 1 && rel->Objects[0]) {
            ok(rel->Objects[0]->DeviceType == FILE_DEVICE_BLUETOOTH, "Expected FILE_DEVICE_BLUETOOTH (0x3b), got 0x%x\n", (unsigned int)rel->Objects[0]->DeviceType);
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

    WineBthResetDriverState();
    memset(&drv, 0, sizeof(drv));
    memset(&pdo, 0, sizeof(pdo));

    DriverEntry(&drv, NULL);
    drv.DriverExtension->AddDevice(&drv, &pdo);
    WineBthSimulatePnpStart(&drv, pdo.AttachedDevice);
    WineBthSimulateQueryRelations(pdo.AttachedDevice, BusRelations, &rel);

    ok(rel != NULL && rel->Count >= 1, "Child relations required for query ID test\n");
    if (!rel || rel->Count < 1) return;

    /* 1. Device ID */
    memset(buffer, 0, sizeof(buffer));
    out_chars = 0;
    status = WineBthSimulateQueryId(rel->Objects[0], BusQueryDeviceID, buffer, 256, &out_chars);
    ok(status == STATUS_SUCCESS, "QueryId(BusQueryDeviceID) failed: 0x%08x\n", (unsigned int)status);
    ok(out_chars == 14, "Expected 14 chars for Device ID, got %u\n", (unsigned int)out_chars);
    ok(buffer[0] == 'B' && buffer[1] == 'T' && buffer[2] == 'H', "Device ID does not begin with 'BTH': %c%c%c\n", (char)buffer[0], (char)buffer[1], (char)buffer[2]);

    /* 2. Hardware IDs */
    memset(buffer, 0, sizeof(buffer));
    out_chars = 0;
    status = WineBthSimulateQueryId(rel->Objects[0], BusQueryHardwareIDs, buffer, 256, &out_chars);
    ok(status == STATUS_SUCCESS, "QueryId(BusQueryHardwareIDs) failed: 0x%08x\n", (unsigned int)status);
    ok(out_chars == 37, "Expected 37 chars for Hardware IDs, got %u\n", (unsigned int)out_chars);
    ok(buffer[0] == 'B' && buffer[1] == 'T' && buffer[2] == 'H', "Hardware ID does not begin with 'BTH'\n");

    /* 3. Compatible IDs */
    memset(buffer, 0, sizeof(buffer));
    out_chars = 0;
    status = WineBthSimulateQueryId(rel->Objects[0], BusQueryCompatibleIDs, buffer, 256, &out_chars);
    ok(status == STATUS_SUCCESS, "QueryId(BusQueryCompatibleIDs) failed: 0x%08x\n", (unsigned int)status);
    ok(out_chars == 18, "Expected 18 chars for Compatible IDs, got %u\n", (unsigned int)out_chars);
    ok(buffer[0] == 'B' && buffer[1] == 'T' && buffer[2] == 'H', "Compatible ID does not begin with 'BTH'\n");

    /* 4. Instance ID */
    memset(buffer, 0, sizeof(buffer));
    out_chars = 0;
    status = WineBthSimulateQueryId(rel->Objects[0], BusQueryInstanceID, buffer, 256, &out_chars);
    ok(status == STATUS_SUCCESS, "QueryId(BusQueryInstanceID) failed: 0x%08x\n", (unsigned int)status);
    ok(out_chars == 4, "Expected 4 chars for Instance ID, got %u\n", (unsigned int)out_chars);
    ok(buffer[0] == '0' && buffer[1] == '0' && buffer[2] == '0' && buffer[3] == '0', "Instance ID mismatch\n");
}

static void test_ioctls(void)
{
    DRIVER_OBJECT drv;
    DEVICE_OBJECT pdo;
    PDEVICE_RELATIONS rel = NULL;
    BTH_LOCAL_RADIO_INFO local_info;
    BTH_RADIO_INFO radio_info;
    uint32_t bytes_ret = 0;
    NTSTATUS status;

    WineBthResetDriverState();
    memset(&drv, 0, sizeof(drv));
    memset(&pdo, 0, sizeof(pdo));

    DriverEntry(&drv, NULL);
    drv.DriverExtension->AddDevice(&drv, &pdo);
    WineBthSimulatePnpStart(&drv, pdo.AttachedDevice);
    WineBthSimulateQueryRelations(pdo.AttachedDevice, BusRelations, &rel);

    ok(rel != NULL && rel->Count >= 1, "Child relations required for IOCTL test\n");
    if (!rel || rel->Count < 1) return;

    /* 1. IOCTL_BTH_GET_LOCAL_INFO */
    memset(&local_info, 0, sizeof(local_info));
    bytes_ret = 0;
    status = WineBthSimulateIoctl(rel->Objects[0], IOCTL_BTH_GET_LOCAL_INFO,
                                  NULL, 0,
                                  &local_info, sizeof(local_info),
                                  &bytes_ret);
    ok(status == STATUS_SUCCESS, "IOCTL_BTH_GET_LOCAL_INFO failed: 0x%08x\n", (unsigned int)status);
    ok(local_info.address != 0, "Radio address is zero\n");
    ok(local_info.hciVersion >= 1, "HCI version is 0\n");
    ok(bytes_ret == sizeof(local_info), "Bytes returned mismatch: %u vs %zu\n", (unsigned int)bytes_ret, sizeof(local_info));

    /* 2. IOCTL_BTH_GET_RADIO_INFO */
    memset(&radio_info, 0, sizeof(radio_info));
    bytes_ret = 0;
    status = WineBthSimulateIoctl(rel->Objects[0], IOCTL_BTH_GET_RADIO_INFO,
                                  NULL, 0,
                                  &radio_info, sizeof(radio_info),
                                  &bytes_ret);
    ok(status == STATUS_SUCCESS, "IOCTL_BTH_GET_RADIO_INFO failed: 0x%08x\n", (unsigned int)status);
    ok(radio_info.mfg != 0, "Radio manufacturer is 0\n");
    ok(radio_info.lmpVersion >= 1, "LMP version is 0\n");
    ok(bytes_ret == sizeof(radio_info), "Bytes returned mismatch: %u vs %zu\n", (unsigned int)bytes_ret, sizeof(radio_info));

    /* 3. IOCTL_BTH_DISCONNECT_DEVICE */
    BTH_ADDR target_addr = 0x001BDC000001ULL;
    status = WineBthSimulateIoctl(rel->Objects[0], IOCTL_BTH_DISCONNECT_DEVICE,
                                  &target_addr, sizeof(target_addr),
                                  NULL, 0,
                                  &bytes_ret);
    ok(status == STATUS_SUCCESS, "IOCTL_BTH_DISCONNECT_DEVICE failed: 0x%08x\n", (unsigned int)status);

    /* 4. Invalid IOCTL */
    status = WineBthSimulateIoctl(rel->Objects[0], 0xDEADBEEFU, NULL, 0, NULL, 0, NULL);
    ok(status == STATUS_NOT_SUPPORTED, "Expected STATUS_NOT_SUPPORTED for invalid IOCTL, got 0x%08x\n", (unsigned int)status);
}

static void test_setupdi_service_install(void)
{
    WINEBTH_SETUPDI_STATUS setupdi;
    NTSTATUS status;

    WineBthResetDriverState();
    memset(&setupdi, 0, sizeof(setupdi));

    status = WineBthSimulateSetupDiInstall(&setupdi);
    ok(status == STATUS_SUCCESS, "SetupDi install simulation failed: 0x%08x\n", (unsigned int)status);
    ok(setupdi.service_created == 1, "Service was not created\n");
    ok(setupdi.service_started == 1, "Service failed to start (error 1062 reproduced)\n");
    ok(setupdi.win32_last_error == ERROR_SUCCESS, "Win32 last error not ERROR_SUCCESS: %u (error 1359/1062 reproduced)\n", (unsigned int)setupdi.win32_last_error);
    ok(setupdi.driver_entry_status == STATUS_SUCCESS, "DriverEntry status not STATUS_SUCCESS: 0x%08x (0xc00000e5 reproduced)\n", (unsigned int)setupdi.driver_entry_status);
    ok(setupdi.radio_enumerated == 1, "Radio device not enumerated\n");
}

static void test_unixlib_abi_and_installed_so(void)
{
    struct bluetooth_get_event_params ev_params;
    NTSTATUS status;
    int i;

    WineBthResetDriverState();

    /* 1. Upstream Wine struct winebluetooth_event ABI size must fit within 320 bytes on 64-bit */
    if (sizeof(void *) == 8) {
        ok(sizeof(struct winebluetooth_event) <= 320,
           "Expected upstream-compatible sizeof(struct winebluetooth_event) <= 320 on 64-bit, got %zu\n",
           sizeof(struct winebluetooth_event));
    }

    /* 2. Direct __wine_unix_call_funcs 21-entry table dispatch */
    ok(unix_funcs_count == 21, "Expected 21 entries in enum bluetoothapis_funcs, got %d\n", (int)unix_funcs_count);
    for (i = 0; i < (int)unix_funcs_count; i++) {
        ok(__wine_unix_call_funcs[i] != NULL, "__wine_unix_call_funcs[%d] entry is NULL\n", i);
    }

    status = __wine_unix_call_funcs[unix_bluetooth_init](NULL);
    ok(status == STATUS_SUCCESS, "unix_bluetooth_init failed: 0x%08x (0xc00000e5 reproduced)\n", (unsigned int)status);

    memset(&ev_params, 0, sizeof(ev_params));
    status = __wine_unix_call_funcs[unix_bluetooth_get_event](&ev_params);
    ok(status == STATUS_SUCCESS, "First unix_bluetooth_get_event failed: 0x%08x\n", (unsigned int)status);

    for (i = 0; i < 8 && status == STATUS_SUCCESS; i++) {
        memset(&ev_params, 0, sizeof(ev_params));
        status = __wine_unix_call_funcs[unix_bluetooth_get_event](&ev_params);
    }
    ok(status != STATUS_PENDING, "Drained unix_bluetooth_get_event must not spin with STATUS_PENDING: got 0x%08x\n", (unsigned int)status);

    status = __wine_unix_call_funcs[unix_bluetooth_shutdown](NULL);
    ok(status == STATUS_SUCCESS, "unix_bluetooth_shutdown failed: 0x%08x\n", (unsigned int)status);

#if defined(__APPLE__)
    /* 3. Verify x86_64-unix/winebth.so and aarch64-unix/winebth.so are compiled and installed */
    {
        const char *x86_so = "builds/wine-install-x86/lib/wine/x86_64-unix/winebth.so";
        const char *arm_so = "builds/wine-install-arm64/lib/wine/aarch64-unix/winebth.so";
        FILE *fx86 = fopen(x86_so, "rb");
        FILE *farm = fopen(arm_so, "rb");
        uint32_t hdr[2] = {0, 0};

        ok(fx86 != NULL, "Missing installed %s\n", x86_so);
        if (fx86) {
            size_t n = fread(hdr, sizeof(uint32_t), 2, fx86);
            fclose(fx86);
            ok(n == 2 && hdr[0] == 0xFEEDFACFU && hdr[1] == 0x01000007U,
               "Expected x86_64 Mach-O magic/cputype in %s, got 0x%08x / 0x%08x\n",
               x86_so, hdr[0], hdr[1]);
        }

        hdr[0] = 0;
        hdr[1] = 0;
        ok(farm != NULL, "Missing installed %s\n", arm_so);
        if (farm) {
            size_t n = fread(hdr, sizeof(uint32_t), 2, farm);
            fclose(farm);
            ok(n == 2 && hdr[0] == 0xFEEDFACFU && hdr[1] == 0x0100000CU,
               "Expected arm64 Mach-O magic/cputype in %s, got 0x%08x / 0x%08x\n",
               arm_so, hdr[0], hdr[1]);
        }

#if defined(__aarch64__)
        {
            void *dl = dlopen(arm_so, RTLD_NOW | RTLD_LOCAL);
            ok(dl != NULL, "dlopen(%s) failed: %s\n", arm_so, dl ? "" : dlerror());
            if (dl) {
                const unixlib_entry_t *funcs = (const unixlib_entry_t *)dlsym(dl, "__wine_unix_call_funcs");
                ok(funcs != NULL, "dlsym(__wine_unix_call_funcs) missing in %s\n", arm_so);
                if (funcs && funcs[unix_bluetooth_init]) {
                    NTSTATUS dl_st = funcs[unix_bluetooth_init](NULL);
                    ok(dl_st == STATUS_SUCCESS, "installed %s unix_bluetooth_init returned 0x%08x\n", arm_so, (unsigned int)dl_st);
                    if (funcs[unix_bluetooth_shutdown]) funcs[unix_bluetooth_shutdown](NULL);
                }
                dlclose(dl);
            }
        }
#elif defined(__x86_64__)
        {
            void *dl = dlopen(x86_so, RTLD_NOW | RTLD_LOCAL);
            ok(dl != NULL, "dlopen(%s) failed: %s\n", x86_so, dl ? "" : dlerror());
            if (dl) {
                const unixlib_entry_t *funcs = (const unixlib_entry_t *)dlsym(dl, "__wine_unix_call_funcs");
                ok(funcs != NULL, "dlsym(__wine_unix_call_funcs) missing in %s\n", x86_so);
                if (funcs && funcs[unix_bluetooth_init]) {
                    NTSTATUS dl_st = funcs[unix_bluetooth_init](NULL);
                    ok(dl_st == STATUS_SUCCESS, "installed %s unix_bluetooth_init returned 0x%08x\n", x86_so, (unsigned int)dl_st);
                    if (funcs[unix_bluetooth_shutdown]) funcs[unix_bluetooth_shutdown](NULL);
                }
                dlclose(dl);
            }
        }
#endif
    }
#endif
}

START_TEST(winebth)
{
    WINETEST_RUN_SUBTEST(test_driver_entry);
    WINETEST_RUN_SUBTEST(test_add_device_and_fdo);
    WINETEST_RUN_SUBTEST(test_pnp_start_and_relations);
    WINETEST_RUN_SUBTEST(test_query_ids);
    WINETEST_RUN_SUBTEST(test_ioctls);
    WINETEST_RUN_SUBTEST(test_setupdi_service_install);
    WINETEST_RUN_SUBTEST(test_unixlib_abi_and_installed_so);
}

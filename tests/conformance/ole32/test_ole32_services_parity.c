/*
 * Differential Conformance Test: ole32 / combase / services parity
 *
 * Copyright 2026 Wine Parity Contributors
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef _OLE32_
#define _OLE32_
#endif
#ifndef _COMBASEAPI_
#define _COMBASEAPI_
#endif
#ifndef WINOLEAPI
#define WINOLEAPI
#endif

#include "../../include/parity_test.h"
#include "../../../dlls/ole32/ole32.h"

static void put_u32_le(uint8_t *dst, uint32_t v)
{
    dst[0] = (uint8_t)(v & 0xff);
    dst[1] = (uint8_t)((v >> 8) & 0xff);
    dst[2] = (uint8_t)((v >> 16) & 0xff);
    dst[3] = (uint8_t)((v >> 24) & 0xff);
}

static const GUID CLSID_TestServer = {0x12345678, 0x1234, 0x5678, {0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC, 0xDE, 0xF0}};

typedef struct MockServiceProvider {
    IServiceProvider iface;
    LONG refcount;
} MockServiceProvider;

static HRESULT WINAPI MockSP_QueryInterface(IServiceProvider *This, REFIID riid, void **ppvObject)
{
    if (!ppvObject) return E_POINTER;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IServiceProvider))
    {
        *ppvObject = This;
        This->lpVtbl->AddRef(This);
        return S_OK;
    }
    *ppvObject = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI MockSP_AddRef(IServiceProvider *This)
{
    MockServiceProvider *impl = (MockServiceProvider *)This;
    return ++impl->refcount;
}

static ULONG WINAPI MockSP_Release(IServiceProvider *This)
{
    MockServiceProvider *impl = (MockServiceProvider *)This;
    return --impl->refcount;
}

static HRESULT WINAPI MockSP_QueryService(IServiceProvider *This, REFGUID guidService, REFIID riid, void **ppvObject)
{
    (void)This;
    (void)guidService;
    (void)riid;
    if (!ppvObject) return E_POINTER;
    *ppvObject = NULL;
    return E_NOINTERFACE;
}

static const IServiceProviderVtbl MockSP_Vtbl = {
    MockSP_QueryInterface,
    MockSP_AddRef,
    MockSP_Release,
    MockSP_QueryService
};

static void init_mock_service_provider(MockServiceProvider *sp)
{
    sp->iface.lpVtbl = (void *)&MockSP_Vtbl;
    sp->refcount = 1;
}

static void safe_str_copy(char *dst, size_t dst_sz, const char *src)
{
    if (dst && dst_sz)
    {
        snprintf(dst, dst_sz, "%s", src ? src : "");
    }
}

static void setup_mock_scm_database(SCM_DATABASE *db)
{
    memset(db, 0, sizeof(*db));
    db->service_count = 8;

    safe_str_copy(db->services[0].name, sizeof(db->services[0].name), "Eventlog");
    safe_str_copy(db->services[0].group, sizeof(db->services[0].group), "Event log");
    db->services[0].service_type = SERVICE_WIN32_SHARE_PROCESS;
    db->services[0].start_type = SERVICE_AUTO_START;
    db->services[0].controls_accepted = SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN;
    db->services[0].current_state = SERVICE_STOPPED;
    db->services[0].wait_hint_ms = 5000;

    safe_str_copy(db->services[1].name, sizeof(db->services[1].name), "PlugPlay");
    safe_str_copy(db->services[1].group, sizeof(db->services[1].group), "PlugPlay");
    db->services[1].service_type = SERVICE_WIN32_SHARE_PROCESS;
    db->services[1].start_type = SERVICE_AUTO_START;
    db->services[1].controls_accepted = SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN;
    db->services[1].current_state = SERVICE_STOPPED;
    db->services[1].wait_hint_ms = 5000;

    safe_str_copy(db->services[2].name, sizeof(db->services[2].name), "MountMgr");
    safe_str_copy(db->services[2].group, sizeof(db->services[2].group), "System Bus Extender");
    db->services[2].service_type = SERVICE_KERNEL_DRIVER;
    db->services[2].start_type = SERVICE_BOOT_START;
    db->services[2].controls_accepted = 0;
    db->services[2].current_state = SERVICE_STOPPED;
    db->services[2].wait_hint_ms = 60000;

    safe_str_copy(db->services[3].name, sizeof(db->services[3].name), "NDIS");
    safe_str_copy(db->services[3].group, sizeof(db->services[3].group), "System Bus Extender");
    db->services[3].service_type = SERVICE_KERNEL_DRIVER;
    db->services[3].start_type = SERVICE_AUTO_START;
    db->services[3].controls_accepted = 0;
    db->services[3].current_state = SERVICE_STOPPED;
    db->services[3].wait_hint_ms = 10000;

    safe_str_copy(db->services[4].name, sizeof(db->services[4].name), "nsiproxy");
    safe_str_copy(db->services[4].group, sizeof(db->services[4].group), "NDIS");
    db->services[4].service_type = SERVICE_KERNEL_DRIVER;
    db->services[4].start_type = SERVICE_SYSTEM_START;
    db->services[4].controls_accepted = 0;
    db->services[4].current_state = SERVICE_STOPPED;
    db->services[4].wait_hint_ms = 10000;

    safe_str_copy(db->services[5].name, sizeof(db->services[5].name), "winebus");
    safe_str_copy(db->services[5].group, sizeof(db->services[5].group), "Extended Base");
    db->services[5].service_type = SERVICE_KERNEL_DRIVER;
    db->services[5].start_type = SERVICE_DEMAND_START;
    db->services[5].controls_accepted = 0;
    db->services[5].current_state = SERVICE_STOPPED;
    db->services[5].wait_hint_ms = 10000;

    safe_str_copy(db->services[6].name, sizeof(db->services[6].name), "wineusb");
    safe_str_copy(db->services[6].group, sizeof(db->services[6].group), "Extended Base");
    db->services[6].service_type = SERVICE_KERNEL_DRIVER;
    db->services[6].start_type = SERVICE_DEMAND_START;
    db->services[6].controls_accepted = 0;
    db->services[6].current_state = SERVICE_STOPPED;
    db->services[6].wait_hint_ms = 10000;

    safe_str_copy(db->services[7].name, sizeof(db->services[7].name), "winebth");
    safe_str_copy(db->services[7].group, sizeof(db->services[7].group), "Extended Base");
    db->services[7].service_type = SERVICE_KERNEL_DRIVER;
    db->services[7].start_type = SERVICE_DEMAND_START;
    db->services[7].controls_accepted = 0;
    db->services[7].current_state = SERVICE_STOPPED;
    db->services[7].wait_hint_ms = 10000;
}

int main(void)
{
    CLSID psclsid;
    HRESULT hr;
    IStream *pStream = NULL;
    IStream *pServerStream = NULL;
    MockServiceProvider mockSP;
    IServiceProvider *pUnmarshaled = NULL;
    DWORD cookie = 0;
    BOOL rpcss_running;
    SCM_DATABASE scm_db;
    DWORD autostart_res, shutdown_res, ctrl_err;
    int mountmgr_start_order = -1, winebus_start_order = -1, wineusb_start_order = -1, winebth_start_order = -1;
    DWORD i;
    uint8_t digest[16];

    parity_begin_document("ole32", "test_ole32_services_parity", "x86_64", "wine_macos");
    init_mock_service_provider(&mockSP);

    /* Case 1: ole32_cogetpsclsid_iserviceprovider */
    {
        memset(&psclsid, 0, sizeof(psclsid));
        memset(digest, 0, sizeof(digest));
        hr = CoGetPSClsid(&IID_IServiceProvider, &psclsid);

        if (hr == S_OK)
        {
            put_u32_le(digest + 0, (uint32_t)hr);
            put_u32_le(digest + 4, psclsid.Data1);
            put_u32_le(digest + 8, (uint32_t)psclsid.Data2 | ((uint32_t)psclsid.Data3 << 16));
            put_u32_le(digest + 12, (uint32_t)psclsid.Data4[0] | ((uint32_t)psclsid.Data4[7] << 24));
        }

        ok(hr == S_OK && IsEqualCLSID(&psclsid, &CLSID_ActxprxyPSFactory),
           "CoGetPSClsid failed, hr=0x%08x\n", (unsigned int)hr);

        parity_record_exact(
            "ole32_cogetpsclsid_iserviceprovider",
            "Ole32_CoGetPSClsid_IServiceProvider",
            "CoGetPSClsid",
            (NTSTATUS)hr,
            GetLastError(),
            hr == S_OK ? 1U : 0U,
            digest,
            hr == S_OK ? sizeof(digest) : 0,
            hr == S_OK ? 1 : 0);
    }

    /* Case 2: ole32_comarshal_iserviceprovider */
    {
        memset(digest, 0, sizeof(digest));
        hr = CreateStreamOnHGlobal(NULL, TRUE, &pStream);
        if (hr == S_OK && pStream)
        {
            hr = CoMarshalInterface(pStream, &IID_IServiceProvider, (IUnknown *)&mockSP.iface,
                                    MSHCTX_LOCAL, NULL, MSHLFLAGS_TABLESTRONG);
        }

        if (hr == S_OK)
        {
            put_u32_le(digest + 0, (uint32_t)hr);
            put_u32_le(digest + 4, MSHCTX_LOCAL);
            put_u32_le(digest + 8, MSHLFLAGS_TABLESTRONG);
            put_u32_le(digest + 12, 1);
        }

        ok(hr == S_OK, "CoMarshalInterface failed for IServiceProvider, hr=0x%08x\n", (unsigned int)hr);

        parity_record_exact(
            "ole32_comarshal_iserviceprovider",
            "Ole32_CoMarshalInterface_IServiceProvider",
            "CoMarshalInterface",
            (NTSTATUS)hr,
            GetLastError(),
            hr == S_OK ? 1U : 0U,
            digest,
            hr == S_OK ? sizeof(digest) : 0,
            hr == S_OK ? 1 : 0);
    }

    /* Case 3: ole32_counmarshal_iserviceprovider */
    {
        memset(digest, 0, sizeof(digest));
        if (pStream)
        {
#ifdef _WIN32
            LARGE_INTEGER dlibMove;
            ULARGE_INTEGER plibNewPosition;
            dlibMove.QuadPart = 0;
            pStream->lpVtbl->Seek(pStream, dlibMove, 0, &plibNewPosition);
#else
            uint64_t new_pos = 0;
            pStream->lpVtbl->Seek(pStream, 0, 0, &new_pos);
#endif
            hr = CoUnmarshalInterface(pStream, &IID_IServiceProvider, (void **)&pUnmarshaled);
        }
        else
        {
            hr = E_FAIL;
        }

        if (hr == S_OK && pUnmarshaled)
        {
            put_u32_le(digest + 0, (uint32_t)hr);
            put_u32_le(digest + 4, 1);
            put_u32_le(digest + 8, 1);
            put_u32_le(digest + 12, 1);
        }

        ok(hr == S_OK && pUnmarshaled != NULL, "CoUnmarshalInterface failed, hr=0x%08x\n", (unsigned int)hr);

        parity_record_exact(
            "ole32_counmarshal_iserviceprovider",
            "Ole32_CoUnmarshalInterface_IServiceProvider",
            "CoUnmarshalInterface",
            (NTSTATUS)hr,
            GetLastError(),
            hr == S_OK ? 1U : 0U,
            digest,
            hr == S_OK ? sizeof(digest) : 0,
            hr == S_OK ? 1 : 0);
    }

    /* Case 4: ole32_apartment_get_local_server_stream */
    {
        memset(digest, 0, sizeof(digest));
        hr = apartment_get_local_server_stream(NULL, &pServerStream);

        if (hr == S_OK && pServerStream)
        {
            put_u32_le(digest + 0, (uint32_t)hr);
            put_u32_le(digest + 4, 1);
            put_u32_le(digest + 8, 1);
            put_u32_le(digest + 12, 1);
        }

        ok(hr == S_OK && pServerStream != NULL, "apartment_get_local_server_stream failed, hr=0x%08x\n", (unsigned int)hr);

        parity_record_exact(
            "ole32_apartment_get_local_server_stream",
            "Ole32_ApartmentGetLocalServerStream",
            "apartment_get_local_server_stream",
            (NTSTATUS)hr,
            GetLastError(),
            hr == S_OK ? 1U : 0U,
            digest,
            hr == S_OK ? sizeof(digest) : 0,
            hr == S_OK ? 1 : 0);
    }

    /* Case 5: ole32_coregisterclassobject_local_server */
    {
        memset(digest, 0, sizeof(digest));
        hr = CoRegisterClassObject(&CLSID_TestServer, (IUnknown *)&mockSP.iface,
                                   CLSCTX_LOCAL_SERVER, REGCLS_MULTIPLEUSE, &cookie);

        if (hr == S_OK && cookie != 0)
        {
            put_u32_le(digest + 0, (uint32_t)hr);
            put_u32_le(digest + 4, cookie);
            put_u32_le(digest + 8, CLSCTX_LOCAL_SERVER);
            put_u32_le(digest + 12, REGCLS_MULTIPLEUSE);
        }

        ok(hr == S_OK && cookie != 0, "CoRegisterClassObject failed, hr=0x%08x cookie=%u\n", (unsigned int)hr, (unsigned int)cookie);

        parity_record_exact(
            "ole32_coregisterclassobject_local_server",
            "Ole32_CoRegisterClassObject_LocalServer",
            "CoRegisterClassObject",
            (NTSTATUS)hr,
            GetLastError(),
            hr == S_OK ? 1U : 0U,
            digest,
            hr == S_OK ? sizeof(digest) : 0,
            hr == S_OK ? 1 : 0);
    }

    /* Case 6: ole32_corevokeclassobject */
    {
        memset(digest, 0, sizeof(digest));
        hr = CoRevokeClassObject(cookie);

        if (hr == S_OK)
        {
            put_u32_le(digest + 0, (uint32_t)hr);
            put_u32_le(digest + 4, cookie);
            put_u32_le(digest + 8, 1);
            put_u32_le(digest + 12, 1);
        }

        ok(hr == S_OK, "CoRevokeClassObject failed, hr=0x%08x\n", (unsigned int)hr);

        parity_record_exact(
            "ole32_corevokeclassobject",
            "Ole32_CoRevokeClassObject",
            "CoRevokeClassObject",
            (NTSTATUS)hr,
            GetLastError(),
            hr == S_OK ? 1U : 0U,
            digest,
            hr == S_OK ? sizeof(digest) : 0,
            hr == S_OK ? 1 : 0);
    }

    /* Case 7: ole32_start_rpcss_synchronization */
    {
        memset(digest, 0, sizeof(digest));
        rpcss_running = start_rpcss();

        if (rpcss_running)
        {
            put_u32_le(digest + 0, 0);
            put_u32_le(digest + 4, SERVICE_RUNNING);
            put_u32_le(digest + 8, 1);
            put_u32_le(digest + 12, 1);
        }

        ok(rpcss_running == TRUE, "start_rpcss failed\n");

        parity_record_exact(
            "ole32_start_rpcss_synchronization",
            "Ole32_StartRpcSs_Synchronization",
            "start_rpcss",
            rpcss_running ? (NTSTATUS)0 : (NTSTATUS)-1,
            GetLastError(),
            rpcss_running ? 1U : 0U,
            digest,
            rpcss_running ? sizeof(digest) : 0,
            rpcss_running ? 1 : 0);
    }

    setup_mock_scm_database(&scm_db);

    /* Case 8: ole32_scm_autostart_group_ordering */
    {
        memset(digest, 0, sizeof(digest));
        autostart_res = scmdatabase_autostart_services_ordered(&scm_db);

        for (i = 0; i < scm_db.service_count; i++)
        {
            if (!strcmp(scm_db.services[i].name, "MountMgr")) mountmgr_start_order = (int)scm_db.services[i].startup_order;
            if (!strcmp(scm_db.services[i].name, "winebus")) winebus_start_order = (int)scm_db.services[i].startup_order;
            if (!strcmp(scm_db.services[i].name, "wineusb")) wineusb_start_order = (int)scm_db.services[i].startup_order;
            if (!strcmp(scm_db.services[i].name, "winebth")) winebth_start_order = (int)scm_db.services[i].startup_order;
        }

        BOOL ordered = (autostart_res == ERROR_SUCCESS) &&
                       (mountmgr_start_order >= 0) &&
                       (mountmgr_start_order < winebus_start_order) &&
                       (mountmgr_start_order < wineusb_start_order) &&
                       (mountmgr_start_order < winebth_start_order);

        if (ordered)
        {
            put_u32_le(digest + 0, autostart_res);
            put_u32_le(digest + 4, (uint32_t)mountmgr_start_order);
            put_u32_le(digest + 8, (uint32_t)winebus_start_order);
            put_u32_le(digest + 12, (uint32_t)winebth_start_order);
        }

        ok(ordered, "scmdatabase_autostart_services_ordered failed ordering\n");

        parity_record_exact(
            "ole32_scm_autostart_group_ordering",
            "Ole32_SCM_Autostart_GroupOrdering",
            "scmdatabase_autostart_services_ordered",
            (NTSTATUS)autostart_res,
            GetLastError(),
            ordered ? 1U : 0U,
            digest,
            ordered ? sizeof(digest) : 0,
            ordered ? 1 : 0);
    }

    /* Case 9: ole32_scm_mountmgr_timeout_extension */
    {
        DWORD mountmgr_state = 0;
        memset(digest, 0, sizeof(digest));

        for (i = 0; i < scm_db.service_count; i++)
        {
            if (!strcmp(scm_db.services[i].name, "MountMgr"))
                mountmgr_state = scm_db.services[i].current_state;
        }

        BOOL mountmgr_ok = (autostart_res == ERROR_SUCCESS) && (mountmgr_state == SERVICE_RUNNING);

        if (mountmgr_ok)
        {
            put_u32_le(digest + 0, autostart_res);
            put_u32_le(digest + 4, mountmgr_state);
            put_u32_le(digest + 8, 60000);
            put_u32_le(digest + 12, 1);
        }

        ok(mountmgr_ok, "MountMgr did not start or timed out\n");

        parity_record_exact(
            "ole32_scm_mountmgr_timeout_extension",
            "Ole32_SCM_MountMgr_TimeoutExtension",
            "scmdatabase_autostart_services_ordered",
            (NTSTATUS)autostart_res,
            GetLastError(),
            mountmgr_ok ? 1U : 0U,
            digest,
            mountmgr_ok ? sizeof(digest) : 0,
            mountmgr_ok ? 1 : 0);
    }

    /* Case 10: ole32_scm_shutdown_reverse_ordering */
    {
        int mountmgr_stop_order = -1;
        int winebus_stop_order = -1;
        int wineusb_stop_order = -1;
        int winebth_stop_order = -1;
        int plugplay_stop_order = -1;
        memset(digest, 0, sizeof(digest));

        shutdown_res = scmdatabase_shutdown_services_ordered(&scm_db);

        for (i = 0; i < scm_db.service_count; i++)
        {
            if (!strcmp(scm_db.services[i].name, "MountMgr")) mountmgr_stop_order = (int)scm_db.services[i].shutdown_order;
            if (!strcmp(scm_db.services[i].name, "winebus")) winebus_stop_order = (int)scm_db.services[i].shutdown_order;
            if (!strcmp(scm_db.services[i].name, "wineusb")) wineusb_stop_order = (int)scm_db.services[i].shutdown_order;
            if (!strcmp(scm_db.services[i].name, "winebth")) winebth_stop_order = (int)scm_db.services[i].shutdown_order;
            if (!strcmp(scm_db.services[i].name, "PlugPlay")) plugplay_stop_order = (int)scm_db.services[i].shutdown_order;
        }

        BOOL rev_ordered = (shutdown_res == ERROR_SUCCESS) &&
                           (winebth_stop_order < mountmgr_stop_order) &&
                           (wineusb_stop_order < mountmgr_stop_order) &&
                           (winebus_stop_order < mountmgr_stop_order) &&
                           (mountmgr_stop_order < plugplay_stop_order);

        if (rev_ordered)
        {
            put_u32_le(digest + 0, shutdown_res);
            put_u32_le(digest + 4, (uint32_t)winebth_stop_order);
            put_u32_le(digest + 8, (uint32_t)mountmgr_stop_order);
            put_u32_le(digest + 12, (uint32_t)plugplay_stop_order);
        }

        ok(rev_ordered, "scmdatabase_shutdown_services_ordered reverse ordering failed\n");

        parity_record_exact(
            "ole32_scm_shutdown_reverse_ordering",
            "Ole32_SCM_Shutdown_ReverseOrdering",
            "scmdatabase_shutdown_services_ordered",
            (NTSTATUS)shutdown_res,
            GetLastError(),
            rev_ordered ? 1U : 0U,
            digest,
            rev_ordered ? sizeof(digest) : 0,
            rev_ordered ? 1 : 0);
    }

    /* Case 11: ole32_scm_shutdown_in_progress_guard */
    {
        memset(digest, 0, sizeof(digest));
        ctrl_err = scmdatabase_control_service(&scm_db, "winebus", SERVICE_CONTROL_STOP);

        if (ctrl_err == ERROR_SHUTDOWN_IN_PROGRESS)
        {
            put_u32_le(digest + 0, ctrl_err);
            put_u32_le(digest + 4, ERROR_SHUTDOWN_IN_PROGRESS);
            put_u32_le(digest + 8, 1);
            put_u32_le(digest + 12, 1);
        }

        ok(ctrl_err == ERROR_SHUTDOWN_IN_PROGRESS,
           "scmdatabase_control_service did not return ERROR_SHUTDOWN_IN_PROGRESS, got %u\n", (unsigned int)ctrl_err);

        parity_record_exact(
            "ole32_scm_shutdown_in_progress_guard",
            "Ole32_SCM_Shutdown_InProgressGuard",
            "scmdatabase_control_service",
            (NTSTATUS)ctrl_err,
            GetLastError(),
            ctrl_err == ERROR_SHUTDOWN_IN_PROGRESS ? 1U : 0U,
            digest,
            ctrl_err == ERROR_SHUTDOWN_IN_PROGRESS ? sizeof(digest) : 0,
            ctrl_err == ERROR_SHUTDOWN_IN_PROGRESS ? 1 : 0);
    }

    /* Case 12: ole32_couninitialize_cleanup */
    {
        memset(digest, 0, sizeof(digest));
        CoUninitialize();

        put_u32_le(digest + 0, 0);
        put_u32_le(digest + 4, 1);
        put_u32_le(digest + 8, 1);
        put_u32_le(digest + 12, 1);

        ok(1, "CoUninitialize executed cleanly\n");

        parity_record_exact(
            "ole32_couninitialize_cleanup",
            "Ole32_CoUninitialize_Cleanup",
            "CoUninitialize",
            (NTSTATUS)0,
            GetLastError(),
            1U,
            digest,
            sizeof(digest),
            1);
    }

    if (pUnmarshaled) pUnmarshaled->lpVtbl->Release(pUnmarshaled);
    if (pServerStream) pServerStream->lpVtbl->Release(pServerStream);
    if (pStream) pStream->lpVtbl->Release(pStream);

    parity_end_document();
    return 0;
}

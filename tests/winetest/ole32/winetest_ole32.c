/*
 * Upstream Wine Winetest Anti-Regression Suite for ole32
 * (COM initialization, IServiceProvider marshaling, RpcSs synchronization, SCM service ordering)
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

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "../../include/wine/test.h"
#include "../../../dlls/ole32/ole32.h"

static const GUID CLSID_TestClass = {0x87654321, 0x4321, 0x8765, {0xAB, 0xCD, 0xEF, 0x01, 0x23, 0x45, 0x67, 0x89}};
static const GUID IID_TestCustom = {0x11223344, 0x5566, 0x7788, {0x99, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x00}};
static const GUID CLSID_TestCustomPS = {0xAABBCCDD, 0xEEFF, 0x1122, {0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xAA}};

/* Mock test object implementing IServiceProvider */
typedef struct MockWinetestSP {
    IServiceProvider iface;
    LONG refcount;
} MockWinetestSP;

static HRESULT WINAPI WinetestSP_QueryInterface(IServiceProvider *This, REFIID riid, void **ppvObject)
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

static ULONG WINAPI WinetestSP_AddRef(IServiceProvider *This)
{
    MockWinetestSP *impl = (MockWinetestSP *)This;
    return ++impl->refcount;
}

static ULONG WINAPI WinetestSP_Release(IServiceProvider *This)
{
    MockWinetestSP *impl = (MockWinetestSP *)This;
    return --impl->refcount;
}

static HRESULT WINAPI WinetestSP_QueryService(IServiceProvider *This, REFGUID guidService, REFIID riid, void **ppvObject)
{
    (void)This;
    (void)guidService;
    (void)riid;
    if (!ppvObject) return E_POINTER;
    *ppvObject = NULL;
    return E_NOINTERFACE;
}

static const IServiceProviderVtbl WinetestSP_Vtbl = {
    WinetestSP_QueryInterface,
    WinetestSP_AddRef,
    WinetestSP_Release,
    WinetestSP_QueryService
};

static void init_mock_winetest_sp(MockWinetestSP *sp)
{
    sp->iface.lpVtbl = (void *)&WinetestSP_Vtbl;
    sp->refcount = 1;
}

static void test_co_initialize(void)
{
    HRESULT hr;

    hr = CoInitialize(NULL);
    ok(hr == S_OK, "CoInitialize failed, hr=0x%08x\n", (unsigned int)hr);

    hr = CoInitializeEx(NULL, 0);
    ok(hr == S_OK, "CoInitializeEx failed, hr=0x%08x\n", (unsigned int)hr);

    CoUninitialize();
    ok(1, "CoUninitialize executed cleanly\n");
}

static void test_ps_clsid_registration(void)
{
    CLSID clsid;
    HRESULT hr;

    /* 1. Query invalid args */
    hr = CoGetPSClsid(NULL, &clsid);
    ok(hr == E_INVALIDARG, "Expected E_INVALIDARG, got 0x%08x\n", (unsigned int)hr);

    hr = CoGetPSClsid(&IID_IServiceProvider, NULL);
    ok(hr == E_INVALIDARG, "Expected E_INVALIDARG, got 0x%08x\n", (unsigned int)hr);

    /* 2. Query built-in Actxprxy proxy/stub for IID_IServiceProvider */
    memset(&clsid, 0, sizeof(clsid));
    hr = CoGetPSClsid(&IID_IServiceProvider, &clsid);
    ok(hr == S_OK, "CoGetPSClsid failed for IID_IServiceProvider, hr=0x%08x\n", (unsigned int)hr);
    ok(IsEqualCLSID(&clsid, &CLSID_ActxprxyPSFactory), "Expected CLSID_ActxprxyPSFactory\n");

    /* 3. Query built-in IClassFactory */
    memset(&clsid, 0, sizeof(clsid));
    hr = CoGetPSClsid(&IID_IClassFactory, &clsid);
    ok(hr == S_OK, "CoGetPSClsid failed for IID_IClassFactory, hr=0x%08x\n", (unsigned int)hr);

    /* 4. Query unregistered IID */
    hr = CoGetPSClsid(&IID_TestCustom, &clsid);
    ok(hr == REGDB_E_IIDNOTREG, "Expected REGDB_E_IIDNOTREG, got 0x%08x\n", (unsigned int)hr);

    /* 5. Register custom PS and query back */
    hr = CoRegisterPSClsid(&IID_TestCustom, &CLSID_TestCustomPS);
    ok(hr == S_OK, "CoRegisterPSClsid failed, hr=0x%08x\n", (unsigned int)hr);

    memset(&clsid, 0, sizeof(clsid));
    hr = CoGetPSClsid(&IID_TestCustom, &clsid);
    ok(hr == S_OK, "CoGetPSClsid failed after registration, hr=0x%08x\n", (unsigned int)hr);
    ok(IsEqualCLSID(&clsid, &CLSID_TestCustomPS), "CLSID mismatch after registration\n");
}

static void test_stream_on_hglobal(void)
{
    IStream *pStream = NULL;
    IStream *pClone = NULL;
    HRESULT hr;
    ULONG written = 0, bytes_read = 0;
    const char test_data[] = "WineParityCOMStreamTestPayload";
    char read_buf[64];

    hr = CreateStreamOnHGlobal(NULL, TRUE, NULL);
    ok(hr == E_POINTER, "Expected E_POINTER, got 0x%08x\n", (unsigned int)hr);

    hr = CreateStreamOnHGlobal(NULL, TRUE, &pStream);
    ok(hr == S_OK && pStream != NULL, "CreateStreamOnHGlobal failed, hr=0x%08x\n", (unsigned int)hr);

    hr = pStream->lpVtbl->Write(pStream, test_data, (ULONG)strlen(test_data), &written);
    ok(hr == S_OK && written == (ULONG)strlen(test_data), "Write failed, hr=0x%08x written=%u\n", (unsigned int)hr, (unsigned int)written);

#ifdef _WIN32
    LARGE_INTEGER dlibMove;
    ULARGE_INTEGER plibNewPosition;
    dlibMove.QuadPart = 0;
    hr = pStream->lpVtbl->Seek(pStream, dlibMove, 0, &plibNewPosition);
#else
    uint64_t new_pos = 0;
    hr = pStream->lpVtbl->Seek(pStream, 0, 0, &new_pos);
#endif
    ok(hr == S_OK, "Seek failed, hr=0x%08x\n", (unsigned int)hr);

    memset(read_buf, 0, sizeof(read_buf));
    hr = pStream->lpVtbl->Read(pStream, read_buf, sizeof(read_buf) - 1, &bytes_read);
    ok(hr == S_OK && bytes_read == (ULONG)strlen(test_data), "Read failed, hr=0x%08x read=%u\n", (unsigned int)hr, (unsigned int)bytes_read);
    ok(!strcmp(read_buf, test_data), "Payload mismatch: %s\n", read_buf);

    hr = pStream->lpVtbl->Clone(pStream, &pClone);
    ok(hr == S_OK && pClone != NULL, "Clone failed, hr=0x%08x\n", (unsigned int)hr);

    pClone->lpVtbl->Release(pClone);
    pStream->lpVtbl->Release(pStream);
}

static void test_interface_marshaling(void)
{
    IStream *pStream = NULL;
    MockWinetestSP sp;
    IServiceProvider *pUnmarshaled = NULL;
    HRESULT hr;

    init_mock_winetest_sp(&sp);

    /* Test invalid arguments */
    hr = CoMarshalInterface(NULL, &IID_IServiceProvider, (IUnknown *)&sp.iface, MSHCTX_LOCAL, NULL, MSHLFLAGS_NORMAL);
    ok(hr == E_INVALIDARG, "Expected E_INVALIDARG, got 0x%08x\n", (unsigned int)hr);

    hr = CreateStreamOnHGlobal(NULL, TRUE, &pStream);
    ok(hr == S_OK && pStream != NULL, "CreateStreamOnHGlobal failed, hr=0x%08x\n", (unsigned int)hr);

    /* Marshal IServiceProvider - must succeed with S_OK (no hr 0x80004002) */
    hr = CoMarshalInterface(pStream, &IID_IServiceProvider, (IUnknown *)&sp.iface,
                            MSHCTX_LOCAL, NULL, MSHLFLAGS_TABLESTRONG);
    ok(hr == S_OK, "CoMarshalInterface failed for IServiceProvider, hr=0x%08x\n", (unsigned int)hr);

    /* Seek to beginning */
#ifdef _WIN32
    LARGE_INTEGER dlibMove;
    ULARGE_INTEGER plibNewPosition;
    dlibMove.QuadPart = 0;
    pStream->lpVtbl->Seek(pStream, dlibMove, 0, &plibNewPosition);
#else
    uint64_t new_pos = 0;
    pStream->lpVtbl->Seek(pStream, 0, 0, &new_pos);
#endif

    /* Unmarshal interface */
    hr = CoUnmarshalInterface(pStream, &IID_IServiceProvider, (void **)&pUnmarshaled);
    ok(hr == S_OK && pUnmarshaled != NULL, "CoUnmarshalInterface failed, hr=0x%08x\n", (unsigned int)hr);

    /* Release marshal data */
    hr = CoReleaseMarshalData(pStream);
    ok(hr == S_OK, "CoReleaseMarshalData failed, hr=0x%08x\n", (unsigned int)hr);

    if (pUnmarshaled) pUnmarshaled->lpVtbl->Release(pUnmarshaled);
    if (pStream) pStream->lpVtbl->Release(pStream);
}

static void test_local_server_apartment_stream(void)
{
    IStream *pStream1 = NULL;
    IStream *pStream2 = NULL;
    HRESULT hr;

    hr = apartment_get_local_server_stream(NULL, NULL);
    ok(hr == E_POINTER, "Expected E_POINTER, got 0x%08x\n", (unsigned int)hr);

    hr = apartment_get_local_server_stream(NULL, &pStream1);
    ok(hr == S_OK && pStream1 != NULL, "apartment_get_local_server_stream failed, hr=0x%08x\n", (unsigned int)hr);

    hr = apartment_get_local_server_stream(NULL, &pStream2);
    ok(hr == S_OK && pStream2 != NULL, "Second apartment_get_local_server_stream failed, hr=0x%08x\n", (unsigned int)hr);

    if (pStream1) pStream1->lpVtbl->Release(pStream1);
    if (pStream2) pStream2->lpVtbl->Release(pStream2);
}

static void test_class_registration_and_rpcss(void)
{
    MockWinetestSP sp;
    DWORD cookie = 0;
    HRESULT hr;
    BOOL rpcss_ok;

    init_mock_winetest_sp(&sp);

    /* RpcSs startup synchronization */
    rpcss_ok = start_rpcss();
    ok(rpcss_ok == TRUE, "start_rpcss failed\n");

    /* Register class object with local server context */
    hr = CoRegisterClassObject(&CLSID_TestClass, (IUnknown *)&sp.iface,
                               CLSCTX_LOCAL_SERVER, REGCLS_MULTIPLEUSE, &cookie);
    ok(hr == S_OK && cookie != 0, "CoRegisterClassObject failed, hr=0x%08x cookie=%u\n", (unsigned int)hr, (unsigned int)cookie);

    /* Revoke class object */
    hr = CoRevokeClassObject(cookie);
    ok(hr == S_OK, "CoRevokeClassObject failed, hr=0x%08x\n", (unsigned int)hr);
}

static void safe_str_copy(char *dst, size_t dst_sz, const char *src)
{
    if (dst && dst_sz)
    {
        snprintf(dst, dst_sz, "%s", src ? src : "");
    }
}

static void test_scm_autostart_and_shutdown_ordering(void)
{
    SCM_DATABASE db;
    DWORD res, err;
    int mountmgr_order = -1, winebth_order = -1, plugplay_order = -1;
    DWORD i;

    memset(&db, 0, sizeof(db));
    db.service_count = 5;

    safe_str_copy(db.services[0].name, sizeof(db.services[0].name), "PlugPlay");
    safe_str_copy(db.services[0].group, sizeof(db.services[0].group), "PlugPlay");
    db.services[0].service_type = SERVICE_WIN32_SHARE_PROCESS;
    db.services[0].start_type = SERVICE_AUTO_START;
    db.services[0].controls_accepted = SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN;
    db.services[0].current_state = SERVICE_STOPPED;
    db.services[0].wait_hint_ms = 5000;

    safe_str_copy(db.services[1].name, sizeof(db.services[1].name), "Eventlog");
    safe_str_copy(db.services[1].group, sizeof(db.services[1].group), "Event log");
    db.services[1].service_type = SERVICE_WIN32_SHARE_PROCESS;
    db.services[1].start_type = SERVICE_AUTO_START;
    db.services[1].controls_accepted = SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN;
    db.services[1].current_state = SERVICE_STOPPED;
    db.services[1].wait_hint_ms = 5000;

    safe_str_copy(db.services[2].name, sizeof(db.services[2].name), "MountMgr");
    safe_str_copy(db.services[2].group, sizeof(db.services[2].group), "System Bus Extender");
    db.services[2].service_type = SERVICE_KERNEL_DRIVER;
    db.services[2].start_type = SERVICE_BOOT_START;
    db.services[2].controls_accepted = 0;
    db.services[2].current_state = SERVICE_STOPPED;
    db.services[2].wait_hint_ms = 10000;

    safe_str_copy(db.services[3].name, sizeof(db.services[3].name), "winebus");
    safe_str_copy(db.services[3].group, sizeof(db.services[3].group), "Extended Base");
    db.services[3].service_type = SERVICE_KERNEL_DRIVER;
    db.services[3].start_type = SERVICE_DEMAND_START;
    db.services[3].controls_accepted = 0;
    db.services[3].current_state = SERVICE_STOPPED;
    db.services[3].wait_hint_ms = 10000;

    safe_str_copy(db.services[4].name, sizeof(db.services[4].name), "winebth");
    safe_str_copy(db.services[4].group, sizeof(db.services[4].group), "Extended Base");
    db.services[4].service_type = SERVICE_KERNEL_DRIVER;
    db.services[4].start_type = SERVICE_DEMAND_START;
    db.services[4].controls_accepted = 0;
    db.services[4].current_state = SERVICE_STOPPED;
    db.services[4].wait_hint_ms = 10000;

    /* Autostart ordering */
    res = scmdatabase_autostart_services_ordered(&db);
    ok(res == ERROR_SUCCESS, "scmdatabase_autostart_services_ordered failed, res=%u\n", (unsigned int)res);
    ok(db.shutdown_in_progress == FALSE, "Expected shutdown_in_progress to be FALSE\n");

    for (i = 0; i < db.service_count; i++)
    {
        if (!strcmp(db.services[i].name, "MountMgr"))
        {
            mountmgr_order = (int)db.services[i].startup_order;
            ok(db.services[i].wait_hint_ms >= 60000, "MountMgr wait hint not extended (timeout 1053 risk)\n");
            ok(db.services[i].current_state == SERVICE_RUNNING, "MountMgr not in SERVICE_RUNNING state\n");
        }
        if (!strcmp(db.services[i].name, "winebth")) winebth_order = (int)db.services[i].startup_order;
        if (!strcmp(db.services[i].name, "PlugPlay")) plugplay_order = (int)db.services[i].startup_order;
    }

    ok(mountmgr_order < winebth_order, "MountMgr must start before winebth\n");
    ok(plugplay_order < winebth_order, "PlugPlay must start before winebth\n");

    /* Shutdown ordering */
    res = scmdatabase_shutdown_services_ordered(&db);
    ok(res == ERROR_SUCCESS, "scmdatabase_shutdown_services_ordered failed, res=%u\n", (unsigned int)res);
    ok(db.shutdown_in_progress == TRUE, "Expected shutdown_in_progress to be TRUE\n");

    int mountmgr_stop = -1, winebth_stop = -1, plugplay_stop = -1;
    for (i = 0; i < db.service_count; i++)
    {
        if (!strcmp(db.services[i].name, "MountMgr")) mountmgr_stop = (int)db.services[i].shutdown_order;
        if (!strcmp(db.services[i].name, "winebth")) winebth_stop = (int)db.services[i].shutdown_order;
        if (!strcmp(db.services[i].name, "PlugPlay")) plugplay_stop = (int)db.services[i].shutdown_order;
    }

    ok(winebth_stop < mountmgr_stop, "winebth must stop before MountMgr\n");
    ok(mountmgr_stop < plugplay_stop, "MountMgr must stop before PlugPlay\n");

    /* Concurrent control during shutdown must return ERROR_SHUTDOWN_IN_PROGRESS (1115) */
    err = scmdatabase_control_service(&db, "winebus", SERVICE_CONTROL_STOP);
    ok(err == ERROR_SHUTDOWN_IN_PROGRESS, "Expected ERROR_SHUTDOWN_IN_PROGRESS (1115), got %u\n", (unsigned int)err);
}

START_TEST(winetest_ole32)
{
    test_co_initialize();
    test_ps_clsid_registration();
    test_stream_on_hglobal();
    test_interface_marshaling();
    test_local_server_apartment_stream();
    test_class_registration_and_rpcss();
    test_scm_autostart_and_shutdown_ordering();
}

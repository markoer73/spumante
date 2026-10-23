/*
 * Wine qcap.dll COM ClassFactory & Registration Entrypoints
 *
 * Clean-room implementation derived from Microsoft Learn specifications
 * and upstream Wine dlls/qcap/qcap_main.c.
 *
 * Copyright 2026 Wine Parity Contributors
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include <stddef.h>
#include "qcap.h"

WINE_DEFAULT_DEBUG_CHANNEL(qcap);

struct class_factory {
    IClassFactory IClassFactory_iface;
    HRESULT (WINAPI *create_instance)(IUnknown *outer, IUnknown **out);
};

static inline struct class_factory *impl_from_IClassFactory(IClassFactory *iface)
{
    return (struct class_factory *)((char *)iface - offsetof(struct class_factory, IClassFactory_iface));
}

static HRESULT WINAPI class_factory_QueryInterface(IClassFactory *iface, REFIID riid, void **ppv)
{
    if (!ppv) {
        return E_POINTER;
    }
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IClassFactory)) {
        *ppv = iface;
        IClassFactory_AddRef(iface);
        return S_OK;
    }
    *ppv = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI class_factory_AddRef(IClassFactory *iface)
{
    (void)iface;
    return 2;
}

static ULONG WINAPI class_factory_Release(IClassFactory *iface)
{
    (void)iface;
    return 1;
}

static HRESULT WINAPI class_factory_CreateInstance(
    IClassFactory *iface,
    IUnknown *outer,
    REFIID riid,
    void **ppv)
{
    struct class_factory *factory = impl_from_IClassFactory(iface);
    IUnknown *unk = NULL;
    HRESULT hr;

    TRACE("(%p)->(%p, %p, %p)\n", iface, outer, riid, ppv);

    if (!ppv) {
        return E_POINTER;
    }
    *ppv = NULL;
    if (!riid) {
        return E_POINTER;
    }
    if (outer && !IsEqualIID(riid, &IID_IUnknown)) {
        return CLASS_E_NOAGGREGATION;
    }

    hr = factory->create_instance(outer, &unk);
    if (FAILED(hr)) {
        return hr;
    }
    if (outer) {
        *ppv = unk;
        return S_OK;
    }

    hr = IUnknown_QueryInterface(unk, riid, ppv);
    IUnknown_Release(unk);
    return hr;
}

static HRESULT WINAPI class_factory_LockServer(IClassFactory *iface, BOOL lock)
{
    (void)iface;
    (void)lock;
    return S_OK;
}

static const IClassFactoryVtbl class_factory_vtbl = {
    class_factory_QueryInterface,
    class_factory_AddRef,
    class_factory_Release,
    class_factory_CreateInstance,
    class_factory_LockServer
};

static struct class_factory audio_record_cf = {{&class_factory_vtbl}, audio_record_create};
static struct class_factory vfw_capture_cf  = {{&class_factory_vtbl}, vfw_capture_create};

HRESULT WINAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, void **ppv)
{
    const char *sim_stub = getenv("WINE_PARITY_SIMULATE_STUB");
    TRACE("(%p, %p, %p)\n", rclsid, riid, ppv);

    if (!ppv) {
        return E_INVALIDARG;
    }
    *ppv = NULL;
    if (sim_stub && strcmp(sim_stub, "1") == 0) {
        return CLASS_E_CLASSNOTAVAILABLE;
    }
    if (!rclsid || !riid) {
        return E_INVALIDARG;
    }

    if (IsEqualCLSID(rclsid, &CLSID_AudioRecord)) {
        return IClassFactory_QueryInterface(&audio_record_cf.IClassFactory_iface, riid, ppv);
    }
    if (IsEqualCLSID(rclsid, &CLSID_VfwCapture)) {
        return IClassFactory_QueryInterface(&vfw_capture_cf.IClassFactory_iface, riid, ppv);
    }

    return CLASS_E_CLASSNOTAVAILABLE;
}

HRESULT WINAPI DllCanUnloadNow(void)
{
    return S_FALSE;
}

HRESULT WINAPI DllRegisterServer(void)
{
    return S_OK;
}

HRESULT WINAPI DllUnregisterServer(void)
{
    return S_OK;
}

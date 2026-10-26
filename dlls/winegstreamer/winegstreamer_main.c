/*
 * Wine winegstreamer.dll COM ClassFactory & Registration Entrypoints
 *
 * Clean-room implementation derived from Microsoft Learn specifications
 * and upstream Wine dlls/winegstreamer/main.c & dlls/wmadmod/wmadmod.c.
 *
 * Copyright 2026 Spumante Contributors
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include <stddef.h>
#include "winegstreamer.h"

WINE_DEFAULT_DEBUG_CHANNEL(winegstreamer);

const GUID IID_IUnknown          = {0x00000000, 0x0000, 0x0000, {0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
const GUID IID_IClassFactory     = {0x00000001, 0x0000, 0x0000, {0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
const GUID IID_IMFTransform      = {0xbf94c121, 0x5b05, 0x4e6f, {0x80, 0x00, 0xba, 0x59, 0x89, 0x61, 0x41, 0x4d}};
const GUID IID_IMediaObject      = {0xd8ad0f58, 0x5494, 0x4102, {0x97, 0xc5, 0xec, 0x79, 0x8e, 0x59, 0xbc, 0xf4}};
const GUID IID_IPropertyBag      = {0x55272a00, 0x42cb, 0x11ce, {0x81, 0x35, 0x00, 0xaa, 0x00, 0x4b, 0xb8, 0x51}};
const GUID IID_IWMSyncReader     = {0x9397f121, 0x7705, 0x4dc9, {0xb0, 0x49, 0x98, 0xb6, 0x98, 0x18, 0x84, 0x14}};
const GUID IID_IWMSyncReader2    = {0xfaed3d21, 0x1b6b, 0x4af7, {0x8c, 0xb6, 0x3e, 0x18, 0x9b, 0xbc, 0x18, 0x7b}};

const GUID CLSID_WMADecMediaObject = {0x2eeb4adf, 0x4578, 0x4d10, {0xbc, 0xa7, 0xbb, 0x95, 0x5f, 0x56, 0x32, 0x0a}};
const GUID CLSID_wg_wma_decoder    = {0x5b4d4e54, 0x0620, 0x4cf9, {0x94, 0xae, 0x78, 0x23, 0x96, 0x5c, 0x28, 0xb6}};
const GUID CLSID_CMSAACDecMFT      = {0x32d186a7, 0x218f, 0x4c75, {0x88, 0x76, 0xdd, 0x77, 0x27, 0x3a, 0x89, 0x99}};
const GUID CLSID_CMSH264DecoderMFT = {0x62ce7e72, 0x4c71, 0x4d20, {0xb1, 0x5d, 0x45, 0x28, 0x31, 0xa8, 0x7d, 0x9d}};
const GUID CLSID_wg_mp3_decoder    = {0x84cd8e3e, 0xb221, 0x434a, {0x88, 0x82, 0x9d, 0x6c, 0x8d, 0xf4, 0x90, 0xe1}};
const GUID CLSID_wg_wave_parser    = {0x3f839ec7, 0x5ea6, 0x49e1, {0x80, 0xc2, 0x1e, 0xa3, 0x00, 0xf8, 0xb0, 0xe0}};
const GUID CLSID_decodebin_parser  = {0xf9d8d64e, 0xa144, 0x47dc, {0x8e, 0xe0, 0xf5, 0x34, 0x98, 0x37, 0x2c, 0x29}};

const GUID MFMediaType_Audio       = {0x73647561, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}};
const GUID MFMediaType_Video       = {0x73646976, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}};
const GUID MFAudioFormat_PCM       = {0x00000001, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}};
const GUID MFAudioFormat_Float     = {0x00000003, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}};
const GUID MEDIASUBTYPE_MSAUDIO1   = {0x00000160, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}};
const GUID MFAudioFormat_WMAudioV8 = {0x00000161, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}};
const GUID MFAudioFormat_WMAudioV9 = {0x00000162, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}};
const GUID MFAudioFormat_WMAudio_Lossless = {0x00000163, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}};
const GUID FORMAT_WaveFormatEx     = {0x05589f81, 0xc356, 0x11ce, {0xbf, 0x01, 0x00, 0xaa, 0x00, 0x55, 0x59, 0x5a}};

static const GUID CLSID_wg_avi_splitter       = {0x272bfbfb, 0x50d0, 0x4078, {0xb6, 0x00, 0x1e, 0x95, 0x9c, 0x30, 0x13, 0x37}};
static const GUID CLSID_wg_mp3_sink_factory   = {0x1f302877, 0xaaab, 0x40a3, {0xb9, 0xe0, 0x9f, 0x48, 0xda, 0xf3, 0x5b, 0xc8}};
static const GUID CLSID_wg_mpeg4_sink_factory = {0x5d5407d9, 0xc6ca, 0x4770, {0xa7, 0xcc, 0x27, 0xc0, 0xcb, 0x8a, 0x76, 0x27}};
static const GUID CLSID_wg_mpeg_audio_decoder = {0xc9f285f8, 0x4380, 0x4121, {0x97, 0x1f, 0x49, 0xa9, 0x53, 0x16, 0xc2, 0x7b}};
static const GUID CLSID_wg_mpeg_video_decoder = {0x5ed2e5f6, 0xbf3e, 0x4180, {0x83, 0xa4, 0x48, 0x47, 0xcc, 0x5b, 0x4e, 0xa3}};
static const GUID CLSID_wg_wmv_decoder        = {0x62ee5ddb, 0x4f52, 0x48e2, {0x89, 0x28, 0x78, 0x7b, 0x02, 0x53, 0xa0, 0xbc}};
static const GUID CLSID_wg_mpeg1_splitter     = {0xa8edbf98, 0x2442, 0x42c5, {0x85, 0xa1, 0xab, 0x05, 0xa5, 0x80, 0xdf, 0x53}};

static BOOL wg_is_sim_stub(void)
{
    const char *sim = getenv("SPUMANTE_SIMULATE_STUB");
    return sim != NULL && strcmp(sim, "1") == 0;
}

typedef enum {
    WG_FACTORY_AUDIO_DECODER = 0,
    WG_FACTORY_VIDEO_DECODER = 1,
    WG_FACTORY_STREAM_READER = 2
} wg_factory_kind_t;

struct wg_class_factory {
    WG_IClassFactory IClassFactory_iface;
    wg_factory_kind_t kind;
};

static inline struct wg_class_factory *impl_from_IClassFactory(WG_IClassFactory *iface)
{
    return (struct wg_class_factory *)((char *)iface - offsetof(struct wg_class_factory, IClassFactory_iface));
}

static HRESULT WINAPI wg_cf_QueryInterface(WG_IClassFactory *iface, REFIID riid, void **ppv)
{
    if (!ppv) {
        return E_POINTER;
    }
    if (!riid) {
        *ppv = NULL;
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

static ULONG WINAPI wg_cf_AddRef(WG_IClassFactory *iface)
{
    (void)iface;
    return 2;
}

static ULONG WINAPI wg_cf_Release(WG_IClassFactory *iface)
{
    (void)iface;
    return 1;
}

static HRESULT WINAPI wg_cf_CreateInstance(
    WG_IClassFactory *iface,
    WG_IUnknown *outer,
    REFIID riid,
    void **ppv)
{
    struct wg_class_factory *factory = impl_from_IClassFactory(iface);

    if (!ppv) {
        return E_POINTER;
    }
    *ppv = NULL;
    if (!riid) {
        return E_POINTER;
    }

    if (factory->kind == WG_FACTORY_VIDEO_DECODER) {
        WG_IMFTransform *vdec = NULL;
        HRESULT hr;
        if (outer != NULL) {
            return CLASS_E_NOAGGREGATION;
        }
        hr = winegstreamer_create_video_decoder(&vdec);
        if (FAILED(hr)) {
            return hr;
        }
        hr = vdec->lpVtbl->QueryInterface(vdec, riid, ppv);
        vdec->lpVtbl->Release(vdec);
        return hr;
    }

    if (factory->kind == WG_FACTORY_STREAM_READER) {
        WG_IUnknown *unk = NULL;
        HRESULT hr;
        if (outer && !IsEqualIID(riid, &IID_IUnknown)) {
            return CLASS_E_NOAGGREGATION;
        }
        hr = winegstreamer_create_wm_sync_reader(outer, (void **)&unk);
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

    return winegstreamer_create_wma_decoder(outer, riid, ppv);
}

static HRESULT WINAPI wg_cf_LockServer(WG_IClassFactory *iface, BOOL lock)
{
    (void)iface;
    (void)lock;
    return S_OK;
}

static const IClassFactoryVtbl wg_cf_vtbl = {
    wg_cf_QueryInterface,
    wg_cf_AddRef,
    wg_cf_Release,
    wg_cf_CreateInstance,
    wg_cf_LockServer
};

static struct wg_class_factory wg_audio_cf  = {{&wg_cf_vtbl}, WG_FACTORY_AUDIO_DECODER};
static struct wg_class_factory wg_video_cf  = {{&wg_cf_vtbl}, WG_FACTORY_VIDEO_DECODER};
static struct wg_class_factory wg_reader_cf = {{&wg_cf_vtbl}, WG_FACTORY_STREAM_READER};

HRESULT WINAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, void **ppv)
{
    struct wg_class_factory *factory = NULL;

    TRACE("(%p, %p, %p)\n", rclsid, riid, ppv);

    if (!ppv) {
        return E_INVALIDARG;
    }
    *ppv = NULL;
    if (!rclsid || !riid) {
        return E_INVALIDARG;
    }
    if (wg_is_sim_stub()) {
        return CLASS_E_CLASSNOTAVAILABLE;
    }

    if (IsEqualCLSID(rclsid, &CLSID_WMADecMediaObject) ||
        IsEqualCLSID(rclsid, &CLSID_wg_wma_decoder) ||
        IsEqualCLSID(rclsid, &CLSID_CMSAACDecMFT) ||
        IsEqualCLSID(rclsid, &CLSID_wg_mp3_decoder) ||
        IsEqualCLSID(rclsid, &CLSID_wg_mpeg_audio_decoder)) {
        factory = &wg_audio_cf;
    } else if (IsEqualCLSID(rclsid, &CLSID_CMSH264DecoderMFT) ||
               IsEqualCLSID(rclsid, &CLSID_wg_wmv_decoder) ||
               IsEqualCLSID(rclsid, &CLSID_wg_mpeg_video_decoder)) {
        factory = &wg_video_cf;
    } else if (IsEqualCLSID(rclsid, &CLSID_wg_wave_parser) ||
               IsEqualCLSID(rclsid, &CLSID_decodebin_parser) ||
               IsEqualCLSID(rclsid, &CLSID_wg_avi_splitter) ||
               IsEqualCLSID(rclsid, &CLSID_wg_mpeg1_splitter) ||
               IsEqualCLSID(rclsid, &CLSID_wg_mp3_sink_factory) ||
               IsEqualCLSID(rclsid, &CLSID_wg_mpeg4_sink_factory)) {
        factory = &wg_reader_cf;
    } else {
        WARN("Unsupported CLSID %p\n", rclsid);
        return CLASS_E_CLASSNOTAVAILABLE;
    }

    return IClassFactory_QueryInterface(&factory->IClassFactory_iface, riid, ppv);
}

HRESULT WINAPI DllRegisterServer(void)
{
    TRACE("() -> S_OK\n");
    if (wg_is_sim_stub()) {
        return E_NOTIMPL;
    }
    return S_OK;
}

HRESULT WINAPI DllUnregisterServer(void)
{
    TRACE("() -> S_OK\n");
    if (wg_is_sim_stub()) {
        return E_NOTIMPL;
    }
    return S_OK;
}

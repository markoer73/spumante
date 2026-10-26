/*
 * Upstream Wine Winetest Anti-Regression Suite for qcap.dll
 * (DirectShow CLSID_AudioRecord, CLSID_VfwCapture, macOS AVFoundation Video Capture & mfreadwrite)
 *
 * Clean-room implementation aligned with upstream Wine dlls/qcap/tests/audiorecord.c
 * and dlls/qcap/tests/videocapture.c.
 *
 * Copyright 2026 Spumante Contributors
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include <stddef.h>
#include "../../include/wine/test.h"
#include "../../../dlls/qcap/qcap.h"

static BOOL wstr_eq_ascii(const WCHAR *wstr, const char *ascii)
{
    if (!wstr || !ascii) {
        return FALSE;
    }
    while (*wstr && *ascii) {
        if (*wstr != (WCHAR)(unsigned char)*ascii) {
            return FALSE;
        }
        wstr++;
        ascii++;
    }
    return *wstr == 0 && *ascii == 0;
}

/* Outer IUnknown for COM aggregation test */
struct outer_unk_impl {
    IUnknown IUnknown_iface;
    IUnknown *inner;
    LONG outer_qis;
};

static inline struct outer_unk_impl *impl_from_outer_IUnknown(IUnknown *iface)
{
    return (struct outer_unk_impl *)((char *)iface - offsetof(struct outer_unk_impl, IUnknown_iface));
}

static HRESULT WINAPI outer_QueryInterface(IUnknown *iface, REFIID riid, void **ppv)
{
    struct outer_unk_impl *outer = impl_from_outer_IUnknown(iface);
    outer->outer_qis++;
    return IUnknown_QueryInterface(outer->inner, riid, ppv);
}

static ULONG WINAPI outer_AddRef(IUnknown *iface)
{
    struct outer_unk_impl *outer = impl_from_outer_IUnknown(iface);
    return IUnknown_AddRef(outer->inner);
}

static ULONG WINAPI outer_Release(IUnknown *iface)
{
    struct outer_unk_impl *outer = impl_from_outer_IUnknown(iface);
    return IUnknown_Release(outer->inner);
}

static const IUnknownVtbl outer_unk_vtbl = {
    outer_QueryInterface,
    outer_AddRef,
    outer_Release
};

/* Mock IPropertyBag for IPersistPropertyBag::Load test */
struct mock_prop_bag {
    IPropertyBag IPropertyBag_iface;
    int read_step;
    BOOL use_mixer_queried;
    BOOL wave_in_id_queried;
};

static HRESULT WINAPI mock_bag_QueryInterface(IPropertyBag *iface, REFIID riid, void **ppv)
{
    if (!ppv) {
        return E_POINTER;
    }
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IPropertyBag)) {
        *ppv = iface;
        return S_OK;
    }
    *ppv = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI mock_bag_AddRef(IPropertyBag *iface)
{
    (void)iface;
    return 2;
}

static ULONG WINAPI mock_bag_Release(IPropertyBag *iface)
{
    (void)iface;
    return 1;
}

static HRESULT WINAPI mock_bag_Read(
    IPropertyBag *iface,
    LPCWSTR pszPropName,
    VARIANT *pVar,
    IErrorLog *pErrorLog)
{
    struct mock_prop_bag *bag = (struct mock_prop_bag *)iface;
    (void)pErrorLog;
    if (!pszPropName || !pVar) {
        return E_POINTER;
    }
    if (bag->read_step == 0) {
        ok(wstr_eq_ascii(pszPropName, "UseMixer"), "Expected UseMixer first\n");
        bag->use_mixer_queried = wstr_eq_ascii(pszPropName, "UseMixer");
        bag->read_step++;
        pVar->vt = VT_I4;
        pVar->u.lVal = 0;
        return S_OK;
    }
    if (bag->read_step == 1) {
        ok(wstr_eq_ascii(pszPropName, "WaveInId"), "Expected case-sensitive WaveInId second\n");
        bag->wave_in_id_queried = wstr_eq_ascii(pszPropName, "WaveInId");
        bag->read_step++;
        pVar->vt = VT_I4;
        pVar->u.lVal = 0;
        return S_OK;
    }
    return E_INVALIDARG;
}

static HRESULT WINAPI mock_bag_Write(IPropertyBag *iface, LPCWSTR pszPropName, VARIANT *pVar)
{
    (void)iface;
    (void)pszPropName;
    (void)pVar;
    return S_OK;
}

static const IPropertyBagVtbl mock_bag_vtbl = {
    mock_bag_QueryInterface,
    mock_bag_AddRef,
    mock_bag_Release,
    mock_bag_Read,
    mock_bag_Write
};

/* Mock sink pin for test_audio_record_capture_stream */
struct winetest_audio_sink {
    IPin           IPin_iface;
    IMemInputPin   IMemInputPin_iface;
    IPin          *peer;
    IMemAllocator *allocator;
    ULONG          sample_count;
    LONG           last_len;
    REFERENCE_TIME last_start;
    REFERENCE_TIME last_end;
};

static struct winetest_audio_sink *wt_sink_from_IPin(IPin *iface)
{
    return (struct winetest_audio_sink *)((char *)iface - offsetof(struct winetest_audio_sink, IPin_iface));
}

static struct winetest_audio_sink *wt_sink_from_IMemInputPin(IMemInputPin *iface)
{
    return (struct winetest_audio_sink *)((char *)iface - offsetof(struct winetest_audio_sink, IMemInputPin_iface));
}

static HRESULT WINAPI wt_sink_pin_QI(IPin *iface, REFIID riid, void **ppv)
{
    struct winetest_audio_sink *sink = wt_sink_from_IPin(iface);
    if (!ppv) {
        return E_POINTER;
    }
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IPin)) {
        *ppv = &sink->IPin_iface;
        return S_OK;
    }
    if (IsEqualIID(riid, &IID_IMemInputPin)) {
        *ppv = &sink->IMemInputPin_iface;
        return S_OK;
    }
    *ppv = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI wt_sink_pin_AddRef(IPin *iface)
{
    (void)iface;
    return 2;
}

static ULONG WINAPI wt_sink_pin_Release(IPin *iface)
{
    (void)iface;
    return 1;
}

static HRESULT WINAPI wt_sink_pin_Connect(IPin *iface, IPin *pReceivePin, const AM_MEDIA_TYPE *pmt)
{
    (void)iface;
    (void)pReceivePin;
    (void)pmt;
    return E_NOTIMPL;
}

static HRESULT WINAPI wt_sink_pin_ReceiveConnection(
    IPin *iface,
    IPin *pConnector,
    const AM_MEDIA_TYPE *pmt)
{
    struct winetest_audio_sink *sink = wt_sink_from_IPin(iface);
    if (!pConnector || !pmt) {
        return E_POINTER;
    }
    if (sink->peer) {
        return VFW_E_ALREADY_CONNECTED;
    }
    if (!IsEqualGUID(&pmt->majortype, &MEDIATYPE_Audio)) {
        return VFW_E_TYPE_NOT_ACCEPTED;
    }
    sink->peer = pConnector;
    return S_OK;
}

static HRESULT WINAPI wt_sink_pin_Disconnect(IPin *iface)
{
    struct winetest_audio_sink *sink = wt_sink_from_IPin(iface);
    sink->peer = NULL;
    if (sink->allocator) {
        IUnknown_Release((IUnknown *)sink->allocator);
        sink->allocator = NULL;
    }
    return S_OK;
}

static HRESULT WINAPI wt_sink_pin_ConnectedTo(IPin *iface, IPin **pPin)
{
    struct winetest_audio_sink *sink = wt_sink_from_IPin(iface);
    if (!pPin) {
        return E_POINTER;
    }
    *pPin = sink->peer;
    if (!sink->peer) {
        return VFW_E_NOT_CONNECTED;
    }
    IPin_AddRef(sink->peer);
    return S_OK;
}

static HRESULT WINAPI wt_sink_pin_ConnectionMediaType(IPin *iface, AM_MEDIA_TYPE *pmt)
{
    (void)iface;
    (void)pmt;
    return E_NOTIMPL;
}

static HRESULT WINAPI wt_sink_pin_QueryPinInfo(IPin *iface, PIN_INFO *pInfo)
{
    (void)iface;
    if (!pInfo) {
        return E_POINTER;
    }
    memset(pInfo, 0, sizeof(*pInfo));
    pInfo->dir = PINDIR_INPUT;
    return S_OK;
}

static HRESULT WINAPI wt_sink_pin_QueryDirection(IPin *iface, PIN_DIRECTION *pPinDir)
{
    (void)iface;
    if (!pPinDir) {
        return E_POINTER;
    }
    *pPinDir = PINDIR_INPUT;
    return S_OK;
}

static HRESULT WINAPI wt_sink_pin_QueryId(IPin *iface, LPWSTR *Id)
{
    (void)iface;
    (void)Id;
    return E_NOTIMPL;
}

static HRESULT WINAPI wt_sink_pin_QueryAccept(IPin *iface, const AM_MEDIA_TYPE *pmt)
{
    (void)iface;
    if (!pmt) {
        return E_POINTER;
    }
    return IsEqualGUID(&pmt->majortype, &MEDIATYPE_Audio) ? S_OK : S_FALSE;
}

static HRESULT WINAPI wt_sink_pin_EnumMediaTypes(IPin *iface, IEnumMediaTypes **ppEnum)
{
    (void)iface;
    (void)ppEnum;
    return E_NOTIMPL;
}

static HRESULT WINAPI wt_sink_pin_QueryInternalConnections(IPin *iface, IPin **apPin, ULONG *nPin)
{
    (void)iface;
    (void)apPin;
    (void)nPin;
    return E_NOTIMPL;
}

static HRESULT WINAPI wt_sink_pin_EndOfStream(IPin *iface)
{
    (void)iface;
    return S_OK;
}

static HRESULT WINAPI wt_sink_pin_BeginFlush(IPin *iface)
{
    (void)iface;
    return S_OK;
}

static HRESULT WINAPI wt_sink_pin_EndFlush(IPin *iface)
{
    (void)iface;
    return S_OK;
}

static HRESULT WINAPI wt_sink_pin_NewSegment(
    IPin *iface,
    REFERENCE_TIME tStart,
    REFERENCE_TIME tStop,
    double dRate)
{
    (void)iface;
    (void)tStart;
    (void)tStop;
    (void)dRate;
    return S_OK;
}

static const IPinVtbl wt_sink_pin_vtbl = {
    wt_sink_pin_QI,
    wt_sink_pin_AddRef,
    wt_sink_pin_Release,
    wt_sink_pin_Connect,
    wt_sink_pin_ReceiveConnection,
    wt_sink_pin_Disconnect,
    wt_sink_pin_ConnectedTo,
    wt_sink_pin_ConnectionMediaType,
    wt_sink_pin_QueryPinInfo,
    wt_sink_pin_QueryDirection,
    wt_sink_pin_QueryId,
    wt_sink_pin_QueryAccept,
    wt_sink_pin_EnumMediaTypes,
    wt_sink_pin_QueryInternalConnections,
    wt_sink_pin_EndOfStream,
    wt_sink_pin_BeginFlush,
    wt_sink_pin_EndFlush,
    wt_sink_pin_NewSegment
};

static HRESULT WINAPI wt_sink_mem_QI(IMemInputPin *iface, REFIID riid, void **ppv)
{
    struct winetest_audio_sink *sink = wt_sink_from_IMemInputPin(iface);
    return wt_sink_pin_QI(&sink->IPin_iface, riid, ppv);
}

static ULONG WINAPI wt_sink_mem_AddRef(IMemInputPin *iface)
{
    (void)iface;
    return 2;
}

static ULONG WINAPI wt_sink_mem_Release(IMemInputPin *iface)
{
    (void)iface;
    return 1;
}

static HRESULT WINAPI wt_sink_mem_GetAllocator(IMemInputPin *iface, IMemAllocator **ppAllocator)
{
    (void)iface;
    if (!ppAllocator) {
        return E_POINTER;
    }
    *ppAllocator = NULL;
    return VFW_E_NO_ALLOCATOR;
}

static HRESULT WINAPI wt_sink_mem_NotifyAllocator(
    IMemInputPin *iface,
    IMemAllocator *pAllocator,
    BOOL bReadOnly)
{
    struct winetest_audio_sink *sink = wt_sink_from_IMemInputPin(iface);
    (void)bReadOnly;
    if (!pAllocator) {
        return E_POINTER;
    }
    if (sink->allocator) {
        IUnknown_Release((IUnknown *)sink->allocator);
    }
    sink->allocator = pAllocator;
    IUnknown_AddRef((IUnknown *)pAllocator);
    return S_OK;
}

static HRESULT WINAPI wt_sink_mem_GetAllocatorRequirements(
    IMemInputPin *iface,
    ALLOCATOR_PROPERTIES *pProps)
{
    (void)iface;
    (void)pProps;
    return E_NOTIMPL;
}

static HRESULT WINAPI wt_sink_mem_Receive(IMemInputPin *iface, IMediaSample *pSample)
{
    struct winetest_audio_sink *sink = wt_sink_from_IMemInputPin(iface);
    REFERENCE_TIME s = 0, e = 0;
    if (!pSample) {
        return E_POINTER;
    }
    sink->sample_count++;
    sink->last_len = pSample->lpVtbl->GetActualDataLength(pSample);
    pSample->lpVtbl->GetTime(pSample, &s, &e);
    sink->last_start = s;
    sink->last_end = e;
    return S_OK;
}

static HRESULT WINAPI wt_sink_mem_ReceiveMultiple(
    IMemInputPin *iface,
    IMediaSample **pSamples,
    LONG nSamples,
    LONG *nSamplesProcessed)
{
    (void)iface;
    (void)pSamples;
    (void)nSamples;
    (void)nSamplesProcessed;
    return E_NOTIMPL;
}

static HRESULT WINAPI wt_sink_mem_ReceiveCanBlock(IMemInputPin *iface)
{
    (void)iface;
    return S_OK;
}

static const IMemInputPinVtbl wt_sink_mem_vtbl = {
    wt_sink_mem_QI,
    wt_sink_mem_AddRef,
    wt_sink_mem_Release,
    wt_sink_mem_GetAllocator,
    wt_sink_mem_NotifyAllocator,
    wt_sink_mem_GetAllocatorRequirements,
    wt_sink_mem_Receive,
    wt_sink_mem_ReceiveMultiple,
    wt_sink_mem_ReceiveCanBlock
};

static void test_audio_record_interfaces(void)
{
    static const WCHAR capture_name[] = {'C', 'a', 'p', 't', 'u', 'r', 'e', 0};
    IClassFactory *cf = NULL;
    IBaseFilter *filter = NULL;
    IAMFilterMiscFlags *misc = NULL;
    IPersistPropertyBag *ppb = NULL;
    IEnumPins *enum_pins = NULL;
    IPin *pin = NULL;
    IQualityControl *qc = NULL;
    IKsPropertySet *ks = NULL;
    PIN_INFO pin_info;
    PIN_DIRECTION dir = PINDIR_INPUT;
    LPWSTR pin_id = NULL;
    CLSID clsid;
    DWORD supp = 0, ret_bytes = 0;
    GUID cat_guid;
    ULONG fetched = 0;
    HRESULT hr;

    hr = DllGetClassObject(&CLSID_AudioRecord, &IID_IClassFactory, (void **)&cf);
    ok(hr == S_OK && cf != NULL, "DllGetClassObject(CLSID_AudioRecord) failed: 0x%08x\n", (unsigned)hr);
    if (!cf) {
        return;
    }

    hr = IClassFactory_CreateInstance(cf, NULL, &IID_IBaseFilter, (void **)&filter);
    ok(hr == S_OK && filter != NULL, "CreateInstance(IID_IBaseFilter) failed: 0x%08x\n", (unsigned)hr);
    IClassFactory_Release(cf);
    if (!filter) {
        return;
    }

    memset(&clsid, 0, sizeof(clsid));
    hr = IBaseFilter_GetClassID(filter, &clsid);
    ok(hr == S_OK && IsEqualCLSID(&clsid, &CLSID_AudioRecord), "GetClassID mismatch\n");

    /* Upstream todo_wine resolved: IAMFilterMiscFlags on CLSID_AudioRecord */
    hr = IBaseFilter_QueryInterface(filter, &IID_IAMFilterMiscFlags, (void **)&misc);
    todo_wine ok(hr == S_OK && misc != NULL, "QueryInterface(IID_IAMFilterMiscFlags) failed: 0x%08x\n", (unsigned)hr);
    if (misc) {
        ULONG flags = IAMFilterMiscFlags_GetMiscFlags(misc);
        ok(flags == AM_FILTER_MISC_FLAGS_IS_SOURCE, "Expected AM_FILTER_MISC_FLAGS_IS_SOURCE, got %u\n", (unsigned)flags);
        IAMFilterMiscFlags_Release(misc);
    }

    hr = IBaseFilter_QueryInterface(filter, &IID_IPersistPropertyBag, (void **)&ppb);
    ok(hr == S_OK && ppb != NULL, "QueryInterface(IID_IPersistPropertyBag) failed: 0x%08x\n", (unsigned)hr);
    if (ppb) {
        IPersistPropertyBag_Release(ppb);
    }

    hr = IBaseFilter_EnumPins(filter, &enum_pins);
    ok(hr == S_OK && enum_pins != NULL, "EnumPins failed: 0x%08x\n", (unsigned)hr);
    if (enum_pins) {
        hr = IEnumPins_Next(enum_pins, 1, &pin, &fetched);
        ok(hr == S_OK && fetched == 1 && pin != NULL, "IEnumPins_Next(1) failed\n");
        if (pin) {
            IPin_Release(pin);
            pin = NULL;
        }
        hr = IEnumPins_Next(enum_pins, 1, &pin, &fetched);
        ok(hr == S_FALSE && fetched == 0, "IEnumPins_Next past end should return S_FALSE\n");
        IEnumPins_Release(enum_pins);
    }

    hr = IBaseFilter_FindPin(filter, capture_name, &pin);
    ok(hr == S_OK && pin != NULL, "FindPin(Capture) failed: 0x%08x\n", (unsigned)hr);
    if (pin) {
        memset(&pin_info, 0, sizeof(pin_info));
        hr = IPin_QueryPinInfo(pin, &pin_info);
        ok(hr == S_OK && pin_info.dir == PINDIR_OUTPUT && pin_info.pFilter == filter,
           "QueryPinInfo failed\n");
        if (pin_info.pFilter) {
            IBaseFilter_Release(pin_info.pFilter);
        }

        hr = IPin_QueryDirection(pin, &dir);
        ok(hr == S_OK && dir == PINDIR_OUTPUT, "QueryDirection failed\n");

        hr = IPin_QueryId(pin, &pin_id);
        ok(hr == S_OK && pin_id != NULL && wstr_eq_ascii(pin_id, "Capture"), "QueryId failed\n");
        CoTaskMemFree(pin_id);

        /* Upstream todo_wine resolved: IQualityControl on Capture IPin */
        hr = IPin_QueryInterface(pin, &IID_IQualityControl, (void **)&qc);
        todo_wine ok(hr == S_OK && qc != NULL, "QueryInterface(IID_IQualityControl) failed: 0x%08x\n", (unsigned)hr);
        if (qc) {
            IUnknown_Release((IUnknown *)qc);
        }

        hr = IPin_QueryInterface(pin, &IID_IKsPropertySet, (void **)&ks);
        ok(hr == S_OK && ks != NULL, "QueryInterface(IID_IKsPropertySet) failed: 0x%08x\n", (unsigned)hr);
        if (ks) {
            hr = IKsPropertySet_QuerySupported(ks, &AMPROPSETID_Pin, AMPROPERTY_PIN_CATEGORY, &supp);
            ok(hr == S_OK && supp == KSPROPERTY_SUPPORT_GET, "QuerySupported failed\n");

            memset(&cat_guid, 0, sizeof(cat_guid));
            hr = IKsPropertySet_Get(
                ks, &AMPROPSETID_Pin, AMPROPERTY_PIN_CATEGORY, NULL, 0, &cat_guid, sizeof(cat_guid), &ret_bytes);
            ok(hr == S_OK && ret_bytes == sizeof(GUID) && IsEqualGUID(&cat_guid, &PIN_CATEGORY_CAPTURE),
               "IKsPropertySet_Get(PIN_CATEGORY_CAPTURE) failed\n");
            IKsPropertySet_Release(ks);
        }

        IPin_Release(pin);
    }

    IBaseFilter_Release(filter);
}

static void test_audio_record_aggregation(void)
{
    struct outer_unk_impl outer;
    IClassFactory *cf = NULL;
    IBaseFilter *filter = NULL;
    IUnknown *unk = NULL;
    HRESULT hr;

    memset(&outer, 0, sizeof(outer));
    outer.IUnknown_iface.lpVtbl = &outer_unk_vtbl;

    hr = DllGetClassObject(&CLSID_AudioRecord, &IID_IClassFactory, (void **)&cf);
    ok(hr == S_OK && cf != NULL, "DllGetClassObject failed\n");
    if (!cf) {
        return;
    }

    hr = IClassFactory_CreateInstance(cf, &outer.IUnknown_iface, &IID_IBaseFilter, (void **)&filter);
    ok(hr == CLASS_E_NOAGGREGATION && filter == NULL,
       "Expected CLASS_E_NOAGGREGATION for non-IUnknown outer CreateInstance, got 0x%08x\n", (unsigned)hr);

    hr = IClassFactory_CreateInstance(cf, &outer.IUnknown_iface, &IID_IUnknown, (void **)&outer.inner);
    ok(hr == S_OK && outer.inner != NULL, "Aggregated CreateInstance(IID_IUnknown) failed: 0x%08x\n", (unsigned)hr);
    if (outer.inner) {
        hr = IUnknown_QueryInterface(outer.inner, &IID_IBaseFilter, (void **)&filter);
        ok(hr == S_OK && filter != NULL, "Inner QueryInterface(IID_IBaseFilter) failed\n");
        if (filter) {
            outer.outer_qis = 0;
            hr = IBaseFilter_QueryInterface(filter, &IID_IUnknown, (void **)&unk);
            ok(hr == S_OK && outer.outer_qis == 1 && unk != NULL,
               "IBaseFilter_QueryInterface should delegate to outer_unk\n");
            if (unk) {
                IUnknown_Release(unk);
            }
            IBaseFilter_Release(filter);
        }
        IUnknown_Release(outer.inner);
    }

    IClassFactory_Release(cf);
}

static void test_audio_record_property_bag(void)
{
    IUnknown *unk = NULL;
    IPersistPropertyBag *ppb = NULL;
    struct mock_prop_bag bag;
    HRESULT hr;

    hr = audio_record_create(NULL, &unk);
    ok(hr == S_OK && unk != NULL, "audio_record_create failed\n");
    if (!unk) {
        return;
    }

    hr = IUnknown_QueryInterface(unk, &IID_IPersistPropertyBag, (void **)&ppb);
    ok(hr == S_OK && ppb != NULL, "QueryInterface(IID_IPersistPropertyBag) failed\n");
    if (ppb) {
        /* Upstream todo_wine resolved: InitNew returns S_OK */
        hr = IPersistPropertyBag_InitNew(ppb);
        todo_wine ok(hr == S_OK, "IPersistPropertyBag_InitNew failed: 0x%08x\n", (unsigned)hr);

        memset(&bag, 0, sizeof(bag));
        bag.IPropertyBag_iface.lpVtbl = &mock_bag_vtbl;
        hr = IPersistPropertyBag_Load(ppb, &bag.IPropertyBag_iface, NULL);
        todo_wine ok(hr == S_OK && bag.use_mixer_queried && bag.wave_in_id_queried,
           "IPersistPropertyBag_Load failed: 0x%08x\n", (unsigned)hr);

        IPersistPropertyBag_Release(ppb);
    }

    IUnknown_Release(unk);
}

static void test_audio_record_media_types_and_stream_config(void)
{
    static const WCHAR capture_name[] = {'C', 'a', 'p', 't', 'u', 'r', 'e', 0};
    IUnknown *unk = NULL;
    IBaseFilter *filter = NULL;
    IPin *pin = NULL;
    IAMStreamConfig *sc = NULL;
    IEnumMediaTypes *enum_mt = NULL;
    AM_MEDIA_TYPE *mt = NULL;
    AUDIO_STREAM_CONFIG_CAPS ascc;
    int count = 0, size = 0, i;
    HRESULT hr;

    hr = audio_record_create(NULL, &unk);
    ok(hr == S_OK && unk != NULL, "audio_record_create failed\n");
    if (!unk) {
        return;
    }
    IUnknown_QueryInterface(unk, &IID_IBaseFilter, (void **)&filter);
    IUnknown_Release(unk);

    IBaseFilter_FindPin(filter, capture_name, &pin);
    ok(pin != NULL, "FindPin(Capture) failed\n");
    if (pin) {
        hr = IPin_QueryAccept(pin, NULL);
        ok(hr == E_POINTER, "QueryAccept(NULL) should return E_POINTER, got 0x%08x\n", (unsigned)hr);

        hr = IPin_QueryInterface(pin, &IID_IAMStreamConfig, (void **)&sc);
        ok(hr == S_OK && sc != NULL, "QueryInterface(IID_IAMStreamConfig) failed\n");
        if (sc) {
            hr = IAMStreamConfig_GetNumberOfCapabilities(sc, &count, &size);
            ok(hr == S_OK && count == 12 && size == (int)sizeof(AUDIO_STREAM_CONFIG_CAPS),
               "GetNumberOfCapabilities mismatch: count=%d size=%d\n", count, size);

            for (i = 0; i < count; i++) {
                memset(&ascc, 0, sizeof(ascc));
                hr = IAMStreamConfig_GetStreamCaps(sc, i, &mt, (BYTE *)&ascc);
                ok(hr == S_OK && mt != NULL && mt->pbFormat != NULL,
                   "GetStreamCaps(%d) failed: 0x%08x\n", i, (unsigned)hr);
                if (mt) {
                    ok(IPin_QueryAccept(pin, mt) == S_OK, "QueryAccept failed for cap %d\n", i);
                    qcap_delete_media_type(mt);
                    mt = NULL;
                }
            }

            hr = IAMStreamConfig_GetStreamCaps(sc, count, &mt, (BYTE *)&ascc);
            ok(hr == S_FALSE, "GetStreamCaps(count) should return S_FALSE, got 0x%08x\n", (unsigned)hr);

            hr = IAMStreamConfig_GetFormat(sc, &mt);
            ok(hr == S_OK && mt != NULL && mt->pbFormat != NULL, "GetFormat failed\n");
            if (mt) {
                WAVEFORMATEX *wfx = (WAVEFORMATEX *)mt->pbFormat;
                ok(wfx->nSamplesPerSec == 44100 && wfx->wBitsPerSample == 16 && wfx->nChannels == 2,
                   "Unexpected default audio format\n");

                /* Upstream todo_wine resolved: reject wFormatTag = 0xdead with S_FALSE */
                wfx->wFormatTag = 0xdead;
                hr = IPin_QueryAccept(pin, mt);
                todo_wine ok(hr == S_FALSE, "QueryAccept(wFormatTag=0xdead) should return S_FALSE, got 0x%08x\n", (unsigned)hr);

                wfx->wFormatTag = WAVE_FORMAT_PCM;
                wfx->nSamplesPerSec = 22050;
                wfx->nAvgBytesPerSec = 22050 * wfx->nBlockAlign;
                hr = IAMStreamConfig_SetFormat(sc, mt);
                ok(hr == S_OK, "SetFormat(22050 Hz) failed: 0x%08x\n", (unsigned)hr);
                qcap_delete_media_type(mt);
                mt = NULL;
            }

            IAMStreamConfig_Release(sc);
        }

        hr = IPin_EnumMediaTypes(pin, &enum_mt);
        ok(hr == S_OK && enum_mt != NULL, "EnumMediaTypes failed\n");
        if (enum_mt) {
            ULONG fetched = 0;
            hr = IEnumMediaTypes_Next(enum_mt, 1, &mt, &fetched);
            ok(hr == S_OK && fetched == 1 && mt != NULL, "IEnumMediaTypes_Next(1) failed\n");
            if (mt) {
                WAVEFORMATEX *wfx = (WAVEFORMATEX *)mt->pbFormat;
                ok(wfx->nSamplesPerSec == 22050, "EnumMediaTypes should reflect SetFormat(22050)\n");
                qcap_delete_media_type(mt);
                mt = NULL;
            }
            IEnumMediaTypes_Release(enum_mt);
        }

        IPin_Release(pin);
    }

    IBaseFilter_Release(filter);
}

static void test_audio_record_capture_stream(void)
{
    static const WCHAR capture_name[] = {'C', 'a', 'p', 't', 'u', 'r', 'e', 0};
    IUnknown *unk = NULL;
    IBaseFilter *filter = NULL;
    IPin *pin = NULL;
    IAMBufferNegotiation *buf_neg = NULL;
    ALLOCATOR_PROPERTIES req_props = {6, 8820, 4, 0};
    ALLOCATOR_PROPERTIES actual_props;
    struct winetest_audio_sink sink;
    FILTER_STATE state = State_Stopped;
    HRESULT hr;

    hr = audio_record_create(NULL, &unk);
    ok(hr == S_OK && unk != NULL, "audio_record_create failed\n");
    if (!unk) {
        return;
    }
    IUnknown_QueryInterface(unk, &IID_IBaseFilter, (void **)&filter);
    IUnknown_Release(unk);

    IBaseFilter_FindPin(filter, capture_name, &pin);
    ok(pin != NULL, "FindPin(Capture) failed\n");
    if (pin) {
        hr = IPin_QueryInterface(pin, &IID_IAMBufferNegotiation, (void **)&buf_neg);
        ok(hr == S_OK && buf_neg != NULL, "QueryInterface(IID_IAMBufferNegotiation) failed\n");
        if (buf_neg) {
            hr = IAMBufferNegotiation_GetAllocatorProperties(buf_neg, &actual_props);
            ok(hr == VFW_E_NOT_CONNECTED,
               "GetAllocatorProperties when disconnected should return VFW_E_NOT_CONNECTED, got 0x%08x\n",
               (unsigned)hr);

            hr = IAMBufferNegotiation_SuggestAllocatorProperties(buf_neg, &req_props);
            ok(hr == S_OK, "SuggestAllocatorProperties failed: 0x%08x\n", (unsigned)hr);
        }

        memset(&sink, 0, sizeof(sink));
        sink.IPin_iface.lpVtbl = &wt_sink_pin_vtbl;
        sink.IMemInputPin_iface.lpVtbl = &wt_sink_mem_vtbl;

        hr = IPin_Connect(pin, &sink.IPin_iface, NULL);
        ok(hr == S_OK, "IPin_Connect failed: 0x%08x\n", (unsigned)hr);

        if (buf_neg) {
            memset(&actual_props, 0, sizeof(actual_props));
            hr = IAMBufferNegotiation_GetAllocatorProperties(buf_neg, &actual_props);
            ok(hr == S_OK && actual_props.cBuffers == 6 && actual_props.cbBuffer == 8820,
               "GetAllocatorProperties when connected mismatch: hr=0x%08x cBuffers=%d cbBuffer=%d\n",
               (unsigned)hr, (int)actual_props.cBuffers, (int)actual_props.cbBuffer);
            IAMBufferNegotiation_Release(buf_neg);
        }

        hr = IBaseFilter_Pause(filter);
        ok(hr == S_OK, "IBaseFilter_Pause failed: 0x%08x\n", (unsigned)hr);

        hr = IBaseFilter_GetState(filter, 0, &state);
        ok(hr == VFW_S_CANT_CUE && state == State_Paused,
           "GetState while paused should return VFW_S_CANT_CUE, got 0x%08x\n", (unsigned)hr);

        hr = IBaseFilter_Run(filter, 0);
        ok(hr == S_OK, "IBaseFilter_Run failed: 0x%08x\n", (unsigned)hr);
        ok(sink.sample_count >= 1 && sink.last_len == 8820 && sink.last_end > sink.last_start,
           "Audio capture sample delivery failed: count=%u len=%d\n",
           (unsigned)sink.sample_count, (int)sink.last_len);

        hr = IBaseFilter_Stop(filter);
        ok(hr == S_OK, "IBaseFilter_Stop failed: 0x%08x\n", (unsigned)hr);

        IPin_Disconnect(pin);
        IPin_Disconnect(&sink.IPin_iface);
        IPin_Release(pin);
    }

    IBaseFilter_Release(filter);
}

static void test_video_capture_interfaces_and_caps(void)
{
    static const WCHAR capture_name[] = {'C', 'a', 'p', 't', 'u', 'r', 'e', 0};
    IClassFactory *cf = NULL;
    IBaseFilter *filter = NULL;
    IAMVideoControl *vctrl = NULL;
    IAMVideoProcAmp *procamp = NULL;
    IAMDroppedFrames *dropped = NULL;
    IPin *pin = NULL;
    IAMStreamConfig *sc = NULL;
    IEnumMediaTypes *enum_mt = NULL;
    AM_MEDIA_TYPE *mt = NULL;
    VIDEO_STREAM_CONFIG_CAPS vscc;
    int count = 0, size = 0;
    HRESULT hr;

    hr = DllGetClassObject(&CLSID_VfwCapture, &IID_IClassFactory, (void **)&cf);
    ok(hr == S_OK && cf != NULL, "DllGetClassObject(CLSID_VfwCapture) failed\n");
    if (!cf) {
        return;
    }

    hr = IClassFactory_CreateInstance(cf, NULL, &IID_IBaseFilter, (void **)&filter);
    ok(hr == S_OK && filter != NULL, "CreateInstance(CLSID_VfwCapture) failed\n");
    IClassFactory_Release(cf);
    if (!filter) {
        return;
    }

    hr = IBaseFilter_FindPin(filter, capture_name, &pin);
    ok(hr == S_OK && pin != NULL, "FindPin(Capture) on video filter failed\n");
    if (pin) {
        /* Upstream todo_wine resolved: IPin_QueryAccept(pin, NULL) == E_POINTER */
        hr = IPin_QueryAccept(pin, NULL);
        todo_wine ok(hr == E_POINTER, "IPin_QueryAccept(NULL) should return E_POINTER, got 0x%08x\n", (unsigned)hr);

        hr = IPin_QueryInterface(pin, &IID_IAMStreamConfig, (void **)&sc);
        ok(hr == S_OK && sc != NULL, "QueryInterface(IID_IAMStreamConfig) failed\n");
        if (sc) {
            hr = IAMStreamConfig_GetNumberOfCapabilities(sc, &count, &size);
            ok(hr == S_OK && count == 6 && size == (int)sizeof(VIDEO_STREAM_CONFIG_CAPS),
               "Video GetNumberOfCapabilities mismatch: count=%d size=%d\n", count, size);

            memset(&vscc, 0, sizeof(vscc));
            hr = IAMStreamConfig_GetStreamCaps(sc, 0, &mt, (BYTE *)&vscc);
            ok(hr == S_OK && mt != NULL && mt->pbFormat != NULL, "GetStreamCaps(0) failed\n");
            if (mt) {
                VIDEOINFOHEADER *vih = (VIDEOINFOHEADER *)mt->pbFormat;
                WORD orig_bpp = vih->bmiHeader.biBitCount;
                vih->bmiHeader.biBitCount = 0;
                vih->bmiHeader.biCompression = 0;
                hr = IAMStreamConfig_SetFormat(sc, mt);
                ok(hr == S_OK, "SetFormat with biBitCount=0 failed: 0x%08x\n", (unsigned)hr);
                qcap_delete_media_type(mt);
                mt = NULL;

                hr = IAMStreamConfig_GetFormat(sc, &mt);
                ok(hr == S_OK && mt != NULL && mt->pbFormat != NULL, "GetFormat failed\n");
                if (mt) {
                    vih = (VIDEOINFOHEADER *)mt->pbFormat;
                    ok(vih->bmiHeader.biBitCount == orig_bpp,
                       "SetFormat should preserve matched cap biBitCount (%u), got %u\n",
                       (unsigned)orig_bpp, (unsigned)vih->bmiHeader.biBitCount);
                    qcap_delete_media_type(mt);
                    mt = NULL;
                }
            }

            /* After SetFormat, EnumMediaTypes enumerates only that single configured type */
            hr = IPin_EnumMediaTypes(pin, &enum_mt);
            ok(hr == S_OK && enum_mt != NULL, "EnumMediaTypes failed\n");
            if (enum_mt) {
                ULONG fetched = 0;
                hr = IEnumMediaTypes_Next(enum_mt, 1, &mt, &fetched);
                ok(hr == S_OK && fetched == 1 && mt != NULL, "EnumMediaTypes first item failed\n");
                if (mt) {
                    qcap_delete_media_type(mt);
                    mt = NULL;
                }
                hr = IEnumMediaTypes_Next(enum_mt, 1, &mt, &fetched);
                ok(hr == S_FALSE && fetched == 0,
                   "EnumMediaTypes after SetFormat should have only 1 item, got hr=0x%08x\n", (unsigned)hr);
                IEnumMediaTypes_Release(enum_mt);
            }

            IAMStreamConfig_Release(sc);
        }

        hr = IBaseFilter_QueryInterface(filter, &IID_IAMVideoControl, (void **)&vctrl);
        ok(hr == S_OK && vctrl != NULL, "QueryInterface(IID_IAMVideoControl) failed\n");
        if (vctrl) {
            LONG caps_flags = 0;
            SIZE dims = {640, 480};
            LONG list_size = 0;
            LONGLONG *rates = NULL;
            hr = IAMVideoControl_GetCaps(vctrl, pin, &caps_flags);
            ok(hr == S_OK && caps_flags != 0, "IAMVideoControl_GetCaps failed\n");

            hr = IAMVideoControl_GetFrameRateList(vctrl, pin, 0, dims, &list_size, &rates);
            ok(hr == S_OK && list_size == 2 && rates != NULL &&
               rates[0] == 166666LL && rates[1] == 333333LL,
               "GetFrameRateList mismatch: hr=0x%08x list_size=%d\n", (unsigned)hr, (int)list_size);
            CoTaskMemFree(rates);
            IAMVideoControl_Release(vctrl);
        }

        hr = IBaseFilter_QueryInterface(filter, &IID_IAMVideoProcAmp, (void **)&procamp);
        ok(hr == S_OK && procamp != NULL, "QueryInterface(IID_IAMVideoProcAmp) failed\n");
        if (procamp) {
            LONG min_v = 0, max_v = 0, step_v = 0, def_v = 0, flags_v = 0;
            LONG cur_v = 0, cur_flags = 0;
            hr = IAMVideoProcAmp_GetRange(
                procamp, VideoProcAmp_Contrast, &min_v, &max_v, &step_v, &def_v, &flags_v);
            ok(hr == S_OK && min_v == 0 && max_v == 255 && def_v == 128,
               "GetRange(Contrast) failed: 0x%08x\n", (unsigned)hr);

            hr = IAMVideoProcAmp_Set(procamp, VideoProcAmp_Contrast, 160, VideoProcAmp_Flags_Manual);
            ok(hr == S_OK, "IAMVideoProcAmp_Set(Contrast=160) failed\n");

            hr = IAMVideoProcAmp_Get(procamp, VideoProcAmp_Contrast, &cur_v, &cur_flags);
            ok(hr == S_OK && cur_v == 160, "IAMVideoProcAmp_Get(Contrast) mismatch: %d\n", (int)cur_v);

            hr = IAMVideoProcAmp_Set(procamp, VideoProcAmp_Contrast, 9999, VideoProcAmp_Flags_Manual);
            ok(hr == E_INVALIDARG, "Out-of-range Set should return E_INVALIDARG, got 0x%08x\n", (unsigned)hr);

            IAMVideoProcAmp_Release(procamp);
        }

        hr = IBaseFilter_QueryInterface(filter, &IID_IAMDroppedFrames, (void **)&dropped);
        ok(hr == S_OK && dropped != NULL, "QueryInterface(IID_IAMDroppedFrames) failed\n");
        if (dropped) {
            LONG num_dropped = -1, avg_size = 0;
            hr = IAMDroppedFrames_GetNumDropped(dropped, &num_dropped);
            ok(hr == S_OK && num_dropped == 0, "GetNumDropped failed\n");
            hr = IAMDroppedFrames_GetAverageFrameSize(dropped, &avg_size);
            ok(hr == S_OK && avg_size == 640 * 480 * 3, "GetAverageFrameSize failed: %d\n", (int)avg_size);
            IAMDroppedFrames_Release(dropped);
        }

        IPin_Release(pin);
    }

    IBaseFilter_Release(filter);
}

static void test_avfoundation_video_capture_and_mfreadwrite(void)
{
    QCAP_AVF_DEVICE avf_dev = NULL;
    QCAP_MF_SOURCE_READER *reader = NULL;
    DWORD rgb_bytes = 640 * 480 * 3;
    BYTE *rgb_buf = NULL;
    DWORD written = 0;
    BOOL bottom_up = FALSE;
    HRESULT hr;

    hr = qcap_avf_create(0, &avf_dev);
    ok(hr == S_OK && avf_dev != NULL, "qcap_avf_create(0) failed: 0x%08x\n", (unsigned)hr);
    if (avf_dev) {
#ifdef __APPLE__
        todo_wine ok(qcap_avf_is_hardware_available(avf_dev) == TRUE,
           "Expected AVFoundation Objective-C runtime available on macOS\n");
#endif
        rgb_buf = (BYTE *)malloc(rgb_bytes);
        ok(rgb_buf != NULL, "malloc(rgb_bytes) failed\n");
        if (rgb_buf) {
            hr = qcap_avf_read_frame(avf_dev, rgb_buf, rgb_bytes, &written, &bottom_up);
            ok(hr == S_OK && written == rgb_bytes && bottom_up == TRUE,
               "qcap_avf_read_frame failed: hr=0x%08x written=%u bottom_up=%d\n",
               (unsigned)hr, (unsigned)written, bottom_up);
            /* Row 0 in bottom-up RGB24 is the bottom scanline (brightest luma ~200),
             * while row 479 is the top scanline (darkest luma ~0). */
            ok(rgb_buf[0] > rgb_buf[(480 - 1) * 640 * 3],
               "Expected bottom-up RGB24 scanline flip (row 0 > row 479), got %u vs %u\n",
               (unsigned)rgb_buf[0], (unsigned)rgb_buf[(480 - 1) * 640 * 3]);
            free(rgb_buf);
        }
        qcap_avf_destroy(avf_dev);
    }

    hr = QcapCreateAVFoundationReader(0, &reader);
    ok(hr == S_OK && reader != NULL, "QcapCreateAVFoundationReader failed: 0x%08x\n", (unsigned)hr);
    if (reader) {
        GUID major, sub;
        UINT32 w = 0, h = 0, bpp = 0;
        DWORD nv12_size = 640 * 480 * 3 / 2;
        BYTE *nv12_buf = (BYTE *)malloc(nv12_size);
        BYTE pcm_buf[16384];
        DWORD actual_stream = 99, flags = 0, out_bytes = 0;
        LONGLONG ts1 = -1, ts2 = -1;

        hr = QcapAVFoundationReaderGetStreamFormat(
            reader, QCAP_MF_SOURCE_READER_FIRST_VIDEO_STREAM, &major, &sub, &w, &h, &bpp);
        ok(hr == S_OK && IsEqualGUID(&major, &MEDIATYPE_Video) && w == 640 && h == 480,
           "GetStreamFormat(VIDEO) failed\n");

        hr = QcapAVFoundationReaderSetStreamFormat(
            reader, QCAP_MF_SOURCE_READER_FIRST_VIDEO_STREAM, &MEDIASUBTYPE_NV12, 640, 480, 12);
        ok(hr == S_OK, "SetStreamFormat(NV12) failed: 0x%08x\n", (unsigned)hr);

        if (nv12_buf) {
            hr = QcapAVFoundationReaderReadSample(
                reader,
                QCAP_MF_SOURCE_READER_FIRST_VIDEO_STREAM,
                &actual_stream,
                &flags,
                &ts1,
                nv12_buf,
                nv12_size,
                &out_bytes);
            ok(hr == S_OK && actual_stream == 0 && out_bytes == nv12_size && ts1 == 0,
               "ReadSample(VIDEO #1) failed\n");

            hr = QcapAVFoundationReaderReadSample(
                reader,
                QCAP_MF_SOURCE_READER_FIRST_VIDEO_STREAM,
                &actual_stream,
                &flags,
                &ts2,
                nv12_buf,
                nv12_size,
                &out_bytes);
            ok(hr == S_OK && ts2 > ts1, "ReadSample(VIDEO #2) timestamp did not advance\n");
            free(nv12_buf);
        }

        hr = QcapAVFoundationReaderReadSample(
            reader,
            QCAP_MF_SOURCE_READER_FIRST_AUDIO_STREAM,
            &actual_stream,
            &flags,
            &ts1,
            pcm_buf,
            sizeof(pcm_buf),
            &out_bytes);
        ok(hr == S_OK && actual_stream == 1 && out_bytes == 8820 && ts1 == 0,
           "ReadSample(AUDIO) failed: hr=0x%08x bytes=%u\n", (unsigned)hr, (unsigned)out_bytes);

        QcapDestroyAVFoundationReader(reader);
    }
}

START_TEST(qcap)
{
    WINETEST_RUN_SUBTEST(test_audio_record_interfaces);
    WINETEST_RUN_SUBTEST(test_audio_record_aggregation);
    WINETEST_RUN_SUBTEST(test_audio_record_property_bag);
    WINETEST_RUN_SUBTEST(test_audio_record_media_types_and_stream_config);
    WINETEST_RUN_SUBTEST(test_audio_record_capture_stream);
    WINETEST_RUN_SUBTEST(test_video_capture_interfaces_and_caps);
    WINETEST_RUN_SUBTEST(test_avfoundation_video_capture_and_mfreadwrite);
}

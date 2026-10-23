/*
 * Wine Parity Differential Conformance Test: qcap.dll
 * (macOS AVFoundation Video Capture, DirectShow CLSID_AudioRecord, and mfreadwrite Bridge)
 *
 * Clean-room test derived from Microsoft Learn specifications and upstream
 * Wine dlls/qcap/tests/audiorecord.c & dlls/qcap/tests/videocapture.c.
 *
 * Copyright 2026 Wine Parity Contributors
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include <stddef.h>
#include "../../include/parity_test.h"
#include "../../../dlls/qcap/qcap.h"

static int match_filter(const char *filter, const char *case_name)
{
    if (!filter || filter[0] == '\0') {
        return 1;
    }
    return strstr(case_name, filter) != NULL;
}

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

/* Mock IPropertyBag to verify IPersistPropertyBag::Load reads UseMixer and WaveInId */
typedef struct {
    IPropertyBag IPropertyBag_iface;
    int read_count;
    BOOL saw_use_mixer;
    BOOL saw_wave_in_id;
} test_prop_bag_t;

static HRESULT WINAPI test_prop_bag_QueryInterface(IPropertyBag *iface, REFIID riid, void **ppv)
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

static ULONG WINAPI test_prop_bag_AddRef(IPropertyBag *iface)
{
    (void)iface;
    return 2;
}

static ULONG WINAPI test_prop_bag_Release(IPropertyBag *iface)
{
    (void)iface;
    return 1;
}

static HRESULT WINAPI test_prop_bag_Read(
    IPropertyBag *iface,
    LPCWSTR pszPropName,
    VARIANT *pVar,
    IErrorLog *pErrorLog)
{
    test_prop_bag_t *bag = (test_prop_bag_t *)iface;
    (void)pErrorLog;
    if (!pszPropName || !pVar) {
        return E_POINTER;
    }
    if (bag->read_count == 0 && wstr_eq_ascii(pszPropName, "UseMixer")) {
        bag->saw_use_mixer = TRUE;
        bag->read_count++;
        pVar->vt = VT_I4;
        pVar->u.lVal = 0;
        return S_OK;
    }
    if (bag->read_count == 1 && wstr_eq_ascii(pszPropName, "WaveInId")) {
        bag->saw_wave_in_id = TRUE;
        bag->read_count++;
        pVar->vt = VT_I4;
        pVar->u.lVal = 0;
        return S_OK;
    }
    if (wstr_eq_ascii(pszPropName, "VFWIndex")) {
        bag->read_count++;
        pVar->vt = VT_I4;
        pVar->u.lVal = 0;
        return S_OK;
    }
    return E_INVALIDARG;
}

static HRESULT WINAPI test_prop_bag_Write(IPropertyBag *iface, LPCWSTR pszPropName, VARIANT *pVar)
{
    (void)iface;
    (void)pszPropName;
    (void)pVar;
    return S_OK;
}

static const IPropertyBagVtbl test_prop_bag_vtbl = {
    test_prop_bag_QueryInterface,
    test_prop_bag_AddRef,
    test_prop_bag_Release,
    test_prop_bag_Read,
    test_prop_bag_Write
};

/* Mock sink IPin + IMemInputPin to verify CLSID_AudioRecord streaming capture */
typedef struct {
    IPin         IPin_iface;
    IMemInputPin IMemInputPin_iface;
    IPin        *connected_to;
    AM_MEDIA_TYPE connected_mt;
    IMemAllocator *allocator;
    ULONG        received_samples;
    LONG         last_sample_len;
    REFERENCE_TIME last_start_time;
    REFERENCE_TIME last_end_time;
} test_audio_sink_t;

static test_audio_sink_t *sink_from_IPin(IPin *iface)
{
    return (test_audio_sink_t *)((char *)iface - offsetof(test_audio_sink_t, IPin_iface));
}

static test_audio_sink_t *sink_from_IMemInputPin(IMemInputPin *iface)
{
    return (test_audio_sink_t *)((char *)iface - offsetof(test_audio_sink_t, IMemInputPin_iface));
}

static HRESULT WINAPI sink_pin_QueryInterface(IPin *iface, REFIID riid, void **ppv)
{
    test_audio_sink_t *sink = sink_from_IPin(iface);
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

static ULONG WINAPI sink_pin_AddRef(IPin *iface)
{
    (void)iface;
    return 2;
}

static ULONG WINAPI sink_pin_Release(IPin *iface)
{
    (void)iface;
    return 1;
}

static HRESULT WINAPI sink_pin_Connect(IPin *iface, IPin *pReceivePin, const AM_MEDIA_TYPE *pmt)
{
    (void)iface;
    (void)pReceivePin;
    (void)pmt;
    return E_NOTIMPL;
}

static HRESULT WINAPI sink_pin_ReceiveConnection(IPin *iface, IPin *pConnector, const AM_MEDIA_TYPE *pmt)
{
    test_audio_sink_t *sink = sink_from_IPin(iface);
    if (!pConnector || !pmt) {
        return E_POINTER;
    }
    if (sink->connected_to) {
        return VFW_E_ALREADY_CONNECTED;
    }
    if (!IsEqualGUID(&pmt->majortype, &MEDIATYPE_Audio) ||
        !IsEqualGUID(&pmt->subtype, &MEDIASUBTYPE_PCM)) {
        return VFW_E_TYPE_NOT_ACCEPTED;
    }
    sink->connected_to = pConnector;
    sink->connected_mt = *pmt;
    sink->connected_mt.pbFormat = NULL;
    sink->connected_mt.cbFormat = 0;
    return S_OK;
}

static HRESULT WINAPI sink_pin_Disconnect(IPin *iface)
{
    test_audio_sink_t *sink = sink_from_IPin(iface);
    sink->connected_to = NULL;
    if (sink->allocator) {
        IUnknown_Release((IUnknown *)sink->allocator);
        sink->allocator = NULL;
    }
    return S_OK;
}

static HRESULT WINAPI sink_pin_ConnectedTo(IPin *iface, IPin **pPin)
{
    test_audio_sink_t *sink = sink_from_IPin(iface);
    if (!pPin) {
        return E_POINTER;
    }
    *pPin = sink->connected_to;
    if (!sink->connected_to) {
        return VFW_E_NOT_CONNECTED;
    }
    IPin_AddRef(sink->connected_to);
    return S_OK;
}

static HRESULT WINAPI sink_pin_ConnectionMediaType(IPin *iface, AM_MEDIA_TYPE *pmt)
{
    (void)iface;
    (void)pmt;
    return E_NOTIMPL;
}

static HRESULT WINAPI sink_pin_QueryPinInfo(IPin *iface, PIN_INFO *pInfo)
{
    if (!pInfo) {
        return E_POINTER;
    }
    memset(pInfo, 0, sizeof(*pInfo));
    pInfo->dir = PINDIR_INPUT;
    (void)iface;
    return S_OK;
}

static HRESULT WINAPI sink_pin_QueryDirection(IPin *iface, PIN_DIRECTION *pPinDir)
{
    (void)iface;
    if (!pPinDir) {
        return E_POINTER;
    }
    *pPinDir = PINDIR_INPUT;
    return S_OK;
}

static HRESULT WINAPI sink_pin_QueryId(IPin *iface, LPWSTR *Id)
{
    (void)iface;
    (void)Id;
    return E_NOTIMPL;
}

static HRESULT WINAPI sink_pin_QueryAccept(IPin *iface, const AM_MEDIA_TYPE *pmt)
{
    (void)iface;
    if (!pmt) {
        return E_POINTER;
    }
    return IsEqualGUID(&pmt->majortype, &MEDIATYPE_Audio) ? S_OK : S_FALSE;
}

static HRESULT WINAPI sink_pin_EnumMediaTypes(IPin *iface, IEnumMediaTypes **ppEnum)
{
    (void)iface;
    (void)ppEnum;
    return E_NOTIMPL;
}

static HRESULT WINAPI sink_pin_QueryInternalConnections(IPin *iface, IPin **apPin, ULONG *nPin)
{
    (void)iface;
    (void)apPin;
    (void)nPin;
    return E_NOTIMPL;
}

static HRESULT WINAPI sink_pin_EndOfStream(IPin *iface)
{
    (void)iface;
    return S_OK;
}

static HRESULT WINAPI sink_pin_BeginFlush(IPin *iface)
{
    (void)iface;
    return S_OK;
}

static HRESULT WINAPI sink_pin_EndFlush(IPin *iface)
{
    (void)iface;
    return S_OK;
}

static HRESULT WINAPI sink_pin_NewSegment(IPin *iface, REFERENCE_TIME tStart, REFERENCE_TIME tStop, double dRate)
{
    (void)iface;
    (void)tStart;
    (void)tStop;
    (void)dRate;
    return S_OK;
}

static const IPinVtbl sink_pin_vtbl = {
    sink_pin_QueryInterface,
    sink_pin_AddRef,
    sink_pin_Release,
    sink_pin_Connect,
    sink_pin_ReceiveConnection,
    sink_pin_Disconnect,
    sink_pin_ConnectedTo,
    sink_pin_ConnectionMediaType,
    sink_pin_QueryPinInfo,
    sink_pin_QueryDirection,
    sink_pin_QueryId,
    sink_pin_QueryAccept,
    sink_pin_EnumMediaTypes,
    sink_pin_QueryInternalConnections,
    sink_pin_EndOfStream,
    sink_pin_BeginFlush,
    sink_pin_EndFlush,
    sink_pin_NewSegment
};

static HRESULT WINAPI sink_mem_QueryInterface(IMemInputPin *iface, REFIID riid, void **ppv)
{
    test_audio_sink_t *sink = sink_from_IMemInputPin(iface);
    return sink_pin_QueryInterface(&sink->IPin_iface, riid, ppv);
}

static ULONG WINAPI sink_mem_AddRef(IMemInputPin *iface)
{
    (void)iface;
    return 2;
}

static ULONG WINAPI sink_mem_Release(IMemInputPin *iface)
{
    (void)iface;
    return 1;
}

static HRESULT WINAPI sink_mem_GetAllocator(IMemInputPin *iface, IMemAllocator **ppAllocator)
{
    (void)iface;
    if (!ppAllocator) {
        return E_POINTER;
    }
    *ppAllocator = NULL;
    return VFW_E_NO_ALLOCATOR;
}

static HRESULT WINAPI sink_mem_NotifyAllocator(IMemInputPin *iface, IMemAllocator *pAllocator, BOOL bReadOnly)
{
    test_audio_sink_t *sink = sink_from_IMemInputPin(iface);
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

static HRESULT WINAPI sink_mem_GetAllocatorRequirements(IMemInputPin *iface, ALLOCATOR_PROPERTIES *pProps)
{
    (void)iface;
    (void)pProps;
    return E_NOTIMPL;
}

static HRESULT WINAPI sink_mem_Receive(IMemInputPin *iface, IMediaSample *pSample)
{
    test_audio_sink_t *sink = sink_from_IMemInputPin(iface);
    REFERENCE_TIME t_start = 0, t_end = 0;
    if (!pSample) {
        return E_POINTER;
    }
    sink->received_samples++;
    sink->last_sample_len = pSample->lpVtbl->GetActualDataLength(pSample);
    pSample->lpVtbl->GetTime(pSample, &t_start, &t_end);
    sink->last_start_time = t_start;
    sink->last_end_time = t_end;
    return S_OK;
}

static HRESULT WINAPI sink_mem_ReceiveMultiple(
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

static HRESULT WINAPI sink_mem_ReceiveCanBlock(IMemInputPin *iface)
{
    (void)iface;
    return S_OK;
}

static const IMemInputPinVtbl sink_mem_vtbl = {
    sink_mem_QueryInterface,
    sink_mem_AddRef,
    sink_mem_Release,
    sink_mem_GetAllocator,
    sink_mem_NotifyAllocator,
    sink_mem_GetAllocatorRequirements,
    sink_mem_Receive,
    sink_mem_ReceiveMultiple,
    sink_mem_ReceiveCanBlock
};

int main(int argc, char **argv)
{
    static const WCHAR capture_pin_name[] = {'C', 'a', 'p', 't', 'u', 'r', 'e', 0};
    const char *filter = NULL;
    IClassFactory *cf_audio = NULL;
    IClassFactory *cf_video = NULL;
    IBaseFilter *audio_filter = NULL;
    IBaseFilter *video_filter = NULL;
    IAMFilterMiscFlags *misc_flags = NULL;
    IPersistPropertyBag *ppb = NULL;
    IPin *audio_pin = NULL;
    IPin *video_pin = NULL;
    IQualityControl *qc = NULL;
    IAMStreamConfig *audio_sc = NULL;
    IAMBufferNegotiation *buf_neg = NULL;
    IKsPropertySet *ks_props = NULL;
    IAMVideoControl *video_ctrl = NULL;
    IAMVideoProcAmp *proc_amp = NULL;
    QCAP_AVF_DEVICE avf_dev = NULL;
    QCAP_MF_SOURCE_READER *mf_reader = NULL;
    HRESULT hr;
    int i;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--filter") == 0 && (i + 1) < argc) {
            filter = argv[++i];
        }
    }

    parity_begin_document("qcap", "test_qcap_parity", "x86_64", "wine");

    /* 1. DllGetClassObject_Invalid */
    if (match_filter(filter, "dll_get_class_object_invalid")) {
        hr = DllGetClassObject(&CLSID_AudioRecord, &IID_IClassFactory, NULL);
        if (hr == E_INVALIDARG) {
            HRESULT hr_unknown = DllGetClassObject(&GUID_NULL, &IID_IClassFactory, (void **)&cf_audio);
            if (hr_unknown == CLASS_E_CLASSNOTAVAILABLE && cf_audio == NULL) {
                hr = CLASS_E_CLASSNOTAVAILABLE;
            } else {
                hr = E_FAIL;
            }
        }
        parity_record_exact(
            "qcap_001",
            "dll_get_class_object_invalid",
            "DllGetClassObject",
            (NTSTATUS)hr,
            0,
            0,
            NULL,
            0,
            0
        );
    }

    /* 2. DllGetClassObject_AudioRecordAndVfwCapture */
    hr = DllGetClassObject(&CLSID_AudioRecord, &IID_IClassFactory, (void **)&cf_audio);
    if (hr == S_OK && cf_audio != NULL) {
        hr = IClassFactory_CreateInstance(cf_audio, NULL, &IID_IBaseFilter, (void **)&audio_filter);
        IClassFactory_Release(cf_audio);
        cf_audio = NULL;
    }
    if (hr == S_OK && audio_filter != NULL) {
        hr = DllGetClassObject(&CLSID_VfwCapture, &IID_IClassFactory, (void **)&cf_video);
        if (hr == S_OK && cf_video != NULL) {
            hr = IClassFactory_CreateInstance(cf_video, NULL, &IID_IBaseFilter, (void **)&video_filter);
            IClassFactory_Release(cf_video);
            cf_video = NULL;
        }
    }
    if (hr != S_OK || !audio_filter || !video_filter) {
        hr = E_FAIL;
    }
    if (match_filter(filter, "dll_get_class_object_create_filters")) {
        unsigned char tag[2] = {
            (unsigned char)(audio_filter != NULL ? 1 : 0),
            (unsigned char)(video_filter != NULL ? 1 : 0)
        };
        parity_record_roundtrip(
            "qcap_002",
            "dll_get_class_object_create_filters",
            "DllGetClassObject",
            (NTSTATUS)hr,
            0,
            2,
            tag,
            2,
            hr == S_OK ? 1 : 0
        );
    }

    /* 3. AudioRecord_InterfacesAndMiscFlags */
    hr = E_FAIL;
    if (audio_filter) {
        hr = IBaseFilter_QueryInterface(audio_filter, &IID_IAMFilterMiscFlags, (void **)&misc_flags);
        if (hr == S_OK && misc_flags != NULL) {
            ULONG flags = IAMFilterMiscFlags_GetMiscFlags(misc_flags);
            IAMFilterMiscFlags_Release(misc_flags);
            if (flags == AM_FILTER_MISC_FLAGS_IS_SOURCE) {
                hr = IBaseFilter_FindPin(audio_filter, capture_pin_name, &audio_pin);
                if (hr == S_OK && audio_pin != NULL) {
                    hr = IPin_QueryInterface(audio_pin, &IID_IQualityControl, (void **)&qc);
                    if (hr == S_OK && qc != NULL) {
                        IUnknown_Release((IUnknown *)qc);
                        qc = NULL;
                    } else {
                        hr = E_FAIL;
                    }
                }
            } else {
                hr = E_FAIL;
            }
        }
    }
    if (match_filter(filter, "audio_record_interfaces_and_misc_flags")) {
        parity_record_exact(
            "qcap_003",
            "audio_record_interfaces_and_misc_flags",
            "audio_record_create",
            (NTSTATUS)hr,
            0,
            0,
            NULL,
            0,
            hr == S_OK ? 1 : 0
        );
    }

    /* 4. AudioRecord_PersistPropertyBag */
    if (match_filter(filter, "audio_record_persist_property_bag")) {
        hr = E_FAIL;
        if (audio_filter) {
            hr = IBaseFilter_QueryInterface(audio_filter, &IID_IPersistPropertyBag, (void **)&ppb);
            if (hr == S_OK && ppb != NULL) {
                test_prop_bag_t bag;
                memset(&bag, 0, sizeof(bag));
                bag.IPropertyBag_iface.lpVtbl = &test_prop_bag_vtbl;
                hr = IPersistPropertyBag_InitNew(ppb);
                if (hr == S_OK) {
                    hr = IPersistPropertyBag_Load(ppb, &bag.IPropertyBag_iface, NULL);
                    if (hr != S_OK || !bag.saw_use_mixer || !bag.saw_wave_in_id || bag.read_count != 2) {
                        hr = E_FAIL;
                    }
                }
                IPersistPropertyBag_Release(ppb);
                ppb = NULL;
            }
        }
        parity_record_exact(
            "qcap_004",
            "audio_record_persist_property_bag",
            "audio_record_create",
            (NTSTATUS)hr,
            0,
            0,
            NULL,
            0,
            hr == S_OK ? 1 : 0
        );
    }

    /* 5. AudioRecord_StreamConfigAndFormatValidation */
    if (match_filter(filter, "audio_record_stream_config_and_format")) {
        hr = E_FAIL;
        if (audio_pin) {
            hr = IPin_QueryInterface(audio_pin, &IID_IAMStreamConfig, (void **)&audio_sc);
            if (hr == S_OK && audio_sc != NULL) {
                int count = 0, size = 0;
                hr = IAMStreamConfig_GetNumberOfCapabilities(audio_sc, &count, &size);
                if (hr == S_OK && count == 12 && size == (int)sizeof(AUDIO_STREAM_CONFIG_CAPS)) {
                    AM_MEDIA_TYPE *mt = NULL;
                    AUDIO_STREAM_CONFIG_CAPS ascc;
                    memset(&ascc, 0, sizeof(ascc));
                    hr = IAMStreamConfig_GetStreamCaps(audio_sc, 0, &mt, (BYTE *)&ascc);
                    if (hr == S_OK && mt != NULL && mt->pbFormat != NULL) {
                        WAVEFORMATEX *wfx = (WAVEFORMATEX *)mt->pbFormat;
                        if (wfx->wFormatTag == WAVE_FORMAT_PCM &&
                            wfx->nSamplesPerSec == 44100 &&
                            wfx->wBitsPerSample == 16 &&
                            wfx->nChannels == 2 &&
                            IPin_QueryAccept(audio_pin, mt) == S_OK) {
                            wfx->wFormatTag = 0xdead;
                            if (IPin_QueryAccept(audio_pin, mt) == S_FALSE) {
                                hr = S_OK;
                            } else {
                                hr = E_FAIL;
                            }
                        } else {
                            hr = E_FAIL;
                        }
                        qcap_delete_media_type(mt);
                    } else {
                        hr = E_FAIL;
                    }
                } else {
                    hr = E_FAIL;
                }
                IAMStreamConfig_Release(audio_sc);
                audio_sc = NULL;
            }
        }
        parity_record_exact(
            "qcap_005",
            "audio_record_stream_config_and_format",
            "audio_record_create",
            (NTSTATUS)hr,
            0,
            0,
            NULL,
            0,
            hr == S_OK ? 1 : 0
        );
    }

    /* 6. AudioRecord_BufferNegotiationAndKsPropertySet */
    if (match_filter(filter, "audio_record_buffer_neg_and_ks_props")) {
        hr = E_FAIL;
        if (audio_pin) {
            hr = IPin_QueryInterface(audio_pin, &IID_IAMBufferNegotiation, (void **)&buf_neg);
            if (hr == S_OK && buf_neg != NULL) {
                ALLOCATOR_PROPERTIES props = {4, 17640, 4, 0};
                ALLOCATOR_PROPERTIES actual_props;
                HRESULT hr_disconn = IAMBufferNegotiation_GetAllocatorProperties(buf_neg, &actual_props);
                HRESULT hr_suggest = IAMBufferNegotiation_SuggestAllocatorProperties(buf_neg, &props);
                IAMBufferNegotiation_Release(buf_neg);
                buf_neg = NULL;
                if (hr_disconn == VFW_E_NOT_CONNECTED && hr_suggest == S_OK) {
                    hr = IPin_QueryInterface(audio_pin, &IID_IKsPropertySet, (void **)&ks_props);
                    if (hr == S_OK && ks_props != NULL) {
                        DWORD supp = 0, ret_bytes = 0;
                        GUID cat_guid;
                        memset(&cat_guid, 0, sizeof(cat_guid));
                        hr = IKsPropertySet_QuerySupported(
                            ks_props, &AMPROPSETID_Pin, AMPROPERTY_PIN_CATEGORY, &supp);
                        if (hr == S_OK && supp == KSPROPERTY_SUPPORT_GET) {
                            hr = IKsPropertySet_Get(
                                ks_props,
                                &AMPROPSETID_Pin,
                                AMPROPERTY_PIN_CATEGORY,
                                NULL,
                                0,
                                &cat_guid,
                                sizeof(cat_guid),
                                &ret_bytes);
                            if (hr != S_OK || ret_bytes != sizeof(GUID) ||
                                !IsEqualGUID(&cat_guid, &PIN_CATEGORY_CAPTURE)) {
                                hr = E_FAIL;
                            }
                        } else {
                            hr = E_FAIL;
                        }
                        IKsPropertySet_Release(ks_props);
                        ks_props = NULL;
                    }
                } else {
                    hr = E_FAIL;
                }
            }
        }
        parity_record_exact(
            "qcap_006",
            "audio_record_buffer_neg_and_ks_props",
            "audio_record_create",
            (NTSTATUS)hr,
            0,
            0,
            NULL,
            0,
            hr == S_OK ? 1 : 0
        );
    }

    /* 7. AudioRecord_CaptureStreamDelivery */
    if (match_filter(filter, "audio_record_capture_stream_delivery")) {
        hr = E_FAIL;
        if (audio_filter && audio_pin) {
            test_audio_sink_t sink;
            memset(&sink, 0, sizeof(sink));
            sink.IPin_iface.lpVtbl = &sink_pin_vtbl;
            sink.IMemInputPin_iface.lpVtbl = &sink_mem_vtbl;

            hr = IPin_Connect(audio_pin, &sink.IPin_iface, NULL);
            if (hr == S_OK) {
                hr = IBaseFilter_Pause(audio_filter);
                if (hr == S_OK) {
                    hr = IBaseFilter_Run(audio_filter, 0);
                    if (hr == S_OK) {
                        IBaseFilter_Stop(audio_filter);
                        if (sink.received_samples >= 1 &&
                            sink.last_sample_len > 0 &&
                            sink.last_end_time > sink.last_start_time) {
                            hr = S_OK;
                        } else {
                            hr = E_FAIL;
                        }
                    }
                }
                IPin_Disconnect(audio_pin);
                IPin_Disconnect(&sink.IPin_iface);
            }
        }
        parity_record_exact(
            "qcap_007",
            "audio_record_capture_stream_delivery",
            "audio_record_create",
            (NTSTATUS)hr,
            0,
            0,
            NULL,
            0,
            hr == S_OK ? 1 : 0
        );
    }

    /* 8. AVFoundation_DeviceCapsAndFormatNegotiation */
    hr = qcap_avf_create(0, &avf_dev);
    if (hr == S_OK && avf_dev != NULL) {
        int caps_count = qcap_avf_get_caps_count(avf_dev);
        AM_MEDIA_TYPE mt_cap;
        VIDEO_STREAM_CONFIG_CAPS vscc;
        memset(&mt_cap, 0, sizeof(mt_cap));
        memset(&vscc, 0, sizeof(vscc));
        if (caps_count >= 3 && qcap_avf_get_caps(avf_dev, 0, &mt_cap, &vscc) == S_OK && mt_cap.pbFormat) {
            VIDEOINFOHEADER *vih = (VIDEOINFOHEADER *)mt_cap.pbFormat;
            vih->bmiHeader.biBitCount = 0;
            vih->bmiHeader.biCompression = 0;
            hr = qcap_avf_set_format(avf_dev, &mt_cap);
            qcap_free_media_type_contents(&mt_cap);
            if (hr == S_OK) {
                AM_MEDIA_TYPE mt_cur;
                memset(&mt_cur, 0, sizeof(mt_cur));
                hr = qcap_avf_get_format(avf_dev, &mt_cur);
                if (hr == S_OK && mt_cur.pbFormat) {
                    VIDEOINFOHEADER *vih_cur = (VIDEOINFOHEADER *)mt_cur.pbFormat;
                    if (vih_cur->bmiHeader.biWidth != 640 ||
                        vih_cur->bmiHeader.biHeight != 480 ||
                        vih_cur->bmiHeader.biBitCount != 24) {
                        hr = E_FAIL;
                    }
                    qcap_free_media_type_contents(&mt_cur);
                } else {
                    hr = E_FAIL;
                }
            }
        } else {
            hr = E_FAIL;
        }
    } else {
        hr = E_FAIL;
    }
    if (match_filter(filter, "avf_device_caps_and_format_negotiation")) {
        parity_record_exact(
            "qcap_008",
            "avf_device_caps_and_format_negotiation",
            "qcap_avf_create",
            (NTSTATUS)hr,
            0,
            0,
            NULL,
            0,
            hr == S_OK ? 1 : 0
        );
    }

    /* 9. AVFoundation_VideoControlFrameRates */
    if (match_filter(filter, "avf_video_control_frame_rates")) {
        hr = E_FAIL;
        if (video_filter) {
            hr = IBaseFilter_FindPin(video_filter, capture_pin_name, &video_pin);
            if (hr == S_OK && video_pin != NULL) {
                HRESULT hr_null_accept = IPin_QueryAccept(video_pin, NULL);
                hr = IBaseFilter_QueryInterface(video_filter, &IID_IAMVideoControl, (void **)&video_ctrl);
                if (hr_null_accept == E_POINTER && hr == S_OK && video_ctrl != NULL) {
                    SIZE dims = {640, 480};
                    LONG list_size = 0;
                    LONGLONG *rates = NULL;
                    hr = IAMVideoControl_GetFrameRateList(video_ctrl, video_pin, 0, dims, &list_size, &rates);
                    if (hr == S_OK && list_size >= 1 && rates != NULL &&
                        rates[0] >= 166666 && rates[0] <= 333333) {
                        CoTaskMemFree(rates);
                        hr = S_OK;
                    } else {
                        CoTaskMemFree(rates);
                        hr = E_FAIL;
                    }
                    IAMVideoControl_Release(video_ctrl);
                    video_ctrl = NULL;
                } else {
                    hr = E_FAIL;
                }
            }
        }
        parity_record_exact(
            "qcap_009",
            "avf_video_control_frame_rates",
            "qcap_avf_create",
            (NTSTATUS)hr,
            0,
            0,
            NULL,
            0,
            hr == S_OK ? 1 : 0
        );
    }

    /* 10. AVFoundation_VideoProcAmpRangeAndSet */
    if (match_filter(filter, "avf_video_proc_amp_range_and_set")) {
        hr = E_FAIL;
        if (video_filter) {
            hr = IBaseFilter_QueryInterface(video_filter, &IID_IAMVideoProcAmp, (void **)&proc_amp);
            if (hr == S_OK && proc_amp != NULL) {
                LONG min_v = 0, max_v = 0, step_v = 0, def_v = 0, flags_v = 0;
                hr = IAMVideoProcAmp_GetRange(
                    proc_amp, VideoProcAmp_Brightness, &min_v, &max_v, &step_v, &def_v, &flags_v);
                if (hr == S_OK && min_v < max_v && step_v > 0) {
                    LONG target_val = def_v + step_v;
                    LONG read_val = 0, read_flags = 0;
                    hr = IAMVideoProcAmp_Set(
                        proc_amp, VideoProcAmp_Brightness, target_val, VideoProcAmp_Flags_Manual);
                    if (hr == S_OK &&
                        IAMVideoProcAmp_Get(proc_amp, VideoProcAmp_Brightness, &read_val, &read_flags) == S_OK &&
                        read_val == target_val &&
                        IAMVideoProcAmp_GetRange(
                            proc_amp, 999, &min_v, &max_v, &step_v, &def_v, &flags_v) == E_PROP_ID_UNSUPPORTED) {
                        hr = S_OK;
                    } else {
                        hr = E_FAIL;
                    }
                } else {
                    hr = E_FAIL;
                }
                IAMVideoProcAmp_Release(proc_amp);
                proc_amp = NULL;
            }
        }
        parity_record_exact(
            "qcap_010",
            "avf_video_proc_amp_range_and_set",
            "qcap_avf_create",
            (NTSTATUS)hr,
            0,
            0,
            NULL,
            0,
            hr == S_OK ? 1 : 0
        );
    }

    /* 11. AVFoundation_ReadFrameBottomUpRGB24 */
    if (match_filter(filter, "avf_read_frame_bottom_up_rgb24")) {
        DWORD written = 0;
        hr = E_FAIL;
        if (avf_dev) {
            DWORD frame_bytes = 640 * 480 * 3;
            BYTE *frame_buf = (BYTE *)malloc(frame_bytes);
            if (frame_buf) {
                BOOL is_bottom_up = FALSE;
                memset(frame_buf, 0, frame_bytes);
                hr = qcap_avf_read_frame(avf_dev, frame_buf, frame_bytes, &written, &is_bottom_up);
                if (hr == S_OK && written == frame_bytes && is_bottom_up == TRUE &&
                    frame_buf[0] != frame_buf[(480 - 1) * 640 * 3]) {
                    hr = S_OK;
                } else {
                    hr = E_FAIL;
                }
                free(frame_buf);
            }
        }
        parity_record_exact(
            "qcap_011",
            "avf_read_frame_bottom_up_rgb24",
            "qcap_avf_read_frame",
            (NTSTATUS)hr,
            0,
            written,
            NULL,
            0,
            hr == S_OK ? 1 : 0
        );
    }

    /* 12. MFSourceReader_AVFoundationVideoAndAudioCapture */
    if (match_filter(filter, "mf_source_reader_avf_video_and_audio")) {
        DWORD video_bytes = 0;
        hr = QcapCreateAVFoundationReader(0, &mf_reader);
        if (hr == S_OK && mf_reader != NULL) {
            hr = QcapAVFoundationReaderSetStreamFormat(
                mf_reader, QCAP_MF_SOURCE_READER_FIRST_VIDEO_STREAM, &MEDIASUBTYPE_NV12, 640, 480, 12);
            if (hr == S_OK) {
                DWORD nv12_cap = 640 * 480 * 3 / 2;
                BYTE *nv12_buf = (BYTE *)malloc(nv12_cap);
                BYTE pcm_buf[8192];
                DWORD actual_stream = 0, flags = 0, audio_bytes = 0;
                LONGLONG ts_video = -1, ts_audio = -1;
                if (nv12_buf) {
                    HRESULT hr_v = QcapAVFoundationReaderReadSample(
                        mf_reader,
                        QCAP_MF_SOURCE_READER_FIRST_VIDEO_STREAM,
                        &actual_stream,
                        &flags,
                        &ts_video,
                        nv12_buf,
                        nv12_cap,
                        &video_bytes);
                    HRESULT hr_a = QcapAVFoundationReaderReadSample(
                        mf_reader,
                        QCAP_MF_SOURCE_READER_FIRST_AUDIO_STREAM,
                        &actual_stream,
                        &flags,
                        &ts_audio,
                        pcm_buf,
                        sizeof(pcm_buf),
                        &audio_bytes);
                    if (hr_v == S_OK && video_bytes == nv12_cap && ts_video >= 0 &&
                        hr_a == S_OK && audio_bytes > 0 && ts_audio >= 0) {
                        hr = S_OK;
                    } else {
                        hr = E_FAIL;
                    }
                    free(nv12_buf);
                } else {
                    hr = E_OUTOFMEMORY;
                }
            }
            QcapDestroyAVFoundationReader(mf_reader);
            mf_reader = NULL;
        } else {
            hr = E_FAIL;
        }
        parity_record_exact(
            "qcap_012",
            "mf_source_reader_avf_video_and_audio",
            "QcapCreateAVFoundationReader",
            (NTSTATUS)hr,
            0,
            video_bytes,
            NULL,
            0,
            hr == S_OK ? 1 : 0
        );
    }

    if (avf_dev) {
        qcap_avf_destroy(avf_dev);
    }
    if (video_pin) {
        IPin_Release(video_pin);
    }
    if (audio_pin) {
        IPin_Release(audio_pin);
    }
    if (video_filter) {
        IBaseFilter_Release(video_filter);
    }
    if (audio_filter) {
        IBaseFilter_Release(audio_filter);
    }

    parity_end_document();
    return 0;
}

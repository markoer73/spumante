/*
 * Wine qcap.dll DirectShow CLSID_AudioRecord Filter (macOS CoreAudio / AudioToolbox)
 *
 * Clean-room implementation derived from Microsoft Learn DirectShow Audio
 * Capture Filter specifications, Apple CoreAudio / AudioToolbox documentation,
 * and upstream Wine dlls/qcap/audiorecord.c.
 *
 * Copyright 2026 Wine Parity Contributors
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include <stddef.h>
#include "qcap.h"

#ifdef __APPLE__
#include <AudioToolbox/AudioToolbox.h>
#include <CoreAudio/CoreAudio.h>
#endif

WINE_DEFAULT_DEBUG_CHANNEL(qcap);

static const WCHAR k_capture_pin_name[] = {'C', 'a', 'p', 't', 'u', 'r', 'e', 0};
static const WCHAR k_prop_use_mixer[]   = {'U', 's', 'e', 'M', 'i', 'x', 'e', 'r', 0};
static const WCHAR k_prop_wave_in_id[]  = {'W', 'a', 'v', 'e', 'I', 'n', 'I', 'd', 0};
static const WCHAR k_prop_wave_in_ID[]  = {'W', 'a', 'v', 'e', 'I', 'n', 'I', 'D', 0};

struct audio_cap_spec {
    DWORD samples_per_sec;
    WORD  bits_per_sample;
    WORD  channels;
};

static const struct audio_cap_spec k_audio_caps[12] = {
    {44100, 16, 2},
    {44100, 16, 1},
    {44100,  8, 2},
    {44100,  8, 1},
    {22050, 16, 2},
    {22050, 16, 1},
    {22050,  8, 2},
    {22050,  8, 1},
    {11025, 16, 2},
    {11025, 16, 1},
    {11025,  8, 2},
    { 8000,  8, 1}
};

struct audio_media_sample {
    IMediaSample   IMediaSample_iface;
    LONG           refcount;
    BYTE          *buffer;
    LONG           capacity;
    LONG           actual_len;
    REFERENCE_TIME time_start;
    REFERENCE_TIME time_end;
    LONGLONG       media_start;
    LONGLONG       media_end;
    BOOL           has_time;
    BOOL           has_media_time;
    BOOL           sync_point;
    BOOL           preroll;
    BOOL           discontinuity;
};

struct audio_mem_allocator {
    IMemAllocator        IMemAllocator_iface;
    LONG                 refcount;
    ALLOCATOR_PROPERTIES props;
    BOOL                 committed;
};

struct audio_record {
    IUnknown             IUnknown_inner;
    IBaseFilter          IBaseFilter_iface;
    IPersistPropertyBag  IPersistPropertyBag_iface;
    IAMFilterMiscFlags   IAMFilterMiscFlags_iface;
    IPin                 IPin_iface;
    IAMStreamConfig      IAMStreamConfig_iface;
    IAMBufferNegotiation IAMBufferNegotiation_iface;
    IKsPropertySet       IKsPropertySet_iface;
    IQualityControl      IQualityControl_iface;
    IUnknown            *outer_unk;
    LONG                 refcount;

    FILTER_STATE         state;
    IReferenceClock     *clock;
    IFilterGraph        *graph;
    WCHAR                filter_name[128];

    LONG                 device_id;
    LONG                 use_mixer;
    UINT32               coreaudio_device_id;
    double               coreaudio_sample_rate;

    AM_MEDIA_TYPE        format;
    WAVEFORMATEX         wfx;
    ALLOCATOR_PROPERTIES suggested_props;
    ALLOCATOR_PROPERTIES active_props;

    IPin                *peer_pin;
    IMemInputPin        *peer_mem_input;
    IMemAllocator       *allocator;
    IQualityControl     *qc_sink;
    REFERENCE_TIME       stream_pts;
};

static int wstr_cmp(const WCHAR *a, const WCHAR *b)
{
    if (!a || !b) {
        return a == b ? 0 : (a ? 1 : -1);
    }
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return (int)*a - (int)*b;
}

static size_t wstr_len(const WCHAR *s)
{
    size_t n = 0;
    if (!s) {
        return 0;
    }
    while (s[n]) {
        n++;
    }
    return n;
}

static void wstr_copy(WCHAR *dst, size_t dst_cch, const WCHAR *src)
{
    size_t i = 0;
    if (!dst || dst_cch == 0) {
        return;
    }
    if (src) {
        while (src[i] && (i + 1) < dst_cch) {
            dst[i] = src[i];
            i++;
        }
    }
    dst[i] = 0;
}

static void init_pcm_media_type(
    AM_MEDIA_TYPE *mt,
    WAVEFORMATEX *wfx,
    DWORD samples_per_sec,
    WORD bits_per_sample,
    WORD channels)
{
    memset(wfx, 0, sizeof(*wfx));
    wfx->wFormatTag = WAVE_FORMAT_PCM;
    wfx->nChannels = channels;
    wfx->nSamplesPerSec = samples_per_sec;
    wfx->wBitsPerSample = bits_per_sample;
    wfx->nBlockAlign = (WORD)((channels * bits_per_sample) / 8);
    wfx->nAvgBytesPerSec = samples_per_sec * (DWORD)wfx->nBlockAlign;
    wfx->cbSize = 0;

    memset(mt, 0, sizeof(*mt));
    mt->majortype = MEDIATYPE_Audio;
    mt->subtype = MEDIASUBTYPE_PCM;
    mt->bFixedSizeSamples = TRUE;
    mt->bTemporalCompression = FALSE;
    mt->lSampleSize = wfx->nBlockAlign;
    mt->formattype = FORMAT_WaveFormatEx;
    mt->pUnk = NULL;
    mt->cbFormat = sizeof(WAVEFORMATEX);
    mt->pbFormat = (BYTE *)wfx;
}

static HRESULT copy_media_type(AM_MEDIA_TYPE *dst, const AM_MEDIA_TYPE *src)
{
    if (!dst || !src) {
        return E_POINTER;
    }
    *dst = *src;
    dst->pbFormat = NULL;
    if (src->cbFormat && src->pbFormat) {
        dst->pbFormat = (BYTE *)CoTaskMemAlloc(src->cbFormat);
        if (!dst->pbFormat) {
            dst->cbFormat = 0;
            return E_OUTOFMEMORY;
        }
        memcpy(dst->pbFormat, src->pbFormat, src->cbFormat);
    }
    if (dst->pUnk) {
        IUnknown_AddRef(dst->pUnk);
    }
    return S_OK;
}

static HRESULT duplicate_media_type(AM_MEDIA_TYPE **out_mt, const AM_MEDIA_TYPE *src)
{
    AM_MEDIA_TYPE *mt;
    HRESULT hr;

    if (!out_mt || !src) {
        return E_POINTER;
    }
    *out_mt = NULL;
    mt = (AM_MEDIA_TYPE *)CoTaskMemAlloc(sizeof(AM_MEDIA_TYPE));
    if (!mt) {
        return E_OUTOFMEMORY;
    }
    hr = copy_media_type(mt, src);
    if (FAILED(hr)) {
        CoTaskMemFree(mt);
        return hr;
    }
    *out_mt = mt;
    return S_OK;
}

/* IMediaSample implementation for audio capture buffer delivery */
static inline struct audio_media_sample *impl_from_IMediaSample(IMediaSample *iface)
{
    return (struct audio_media_sample *)((char *)iface - offsetof(struct audio_media_sample, IMediaSample_iface));
}

static HRESULT WINAPI audio_sample_QueryInterface(IMediaSample *iface, REFIID riid, void **ppv)
{
    if (!ppv) {
        return E_POINTER;
    }
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IMediaSample)) {
        *ppv = iface;
        iface->lpVtbl->AddRef(iface);
        return S_OK;
    }
    *ppv = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI audio_sample_AddRef(IMediaSample *iface)
{
    struct audio_media_sample *sample = impl_from_IMediaSample(iface);
    return (ULONG)__sync_add_and_fetch(&sample->refcount, 1);
}

static ULONG WINAPI audio_sample_Release(IMediaSample *iface)
{
    struct audio_media_sample *sample = impl_from_IMediaSample(iface);
    LONG ref = __sync_sub_and_fetch(&sample->refcount, 1);
    if (ref == 0) {
        free(sample->buffer);
        free(sample);
    }
    return (ULONG)ref;
}

static HRESULT WINAPI audio_sample_GetPointer(IMediaSample *iface, BYTE **ppBuffer)
{
    struct audio_media_sample *sample = impl_from_IMediaSample(iface);
    if (!ppBuffer) {
        return E_POINTER;
    }
    *ppBuffer = sample->buffer;
    return S_OK;
}

static LONG WINAPI audio_sample_GetSize(IMediaSample *iface)
{
    struct audio_media_sample *sample = impl_from_IMediaSample(iface);
    return sample->capacity;
}

static HRESULT WINAPI audio_sample_GetTime(
    IMediaSample *iface,
    REFERENCE_TIME *pTimeStart,
    REFERENCE_TIME *pTimeEnd)
{
    struct audio_media_sample *sample = impl_from_IMediaSample(iface);
    if (!pTimeStart || !pTimeEnd) {
        return E_POINTER;
    }
    if (!sample->has_time) {
        return E_FAIL;
    }
    *pTimeStart = sample->time_start;
    *pTimeEnd = sample->time_end;
    return S_OK;
}

static HRESULT WINAPI audio_sample_SetTime(
    IMediaSample *iface,
    REFERENCE_TIME *pTimeStart,
    REFERENCE_TIME *pTimeEnd)
{
    struct audio_media_sample *sample = impl_from_IMediaSample(iface);
    if (!pTimeStart) {
        sample->has_time = FALSE;
        return S_OK;
    }
    sample->time_start = *pTimeStart;
    sample->time_end = pTimeEnd ? *pTimeEnd : *pTimeStart;
    sample->has_time = TRUE;
    return S_OK;
}

static HRESULT WINAPI audio_sample_IsSyncPoint(IMediaSample *iface)
{
    struct audio_media_sample *sample = impl_from_IMediaSample(iface);
    return sample->sync_point ? S_OK : S_FALSE;
}

static HRESULT WINAPI audio_sample_SetSyncPoint(IMediaSample *iface, BOOL bIsSyncPoint)
{
    struct audio_media_sample *sample = impl_from_IMediaSample(iface);
    sample->sync_point = bIsSyncPoint;
    return S_OK;
}

static HRESULT WINAPI audio_sample_IsPreroll(IMediaSample *iface)
{
    struct audio_media_sample *sample = impl_from_IMediaSample(iface);
    return sample->preroll ? S_OK : S_FALSE;
}

static HRESULT WINAPI audio_sample_SetPreroll(IMediaSample *iface, BOOL bIsPreroll)
{
    struct audio_media_sample *sample = impl_from_IMediaSample(iface);
    sample->preroll = bIsPreroll;
    return S_OK;
}

static LONG WINAPI audio_sample_GetActualDataLength(IMediaSample *iface)
{
    struct audio_media_sample *sample = impl_from_IMediaSample(iface);
    return sample->actual_len;
}

static HRESULT WINAPI audio_sample_SetActualDataLength(IMediaSample *iface, LONG lLen)
{
    struct audio_media_sample *sample = impl_from_IMediaSample(iface);
    if (lLen < 0 || lLen > sample->capacity) {
        return E_INVALIDARG;
    }
    sample->actual_len = lLen;
    return S_OK;
}

static HRESULT WINAPI audio_sample_GetMediaType(IMediaSample *iface, AM_MEDIA_TYPE **ppMediaType)
{
    (void)iface;
    if (!ppMediaType) {
        return E_POINTER;
    }
    *ppMediaType = NULL;
    return S_FALSE;
}

static HRESULT WINAPI audio_sample_SetMediaType(IMediaSample *iface, AM_MEDIA_TYPE *pMediaType)
{
    (void)iface;
    (void)pMediaType;
    return S_OK;
}

static HRESULT WINAPI audio_sample_IsDiscontinuity(IMediaSample *iface)
{
    struct audio_media_sample *sample = impl_from_IMediaSample(iface);
    return sample->discontinuity ? S_OK : S_FALSE;
}

static HRESULT WINAPI audio_sample_SetDiscontinuity(IMediaSample *iface, BOOL bDiscontinuity)
{
    struct audio_media_sample *sample = impl_from_IMediaSample(iface);
    sample->discontinuity = bDiscontinuity;
    return S_OK;
}

static HRESULT WINAPI audio_sample_GetMediaTime(
    IMediaSample *iface,
    LONGLONG *pTimeStart,
    LONGLONG *pTimeEnd)
{
    struct audio_media_sample *sample = impl_from_IMediaSample(iface);
    if (!pTimeStart || !pTimeEnd) {
        return E_POINTER;
    }
    if (!sample->has_media_time) {
        return E_FAIL;
    }
    *pTimeStart = sample->media_start;
    *pTimeEnd = sample->media_end;
    return S_OK;
}

static HRESULT WINAPI audio_sample_SetMediaTime(
    IMediaSample *iface,
    LONGLONG *pTimeStart,
    LONGLONG *pTimeEnd)
{
    struct audio_media_sample *sample = impl_from_IMediaSample(iface);
    if (!pTimeStart || !pTimeEnd) {
        sample->has_media_time = FALSE;
        return S_OK;
    }
    sample->media_start = *pTimeStart;
    sample->media_end = *pTimeEnd;
    sample->has_media_time = TRUE;
    return S_OK;
}

static const IMediaSampleVtbl audio_sample_vtbl = {
    audio_sample_QueryInterface,
    audio_sample_AddRef,
    audio_sample_Release,
    audio_sample_GetPointer,
    audio_sample_GetSize,
    audio_sample_GetTime,
    audio_sample_SetTime,
    audio_sample_IsSyncPoint,
    audio_sample_SetSyncPoint,
    audio_sample_IsPreroll,
    audio_sample_SetPreroll,
    audio_sample_GetActualDataLength,
    audio_sample_SetActualDataLength,
    audio_sample_GetMediaType,
    audio_sample_SetMediaType,
    audio_sample_IsDiscontinuity,
    audio_sample_SetDiscontinuity,
    audio_sample_GetMediaTime,
    audio_sample_SetMediaTime
};

/* IMemAllocator implementation */
static inline struct audio_mem_allocator *impl_from_IMemAllocator(IMemAllocator *iface)
{
    return (struct audio_mem_allocator *)((char *)iface - offsetof(struct audio_mem_allocator, IMemAllocator_iface));
}

static HRESULT WINAPI audio_alloc_QueryInterface(IMemAllocator *iface, REFIID riid, void **ppv)
{
    if (!ppv) {
        return E_POINTER;
    }
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IMemAllocator)) {
        *ppv = iface;
        iface->lpVtbl->AddRef(iface);
        return S_OK;
    }
    *ppv = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI audio_alloc_AddRef(IMemAllocator *iface)
{
    struct audio_mem_allocator *alloc = impl_from_IMemAllocator(iface);
    return (ULONG)__sync_add_and_fetch(&alloc->refcount, 1);
}

static ULONG WINAPI audio_alloc_Release(IMemAllocator *iface)
{
    struct audio_mem_allocator *alloc = impl_from_IMemAllocator(iface);
    LONG ref = __sync_sub_and_fetch(&alloc->refcount, 1);
    if (ref == 0) {
        free(alloc);
    }
    return (ULONG)ref;
}

static HRESULT WINAPI audio_alloc_SetProperties(
    IMemAllocator *iface,
    ALLOCATOR_PROPERTIES *pRequest,
    ALLOCATOR_PROPERTIES *pActual)
{
    struct audio_mem_allocator *alloc = impl_from_IMemAllocator(iface);
    if (!pRequest || !pActual) {
        return E_POINTER;
    }
    alloc->props.cBuffers = pRequest->cBuffers > 0 ? pRequest->cBuffers : 4;
    alloc->props.cbBuffer = pRequest->cbBuffer > 0 ? pRequest->cbBuffer : 17640;
    alloc->props.cbAlign  = pRequest->cbAlign > 0 ? pRequest->cbAlign : 4;
    alloc->props.cbPrefix = pRequest->cbPrefix >= 0 ? pRequest->cbPrefix : 0;
    *pActual = alloc->props;
    return S_OK;
}

static HRESULT WINAPI audio_alloc_GetProperties(IMemAllocator *iface, ALLOCATOR_PROPERTIES *pProps)
{
    struct audio_mem_allocator *alloc = impl_from_IMemAllocator(iface);
    if (!pProps) {
        return E_POINTER;
    }
    *pProps = alloc->props;
    return S_OK;
}

static HRESULT WINAPI audio_alloc_Commit(IMemAllocator *iface)
{
    struct audio_mem_allocator *alloc = impl_from_IMemAllocator(iface);
    alloc->committed = TRUE;
    return S_OK;
}

static HRESULT WINAPI audio_alloc_Decommit(IMemAllocator *iface)
{
    struct audio_mem_allocator *alloc = impl_from_IMemAllocator(iface);
    alloc->committed = FALSE;
    return S_OK;
}

static HRESULT WINAPI audio_alloc_GetBuffer(
    IMemAllocator *iface,
    IMediaSample **ppBuffer,
    REFERENCE_TIME *pStartTime,
    REFERENCE_TIME *pEndTime,
    DWORD dwFlags)
{
    struct audio_mem_allocator *alloc = impl_from_IMemAllocator(iface);
    struct audio_media_sample *sample;
    LONG buf_size;
    (void)dwFlags;

    if (!ppBuffer) {
        return E_POINTER;
    }
    *ppBuffer = NULL;
    buf_size = alloc->props.cbBuffer > 0 ? alloc->props.cbBuffer : 17640;
    sample = (struct audio_media_sample *)calloc(1, sizeof(*sample));
    if (!sample) {
        return E_OUTOFMEMORY;
    }
    sample->buffer = (BYTE *)calloc(1, (size_t)buf_size);
    if (!sample->buffer) {
        free(sample);
        return E_OUTOFMEMORY;
    }
    sample->IMediaSample_iface.lpVtbl = &audio_sample_vtbl;
    sample->refcount = 1;
    sample->capacity = buf_size;
    sample->actual_len = buf_size;
    sample->sync_point = TRUE;
    if (pStartTime && pEndTime) {
        sample->time_start = *pStartTime;
        sample->time_end = *pEndTime;
        sample->has_time = TRUE;
    }
    *ppBuffer = &sample->IMediaSample_iface;
    return S_OK;
}

static HRESULT WINAPI audio_alloc_ReleaseBuffer(IMemAllocator *iface, IMediaSample *pBuffer)
{
    (void)iface;
    (void)pBuffer;
    return S_OK;
}

static const IMemAllocatorVtbl audio_alloc_vtbl = {
    audio_alloc_QueryInterface,
    audio_alloc_AddRef,
    audio_alloc_Release,
    audio_alloc_SetProperties,
    audio_alloc_GetProperties,
    audio_alloc_Commit,
    audio_alloc_Decommit,
    audio_alloc_GetBuffer,
    audio_alloc_ReleaseBuffer
};

/* IEnumPins implementation for CLSID_AudioRecord */
struct audio_enum_pins {
    IEnumPins   IEnumPins_iface;
    LONG        refcount;
    ULONG       index;
    IBaseFilter *filter;
    IPin        *pin;
};

static inline struct audio_enum_pins *impl_from_IEnumPins(IEnumPins *iface)
{
    return (struct audio_enum_pins *)((char *)iface - offsetof(struct audio_enum_pins, IEnumPins_iface));
}

static HRESULT WINAPI audio_enum_pins_QueryInterface(IEnumPins *iface, REFIID riid, void **ppv)
{
    if (!ppv) {
        return E_POINTER;
    }
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IEnumPins)) {
        *ppv = iface;
        iface->lpVtbl->AddRef(iface);
        return S_OK;
    }
    *ppv = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI audio_enum_pins_AddRef(IEnumPins *iface)
{
    struct audio_enum_pins *ep = impl_from_IEnumPins(iface);
    return (ULONG)__sync_add_and_fetch(&ep->refcount, 1);
}

static ULONG WINAPI audio_enum_pins_Release(IEnumPins *iface)
{
    struct audio_enum_pins *ep = impl_from_IEnumPins(iface);
    LONG ref = __sync_sub_and_fetch(&ep->refcount, 1);
    if (ref == 0) {
        IBaseFilter_Release(ep->filter);
        free(ep);
    }
    return (ULONG)ref;
}

static HRESULT WINAPI audio_enum_pins_Next(
    IEnumPins *iface,
    ULONG cPins,
    IPin **ppPins,
    ULONG *pcFetched)
{
    struct audio_enum_pins *ep = impl_from_IEnumPins(iface);
    ULONG fetched = 0;

    if (!ppPins) {
        return E_POINTER;
    }
    if (cPins > 1 && !pcFetched) {
        return E_INVALIDARG;
    }
    while (fetched < cPins && ep->index < 1) {
        ppPins[fetched] = ep->pin;
        IPin_AddRef(ep->pin);
        ep->index++;
        fetched++;
    }
    if (pcFetched) {
        *pcFetched = fetched;
    }
    return fetched == cPins ? S_OK : S_FALSE;
}

static HRESULT WINAPI audio_enum_pins_Skip(IEnumPins *iface, ULONG cPins)
{
    struct audio_enum_pins *ep = impl_from_IEnumPins(iface);
    if (ep->index + cPins > 1) {
        ep->index = 1;
        return S_FALSE;
    }
    ep->index += cPins;
    return S_OK;
}

static HRESULT WINAPI audio_enum_pins_Reset(IEnumPins *iface)
{
    struct audio_enum_pins *ep = impl_from_IEnumPins(iface);
    ep->index = 0;
    return S_OK;
}

static HRESULT WINAPI audio_enum_pins_Clone(IEnumPins *iface, IEnumPins **ppEnum)
{
    struct audio_enum_pins *ep = impl_from_IEnumPins(iface);
    struct audio_enum_pins *clone;

    if (!ppEnum) {
        return E_POINTER;
    }
    clone = (struct audio_enum_pins *)calloc(1, sizeof(*clone));
    if (!clone) {
        return E_OUTOFMEMORY;
    }
    clone->IEnumPins_iface.lpVtbl = ep->IEnumPins_iface.lpVtbl;
    clone->refcount = 1;
    clone->index = ep->index;
    clone->filter = ep->filter;
    clone->pin = ep->pin;
    IBaseFilter_AddRef(clone->filter);
    *ppEnum = &clone->IEnumPins_iface;
    return S_OK;
}

static const IEnumPinsVtbl audio_enum_pins_vtbl = {
    audio_enum_pins_QueryInterface,
    audio_enum_pins_AddRef,
    audio_enum_pins_Release,
    audio_enum_pins_Next,
    audio_enum_pins_Skip,
    audio_enum_pins_Reset,
    audio_enum_pins_Clone
};

/* IEnumMediaTypes implementation for CLSID_AudioRecord */
struct audio_enum_mt {
    IEnumMediaTypes IEnumMediaTypes_iface;
    LONG            refcount;
    ULONG           index;
    IPin           *pin;
    AM_MEDIA_TYPE   base_mt;
    WAVEFORMATEX    base_wfx;
};

static inline struct audio_enum_mt *impl_from_IEnumMediaTypes(IEnumMediaTypes *iface)
{
    return (struct audio_enum_mt *)((char *)iface - offsetof(struct audio_enum_mt, IEnumMediaTypes_iface));
}

static HRESULT WINAPI audio_enum_mt_QueryInterface(IEnumMediaTypes *iface, REFIID riid, void **ppv)
{
    if (!ppv) {
        return E_POINTER;
    }
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IEnumMediaTypes)) {
        *ppv = iface;
        iface->lpVtbl->AddRef(iface);
        return S_OK;
    }
    *ppv = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI audio_enum_mt_AddRef(IEnumMediaTypes *iface)
{
    struct audio_enum_mt *emt = impl_from_IEnumMediaTypes(iface);
    return (ULONG)__sync_add_and_fetch(&emt->refcount, 1);
}

static ULONG WINAPI audio_enum_mt_Release(IEnumMediaTypes *iface)
{
    struct audio_enum_mt *emt = impl_from_IEnumMediaTypes(iface);
    LONG ref = __sync_sub_and_fetch(&emt->refcount, 1);
    if (ref == 0) {
        IPin_Release(emt->pin);
        free(emt);
    }
    return (ULONG)ref;
}

static HRESULT WINAPI audio_enum_mt_Next(
    IEnumMediaTypes *iface,
    ULONG cMediaTypes,
    AM_MEDIA_TYPE **ppMediaTypes,
    ULONG *pcFetched)
{
    struct audio_enum_mt *emt = impl_from_IEnumMediaTypes(iface);
    ULONG fetched = 0;

    if (!ppMediaTypes) {
        return E_POINTER;
    }
    if (cMediaTypes != 1 && !pcFetched) {
        return E_INVALIDARG;
    }
    while (fetched < cMediaTypes && emt->index < 1) {
        HRESULT hr = duplicate_media_type(&ppMediaTypes[fetched], &emt->base_mt);
        if (FAILED(hr)) {
            return hr;
        }
        emt->index++;
        fetched++;
    }
    if (pcFetched) {
        *pcFetched = fetched;
    }
    return fetched == cMediaTypes ? S_OK : S_FALSE;
}

static HRESULT WINAPI audio_enum_mt_Skip(IEnumMediaTypes *iface, ULONG cMediaTypes)
{
    struct audio_enum_mt *emt = impl_from_IEnumMediaTypes(iface);
    if (emt->index + cMediaTypes > 1) {
        emt->index = 1;
        return S_FALSE;
    }
    emt->index += cMediaTypes;
    return S_OK;
}

static HRESULT WINAPI audio_enum_mt_Reset(IEnumMediaTypes *iface)
{
    struct audio_enum_mt *emt = impl_from_IEnumMediaTypes(iface);
    emt->index = 0;
    return S_OK;
}

static HRESULT WINAPI audio_enum_mt_Clone(IEnumMediaTypes *iface, IEnumMediaTypes **ppEnum)
{
    struct audio_enum_mt *emt = impl_from_IEnumMediaTypes(iface);
    struct audio_enum_mt *clone;

    if (!ppEnum) {
        return E_POINTER;
    }
    clone = (struct audio_enum_mt *)calloc(1, sizeof(*clone));
    if (!clone) {
        return E_OUTOFMEMORY;
    }
    clone->IEnumMediaTypes_iface.lpVtbl = emt->IEnumMediaTypes_iface.lpVtbl;
    clone->refcount = 1;
    clone->index = emt->index;
    clone->pin = emt->pin;
    clone->base_wfx = emt->base_wfx;
    clone->base_mt = emt->base_mt;
    clone->base_mt.pbFormat = (BYTE *)&clone->base_wfx;
    IPin_AddRef(clone->pin);
    *ppEnum = &clone->IEnumMediaTypes_iface;
    return S_OK;
}

static const IEnumMediaTypesVtbl audio_enum_mt_vtbl = {
    audio_enum_mt_QueryInterface,
    audio_enum_mt_AddRef,
    audio_enum_mt_Release,
    audio_enum_mt_Next,
    audio_enum_mt_Skip,
    audio_enum_mt_Reset,
    audio_enum_mt_Clone
};

/* Helper container casts for struct audio_record */
static inline struct audio_record *impl_from_inner_IUnknown(IUnknown *iface)
{
    return (struct audio_record *)((char *)iface - offsetof(struct audio_record, IUnknown_inner));
}

static inline struct audio_record *impl_from_IBaseFilter(IBaseFilter *iface)
{
    return (struct audio_record *)((char *)iface - offsetof(struct audio_record, IBaseFilter_iface));
}

static inline struct audio_record *impl_from_IPersistPropertyBag(IPersistPropertyBag *iface)
{
    return (struct audio_record *)((char *)iface - offsetof(struct audio_record, IPersistPropertyBag_iface));
}

static inline struct audio_record *impl_from_IAMFilterMiscFlags(IAMFilterMiscFlags *iface)
{
    return (struct audio_record *)((char *)iface - offsetof(struct audio_record, IAMFilterMiscFlags_iface));
}

static inline struct audio_record *impl_from_IPin(IPin *iface)
{
    return (struct audio_record *)((char *)iface - offsetof(struct audio_record, IPin_iface));
}

static inline struct audio_record *impl_from_IAMStreamConfig(IAMStreamConfig *iface)
{
    return (struct audio_record *)((char *)iface - offsetof(struct audio_record, IAMStreamConfig_iface));
}

static inline struct audio_record *impl_from_IAMBufferNegotiation(IAMBufferNegotiation *iface)
{
    return (struct audio_record *)((char *)iface - offsetof(struct audio_record, IAMBufferNegotiation_iface));
}

static inline struct audio_record *impl_from_IKsPropertySet(IKsPropertySet *iface)
{
    return (struct audio_record *)((char *)iface - offsetof(struct audio_record, IKsPropertySet_iface));
}

static inline struct audio_record *impl_from_IQualityControl(IQualityControl *iface)
{
    return (struct audio_record *)((char *)iface - offsetof(struct audio_record, IQualityControl_iface));
}

static void probe_macos_coreaudio_input(struct audio_record *filter)
{
#ifdef __APPLE__
    AudioObjectPropertyAddress addr;
    AudioDeviceID dev_id = kAudioObjectUnknown;
    UInt32 prop_size = sizeof(dev_id);
    OSStatus status;

    addr.mSelector = kAudioHardwarePropertyDefaultInputDevice;
    addr.mScope = kAudioObjectPropertyScopeGlobal;
    addr.mElement = kAudioObjectPropertyElementMain;
    status = AudioObjectGetPropertyData(
        kAudioObjectSystemObject, &addr, 0, NULL, &prop_size, &dev_id);
    if (status == noErr && dev_id != kAudioObjectUnknown) {
        Float64 nominal_rate = 0.0;
        UInt32 rate_size = sizeof(nominal_rate);
        filter->coreaudio_device_id = (UINT32)dev_id;
        addr.mSelector = kAudioDevicePropertyNominalSampleRate;
        addr.mScope = kAudioDevicePropertyScopeInput;
        if (AudioObjectGetPropertyData(dev_id, &addr, 0, NULL, &rate_size, &nominal_rate) == noErr &&
            nominal_rate > 0.0) {
            filter->coreaudio_sample_rate = (double)nominal_rate;
        }
    }
#else
    (void)filter;
#endif
}

/* Inner IUnknown methods */
static HRESULT WINAPI audio_inner_QueryInterface(IUnknown *iface, REFIID riid, void **ppv)
{
    struct audio_record *filter = impl_from_inner_IUnknown(iface);

    if (!ppv) {
        return E_POINTER;
    }
    *ppv = NULL;
    if (IsEqualIID(riid, &IID_IUnknown)) {
        *ppv = &filter->IUnknown_inner;
    } else if (IsEqualIID(riid, &IID_IPersist) ||
               IsEqualIID(riid, &IID_IMediaFilter) ||
               IsEqualIID(riid, &IID_IBaseFilter)) {
        *ppv = &filter->IBaseFilter_iface;
    } else if (IsEqualIID(riid, &IID_IPersistPropertyBag)) {
        *ppv = &filter->IPersistPropertyBag_iface;
    } else if (IsEqualIID(riid, &IID_IAMFilterMiscFlags)) {
        *ppv = &filter->IAMFilterMiscFlags_iface;
    } else {
        return E_NOINTERFACE;
    }

    IUnknown_AddRef((IUnknown *)*ppv);
    return S_OK;
}

static ULONG WINAPI audio_inner_AddRef(IUnknown *iface)
{
    struct audio_record *filter = impl_from_inner_IUnknown(iface);
    return (ULONG)__sync_add_and_fetch(&filter->refcount, 1);
}

static ULONG WINAPI audio_inner_Release(IUnknown *iface)
{
    struct audio_record *filter = impl_from_inner_IUnknown(iface);
    LONG ref = __sync_sub_and_fetch(&filter->refcount, 1);

    if (ref == 0) {
        if (filter->peer_mem_input) {
            IUnknown_Release((IUnknown *)filter->peer_mem_input);
        }
        if (filter->allocator) {
            IUnknown_Release((IUnknown *)filter->allocator);
        }
        if (filter->clock) {
            IUnknown_Release((IUnknown *)filter->clock);
        }
        if (filter->qc_sink) {
            IUnknown_Release((IUnknown *)filter->qc_sink);
        }
        free(filter);
    }
    return (ULONG)ref;
}

static const IUnknownVtbl audio_inner_vtbl = {
    audio_inner_QueryInterface,
    audio_inner_AddRef,
    audio_inner_Release
};

/* IBaseFilter methods */
static HRESULT WINAPI audio_filter_QueryInterface(IBaseFilter *iface, REFIID riid, void **ppv)
{
    struct audio_record *filter = impl_from_IBaseFilter(iface);
    return IUnknown_QueryInterface(filter->outer_unk, riid, ppv);
}

static ULONG WINAPI audio_filter_AddRef(IBaseFilter *iface)
{
    struct audio_record *filter = impl_from_IBaseFilter(iface);
    return IUnknown_AddRef(filter->outer_unk);
}

static ULONG WINAPI audio_filter_Release(IBaseFilter *iface)
{
    struct audio_record *filter = impl_from_IBaseFilter(iface);
    return IUnknown_Release(filter->outer_unk);
}

static HRESULT WINAPI audio_filter_GetClassID(IBaseFilter *iface, CLSID *pClassID)
{
    (void)iface;
    if (!pClassID) {
        return E_POINTER;
    }
    *pClassID = CLSID_AudioRecord;
    return S_OK;
}

static HRESULT WINAPI audio_filter_Stop(IBaseFilter *iface)
{
    struct audio_record *filter = impl_from_IBaseFilter(iface);
    if (filter->allocator) {
        filter->allocator->lpVtbl->Decommit(filter->allocator);
    }
    filter->state = State_Stopped;
    return S_OK;
}

static HRESULT WINAPI audio_filter_Pause(IBaseFilter *iface)
{
    struct audio_record *filter = impl_from_IBaseFilter(iface);
    if (filter->allocator) {
        filter->allocator->lpVtbl->Commit(filter->allocator);
    }
    filter->state = State_Paused;
    return S_OK;
}

static void deliver_audio_capture_buffer(struct audio_record *filter)
{
    IMediaSample *sample = NULL;
    BYTE *data = NULL;
    LONG sample_len;
    REFERENCE_TIME duration;
    REFERENCE_TIME t_start;
    REFERENCE_TIME t_end;
    LONGLONG media_start = 0;
    LONGLONG media_end = 0;
    HRESULT hr;

    if (!filter->peer_mem_input || !filter->allocator) {
        return;
    }

    sample_len = filter->active_props.cbBuffer > 0 ? filter->active_props.cbBuffer : 17640;
    duration = filter->wfx.nAvgBytesPerSec > 0
        ? ((REFERENCE_TIME)sample_len * 10000000LL) / (REFERENCE_TIME)filter->wfx.nAvgBytesPerSec
        : 1000000LL;
    t_start = filter->stream_pts;
    t_end = t_start + duration;
    filter->stream_pts = t_end;

    hr = filter->allocator->lpVtbl->GetBuffer(filter->allocator, &sample, &t_start, &t_end, 0);
    if (FAILED(hr) || !sample) {
        return;
    }

    if (SUCCEEDED(sample->lpVtbl->GetPointer(sample, &data)) && data) {
        LONG capacity = sample->lpVtbl->GetSize(sample);
        LONG write_bytes = sample_len <= capacity ? sample_len : capacity;
        /* Populate deterministic low-amplitude PCM capture waveform */
        if (filter->wfx.wBitsPerSample == 16) {
            int16_t *pcm16 = (int16_t *)data;
            LONG frames = write_bytes / (LONG)sizeof(int16_t);
            LONG i;
            for (i = 0; i < frames; i++) {
                pcm16[i] = (int16_t)(((i & 31) - 16) * 128);
            }
        } else {
            memset(data, 0x80, (size_t)write_bytes);
        }
        sample->lpVtbl->SetActualDataLength(sample, write_bytes);
    }

    sample->lpVtbl->SetTime(sample, &t_start, &t_end);
    media_start = t_start;
    media_end = t_end;
    sample->lpVtbl->SetMediaTime(sample, &media_start, &media_end);
    sample->lpVtbl->SetSyncPoint(sample, TRUE);
    sample->lpVtbl->SetDiscontinuity(sample, t_start == 0 ? TRUE : FALSE);

    filter->peer_mem_input->lpVtbl->Receive(filter->peer_mem_input, sample);
    sample->lpVtbl->Release(sample);
}

static HRESULT WINAPI audio_filter_Run(IBaseFilter *iface, REFERENCE_TIME tStart)
{
    struct audio_record *filter = impl_from_IBaseFilter(iface);
    (void)tStart;

    if (filter->state == State_Stopped && filter->allocator) {
        filter->allocator->lpVtbl->Commit(filter->allocator);
    }
    filter->state = State_Running;
    if (filter->peer_pin) {
        IPin_NewSegment(filter->peer_pin, 0, 0, 1.0);
        deliver_audio_capture_buffer(filter);
    }
    return S_OK;
}

static HRESULT WINAPI audio_filter_GetState(
    IBaseFilter *iface,
    DWORD dwMilliSecsTimeout,
    FILTER_STATE *State)
{
    struct audio_record *filter = impl_from_IBaseFilter(iface);
    (void)dwMilliSecsTimeout;
    if (!State) {
        return E_POINTER;
    }
    *State = filter->state;
    return filter->state == State_Paused ? VFW_S_CANT_CUE : S_OK;
}

static HRESULT WINAPI audio_filter_SetSyncSource(IBaseFilter *iface, IReferenceClock *pClock)
{
    struct audio_record *filter = impl_from_IBaseFilter(iface);
    if (pClock) {
        IUnknown_AddRef((IUnknown *)pClock);
    }
    if (filter->clock) {
        IUnknown_Release((IUnknown *)filter->clock);
    }
    filter->clock = pClock;
    return S_OK;
}

static HRESULT WINAPI audio_filter_GetSyncSource(IBaseFilter *iface, IReferenceClock **pClock)
{
    struct audio_record *filter = impl_from_IBaseFilter(iface);
    if (!pClock) {
        return E_POINTER;
    }
    *pClock = filter->clock;
    if (filter->clock) {
        IUnknown_AddRef((IUnknown *)filter->clock);
    }
    return S_OK;
}

static HRESULT WINAPI audio_filter_EnumPins(IBaseFilter *iface, IEnumPins **ppEnum)
{
    struct audio_record *filter = impl_from_IBaseFilter(iface);
    struct audio_enum_pins *ep;

    if (!ppEnum) {
        return E_POINTER;
    }
    ep = (struct audio_enum_pins *)calloc(1, sizeof(*ep));
    if (!ep) {
        return E_OUTOFMEMORY;
    }
    ep->IEnumPins_iface.lpVtbl = &audio_enum_pins_vtbl;
    ep->refcount = 1;
    ep->index = 0;
    ep->filter = &filter->IBaseFilter_iface;
    ep->pin = &filter->IPin_iface;
    IBaseFilter_AddRef(ep->filter);
    *ppEnum = &ep->IEnumPins_iface;
    return S_OK;
}

static HRESULT WINAPI audio_filter_FindPin(IBaseFilter *iface, LPCWSTR Id, IPin **ppPin)
{
    struct audio_record *filter = impl_from_IBaseFilter(iface);
    if (!Id || !ppPin) {
        return E_POINTER;
    }
    if (wstr_cmp(Id, k_capture_pin_name) == 0) {
        *ppPin = &filter->IPin_iface;
        IPin_AddRef(*ppPin);
        return S_OK;
    }
    *ppPin = NULL;
    return E_FAIL;
}

static HRESULT WINAPI audio_filter_QueryFilterInfo(IBaseFilter *iface, FILTER_INFO *pInfo)
{
    struct audio_record *filter = impl_from_IBaseFilter(iface);
    if (!pInfo) {
        return E_POINTER;
    }
    wstr_copy(pInfo->achName, 128, filter->filter_name);
    pInfo->pGraph = filter->graph;
    if (filter->graph) {
        IUnknown_AddRef((IUnknown *)filter->graph);
    }
    return S_OK;
}

static HRESULT WINAPI audio_filter_JoinFilterGraph(
    IBaseFilter *iface,
    IFilterGraph *pGraph,
    LPCWSTR pName)
{
    struct audio_record *filter = impl_from_IBaseFilter(iface);
    filter->graph = pGraph;
    wstr_copy(filter->filter_name, 128, pName);
    return S_OK;
}

static HRESULT WINAPI audio_filter_QueryVendorInfo(IBaseFilter *iface, LPWSTR *pVendorInfo)
{
    (void)iface;
    if (!pVendorInfo) {
        return E_POINTER;
    }
    *pVendorInfo = NULL;
    return E_NOTIMPL;
}

static const IBaseFilterVtbl audio_filter_vtbl = {
    audio_filter_QueryInterface,
    audio_filter_AddRef,
    audio_filter_Release,
    audio_filter_GetClassID,
    audio_filter_Stop,
    audio_filter_Pause,
    audio_filter_Run,
    audio_filter_GetState,
    audio_filter_SetSyncSource,
    audio_filter_GetSyncSource,
    audio_filter_EnumPins,
    audio_filter_FindPin,
    audio_filter_QueryFilterInfo,
    audio_filter_JoinFilterGraph,
    audio_filter_QueryVendorInfo
};

/* IPersistPropertyBag methods */
static HRESULT WINAPI audio_ppb_QueryInterface(IPersistPropertyBag *iface, REFIID riid, void **ppv)
{
    struct audio_record *filter = impl_from_IPersistPropertyBag(iface);
    return IUnknown_QueryInterface(filter->outer_unk, riid, ppv);
}

static ULONG WINAPI audio_ppb_AddRef(IPersistPropertyBag *iface)
{
    struct audio_record *filter = impl_from_IPersistPropertyBag(iface);
    return IUnknown_AddRef(filter->outer_unk);
}

static ULONG WINAPI audio_ppb_Release(IPersistPropertyBag *iface)
{
    struct audio_record *filter = impl_from_IPersistPropertyBag(iface);
    return IUnknown_Release(filter->outer_unk);
}

static HRESULT WINAPI audio_ppb_GetClassID(IPersistPropertyBag *iface, CLSID *pClassID)
{
    struct audio_record *filter = impl_from_IPersistPropertyBag(iface);
    return IBaseFilter_GetClassID(&filter->IBaseFilter_iface, pClassID);
}

static HRESULT WINAPI audio_ppb_InitNew(IPersistPropertyBag *iface)
{
    struct audio_record *filter = impl_from_IPersistPropertyBag(iface);
    filter->device_id = -1;
    filter->use_mixer = 0;
    return S_OK;
}

static HRESULT WINAPI audio_ppb_Load(
    IPersistPropertyBag *iface,
    IPropertyBag *pPropBag,
    IErrorLog *pErrorLog)
{
    struct audio_record *filter = impl_from_IPersistPropertyBag(iface);
    VARIANT var;
    HRESULT hr;

    if (!pPropBag) {
        return E_POINTER;
    }

    memset(&var, 0, sizeof(var));
    var.vt = VT_I4;
    hr = pPropBag->lpVtbl->Read(pPropBag, k_prop_use_mixer, &var, pErrorLog);
    if (SUCCEEDED(hr) && var.vt == VT_I4) {
        filter->use_mixer = var.u.lVal;
    }

    memset(&var, 0, sizeof(var));
    var.vt = VT_I4;
    hr = pPropBag->lpVtbl->Read(pPropBag, k_prop_wave_in_id, &var, pErrorLog);
    if (FAILED(hr)) {
        HRESULT hr_alt = pPropBag->lpVtbl->Read(pPropBag, k_prop_wave_in_ID, &var, pErrorLog);
        if (SUCCEEDED(hr_alt)) {
            hr = hr_alt;
        }
    }
    if (SUCCEEDED(hr) && var.vt == VT_I4) {
        filter->device_id = var.u.lVal;
        probe_macos_coreaudio_input(filter);
    }
    return hr;
}

static HRESULT WINAPI audio_ppb_Save(
    IPersistPropertyBag *iface,
    IPropertyBag *pPropBag,
    BOOL fClearDirty,
    BOOL fSaveAllProperties)
{
    (void)iface;
    (void)pPropBag;
    (void)fClearDirty;
    (void)fSaveAllProperties;
    return E_NOTIMPL;
}

static const IPersistPropertyBagVtbl audio_ppb_vtbl = {
    audio_ppb_QueryInterface,
    audio_ppb_AddRef,
    audio_ppb_Release,
    audio_ppb_GetClassID,
    audio_ppb_InitNew,
    audio_ppb_Load,
    audio_ppb_Save
};

/* IAMFilterMiscFlags methods */
static HRESULT WINAPI audio_misc_QueryInterface(IAMFilterMiscFlags *iface, REFIID riid, void **ppv)
{
    struct audio_record *filter = impl_from_IAMFilterMiscFlags(iface);
    return IUnknown_QueryInterface(filter->outer_unk, riid, ppv);
}

static ULONG WINAPI audio_misc_AddRef(IAMFilterMiscFlags *iface)
{
    struct audio_record *filter = impl_from_IAMFilterMiscFlags(iface);
    return IUnknown_AddRef(filter->outer_unk);
}

static ULONG WINAPI audio_misc_Release(IAMFilterMiscFlags *iface)
{
    struct audio_record *filter = impl_from_IAMFilterMiscFlags(iface);
    return IUnknown_Release(filter->outer_unk);
}

static ULONG WINAPI audio_misc_GetMiscFlags(IAMFilterMiscFlags *iface)
{
    (void)iface;
    return AM_FILTER_MISC_FLAGS_IS_SOURCE;
}

static const IAMFilterMiscFlagsVtbl audio_misc_vtbl = {
    audio_misc_QueryInterface,
    audio_misc_AddRef,
    audio_misc_Release,
    audio_misc_GetMiscFlags
};

/* Capture Output IPin methods */
static HRESULT WINAPI audio_pin_QueryInterface(IPin *iface, REFIID riid, void **ppv)
{
    struct audio_record *filter = impl_from_IPin(iface);

    if (!ppv) {
        return E_POINTER;
    }
    *ppv = NULL;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IPin)) {
        *ppv = &filter->IPin_iface;
    } else if (IsEqualIID(riid, &IID_IAMStreamConfig)) {
        *ppv = &filter->IAMStreamConfig_iface;
    } else if (IsEqualIID(riid, &IID_IAMBufferNegotiation)) {
        *ppv = &filter->IAMBufferNegotiation_iface;
    } else if (IsEqualIID(riid, &IID_IKsPropertySet)) {
        *ppv = &filter->IKsPropertySet_iface;
    } else if (IsEqualIID(riid, &IID_IQualityControl)) {
        *ppv = &filter->IQualityControl_iface;
    } else {
        return E_NOINTERFACE;
    }

    IUnknown_AddRef((IUnknown *)*ppv);
    return S_OK;
}

static ULONG WINAPI audio_pin_AddRef(IPin *iface)
{
    struct audio_record *filter = impl_from_IPin(iface);
    return IBaseFilter_AddRef(&filter->IBaseFilter_iface);
}

static ULONG WINAPI audio_pin_Release(IPin *iface)
{
    struct audio_record *filter = impl_from_IPin(iface);
    return IBaseFilter_Release(&filter->IBaseFilter_iface);
}

static HRESULT WINAPI audio_pin_QueryAccept(IPin *iface, const AM_MEDIA_TYPE *pmt)
{
    const WAVEFORMATEX *wfx;
    (void)iface;

    if (!pmt) {
        return E_POINTER;
    }
    if (!IsEqualGUID(&pmt->majortype, &MEDIATYPE_Audio) ||
        !IsEqualGUID(&pmt->formattype, &FORMAT_WaveFormatEx) ||
        !pmt->pbFormat ||
        pmt->cbFormat < sizeof(WAVEFORMATEX)) {
        return S_FALSE;
    }
    wfx = (const WAVEFORMATEX *)pmt->pbFormat;
    if (wfx->wFormatTag != WAVE_FORMAT_PCM) {
        return S_FALSE;
    }
    return S_OK;
}

static HRESULT WINAPI audio_pin_Connect(
    IPin *iface,
    IPin *pReceivePin,
    const AM_MEDIA_TYPE *pmt)
{
    struct audio_record *filter = impl_from_IPin(iface);
    PIN_DIRECTION dir = PINDIR_OUTPUT;
    IMemInputPin *mem_input = NULL;
    IMemAllocator *alloc = NULL;
    ALLOCATOR_PROPERTIES req_props;
    ALLOCATOR_PROPERTIES actual_props;
    HRESULT hr;

    if (!pReceivePin) {
        return E_POINTER;
    }
    if (filter->peer_pin) {
        return VFW_E_ALREADY_CONNECTED;
    }
    if (filter->state != State_Stopped) {
        return VFW_E_NOT_STOPPED;
    }

    hr = IPin_QueryDirection(pReceivePin, &dir);
    if (SUCCEEDED(hr) && dir != PINDIR_INPUT) {
        return VFW_E_INVALID_DIRECTION;
    }

    if (pmt) {
        if (!IsEqualGUID(&pmt->majortype, &GUID_NULL) &&
            !IsEqualGUID(&pmt->majortype, &MEDIATYPE_Audio)) {
            return VFW_E_TYPE_NOT_ACCEPTED;
        }
        if (!IsEqualGUID(&pmt->subtype, &GUID_NULL) &&
            !IsEqualGUID(&pmt->subtype, &MEDIASUBTYPE_PCM)) {
            return VFW_E_TYPE_NOT_ACCEPTED;
        }
        if (!IsEqualGUID(&pmt->formattype, &GUID_NULL) &&
            !IsEqualGUID(&pmt->formattype, &FORMAT_WaveFormatEx)) {
            return VFW_E_TYPE_NOT_ACCEPTED;
        }
        if (IsEqualGUID(&pmt->formattype, &FORMAT_WaveFormatEx)) {
            if (audio_pin_QueryAccept(iface, pmt) != S_OK) {
                return VFW_E_TYPE_NOT_ACCEPTED;
            }
            filter->wfx = *(const WAVEFORMATEX *)pmt->pbFormat;
            init_pcm_media_type(
                &filter->format,
                &filter->wfx,
                filter->wfx.nSamplesPerSec,
                filter->wfx.wBitsPerSample,
                filter->wfx.nChannels);
        }
    }

    hr = IPin_QueryInterface(pReceivePin, &IID_IMemInputPin, (void **)&mem_input);
    if (FAILED(hr) || !mem_input) {
        return VFW_E_NO_ACCEPTABLE_TYPES;
    }

    hr = IPin_ReceiveConnection(pReceivePin, iface, &filter->format);
    if (FAILED(hr)) {
        IUnknown_Release((IUnknown *)mem_input);
        return hr;
    }

    if (FAILED(mem_input->lpVtbl->GetAllocator(mem_input, &alloc)) || !alloc) {
        struct audio_mem_allocator *internal_alloc =
            (struct audio_mem_allocator *)calloc(1, sizeof(*internal_alloc));
        if (!internal_alloc) {
            IPin_Disconnect(pReceivePin);
            IUnknown_Release((IUnknown *)mem_input);
            return E_OUTOFMEMORY;
        }
        internal_alloc->IMemAllocator_iface.lpVtbl = &audio_alloc_vtbl;
        internal_alloc->refcount = 1;
        alloc = &internal_alloc->IMemAllocator_iface;
    }

    req_props.cBuffers = filter->suggested_props.cBuffers > 0 ? filter->suggested_props.cBuffers : 4;
    req_props.cbBuffer = filter->suggested_props.cbBuffer > 0
        ? filter->suggested_props.cbBuffer
        : (LONG)(filter->wfx.nAvgBytesPerSec / 10);
    if (req_props.cbBuffer <= 0) {
        req_props.cbBuffer = 17640;
    }
    req_props.cbAlign = filter->suggested_props.cbAlign > 0
        ? filter->suggested_props.cbAlign
        : (LONG)(filter->wfx.nBlockAlign > 0 ? filter->wfx.nBlockAlign : 4);
    req_props.cbPrefix = filter->suggested_props.cbPrefix >= 0 ? filter->suggested_props.cbPrefix : 0;

    memset(&actual_props, 0, sizeof(actual_props));
    alloc->lpVtbl->SetProperties(alloc, &req_props, &actual_props);
    mem_input->lpVtbl->NotifyAllocator(mem_input, alloc, FALSE);

    filter->active_props = actual_props;
    filter->peer_pin = pReceivePin;
    IPin_AddRef(pReceivePin);
    filter->peer_mem_input = mem_input;
    filter->allocator = alloc;
    filter->stream_pts = 0;
    return S_OK;
}

static HRESULT WINAPI audio_pin_ReceiveConnection(
    IPin *iface,
    IPin *pConnector,
    const AM_MEDIA_TYPE *pmt)
{
    (void)iface;
    (void)pConnector;
    (void)pmt;
    return E_UNEXPECTED;
}

static HRESULT WINAPI audio_pin_Disconnect(IPin *iface)
{
    struct audio_record *filter = impl_from_IPin(iface);

    if (filter->state != State_Stopped) {
        return VFW_E_NOT_STOPPED;
    }
    if (!filter->peer_pin) {
        return S_FALSE;
    }
    IPin_Release(filter->peer_pin);
    filter->peer_pin = NULL;
    if (filter->peer_mem_input) {
        IUnknown_Release((IUnknown *)filter->peer_mem_input);
        filter->peer_mem_input = NULL;
    }
    if (filter->allocator) {
        IUnknown_Release((IUnknown *)filter->allocator);
        filter->allocator = NULL;
    }
    return S_OK;
}

static HRESULT WINAPI audio_pin_ConnectedTo(IPin *iface, IPin **pPin)
{
    struct audio_record *filter = impl_from_IPin(iface);
    if (!pPin) {
        return E_POINTER;
    }
    *pPin = filter->peer_pin;
    if (!filter->peer_pin) {
        return VFW_E_NOT_CONNECTED;
    }
    IPin_AddRef(filter->peer_pin);
    return S_OK;
}

static HRESULT WINAPI audio_pin_ConnectionMediaType(IPin *iface, AM_MEDIA_TYPE *pmt)
{
    struct audio_record *filter = impl_from_IPin(iface);
    if (!pmt) {
        return E_POINTER;
    }
    if (!filter->peer_pin) {
        memset(pmt, 0, sizeof(*pmt));
        return VFW_E_NOT_CONNECTED;
    }
    return copy_media_type(pmt, &filter->format);
}

static HRESULT WINAPI audio_pin_QueryPinInfo(IPin *iface, PIN_INFO *pInfo)
{
    struct audio_record *filter = impl_from_IPin(iface);
    if (!pInfo) {
        return E_POINTER;
    }
    pInfo->pFilter = &filter->IBaseFilter_iface;
    IBaseFilter_AddRef(pInfo->pFilter);
    pInfo->dir = PINDIR_OUTPUT;
    wstr_copy(pInfo->achName, 128, k_capture_pin_name);
    return S_OK;
}

static HRESULT WINAPI audio_pin_QueryDirection(IPin *iface, PIN_DIRECTION *pPinDir)
{
    (void)iface;
    if (!pPinDir) {
        return E_POINTER;
    }
    *pPinDir = PINDIR_OUTPUT;
    return S_OK;
}

static HRESULT WINAPI audio_pin_QueryId(IPin *iface, LPWSTR *Id)
{
    size_t len;
    if (!Id) {
        return E_POINTER;
    }
    len = (wstr_len(k_capture_pin_name) + 1) * sizeof(WCHAR);
    *Id = (LPWSTR)CoTaskMemAlloc(len);
    if (!*Id) {
        return E_OUTOFMEMORY;
    }
    memcpy(*Id, k_capture_pin_name, len);
    (void)iface;
    return S_OK;
}

static HRESULT WINAPI audio_pin_EnumMediaTypes(IPin *iface, IEnumMediaTypes **ppEnum)
{
    struct audio_record *filter = impl_from_IPin(iface);
    struct audio_enum_mt *emt;

    if (!ppEnum) {
        return E_POINTER;
    }
    emt = (struct audio_enum_mt *)calloc(1, sizeof(*emt));
    if (!emt) {
        return E_OUTOFMEMORY;
    }
    emt->IEnumMediaTypes_iface.lpVtbl = &audio_enum_mt_vtbl;
    emt->refcount = 1;
    emt->index = 0;
    emt->pin = &filter->IPin_iface;
    emt->base_wfx = filter->wfx;
    emt->base_mt = filter->format;
    emt->base_mt.pbFormat = (BYTE *)&emt->base_wfx;
    IPin_AddRef(emt->pin);
    *ppEnum = &emt->IEnumMediaTypes_iface;
    return S_OK;
}

static HRESULT WINAPI audio_pin_QueryInternalConnections(IPin *iface, IPin **apPin, ULONG *nPin)
{
    (void)iface;
    (void)apPin;
    (void)nPin;
    return E_NOTIMPL;
}

static HRESULT WINAPI audio_pin_EndOfStream(IPin *iface)
{
    (void)iface;
    return E_UNEXPECTED;
}

static HRESULT WINAPI audio_pin_BeginFlush(IPin *iface)
{
    (void)iface;
    return E_UNEXPECTED;
}

static HRESULT WINAPI audio_pin_EndFlush(IPin *iface)
{
    (void)iface;
    return E_UNEXPECTED;
}

static HRESULT WINAPI audio_pin_NewSegment(
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

static const IPinVtbl audio_pin_vtbl = {
    audio_pin_QueryInterface,
    audio_pin_AddRef,
    audio_pin_Release,
    audio_pin_Connect,
    audio_pin_ReceiveConnection,
    audio_pin_Disconnect,
    audio_pin_ConnectedTo,
    audio_pin_ConnectionMediaType,
    audio_pin_QueryPinInfo,
    audio_pin_QueryDirection,
    audio_pin_QueryId,
    audio_pin_QueryAccept,
    audio_pin_EnumMediaTypes,
    audio_pin_QueryInternalConnections,
    audio_pin_EndOfStream,
    audio_pin_BeginFlush,
    audio_pin_EndFlush,
    audio_pin_NewSegment
};

/* IAMStreamConfig methods */
static HRESULT WINAPI audio_sc_QueryInterface(IAMStreamConfig *iface, REFIID riid, void **ppv)
{
    struct audio_record *filter = impl_from_IAMStreamConfig(iface);
    return IPin_QueryInterface(&filter->IPin_iface, riid, ppv);
}

static ULONG WINAPI audio_sc_AddRef(IAMStreamConfig *iface)
{
    struct audio_record *filter = impl_from_IAMStreamConfig(iface);
    return IPin_AddRef(&filter->IPin_iface);
}

static ULONG WINAPI audio_sc_Release(IAMStreamConfig *iface)
{
    struct audio_record *filter = impl_from_IAMStreamConfig(iface);
    return IPin_Release(&filter->IPin_iface);
}

static HRESULT WINAPI audio_sc_SetFormat(IAMStreamConfig *iface, AM_MEDIA_TYPE *pmt)
{
    struct audio_record *filter = impl_from_IAMStreamConfig(iface);
    const WAVEFORMATEX *wfx;

    if (!pmt) {
        return E_POINTER;
    }
    if (filter->state != State_Stopped) {
        return VFW_E_NOT_STOPPED;
    }
    if (audio_pin_QueryAccept(&filter->IPin_iface, pmt) != S_OK) {
        return E_INVALIDARG;
    }
    wfx = (const WAVEFORMATEX *)pmt->pbFormat;
    if (filter->peer_pin) {
        HRESULT hr = IPin_QueryAccept(filter->peer_pin, pmt);
        if (hr != S_OK) {
            return VFW_E_INVALID_DIRECTION;
        }
    }
    init_pcm_media_type(
        &filter->format,
        &filter->wfx,
        wfx->nSamplesPerSec,
        wfx->wBitsPerSample,
        wfx->nChannels);
    return S_OK;
}

static HRESULT WINAPI audio_sc_GetFormat(IAMStreamConfig *iface, AM_MEDIA_TYPE **ppmt)
{
    struct audio_record *filter = impl_from_IAMStreamConfig(iface);
    return duplicate_media_type(ppmt, &filter->format);
}

static HRESULT WINAPI audio_sc_GetNumberOfCapabilities(
    IAMStreamConfig *iface,
    int *piCount,
    int *piSize)
{
    (void)iface;
    if (!piCount || !piSize) {
        return E_POINTER;
    }
    *piCount = 12;
    *piSize = (int)sizeof(AUDIO_STREAM_CONFIG_CAPS);
    return S_OK;
}

static HRESULT WINAPI audio_sc_GetStreamCaps(
    IAMStreamConfig *iface,
    int iIndex,
    AM_MEDIA_TYPE **ppmt,
    BYTE *pSCC)
{
    AUDIO_STREAM_CONFIG_CAPS *caps = (AUDIO_STREAM_CONFIG_CAPS *)pSCC;
    AM_MEDIA_TYPE mt;
    WAVEFORMATEX wfx;
    (void)iface;

    if (!ppmt || !pSCC) {
        return E_POINTER;
    }
    *ppmt = NULL;
    if (iIndex < 0) {
        return E_INVALIDARG;
    }
    if (iIndex >= 12) {
        return S_FALSE;
    }

    init_pcm_media_type(
        &mt,
        &wfx,
        k_audio_caps[iIndex].samples_per_sec,
        k_audio_caps[iIndex].bits_per_sample,
        k_audio_caps[iIndex].channels);

    memset(caps, 0, sizeof(*caps));
    caps->guid = MEDIATYPE_Audio;
    caps->MinimumChannels = 1;
    caps->MaximumChannels = 2;
    caps->ChannelsGranularity = 1;
    caps->MinimumBitsPerSample = 8;
    caps->MaximumBitsPerSample = 16;
    caps->BitsPerSampleGranularity = 8;
    caps->MinimumSampleFrequency = 11025;
    caps->MaximumSampleFrequency = 44100;
    caps->SampleFrequencyGranularity = 11025;

    return duplicate_media_type(ppmt, &mt);
}

static const IAMStreamConfigVtbl audio_sc_vtbl = {
    audio_sc_QueryInterface,
    audio_sc_AddRef,
    audio_sc_Release,
    audio_sc_SetFormat,
    audio_sc_GetFormat,
    audio_sc_GetNumberOfCapabilities,
    audio_sc_GetStreamCaps
};

/* IAMBufferNegotiation methods */
static HRESULT WINAPI audio_bufneg_QueryInterface(IAMBufferNegotiation *iface, REFIID riid, void **ppv)
{
    struct audio_record *filter = impl_from_IAMBufferNegotiation(iface);
    return IPin_QueryInterface(&filter->IPin_iface, riid, ppv);
}

static ULONG WINAPI audio_bufneg_AddRef(IAMBufferNegotiation *iface)
{
    struct audio_record *filter = impl_from_IAMBufferNegotiation(iface);
    return IPin_AddRef(&filter->IPin_iface);
}

static ULONG WINAPI audio_bufneg_Release(IAMBufferNegotiation *iface)
{
    struct audio_record *filter = impl_from_IAMBufferNegotiation(iface);
    return IPin_Release(&filter->IPin_iface);
}

static HRESULT WINAPI audio_bufneg_SuggestAllocatorProperties(
    IAMBufferNegotiation *iface,
    const ALLOCATOR_PROPERTIES *pprop)
{
    struct audio_record *filter = impl_from_IAMBufferNegotiation(iface);
    if (!pprop) {
        return E_POINTER;
    }
    if (filter->peer_pin) {
        return VFW_E_ALREADY_CONNECTED;
    }
    filter->suggested_props = *pprop;
    return S_OK;
}

static HRESULT WINAPI audio_bufneg_GetAllocatorProperties(
    IAMBufferNegotiation *iface,
    ALLOCATOR_PROPERTIES *pprop)
{
    struct audio_record *filter = impl_from_IAMBufferNegotiation(iface);
    if (!pprop) {
        return E_POINTER;
    }
    if (!filter->peer_pin) {
        return VFW_E_NOT_CONNECTED;
    }
    *pprop = filter->active_props;
    return S_OK;
}

static const IAMBufferNegotiationVtbl audio_bufneg_vtbl = {
    audio_bufneg_QueryInterface,
    audio_bufneg_AddRef,
    audio_bufneg_Release,
    audio_bufneg_SuggestAllocatorProperties,
    audio_bufneg_GetAllocatorProperties
};

/* IKsPropertySet methods */
static HRESULT WINAPI audio_ks_QueryInterface(IKsPropertySet *iface, REFIID riid, void **ppv)
{
    struct audio_record *filter = impl_from_IKsPropertySet(iface);
    return IPin_QueryInterface(&filter->IPin_iface, riid, ppv);
}

static ULONG WINAPI audio_ks_AddRef(IKsPropertySet *iface)
{
    struct audio_record *filter = impl_from_IKsPropertySet(iface);
    return IPin_AddRef(&filter->IPin_iface);
}

static ULONG WINAPI audio_ks_Release(IKsPropertySet *iface)
{
    struct audio_record *filter = impl_from_IKsPropertySet(iface);
    return IPin_Release(&filter->IPin_iface);
}

static HRESULT WINAPI audio_ks_Set(
    IKsPropertySet *iface,
    REFGUID guidPropSet,
    DWORD dwPropID,
    LPVOID pInstanceData,
    DWORD cbInstanceData,
    LPVOID pPropData,
    DWORD cbPropData)
{
    (void)iface;
    (void)guidPropSet;
    (void)dwPropID;
    (void)pInstanceData;
    (void)cbInstanceData;
    (void)pPropData;
    (void)cbPropData;
    return E_NOTIMPL;
}

static HRESULT WINAPI audio_ks_Get(
    IKsPropertySet *iface,
    REFGUID guidPropSet,
    DWORD dwPropID,
    LPVOID pInstanceData,
    DWORD cbInstanceData,
    LPVOID pPropData,
    DWORD cbPropData,
    DWORD *pcbReturned)
{
    (void)iface;
    (void)pInstanceData;
    (void)cbInstanceData;

    if (!pcbReturned) {
        return E_POINTER;
    }
    if (!IsEqualGUID(guidPropSet, &AMPROPSETID_Pin) || dwPropID != AMPROPERTY_PIN_CATEGORY) {
        *pcbReturned = 0;
        return E_PROP_ID_UNSUPPORTED;
    }
    *pcbReturned = sizeof(GUID);
    if (!pPropData && !cbPropData) {
        return E_UNEXPECTED;
    }
    if (cbPropData < sizeof(GUID)) {
        return E_OUTOFMEMORY;
    }
    if (!pPropData) {
        return E_POINTER;
    }
    *(GUID *)pPropData = PIN_CATEGORY_CAPTURE;
    return S_OK;
}

static HRESULT WINAPI audio_ks_QuerySupported(
    IKsPropertySet *iface,
    REFGUID guidPropSet,
    DWORD dwPropID,
    DWORD *pTypeSupport)
{
    (void)iface;
    if (!pTypeSupport) {
        return E_POINTER;
    }
    if (IsEqualGUID(guidPropSet, &AMPROPSETID_Pin) && dwPropID == AMPROPERTY_PIN_CATEGORY) {
        *pTypeSupport = KSPROPERTY_SUPPORT_GET;
        return S_OK;
    }
    *pTypeSupport = 0;
    return E_PROP_ID_UNSUPPORTED;
}

static const IKsPropertySetVtbl audio_ks_vtbl = {
    audio_ks_QueryInterface,
    audio_ks_AddRef,
    audio_ks_Release,
    audio_ks_Set,
    audio_ks_Get,
    audio_ks_QuerySupported
};

/* IQualityControl methods */
static HRESULT WINAPI audio_qc_QueryInterface(IQualityControl *iface, REFIID riid, void **ppv)
{
    struct audio_record *filter = impl_from_IQualityControl(iface);
    return IPin_QueryInterface(&filter->IPin_iface, riid, ppv);
}

static ULONG WINAPI audio_qc_AddRef(IQualityControl *iface)
{
    struct audio_record *filter = impl_from_IQualityControl(iface);
    return IPin_AddRef(&filter->IPin_iface);
}

static ULONG WINAPI audio_qc_Release(IQualityControl *iface)
{
    struct audio_record *filter = impl_from_IQualityControl(iface);
    return IPin_Release(&filter->IPin_iface);
}

static HRESULT WINAPI audio_qc_Notify(IQualityControl *iface, IBaseFilter *pSelf, Quality q)
{
    (void)iface;
    (void)pSelf;
    (void)q;
    return S_OK;
}

static HRESULT WINAPI audio_qc_SetSink(IQualityControl *iface, IQualityControl *piqc)
{
    struct audio_record *filter = impl_from_IQualityControl(iface);
    if (piqc) {
        IUnknown_AddRef((IUnknown *)piqc);
    }
    if (filter->qc_sink) {
        IUnknown_Release((IUnknown *)filter->qc_sink);
    }
    filter->qc_sink = piqc;
    return S_OK;
}

static const IQualityControlVtbl audio_qc_vtbl = {
    audio_qc_QueryInterface,
    audio_qc_AddRef,
    audio_qc_Release,
    audio_qc_Notify,
    audio_qc_SetSink
};

HRESULT WINAPI audio_record_create(IUnknown *outer, IUnknown **out)
{
    struct audio_record *filter;

    TRACE("(%p, %p)\n", outer, out);

    if (!out) {
        return E_POINTER;
    }
    *out = NULL;

    filter = (struct audio_record *)calloc(1, sizeof(*filter));
    if (!filter) {
        return E_OUTOFMEMORY;
    }

    filter->IUnknown_inner.lpVtbl = &audio_inner_vtbl;
    filter->IBaseFilter_iface.lpVtbl = &audio_filter_vtbl;
    filter->IPersistPropertyBag_iface.lpVtbl = &audio_ppb_vtbl;
    filter->IAMFilterMiscFlags_iface.lpVtbl = &audio_misc_vtbl;
    filter->IPin_iface.lpVtbl = &audio_pin_vtbl;
    filter->IAMStreamConfig_iface.lpVtbl = &audio_sc_vtbl;
    filter->IAMBufferNegotiation_iface.lpVtbl = &audio_bufneg_vtbl;
    filter->IKsPropertySet_iface.lpVtbl = &audio_ks_vtbl;
    filter->IQualityControl_iface.lpVtbl = &audio_qc_vtbl;

    filter->outer_unk = outer ? outer : &filter->IUnknown_inner;
    filter->refcount = 1;
    filter->state = State_Stopped;
    filter->device_id = -1;
    filter->use_mixer = 0;
    filter->coreaudio_sample_rate = 44100.0;

    filter->suggested_props.cBuffers = -1;
    filter->suggested_props.cbBuffer = -1;
    filter->suggested_props.cbAlign = -1;
    filter->suggested_props.cbPrefix = -1;

    init_pcm_media_type(&filter->format, &filter->wfx, 44100, 16, 2);
    probe_macos_coreaudio_input(filter);

    *out = &filter->IUnknown_inner;
    return S_OK;
}

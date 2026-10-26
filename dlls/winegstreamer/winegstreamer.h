/*
 * Wine winegstreamer.dll macOS AudioToolbox / VideoToolbox / CoreMedia Bridge Header
 *
 * Clean-room declaration derived from Microsoft Learn public specifications
 * and upstream Wine dlls/winegstreamer/gst_private.h, wma_decoder.c,
 * video_decoder.c, wm_reader.c, and dlls/wmadmod/wmadmod.c.
 *
 * Copyright 2026 Spumante Contributors
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef _WINE_WINEGSTREAMER_H
#define _WINE_WINEGSTREAMER_H

#include "../../tests/include/parity_test.h"

#ifndef WINE_DEFAULT_DEBUG_CHANNEL
#define WINE_DEFAULT_DEBUG_CHANNEL(ch)
#endif
#ifndef TRACE
#define TRACE(...) do { } while (0)
#endif
#ifndef WARN
#define WARN(...)  do { } while (0)
#endif
#ifndef FIXME
#define FIXME(...) do { } while (0)
#endif
#ifndef ERR
#define ERR(...)   do { } while (0)
#endif

#ifndef _WIN32
typedef int32_t HRESULT;
typedef uint16_t WORD;
typedef uint8_t UINT8;
typedef uint32_t UINT32;
typedef uint64_t UINT64;
typedef int64_t LONGLONG;
typedef uint64_t ULONGLONG;
typedef uintptr_t ULONG_PTR;
typedef uint16_t VARTYPE;
typedef WCHAR *LPWSTR;
typedef const WCHAR *LPCWSTR;
typedef WCHAR *BSTR;
typedef LONG DISPID;

#ifndef _GUID_DEFINED
#define _GUID_DEFINED
typedef struct _GUID {
    uint32_t Data1;
    uint16_t Data2;
    uint16_t Data3;
    uint8_t  Data4[8];
} GUID;
#endif

typedef GUID IID;
typedef GUID CLSID;
typedef const GUID *REFGUID;
typedef const IID *REFIID;
typedef const CLSID *REFCLSID;

static inline int IsEqualGUID(REFGUID rguid1, REFGUID rguid2)
{
    if (!rguid1 || !rguid2) {
        return 0;
    }
    return memcmp(rguid1, rguid2, sizeof(GUID)) == 0;
}

static inline int IsEqualIID(REFIID riid1, REFIID riid2)
{
    return IsEqualGUID(riid1, riid2);
}

static inline int IsEqualCLSID(REFCLSID rclsid1, REFCLSID rclsid2)
{
    return IsEqualGUID(rclsid1, rclsid2);
}

static inline void *CoTaskMemAlloc(size_t cb)
{
    return malloc(cb ? cb : 1);
}

static inline void CoTaskMemFree(void *pv)
{
    free(pv);
}
#endif /* !_WIN32 */

#ifndef S_OK
#define S_OK                         ((HRESULT)0x00000000L)
#endif
#ifndef S_FALSE
#define S_FALSE                      ((HRESULT)0x00000001L)
#endif
#ifndef E_NOTIMPL
#define E_NOTIMPL                    ((HRESULT)0x80004001L)
#endif
#ifndef E_NOINTERFACE
#define E_NOINTERFACE                ((HRESULT)0x80004002L)
#endif
#ifndef E_POINTER
#define E_POINTER                    ((HRESULT)0x80004003L)
#endif
#ifndef E_FAIL
#define E_FAIL                       ((HRESULT)0x80004005L)
#endif
#ifndef E_OUTOFMEMORY
#define E_OUTOFMEMORY                ((HRESULT)0x8007000EL)
#endif
#ifndef E_INVALIDARG
#define E_INVALIDARG                 ((HRESULT)0x80070057L)
#endif
#ifndef CLASS_E_NOAGGREGATION
#define CLASS_E_NOAGGREGATION        ((HRESULT)0x80040110L)
#endif
#ifndef CLASS_E_CLASSNOTAVAILABLE
#define CLASS_E_CLASSNOTAVAILABLE    ((HRESULT)0x80040111L)
#endif

#ifndef MF_E_INVALIDMEDIATYPE
#define MF_E_INVALIDMEDIATYPE        ((HRESULT)0xC00D36B4L)
#endif
#ifndef MF_E_INVALIDSTREAMNUMBER
#define MF_E_INVALIDSTREAMNUMBER     ((HRESULT)0xC00D36B3L)
#endif
#ifndef MF_E_NO_MORE_TYPES
#define MF_E_NO_MORE_TYPES           ((HRESULT)0xC00D36B9L)
#endif
#ifndef MF_E_TRANSFORM_TYPE_NOT_SET
#define MF_E_TRANSFORM_TYPE_NOT_SET  ((HRESULT)0xC00D6D60L)
#endif
#ifndef MF_E_TRANSFORM_NEED_MORE_INPUT
#define MF_E_TRANSFORM_NEED_MORE_INPUT ((HRESULT)0xC00D6D72L)
#endif

#ifndef DMO_E_INVALIDSTREAMINDEX
#define DMO_E_INVALIDSTREAMINDEX     ((HRESULT)0x80040201L)
#endif
#ifndef DMO_E_INVALIDTYPE
#define DMO_E_INVALIDTYPE            ((HRESULT)0x80040202L)
#endif
#ifndef DMO_E_TYPE_NOT_SET
#define DMO_E_TYPE_NOT_SET           ((HRESULT)0x80040203L)
#endif
#ifndef DMO_E_NOTACCEPTING
#define DMO_E_NOTACCEPTING           ((HRESULT)0x80040204L)
#endif
#ifndef DMO_E_TYPE_NOT_ACCEPTED
#define DMO_E_TYPE_NOT_ACCEPTED      ((HRESULT)0x80040205L)
#endif
#ifndef DMO_E_NO_MORE_ITEMS
#define DMO_E_NO_MORE_ITEMS          ((HRESULT)0x80040206L)
#endif

#ifndef NS_E_INVALID_REQUEST
#define NS_E_INVALID_REQUEST         ((HRESULT)0xC00D002BL)
#endif
#ifndef NS_E_NO_MORE_SAMPLES
#define NS_E_NO_MORE_SAMPLES         ((HRESULT)0xC00D0BCFL)
#endif

#ifndef SUCCEEDED
#define SUCCEEDED(hr) (((HRESULT)(hr)) >= 0)
#endif
#ifndef FAILED
#define FAILED(hr)    (((HRESULT)(hr)) < 0)
#endif

#ifndef WAVE_FORMAT_PCM
#define WAVE_FORMAT_PCM              0x0001
#endif
#ifndef WAVE_FORMAT_IEEE_FLOAT
#define WAVE_FORMAT_IEEE_FLOAT       0x0003
#endif
#ifndef WAVE_FORMAT_MSAUDIO1
#define WAVE_FORMAT_MSAUDIO1         0x0160
#endif
#ifndef WAVE_FORMAT_WMAUDIO2
#define WAVE_FORMAT_WMAUDIO2         0x0161
#endif
#ifndef WAVE_FORMAT_WMAUDIO3
#define WAVE_FORMAT_WMAUDIO3         0x0162
#endif
#ifndef WAVE_FORMAT_WMAUDIO_LOSSLESS
#define WAVE_FORMAT_WMAUDIO_LOSSLESS 0x0163
#endif

#ifndef MFT_SET_TYPE_TEST_ONLY
#define MFT_SET_TYPE_TEST_ONLY       0x00000001
#endif
#ifndef MFT_INPUT_STATUS_ACCEPT_DATA
#define MFT_INPUT_STATUS_ACCEPT_DATA 0x00000001
#endif
#ifndef MFT_OUTPUT_STATUS_SAMPLE_READY
#define MFT_OUTPUT_STATUS_SAMPLE_READY 0x00000001
#endif
#ifndef MFT_OUTPUT_DATA_BUFFER_NO_SAMPLE
#define MFT_OUTPUT_DATA_BUFFER_NO_SAMPLE 0x00000200
#endif

#ifndef DMO_SET_TYPEF_TEST_ONLY
#define DMO_SET_TYPEF_TEST_ONLY      0x00000001
#endif
#ifndef DMO_SET_TYPEF_CLEAR
#define DMO_SET_TYPEF_CLEAR          0x00000002
#endif
#ifndef DMO_INPUT_STATUSF_ACCEPT_DATA
#define DMO_INPUT_STATUSF_ACCEPT_DATA 0x00000001
#endif

typedef enum _MFT_MESSAGE_TYPE {
    MFT_MESSAGE_COMMAND_FLUSH                   = 0x00000000,
    MFT_MESSAGE_COMMAND_DRAIN                   = 0x00000001,
    MFT_MESSAGE_SET_D3D_MANAGER                 = 0x00000002,
    MFT_MESSAGE_DROP_SAMPLES                    = 0x00000003,
    MFT_MESSAGE_COMMAND_TICK                    = 0x00000004,
    MFT_MESSAGE_NOTIFY_BEGIN_STREAMING          = 0x10000000,
    MFT_MESSAGE_NOTIFY_END_STREAMING            = 0x10000001,
    MFT_MESSAGE_NOTIFY_END_OF_STREAM            = 0x10000002,
    MFT_MESSAGE_NOTIFY_START_OF_STREAM          = 0x10000003
} MFT_MESSAGE_TYPE;

typedef struct _MFT_INPUT_STREAM_INFO {
    LONGLONG hnsMaxLatency;
    DWORD    dwFlags;
    DWORD    cbSize;
    DWORD    cbMaxLookahead;
    DWORD    cbAlignment;
} MFT_INPUT_STREAM_INFO;

typedef struct _MFT_OUTPUT_STREAM_INFO {
    DWORD dwFlags;
    DWORD cbSize;
    DWORD cbAlignment;
} MFT_OUTPUT_STREAM_INFO;

typedef struct _WG_WAVEFORMATEX {
    WORD  wFormatTag;
    WORD  nChannels;
    DWORD nSamplesPerSec;
    DWORD nAvgBytesPerSec;
    WORD  nBlockAlign;
    WORD  wBitsPerSample;
    WORD  cbSize;
} WG_WAVEFORMATEX;

typedef struct _WG_DMO_MEDIA_TYPE {
    GUID     majortype;
    GUID     subtype;
    BOOL     bFixedSizeSamples;
    BOOL     bTemporalCompression;
     ULONG   lSampleSize;
    GUID     formattype;
    void    *pUnk;
    ULONG    cbFormat;
    BYTE    *pbFormat;
} WG_DMO_MEDIA_TYPE;

/* Forward declarations of lightweight COM interfaces for dual-mode C compilation */
typedef struct IUnknown WG_IUnknown;
typedef struct IClassFactory WG_IClassFactory;
typedef struct IMFTransform WG_IMFTransform;
typedef struct IMediaObject WG_IMediaObject;
typedef struct IPropertyBag WG_IPropertyBag;
typedef struct IWMSyncReader2 WG_IWMSyncReader2;

#ifndef __IUnknown_INTERFACE_DEFINED__
#define __IUnknown_INTERFACE_DEFINED__
typedef struct IUnknownVtbl {
    HRESULT (WINAPI *QueryInterface)(WG_IUnknown *This, REFIID riid, void **ppvObject);
    ULONG   (WINAPI *AddRef)(WG_IUnknown *This);
    ULONG   (WINAPI *Release)(WG_IUnknown *This);
} IUnknownVtbl;

struct IUnknown {
    const IUnknownVtbl *lpVtbl;
};
#endif

#ifndef IUnknown_QueryInterface
#define IUnknown_QueryInterface(This, riid, ppv) (This)->lpVtbl->QueryInterface(This, riid, ppv)
#define IUnknown_AddRef(This)                    (This)->lpVtbl->AddRef(This)
#define IUnknown_Release(This)                   (This)->lpVtbl->Release(This)
#endif

#ifndef __IClassFactory_INTERFACE_DEFINED__
#define __IClassFactory_INTERFACE_DEFINED__
typedef struct IClassFactoryVtbl {
    HRESULT (WINAPI *QueryInterface)(WG_IClassFactory *This, REFIID riid, void **ppvObject);
    ULONG   (WINAPI *AddRef)(WG_IClassFactory *This);
    ULONG   (WINAPI *Release)(WG_IClassFactory *This);
    HRESULT (WINAPI *CreateInstance)(WG_IClassFactory *This, WG_IUnknown *pUnkOuter, REFIID riid, void **ppvObject);
    HRESULT (WINAPI *LockServer)(WG_IClassFactory *This, BOOL fLock);
} IClassFactoryVtbl;

struct IClassFactory {
    const IClassFactoryVtbl *lpVtbl;
};
#endif

#ifndef IClassFactory_QueryInterface
#define IClassFactory_QueryInterface(This, riid, ppv) (This)->lpVtbl->QueryInterface(This, riid, ppv)
#define IClassFactory_AddRef(This)                    (This)->lpVtbl->AddRef(This)
#define IClassFactory_Release(This)                   (This)->lpVtbl->Release(This)
#define IClassFactory_CreateInstance(This, o, r, p)   (This)->lpVtbl->CreateInstance(This, o, r, p)
#define IClassFactory_LockServer(This, l)             (This)->lpVtbl->LockServer(This, l)
#endif

typedef struct _MFT_OUTPUT_DATA_BUFFER {
    DWORD        dwStreamID;
    void        *pSample;
    DWORD        dwStatus;
    void        *pEvents;
} MFT_OUTPUT_DATA_BUFFER;

typedef struct IMFTransformVtbl {
    HRESULT (WINAPI *QueryInterface)(WG_IMFTransform *This, REFIID riid, void **ppvObject);
    ULONG   (WINAPI *AddRef)(WG_IMFTransform *This);
    ULONG   (WINAPI *Release)(WG_IMFTransform *This);
    HRESULT (WINAPI *GetStreamLimits)(WG_IMFTransform *This, DWORD *pdwInputMinimum, DWORD *pdwInputMaximum, DWORD *pdwOutputMinimum, DWORD *pdwOutputMaximum);
    HRESULT (WINAPI *GetStreamCount)(WG_IMFTransform *This, DWORD *pcInputStreams, DWORD *pcOutputStreams);
    HRESULT (WINAPI *GetStreamIDs)(WG_IMFTransform *This, DWORD dwInputIDArraySize, DWORD *pdwInputIDs, DWORD dwOutputIDArraySize, DWORD *pdwOutputIDs);
    HRESULT (WINAPI *GetInputStreamInfo)(WG_IMFTransform *This, DWORD dwInputStreamID, MFT_INPUT_STREAM_INFO *pStreamInfo);
    HRESULT (WINAPI *GetOutputStreamInfo)(WG_IMFTransform *This, DWORD dwOutputStreamID, MFT_OUTPUT_STREAM_INFO *pStreamInfo);
    HRESULT (WINAPI *GetAttributes)(WG_IMFTransform *This, void **pAttributes);
    HRESULT (WINAPI *GetInputStreamAttributes)(WG_IMFTransform *This, DWORD dwInputStreamID, void **pAttributes);
    HRESULT (WINAPI *GetOutputStreamAttributes)(WG_IMFTransform *This, DWORD dwOutputStreamID, void **pAttributes);
    HRESULT (WINAPI *DeleteInputStream)(WG_IMFTransform *This, DWORD dwStreamID);
    HRESULT (WINAPI *AddInputStreams)(WG_IMFTransform *This, DWORD cStreams, DWORD *adwStreamIDs);
    HRESULT (WINAPI *GetInputAvailableType)(WG_IMFTransform *This, DWORD dwInputStreamID, DWORD dwTypeIndex, void **ppType);
    HRESULT (WINAPI *GetOutputAvailableType)(WG_IMFTransform *This, DWORD dwOutputStreamID, DWORD dwTypeIndex, void **ppType);
    HRESULT (WINAPI *SetInputType)(WG_IMFTransform *This, DWORD dwInputStreamID, void *pType, DWORD dwFlags);
    HRESULT (WINAPI *SetOutputType)(WG_IMFTransform *This, DWORD dwOutputStreamID, void *pType, DWORD dwFlags);
    HRESULT (WINAPI *GetInputCurrentType)(WG_IMFTransform *This, DWORD dwInputStreamID, void **ppType);
    HRESULT (WINAPI *GetOutputCurrentType)(WG_IMFTransform *This, DWORD dwOutputStreamID, void **ppType);
    HRESULT (WINAPI *GetInputStatus)(WG_IMFTransform *This, DWORD dwInputStreamID, DWORD *pdwFlags);
    HRESULT (WINAPI *GetOutputStatus)(WG_IMFTransform *This, DWORD *pdwFlags);
    HRESULT (WINAPI *SetOutputBounds)(WG_IMFTransform *This, LONGLONG hnsLowerBound, LONGLONG hnsUpperBound);
    HRESULT (WINAPI *ProcessEvent)(WG_IMFTransform *This, DWORD dwInputStreamID, void *pEvent);
    HRESULT (WINAPI *ProcessMessage)(WG_IMFTransform *This, MFT_MESSAGE_TYPE eMessage, ULONG_PTR ulParam);
    HRESULT (WINAPI *ProcessInput)(WG_IMFTransform *This, DWORD dwInputStreamID, void *pSample, DWORD dwFlags);
    HRESULT (WINAPI *ProcessOutput)(WG_IMFTransform *This, DWORD dwFlags, DWORD cOutputBufferCount, MFT_OUTPUT_DATA_BUFFER *pOutputSamples, DWORD *pdwStatus);
} IMFTransformVtbl;

struct IMFTransform {
    const IMFTransformVtbl *lpVtbl;
};

typedef struct _DMO_OUTPUT_DATA_BUFFER {
    void     *pBuffer;
    DWORD     dwStatus;
    LONGLONG  rtTimestamp;
    LONGLONG  rtTimelength;
} DMO_OUTPUT_DATA_BUFFER;

typedef struct IMediaObjectVtbl {
    HRESULT (WINAPI *QueryInterface)(WG_IMediaObject *This, REFIID riid, void **ppvObject);
    ULONG   (WINAPI *AddRef)(WG_IMediaObject *This);
    ULONG   (WINAPI *Release)(WG_IMediaObject *This);
    HRESULT (WINAPI *GetStreamCount)(WG_IMediaObject *This, DWORD *pcInputStreams, DWORD *pcOutputStreams);
    HRESULT (WINAPI *GetInputStreamInfo)(WG_IMediaObject *This, DWORD dwInputStreamIndex, DWORD *pdwFlags);
    HRESULT (WINAPI *GetOutputStreamInfo)(WG_IMediaObject *This, DWORD dwOutputStreamIndex, DWORD *pdwFlags);
    HRESULT (WINAPI *GetInputType)(WG_IMediaObject *This, DWORD dwInputStreamIndex, DWORD dwTypeIndex, WG_DMO_MEDIA_TYPE *pmt);
    HRESULT (WINAPI *GetOutputType)(WG_IMediaObject *This, DWORD dwOutputStreamIndex, DWORD dwTypeIndex, WG_DMO_MEDIA_TYPE *pmt);
    HRESULT (WINAPI *SetInputType)(WG_IMediaObject *This, DWORD dwInputStreamIndex, const WG_DMO_MEDIA_TYPE *pmt, DWORD dwFlags);
    HRESULT (WINAPI *SetOutputType)(WG_IMediaObject *This, DWORD dwOutputStreamIndex, const WG_DMO_MEDIA_TYPE *pmt, DWORD dwFlags);
    HRESULT (WINAPI *GetInputCurrentType)(WG_IMediaObject *This, DWORD dwInputStreamIndex, WG_DMO_MEDIA_TYPE *pmt);
    HRESULT (WINAPI *GetOutputCurrentType)(WG_IMediaObject *This, DWORD dwOutputStreamIndex, WG_DMO_MEDIA_TYPE *pmt);
    HRESULT (WINAPI *GetInputSizeInfo)(WG_IMediaObject *This, DWORD dwInputStreamIndex, DWORD *pcbSize, DWORD *pcbMaxLookahead, DWORD *pcbAlignment);
    HRESULT (WINAPI *GetOutputSizeInfo)(WG_IMediaObject *This, DWORD dwOutputStreamIndex, DWORD *pcbSize, DWORD *pcbAlignment);
    HRESULT (WINAPI *GetInputMaxLatency)(WG_IMediaObject *This, DWORD dwInputStreamIndex, LONGLONG *prtMaxLatency);
    HRESULT (WINAPI *SetInputMaxLatency)(WG_IMediaObject *This, DWORD dwInputStreamIndex, LONGLONG rtMaxLatency);
    HRESULT (WINAPI *Flush)(WG_IMediaObject *This);
    HRESULT (WINAPI *Discontinuity)(WG_IMediaObject *This, DWORD dwInputStreamIndex);
    HRESULT (WINAPI *AllocateStreamingResources)(WG_IMediaObject *This);
    HRESULT (WINAPI *FreeStreamingResources)(WG_IMediaObject *This);
    HRESULT (WINAPI *GetInputStatus)(WG_IMediaObject *This, DWORD dwInputStreamIndex, DWORD *dwFlags);
    HRESULT (WINAPI *ProcessInput)(WG_IMediaObject *This, DWORD dwInputStreamIndex, void *pBuffer, DWORD dwFlags, LONGLONG rtTimestamp, LONGLONG rtTimelength);
    HRESULT (WINAPI *ProcessOutput)(WG_IMediaObject *This, DWORD dwFlags, DWORD cOutputBufferCount, DMO_OUTPUT_DATA_BUFFER *pOutputBuffers, DWORD *pdwStatus);
    HRESULT (WINAPI *Lock)(WG_IMediaObject *This, LONG bLock);
} IMediaObjectVtbl;

struct IMediaObject {
    const IMediaObjectVtbl *lpVtbl;
};

typedef struct IPropertyBagVtbl {
    HRESULT (WINAPI *QueryInterface)(WG_IPropertyBag *This, REFIID riid, void **ppvObject);
    ULONG   (WINAPI *AddRef)(WG_IPropertyBag *This);
    ULONG   (WINAPI *Release)(WG_IPropertyBag *This);
    HRESULT (WINAPI *Read)(WG_IPropertyBag *This, LPCWSTR pszPropName, void *pVar, void *pErrorLog);
    HRESULT (WINAPI *Write)(WG_IPropertyBag *This, LPCWSTR pszPropName, void *pVar);
} IPropertyBagVtbl;

struct IPropertyBag {
    const IPropertyBagVtbl *lpVtbl;
};

typedef struct IWMSyncReader2Vtbl {
    HRESULT (WINAPI *QueryInterface)(WG_IWMSyncReader2 *This, REFIID riid, void **ppvObject);
    ULONG   (WINAPI *AddRef)(WG_IWMSyncReader2 *This);
    ULONG   (WINAPI *Release)(WG_IWMSyncReader2 *This);
    HRESULT (WINAPI *Open)(WG_IWMSyncReader2 *This, const WCHAR *pwszFilename);
    HRESULT (WINAPI *Close)(WG_IWMSyncReader2 *This);
    HRESULT (WINAPI *SetRange)(WG_IWMSyncReader2 *This, ULONGLONG cnsStartTime, LONGLONG cnsDuration);
    HRESULT (WINAPI *SetRangeByFrame)(WG_IWMSyncReader2 *This, WORD wStreamNum, ULONGLONG qwFrameNumber, LONGLONG cFramesToRead);
    HRESULT (WINAPI *GetNextSample)(WG_IWMSyncReader2 *This, WORD wStreamNum, void **ppSample, ULONGLONG *pcnsSampleTime, ULONGLONG *pcnsDuration, DWORD *pdwFlags, DWORD *pdwOutputNum, WORD *pwStreamNum);
    HRESULT (WINAPI *SetStreamsSelected)(WG_IWMSyncReader2 *This, WORD cStreamCount, WORD *pwStreamNumbers, int *pSelections);
    HRESULT (WINAPI *GetStreamSelected)(WG_IWMSyncReader2 *This, WORD wStreamNum, int *pSelection);
    HRESULT (WINAPI *SetReadStreamSamples)(WG_IWMSyncReader2 *This, WORD wStreamNum, BOOL fCompressed);
    HRESULT (WINAPI *GetReadStreamSamples)(WG_IWMSyncReader2 *This, WORD wStreamNum, BOOL *pfCompressed);
    HRESULT (WINAPI *GetOutputSetting)(WG_IWMSyncReader2 *This, DWORD dwOutputNum, const WCHAR *pszName, int *pType, BYTE *pValue, WORD *pcbLength);
    HRESULT (WINAPI *SetOutputSetting)(WG_IWMSyncReader2 *This, DWORD dwOutputNum, const WCHAR *pszName, int Type, const BYTE *pValue, WORD cbLength);
    HRESULT (WINAPI *GetOutputCount)(WG_IWMSyncReader2 *This, DWORD *pcOutputs);
    HRESULT (WINAPI *GetOutputProps)(WG_IWMSyncReader2 *This, DWORD dwOutputNum, void **ppOutput);
    HRESULT (WINAPI *SetOutputProps)(WG_IWMSyncReader2 *This, DWORD dwOutputNum, void *pOutput);
    HRESULT (WINAPI *GetOutputFormatCount)(WG_IWMSyncReader2 *This, DWORD dwOutputNum, DWORD *pcFormats);
    HRESULT (WINAPI *GetOutputFormat)(WG_IWMSyncReader2 *This, DWORD dwOutputNum, DWORD dwFormatNum, void **ppProps);
    HRESULT (WINAPI *GetOutputNumberForStream)(WG_IWMSyncReader2 *This, WORD wStreamNum, DWORD *pdwOutputNum);
    HRESULT (WINAPI *GetStreamNumberForOutput)(WG_IWMSyncReader2 *This, DWORD dwOutputNum, WORD *pwStreamNum);
    HRESULT (WINAPI *GetMaxOutputSampleSize)(WG_IWMSyncReader2 *This, DWORD dwOutput, DWORD *pcbMax);
    HRESULT (WINAPI *GetMaxStreamSampleSize)(WG_IWMSyncReader2 *This, WORD wStream, DWORD *pcbMax);
    HRESULT (WINAPI *OpenStream)(WG_IWMSyncReader2 *This, void *pStream);
    HRESULT (WINAPI *SetRangeByTimecode)(WG_IWMSyncReader2 *This, WORD wStreamNum, void *pStart, void *pEnd);
    HRESULT (WINAPI *SetRangeByFrameEx)(WG_IWMSyncReader2 *This, WORD wStreamNum, ULONGLONG qwFrameNumber, LONGLONG cFramesToRead, ULONGLONG *pcnsStartTime);
    HRESULT (WINAPI *SetAllocateForOutput)(WG_IWMSyncReader2 *This, DWORD dwOutputNum, void *pAllocator);
    HRESULT (WINAPI *GetAllocateForOutput)(WG_IWMSyncReader2 *This, DWORD dwOutputNum, void **ppAllocator);
    HRESULT (WINAPI *SetAllocateForStream)(WG_IWMSyncReader2 *This, WORD wStreamNum, void *pAllocator);
    HRESULT (WINAPI *GetAllocateForStream)(WG_IWMSyncReader2 *This, WORD wStreamNum, void **ppAllocator);
} IWMSyncReader2Vtbl;

struct IWMSyncReader2 {
    const IWMSyncReader2Vtbl *lpVtbl;
};

/* Standard GUID declarations */
extern const GUID IID_IUnknown;
extern const GUID IID_IClassFactory;
extern const GUID IID_IMFTransform;
extern const GUID IID_IMediaObject;
extern const GUID IID_IPropertyBag;
extern const GUID IID_IWMSyncReader;
extern const GUID IID_IWMSyncReader2;

extern const GUID CLSID_WMADecMediaObject;
extern const GUID CLSID_wg_wma_decoder;
extern const GUID CLSID_CMSAACDecMFT;
extern const GUID CLSID_CMSH264DecoderMFT;
extern const GUID CLSID_wg_mp3_decoder;
extern const GUID CLSID_wg_wave_parser;
extern const GUID CLSID_decodebin_parser;

extern const GUID MFMediaType_Audio;
extern const GUID MFMediaType_Video;
extern const GUID MFAudioFormat_PCM;
extern const GUID MFAudioFormat_Float;
extern const GUID MEDIASUBTYPE_MSAUDIO1;
extern const GUID MFAudioFormat_WMAudioV8;
extern const GUID MFAudioFormat_WMAudioV9;
extern const GUID MFAudioFormat_WMAudio_Lossless;
extern const GUID FORMAT_WaveFormatEx;

/* Canonical winegstreamer.spec exports */
HRESULT WINAPI DllCanUnloadNow(void);
HRESULT WINAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, void **ppv);
HRESULT WINAPI DllRegisterServer(void);
HRESULT WINAPI DllUnregisterServer(void);
HRESULT WINAPI winegstreamer_create_wm_sync_reader(WG_IUnknown *outer, void **out);
HRESULT WINAPI winegstreamer_create_video_decoder(WG_IMFTransform **out);
HRESULT WINAPI winegstreamer_create_wma_decoder(WG_IUnknown *outer, REFIID riid, void **out);

/* macOS AudioToolbox / VideoToolbox / CoreMedia bridge entrypoints */
HRESULT WINAPI WineGStreamerQueryMacOSBackend(
    BOOL *out_audiotoolbox_ready,
    BOOL *out_videotoolbox_ready,
    BOOL *out_coremedia_ready);

HRESULT WINAPI WineGStreamerAudioToolboxDecodePacket(
    const WG_WAVEFORMATEX *input_wfx,
    const BYTE *input_packet,
    DWORD input_len,
    BOOL output_ieee_float,
    BYTE *output_pcm,
    DWORD output_cap,
    DWORD *out_written);

HRESULT WINAPI WineGStreamerVideoToolboxDecodeFrame(
    DWORD codec_fourcc,
    UINT32 width,
    UINT32 height,
    const BYTE *compressed_frame,
    DWORD compressed_len,
    BYTE *output_nv12,
    DWORD output_cap,
    DWORD *out_written);

#endif /* _WINE_WINEGSTREAMER_H */

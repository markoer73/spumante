/*
 * Wine qcap.dll DirectShow & Media Foundation Capture Header
 * (macOS AVFoundation Video Capture & CoreAudio CLSID_AudioRecord)
 *
 * Clean-room declaration derived from Microsoft Learn public specifications
 * and upstream Wine dlls/qcap/qcap_private.h & include/strmif.idl.
 *
 * Copyright 2026 Spumante Contributors
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef _WINE_QCAP_H
#define _WINE_QCAP_H

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
typedef uint16_t VARTYPE;
typedef WCHAR *LPWSTR;
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

typedef struct tagRECT {
    LONG left;
    LONG top;
    LONG right;
    LONG bottom;
} RECT;

typedef struct tagSIZE {
    LONG cx;
    LONG cy;
} SIZE;

typedef struct tagBITMAPINFOHEADER {
    DWORD biSize;
    LONG  biWidth;
    LONG  biHeight;
    WORD  biPlanes;
    WORD  biBitCount;
    DWORD biCompression;
    DWORD biSizeImage;
    LONG  biXPelsPerMeter;
    LONG  biYPelsPerMeter;
    DWORD biClrUsed;
    DWORD biClrImportant;
} BITMAPINFOHEADER;

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
#else
typedef unsigned short VARTYPE;
typedef WCHAR *BSTR;
typedef LONG DISPID;

static inline void *qcap_task_alloc(size_t cb)
{
    return malloc(cb ? cb : 1);
}

static inline void qcap_task_free(void *pv)
{
    free(pv);
}
#define CoTaskMemAlloc qcap_task_alloc
#define CoTaskMemFree  qcap_task_free
#endif

typedef LONGLONG REFERENCE_TIME;

#ifndef BI_RGB
#define BI_RGB 0U
#endif

#ifndef WAVE_FORMAT_PCM
#define WAVE_FORMAT_PCM 1U
#endif

#ifndef VT_EMPTY
#define VT_EMPTY 0
#define VT_I4    3
#define VT_BSTR  8
#endif

typedef struct tagVARIANT {
    VARTYPE vt;
    WORD    wReserved1;
    WORD    wReserved2;
    WORD    wReserved3;
    union {
        LONG   lVal;
        BSTR   bstrVal;
        void  *byref;
    } u;
} VARIANT;

#ifndef SUCCEEDED
#define SUCCEEDED(hr) (((HRESULT)(hr)) >= 0)
#endif
#ifndef FAILED
#define FAILED(hr) (((HRESULT)(hr)) < 0)
#endif

#ifndef S_OK
#define S_OK ((HRESULT)0x00000000L)
#endif
#ifndef S_FALSE
#define S_FALSE ((HRESULT)0x00000001L)
#endif
#ifndef E_NOTIMPL
#define E_NOTIMPL ((HRESULT)0x80004001L)
#endif
#ifndef E_NOINTERFACE
#define E_NOINTERFACE ((HRESULT)0x80004002L)
#endif
#ifndef E_POINTER
#define E_POINTER ((HRESULT)0x80004003L)
#endif
#ifndef E_ABORT
#define E_ABORT ((HRESULT)0x80004004L)
#endif
#ifndef E_FAIL
#define E_FAIL ((HRESULT)0x80004005L)
#endif
#ifndef E_UNEXPECTED
#define E_UNEXPECTED ((HRESULT)0x8000FFFFL)
#endif
#ifndef E_OUTOFMEMORY
#define E_OUTOFMEMORY ((HRESULT)0x8007000EL)
#endif
#ifndef E_INVALIDARG
#define E_INVALIDARG ((HRESULT)0x80070057L)
#endif
#ifndef E_PROP_ID_UNSUPPORTED
#define E_PROP_ID_UNSUPPORTED ((HRESULT)0x80070490L)
#endif

#ifndef CLASS_E_NOAGGREGATION
#define CLASS_E_NOAGGREGATION ((HRESULT)0x80040110L)
#endif
#ifndef CLASS_E_CLASSNOTAVAILABLE
#define CLASS_E_CLASSNOTAVAILABLE ((HRESULT)0x80040111L)
#endif

#ifndef VFW_E_ALREADY_CONNECTED
#define VFW_E_ALREADY_CONNECTED ((HRESULT)0x80040204L)
#endif
#ifndef VFW_E_NO_ACCEPTABLE_TYPES
#define VFW_E_NO_ACCEPTABLE_TYPES ((HRESULT)0x80040207L)
#endif
#ifndef VFW_E_INVALID_DIRECTION
#define VFW_E_INVALID_DIRECTION ((HRESULT)0x80040208L)
#endif
#ifndef VFW_E_NOT_CONNECTED
#define VFW_E_NOT_CONNECTED ((HRESULT)0x80040209L)
#endif
#ifndef VFW_E_NO_ALLOCATOR
#define VFW_E_NO_ALLOCATOR ((HRESULT)0x8004020AL)
#endif
#ifndef VFW_E_NO_CAPTURE_HARDWARE
#define VFW_E_NO_CAPTURE_HARDWARE ((HRESULT)0x8004021CL)
#endif
#ifndef VFW_E_NOT_STOPPED
#define VFW_E_NOT_STOPPED ((HRESULT)0x80040224L)
#endif
#ifndef VFW_E_TYPE_NOT_ACCEPTED
#define VFW_E_TYPE_NOT_ACCEPTED ((HRESULT)0x8004022AL)
#endif
#ifndef VFW_S_CANT_CUE
#define VFW_S_CANT_CUE ((HRESULT)0x00040268L)
#endif

#ifndef MF_E_INVALIDSTREAMNUMBER
#define MF_E_INVALIDSTREAMNUMBER ((HRESULT)0xC00D36B3L)
#endif
#ifndef MF_E_INVALIDMEDIATYPE
#define MF_E_INVALIDMEDIATYPE ((HRESULT)0xC00D36B4L)
#endif

#define QCAP_MF_SOURCE_READER_FIRST_VIDEO_STREAM 0xFFFFFFFCU
#define QCAP_MF_SOURCE_READER_FIRST_AUDIO_STREAM 0xFFFFFFFDU

/* Forward interface declarations */
typedef struct IUnknown IUnknown;
typedef struct IClassFactory IClassFactory;
typedef struct IErrorLog IErrorLog;
typedef struct IPropertyBag IPropertyBag;
typedef struct IPersistPropertyBag IPersistPropertyBag;
typedef struct IReferenceClock IReferenceClock;
typedef struct IFilterGraph IFilterGraph;
typedef struct IBaseFilter IBaseFilter;
typedef struct IPin IPin;
typedef struct IEnumPins IEnumPins;
typedef struct IEnumMediaTypes IEnumMediaTypes;
typedef struct IAMFilterMiscFlags IAMFilterMiscFlags;
typedef struct IAMStreamConfig IAMStreamConfig;
typedef struct IAMBufferNegotiation IAMBufferNegotiation;
typedef struct IKsPropertySet IKsPropertySet;
typedef struct IQualityControl IQualityControl;
typedef struct IAMVideoControl IAMVideoControl;
typedef struct IAMVideoProcAmp IAMVideoProcAmp;
typedef struct IAMDroppedFrames IAMDroppedFrames;
typedef struct IMediaSample IMediaSample;
typedef struct IMemAllocator IMemAllocator;
typedef struct IMemInputPin IMemInputPin;

/* DirectShow Structures & Enums */
typedef struct tWAVEFORMATEX {
    WORD  wFormatTag;
    WORD  nChannels;
    DWORD nSamplesPerSec;
    DWORD nAvgBytesPerSec;
    WORD  nBlockAlign;
    WORD  wBitsPerSample;
    WORD  cbSize;
} WAVEFORMATEX;

typedef struct tagVIDEOINFOHEADER {
    RECT             rcSource;
    RECT             rcTarget;
    DWORD            dwBitRate;
    DWORD            dwBitErrorRate;
    REFERENCE_TIME   AvgTimePerFrame;
    BITMAPINFOHEADER bmiHeader;
} VIDEOINFOHEADER;

typedef struct _AMMediaType {
    GUID     majortype;
    GUID     subtype;
    BOOL     bFixedSizeSamples;
    BOOL     bTemporalCompression;
    ULONG    lSampleSize;
    GUID     formattype;
    IUnknown *pUnk;
    ULONG    cbFormat;
    BYTE     *pbFormat;
} AM_MEDIA_TYPE;

typedef enum _PinDirection {
    PINDIR_INPUT  = 0,
    PINDIR_OUTPUT = 1
} PIN_DIRECTION;

typedef struct _PinInfo {
    IBaseFilter   *pFilter;
    PIN_DIRECTION dir;
    WCHAR         achName[128];
} PIN_INFO;

typedef enum _FilterState {
    State_Stopped = 0,
    State_Paused  = 1,
    State_Running = 2
} FILTER_STATE;

typedef struct _FilterInfo {
    WCHAR        achName[128];
    IFilterGraph *pGraph;
} FILTER_INFO;

typedef struct _AllocatorProperties {
    LONG cBuffers;
    LONG cbBuffer;
    LONG cbAlign;
    LONG cbPrefix;
} ALLOCATOR_PROPERTIES;

typedef struct _AUDIO_STREAM_CONFIG_CAPS {
    GUID  guid;
    ULONG MinimumChannels;
    ULONG MaximumChannels;
    ULONG ChannelsGranularity;
    ULONG MinimumBitsPerSample;
    ULONG MaximumBitsPerSample;
    ULONG BitsPerSampleGranularity;
    ULONG MinimumSampleFrequency;
    ULONG MaximumSampleFrequency;
    ULONG SampleFrequencyGranularity;
} AUDIO_STREAM_CONFIG_CAPS;

typedef struct _VIDEO_STREAM_CONFIG_CAPS {
    GUID     guid;
    ULONG    VideoStandard;
    SIZE     InputSize;
    SIZE     MinCroppingSize;
    SIZE     MaxCroppingSize;
    int      CropGranularityX;
    int      CropGranularityY;
    int      CropAlignX;
    int      CropAlignY;
    SIZE     MinOutputSize;
    SIZE     MaxOutputSize;
    int      OutputGranularityX;
    int      OutputGranularityY;
    int      StretchTapsX;
    int      StretchTapsY;
    int      ShrinkTapsX;
    int      ShrinkTapsY;
    LONGLONG MinFrameInterval;
    LONGLONG MaxFrameInterval;
    LONG     MinBitsPerSecond;
    LONG     MaxBitsPerSecond;
} VIDEO_STREAM_CONFIG_CAPS;

typedef enum tagVideoProcAmpProperty {
    VideoProcAmp_Brightness            = 0,
    VideoProcAmp_Contrast              = 1,
    VideoProcAmp_Hue                   = 2,
    VideoProcAmp_Saturation            = 3,
    VideoProcAmp_Sharpness             = 4,
    VideoProcAmp_Gamma                 = 5,
    VideoProcAmp_ColorEnable           = 6,
    VideoProcAmp_WhiteBalance          = 7,
    VideoProcAmp_BacklightCompensation = 8,
    VideoProcAmp_Gain                  = 9
} VideoProcAmpProperty;

typedef enum tagVideoProcAmpFlags {
    VideoProcAmp_Flags_Auto   = 0x0001,
    VideoProcAmp_Flags_Manual = 0x0002
} VideoProcAmpFlags;

typedef enum tagVideoControlFlags {
    VideoControlFlag_FlipHorizontal        = 0x0001,
    VideoControlFlag_FlipVertical          = 0x0002,
    VideoControlFlag_ExternalTriggerEnable = 0x0004,
    VideoControlFlag_Trigger               = 0x0008
} VideoControlFlags;

typedef enum _AM_FILTER_MISC_FLAGS {
    AM_FILTER_MISC_FLAGS_IS_RENDERER = 0x00000001,
    AM_FILTER_MISC_FLAGS_IS_SOURCE   = 0x00000002
} AM_FILTER_MISC_FLAGS;

#define AMPROPERTY_PIN_CATEGORY 0
#define KSPROPERTY_SUPPORT_GET  1
#define KSPROPERTY_SUPPORT_SET  2

typedef enum {
    Famine = 0,
    Flood  = 1
} QualityMessageType;

typedef struct tagQuality {
    QualityMessageType Type;
    LONG               Proportion;
    REFERENCE_TIME     Late;
    REFERENCE_TIME     TimeStamp;
} Quality;

/* Well-known COM / DirectShow CLSIDs & IIDs */
static const GUID IID_IUnknown = {
    0x00000000, 0x0000, 0x0000, {0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}
};
static const GUID IID_IClassFactory = {
    0x00000001, 0x0000, 0x0000, {0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}
};
static const GUID IID_IPersist = {
    0x0000010c, 0x0000, 0x0000, {0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}
};
static const GUID IID_IPropertyBag = {
    0x55272a00, 0x42cb, 0x11ce, {0x81, 0x35, 0x00, 0xaa, 0x00, 0x4b, 0xb8, 0x51}
};
static const GUID IID_IPersistPropertyBag = {
    0x37d84f60, 0x42cb, 0x11ce, {0x81, 0x35, 0x00, 0xaa, 0x00, 0x4b, 0xb8, 0x51}
};
static const GUID IID_IMediaFilter = {
    0x56a86899, 0x0ad4, 0x11ce, {0xb0, 0x3a, 0x00, 0x20, 0xaf, 0x0b, 0xa7, 0x70}
};
static const GUID IID_IBaseFilter = {
    0x56a86895, 0x0ad4, 0x11ce, {0xb0, 0x3a, 0x00, 0x20, 0xaf, 0x0b, 0xa7, 0x70}
};
static const GUID IID_IPin = {
    0x56a86891, 0x0ad4, 0x11ce, {0xb0, 0x3a, 0x00, 0x20, 0xaf, 0x0b, 0xa7, 0x70}
};
static const GUID IID_IEnumPins = {
    0x56a86892, 0x0ad4, 0x11ce, {0xb0, 0x3a, 0x00, 0x20, 0xaf, 0x0b, 0xa7, 0x70}
};
static const GUID IID_IEnumMediaTypes = {
    0x89c31040, 0x846b, 0x11ce, {0x97, 0xd3, 0x00, 0xaa, 0x00, 0x55, 0x59, 0x5a}
};
static const GUID IID_IMediaSample = {
    0x56a8689a, 0x0ad4, 0x11ce, {0xb0, 0x3a, 0x00, 0x20, 0xaf, 0x0b, 0xa7, 0x70}
};
static const GUID IID_IMemAllocator = {
    0x56a8689c, 0x0ad4, 0x11ce, {0xb0, 0x3a, 0x00, 0x20, 0xaf, 0x0b, 0xa7, 0x70}
};
static const GUID IID_IMemInputPin = {
    0x56a8689d, 0x0ad4, 0x11ce, {0xb0, 0x3a, 0x00, 0x20, 0xaf, 0x0b, 0xa7, 0x70}
};
static const GUID IID_IAMFilterMiscFlags = {
    0x2dd74950, 0xa890, 0x11d1, {0xab, 0xe8, 0x00, 0xa0, 0xc9, 0x05, 0xf3, 0x75}
};
static const GUID IID_IAMStreamConfig = {
    0xc6e13340, 0x30ac, 0x11d0, {0xa1, 0x8c, 0x00, 0xa0, 0xc9, 0x11, 0x89, 0x56}
};
static const GUID IID_IAMBufferNegotiation = {
    0x56ed71a0, 0xaf5f, 0x11d0, {0xb3, 0xf0, 0x00, 0xaa, 0x00, 0x37, 0x61, 0xc5}
};
static const GUID IID_IKsPropertySet = {
    0x31efac30, 0x515c, 0x11d0, {0xa9, 0xaa, 0x00, 0xaa, 0x00, 0x61, 0xbe, 0x93}
};
static const GUID IID_IQualityControl = {
    0x56a868a5, 0x0ad4, 0x11ce, {0xb0, 0x3a, 0x00, 0x20, 0xaf, 0x0b, 0xa7, 0x70}
};
static const GUID IID_IAMVideoControl = {
    0x6a2e0670, 0x28e4, 0x11d0, {0xa1, 0x8c, 0x00, 0xa0, 0xc9, 0x11, 0x89, 0x56}
};
static const GUID IID_IAMVideoProcAmp = {
    0xc6e13360, 0x30ac, 0x11d0, {0xa1, 0x8c, 0x00, 0xa0, 0xc9, 0x11, 0x89, 0x56}
};
static const GUID IID_IAMDroppedFrames = {
    0xc6e13344, 0x30ac, 0x11d0, {0xa1, 0x8c, 0x00, 0xa0, 0xc9, 0x11, 0x89, 0x56}
};

/* qcap Filter CLSIDs */
static const GUID CLSID_AudioRecord = {
    0xe30629d2, 0x27e5, 0x11ce, {0x87, 0x5d, 0x00, 0x60, 0x8c, 0xb7, 0x80, 0x66}
};
static const GUID CLSID_VfwCapture = {
    0x1b544c22, 0xfd0b, 0x11ce, {0x8c, 0x63, 0x00, 0xaa, 0x00, 0x44, 0xb5, 0x1e}
};
static const GUID CLSID_AVIDec = {
    0xcf49d4e0, 0x1115, 0x11ce, {0xb0, 0x3a, 0x00, 0x20, 0xaf, 0x0b, 0xa7, 0x70}
};
static const GUID CLSID_AviDest = {
    0xe2510970, 0xf137, 0x11ce, {0x8b, 0x67, 0x00, 0xaa, 0x00, 0xa3, 0xf1, 0xa6}
};
static const GUID CLSID_CaptureGraphBuilder = {
    0xbf87b6e0, 0x8c27, 0x11d0, {0xb3, 0xf0, 0x00, 0xaa, 0x00, 0x37, 0x61, 0xc5}
};
static const GUID CLSID_CaptureGraphBuilder2 = {
    0xbf87b6e1, 0x8c27, 0x11d0, {0xb3, 0xf0, 0x00, 0xaa, 0x00, 0x37, 0x61, 0xc5}
};
static const GUID CLSID_FileWriter = {
    0x8596e5f0, 0x0da5, 0x11d0, {0xbd, 0x21, 0x00, 0xa0, 0xc9, 0x11, 0xce, 0x86}
};
static const GUID CLSID_SmartTee = {
    0xcc58e280, 0x8aa1, 0x11d1, {0xb3, 0xf1, 0x00, 0xaa, 0x00, 0x37, 0x61, 0xc5}
};

/* Media Type & Category GUIDs */
static const GUID GUID_NULL = {
    0x00000000, 0x0000, 0x0000, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}
};
static const GUID MEDIATYPE_Video = {
    0x73646976, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}
};
static const GUID MEDIATYPE_Audio = {
    0x73647561, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}
};
static const GUID MEDIASUBTYPE_PCM = {
    0x00000001, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}
};
static const GUID MEDIASUBTYPE_RGB24 = {
    0xe436eb7d, 0x524f, 0x11ce, {0x9f, 0x53, 0x00, 0x20, 0xaf, 0x0b, 0xa7, 0x70}
};
static const GUID MEDIASUBTYPE_RGB32 = {
    0xe436eb7e, 0x524f, 0x11ce, {0x9f, 0x53, 0x00, 0x20, 0xaf, 0x0b, 0xa7, 0x70}
};
static const GUID MEDIASUBTYPE_NV12 = {
    0x3231564e, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}
};
static const GUID MEDIASUBTYPE_YUY2 = {
    0x32595559, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}
};
static const GUID FORMAT_VideoInfo = {
    0x05589f80, 0xc356, 0x11ce, {0xbf, 0x01, 0x00, 0xaa, 0x00, 0x55, 0x59, 0x5a}
};
static const GUID FORMAT_WaveFormatEx = {
    0x05589f81, 0xc356, 0x11ce, {0xbf, 0x01, 0x00, 0xaa, 0x00, 0x55, 0x59, 0x5a}
};
static const GUID AMPROPSETID_Pin = {
    0x9b00f101, 0x1567, 0x11d1, {0xb3, 0xf1, 0x00, 0xaa, 0x00, 0x37, 0x61, 0xc5}
};
static const GUID PIN_CATEGORY_CAPTURE = {
    0xfb6c4281, 0x0353, 0x11d1, {0x90, 0x5f, 0x00, 0x00, 0xc0, 0xcc, 0x16, 0xba}
};

/* COM Vtable Definitions */
typedef struct IUnknownVtbl {
    HRESULT (WINAPI *QueryInterface)(IUnknown *iface, REFIID riid, void **ppvObject);
    ULONG   (WINAPI *AddRef)(IUnknown *iface);
    ULONG   (WINAPI *Release)(IUnknown *iface);
} IUnknownVtbl;

struct IUnknown {
    const IUnknownVtbl *lpVtbl;
};

typedef struct IClassFactoryVtbl {
    HRESULT (WINAPI *QueryInterface)(IClassFactory *iface, REFIID riid, void **ppvObject);
    ULONG   (WINAPI *AddRef)(IClassFactory *iface);
    ULONG   (WINAPI *Release)(IClassFactory *iface);
    HRESULT (WINAPI *CreateInstance)(IClassFactory *iface, IUnknown *pUnkOuter, REFIID riid, void **ppvObject);
    HRESULT (WINAPI *LockServer)(IClassFactory *iface, BOOL fLock);
} IClassFactoryVtbl;

struct IClassFactory {
    const IClassFactoryVtbl *lpVtbl;
};

typedef struct IPropertyBagVtbl {
    HRESULT (WINAPI *QueryInterface)(IPropertyBag *iface, REFIID riid, void **ppvObject);
    ULONG   (WINAPI *AddRef)(IPropertyBag *iface);
    ULONG   (WINAPI *Release)(IPropertyBag *iface);
    HRESULT (WINAPI *Read)(IPropertyBag *iface, LPCWSTR pszPropName, VARIANT *pVar, IErrorLog *pErrorLog);
    HRESULT (WINAPI *Write)(IPropertyBag *iface, LPCWSTR pszPropName, VARIANT *pVar);
} IPropertyBagVtbl;

struct IPropertyBag {
    const IPropertyBagVtbl *lpVtbl;
};

typedef struct IPersistPropertyBagVtbl {
    HRESULT (WINAPI *QueryInterface)(IPersistPropertyBag *iface, REFIID riid, void **ppvObject);
    ULONG   (WINAPI *AddRef)(IPersistPropertyBag *iface);
    ULONG   (WINAPI *Release)(IPersistPropertyBag *iface);
    HRESULT (WINAPI *GetClassID)(IPersistPropertyBag *iface, CLSID *pClassID);
    HRESULT (WINAPI *InitNew)(IPersistPropertyBag *iface);
    HRESULT (WINAPI *Load)(IPersistPropertyBag *iface, IPropertyBag *pPropBag, IErrorLog *pErrorLog);
    HRESULT (WINAPI *Save)(IPersistPropertyBag *iface, IPropertyBag *pPropBag, BOOL fClearDirty, BOOL fSaveAllProperties);
} IPersistPropertyBagVtbl;

struct IPersistPropertyBag {
    const IPersistPropertyBagVtbl *lpVtbl;
};

typedef struct IBaseFilterVtbl {
    HRESULT (WINAPI *QueryInterface)(IBaseFilter *iface, REFIID riid, void **ppvObject);
    ULONG   (WINAPI *AddRef)(IBaseFilter *iface);
    ULONG   (WINAPI *Release)(IBaseFilter *iface);
    HRESULT (WINAPI *GetClassID)(IBaseFilter *iface, CLSID *pClassID);
    HRESULT (WINAPI *Stop)(IBaseFilter *iface);
    HRESULT (WINAPI *Pause)(IBaseFilter *iface);
    HRESULT (WINAPI *Run)(IBaseFilter *iface, REFERENCE_TIME tStart);
    HRESULT (WINAPI *GetState)(IBaseFilter *iface, DWORD dwMilliSecsTimeout, FILTER_STATE *State);
    HRESULT (WINAPI *SetSyncSource)(IBaseFilter *iface, IReferenceClock *pClock);
    HRESULT (WINAPI *GetSyncSource)(IBaseFilter *iface, IReferenceClock **pClock);
    HRESULT (WINAPI *EnumPins)(IBaseFilter *iface, IEnumPins **ppEnum);
    HRESULT (WINAPI *FindPin)(IBaseFilter *iface, LPCWSTR Id, IPin **ppPin);
    HRESULT (WINAPI *QueryFilterInfo)(IBaseFilter *iface, FILTER_INFO *pInfo);
    HRESULT (WINAPI *JoinFilterGraph)(IBaseFilter *iface, IFilterGraph *pGraph, LPCWSTR pName);
    HRESULT (WINAPI *QueryVendorInfo)(IBaseFilter *iface, LPWSTR *pVendorInfo);
} IBaseFilterVtbl;

struct IBaseFilter {
    const IBaseFilterVtbl *lpVtbl;
};

typedef struct IPinVtbl {
    HRESULT (WINAPI *QueryInterface)(IPin *iface, REFIID riid, void **ppvObject);
    ULONG   (WINAPI *AddRef)(IPin *iface);
    ULONG   (WINAPI *Release)(IPin *iface);
    HRESULT (WINAPI *Connect)(IPin *iface, IPin *pReceivePin, const AM_MEDIA_TYPE *pmt);
    HRESULT (WINAPI *ReceiveConnection)(IPin *iface, IPin *pConnector, const AM_MEDIA_TYPE *pmt);
    HRESULT (WINAPI *Disconnect)(IPin *iface);
    HRESULT (WINAPI *ConnectedTo)(IPin *iface, IPin **pPin);
    HRESULT (WINAPI *ConnectionMediaType)(IPin *iface, AM_MEDIA_TYPE *pmt);
    HRESULT (WINAPI *QueryPinInfo)(IPin *iface, PIN_INFO *pInfo);
    HRESULT (WINAPI *QueryDirection)(IPin *iface, PIN_DIRECTION *pPinDir);
    HRESULT (WINAPI *QueryId)(IPin *iface, LPWSTR *Id);
    HRESULT (WINAPI *QueryAccept)(IPin *iface, const AM_MEDIA_TYPE *pmt);
    HRESULT (WINAPI *EnumMediaTypes)(IPin *iface, IEnumMediaTypes **ppEnum);
    HRESULT (WINAPI *QueryInternalConnections)(IPin *iface, IPin **apPin, ULONG *nPin);
    HRESULT (WINAPI *EndOfStream)(IPin *iface);
    HRESULT (WINAPI *BeginFlush)(IPin *iface);
    HRESULT (WINAPI *EndFlush)(IPin *iface);
    HRESULT (WINAPI *NewSegment)(IPin *iface, REFERENCE_TIME tStart, REFERENCE_TIME tStop, double dRate);
} IPinVtbl;

struct IPin {
    const IPinVtbl *lpVtbl;
};

typedef struct IEnumPinsVtbl {
    HRESULT (WINAPI *QueryInterface)(IEnumPins *iface, REFIID riid, void **ppvObject);
    ULONG   (WINAPI *AddRef)(IEnumPins *iface);
    ULONG   (WINAPI *Release)(IEnumPins *iface);
    HRESULT (WINAPI *Next)(IEnumPins *iface, ULONG cPins, IPin **ppPins, ULONG *pcFetched);
    HRESULT (WINAPI *Skip)(IEnumPins *iface, ULONG cPins);
    HRESULT (WINAPI *Reset)(IEnumPins *iface);
    HRESULT (WINAPI *Clone)(IEnumPins *iface, IEnumPins **ppEnum);
} IEnumPinsVtbl;

struct IEnumPins {
    const IEnumPinsVtbl *lpVtbl;
};

typedef struct IEnumMediaTypesVtbl {
    HRESULT (WINAPI *QueryInterface)(IEnumMediaTypes *iface, REFIID riid, void **ppvObject);
    ULONG   (WINAPI *AddRef)(IEnumMediaTypes *iface);
    ULONG   (WINAPI *Release)(IEnumMediaTypes *iface);
    HRESULT (WINAPI *Next)(IEnumMediaTypes *iface, ULONG cMediaTypes, AM_MEDIA_TYPE **ppMediaTypes, ULONG *pcFetched);
    HRESULT (WINAPI *Skip)(IEnumMediaTypes *iface, ULONG cMediaTypes);
    HRESULT (WINAPI *Reset)(IEnumMediaTypes *iface);
    HRESULT (WINAPI *Clone)(IEnumMediaTypes *iface, IEnumMediaTypes **ppEnum);
} IEnumMediaTypesVtbl;

struct IEnumMediaTypes {
    const IEnumMediaTypesVtbl *lpVtbl;
};

typedef struct IAMFilterMiscFlagsVtbl {
    HRESULT (WINAPI *QueryInterface)(IAMFilterMiscFlags *iface, REFIID riid, void **ppvObject);
    ULONG   (WINAPI *AddRef)(IAMFilterMiscFlags *iface);
    ULONG   (WINAPI *Release)(IAMFilterMiscFlags *iface);
    ULONG   (WINAPI *GetMiscFlags)(IAMFilterMiscFlags *iface);
} IAMFilterMiscFlagsVtbl;

struct IAMFilterMiscFlags {
    const IAMFilterMiscFlagsVtbl *lpVtbl;
};

typedef struct IAMStreamConfigVtbl {
    HRESULT (WINAPI *QueryInterface)(IAMStreamConfig *iface, REFIID riid, void **ppvObject);
    ULONG   (WINAPI *AddRef)(IAMStreamConfig *iface);
    ULONG   (WINAPI *Release)(IAMStreamConfig *iface);
    HRESULT (WINAPI *SetFormat)(IAMStreamConfig *iface, AM_MEDIA_TYPE *pmt);
    HRESULT (WINAPI *GetFormat)(IAMStreamConfig *iface, AM_MEDIA_TYPE **ppmt);
    HRESULT (WINAPI *GetNumberOfCapabilities)(IAMStreamConfig *iface, int *piCount, int *piSize);
    HRESULT (WINAPI *GetStreamCaps)(IAMStreamConfig *iface, int iIndex, AM_MEDIA_TYPE **ppmt, BYTE *pSCC);
} IAMStreamConfigVtbl;

struct IAMStreamConfig {
    const IAMStreamConfigVtbl *lpVtbl;
};

typedef struct IAMBufferNegotiationVtbl {
    HRESULT (WINAPI *QueryInterface)(IAMBufferNegotiation *iface, REFIID riid, void **ppvObject);
    ULONG   (WINAPI *AddRef)(IAMBufferNegotiation *iface);
    ULONG   (WINAPI *Release)(IAMBufferNegotiation *iface);
    HRESULT (WINAPI *SuggestAllocatorProperties)(IAMBufferNegotiation *iface, const ALLOCATOR_PROPERTIES *pprop);
    HRESULT (WINAPI *GetAllocatorProperties)(IAMBufferNegotiation *iface, ALLOCATOR_PROPERTIES *pprop);
} IAMBufferNegotiationVtbl;

struct IAMBufferNegotiation {
    const IAMBufferNegotiationVtbl *lpVtbl;
};

typedef struct IKsPropertySetVtbl {
    HRESULT (WINAPI *QueryInterface)(IKsPropertySet *iface, REFIID riid, void **ppvObject);
    ULONG   (WINAPI *AddRef)(IKsPropertySet *iface);
    ULONG   (WINAPI *Release)(IKsPropertySet *iface);
    HRESULT (WINAPI *Set)(IKsPropertySet *iface, REFGUID guidPropSet, DWORD dwPropID, LPVOID pInstanceData, DWORD cbInstanceData, LPVOID pPropData, DWORD cbPropData);
    HRESULT (WINAPI *Get)(IKsPropertySet *iface, REFGUID guidPropSet, DWORD dwPropID, LPVOID pInstanceData, DWORD cbInstanceData, LPVOID pPropData, DWORD cbPropData, DWORD *pcbReturned);
    HRESULT (WINAPI *QuerySupported)(IKsPropertySet *iface, REFGUID guidPropSet, DWORD dwPropID, DWORD *pTypeSupport);
} IKsPropertySetVtbl;

struct IKsPropertySet {
    const IKsPropertySetVtbl *lpVtbl;
};

typedef struct IQualityControlVtbl {
    HRESULT (WINAPI *QueryInterface)(IQualityControl *iface, REFIID riid, void **ppvObject);
    ULONG   (WINAPI *AddRef)(IQualityControl *iface);
    ULONG   (WINAPI *Release)(IQualityControl *iface);
    HRESULT (WINAPI *Notify)(IQualityControl *iface, IBaseFilter *pSelf, Quality q);
    HRESULT (WINAPI *SetSink)(IQualityControl *iface, IQualityControl *piqc);
} IQualityControlVtbl;

struct IQualityControl {
    const IQualityControlVtbl *lpVtbl;
};

typedef struct IAMVideoControlVtbl {
    HRESULT (WINAPI *QueryInterface)(IAMVideoControl *iface, REFIID riid, void **ppvObject);
    ULONG   (WINAPI *AddRef)(IAMVideoControl *iface);
    ULONG   (WINAPI *Release)(IAMVideoControl *iface);
    HRESULT (WINAPI *GetCaps)(IAMVideoControl *iface, IPin *pPin, LONG *pCapsFlags);
    HRESULT (WINAPI *SetMode)(IAMVideoControl *iface, IPin *pPin, LONG Mode);
    HRESULT (WINAPI *GetMode)(IAMVideoControl *iface, IPin *pPin, LONG *Mode);
    HRESULT (WINAPI *GetCurrentActualFrameRate)(IAMVideoControl *iface, IPin *pPin, LONGLONG *ActualFrameRate);
    HRESULT (WINAPI *GetMaxAvailableFrameRate)(IAMVideoControl *iface, IPin *pPin, LONG iIndex, SIZE Dimensions, LONGLONG *MaxAvailableFrameRate);
    HRESULT (WINAPI *GetFrameRateList)(IAMVideoControl *iface, IPin *pPin, LONG iIndex, SIZE Dimensions, LONG *ListSize, LONGLONG **FrameRates);
} IAMVideoControlVtbl;

struct IAMVideoControl {
    const IAMVideoControlVtbl *lpVtbl;
};

typedef struct IAMVideoProcAmpVtbl {
    HRESULT (WINAPI *QueryInterface)(IAMVideoProcAmp *iface, REFIID riid, void **ppvObject);
    ULONG   (WINAPI *AddRef)(IAMVideoProcAmp *iface);
    ULONG   (WINAPI *Release)(IAMVideoProcAmp *iface);
    HRESULT (WINAPI *GetRange)(IAMVideoProcAmp *iface, LONG Property, LONG *pMin, LONG *pMax, LONG *pSteppingDelta, LONG *pDefault, LONG *pCapsFlags);
    HRESULT (WINAPI *Set)(IAMVideoProcAmp *iface, LONG Property, LONG lValue, LONG Flags);
    HRESULT (WINAPI *Get)(IAMVideoProcAmp *iface, LONG Property, LONG *lValue, LONG *Flags);
} IAMVideoProcAmpVtbl;

struct IAMVideoProcAmp {
    const IAMVideoProcAmpVtbl *lpVtbl;
};

typedef struct IAMDroppedFramesVtbl {
    HRESULT (WINAPI *QueryInterface)(IAMDroppedFrames *iface, REFIID riid, void **ppvObject);
    ULONG   (WINAPI *AddRef)(IAMDroppedFrames *iface);
    ULONG   (WINAPI *Release)(IAMDroppedFrames *iface);
    HRESULT (WINAPI *GetNumDropped)(IAMDroppedFrames *iface, LONG *plDropped);
    HRESULT (WINAPI *GetNumNotDropped)(IAMDroppedFrames *iface, LONG *plNotDropped);
    HRESULT (WINAPI *GetAverageFrameSize)(IAMDroppedFrames *iface, LONG *plAverageSize);
    HRESULT (WINAPI *GetDroppedInfo)(IAMDroppedFrames *iface, LONG lSize, LONG *plArray, LONG *plNumCopied);
} IAMDroppedFramesVtbl;

struct IAMDroppedFrames {
    const IAMDroppedFramesVtbl *lpVtbl;
};

typedef struct IMediaSampleVtbl {
    HRESULT (WINAPI *QueryInterface)(IMediaSample *iface, REFIID riid, void **ppvObject);
    ULONG   (WINAPI *AddRef)(IMediaSample *iface);
    ULONG   (WINAPI *Release)(IMediaSample *iface);
    HRESULT (WINAPI *GetPointer)(IMediaSample *iface, BYTE **ppBuffer);
    LONG    (WINAPI *GetSize)(IMediaSample *iface);
    HRESULT (WINAPI *GetTime)(IMediaSample *iface, REFERENCE_TIME *pTimeStart, REFERENCE_TIME *pTimeEnd);
    HRESULT (WINAPI *SetTime)(IMediaSample *iface, REFERENCE_TIME *pTimeStart, REFERENCE_TIME *pTimeEnd);
    HRESULT (WINAPI *IsSyncPoint)(IMediaSample *iface);
    HRESULT (WINAPI *SetSyncPoint)(IMediaSample *iface, BOOL bIsSyncPoint);
    HRESULT (WINAPI *IsPreroll)(IMediaSample *iface);
    HRESULT (WINAPI *SetPreroll)(IMediaSample *iface, BOOL bIsPreroll);
    LONG    (WINAPI *GetActualDataLength)(IMediaSample *iface);
    HRESULT (WINAPI *SetActualDataLength)(IMediaSample *iface, LONG lLen);
    HRESULT (WINAPI *GetMediaType)(IMediaSample *iface, AM_MEDIA_TYPE **ppMediaType);
    HRESULT (WINAPI *SetMediaType)(IMediaSample *iface, AM_MEDIA_TYPE *pMediaType);
    HRESULT (WINAPI *IsDiscontinuity)(IMediaSample *iface);
    HRESULT (WINAPI *SetDiscontinuity)(IMediaSample *iface, BOOL bDiscontinuity);
    HRESULT (WINAPI *GetMediaTime)(IMediaSample *iface, LONGLONG *pTimeStart, LONGLONG *pTimeEnd);
    HRESULT (WINAPI *SetMediaTime)(IMediaSample *iface, LONGLONG *pTimeStart, LONGLONG *pTimeEnd);
} IMediaSampleVtbl;

struct IMediaSample {
    const IMediaSampleVtbl *lpVtbl;
};

typedef struct IMemAllocatorVtbl {
    HRESULT (WINAPI *QueryInterface)(IMemAllocator *iface, REFIID riid, void **ppvObject);
    ULONG   (WINAPI *AddRef)(IMemAllocator *iface);
    ULONG   (WINAPI *Release)(IMemAllocator *iface);
    HRESULT (WINAPI *SetProperties)(IMemAllocator *iface, ALLOCATOR_PROPERTIES *pRequest, ALLOCATOR_PROPERTIES *pActual);
    HRESULT (WINAPI *GetProperties)(IMemAllocator *iface, ALLOCATOR_PROPERTIES *pProps);
    HRESULT (WINAPI *Commit)(IMemAllocator *iface);
    HRESULT (WINAPI *Decommit)(IMemAllocator *iface);
    HRESULT (WINAPI *GetBuffer)(IMemAllocator *iface, IMediaSample **ppBuffer, REFERENCE_TIME *pStartTime, REFERENCE_TIME *pEndTime, DWORD dwFlags);
    HRESULT (WINAPI *ReleaseBuffer)(IMemAllocator *iface, IMediaSample *pBuffer);
} IMemAllocatorVtbl;

struct IMemAllocator {
    const IMemAllocatorVtbl *lpVtbl;
};

typedef struct IMemInputPinVtbl {
    HRESULT (WINAPI *QueryInterface)(IMemInputPin *iface, REFIID riid, void **ppvObject);
    ULONG   (WINAPI *AddRef)(IMemInputPin *iface);
    ULONG   (WINAPI *Release)(IMemInputPin *iface);
    HRESULT (WINAPI *GetAllocator)(IMemInputPin *iface, IMemAllocator **ppAllocator);
    HRESULT (WINAPI *NotifyAllocator)(IMemInputPin *iface, IMemAllocator *pAllocator, BOOL bReadOnly);
    HRESULT (WINAPI *GetAllocatorRequirements)(IMemInputPin *iface, ALLOCATOR_PROPERTIES *pProps);
    HRESULT (WINAPI *Receive)(IMemInputPin *iface, IMediaSample *pSample);
    HRESULT (WINAPI *ReceiveMultiple)(IMemInputPin *iface, IMediaSample **pSamples, LONG nSamples, LONG *nSamplesProcessed);
    HRESULT (WINAPI *ReceiveCanBlock)(IMemInputPin *iface);
} IMemInputPinVtbl;

struct IMemInputPin {
    const IMemInputPinVtbl *lpVtbl;
};

/* C Object Helper Macros */
#define IUnknown_QueryInterface(p, a, b) ((p)->lpVtbl->QueryInterface(p, a, b))
#define IUnknown_AddRef(p)               ((p)->lpVtbl->AddRef(p))
#define IUnknown_Release(p)              ((p)->lpVtbl->Release(p))

#define IClassFactory_QueryInterface(p, a, b)    ((p)->lpVtbl->QueryInterface(p, a, b))
#define IClassFactory_AddRef(p)                  ((p)->lpVtbl->AddRef(p))
#define IClassFactory_Release(p)                 ((p)->lpVtbl->Release(p))
#define IClassFactory_CreateInstance(p, a, b, c) ((p)->lpVtbl->CreateInstance(p, a, b, c))
#define IClassFactory_LockServer(p, a)           ((p)->lpVtbl->LockServer(p, a))

#define IPersistPropertyBag_QueryInterface(p, a, b) ((p)->lpVtbl->QueryInterface(p, a, b))
#define IPersistPropertyBag_AddRef(p)               ((p)->lpVtbl->AddRef(p))
#define IPersistPropertyBag_Release(p)              ((p)->lpVtbl->Release(p))
#define IPersistPropertyBag_GetClassID(p, a)        ((p)->lpVtbl->GetClassID(p, a))
#define IPersistPropertyBag_InitNew(p)              ((p)->lpVtbl->InitNew(p))
#define IPersistPropertyBag_Load(p, a, b)           ((p)->lpVtbl->Load(p, a, b))
#define IPersistPropertyBag_Save(p, a, b, c)        ((p)->lpVtbl->Save(p, a, b, c))

#define IBaseFilter_QueryInterface(p, a, b) ((p)->lpVtbl->QueryInterface(p, a, b))
#define IBaseFilter_AddRef(p)               ((p)->lpVtbl->AddRef(p))
#define IBaseFilter_Release(p)              ((p)->lpVtbl->Release(p))
#define IBaseFilter_GetClassID(p, a)        ((p)->lpVtbl->GetClassID(p, a))
#define IBaseFilter_Stop(p)                 ((p)->lpVtbl->Stop(p))
#define IBaseFilter_Pause(p)                ((p)->lpVtbl->Pause(p))
#define IBaseFilter_Run(p, a)               ((p)->lpVtbl->Run(p, a))
#define IBaseFilter_GetState(p, a, b)       ((p)->lpVtbl->GetState(p, a, b))
#define IBaseFilter_SetSyncSource(p, a)     ((p)->lpVtbl->SetSyncSource(p, a))
#define IBaseFilter_GetSyncSource(p, a)     ((p)->lpVtbl->GetSyncSource(p, a))
#define IBaseFilter_EnumPins(p, a)          ((p)->lpVtbl->EnumPins(p, a))
#define IBaseFilter_FindPin(p, a, b)        ((p)->lpVtbl->FindPin(p, a, b))
#define IBaseFilter_QueryFilterInfo(p, a)   ((p)->lpVtbl->QueryFilterInfo(p, a))
#define IBaseFilter_JoinFilterGraph(p, a, b)((p)->lpVtbl->JoinFilterGraph(p, a, b))
#define IBaseFilter_QueryVendorInfo(p, a)   ((p)->lpVtbl->QueryVendorInfo(p, a))

#define IPin_QueryInterface(p, a, b)           ((p)->lpVtbl->QueryInterface(p, a, b))
#define IPin_AddRef(p)                         ((p)->lpVtbl->AddRef(p))
#define IPin_Release(p)                        ((p)->lpVtbl->Release(p))
#define IPin_Connect(p, a, b)                  ((p)->lpVtbl->Connect(p, a, b))
#define IPin_ReceiveConnection(p, a, b)        ((p)->lpVtbl->ReceiveConnection(p, a, b))
#define IPin_Disconnect(p)                     ((p)->lpVtbl->Disconnect(p))
#define IPin_ConnectedTo(p, a)                 ((p)->lpVtbl->ConnectedTo(p, a))
#define IPin_ConnectionMediaType(p, a)         ((p)->lpVtbl->ConnectionMediaType(p, a))
#define IPin_QueryPinInfo(p, a)                ((p)->lpVtbl->QueryPinInfo(p, a))
#define IPin_QueryDirection(p, a)              ((p)->lpVtbl->QueryDirection(p, a))
#define IPin_QueryId(p, a)                     ((p)->lpVtbl->QueryId(p, a))
#define IPin_QueryAccept(p, a)                 ((p)->lpVtbl->QueryAccept(p, a))
#define IPin_EnumMediaTypes(p, a)              ((p)->lpVtbl->EnumMediaTypes(p, a))
#define IPin_QueryInternalConnections(p, a, b) ((p)->lpVtbl->QueryInternalConnections(p, a, b))
#define IPin_EndOfStream(p)                    ((p)->lpVtbl->EndOfStream(p))
#define IPin_BeginFlush(p)                     ((p)->lpVtbl->BeginFlush(p))
#define IPin_EndFlush(p)                       ((p)->lpVtbl->EndFlush(p))
#define IPin_NewSegment(p, a, b, c)            ((p)->lpVtbl->NewSegment(p, a, b, c))

#define IEnumPins_Release(p)          ((p)->lpVtbl->Release(p))
#define IEnumPins_Next(p, a, b, c)    ((p)->lpVtbl->Next(p, a, b, c))
#define IEnumPins_Skip(p, a)          ((p)->lpVtbl->Skip(p, a))
#define IEnumPins_Reset(p)            ((p)->lpVtbl->Reset(p))
#define IEnumPins_Clone(p, a)         ((p)->lpVtbl->Clone(p, a))

#define IEnumMediaTypes_Release(p)       ((p)->lpVtbl->Release(p))
#define IEnumMediaTypes_Next(p, a, b, c) ((p)->lpVtbl->Next(p, a, b, c))
#define IEnumMediaTypes_Skip(p, a)       ((p)->lpVtbl->Skip(p, a))
#define IEnumMediaTypes_Reset(p)         ((p)->lpVtbl->Reset(p))
#define IEnumMediaTypes_Clone(p, a)      ((p)->lpVtbl->Clone(p, a))

#define IAMFilterMiscFlags_Release(p)      ((p)->lpVtbl->Release(p))
#define IAMFilterMiscFlags_GetMiscFlags(p) ((p)->lpVtbl->GetMiscFlags(p))

#define IAMStreamConfig_Release(p)                    ((p)->lpVtbl->Release(p))
#define IAMStreamConfig_SetFormat(p, a)               ((p)->lpVtbl->SetFormat(p, a))
#define IAMStreamConfig_GetFormat(p, a)               ((p)->lpVtbl->GetFormat(p, a))
#define IAMStreamConfig_GetNumberOfCapabilities(p,a,b)((p)->lpVtbl->GetNumberOfCapabilities(p, a, b))
#define IAMStreamConfig_GetStreamCaps(p, a, b, c)     ((p)->lpVtbl->GetStreamCaps(p, a, b, c))

#define IAMBufferNegotiation_Release(p)                       ((p)->lpVtbl->Release(p))
#define IAMBufferNegotiation_SuggestAllocatorProperties(p, a) ((p)->lpVtbl->SuggestAllocatorProperties(p, a))
#define IAMBufferNegotiation_GetAllocatorProperties(p, a)     ((p)->lpVtbl->GetAllocatorProperties(p, a))

#define IKsPropertySet_Release(p)                       ((p)->lpVtbl->Release(p))
#define IKsPropertySet_Set(p, a, b, c, d, e, f)         ((p)->lpVtbl->Set(p, a, b, c, d, e, f))
#define IKsPropertySet_Get(p, a, b, c, d, e, f, g)      ((p)->lpVtbl->Get(p, a, b, c, d, e, f, g))
#define IKsPropertySet_QuerySupported(p, a, b, c)       ((p)->lpVtbl->QuerySupported(p, a, b, c))

#define IAMVideoControl_Release(p)                           ((p)->lpVtbl->Release(p))
#define IAMVideoControl_GetCaps(p, a, b)                     ((p)->lpVtbl->GetCaps(p, a, b))
#define IAMVideoControl_SetMode(p, a, b)                     ((p)->lpVtbl->SetMode(p, a, b))
#define IAMVideoControl_GetMode(p, a, b)                     ((p)->lpVtbl->GetMode(p, a, b))
#define IAMVideoControl_GetCurrentActualFrameRate(p, a, b)   ((p)->lpVtbl->GetCurrentActualFrameRate(p, a, b))
#define IAMVideoControl_GetMaxAvailableFrameRate(p,a,b,c,d)  ((p)->lpVtbl->GetMaxAvailableFrameRate(p, a, b, c, d))
#define IAMVideoControl_GetFrameRateList(p, a, b, c, d, e)   ((p)->lpVtbl->GetFrameRateList(p, a, b, c, d, e))

#define IAMVideoProcAmp_Release(p)                        ((p)->lpVtbl->Release(p))
#define IAMVideoProcAmp_GetRange(p, a, b, c, d, e, f)     ((p)->lpVtbl->GetRange(p, a, b, c, d, e, f))
#define IAMVideoProcAmp_Set(p, a, b, c)                   ((p)->lpVtbl->Set(p, a, b, c))
#define IAMVideoProcAmp_Get(p, a, b, c)                   ((p)->lpVtbl->Get(p, a, b, c))

#define IAMDroppedFrames_Release(p)                       ((p)->lpVtbl->Release(p))
#define IAMDroppedFrames_GetNumDropped(p, a)              ((p)->lpVtbl->GetNumDropped(p, a))
#define IAMDroppedFrames_GetNumNotDropped(p, a)           ((p)->lpVtbl->GetNumNotDropped(p, a))
#define IAMDroppedFrames_GetAverageFrameSize(p, a)        ((p)->lpVtbl->GetAverageFrameSize(p, a))
#define IAMDroppedFrames_GetDroppedInfo(p, a, b, c)       ((p)->lpVtbl->GetDroppedInfo(p, a, b, c))

/* Opaque Handle Types for AVFoundation Video Capture & mfreadwrite Bridge */
typedef void *QCAP_AVF_DEVICE;
typedef struct _QCAP_MF_SOURCE_READER QCAP_MF_SOURCE_READER;

/* qcap.dll Exported & Internal APIs */
HRESULT WINAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, void **ppv);
HRESULT WINAPI DllCanUnloadNow(void);
HRESULT WINAPI DllRegisterServer(void);
HRESULT WINAPI DllUnregisterServer(void);

HRESULT WINAPI audio_record_create(IUnknown *outer, IUnknown **out);
HRESULT WINAPI vfw_capture_create(IUnknown *outer, IUnknown **out);

/* macOS AVFoundation Video Capture Backend APIs */
HRESULT WINAPI qcap_avf_create(int device_index, QCAP_AVF_DEVICE *out_dev);
void    WINAPI qcap_avf_destroy(QCAP_AVF_DEVICE dev);
BOOL    WINAPI qcap_avf_is_hardware_available(QCAP_AVF_DEVICE dev);
int     WINAPI qcap_avf_get_caps_count(QCAP_AVF_DEVICE dev);
HRESULT WINAPI qcap_avf_get_caps(
    QCAP_AVF_DEVICE dev,
    int index,
    AM_MEDIA_TYPE *mt,
    VIDEO_STREAM_CONFIG_CAPS *caps);
HRESULT WINAPI qcap_avf_check_format(QCAP_AVF_DEVICE dev, const AM_MEDIA_TYPE *mt);
HRESULT WINAPI qcap_avf_set_format(QCAP_AVF_DEVICE dev, const AM_MEDIA_TYPE *mt);
HRESULT WINAPI qcap_avf_get_format(QCAP_AVF_DEVICE dev, AM_MEDIA_TYPE *mt);
HRESULT WINAPI qcap_avf_get_media_type(QCAP_AVF_DEVICE dev, unsigned int index, AM_MEDIA_TYPE *mt);
HRESULT WINAPI qcap_avf_get_frame_intervals(
    QCAP_AVF_DEVICE dev,
    LONG width,
    LONG height,
    const GUID *subtype,
    LONGLONG **intervals,
    LONG *count);
HRESULT WINAPI qcap_avf_get_prop_range(
    QCAP_AVF_DEVICE dev,
    LONG prop,
    LONG *min_val,
    LONG *max_val,
    LONG *step,
    LONG *def_val,
    LONG *flags);
HRESULT WINAPI qcap_avf_get_prop(QCAP_AVF_DEVICE dev, LONG prop, LONG *val, LONG *flags);
HRESULT WINAPI qcap_avf_set_prop(QCAP_AVF_DEVICE dev, LONG prop, LONG val, LONG flags);
HRESULT WINAPI qcap_avf_read_frame(
    QCAP_AVF_DEVICE dev,
    BYTE *buffer,
    DWORD buffer_size,
    DWORD *bytes_written,
    BOOL *is_bottom_up);

/* Media Foundation (mfreadwrite) Capture SourceReader Bridge */
HRESULT WINAPI QcapCreateAVFoundationReader(UINT32 device_index, QCAP_MF_SOURCE_READER **out_reader);
void    WINAPI QcapDestroyAVFoundationReader(QCAP_MF_SOURCE_READER *reader);
HRESULT WINAPI QcapAVFoundationReaderGetStreamFormat(
    QCAP_MF_SOURCE_READER *reader,
    DWORD stream_index,
    GUID *major_type,
    GUID *subtype,
    UINT32 *width_or_channels,
    UINT32 *height_or_sample_rate,
    UINT32 *bits_per_sample);
HRESULT WINAPI QcapAVFoundationReaderSetStreamFormat(
    QCAP_MF_SOURCE_READER *reader,
    DWORD stream_index,
    const GUID *subtype,
    UINT32 width_or_channels,
    UINT32 height_or_sample_rate,
    UINT32 bits_per_sample);
HRESULT WINAPI QcapAVFoundationReaderReadSample(
    QCAP_MF_SOURCE_READER *reader,
    DWORD stream_index,
    DWORD *actual_stream_index,
    DWORD *stream_flags,
    LONGLONG *timestamp,
    BYTE *sample_buf,
    DWORD sample_buf_capacity,
    DWORD *sample_bytes_written);

/* Helper to free AM_MEDIA_TYPE allocated by GetFormat / GetStreamCaps / ConnectionMediaType */
static inline void qcap_free_media_type_contents(AM_MEDIA_TYPE *mt)
{
    if (!mt) {
        return;
    }
    if (mt->pbFormat) {
        CoTaskMemFree(mt->pbFormat);
        mt->pbFormat = NULL;
        mt->cbFormat = 0;
    }
    if (mt->pUnk) {
        IUnknown_Release(mt->pUnk);
        mt->pUnk = NULL;
    }
}

static inline void qcap_delete_media_type(AM_MEDIA_TYPE *mt)
{
    if (!mt) {
        return;
    }
    qcap_free_media_type_contents(mt);
    CoTaskMemFree(mt);
}

#endif /* _WINE_QCAP_H */

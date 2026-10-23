/*
 * Wine mfplat.dll Core Media Foundation & macOS VideoToolbox Header
 *
 * Clean-room declaration derived from Microsoft Learn public specifications
 * and upstream Wine include/mfapi.h & include/mfobjects.idl.
 *
 * Copyright 2026 Wine Parity Contributors
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef _WINE_MFPLAT_H
#define _WINE_MFPLAT_H

#include "../../tests/include/parity_test.h"

#ifndef _WIN32
typedef int32_t HRESULT;
typedef uint8_t UINT8;
typedef uint32_t UINT32;
typedef uint64_t UINT64;
typedef uint16_t VARTYPE;

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
typedef const GUID *REFGUID;
typedef const IID *REFIID;
typedef WCHAR *LPWSTR;

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

static inline void *mfplat_task_alloc(size_t cb)
{
    return malloc(cb ? cb : 1);
}

static inline void mfplat_task_free(void *pv)
{
    free(pv);
}
#define CoTaskMemAlloc mfplat_task_alloc
#define CoTaskMemFree  mfplat_task_free
#endif

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
#ifndef E_FAIL
#define E_FAIL ((HRESULT)0x80004005L)
#endif
#ifndef E_OUTOFMEMORY
#define E_OUTOFMEMORY ((HRESULT)0x8007000EL)
#endif
#ifndef E_INVALIDARG
#define E_INVALIDARG ((HRESULT)0x80070057L)
#endif
#ifndef E_NOT_SUFFICIENT_BUFFER
#define E_NOT_SUFFICIENT_BUFFER ((HRESULT)0x8007007AL)
#endif
#ifndef STRSAFE_E_INSUFFICIENT_BUFFER
#define STRSAFE_E_INSUFFICIENT_BUFFER ((HRESULT)0x8007007AL)
#endif

#define MF_E_PLATFORM_NOT_INITIALIZED ((HRESULT)0xC00D36B0L)
#define MF_E_BUFFERTOOSMALL           ((HRESULT)0xC00D36B1L)
#define MF_E_INVALIDMEDIATYPE         ((HRESULT)0xC00D36B4L)
#define MF_E_INVALIDTYPE              ((HRESULT)0xC00D36BDL)
#define MF_E_BAD_STARTUP_VERSION      ((HRESULT)0xC00D36E3L)
#define MF_E_ATTRIBUTENOTFOUND        ((HRESULT)0xC00D36E6L)
#define MF_E_SHUTDOWN                 ((HRESULT)0xC00D3E85L)
#define MF_E_TRANSFORM_TYPE_NOT_SET   ((HRESULT)0xC00D6D60L)
#define MF_E_TRANSFORM_NEED_MORE_INPUT ((HRESULT)0xC00D6D72L)

#define MF_SDK_VERSION   0x0002
#define MF_API_VERSION   0x0070
#define MF_VERSION       ((MF_SDK_VERSION << 16) | MF_API_VERSION)
#define MF_VERSION_XP    0x00010070UL
#define MF_VERSION_WIN7  0x00020070UL

#define MFSTARTUP_NOSOCKET 0x00000001UL
#define MFSTARTUP_LITE     MFSTARTUP_NOSOCKET
#define MFSTARTUP_FULL     0x00000000UL

#define MF_1_BYTE_ALIGNMENT   0x00000000UL
#define MF_2_BYTE_ALIGNMENT   0x00000001UL
#define MF_4_BYTE_ALIGNMENT   0x00000003UL
#define MF_8_BYTE_ALIGNMENT   0x00000007UL
#define MF_16_BYTE_ALIGNMENT  0x0000000fUL
#define MF_32_BYTE_ALIGNMENT  0x0000001fUL
#define MF_64_BYTE_ALIGNMENT  0x0000003fUL
#define MF_128_BYTE_ALIGNMENT 0x0000007fUL
#define MF_256_BYTE_ALIGNMENT 0x000000ffUL
#define MF_512_BYTE_ALIGNMENT 0x000001ffUL

#define MF_MEDIATYPE_EQUAL_MAJOR_TYPES      0x00000001UL
#define MF_MEDIATYPE_EQUAL_FORMAT_TYPES     0x00000002UL
#define MF_MEDIATYPE_EQUAL_FORMAT_DATA      0x00000004UL
#define MF_MEDIATYPE_EQUAL_FORMAT_USER_DATA 0x00000008UL

#ifndef MAKEFOURCC
#define MAKEFOURCC(ch0, ch1, ch2, ch3) \
    ((DWORD)(BYTE)(ch0) | ((DWORD)(BYTE)(ch1) << 8) | \
     ((DWORD)(BYTE)(ch2) << 16) | ((DWORD)(BYTE)(ch3) << 24))
#endif

#ifndef D3DFMT_UNKNOWN
#define D3DFMT_UNKNOWN   0U
#define D3DFMT_R8G8B8    20U
#define D3DFMT_A8R8G8B8  21U
#define D3DFMT_X8R8G8B8  22U
#define D3DFMT_NV12      MAKEFOURCC('N', 'V', '1', '2')
#define D3DFMT_YUY2      MAKEFOURCC('Y', 'U', 'Y', '2')
#define D3DFMT_P010      MAKEFOURCC('P', '0', '1', '0')
#define D3DFMT_H264      MAKEFOURCC('H', '2', '6', '4')
#define D3DFMT_HEVC      MAKEFOURCC('H', 'E', 'V', 'C')
#endif

#ifndef __IUnknown_INTERFACE_DEFINED__
#define __IUnknown_INTERFACE_DEFINED__
typedef struct IUnknown IUnknown;
typedef struct IUnknownVtbl {
    HRESULT (WINAPI *QueryInterface)(IUnknown *This, REFIID riid, void **ppvObject);
    ULONG   (WINAPI *AddRef)(IUnknown *This);
    ULONG   (WINAPI *Release)(IUnknown *This);
} IUnknownVtbl;
struct IUnknown {
    const IUnknownVtbl *lpVtbl;
};
#endif

#ifndef _PROPVARIANT_DEFINED
#define _PROPVARIANT_DEFINED
#define VT_EMPTY   0
#define VT_R8      5
#define VT_UNKNOWN 13
#define VT_UI1     17
#define VT_UI4     19
#define VT_UI8     21
#define VT_LPWSTR  31
#define VT_CLSID   72
#define VT_VECTOR  0x1000

typedef struct tagCAUB {
    ULONG cElems;
    UCHAR *pElems;
} CAUB;

typedef struct tagPROPVARIANT {
    VARTYPE vt;
    uint16_t wReserved1;
    uint16_t wReserved2;
    uint16_t wReserved3;
    union {
        ULONG ulVal;
        UINT64 uhVal;
        double dblVal;
        LPWSTR pwszVal;
        GUID *puuid;
        CAUB caub;
        IUnknown *punkVal;
    };
} PROPVARIANT;

static inline void PropVariantInit(PROPVARIANT *pvar)
{
    if (pvar) {
        memset(pvar, 0, sizeof(*pvar));
    }
}

static inline HRESULT PropVariantClear(PROPVARIANT *pvar)
{
    if (!pvar) {
        return E_INVALIDARG;
    }
    if (pvar->vt == VT_LPWSTR && pvar->pwszVal) {
        CoTaskMemFree(pvar->pwszVal);
    } else if (pvar->vt == VT_CLSID && pvar->puuid) {
        CoTaskMemFree(pvar->puuid);
    } else if (pvar->vt == (VT_VECTOR | VT_UI1) && pvar->caub.pElems) {
        CoTaskMemFree(pvar->caub.pElems);
    } else if (pvar->vt == VT_UNKNOWN && pvar->punkVal) {
        pvar->punkVal->lpVtbl->Release(pvar->punkVal);
    }
    memset(pvar, 0, sizeof(*pvar));
    return S_OK;
}
#endif

typedef enum _MF_ATTRIBUTE_TYPE {
    MF_ATTRIBUTE_UINT32   = VT_UI4,
    MF_ATTRIBUTE_UINT64   = VT_UI8,
    MF_ATTRIBUTE_DOUBLE   = VT_R8,
    MF_ATTRIBUTE_GUID     = VT_CLSID,
    MF_ATTRIBUTE_STRING   = VT_LPWSTR,
    MF_ATTRIBUTE_BLOB     = (VT_VECTOR | VT_UI1),
    MF_ATTRIBUTE_IUNKNOWN = VT_UNKNOWN
} MF_ATTRIBUTE_TYPE;

typedef enum _MF_ATTRIBUTES_MATCH_TYPE {
    MF_ATTRIBUTES_MATCH_OUR_ITEMS    = 0,
    MF_ATTRIBUTES_MATCH_THEIR_ITEMS  = 1,
    MF_ATTRIBUTES_MATCH_ALL_ITEMS    = 2,
    MF_ATTRIBUTES_MATCH_INTERSECTION = 3,
    MF_ATTRIBUTES_MATCH_SMALLER      = 4
} MF_ATTRIBUTES_MATCH_TYPE;

typedef struct IMFAttributes IMFAttributes;
typedef struct IMFMediaBuffer IMFMediaBuffer;
typedef struct IMFMediaType IMFMediaType;

typedef struct IMFAttributesVtbl {
    HRESULT (WINAPI *QueryInterface)(IMFAttributes *This, REFIID riid, void **ppvObject);
    ULONG   (WINAPI *AddRef)(IMFAttributes *This);
    ULONG   (WINAPI *Release)(IMFAttributes *This);
    HRESULT (WINAPI *GetItem)(IMFAttributes *This, REFGUID guidKey, PROPVARIANT *pValue);
    HRESULT (WINAPI *GetItemType)(IMFAttributes *This, REFGUID guidKey, MF_ATTRIBUTE_TYPE *pType);
    HRESULT (WINAPI *CompareItem)(IMFAttributes *This, REFGUID guidKey, const PROPVARIANT *Value, BOOL *pbResult);
    HRESULT (WINAPI *Compare)(IMFAttributes *This, IMFAttributes *pTheirs, MF_ATTRIBUTES_MATCH_TYPE MatchType, BOOL *pbResult);
    HRESULT (WINAPI *GetUINT32)(IMFAttributes *This, REFGUID guidKey, UINT32 *punValue);
    HRESULT (WINAPI *GetUINT64)(IMFAttributes *This, REFGUID guidKey, UINT64 *punValue);
    HRESULT (WINAPI *GetDouble)(IMFAttributes *This, REFGUID guidKey, double *pfValue);
    HRESULT (WINAPI *GetGUID)(IMFAttributes *This, REFGUID guidKey, GUID *pguidValue);
    HRESULT (WINAPI *GetStringLength)(IMFAttributes *This, REFGUID guidKey, UINT32 *pcchLength);
    HRESULT (WINAPI *GetString)(IMFAttributes *This, REFGUID guidKey, LPWSTR pwszValue, UINT32 cchBufSize, UINT32 *pcchLength);
    HRESULT (WINAPI *GetAllocatedString)(IMFAttributes *This, REFGUID guidKey, LPWSTR *ppwszValue, UINT32 *pcchLength);
    HRESULT (WINAPI *GetBlobSize)(IMFAttributes *This, REFGUID guidKey, UINT32 *pcbBlobSize);
    HRESULT (WINAPI *GetBlob)(IMFAttributes *This, REFGUID guidKey, UINT8 *pBuf, UINT32 cbBufSize, UINT32 *pcbBlobSize);
    HRESULT (WINAPI *GetAllocatedBlob)(IMFAttributes *This, REFGUID guidKey, UINT8 **ppBuf, UINT32 *pcbSize);
    HRESULT (WINAPI *GetUnknown)(IMFAttributes *This, REFGUID guidKey, REFIID riid, LPVOID *ppv);
    HRESULT (WINAPI *SetItem)(IMFAttributes *This, REFGUID guidKey, const PROPVARIANT *Value);
    HRESULT (WINAPI *DeleteItem)(IMFAttributes *This, REFGUID guidKey);
    HRESULT (WINAPI *DeleteAllItems)(IMFAttributes *This);
    HRESULT (WINAPI *SetUINT32)(IMFAttributes *This, REFGUID guidKey, UINT32 unValue);
    HRESULT (WINAPI *SetUINT64)(IMFAttributes *This, REFGUID guidKey, UINT64 unValue);
    HRESULT (WINAPI *SetDouble)(IMFAttributes *This, REFGUID guidKey, double fValue);
    HRESULT (WINAPI *SetGUID)(IMFAttributes *This, REFGUID guidKey, REFGUID guidValue);
    HRESULT (WINAPI *SetString)(IMFAttributes *This, REFGUID guidKey, LPCWSTR wszValue);
    HRESULT (WINAPI *SetBlob)(IMFAttributes *This, REFGUID guidKey, const UINT8 *pBuf, UINT32 cbBufSize);
    HRESULT (WINAPI *SetUnknown)(IMFAttributes *This, REFGUID guidKey, IUnknown *pUnknown);
    HRESULT (WINAPI *LockStore)(IMFAttributes *This);
    HRESULT (WINAPI *UnlockStore)(IMFAttributes *This);
    HRESULT (WINAPI *GetCount)(IMFAttributes *This, UINT32 *pcItems);
    HRESULT (WINAPI *GetItemByIndex)(IMFAttributes *This, UINT32 unIndex, GUID *pguidKey, PROPVARIANT *pValue);
    HRESULT (WINAPI *CopyAllItems)(IMFAttributes *This, IMFAttributes *pDest);
} IMFAttributesVtbl;

struct IMFAttributes {
    const IMFAttributesVtbl *lpVtbl;
};

typedef struct IMFMediaBufferVtbl {
    HRESULT (WINAPI *QueryInterface)(IMFMediaBuffer *This, REFIID riid, void **ppvObject);
    ULONG   (WINAPI *AddRef)(IMFMediaBuffer *This);
    ULONG   (WINAPI *Release)(IMFMediaBuffer *This);
    HRESULT (WINAPI *Lock)(IMFMediaBuffer *This, BYTE **ppbBuffer, DWORD *pcbMaxLength, DWORD *pcbCurrentLength);
    HRESULT (WINAPI *Unlock)(IMFMediaBuffer *This);
    HRESULT (WINAPI *GetCurrentLength)(IMFMediaBuffer *This, DWORD *pcbCurrentLength);
    HRESULT (WINAPI *SetCurrentLength)(IMFMediaBuffer *This, DWORD cbCurrentLength);
    HRESULT (WINAPI *GetMaxLength)(IMFMediaBuffer *This, DWORD *pcbMaxLength);
} IMFMediaBufferVtbl;

struct IMFMediaBuffer {
    const IMFMediaBufferVtbl *lpVtbl;
};

typedef struct IMFMediaTypeVtbl {
    HRESULT (WINAPI *QueryInterface)(IMFMediaType *This, REFIID riid, void **ppvObject);
    ULONG   (WINAPI *AddRef)(IMFMediaType *This);
    ULONG   (WINAPI *Release)(IMFMediaType *This);
    HRESULT (WINAPI *GetItem)(IMFMediaType *This, REFGUID guidKey, PROPVARIANT *pValue);
    HRESULT (WINAPI *GetItemType)(IMFMediaType *This, REFGUID guidKey, MF_ATTRIBUTE_TYPE *pType);
    HRESULT (WINAPI *CompareItem)(IMFMediaType *This, REFGUID guidKey, const PROPVARIANT *Value, BOOL *pbResult);
    HRESULT (WINAPI *Compare)(IMFMediaType *This, IMFAttributes *pTheirs, MF_ATTRIBUTES_MATCH_TYPE MatchType, BOOL *pbResult);
    HRESULT (WINAPI *GetUINT32)(IMFMediaType *This, REFGUID guidKey, UINT32 *punValue);
    HRESULT (WINAPI *GetUINT64)(IMFMediaType *This, REFGUID guidKey, UINT64 *punValue);
    HRESULT (WINAPI *GetDouble)(IMFMediaType *This, REFGUID guidKey, double *pfValue);
    HRESULT (WINAPI *GetGUID)(IMFMediaType *This, REFGUID guidKey, GUID *pguidValue);
    HRESULT (WINAPI *GetStringLength)(IMFMediaType *This, REFGUID guidKey, UINT32 *pcchLength);
    HRESULT (WINAPI *GetString)(IMFMediaType *This, REFGUID guidKey, LPWSTR pwszValue, UINT32 cchBufSize, UINT32 *pcchLength);
    HRESULT (WINAPI *GetAllocatedString)(IMFMediaType *This, REFGUID guidKey, LPWSTR *ppwszValue, UINT32 *pcchLength);
    HRESULT (WINAPI *GetBlobSize)(IMFMediaType *This, REFGUID guidKey, UINT32 *pcbBlobSize);
    HRESULT (WINAPI *GetBlob)(IMFMediaType *This, REFGUID guidKey, UINT8 *pBuf, UINT32 cbBufSize, UINT32 *pcbBlobSize);
    HRESULT (WINAPI *GetAllocatedBlob)(IMFMediaType *This, REFGUID guidKey, UINT8 **ppBuf, UINT32 *pcbSize);
    HRESULT (WINAPI *GetUnknown)(IMFMediaType *This, REFGUID guidKey, REFIID riid, LPVOID *ppv);
    HRESULT (WINAPI *SetItem)(IMFMediaType *This, REFGUID guidKey, const PROPVARIANT *Value);
    HRESULT (WINAPI *DeleteItem)(IMFMediaType *This, REFGUID guidKey);
    HRESULT (WINAPI *DeleteAllItems)(IMFMediaType *This);
    HRESULT (WINAPI *SetUINT32)(IMFMediaType *This, REFGUID guidKey, UINT32 unValue);
    HRESULT (WINAPI *SetUINT64)(IMFMediaType *This, REFGUID guidKey, UINT64 unValue);
    HRESULT (WINAPI *SetDouble)(IMFMediaType *This, REFGUID guidKey, double fValue);
    HRESULT (WINAPI *SetGUID)(IMFMediaType *This, REFGUID guidKey, REFGUID guidValue);
    HRESULT (WINAPI *SetString)(IMFMediaType *This, REFGUID guidKey, LPCWSTR wszValue);
    HRESULT (WINAPI *SetBlob)(IMFMediaType *This, REFGUID guidKey, const UINT8 *pBuf, UINT32 cbBufSize);
    HRESULT (WINAPI *SetUnknown)(IMFMediaType *This, REFGUID guidKey, IUnknown *pUnknown);
    HRESULT (WINAPI *LockStore)(IMFMediaType *This);
    HRESULT (WINAPI *UnlockStore)(IMFMediaType *This);
    HRESULT (WINAPI *GetCount)(IMFMediaType *This, UINT32 *pcItems);
    HRESULT (WINAPI *GetItemByIndex)(IMFMediaType *This, UINT32 unIndex, GUID *pguidKey, PROPVARIANT *pValue);
    HRESULT (WINAPI *CopyAllItems)(IMFMediaType *This, IMFAttributes *pDest);
    HRESULT (WINAPI *GetMajorType)(IMFMediaType *This, GUID *pguidMajorType);
    HRESULT (WINAPI *IsCompressedFormat)(IMFMediaType *This, BOOL *pfCompressed);
    HRESULT (WINAPI *IsEqual)(IMFMediaType *This, IMFMediaType *pIMediaType, DWORD *pdwFlags);
    HRESULT (WINAPI *GetRepresentation)(IMFMediaType *This, GUID guidRepresentation, LPVOID *ppvRepresentation);
    HRESULT (WINAPI *FreeRepresentation)(IMFMediaType *This, GUID guidRepresentation, LPVOID pvRepresentation);
} IMFMediaTypeVtbl;

struct IMFMediaType {
    const IMFMediaTypeVtbl *lpVtbl;
};

#if defined(__GNUC__) || defined(__clang__)
#define MFPLAT_GUID_UNUSED __attribute__((unused))
#else
#define MFPLAT_GUID_UNUSED
#endif

/* Standard COM & Media Foundation GUIDs */
static const GUID MFPLAT_GUID_UNUSED MFPLAT_IID_IUnknown =
    { 0x00000000, 0x0000, 0x0000, { 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46 } };
static const GUID MFPLAT_GUID_UNUSED MFPLAT_IID_IMFAttributes =
    { 0x2cd2d921, 0xc447, 0x44a7, { 0xa1, 0x3c, 0x4a, 0xda, 0xbf, 0xc2, 0x47, 0xe3 } };
static const GUID MFPLAT_GUID_UNUSED MFPLAT_IID_IMFMediaBuffer =
    { 0x045fa593, 0x8799, 0x42b8, { 0xbc, 0x8d, 0x89, 0x68, 0xc6, 0x45, 0x35, 0x07 } };
static const GUID MFPLAT_GUID_UNUSED MFPLAT_IID_IMFMediaType =
    { 0x44ae0fa8, 0xea31, 0x4109, { 0x8d, 0x2e, 0x4c, 0xae, 0x49, 0x97, 0xc5, 0x55 } };

static const GUID MFPLAT_GUID_UNUSED MF_MT_MAJOR_TYPE =
    { 0x48eba18e, 0xf8c9, 0x4687, { 0xbf, 0x11, 0x0a, 0x74, 0xc9, 0xf9, 0x6a, 0x8f } };
static const GUID MFPLAT_GUID_UNUSED MF_MT_SUBTYPE =
    { 0xf7e34c9a, 0x42e8, 0x4714, { 0xb7, 0x4b, 0xcb, 0x29, 0xd7, 0x2c, 0x35, 0xe5 } };
static const GUID MFPLAT_GUID_UNUSED MF_MT_ALL_SAMPLES_INDEPENDENT =
    { 0xc9173739, 0x5e56, 0x461c, { 0xb7, 0x13, 0x46, 0xfb, 0x99, 0x5c, 0xb9, 0x5f } };
static const GUID MFPLAT_GUID_UNUSED MF_MT_FIXED_SIZE_SAMPLES =
    { 0xb8ebefaf, 0xb718, 0x4e04, { 0xb0, 0xa9, 0x11, 0x67, 0x75, 0xe3, 0x32, 0x1b } };
static const GUID MFPLAT_GUID_UNUSED MF_MT_COMPRESSED =
    { 0x3afd0cee, 0x18f2, 0x4ba5, { 0xa1, 0x10, 0x8b, 0xea, 0x50, 0x2e, 0x1f, 0x92 } };
static const GUID MFPLAT_GUID_UNUSED MF_MT_SAMPLE_SIZE =
    { 0xdad3ab78, 0x1990, 0x408b, { 0xbc, 0xe2, 0xeb, 0xa6, 0x73, 0xda, 0xcc, 0x10 } };
static const GUID MFPLAT_GUID_UNUSED MF_MT_FRAME_SIZE =
    { 0x1652c33d, 0xd6b2, 0x4012, { 0xb8, 0x34, 0x72, 0x03, 0x08, 0x49, 0xa3, 0x7d } };
static const GUID MFPLAT_GUID_UNUSED MF_MT_FRAME_RATE =
    { 0xc459a2e8, 0x3d2c, 0x4e44, { 0xb1, 0x32, 0xfe, 0xe5, 0x15, 0x6c, 0x7b, 0xb0 } };
static const GUID MFPLAT_GUID_UNUSED MF_MT_PIXEL_ASPECT_RATIO =
    { 0xc6376a1e, 0x8d0a, 0x4027, { 0xbe, 0x45, 0x6d, 0x9a, 0x0a, 0xd3, 0x9b, 0xb6 } };
static const GUID MFPLAT_GUID_UNUSED MF_MT_MPEG_SEQUENCE_HEADER =
    { 0x3c036de7, 0x3ad0, 0x4c9e, { 0x92, 0x16, 0xee, 0x6d, 0x6a, 0xc2, 0x1c, 0xb3 } };
static const GUID MFPLAT_GUID_UNUSED MF_MT_USER_DATA =
    { 0xb6bc765f, 0x4c3b, 0x40a4, { 0xbd, 0x51, 0x25, 0x35, 0xb6, 0x6f, 0xe0, 0x9d } };

static const GUID MFPLAT_GUID_UNUSED MFMediaType_Video =
    { 0x73646976, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 } };
static const GUID MFPLAT_GUID_UNUSED MFMediaType_Audio =
    { 0x73647561, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 } };
static const GUID MFPLAT_GUID_UNUSED MFVideoFormat_Base =
    { 0x00000000, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 } };
static const GUID MFPLAT_GUID_UNUSED MFVideoFormat_RGB32 =
    { D3DFMT_X8R8G8B8, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 } };
static const GUID MFPLAT_GUID_UNUSED MFVideoFormat_ARGB32 =
    { D3DFMT_A8R8G8B8, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 } };
static const GUID MFPLAT_GUID_UNUSED MFVideoFormat_NV12 =
    { 0x3231564e, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 } };
static const GUID MFPLAT_GUID_UNUSED MFVideoFormat_YUY2 =
    { 0x32595559, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 } };
static const GUID MFPLAT_GUID_UNUSED MFVideoFormat_I420 =
    { 0x30323449, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 } };
static const GUID MFPLAT_GUID_UNUSED MFVideoFormat_IYUV =
    { 0x56555949, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 } };
static const GUID MFPLAT_GUID_UNUSED MFVideoFormat_YV12 =
    { 0x32315659, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 } };
static const GUID MFPLAT_GUID_UNUSED MFVideoFormat_P010 =
    { 0x30313050, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 } };
static const GUID MFPLAT_GUID_UNUSED MFVideoFormat_H264 =
    { 0x34363248, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 } };
static const GUID MFPLAT_GUID_UNUSED MFVideoFormat_HEVC =
    { 0x43564548, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 } };
static const GUID MFPLAT_GUID_UNUSED MFAudioFormat_PCM =
    { 0x00000001, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 } };
static const GUID MFPLAT_GUID_UNUSED MFAudioFormat_Float =
    { 0x00000003, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 } };

static inline UINT64 Pack2UINT32AsUINT64(UINT32 high, UINT32 low)
{
    return ((UINT64)high << 32) | (UINT64)low;
}

static inline void Unpack2UINT32AsUINT64(UINT64 packed, UINT32 *high, UINT32 *low)
{
    if (high) {
        *high = (UINT32)(packed >> 32);
    }
    if (low) {
        *low = (UINT32)(packed & 0xffffffffULL);
    }
}

static inline HRESULT MFSetAttributeSize(IMFAttributes *attrs, REFGUID key, UINT32 width, UINT32 height)
{
    if (!attrs) {
        return E_INVALIDARG;
    }
    return attrs->lpVtbl->SetUINT64(attrs, key, Pack2UINT32AsUINT64(width, height));
}

static inline HRESULT MFGetAttributeSize(IMFAttributes *attrs, REFGUID key, UINT32 *width, UINT32 *height)
{
    UINT64 packed = 0;
    HRESULT hr;
    if (!attrs || !width || !height) {
        return E_INVALIDARG;
    }
    hr = attrs->lpVtbl->GetUINT64(attrs, key, &packed);
    if (SUCCEEDED(hr)) {
        Unpack2UINT32AsUINT64(packed, width, height);
    }
    return hr;
}

/* Core Media Foundation API exports */
HRESULT WINAPI MFStartup(ULONG version, DWORD flags);
HRESULT WINAPI MFShutdown(void);
HRESULT WINAPI MFLockPlatform(void);
HRESULT WINAPI MFUnlockPlatform(void);
HRESULT WINAPI MFAllocateWorkQueue(DWORD *work_queue);
HRESULT WINAPI MFCreateMemoryBuffer(DWORD max_length, IMFMediaBuffer **buffer);
HRESULT WINAPI MFCreateAlignedMemoryBuffer(DWORD max_length, DWORD alignment, IMFMediaBuffer **buffer);
HRESULT WINAPI MFCreateAttributes(IMFAttributes **attributes, UINT32 initial_size);
HRESULT WINAPI MFCreateMediaType(IMFMediaType **media_type);
HRESULT WINAPI MFCreateGuid(GUID *guid);
DWORD   WINAPI GetD3DFormatFromMFSubtype(const GUID *subtype);
HRESULT WINAPI MFCalculateImageSize(REFGUID subtype, UINT32 width, UINT32 height, UINT32 *image_size);

/* macOS VideoToolbox / CoreMedia Hardware H.264 & HEVC Decode Bridge */
typedef struct MF_VIDEOTOOLBOX_DECODER MF_VIDEOTOOLBOX_DECODER;

typedef struct _MF_VIDEOTOOLBOX_STATE {
    DWORD codec_fourcc;
    DWORD output_fourcc;
    UINT32 width;
    UINT32 height;
    BOOL hardware_accelerated;
    BOOL vt_session_ready;
    BOOL iosurface_backed;
    DWORD decoded_frames;
} MF_VIDEOTOOLBOX_STATE;

HRESULT WINAPI MFVideoToolboxCreateDecoder(
    IMFMediaType *input_type,
    IMFMediaType *output_type,
    MF_VIDEOTOOLBOX_DECODER **out_decoder);

HRESULT WINAPI MFVideoToolboxDecodeSample(
    MF_VIDEOTOOLBOX_DECODER *decoder,
    const BYTE *nal_data,
    DWORD nal_len,
    IMFMediaBuffer **out_buffer);

HRESULT WINAPI MFVideoToolboxQueryState(
    MF_VIDEOTOOLBOX_DECODER *decoder,
    MF_VIDEOTOOLBOX_STATE *out_state);

void WINAPI MFVideoToolboxDestroyDecoder(MF_VIDEOTOOLBOX_DECODER *decoder);

#endif /* _WINE_MFPLAT_H */

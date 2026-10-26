/*
 * Wine mfplat.dll Core Media Foundation Implementation
 *
 * Clean-room implementation derived from Microsoft Learn specifications
 * and upstream Wine dlls/mfplat/ (main.c, buffer.c, mediatype.c).
 *
 * Copyright 2026 Spumante Contributors
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "mfplat.h"

#ifdef __APPLE__
#include <CoreFoundation/CoreFoundation.h>
#include <Security/Security.h>
#endif

#ifndef _WIN32
#include <pthread.h>
#endif

#define WINE_DEFAULT_DEBUG_CHANNEL(ch)
#define TRACE(...) do { } while (0)
#define WARN(...)  do { } while (0)
#define ERR(...)   do { } while (0)

WINE_DEFAULT_DEBUG_CHANNEL(mfplat);

static volatile LONG g_mf_platform_refcount = 0;
static volatile LONG g_mf_platform_locks = 0;
static volatile LONG g_mf_next_work_queue = 1;

static LONG mf_atomic_inc(volatile LONG *ptr)
{
#ifdef _WIN32
    return InterlockedIncrement((LONG *)ptr);
#else
    return __sync_add_and_fetch(ptr, 1);
#endif
}

static LONG mf_atomic_dec(volatile LONG *ptr)
{
#ifdef _WIN32
    return InterlockedDecrement((LONG *)ptr);
#else
    return __sync_sub_and_fetch(ptr, 1);
#endif
}

static LONG mf_atomic_get(volatile LONG *ptr)
{
#ifdef _WIN32
    return InterlockedCompareExchange((LONG *)ptr, 0, 0);
#else
    return __sync_add_and_fetch(ptr, 0);
#endif
}

static size_t mf_wcslen(LPCWSTR str)
{
    size_t len = 0;
    if (!str) {
        return 0;
    }
    while (str[len] != 0) {
        len++;
    }
    return len;
}

/* ========================================================================
 * Platform Startup / Shutdown / WorkQueue Lifecycle
 * ======================================================================== */

HRESULT WINAPI MFStartup(ULONG version, DWORD flags)
{
    const char *sim_stub = getenv("SPUMANTE_SIMULATE_STUB");
    TRACE("(0x%08x, 0x%08x)\n", (unsigned)version, (unsigned)flags);
    (void)flags;

    if (sim_stub && strcmp(sim_stub, "1") == 0) {
        return E_NOTIMPL;
    }

    if (version != MF_VERSION_XP && version != MF_VERSION_WIN7) {
        WARN("Unsupported MFStartup version 0x%08x\n", (unsigned)version);
        return MF_E_BAD_STARTUP_VERSION;
    }

    mf_atomic_inc(&g_mf_platform_refcount);
    return S_OK;
}

HRESULT WINAPI MFShutdown(void)
{
    LONG cur;
    TRACE("()\n");

    cur = mf_atomic_get(&g_mf_platform_refcount);
    if (cur > 0) {
        if (mf_atomic_dec(&g_mf_platform_refcount) < 0) {
            g_mf_platform_refcount = 0;
        }
    }
    return S_OK;
}

HRESULT WINAPI MFLockPlatform(void)
{
    TRACE("()\n");
    mf_atomic_inc(&g_mf_platform_locks);
    return S_OK;
}

HRESULT WINAPI MFUnlockPlatform(void)
{
    TRACE("()\n");
    if (mf_atomic_get(&g_mf_platform_locks) > 0) {
        mf_atomic_dec(&g_mf_platform_locks);
    }
    return S_OK;
}

HRESULT WINAPI MFAllocateWorkQueue(DWORD *work_queue)
{
    TRACE("(%p)\n", (void *)work_queue);
    if (!work_queue) {
        return E_INVALIDARG;
    }
    if (mf_atomic_get(&g_mf_platform_refcount) <= 0 &&
        mf_atomic_get(&g_mf_platform_locks) <= 0) {
        *work_queue = 0;
        return MF_E_SHUTDOWN;
    }
    *work_queue = (DWORD)mf_atomic_inc(&g_mf_next_work_queue);
    return S_OK;
}

/* ========================================================================
 * IMFMediaBuffer (MFCreateMemoryBuffer / MFCreateAlignedMemoryBuffer)
 * ======================================================================== */

struct mf_media_buffer {
    IMFMediaBuffer IMFMediaBuffer_iface;
    volatile LONG refcount;
    volatile LONG lock_count;
    BYTE *raw_alloc;
    BYTE *aligned_data;
    DWORD max_length;
    DWORD current_length;
};

static inline struct mf_media_buffer *impl_from_IMFMediaBuffer(IMFMediaBuffer *iface)
{
    return (struct mf_media_buffer *)iface;
}

static HRESULT WINAPI memory_buffer_QueryInterface(
    IMFMediaBuffer *iface,
    REFIID riid,
    void **ppv)
{
    if (!ppv) {
        return E_POINTER;
    }
    if (!riid) {
        *ppv = NULL;
        return E_INVALIDARG;
    }
    if (IsEqualIID(riid, &MFPLAT_IID_IUnknown) ||
        IsEqualIID(riid, &MFPLAT_IID_IMFMediaBuffer)) {
        *ppv = iface;
        iface->lpVtbl->AddRef(iface);
        return S_OK;
    }
    *ppv = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI memory_buffer_AddRef(IMFMediaBuffer *iface)
{
    struct mf_media_buffer *buffer = impl_from_IMFMediaBuffer(iface);
    return (ULONG)mf_atomic_inc(&buffer->refcount);
}

static ULONG WINAPI memory_buffer_Release(IMFMediaBuffer *iface)
{
    struct mf_media_buffer *buffer = impl_from_IMFMediaBuffer(iface);
    LONG ref = mf_atomic_dec(&buffer->refcount);
    if (ref == 0) {
        free(buffer->raw_alloc);
        free(buffer);
    }
    return (ULONG)ref;
}

static HRESULT WINAPI memory_buffer_Lock(
    IMFMediaBuffer *iface,
    BYTE **ppbBuffer,
    DWORD *pcbMaxLength,
    DWORD *pcbCurrentLength)
{
    struct mf_media_buffer *buffer = impl_from_IMFMediaBuffer(iface);
    if (!ppbBuffer) {
        return E_INVALIDARG;
    }
    mf_atomic_inc(&buffer->lock_count);
    *ppbBuffer = buffer->aligned_data;
    if (pcbMaxLength) {
        *pcbMaxLength = buffer->max_length;
    }
    if (pcbCurrentLength) {
        *pcbCurrentLength = buffer->current_length;
    }
    return S_OK;
}

static HRESULT WINAPI memory_buffer_Unlock(IMFMediaBuffer *iface)
{
    struct mf_media_buffer *buffer = impl_from_IMFMediaBuffer(iface);
    if (mf_atomic_get(&buffer->lock_count) > 0) {
        mf_atomic_dec(&buffer->lock_count);
    }
    return S_OK;
}

static HRESULT WINAPI memory_buffer_GetCurrentLength(
    IMFMediaBuffer *iface,
    DWORD *pcbCurrentLength)
{
    struct mf_media_buffer *buffer = impl_from_IMFMediaBuffer(iface);
    if (!pcbCurrentLength) {
        return E_INVALIDARG;
    }
    *pcbCurrentLength = buffer->current_length;
    return S_OK;
}

static HRESULT WINAPI memory_buffer_SetCurrentLength(
    IMFMediaBuffer *iface,
    DWORD cbCurrentLength)
{
    struct mf_media_buffer *buffer = impl_from_IMFMediaBuffer(iface);
    if (cbCurrentLength > buffer->max_length) {
        return E_INVALIDARG;
    }
    buffer->current_length = cbCurrentLength;
    return S_OK;
}

static HRESULT WINAPI memory_buffer_GetMaxLength(
    IMFMediaBuffer *iface,
    DWORD *pcbMaxLength)
{
    struct mf_media_buffer *buffer = impl_from_IMFMediaBuffer(iface);
    if (!pcbMaxLength) {
        return E_INVALIDARG;
    }
    *pcbMaxLength = buffer->max_length;
    return S_OK;
}

static const IMFMediaBufferVtbl g_memory_buffer_vtbl = {
    memory_buffer_QueryInterface,
    memory_buffer_AddRef,
    memory_buffer_Release,
    memory_buffer_Lock,
    memory_buffer_Unlock,
    memory_buffer_GetCurrentLength,
    memory_buffer_SetCurrentLength,
    memory_buffer_GetMaxLength
};

HRESULT WINAPI MFCreateAlignedMemoryBuffer(
    DWORD max_length,
    DWORD alignment,
    IMFMediaBuffer **buffer)
{
    struct mf_media_buffer *obj;
    size_t align_bytes;
    size_t alloc_size;
    uintptr_t raw_addr;
    uintptr_t aligned_addr;

    TRACE("(%u, 0x%08x, %p)\n", (unsigned)max_length, (unsigned)alignment, (void *)buffer);

    if (!buffer) {
        return E_INVALIDARG;
    }
    *buffer = NULL;

    /* Validate alignment mask: (alignment + 1) must be a power of two */
    align_bytes = (size_t)alignment + 1U;
    if (align_bytes == 0 || (align_bytes & (align_bytes - 1U)) != 0) {
        align_bytes = 16U;
    }
    if (align_bytes < sizeof(void *)) {
        align_bytes = sizeof(void *);
    }

    alloc_size = (max_length > 0 ? (size_t)max_length : 1U) + align_bytes;
    if (alloc_size < (size_t)max_length) {
        return E_OUTOFMEMORY;
    }

    obj = (struct mf_media_buffer *)calloc(1, sizeof(*obj));
    if (!obj) {
        return E_OUTOFMEMORY;
    }

    obj->raw_alloc = (BYTE *)calloc(1, alloc_size);
    if (!obj->raw_alloc) {
        free(obj);
        return E_OUTOFMEMORY;
    }

    raw_addr = (uintptr_t)obj->raw_alloc;
    aligned_addr = (raw_addr + (align_bytes - 1U)) & ~(uintptr_t)(align_bytes - 1U);
    obj->IMFMediaBuffer_iface.lpVtbl = &g_memory_buffer_vtbl;
    obj->refcount = 1;
    obj->lock_count = 0;
    obj->aligned_data = (BYTE *)aligned_addr;
    obj->max_length = max_length;
    obj->current_length = 0;

    *buffer = &obj->IMFMediaBuffer_iface;
    return S_OK;
}

HRESULT WINAPI MFCreateMemoryBuffer(DWORD max_length, IMFMediaBuffer **buffer)
{
    return MFCreateAlignedMemoryBuffer(max_length, MF_1_BYTE_ALIGNMENT, buffer);
}

/* ========================================================================
 * IMFAttributes & IMFMediaType Implementation
 * ======================================================================== */

struct mf_attr_entry {
    GUID key;
    PROPVARIANT value;
};

struct mf_attributes {
    IMFMediaType IMFMediaType_iface;
    volatile LONG refcount;
    BOOL is_media_type;
    struct mf_attr_entry *entries;
    UINT32 count;
    UINT32 capacity;
#ifndef _WIN32
    pthread_mutex_t mutex;
#else
    CRITICAL_SECTION cs;
#endif
};

static inline struct mf_attributes *impl_from_IMFMediaType(IMFMediaType *iface)
{
    return (struct mf_attributes *)iface;
}

static void mf_attr_lock(struct mf_attributes *attrs)
{
#ifndef _WIN32
    pthread_mutex_lock(&attrs->mutex);
#else
    EnterCriticalSection(&attrs->cs);
#endif
}

static void mf_attr_unlock(struct mf_attributes *attrs)
{
#ifndef _WIN32
    pthread_mutex_unlock(&attrs->mutex);
#else
    LeaveCriticalSection(&attrs->cs);
#endif
}

static HRESULT mf_propvariant_copy(PROPVARIANT *dst, const PROPVARIANT *src)
{
    if (!dst || !src) {
        return E_INVALIDARG;
    }
    PropVariantInit(dst);
    switch (src->vt) {
    case VT_EMPTY:
    case VT_UI4:
    case VT_UI8:
    case VT_R8:
        *dst = *src;
        return S_OK;
    case VT_CLSID:
        if (!src->puuid) {
            return E_INVALIDARG;
        }
        dst->puuid = (GUID *)CoTaskMemAlloc(sizeof(GUID));
        if (!dst->puuid) {
            return E_OUTOFMEMORY;
        }
        *dst->puuid = *src->puuid;
        dst->vt = VT_CLSID;
        return S_OK;
    case VT_LPWSTR: {
        size_t len;
        if (!src->pwszVal) {
            return E_INVALIDARG;
        }
        len = mf_wcslen(src->pwszVal);
        dst->pwszVal = (LPWSTR)CoTaskMemAlloc((len + 1U) * sizeof(WCHAR));
        if (!dst->pwszVal) {
            return E_OUTOFMEMORY;
        }
        memcpy(dst->pwszVal, src->pwszVal, (len + 1U) * sizeof(WCHAR));
        dst->vt = VT_LPWSTR;
        return S_OK;
    }
    case (VT_VECTOR | VT_UI1): {
        ULONG sz = src->caub.cElems;
        dst->caub.pElems = (UCHAR *)CoTaskMemAlloc(sz ? sz : 1U);
        if (!dst->caub.pElems) {
            return E_OUTOFMEMORY;
        }
        if (sz > 0 && src->caub.pElems) {
            memcpy(dst->caub.pElems, src->caub.pElems, sz);
        }
        dst->caub.cElems = sz;
        dst->vt = (VT_VECTOR | VT_UI1);
        return S_OK;
    }
    case VT_UNKNOWN:
        dst->punkVal = src->punkVal;
        if (dst->punkVal) {
            dst->punkVal->lpVtbl->AddRef(dst->punkVal);
        }
        dst->vt = VT_UNKNOWN;
        return S_OK;
    default:
        return E_INVALIDARG;
    }
}

static BOOL mf_propvariant_equal(const PROPVARIANT *a, const PROPVARIANT *b)
{
    if (!a || !b || a->vt != b->vt) {
        return FALSE;
    }
    switch (a->vt) {
    case VT_EMPTY:
        return TRUE;
    case VT_UI4:
        return a->ulVal == b->ulVal;
    case VT_UI8:
        return a->uhVal == b->uhVal;
    case VT_R8:
        return a->dblVal == b->dblVal;
    case VT_CLSID:
        return (a->puuid && b->puuid && IsEqualGUID(a->puuid, b->puuid)) ? TRUE : FALSE;
    case VT_LPWSTR: {
        size_t len_a, len_b;
        if (!a->pwszVal || !b->pwszVal) {
            return a->pwszVal == b->pwszVal;
        }
        len_a = mf_wcslen(a->pwszVal);
        len_b = mf_wcslen(b->pwszVal);
        return (len_a == len_b &&
                memcmp(a->pwszVal, b->pwszVal, len_a * sizeof(WCHAR)) == 0) ? TRUE : FALSE;
    }
    case (VT_VECTOR | VT_UI1):
        if (a->caub.cElems != b->caub.cElems) {
            return FALSE;
        }
        if (a->caub.cElems == 0) {
            return TRUE;
        }
        return (a->caub.pElems && b->caub.pElems &&
                memcmp(a->caub.pElems, b->caub.pElems, a->caub.cElems) == 0) ? TRUE : FALSE;
    case VT_UNKNOWN:
        return a->punkVal == b->punkVal ? TRUE : FALSE;
    default:
        return FALSE;
    }
}

static struct mf_attr_entry *mf_attr_find_unlocked(struct mf_attributes *attrs, REFGUID key)
{
    UINT32 i;
    for (i = 0; i < attrs->count; i++) {
        if (IsEqualGUID(&attrs->entries[i].key, key)) {
            return &attrs->entries[i];
        }
    }
    return NULL;
}

static HRESULT mf_attr_set_unlocked(
    struct mf_attributes *attrs,
    REFGUID key,
    const PROPVARIANT *val)
{
    struct mf_attr_entry *entry;
    PROPVARIANT copy_val;
    HRESULT hr;

    if (!key || !val) {
        return E_INVALIDARG;
    }

    hr = mf_propvariant_copy(&copy_val, val);
    if (FAILED(hr)) {
        return hr;
    }

    entry = mf_attr_find_unlocked(attrs, key);
    if (entry) {
        PropVariantClear(&entry->value);
        entry->value = copy_val;
        return S_OK;
    }

    if (attrs->count >= attrs->capacity) {
        UINT32 new_cap = attrs->capacity ? attrs->capacity * 2U : 8U;
        struct mf_attr_entry *new_entries = (struct mf_attr_entry *)realloc(
            attrs->entries,
            new_cap * sizeof(struct mf_attr_entry)
        );
        if (!new_entries) {
            PropVariantClear(&copy_val);
            return E_OUTOFMEMORY;
        }
        attrs->entries = new_entries;
        attrs->capacity = new_cap;
    }

    attrs->entries[attrs->count].key = *key;
    attrs->entries[attrs->count].value = copy_val;
    attrs->count++;
    return S_OK;
}

static HRESULT WINAPI attributes_QueryInterface(
    IMFMediaType *iface,
    REFIID riid,
    void **ppv)
{
    struct mf_attributes *attrs = impl_from_IMFMediaType(iface);
    if (!ppv) {
        return E_POINTER;
    }
    if (!riid) {
        *ppv = NULL;
        return E_INVALIDARG;
    }
    if (IsEqualIID(riid, &MFPLAT_IID_IUnknown) ||
        IsEqualIID(riid, &MFPLAT_IID_IMFAttributes) ||
        (attrs->is_media_type && IsEqualIID(riid, &MFPLAT_IID_IMFMediaType))) {
        *ppv = iface;
        iface->lpVtbl->AddRef(iface);
        return S_OK;
    }
    *ppv = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI attributes_AddRef(IMFMediaType *iface)
{
    struct mf_attributes *attrs = impl_from_IMFMediaType(iface);
    return (ULONG)mf_atomic_inc(&attrs->refcount);
}

static ULONG WINAPI attributes_Release(IMFMediaType *iface)
{
    struct mf_attributes *attrs = impl_from_IMFMediaType(iface);
    LONG ref = mf_atomic_dec(&attrs->refcount);
    if (ref == 0) {
        UINT32 i;
        for (i = 0; i < attrs->count; i++) {
            PropVariantClear(&attrs->entries[i].value);
        }
        free(attrs->entries);
#ifndef _WIN32
        pthread_mutex_destroy(&attrs->mutex);
#else
        DeleteCriticalSection(&attrs->cs);
#endif
        free(attrs);
    }
    return (ULONG)ref;
}

static HRESULT WINAPI attributes_GetItem(
    IMFMediaType *iface,
    REFGUID guidKey,
    PROPVARIANT *pValue)
{
    struct mf_attributes *attrs = impl_from_IMFMediaType(iface);
    struct mf_attr_entry *entry;
    HRESULT hr = S_OK;

    if (!guidKey) {
        return E_INVALIDARG;
    }
    mf_attr_lock(attrs);
    entry = mf_attr_find_unlocked(attrs, guidKey);
    if (!entry) {
        mf_attr_unlock(attrs);
        return MF_E_ATTRIBUTENOTFOUND;
    }
    if (pValue) {
        hr = mf_propvariant_copy(pValue, &entry->value);
    }
    mf_attr_unlock(attrs);
    return hr;
}

static HRESULT WINAPI attributes_GetItemType(
    IMFMediaType *iface,
    REFGUID guidKey,
    MF_ATTRIBUTE_TYPE *pType)
{
    struct mf_attributes *attrs = impl_from_IMFMediaType(iface);
    struct mf_attr_entry *entry;

    if (!guidKey || !pType) {
        return E_INVALIDARG;
    }
    mf_attr_lock(attrs);
    entry = mf_attr_find_unlocked(attrs, guidKey);
    if (!entry) {
        *pType = (MF_ATTRIBUTE_TYPE)0;
        mf_attr_unlock(attrs);
        return MF_E_ATTRIBUTENOTFOUND;
    }
    *pType = (MF_ATTRIBUTE_TYPE)entry->value.vt;
    mf_attr_unlock(attrs);
    return S_OK;
}

static HRESULT WINAPI attributes_CompareItem(
    IMFMediaType *iface,
    REFGUID guidKey,
    const PROPVARIANT *Value,
    BOOL *pbResult)
{
    struct mf_attributes *attrs = impl_from_IMFMediaType(iface);
    struct mf_attr_entry *entry;

    if (!guidKey || !Value) {
        return E_INVALIDARG;
    }
    if (pbResult) {
        *pbResult = FALSE;
    }
    mf_attr_lock(attrs);
    entry = mf_attr_find_unlocked(attrs, guidKey);
    if (entry && pbResult) {
        *pbResult = mf_propvariant_equal(&entry->value, Value);
    }
    mf_attr_unlock(attrs);
    return S_OK;
}

static BOOL mf_match_subset(IMFAttributes *sub, IMFAttributes *super)
{
    UINT32 count = 0;
    UINT32 i;
    if (FAILED(sub->lpVtbl->GetCount(sub, &count))) {
        return FALSE;
    }
    for (i = 0; i < count; i++) {
        GUID key = { 0 };
        PROPVARIANT val;
        BOOL item_match = FALSE;
        PropVariantInit(&val);
        if (FAILED(sub->lpVtbl->GetItemByIndex(sub, i, &key, &val))) {
            return FALSE;
        }
        if (FAILED(super->lpVtbl->CompareItem(super, &key, &val, &item_match)) || !item_match) {
            PropVariantClear(&val);
            return FALSE;
        }
        PropVariantClear(&val);
    }
    return TRUE;
}

static HRESULT WINAPI attributes_Compare(
    IMFMediaType *iface,
    IMFAttributes *pTheirs,
    MF_ATTRIBUTES_MATCH_TYPE MatchType,
    BOOL *pbResult)
{
    IMFAttributes *ours = (IMFAttributes *)iface;
    UINT32 our_count = 0, their_count = 0;

    if (!pTheirs || !pbResult ||
        MatchType < MF_ATTRIBUTES_MATCH_OUR_ITEMS ||
        MatchType > MF_ATTRIBUTES_MATCH_SMALLER) {
        return E_INVALIDARG;
    }

    *pbResult = FALSE;
    ours->lpVtbl->GetCount(ours, &our_count);
    pTheirs->lpVtbl->GetCount(pTheirs, &their_count);

    switch (MatchType) {
    case MF_ATTRIBUTES_MATCH_OUR_ITEMS:
        *pbResult = mf_match_subset(ours, pTheirs);
        break;
    case MF_ATTRIBUTES_MATCH_THEIR_ITEMS:
        *pbResult = mf_match_subset(pTheirs, ours);
        break;
    case MF_ATTRIBUTES_MATCH_ALL_ITEMS:
        *pbResult = (our_count == their_count && mf_match_subset(ours, pTheirs)) ? TRUE : FALSE;
        break;
    case MF_ATTRIBUTES_MATCH_INTERSECTION: {
        UINT32 i;
        BOOL matched = TRUE;
        for (i = 0; i < our_count; i++) {
            GUID key = { 0 };
            PROPVARIANT val;
            MF_ATTRIBUTE_TYPE their_type = (MF_ATTRIBUTE_TYPE)0;
            PropVariantInit(&val);
            if (SUCCEEDED(ours->lpVtbl->GetItemByIndex(ours, i, &key, &val))) {
                if (SUCCEEDED(pTheirs->lpVtbl->GetItemType(pTheirs, &key, &their_type))) {
                    BOOL item_eq = FALSE;
                    pTheirs->lpVtbl->CompareItem(pTheirs, &key, &val, &item_eq);
                    if (!item_eq) {
                        matched = FALSE;
                    }
                }
                PropVariantClear(&val);
            }
            if (!matched) {
                break;
            }
        }
        *pbResult = matched;
        break;
    }
    case MF_ATTRIBUTES_MATCH_SMALLER:
        if (our_count <= their_count) {
            *pbResult = mf_match_subset(ours, pTheirs);
        } else {
            *pbResult = mf_match_subset(pTheirs, ours);
        }
        break;
    }
    return S_OK;
}

static HRESULT WINAPI attributes_GetUINT32(
    IMFMediaType *iface,
    REFGUID guidKey,
    UINT32 *punValue)
{
    struct mf_attributes *attrs = impl_from_IMFMediaType(iface);
    struct mf_attr_entry *entry;

    if (!guidKey || !punValue) {
        return E_INVALIDARG;
    }
    mf_attr_lock(attrs);
    entry = mf_attr_find_unlocked(attrs, guidKey);
    if (!entry) {
        mf_attr_unlock(attrs);
        return MF_E_ATTRIBUTENOTFOUND;
    }
    if (entry->value.vt != VT_UI4) {
        mf_attr_unlock(attrs);
        return MF_E_INVALIDTYPE;
    }
    *punValue = (UINT32)entry->value.ulVal;
    mf_attr_unlock(attrs);
    return S_OK;
}

static HRESULT WINAPI attributes_GetUINT64(
    IMFMediaType *iface,
    REFGUID guidKey,
    UINT64 *punValue)
{
    struct mf_attributes *attrs = impl_from_IMFMediaType(iface);
    struct mf_attr_entry *entry;

    if (!guidKey || !punValue) {
        return E_INVALIDARG;
    }
    mf_attr_lock(attrs);
    entry = mf_attr_find_unlocked(attrs, guidKey);
    if (!entry) {
        mf_attr_unlock(attrs);
        return MF_E_ATTRIBUTENOTFOUND;
    }
    if (entry->value.vt != VT_UI8) {
        mf_attr_unlock(attrs);
        return MF_E_INVALIDTYPE;
    }
    *punValue = entry->value.uhVal;
    mf_attr_unlock(attrs);
    return S_OK;
}

static HRESULT WINAPI attributes_GetDouble(
    IMFMediaType *iface,
    REFGUID guidKey,
    double *pfValue)
{
    struct mf_attributes *attrs = impl_from_IMFMediaType(iface);
    struct mf_attr_entry *entry;

    if (!guidKey || !pfValue) {
        return E_INVALIDARG;
    }
    mf_attr_lock(attrs);
    entry = mf_attr_find_unlocked(attrs, guidKey);
    if (!entry) {
        mf_attr_unlock(attrs);
        return MF_E_ATTRIBUTENOTFOUND;
    }
    if (entry->value.vt != VT_R8) {
        mf_attr_unlock(attrs);
        return MF_E_INVALIDTYPE;
    }
    *pfValue = entry->value.dblVal;
    mf_attr_unlock(attrs);
    return S_OK;
}

static HRESULT WINAPI attributes_GetGUID(
    IMFMediaType *iface,
    REFGUID guidKey,
    GUID *pguidValue)
{
    struct mf_attributes *attrs = impl_from_IMFMediaType(iface);
    struct mf_attr_entry *entry;

    if (!guidKey || !pguidValue) {
        return E_INVALIDARG;
    }
    mf_attr_lock(attrs);
    entry = mf_attr_find_unlocked(attrs, guidKey);
    if (!entry) {
        mf_attr_unlock(attrs);
        return MF_E_ATTRIBUTENOTFOUND;
    }
    if (entry->value.vt != VT_CLSID || !entry->value.puuid) {
        mf_attr_unlock(attrs);
        return MF_E_INVALIDTYPE;
    }
    *pguidValue = *entry->value.puuid;
    mf_attr_unlock(attrs);
    return S_OK;
}

static HRESULT WINAPI attributes_GetStringLength(
    IMFMediaType *iface,
    REFGUID guidKey,
    UINT32 *pcchLength)
{
    struct mf_attributes *attrs = impl_from_IMFMediaType(iface);
    struct mf_attr_entry *entry;

    if (!guidKey || !pcchLength) {
        return E_INVALIDARG;
    }
    mf_attr_lock(attrs);
    entry = mf_attr_find_unlocked(attrs, guidKey);
    if (!entry) {
        mf_attr_unlock(attrs);
        return MF_E_ATTRIBUTENOTFOUND;
    }
    if (entry->value.vt != VT_LPWSTR || !entry->value.pwszVal) {
        mf_attr_unlock(attrs);
        return MF_E_INVALIDTYPE;
    }
    *pcchLength = (UINT32)mf_wcslen(entry->value.pwszVal);
    mf_attr_unlock(attrs);
    return S_OK;
}

static HRESULT WINAPI attributes_GetString(
    IMFMediaType *iface,
    REFGUID guidKey,
    LPWSTR pwszValue,
    UINT32 cchBufSize,
    UINT32 *pcchLength)
{
    struct mf_attributes *attrs = impl_from_IMFMediaType(iface);
    struct mf_attr_entry *entry;
    UINT32 len;

    if (!guidKey || !pwszValue) {
        return E_INVALIDARG;
    }
    mf_attr_lock(attrs);
    entry = mf_attr_find_unlocked(attrs, guidKey);
    if (!entry) {
        mf_attr_unlock(attrs);
        return MF_E_ATTRIBUTENOTFOUND;
    }
    if (entry->value.vt != VT_LPWSTR || !entry->value.pwszVal) {
        mf_attr_unlock(attrs);
        return MF_E_INVALIDTYPE;
    }
    len = (UINT32)mf_wcslen(entry->value.pwszVal);
    if (pcchLength) {
        *pcchLength = len;
    }
    if (cchBufSize <= len) {
        mf_attr_unlock(attrs);
        return STRSAFE_E_INSUFFICIENT_BUFFER;
    }
    memcpy(pwszValue, entry->value.pwszVal, (len + 1U) * sizeof(WCHAR));
    mf_attr_unlock(attrs);
    return S_OK;
}

static HRESULT WINAPI attributes_GetAllocatedString(
    IMFMediaType *iface,
    REFGUID guidKey,
    LPWSTR *ppwszValue,
    UINT32 *pcchLength)
{
    struct mf_attributes *attrs = impl_from_IMFMediaType(iface);
    struct mf_attr_entry *entry;
    UINT32 len;
    LPWSTR copy;

    if (!guidKey || !ppwszValue || !pcchLength) {
        return E_INVALIDARG;
    }
    *ppwszValue = NULL;
    *pcchLength = 0;

    mf_attr_lock(attrs);
    entry = mf_attr_find_unlocked(attrs, guidKey);
    if (!entry) {
        mf_attr_unlock(attrs);
        return MF_E_ATTRIBUTENOTFOUND;
    }
    if (entry->value.vt != VT_LPWSTR || !entry->value.pwszVal) {
        mf_attr_unlock(attrs);
        return MF_E_INVALIDTYPE;
    }
    len = (UINT32)mf_wcslen(entry->value.pwszVal);
    copy = (LPWSTR)CoTaskMemAlloc((len + 1U) * sizeof(WCHAR));
    if (!copy) {
        mf_attr_unlock(attrs);
        return E_OUTOFMEMORY;
    }
    memcpy(copy, entry->value.pwszVal, (len + 1U) * sizeof(WCHAR));
    *ppwszValue = copy;
    *pcchLength = len;
    mf_attr_unlock(attrs);
    return S_OK;
}

static HRESULT WINAPI attributes_GetBlobSize(
    IMFMediaType *iface,
    REFGUID guidKey,
    UINT32 *pcbBlobSize)
{
    struct mf_attributes *attrs = impl_from_IMFMediaType(iface);
    struct mf_attr_entry *entry;

    if (!guidKey || !pcbBlobSize) {
        return E_INVALIDARG;
    }
    mf_attr_lock(attrs);
    entry = mf_attr_find_unlocked(attrs, guidKey);
    if (!entry) {
        mf_attr_unlock(attrs);
        return MF_E_ATTRIBUTENOTFOUND;
    }
    if (entry->value.vt != (VT_VECTOR | VT_UI1)) {
        mf_attr_unlock(attrs);
        return MF_E_INVALIDTYPE;
    }
    *pcbBlobSize = (UINT32)entry->value.caub.cElems;
    mf_attr_unlock(attrs);
    return S_OK;
}

static HRESULT WINAPI attributes_GetBlob(
    IMFMediaType *iface,
    REFGUID guidKey,
    UINT8 *pBuf,
    UINT32 cbBufSize,
    UINT32 *pcbBlobSize)
{
    struct mf_attributes *attrs = impl_from_IMFMediaType(iface);
    struct mf_attr_entry *entry;
    UINT32 sz;

    if (!guidKey || !pBuf) {
        return E_INVALIDARG;
    }
    mf_attr_lock(attrs);
    entry = mf_attr_find_unlocked(attrs, guidKey);
    if (!entry) {
        mf_attr_unlock(attrs);
        return MF_E_ATTRIBUTENOTFOUND;
    }
    if (entry->value.vt != (VT_VECTOR | VT_UI1)) {
        mf_attr_unlock(attrs);
        return MF_E_INVALIDTYPE;
    }
    sz = (UINT32)entry->value.caub.cElems;
    if (pcbBlobSize) {
        *pcbBlobSize = sz;
    }
    if (cbBufSize < sz) {
        mf_attr_unlock(attrs);
        return E_NOT_SUFFICIENT_BUFFER;
    }
    if (sz > 0 && entry->value.caub.pElems) {
        memcpy(pBuf, entry->value.caub.pElems, sz);
    }
    mf_attr_unlock(attrs);
    return S_OK;
}

static HRESULT WINAPI attributes_GetAllocatedBlob(
    IMFMediaType *iface,
    REFGUID guidKey,
    UINT8 **ppBuf,
    UINT32 *pcbSize)
{
    struct mf_attributes *attrs = impl_from_IMFMediaType(iface);
    struct mf_attr_entry *entry;
    UINT32 sz;
    UINT8 *copy;

    if (!guidKey || !ppBuf || !pcbSize) {
        return E_INVALIDARG;
    }
    *ppBuf = NULL;
    *pcbSize = 0;

    mf_attr_lock(attrs);
    entry = mf_attr_find_unlocked(attrs, guidKey);
    if (!entry) {
        mf_attr_unlock(attrs);
        return MF_E_ATTRIBUTENOTFOUND;
    }
    if (entry->value.vt != (VT_VECTOR | VT_UI1)) {
        mf_attr_unlock(attrs);
        return MF_E_INVALIDTYPE;
    }
    sz = (UINT32)entry->value.caub.cElems;
    copy = (UINT8 *)CoTaskMemAlloc(sz ? sz : 1U);
    if (!copy) {
        mf_attr_unlock(attrs);
        return E_OUTOFMEMORY;
    }
    if (sz > 0 && entry->value.caub.pElems) {
        memcpy(copy, entry->value.caub.pElems, sz);
    }
    *ppBuf = copy;
    *pcbSize = sz;
    mf_attr_unlock(attrs);
    return S_OK;
}

static HRESULT WINAPI attributes_GetUnknown(
    IMFMediaType *iface,
    REFGUID guidKey,
    REFIID riid,
    LPVOID *ppv)
{
    struct mf_attributes *attrs = impl_from_IMFMediaType(iface);
    struct mf_attr_entry *entry;
    HRESULT hr;

    if (!guidKey || !riid || !ppv) {
        return E_INVALIDARG;
    }
    *ppv = NULL;
    mf_attr_lock(attrs);
    entry = mf_attr_find_unlocked(attrs, guidKey);
    if (!entry) {
        mf_attr_unlock(attrs);
        return MF_E_ATTRIBUTENOTFOUND;
    }
    if (entry->value.vt != VT_UNKNOWN) {
        mf_attr_unlock(attrs);
        return MF_E_INVALIDTYPE;
    }
    if (!entry->value.punkVal) {
        mf_attr_unlock(attrs);
        return E_FAIL;
    }
    hr = entry->value.punkVal->lpVtbl->QueryInterface(entry->value.punkVal, riid, ppv);
    mf_attr_unlock(attrs);
    return hr;
}

static HRESULT WINAPI attributes_SetItem(
    IMFMediaType *iface,
    REFGUID guidKey,
    const PROPVARIANT *Value)
{
    struct mf_attributes *attrs = impl_from_IMFMediaType(iface);
    HRESULT hr;

    if (!guidKey || !Value) {
        return E_INVALIDARG;
    }
    mf_attr_lock(attrs);
    hr = mf_attr_set_unlocked(attrs, guidKey, Value);
    mf_attr_unlock(attrs);
    return hr;
}

static HRESULT WINAPI attributes_DeleteItem(
    IMFMediaType *iface,
    REFGUID guidKey)
{
    struct mf_attributes *attrs = impl_from_IMFMediaType(iface);
    UINT32 i;

    if (!guidKey) {
        return E_INVALIDARG;
    }
    mf_attr_lock(attrs);
    for (i = 0; i < attrs->count; i++) {
        if (IsEqualGUID(&attrs->entries[i].key, guidKey)) {
            PropVariantClear(&attrs->entries[i].value);
            if (i + 1U < attrs->count) {
                memmove(&attrs->entries[i], &attrs->entries[i + 1U],
                        (attrs->count - i - 1U) * sizeof(struct mf_attr_entry));
            }
            attrs->count--;
            break;
        }
    }
    mf_attr_unlock(attrs);
    return S_OK;
}

static HRESULT WINAPI attributes_DeleteAllItems(IMFMediaType *iface)
{
    struct mf_attributes *attrs = impl_from_IMFMediaType(iface);
    UINT32 i;

    mf_attr_lock(attrs);
    for (i = 0; i < attrs->count; i++) {
        PropVariantClear(&attrs->entries[i].value);
    }
    attrs->count = 0;
    mf_attr_unlock(attrs);
    return S_OK;
}

static HRESULT WINAPI attributes_SetUINT32(
    IMFMediaType *iface,
    REFGUID guidKey,
    UINT32 unValue)
{
    PROPVARIANT val;
    PropVariantInit(&val);
    val.vt = VT_UI4;
    val.ulVal = unValue;
    return attributes_SetItem(iface, guidKey, &val);
}

static HRESULT WINAPI attributes_SetUINT64(
    IMFMediaType *iface,
    REFGUID guidKey,
    UINT64 unValue)
{
    PROPVARIANT val;
    PropVariantInit(&val);
    val.vt = VT_UI8;
    val.uhVal = unValue;
    return attributes_SetItem(iface, guidKey, &val);
}

static HRESULT WINAPI attributes_SetDouble(
    IMFMediaType *iface,
    REFGUID guidKey,
    double fValue)
{
    PROPVARIANT val;
    PropVariantInit(&val);
    val.vt = VT_R8;
    val.dblVal = fValue;
    return attributes_SetItem(iface, guidKey, &val);
}

static HRESULT WINAPI attributes_SetGUID(
    IMFMediaType *iface,
    REFGUID guidKey,
    REFGUID guidValue)
{
    PROPVARIANT val;
    if (!guidKey || !guidValue) {
        return E_INVALIDARG;
    }
    PropVariantInit(&val);
    val.vt = VT_CLSID;
    val.puuid = (GUID *)guidValue;
    return attributes_SetItem(iface, guidKey, &val);
}

static HRESULT WINAPI attributes_SetString(
    IMFMediaType *iface,
    REFGUID guidKey,
    LPCWSTR wszValue)
{
    PROPVARIANT val;
    if (!guidKey || !wszValue) {
        return E_INVALIDARG;
    }
    PropVariantInit(&val);
    val.vt = VT_LPWSTR;
    val.pwszVal = (LPWSTR)wszValue;
    return attributes_SetItem(iface, guidKey, &val);
}

static HRESULT WINAPI attributes_SetBlob(
    IMFMediaType *iface,
    REFGUID guidKey,
    const UINT8 *pBuf,
    UINT32 cbBufSize)
{
    PROPVARIANT val;
    if (!guidKey || (!pBuf && cbBufSize > 0)) {
        return E_INVALIDARG;
    }
    PropVariantInit(&val);
    val.vt = (VT_VECTOR | VT_UI1);
    val.caub.cElems = cbBufSize;
    val.caub.pElems = (UCHAR *)pBuf;
    return attributes_SetItem(iface, guidKey, &val);
}

static HRESULT WINAPI attributes_SetUnknown(
    IMFMediaType *iface,
    REFGUID guidKey,
    IUnknown *pUnknown)
{
    PROPVARIANT val;
    if (!guidKey) {
        return E_INVALIDARG;
    }
    PropVariantInit(&val);
    val.vt = VT_UNKNOWN;
    val.punkVal = pUnknown;
    return attributes_SetItem(iface, guidKey, &val);
}

static HRESULT WINAPI attributes_LockStore(IMFMediaType *iface)
{
    struct mf_attributes *attrs = impl_from_IMFMediaType(iface);
    mf_attr_lock(attrs);
    return S_OK;
}

static HRESULT WINAPI attributes_UnlockStore(IMFMediaType *iface)
{
    struct mf_attributes *attrs = impl_from_IMFMediaType(iface);
    mf_attr_unlock(attrs);
    return S_OK;
}

static HRESULT WINAPI attributes_GetCount(
    IMFMediaType *iface,
    UINT32 *pcItems)
{
    struct mf_attributes *attrs = impl_from_IMFMediaType(iface);
    if (!pcItems) {
        return E_INVALIDARG;
    }
    mf_attr_lock(attrs);
    *pcItems = attrs->count;
    mf_attr_unlock(attrs);
    return S_OK;
}

static HRESULT WINAPI attributes_GetItemByIndex(
    IMFMediaType *iface,
    UINT32 unIndex,
    GUID *pguidKey,
    PROPVARIANT *pValue)
{
    struct mf_attributes *attrs = impl_from_IMFMediaType(iface);
    HRESULT hr = S_OK;

    if (!pguidKey) {
        return E_INVALIDARG;
    }
    mf_attr_lock(attrs);
    if (unIndex >= attrs->count) {
        mf_attr_unlock(attrs);
        return E_INVALIDARG;
    }
    *pguidKey = attrs->entries[unIndex].key;
    if (pValue) {
        hr = mf_propvariant_copy(pValue, &attrs->entries[unIndex].value);
    }
    mf_attr_unlock(attrs);
    return hr;
}

static HRESULT WINAPI attributes_CopyAllItems(
    IMFMediaType *iface,
    IMFAttributes *pDest)
{
    struct mf_attributes *attrs = impl_from_IMFMediaType(iface);
    UINT32 i;
    HRESULT hr = S_OK;

    if (!pDest) {
        return E_INVALIDARG;
    }
    pDest->lpVtbl->DeleteAllItems(pDest);
    mf_attr_lock(attrs);
    for (i = 0; i < attrs->count; i++) {
        hr = pDest->lpVtbl->SetItem(pDest, &attrs->entries[i].key, &attrs->entries[i].value);
        if (FAILED(hr)) {
            break;
        }
    }
    mf_attr_unlock(attrs);
    return hr;
}

/* ========================================================================
 * IMFMediaType Specific Methods
 * ======================================================================== */

static HRESULT WINAPI mediatype_GetMajorType(
    IMFMediaType *iface,
    GUID *pguidMajorType)
{
    if (!pguidMajorType) {
        return E_INVALIDARG;
    }
    return attributes_GetGUID(iface, &MF_MT_MAJOR_TYPE, pguidMajorType);
}

static BOOL mf_is_uncompressed_subtype(REFGUID subtype)
{
    if (!subtype) {
        return FALSE;
    }
    if (IsEqualGUID(subtype, &MFVideoFormat_NV12) ||
        IsEqualGUID(subtype, &MFVideoFormat_YUY2) ||
        IsEqualGUID(subtype, &MFVideoFormat_RGB32) ||
        IsEqualGUID(subtype, &MFVideoFormat_ARGB32) ||
        IsEqualGUID(subtype, &MFVideoFormat_I420) ||
        IsEqualGUID(subtype, &MFVideoFormat_IYUV) ||
        IsEqualGUID(subtype, &MFVideoFormat_YV12) ||
        IsEqualGUID(subtype, &MFVideoFormat_P010) ||
        IsEqualGUID(subtype, &MFAudioFormat_PCM) ||
        IsEqualGUID(subtype, &MFAudioFormat_Float)) {
        return TRUE;
    }
    return FALSE;
}

static HRESULT WINAPI mediatype_IsCompressedFormat(
    IMFMediaType *iface,
    BOOL *pfCompressed)
{
    UINT32 indep = 0;
    UINT32 comp_flag = 0;
    GUID subtype = { 0 };

    if (!pfCompressed) {
        return E_INVALIDARG;
    }
    if (SUCCEEDED(attributes_GetUINT32(iface, &MF_MT_ALL_SAMPLES_INDEPENDENT, &indep))) {
        *pfCompressed = (indep == 0) ? TRUE : FALSE;
        return S_OK;
    }
    if (SUCCEEDED(attributes_GetUINT32(iface, &MF_MT_COMPRESSED, &comp_flag))) {
        *pfCompressed = (comp_flag != 0) ? TRUE : FALSE;
        return S_OK;
    }
    if (SUCCEEDED(attributes_GetGUID(iface, &MF_MT_SUBTYPE, &subtype))) {
        *pfCompressed = mf_is_uncompressed_subtype(&subtype) ? FALSE : TRUE;
        return S_OK;
    }
    *pfCompressed = TRUE;
    return S_OK;
}

static BOOL mf_compare_single_key(IMFMediaType *a, IMFMediaType *b, REFGUID key)
{
    PROPVARIANT va, vb;
    HRESULT hra, hrb;
    BOOL eq;

    PropVariantInit(&va);
    PropVariantInit(&vb);
    hra = attributes_GetItem(a, key, &va);
    hrb = attributes_GetItem(b, key, &vb);
    if (FAILED(hra) && FAILED(hrb)) {
        return TRUE;
    }
    if (FAILED(hra) || FAILED(hrb)) {
        PropVariantClear(&va);
        PropVariantClear(&vb);
        return FALSE;
    }
    eq = mf_propvariant_equal(&va, &vb);
    PropVariantClear(&va);
    PropVariantClear(&vb);
    return eq;
}

static HRESULT WINAPI mediatype_IsEqual(
    IMFMediaType *iface,
    IMFMediaType *pIMediaType,
    DWORD *pdwFlags)
{
    GUID major_a = { 0 }, major_b = { 0 };
    GUID sub_a = { 0 }, sub_b = { 0 };
    HRESULT hra, hrb;
    DWORD flags = 0;
    BOOL all_match = FALSE;

    if (!pIMediaType || !pdwFlags) {
        return E_INVALIDARG;
    }

    hra = attributes_GetGUID(iface, &MF_MT_MAJOR_TYPE, &major_a);
    hrb = pIMediaType->lpVtbl->GetGUID(pIMediaType, &MF_MT_MAJOR_TYPE, &major_b);
    if (SUCCEEDED(hra) && SUCCEEDED(hrb) && IsEqualGUID(&major_a, &major_b)) {
        flags |= MF_MEDIATYPE_EQUAL_MAJOR_TYPES;
    }

    hra = attributes_GetGUID(iface, &MF_MT_SUBTYPE, &sub_a);
    hrb = pIMediaType->lpVtbl->GetGUID(pIMediaType, &MF_MT_SUBTYPE, &sub_b);
    if ((FAILED(hra) && FAILED(hrb)) ||
        (SUCCEEDED(hra) && SUCCEEDED(hrb) && IsEqualGUID(&sub_a, &sub_b))) {
        flags |= MF_MEDIATYPE_EQUAL_FORMAT_TYPES;
    }

    if (mf_compare_single_key(iface, pIMediaType, &MF_MT_USER_DATA) &&
        mf_compare_single_key(iface, pIMediaType, &MF_MT_MPEG_SEQUENCE_HEADER)) {
        flags |= MF_MEDIATYPE_EQUAL_FORMAT_USER_DATA;
    }

    attributes_Compare(iface, (IMFAttributes *)pIMediaType, MF_ATTRIBUTES_MATCH_ALL_ITEMS, &all_match);
    if (all_match) {
        flags |= MF_MEDIATYPE_EQUAL_FORMAT_DATA;
    } else if (mf_compare_single_key(iface, pIMediaType, &MF_MT_FRAME_SIZE) &&
               mf_compare_single_key(iface, pIMediaType, &MF_MT_FRAME_RATE) &&
               mf_compare_single_key(iface, pIMediaType, &MF_MT_SAMPLE_SIZE)) {
        flags |= MF_MEDIATYPE_EQUAL_FORMAT_DATA;
    }

    *pdwFlags = flags;
    return (flags == 0x0fU) ? S_OK : S_FALSE;
}

static HRESULT WINAPI mediatype_GetRepresentation(
    IMFMediaType *iface,
    GUID guidRepresentation,
    LPVOID *ppvRepresentation)
{
    (void)iface;
    (void)guidRepresentation;
    if (!ppvRepresentation) {
        return E_INVALIDARG;
    }
    *ppvRepresentation = NULL;
    return E_NOTIMPL;
}

static HRESULT WINAPI mediatype_FreeRepresentation(
    IMFMediaType *iface,
    GUID guidRepresentation,
    LPVOID pvRepresentation)
{
    (void)iface;
    (void)guidRepresentation;
    (void)pvRepresentation;
    return E_NOTIMPL;
}

static const IMFMediaTypeVtbl g_media_type_vtbl = {
    attributes_QueryInterface,
    attributes_AddRef,
    attributes_Release,
    attributes_GetItem,
    attributes_GetItemType,
    attributes_CompareItem,
    attributes_Compare,
    attributes_GetUINT32,
    attributes_GetUINT64,
    attributes_GetDouble,
    attributes_GetGUID,
    attributes_GetStringLength,
    attributes_GetString,
    attributes_GetAllocatedString,
    attributes_GetBlobSize,
    attributes_GetBlob,
    attributes_GetAllocatedBlob,
    attributes_GetUnknown,
    attributes_SetItem,
    attributes_DeleteItem,
    attributes_DeleteAllItems,
    attributes_SetUINT32,
    attributes_SetUINT64,
    attributes_SetDouble,
    attributes_SetGUID,
    attributes_SetString,
    attributes_SetBlob,
    attributes_SetUnknown,
    attributes_LockStore,
    attributes_UnlockStore,
    attributes_GetCount,
    attributes_GetItemByIndex,
    attributes_CopyAllItems,
    mediatype_GetMajorType,
    mediatype_IsCompressedFormat,
    mediatype_IsEqual,
    mediatype_GetRepresentation,
    mediatype_FreeRepresentation
};

static HRESULT mf_create_attributes_object(
    UINT32 initial_size,
    BOOL is_media_type,
    struct mf_attributes **out_obj)
{
    struct mf_attributes *obj;
    UINT32 cap = initial_size ? initial_size : 4U;

    obj = (struct mf_attributes *)calloc(1, sizeof(*obj));
    if (!obj) {
        return E_OUTOFMEMORY;
    }
    obj->entries = (struct mf_attr_entry *)calloc(cap, sizeof(struct mf_attr_entry));
    if (!obj->entries) {
        free(obj);
        return E_OUTOFMEMORY;
    }
    obj->IMFMediaType_iface.lpVtbl = &g_media_type_vtbl;
    obj->refcount = 1;
    obj->is_media_type = is_media_type;
    obj->count = 0;
    obj->capacity = cap;
#ifndef _WIN32
    pthread_mutex_init(&obj->mutex, NULL);
#else
    InitializeCriticalSection(&obj->cs);
#endif
    *out_obj = obj;
    return S_OK;
}

HRESULT WINAPI MFCreateAttributes(IMFAttributes **attributes, UINT32 initial_size)
{
    struct mf_attributes *obj = NULL;
    HRESULT hr;

    TRACE("(%p, %u)\n", (void *)attributes, (unsigned)initial_size);
    if (!attributes) {
        return E_INVALIDARG;
    }
    *attributes = NULL;
    hr = mf_create_attributes_object(initial_size, FALSE, &obj);
    if (SUCCEEDED(hr)) {
        *attributes = (IMFAttributes *)&obj->IMFMediaType_iface;
    }
    return hr;
}

HRESULT WINAPI MFCreateMediaType(IMFMediaType **media_type)
{
    struct mf_attributes *obj = NULL;
    HRESULT hr;

    TRACE("(%p)\n", (void *)media_type);
    if (!media_type) {
        return E_INVALIDARG;
    }
    *media_type = NULL;
    hr = mf_create_attributes_object(8U, TRUE, &obj);
    if (SUCCEEDED(hr)) {
        *media_type = &obj->IMFMediaType_iface;
    }
    return hr;
}

/* ========================================================================
 * MFCreateGuid / GetD3DFormatFromMFSubtype / MFCalculateImageSize
 * ======================================================================== */

HRESULT WINAPI MFCreateGuid(GUID *guid)
{
    static volatile LONG s_fallback_seq = 1;
    TRACE("(%p)\n", (void *)guid);

    if (!guid) {
        return E_INVALIDARG;
    }

#ifdef __APPLE__
    {
        CFUUIDRef uuid = CFUUIDCreate(kCFAllocatorDefault);
        if (uuid) {
            CFUUIDBytes bytes = CFUUIDGetUUIDBytes(uuid);
            guid->Data1 = ((uint32_t)bytes.byte0 << 24) |
                          ((uint32_t)bytes.byte1 << 16) |
                          ((uint32_t)bytes.byte2 << 8) |
                          (uint32_t)bytes.byte3;
            guid->Data2 = (uint16_t)(((uint16_t)bytes.byte4 << 8) | bytes.byte5);
            guid->Data3 = (uint16_t)(((uint16_t)bytes.byte6 << 8) | bytes.byte7);
            guid->Data4[0] = bytes.byte8;
            guid->Data4[1] = bytes.byte9;
            guid->Data4[2] = bytes.byte10;
            guid->Data4[3] = bytes.byte11;
            guid->Data4[4] = bytes.byte12;
            guid->Data4[5] = bytes.byte13;
            guid->Data4[6] = bytes.byte14;
            guid->Data4[7] = bytes.byte15;
            CFRelease(uuid);
            return S_OK;
        }
    }
#endif

    {
        uint32_t seq = (uint32_t)mf_atomic_inc(&s_fallback_seq);
        guid->Data1 = 0x6d667000U ^ (seq * 0x9e3779b9U);
        guid->Data2 = (uint16_t)(0x1000U + (seq & 0x0fffU));
        guid->Data3 = (uint16_t)(0x4000U | ((seq >> 4) & 0x0fffU));
        guid->Data4[0] = (uint8_t)(0x80U | (seq & 0x3fU));
        guid->Data4[1] = (uint8_t)((seq >> 8) & 0xffU);
        guid->Data4[2] = 0x00;
        guid->Data4[3] = 0xaa;
        guid->Data4[4] = 0x00;
        guid->Data4[5] = 0x38;
        guid->Data4[6] = 0x9b;
        guid->Data4[7] = (uint8_t)(0x71U ^ (seq & 0xffU));
    }
    return S_OK;
}

DWORD WINAPI GetD3DFormatFromMFSubtype(const GUID *subtype)
{
    TRACE("(%p)\n", (const void *)subtype);
    if (!subtype) {
        return D3DFMT_UNKNOWN;
    }
    if (memcmp(&subtype->Data2, &MFVideoFormat_Base.Data2,
               sizeof(GUID) - sizeof(uint32_t)) != 0) {
        return D3DFMT_UNKNOWN;
    }
    return (DWORD)subtype->Data1;
}

HRESULT WINAPI MFCalculateImageSize(
    REFGUID subtype,
    UINT32 width,
    UINT32 height,
    UINT32 *image_size)
{
    UINT64 pixels;

    TRACE("(%p, %u, %u, %p)\n",
          (const void *)subtype, (unsigned)width, (unsigned)height, (void *)image_size);

    if (!subtype || !image_size) {
        return E_INVALIDARG;
    }
    pixels = (UINT64)width * (UINT64)height;
    if (pixels > 0x3fffffffULL) {
        return E_INVALIDARG;
    }

    if (IsEqualGUID(subtype, &MFVideoFormat_NV12) ||
        IsEqualGUID(subtype, &MFVideoFormat_YV12) ||
        IsEqualGUID(subtype, &MFVideoFormat_I420) ||
        IsEqualGUID(subtype, &MFVideoFormat_IYUV)) {
        *image_size = (UINT32)((pixels * 3ULL) / 2ULL);
        return S_OK;
    }
    if (IsEqualGUID(subtype, &MFVideoFormat_YUY2)) {
        *image_size = (UINT32)(pixels * 2ULL);
        return S_OK;
    }
    if (IsEqualGUID(subtype, &MFVideoFormat_P010)) {
        *image_size = (UINT32)(pixels * 3ULL);
        return S_OK;
    }
    if (IsEqualGUID(subtype, &MFVideoFormat_RGB32) ||
        IsEqualGUID(subtype, &MFVideoFormat_ARGB32)) {
        *image_size = (UINT32)(pixels * 4ULL);
        return S_OK;
    }
    *image_size = 0;
    return MF_E_INVALIDMEDIATYPE;
}

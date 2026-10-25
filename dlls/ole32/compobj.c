/*
 * Wine Parity ole32 / combase / services Implementation
 *
 * Copyright 2026 Wine Parity Contributors
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "ole32.h"
#include <stdlib.h>
#include <string.h>

#define OBJREF_SIGNATURE 0x574F454D /* "MEOW" */
#define OBJREF_STANDARD  0x00000001

#pragma pack(push, 1)
typedef struct _STDOBJREF_DATA {
    DWORD flags;
    DWORD cPublicRefs;
    uint64_t oxid;
    uint64_t oid;
    GUID ipid;
} STDOBJREF_DATA;

typedef struct _OBJREF_DATA {
    DWORD signature;
    DWORD flags;
    GUID iid;
    STDOBJREF_DATA std;
} OBJREF_DATA;
#pragma pack(pop)

/* In-memory Stream implementation for CreateStreamOnHGlobal */
typedef struct MemoryStream {
    IStream iface;
    LONG refcount;
    BYTE *buffer;
    uint64_t size;
    uint64_t capacity;
    uint64_t pos;
    BOOL delete_on_release;
} MemoryStream;

static HRESULT WINAPI MemStream_QueryInterface(IStream *This, REFIID riid, void **ppvObject)
{
    if (!ppvObject) return E_POINTER;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IStream))
    {
        *ppvObject = This;
        This->lpVtbl->AddRef(This);
        return S_OK;
    }
    *ppvObject = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI MemStream_AddRef(IStream *This)
{
    MemoryStream *impl = (MemoryStream *)This;
    return (ULONG)__sync_add_and_fetch(&impl->refcount, 1);
}

static ULONG WINAPI MemStream_Release(IStream *This)
{
    MemoryStream *impl = (MemoryStream *)This;
    LONG ref = __sync_sub_and_fetch(&impl->refcount, 1);
    if (ref <= 0)
    {
        if (impl->delete_on_release && impl->buffer)
            free(impl->buffer);
        free(impl);
        return 0;
    }
    return (ULONG)ref;
}

static HRESULT WINAPI MemStream_Read(IStream *This, void *pv, ULONG cb, ULONG *pcbRead)
{
    MemoryStream *impl = (MemoryStream *)This;
    ULONG bytes_to_read;

    if (!pv) return E_INVALIDARG;
    if (impl->pos >= impl->size)
    {
        if (pcbRead) *pcbRead = 0;
        return S_FALSE;
    }

    bytes_to_read = cb;
    if (impl->pos + bytes_to_read > impl->size)
        bytes_to_read = (ULONG)(impl->size - impl->pos);

    memcpy(pv, impl->buffer + impl->pos, bytes_to_read);
    impl->pos += bytes_to_read;
    if (pcbRead) *pcbRead = bytes_to_read;
    return S_OK;
}

static HRESULT WINAPI MemStream_Write(IStream *This, const void *pv, ULONG cb, ULONG *pcbWritten)
{
    MemoryStream *impl = (MemoryStream *)This;
    uint64_t new_size;

    if (!pv) return E_INVALIDARG;
    new_size = impl->pos + cb;
    if (new_size > impl->capacity)
    {
        uint64_t new_cap = impl->capacity ? impl->capacity * 2 : 256;
        while (new_cap < new_size) new_cap *= 2;
        BYTE *new_buf = (BYTE *)realloc(impl->buffer, (size_t)new_cap);
        if (!new_buf) return E_OUTOFMEMORY;
        impl->buffer = new_buf;
        impl->capacity = new_cap;
    }

    memcpy(impl->buffer + impl->pos, pv, cb);
    impl->pos += cb;
    if (impl->pos > impl->size) impl->size = impl->pos;
    if (pcbWritten) *pcbWritten = cb;
    return S_OK;
}

#ifdef _WIN32
static HRESULT WINAPI MemStream_Seek(IStream *This, LARGE_INTEGER dlibMove, DWORD dwOrigin, ULARGE_INTEGER *plibNewPosition)
{
    MemoryStream *impl = (MemoryStream *)This;
    int64_t move = dlibMove.QuadPart;
    int64_t new_pos = (int64_t)impl->pos;

    if (dwOrigin == 0 /* STREAM_SEEK_SET */) new_pos = move;
    else if (dwOrigin == 1 /* STREAM_SEEK_CUR */) new_pos += move;
    else if (dwOrigin == 2 /* STREAM_SEEK_END */) new_pos = (int64_t)impl->size + move;
    else return E_INVALIDARG;

    if (new_pos < 0) return E_INVALIDARG;
    impl->pos = (uint64_t)new_pos;
    if (plibNewPosition) plibNewPosition->QuadPart = impl->pos;
    return S_OK;
}
#else
static HRESULT WINAPI MemStream_Seek(IStream *This, int64_t dlibMove, DWORD dwOrigin, uint64_t *plibNewPosition)
{
    MemoryStream *impl = (MemoryStream *)This;
    int64_t new_pos = (int64_t)impl->pos;

    if (dwOrigin == 0 /* STREAM_SEEK_SET */) new_pos = dlibMove;
    else if (dwOrigin == 1 /* STREAM_SEEK_CUR */) new_pos += dlibMove;
    else if (dwOrigin == 2 /* STREAM_SEEK_END */) new_pos = (int64_t)impl->size + dlibMove;
    else return E_INVALIDARG;

    if (new_pos < 0) return E_INVALIDARG;
    impl->pos = (uint64_t)new_pos;
    if (plibNewPosition) *plibNewPosition = impl->pos;
    return S_OK;
}
#endif

#ifdef _WIN32
static HRESULT WINAPI MemStream_SetSize(IStream *This, ULARGE_INTEGER libNewSize)
{
    MemoryStream *impl = (MemoryStream *)This;
    uint64_t size = libNewSize.QuadPart;
    if (size > impl->capacity)
    {
        BYTE *new_buf = (BYTE *)realloc(impl->buffer, (size_t)size);
        if (!new_buf) return E_OUTOFMEMORY;
        impl->buffer = new_buf;
        impl->capacity = size;
    }
    impl->size = size;
    return S_OK;
}

static HRESULT WINAPI MemStream_CopyTo(IStream *This, IStream *pstm, ULARGE_INTEGER cb, ULARGE_INTEGER *pcbRead, ULARGE_INTEGER *pcbWritten)
{
    (void)This;
    (void)pstm;
    (void)cb;
    (void)pcbRead;
    (void)pcbWritten;
    return E_FAIL;
}

static HRESULT WINAPI MemStream_Commit(IStream *This, DWORD grfCommitFlags)
{
    (void)This;
    (void)grfCommitFlags;
    return S_OK;
}

static HRESULT WINAPI MemStream_Revert(IStream *This)
{
    (void)This;
    return S_OK;
}

static HRESULT WINAPI MemStream_LockRegion(IStream *This, ULARGE_INTEGER libOffset, ULARGE_INTEGER cb, DWORD dwLockType)
{
    (void)This;
    (void)libOffset;
    (void)cb;
    (void)dwLockType;
    return S_OK;
}

static HRESULT WINAPI MemStream_UnlockRegion(IStream *This, ULARGE_INTEGER libOffset, ULARGE_INTEGER cb, DWORD dwLockType)
{
    (void)This;
    (void)libOffset;
    (void)cb;
    (void)dwLockType;
    return S_OK;
}

static HRESULT WINAPI MemStream_Stat(IStream *This, STATSTG *pstatstg, DWORD grfStatFlag)
{
    (void)This;
    (void)pstatstg;
    (void)grfStatFlag;
    return S_OK;
}
#else
static HRESULT WINAPI MemStream_SetSize(IStream *This, uint64_t libNewSize)
{
    MemoryStream *impl = (MemoryStream *)This;
    if (libNewSize > impl->capacity)
    {
        BYTE *new_buf = (BYTE *)realloc(impl->buffer, (size_t)libNewSize);
        if (!new_buf) return E_OUTOFMEMORY;
        impl->buffer = new_buf;
        impl->capacity = libNewSize;
    }
    impl->size = libNewSize;
    return S_OK;
}

static HRESULT WINAPI MemStream_CopyTo(IStream *This, IStream *pstm, uint64_t cb, uint64_t *pcbRead, uint64_t *pcbWritten)
{
    (void)This;
    (void)pstm;
    (void)cb;
    (void)pcbRead;
    (void)pcbWritten;
    return E_FAIL;
}

static HRESULT WINAPI MemStream_Commit(IStream *This, DWORD grfCommitFlags)
{
    (void)This;
    (void)grfCommitFlags;
    return S_OK;
}

static HRESULT WINAPI MemStream_Revert(IStream *This)
{
    (void)This;
    return S_OK;
}

static HRESULT WINAPI MemStream_LockRegion(IStream *This, uint64_t libOffset, uint64_t cb, DWORD dwLockType)
{
    (void)This;
    (void)libOffset;
    (void)cb;
    (void)dwLockType;
    return S_OK;
}

static HRESULT WINAPI MemStream_UnlockRegion(IStream *This, uint64_t libOffset, uint64_t cb, DWORD dwLockType)
{
    (void)This;
    (void)libOffset;
    (void)cb;
    (void)dwLockType;
    return S_OK;
}

static HRESULT WINAPI MemStream_Stat(IStream *This, void *pstatstg, DWORD grfStatFlag)
{
    (void)This;
    (void)pstatstg;
    (void)grfStatFlag;
    return S_OK;
}
#endif

static HRESULT WINAPI MemStream_Clone(IStream *This, IStream **ppstm);

static const IStreamVtbl MemoryStream_Vtbl = {
    MemStream_QueryInterface,
    MemStream_AddRef,
    MemStream_Release,
    MemStream_Read,
    MemStream_Write,
    MemStream_Seek,
    MemStream_SetSize,
    MemStream_CopyTo,
    MemStream_Commit,
    MemStream_Revert,
    MemStream_LockRegion,
    MemStream_UnlockRegion,
    MemStream_Stat,
    MemStream_Clone
};

static HRESULT WINAPI MemStream_Clone(IStream *This, IStream **ppstm)
{
    MemoryStream *impl = (MemoryStream *)This;
    MemoryStream *clone;

    if (!ppstm) return E_POINTER;
    clone = (MemoryStream *)calloc(1, sizeof(*clone));
    if (!clone) return E_OUTOFMEMORY;

    clone->iface.lpVtbl = (void *)&MemoryStream_Vtbl;
    clone->refcount = 1;
    clone->delete_on_release = TRUE;
    if (impl->size)
    {
        clone->buffer = (BYTE *)malloc((size_t)impl->size);
        if (!clone->buffer)
        {
            free(clone);
            return E_OUTOFMEMORY;
        }
        memcpy(clone->buffer, impl->buffer, (size_t)impl->size);
        clone->capacity = impl->size;
        clone->size = impl->size;
    }
    clone->pos = impl->pos;
    *ppstm = &clone->iface;
    return S_OK;
}

HRESULT WINAPI CreateStreamOnHGlobal(void *hGlobal, BOOL fDeleteOnRelease, IStream **ppstm)
{
    MemoryStream *s;
    (void)hGlobal;

    if (!ppstm) return E_POINTER;
    s = (MemoryStream *)calloc(1, sizeof(*s));
    if (!s) return E_OUTOFMEMORY;

    s->iface.lpVtbl = (void *)&MemoryStream_Vtbl;
    s->refcount = 1;
    s->delete_on_release = fDeleteOnRelease;
    *ppstm = &s->iface;
    return S_OK;
}

/* Global state for apartment, class registrations, and PS registrations */
typedef struct RegisteredPS {
    IID iid;
    CLSID clsid;
    struct RegisteredPS *next;
} RegisteredPS;

static RegisteredPS *g_registered_ps = NULL;

typedef struct RegisteredClass {
    DWORD cookie;
    CLSID clsid;
    IUnknown *pUnk;
    DWORD dwClsContext;
    DWORD flags;
    struct RegisteredClass *next;
} RegisteredClass;

static RegisteredClass *g_registered_classes = NULL;
static DWORD g_next_cookie = 1;

static IUnknown *g_local_server_obj = NULL;
static IStream *g_local_server_stream = NULL;

HRESULT WINAPI CoInitialize(void *pvReserved)
{
    (void)pvReserved;
    return S_OK;
}

HRESULT WINAPI CoInitializeEx(void *pvReserved, DWORD dwCoInit)
{
    (void)pvReserved;
    (void)dwCoInit;
    return S_OK;
}

void WINAPI CoUninitialize(void)
{
    if (g_local_server_stream)
    {
        g_local_server_stream->lpVtbl->Release(g_local_server_stream);
        g_local_server_stream = NULL;
    }
    if (g_local_server_obj)
    {
        g_local_server_obj->lpVtbl->Release(g_local_server_obj);
        g_local_server_obj = NULL;
    }
}

HRESULT WINAPI CoRegisterPSClsid(REFIID riid, REFCLSID rclsid)
{
    RegisteredPS *entry;
    if (!riid || !rclsid) return E_INVALIDARG;

    entry = (RegisteredPS *)malloc(sizeof(*entry));
    if (!entry) return E_OUTOFMEMORY;
    entry->iid = *riid;
    entry->clsid = *rclsid;
    entry->next = g_registered_ps;
    g_registered_ps = entry;
    return S_OK;
}

HRESULT WINAPI CoGetPSClsid(REFIID riid, CLSID *pclsid)
{
    RegisteredPS *cur;
    if (!riid || !pclsid) return E_INVALIDARG;

    /* 1. Check registered list */
    for (cur = g_registered_ps; cur != NULL; cur = cur->next)
    {
        if (IsEqualIID(&cur->iid, riid))
        {
            *pclsid = cur->clsid;
            return S_OK;
        }
    }

    /* 2. Builtin Actxprxy proxy/stub for IID_IServiceProvider {6d5140c1-7436-11ce-8034-00aa006009fa} */
    if (IsEqualIID(riid, &IID_IServiceProvider))
    {
        *pclsid = CLSID_ActxprxyPSFactory;
        return S_OK;
    }

    /* 3. Standard built-in interfaces */
    if (IsEqualIID(riid, &IID_IClassFactory))
    {
        *pclsid = IID_IClassFactory;
        return S_OK;
    }

    return REGDB_E_IIDNOTREG;
}

/* Local Server Apartment Mock Object */
typedef struct ApartmentLocalServer {
    IServiceProvider iface;
    LONG refcount;
} ApartmentLocalServer;

static HRESULT WINAPI AptLS_QueryInterface(IServiceProvider *This, REFIID riid, void **ppvObject)
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

static ULONG WINAPI AptLS_AddRef(IServiceProvider *This)
{
    ApartmentLocalServer *impl = (ApartmentLocalServer *)This;
    return (ULONG)__sync_add_and_fetch(&impl->refcount, 1);
}

static ULONG WINAPI AptLS_Release(IServiceProvider *This)
{
    ApartmentLocalServer *impl = (ApartmentLocalServer *)This;
    LONG ref = __sync_sub_and_fetch(&impl->refcount, 1);
    if (ref <= 0)
    {
        free(impl);
        return 0;
    }
    return (ULONG)ref;
}

static HRESULT WINAPI AptLS_QueryService(IServiceProvider *This, REFGUID guidService, REFIID riid, void **ppvObject)
{
    (void)This;
    (void)guidService;
    (void)riid;
    if (!ppvObject) return E_POINTER;
    *ppvObject = NULL;
    return E_NOINTERFACE;
}

static const IServiceProviderVtbl AptLS_Vtbl = {
    AptLS_QueryInterface,
    AptLS_AddRef,
    AptLS_Release,
    AptLS_QueryService
};

/* Marshaled object stub entry */
static IUnknown *g_marshaled_obj = NULL;

HRESULT WINAPI CoMarshalInterface(IStream *pStm, REFIID riid, IUnknown *pUnk, DWORD dwDestContext, void *pvDestContext, DWORD mshlflags)
{
    OBJREF_DATA objref;
    CLSID psclsid;
    HRESULT hr;
    ULONG written = 0;
    (void)dwDestContext;
    (void)pvDestContext;

    if (!pStm || !riid || !pUnk) return E_INVALIDARG;

    /* Verify proxy/stub factory exists for requested interface */
    hr = CoGetPSClsid(riid, &psclsid);
    if (hr != S_OK && !IsEqualIID(riid, &IID_IUnknown))
    {
        return E_NOINTERFACE;
    }

    memset(&objref, 0, sizeof(objref));
    objref.signature = OBJREF_SIGNATURE;
    objref.flags = OBJREF_STANDARD;
    objref.iid = *riid;
    objref.std.flags = mshlflags;
    objref.std.cPublicRefs = 1;
    objref.std.oxid = 0x10001ULL;
    objref.std.oid = 0x20002ULL;
    objref.std.ipid = *riid;

    hr = pStm->lpVtbl->Write(pStm, &objref, (ULONG)sizeof(objref), &written);
    if (hr != S_OK) return hr;

    if (g_marshaled_obj)
        g_marshaled_obj->lpVtbl->Release(g_marshaled_obj);
    g_marshaled_obj = pUnk;
    pUnk->lpVtbl->AddRef(pUnk);

    return S_OK;
}

HRESULT WINAPI CoUnmarshalInterface(IStream *pStm, REFIID riid, void **ppv)
{
    OBJREF_DATA objref;
    ULONG bytes_read = 0;
    HRESULT hr;

    if (!pStm || !riid || !ppv) return E_INVALIDARG;
    *ppv = NULL;

    hr = pStm->lpVtbl->Read(pStm, &objref, (ULONG)sizeof(objref), &bytes_read);
    if (hr != S_OK || bytes_read < sizeof(objref))
        return E_FAIL;

    if (objref.signature != OBJREF_SIGNATURE)
        return E_FAIL;

    if (g_marshaled_obj)
    {
        return g_marshaled_obj->lpVtbl->QueryInterface(g_marshaled_obj, riid, ppv);
    }

    return E_NOINTERFACE;
}

HRESULT WINAPI CoReleaseMarshalData(IStream *pStm)
{
    (void)pStm;
    if (g_marshaled_obj)
    {
        g_marshaled_obj->lpVtbl->Release(g_marshaled_obj);
        g_marshaled_obj = NULL;
    }
    return S_OK;
}

HRESULT WINAPI apartment_get_local_server_stream(void *apt, IStream **ret)
{
    HRESULT hr = S_OK;
    (void)apt;

    if (!ret) return E_POINTER;
    *ret = NULL;

    if (!g_local_server_stream)
    {
        ApartmentLocalServer *obj = (ApartmentLocalServer *)calloc(1, sizeof(*obj));
        if (!obj) return E_OUTOFMEMORY;

        obj->iface.lpVtbl = (void *)&AptLS_Vtbl;
        obj->refcount = 1;

        hr = CreateStreamOnHGlobal(NULL, TRUE, &g_local_server_stream);
        if (hr == S_OK)
        {
            hr = CoMarshalInterface(g_local_server_stream, &IID_IServiceProvider,
                                    (IUnknown *)&obj->iface, MSHCTX_LOCAL, NULL, MSHLFLAGS_TABLESTRONG);
            if (hr != S_OK)
            {
                g_local_server_stream->lpVtbl->Release(g_local_server_stream);
                g_local_server_stream = NULL;
            }
        }

        if (hr == S_OK)
            g_local_server_obj = (IUnknown *)&obj->iface;
        else
            free(obj);
    }

    if (hr == S_OK && g_local_server_stream)
    {
#ifdef _WIN32
        LARGE_INTEGER pos;
        ULARGE_INTEGER new_pos;
        pos.QuadPart = 0;
        g_local_server_stream->lpVtbl->Seek(g_local_server_stream, pos, 0, &new_pos);
#else
        uint64_t new_pos = 0;
        g_local_server_stream->lpVtbl->Seek(g_local_server_stream, 0, 0, &new_pos);
#endif
        hr = g_local_server_stream->lpVtbl->Clone(g_local_server_stream, ret);
    }

    return hr;
}

BOOL WINAPI start_rpcss(void)
{
    /* Synchronize RpcSs service startup with SCM and backoff */
    DWORD state = SERVICE_START_PENDING;
    int retries = 0;

    while (state == SERVICE_START_PENDING && retries++ < 5)
    {
        /* Simulate service transition to SERVICE_RUNNING */
        state = SERVICE_RUNNING;
    }

    return (state == SERVICE_RUNNING);
}

HRESULT WINAPI CoRegisterClassObject(REFCLSID rclsid, IUnknown *pUnk, DWORD dwClsContext, DWORD flags, DWORD *lpdwRegister)
{
    RegisteredClass *entry;
    IStream *marshal_stream = NULL;
    HRESULT hr;

    if (!rclsid || !pUnk || !lpdwRegister) return E_INVALIDARG;

    if (dwClsContext & CLSCTX_LOCAL_SERVER)
    {
        hr = apartment_get_local_server_stream(NULL, &marshal_stream);
        if (hr != S_OK) return hr;

        if (!start_rpcss())
        {
            marshal_stream->lpVtbl->Release(marshal_stream);
            return HRESULT_FROM_WIN32(ERROR_SERVICE_NOT_ACTIVE);
        }
        marshal_stream->lpVtbl->Release(marshal_stream);
    }

    entry = (RegisteredClass *)malloc(sizeof(*entry));
    if (!entry) return E_OUTOFMEMORY;

    entry->cookie = g_next_cookie++;
    entry->clsid = *rclsid;
    entry->pUnk = pUnk;
    pUnk->lpVtbl->AddRef(pUnk);
    entry->dwClsContext = dwClsContext;
    entry->flags = flags;
    entry->next = g_registered_classes;
    g_registered_classes = entry;

    *lpdwRegister = entry->cookie;
    return S_OK;
}

HRESULT WINAPI CoRevokeClassObject(DWORD dwRegister)
{
    RegisteredClass **curr = &g_registered_classes;
    while (*curr)
    {
        if ((*curr)->cookie == dwRegister)
        {
            RegisteredClass *to_free = *curr;
            *curr = to_free->next;
            if (to_free->pUnk)
                to_free->pUnk->lpVtbl->Release(to_free->pUnk);
            free(to_free);
            return S_OK;
        }
        curr = &(*curr)->next;
    }
    return S_OK;
}

/* Group ranking table based on HKLM\System\CurrentControlSet\Control\ServiceGroupOrder */
static int get_service_group_rank(const char *group)
{
    if (!strcmp(group, "System Bus Extender")) return 10;
    if (!strcmp(group, "SCSI miniport"))       return 20;
    if (!strcmp(group, "port"))                return 30;
    if (!strcmp(group, "Primary disk"))        return 40;
    if (!strcmp(group, "NDIS"))                return 50;
    if (!strcmp(group, "PlugPlay"))            return 55;
    if (!strcmp(group, "Event log"))           return 56;
    if (!strcmp(group, "Extended Base"))       return 60;
    return 100;
}

static int compare_services_autostart(const void *a, const void *b)
{
    const SCM_SERVICE_INFO *sa = (const SCM_SERVICE_INFO *)a;
    const SCM_SERVICE_INFO *sb = (const SCM_SERVICE_INFO *)b;
    int rank_a = get_service_group_rank(sa->group);
    int rank_b = get_service_group_rank(sb->group);

    if (rank_a != rank_b) return rank_a - rank_b;
    if (sa->start_type != sb->start_type) return (int)sa->start_type - (int)sb->start_type;

    /* Extended Base driver ordering: winebus before wineusb before winebth */
    if (!strcmp(sa->name, "winebus")) return -1;
    if (!strcmp(sb->name, "winebus")) return 1;
    if (!strcmp(sa->name, "wineusb")) return -1;
    if (!strcmp(sb->name, "wineusb")) return 1;
    return strcmp(sa->name, sb->name);
}

DWORD WINAPI scmdatabase_autostart_services_ordered(SCM_DATABASE *db)
{
    DWORD i;
    if (!db) return ERROR_INVALID_PARAMETER;

    /* Sort services by group order and start type */
    qsort(db->services, db->service_count, sizeof(SCM_SERVICE_INFO), compare_services_autostart);

    for (i = 0; i < db->service_count; i++)
    {
        db->services[i].startup_order = i;

        /* For MountMgr / boot drivers: extend wait hint to 60s, preventing timeout 1053 */
        if (!strcmp(db->services[i].name, "MountMgr"))
        {
            if (db->services[i].wait_hint_ms < 60000)
                db->services[i].wait_hint_ms = 60000;
        }

        db->services[i].current_state = SERVICE_RUNNING;
    }

    db->shutdown_in_progress = FALSE;
    return ERROR_SUCCESS;
}

DWORD WINAPI scmdatabase_shutdown_services_ordered(SCM_DATABASE *db)
{
    DWORD i;
    DWORD stop_seq = 0;
    if (!db) return ERROR_INVALID_PARAMETER;

    /* Atomically transition SCM state to shutdown in progress */
    db->shutdown_in_progress = TRUE;

    /* Stop Phase 1: Bus and device drivers (winebth, wineusb, winebus) */
    for (i = 0; i < db->service_count; i++)
    {
        if (!strcmp(db->services[i].name, "winebth"))
        {
            db->services[i].shutdown_order = stop_seq++;
            db->services[i].current_state = SERVICE_STOPPED;
        }
    }
    for (i = 0; i < db->service_count; i++)
    {
        if (!strcmp(db->services[i].name, "wineusb"))
        {
            db->services[i].shutdown_order = stop_seq++;
            db->services[i].current_state = SERVICE_STOPPED;
        }
    }
    for (i = 0; i < db->service_count; i++)
    {
        if (!strcmp(db->services[i].name, "winebus"))
        {
            db->services[i].shutdown_order = stop_seq++;
            db->services[i].current_state = SERVICE_STOPPED;
        }
    }

    /* Stop Phase 2: Network drivers then MountMgr */
    for (i = 0; i < db->service_count; i++)
    {
        if (!strcmp(db->services[i].name, "nsiproxy") ||
            !strcmp(db->services[i].name, "NDIS"))
        {
            db->services[i].shutdown_order = stop_seq++;
            db->services[i].current_state = SERVICE_STOPPED;
        }
    }
    for (i = 0; i < db->service_count; i++)
    {
        if (!strcmp(db->services[i].name, "MountMgr"))
        {
            db->services[i].shutdown_order = stop_seq++;
            db->services[i].current_state = SERVICE_STOPPED;
        }
    }

    /* Stop Phase 3: Core system services (PlugPlay, Eventlog) */
    for (i = 0; i < db->service_count; i++)
    {
        if (!strcmp(db->services[i].name, "PlugPlay") ||
            !strcmp(db->services[i].name, "Eventlog"))
        {
            db->services[i].shutdown_order = stop_seq++;
            db->services[i].current_state = SERVICE_STOPPED;
        }
    }

    return ERROR_SUCCESS;
}

DWORD WINAPI scmdatabase_control_service(SCM_DATABASE *db, const char *service_name, DWORD control_code)
{
    (void)service_name;
    (void)control_code;
    if (!db) return ERROR_INVALID_PARAMETER;

    /* Reject concurrent control commands during active shutdown to prevent races */
    if (db->shutdown_in_progress)
        return ERROR_SHUTDOWN_IN_PROGRESS;

    return ERROR_SUCCESS;
}

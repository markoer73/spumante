/*
 * Wine Parity ole32 / combase / services Header
 *
 * Copyright 2026 Wine Parity Contributors
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef _WINE_OLE32_H
#define _WINE_OLE32_H

#include <stdint.h>
#include <stddef.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef _OLE32_
#define _OLE32_
#endif
#ifndef _COMBASEAPI_
#define _COMBASEAPI_
#endif
#ifndef WINOLEAPI
#define WINOLEAPI
#endif
#include <windows.h>
#include <objbase.h>
#include <servprov.h>
#include <winsvc.h>
#else
typedef uint8_t BYTE;
typedef uint16_t WORD;
typedef uint32_t DWORD;
typedef uint32_t ULONG;
typedef int32_t LONG;
typedef int32_t HRESULT;
typedef int BOOL;
typedef void *PVOID;
typedef void *LPVOID;
typedef void *HANDLE;
typedef const char *LPCSTR;
typedef const uint16_t *LPCWSTR;
typedef uint16_t *LPWSTR;

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif
#ifndef WINAPI
#define WINAPI
#endif

typedef struct _GUID {
    uint32_t Data1;
    uint16_t Data2;
    uint16_t Data3;
    uint8_t  Data4[8];
} GUID, UUID, IID, CLSID;

typedef const GUID *REFGUID;
typedef const IID *REFIID;
typedef const CLSID *REFCLSID;

#define IsEqualGUID(rguid1, rguid2) (!memcmp(rguid1, rguid2, sizeof(GUID)))
#define IsEqualIID(riid1, riid2) IsEqualGUID(riid1, riid2)
#define IsEqualCLSID(rclsid1, rclsid2) IsEqualGUID(rclsid1, rclsid2)

#define S_OK ((HRESULT)0L)
#define S_FALSE ((HRESULT)1L)
#define E_NOINTERFACE ((HRESULT)0x80004002L)
#define E_POINTER ((HRESULT)0x80004003L)
#define E_FAIL ((HRESULT)0x80004005L)
#define E_OUTOFMEMORY ((HRESULT)0x8007000EL)
#define E_INVALIDARG ((HRESULT)0x80070057L)
#define REGDB_E_IIDNOTREG ((HRESULT)0x80040155L)
#define CO_E_NOTINITIALIZED ((HRESULT)0x800401F0L)

#define ERROR_SUCCESS 0L
#define ERROR_INVALID_PARAMETER 87L
#define ERROR_SERVICE_REQUEST_TIMEOUT 1053L
#define ERROR_SERVICE_CANNOT_ACCEPT_CTRL 1061L
#define ERROR_SERVICE_NOT_ACTIVE 1062L
#define ERROR_SHUTDOWN_IN_PROGRESS 1115L

#ifndef HRESULT_FROM_WIN32
#define HRESULT_FROM_WIN32(x) ((HRESULT)(x) <= 0 ? ((HRESULT)(x)) : ((HRESULT) (((x) & 0x0000FFFF) | (7 << 16) | 0x80000000)))
#endif

/* COM marshaling constants */
typedef enum tagMSHCTX {
    MSHCTX_LOCAL = 0,
    MSHCTX_NOSHAREDMEM = 1,
    MSHCTX_DIFFERENTMACHINE = 2,
    MSHCTX_INPROC = 3,
    MSHCTX_CROSSCTX = 4
} MSHCTX;

typedef enum tagMSHLFLAGS {
    MSHLFLAGS_NORMAL = 0,
    MSHLFLAGS_TABLESTRONG = 1,
    MSHLFLAGS_TABLEWEAK = 2,
    MSHLFLAGS_NOPING = 4
} MSHLFLAGS;

typedef enum tagCLSCTX {
    CLSCTX_INPROC_SERVER = 0x1,
    CLSCTX_INPROC_HANDLER = 0x2,
    CLSCTX_LOCAL_SERVER = 0x4,
    CLSCTX_REMOTE_SERVER = 0x10,
    CLSCTX_ALL = (CLSCTX_INPROC_SERVER | CLSCTX_INPROC_HANDLER | CLSCTX_LOCAL_SERVER | CLSCTX_REMOTE_SERVER)
} CLSCTX;

typedef enum tagREGCLS {
    REGCLS_SINGLEUSE = 0,
    REGCLS_MULTIPLEUSE = 1,
    REGCLS_MULTI_SEPARATE = 2,
    REGCLS_SUSPENDED = 4,
    REGCLS_SURROGATE = 8
} REGCLS;

/* Service manager constants */
#define SERVICE_KERNEL_DRIVER 0x00000001
#define SERVICE_FILE_SYSTEM_DRIVER 0x00000002
#define SERVICE_WIN32_OWN_PROCESS 0x00000010
#define SERVICE_WIN32_SHARE_PROCESS 0x00000020

#define SERVICE_BOOT_START 0x00000000
#define SERVICE_SYSTEM_START 0x00000001
#define SERVICE_AUTO_START 0x00000002
#define SERVICE_DEMAND_START 0x00000003
#define SERVICE_DISABLED 0x00000004

#define SERVICE_STOPPED 0x00000001
#define SERVICE_START_PENDING 0x00000002
#define SERVICE_STOP_PENDING 0x00000003
#define SERVICE_RUNNING 0x00000004
#define SERVICE_CONTINUE_PENDING 0x00000005
#define SERVICE_PAUSE_PENDING 0x00000006
#define SERVICE_PAUSED 0x00000007

#define SERVICE_ACCEPT_STOP 0x00000001
#define SERVICE_ACCEPT_PAUSE_CONTINUE 0x00000002
#define SERVICE_ACCEPT_SHUTDOWN 0x00000004
#define SERVICE_ACCEPT_PRESHUTDOWN 0x00000100

#define SERVICE_CONTROL_STOP 0x00000001
#define SERVICE_CONTROL_PAUSE 0x00000002
#define SERVICE_CONTROL_CONTINUE 0x00000003
#define SERVICE_CONTROL_INTERROGATE 0x00000004
#define SERVICE_CONTROL_SHUTDOWN 0x00000005
#define SERVICE_CONTROL_PRESHUTDOWN 0x0000000F

/* COM Interface Structs for non-Win32 builds */
typedef struct IUnknown IUnknown;
typedef struct IUnknownVtbl {
    HRESULT (WINAPI *QueryInterface)(IUnknown *This, REFIID riid, void **ppvObject);
    ULONG (WINAPI *AddRef)(IUnknown *This);
    ULONG (WINAPI *Release)(IUnknown *This);
} IUnknownVtbl;
struct IUnknown {
    const IUnknownVtbl *lpVtbl;
};

typedef struct IClassFactory IClassFactory;
typedef struct IClassFactoryVtbl {
    HRESULT (WINAPI *QueryInterface)(IClassFactory *This, REFIID riid, void **ppvObject);
    ULONG (WINAPI *AddRef)(IClassFactory *This);
    ULONG (WINAPI *Release)(IClassFactory *This);
    HRESULT (WINAPI *CreateInstance)(IClassFactory *This, IUnknown *pUnkOuter, REFIID riid, void **ppvObject);
    HRESULT (WINAPI *LockServer)(IClassFactory *This, BOOL fLock);
} IClassFactoryVtbl;
struct IClassFactory {
    const IClassFactoryVtbl *lpVtbl;
};

typedef struct IStream IStream;
typedef struct IStreamVtbl {
    HRESULT (WINAPI *QueryInterface)(IStream *This, REFIID riid, void **ppvObject);
    ULONG (WINAPI *AddRef)(IStream *This);
    ULONG (WINAPI *Release)(IStream *This);
    HRESULT (WINAPI *Read)(IStream *This, void *pv, ULONG cb, ULONG *pcbRead);
    HRESULT (WINAPI *Write)(IStream *This, const void *pv, ULONG cb, ULONG *pcbWritten);
    HRESULT (WINAPI *Seek)(IStream *This, int64_t dlibMove, DWORD dwOrigin, uint64_t *plibNewPosition);
    HRESULT (WINAPI *SetSize)(IStream *This, uint64_t libNewSize);
    HRESULT (WINAPI *CopyTo)(IStream *This, IStream *pstm, uint64_t cb, uint64_t *pcbRead, uint64_t *pcbWritten);
    HRESULT (WINAPI *Commit)(IStream *This, DWORD grfCommitFlags);
    HRESULT (WINAPI *Revert)(IStream *This);
    HRESULT (WINAPI *LockRegion)(IStream *This, uint64_t libOffset, uint64_t cb, DWORD dwLockType);
    HRESULT (WINAPI *UnlockRegion)(IStream *This, uint64_t libOffset, uint64_t cb, DWORD dwLockType);
    HRESULT (WINAPI *Stat)(IStream *This, void *pstatstg, DWORD grfStatFlag);
    HRESULT (WINAPI *Clone)(IStream *This, IStream **ppstm);
} IStreamVtbl;
struct IStream {
    const IStreamVtbl *lpVtbl;
};

typedef struct IServiceProvider IServiceProvider;
typedef struct IServiceProviderVtbl {
    HRESULT (WINAPI *QueryInterface)(IServiceProvider *This, REFIID riid, void **ppvObject);
    ULONG (WINAPI *AddRef)(IServiceProvider *This);
    ULONG (WINAPI *Release)(IServiceProvider *This);
    HRESULT (WINAPI *QueryService)(IServiceProvider *This, REFGUID guidService, REFIID riid, void **ppvObject);
} IServiceProviderVtbl;
struct IServiceProvider {
    const IServiceProviderVtbl *lpVtbl;
};

HRESULT WINAPI CoInitialize(void *pvReserved);
HRESULT WINAPI CoInitializeEx(void *pvReserved, DWORD dwCoInit);
void WINAPI CoUninitialize(void);
HRESULT WINAPI CoGetPSClsid(REFIID riid, CLSID *pclsid);
HRESULT WINAPI CoRegisterPSClsid(REFIID riid, REFCLSID rclsid);
HRESULT WINAPI CreateStreamOnHGlobal(void *hGlobal, BOOL fDeleteOnRelease, IStream **ppstm);
HRESULT WINAPI CoMarshalInterface(IStream *pStm, REFIID riid, IUnknown *pUnk, DWORD dwDestContext, void *pvDestContext, DWORD mshlflags);
HRESULT WINAPI CoUnmarshalInterface(IStream *pStm, REFIID riid, void **ppv);
HRESULT WINAPI CoReleaseMarshalData(IStream *pStm);
HRESULT WINAPI CoRegisterClassObject(REFCLSID rclsid, IUnknown *pUnk, DWORD dwClsContext, DWORD flags, DWORD *lpdwRegister);
HRESULT WINAPI CoRevokeClassObject(DWORD dwRegister);

#endif /* _WIN32 */

/* Standard GUID declarations */
static const GUID IID_IUnknown_Val = {0x00000000, 0x0000, 0x0000, {0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
static const GUID IID_IClassFactory_Val = {0x00000001, 0x0000, 0x0000, {0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
static const GUID IID_IStream_Val = {0x0000000C, 0x0000, 0x0000, {0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
static const GUID IID_IMarshal_Val = {0x00000003, 0x0000, 0x0000, {0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
static const GUID IID_IPSFactoryBuffer_Val = {0xD5F569D0, 0x593B, 0x101A, {0xB5, 0x69, 0x08, 0x00, 0x2B, 0x2D, 0xBF, 0x7A}};
static const GUID IID_IServiceProvider_Val = {0x6D5140C1, 0x7436, 0x11CE, {0x80, 0x34, 0x00, 0xAA, 0x00, 0x60, 0x09, 0xFA}};
static const GUID CLSID_ActxprxyPSFactory_Val = {0xB8DA6310, 0xE19B, 0x11D0, {0x93, 0x3C, 0x00, 0xA0, 0xC9, 0x0D, 0xCA, 0xA9}};
static const GUID CLSID_StdMarshal_Val = {0x00000017, 0x0000, 0x0000, {0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};

#define IID_IUnknown IID_IUnknown_Val
#define IID_IClassFactory IID_IClassFactory_Val
#define IID_IStream IID_IStream_Val
#define IID_IMarshal IID_IMarshal_Val
#define IID_IPSFactoryBuffer IID_IPSFactoryBuffer_Val
#define IID_IServiceProvider IID_IServiceProvider_Val
#define CLSID_ActxprxyPSFactory CLSID_ActxprxyPSFactory_Val
#define CLSID_StdMarshal CLSID_StdMarshal_Val

/* SCM Simulation structures */
typedef struct _SCM_SERVICE_INFO {
    char name[64];
    char group[64];
    DWORD service_type;
    DWORD start_type;
    DWORD controls_accepted;
    DWORD current_state;
    DWORD wait_hint_ms;
    DWORD startup_order;
    DWORD shutdown_order;
} SCM_SERVICE_INFO;

typedef struct _SCM_DATABASE {
    DWORD service_count;
    SCM_SERVICE_INFO services[32];
    BOOL shutdown_in_progress;
    DWORD last_error;
} SCM_DATABASE;

/* Internal / Extended Functions implemented in ole32 / combase */
HRESULT WINAPI apartment_get_local_server_stream(void *apt, IStream **ret);
BOOL WINAPI start_rpcss(void);

DWORD WINAPI scmdatabase_autostart_services_ordered(SCM_DATABASE *db);
DWORD WINAPI scmdatabase_shutdown_services_ordered(SCM_DATABASE *db);
DWORD WINAPI scmdatabase_control_service(SCM_DATABASE *db, const char *service_name, DWORD control_code);

#endif /* _WINE_OLE32_H */

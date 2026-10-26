/*
 * Wine Bluetooth Bus Driver & WDM Definitions (macOS IOBluetooth Parity)
 *
 * Copyright 2026 Spumante Contributors
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef _WINE_WINEBTH_H
#define _WINE_WINEBTH_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "../../tests/include/parity_test.h"

/* NTSTATUS Codes */
#ifndef STATUS_SUCCESS
#define STATUS_SUCCESS                   ((NTSTATUS)0x00000000)
#endif
#ifndef STATUS_UNSUCCESSFUL
#define STATUS_UNSUCCESSFUL             ((NTSTATUS)0xC0000001)
#endif
#ifndef STATUS_NOT_IMPLEMENTED
#define STATUS_NOT_IMPLEMENTED          ((NTSTATUS)0xC0000002)
#endif
#ifndef STATUS_INVALID_HANDLE
#define STATUS_INVALID_HANDLE           ((NTSTATUS)0xC0000008)
#endif
#ifndef STATUS_INVALID_PARAMETER
#define STATUS_INVALID_PARAMETER        ((NTSTATUS)0xC000000D)
#endif
#ifndef STATUS_NO_MEMORY
#define STATUS_NO_MEMORY                ((NTSTATUS)0xC0000017)
#endif
#ifndef STATUS_BUFFER_TOO_SMALL
#define STATUS_BUFFER_TOO_SMALL         ((NTSTATUS)0xC0000023)
#endif
#ifndef STATUS_PENDING
#define STATUS_PENDING                  ((NTSTATUS)0x00000103)
#endif
#ifndef STATUS_INTERNAL_ERROR
#define STATUS_INTERNAL_ERROR           ((NTSTATUS)0xC00000E5)
#endif
#ifndef STATUS_NOT_SUPPORTED
#define STATUS_NOT_SUPPORTED            ((NTSTATUS)0xC00000BB)
#endif
#ifndef STATUS_DLL_NOT_FOUND
#define STATUS_DLL_NOT_FOUND            ((NTSTATUS)0xC0000135)
#endif

/* Win32 Error Codes */
#ifndef ERROR_SUCCESS
#define ERROR_SUCCESS                   0U
#endif
#ifndef ERROR_MOD_NOT_FOUND
#define ERROR_MOD_NOT_FOUND             126U
#endif
#ifndef ERROR_INVALID_PARAMETER
#define ERROR_INVALID_PARAMETER         87U
#endif
#ifndef ERROR_INTERNAL_ERROR
#define ERROR_INTERNAL_ERROR            1359U
#endif
#ifndef ERROR_SERVICE_NOT_ACTIVE
#define ERROR_SERVICE_NOT_ACTIVE        1062U
#endif

/* WDM Device Types & Characteristics */
#ifndef FILE_DEVICE_BUS_EXTENDER
#define FILE_DEVICE_BUS_EXTENDER        0x0000002AU
#endif
#ifndef FILE_DEVICE_BLUETOOTH
#define FILE_DEVICE_BLUETOOTH           0x0000003BU
#endif
#ifndef DO_DEVICE_INITIALIZING
#define DO_DEVICE_INITIALIZING          0x00000080U
#endif

/* Major Function Codes */
#ifndef IRP_MJ_CREATE
#define IRP_MJ_CREATE                   0x00U
#endif
#ifndef IRP_MJ_DEVICE_CONTROL
#define IRP_MJ_DEVICE_CONTROL           0x0EU
#endif
#ifndef IRP_MJ_PNP
#define IRP_MJ_PNP                      0x1BU
#endif

/* Minor PnP Function Codes */
#ifndef IRP_MN_START_DEVICE
#define IRP_MN_START_DEVICE             0x00U
#endif
#ifndef IRP_MN_REMOVE_DEVICE
#define IRP_MN_REMOVE_DEVICE            0x02U
#endif
#ifndef IRP_MN_QUERY_DEVICE_RELATIONS
#define IRP_MN_QUERY_DEVICE_RELATIONS   0x07U
#endif
#ifndef IRP_MN_QUERY_ID
#define IRP_MN_QUERY_ID                 0x13U
#endif

/* PnP Bus Query ID Types */
#ifndef _BUS_QUERY_ID_TYPE_DEFINED
#define _BUS_QUERY_ID_TYPE_DEFINED
typedef enum _BUS_QUERY_ID_TYPE {
    BusQueryDeviceID = 0,
    BusQueryHardwareIDs = 1,
    BusQueryCompatibleIDs = 2,
    BusQueryInstanceID = 3,
    BusQueryDeviceSerialNumber = 4,
    BusQueryContainerID = 5
} BUS_QUERY_ID_TYPE, *PBUS_QUERY_ID_TYPE;
#endif

/* PnP Device Relation Types */
#ifndef _DEVICE_RELATION_TYPE_DEFINED
#define _DEVICE_RELATION_TYPE_DEFINED
typedef enum _DEVICE_RELATION_TYPE {
    BusRelations = 0,
    EjectionRelations = 1,
    PowerRelations = 2,
    RemovalRelations = 3,
    TargetDeviceRelation = 4,
    SingleBusRelations = 5
} DEVICE_RELATION_TYPE, *PDEVICE_RELATION_TYPE;
#endif

/* Bluetooth Type Definitions */
typedef uint64_t BTH_ADDR;

#define IOCTL_BTH_GET_LOCAL_INFO        0x00410000U
#define IOCTL_BTH_GET_RADIO_INFO        0x00410004U
#define IOCTL_BTH_DISCONNECT_DEVICE      0x00410008U

#pragma pack(push, 1)

typedef struct _BTH_RADIO_INFO {
    uint64_t lmpSupportedFeatures;
    uint8_t  mfg;
    uint16_t lmpSubversion;
    uint8_t  lmpVersion;
} BTH_RADIO_INFO, *PBTH_RADIO_INFO;

typedef struct _BTH_LOCAL_RADIO_INFO {
    BTH_RADIO_INFO localInfo;
    BTH_ADDR       address;
    uint32_t       flags;
    uint16_t       hciVersion;
    uint16_t       hciRevision;
    uint8_t        radioMode;
    char           name[248];
} BTH_LOCAL_RADIO_INFO, *PBTH_LOCAL_RADIO_INFO;

typedef struct _BTH_DEVICE_INFO {
    uint32_t flags;
    BTH_ADDR address;
    uint32_t classOfDevice;
    char     name[248];
} BTH_DEVICE_INFO, *PBTH_DEVICE_INFO;

#pragma pack(pop)

/* Forward declarations of driver/device structures */
struct _DRIVER_OBJECT;
struct _DEVICE_OBJECT;
struct _IRP;

typedef NTSTATUS (WINAPI *PDRIVER_ADD_DEVICE)(struct _DRIVER_OBJECT *DriverObject, struct _DEVICE_OBJECT *PhysicalDeviceObject);
typedef void (WINAPI *PDRIVER_UNLOAD)(struct _DRIVER_OBJECT *DriverObject);
typedef NTSTATUS (WINAPI *PDRIVER_DISPATCH)(struct _DEVICE_OBJECT *DeviceObject, struct _IRP *Irp);
typedef void (WINAPI *PDRIVER_STARTIO)(struct _DEVICE_OBJECT *DeviceObject, struct _IRP *Irp);

typedef struct _DRIVER_EXTENSION {
    struct _DRIVER_OBJECT *DriverObject;
    PDRIVER_ADD_DEVICE     AddDevice;
    uint32_t               Count;
    void                  *ServiceKeyName;
} DRIVER_EXTENSION, *PDRIVER_EXTENSION;

typedef struct _DRIVER_OBJECT {
    int16_t           Type;
    int16_t           Size;
    struct _DEVICE_OBJECT *DeviceObject;
    uint32_t          Flags;
    void             *DriverStart;
    uint32_t          DriverSize;
    void             *DriverSection;
    PDRIVER_EXTENSION DriverExtension;
    void             *DriverName;
    void             *HardwareDatabase;
    void             *FastIoDispatch;
    void             *DriverInit;
    PDRIVER_STARTIO   DriverStartIo;
    PDRIVER_UNLOAD    DriverUnload;
    PDRIVER_DISPATCH  MajorFunction[28];
} DRIVER_OBJECT, *PDRIVER_OBJECT;

typedef struct _DEVICE_OBJECT {
    int16_t           Type;
    uint16_t          Size;
    int32_t           ReferenceCount;
    struct _DRIVER_OBJECT *DriverObject;
    struct _DEVICE_OBJECT *NextDevice;
    struct _DEVICE_OBJECT *AttachedDevice;
    struct _IRP      *CurrentIrp;
    uint32_t          Flags;
    uint32_t          Characteristics;
    void             *DeviceExtension;
    uint32_t          DeviceType;
    void             *Reserved;
} DEVICE_OBJECT, *PDEVICE_OBJECT;

typedef struct _IO_STATUS_BLOCK {
    union {
        NTSTATUS Status;
        void    *Pointer;
    };
    uintptr_t Information;
} IO_STATUS_BLOCK, *PIO_STATUS_BLOCK;

typedef struct _IO_STACK_LOCATION {
    uint8_t  MajorFunction;
    uint8_t  MinorFunction;
    uint8_t  Flags;
    uint8_t  Control;
    union {
        struct {
            uint32_t OutputBufferLength;
            uint32_t InputBufferLength;
            uint32_t IoControlCode;
            void    *Type3InputBuffer;
        } DeviceIoControl;
        struct {
            DEVICE_RELATION_TYPE Type;
        } QueryDeviceRelations;
        struct {
            BUS_QUERY_ID_TYPE IdType;
        } QueryId;
    } Parameters;
    struct _DEVICE_OBJECT *DeviceObject;
    void                  *FileObject;
} IO_STACK_LOCATION, *PIO_STACK_LOCATION;

typedef struct _IRP {
    int16_t         Type;
    uint16_t        Size;
    IO_STATUS_BLOCK IoStatus;
    uint32_t        Flags;
    IO_STACK_LOCATION CurrentStackLocation;
} IRP, *PIRP;

typedef struct _DEVICE_RELATIONS {
    uint32_t Count;
    PDEVICE_OBJECT Objects[1];
} DEVICE_RELATIONS, *PDEVICE_RELATIONS;

/* SetupDi & Service Installation Simulation Status */
typedef struct _WINEBTH_SETUPDI_STATUS {
    uint32_t service_created;
    uint32_t service_started;
    uint32_t driver_entry_status;
    uint32_t win32_last_error;
    uint32_t radio_enumerated;
    uint32_t device_count;
} WINEBTH_SETUPDI_STATUS;

/* WineBth Public Entrypoints */
NTSTATUS WINAPI DriverEntry(PDRIVER_OBJECT DriverObject, PVOID RegistryPath);

NTSTATUS WINAPI WineBthSimulatePnpStart(
    PDRIVER_OBJECT DriverObject,
    PDEVICE_OBJECT BusFdo);

NTSTATUS WINAPI WineBthSimulateQueryRelations(
    PDEVICE_OBJECT BusFdo,
    DEVICE_RELATION_TYPE RelationType,
    PDEVICE_RELATIONS *OutRelations);

NTSTATUS WINAPI WineBthSimulateQueryId(
    PDEVICE_OBJECT ChildPdo,
    BUS_QUERY_ID_TYPE IdType,
    WCHAR *OutBuffer,
    uint32_t MaxChars,
    uint32_t *OutChars);

NTSTATUS WINAPI WineBthSimulateIoctl(
    PDEVICE_OBJECT ChildPdo,
    uint32_t IoControlCode,
    void *InBuffer,
    uint32_t InBufferSize,
    void *OutBuffer,
    uint32_t OutBufferSize,
    uint32_t *BytesReturned);

NTSTATUS WINAPI WineBthSimulateSetupDiInstall(
    WINEBTH_SETUPDI_STATUS *OutStatus);

void WINAPI WineBthResetDriverState(void);
void WINAPI WineBthResetInternalDriver(void);

#endif /* _WINE_WINEBTH_H */

/*
 * Wine USB Bus Driver & WDM/URB Definitions (macOS IOKit & libusb-1.0 Parity)
 *
 * Copyright 2026 Wine Parity Contributors
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef _WINE_WINEUSB_H
#define _WINE_WINEUSB_H

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
#ifndef STATUS_DELETE_PENDING
#define STATUS_DELETE_PENDING           ((NTSTATUS)0xC0000056)
#endif
#ifndef STATUS_PENDING
#define STATUS_PENDING                  ((NTSTATUS)0x00000103)
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
#ifndef ERROR_SERVICE_NOT_ACTIVE
#define ERROR_SERVICE_NOT_ACTIVE        1062U
#endif

/* WDM Device Types & Characteristics */
#ifndef FILE_DEVICE_USB
#define FILE_DEVICE_USB                 0x00000022U
#endif
#ifndef FILE_DEVICE_BUS_EXTENDER
#define FILE_DEVICE_BUS_EXTENDER        0x0000002AU
#endif
#ifndef DO_DEVICE_INITIALIZING
#define DO_DEVICE_INITIALIZING          0x00000080U
#endif

/* Major Function Codes */
#define IRP_MJ_INTERNAL_DEVICE_CONTROL  0x0FU
#define IRP_MJ_PNP                      0x1BU

/* Minor PnP Function Codes */
#define IRP_MN_START_DEVICE             0x00U
#define IRP_MN_REMOVE_DEVICE            0x02U
#define IRP_MN_QUERY_DEVICE_RELATIONS   0x07U
#define IRP_MN_QUERY_CAPABILITIES       0x09U
#define IRP_MN_QUERY_ID                 0x13U
#define IRP_MN_SURPRISE_REMOVAL         0x17U

/* Device Relations Types */
typedef enum _DEVICE_RELATION_TYPE {
    BusRelations = 0,
    EjectionRelations,
    PowerRelations,
    RemovalRelations,
    TargetDeviceRelation,
    SingleBusRelations
} DEVICE_RELATION_TYPE;

/* Bus Query ID Types */
typedef enum _BUS_QUERY_ID_TYPE {
    BusQueryDeviceID = 0,
    BusQueryHardwareIDs,
    BusQueryCompatibleIDs,
    BusQueryInstanceID,
    BusQueryDeviceSerialNumber
} BUS_QUERY_ID_TYPE;

/* USB IOCTLs */
#define IOCTL_INTERNAL_USB_SUBMIT_URB \
    ((0x22U << 16) | (0U << 14) | (0U << 2) | 3U)

/* URB Functions */
#define URB_FUNCTION_SELECT_CONFIGURATION                0x0000U
#define URB_FUNCTION_SELECT_INTERFACE                    0x0001U
#define URB_FUNCTION_ABORT_PIPE                          0x0002U
#define URB_FUNCTION_TAKE_FRAME_LENGTH_CONTROL           0x0003U
#define URB_FUNCTION_RELEASE_FRAME_LENGTH_CONTROL        0x0004U
#define URB_FUNCTION_GET_FRAME_LENGTH                    0x0005U
#define URB_FUNCTION_SET_FRAME_LENGTH                    0x0006U
#define URB_FUNCTION_GET_CURRENT_FRAME_NUMBER            0x0007U
#define URB_FUNCTION_CONTROL_TRANSFER                    0x0008U
#define URB_FUNCTION_BULK_OR_INTERRUPT_TRANSFER          0x0009U
#define URB_FUNCTION_ISOCH_TRANSFER                      0x000AU
#define URB_FUNCTION_GET_DESCRIPTOR_FROM_DEVICE          0x000BU
#define URB_FUNCTION_SET_DESCRIPTOR_TO_DEVICE            0x000CU
#define URB_FUNCTION_SET_FEATURE_TO_DEVICE               0x000DU
#define URB_FUNCTION_SET_FEATURE_TO_INTERFACE            0x000EU
#define URB_FUNCTION_SET_FEATURE_TO_ENDPOINT             0x000FU
#define URB_FUNCTION_CLEAR_FEATURE_TO_DEVICE             0x0010U
#define URB_FUNCTION_CLEAR_FEATURE_TO_INTERFACE          0x0011U
#define URB_FUNCTION_CLEAR_FEATURE_TO_ENDPOINT           0x0012U
#define URB_FUNCTION_GET_STATUS_FROM_DEVICE              0x0013U
#define URB_FUNCTION_GET_STATUS_FROM_INTERFACE           0x0014U
#define URB_FUNCTION_GET_STATUS_FROM_ENDPOINT            0x0015U
#define URB_FUNCTION_VENDOR_DEVICE                       0x0017U
#define URB_FUNCTION_VENDOR_INTERFACE                    0x0018U
#define URB_FUNCTION_VENDOR_ENDPOINT                     0x0019U
#define URB_FUNCTION_CLASS_DEVICE                        0x001AU
#define URB_FUNCTION_CLASS_INTERFACE                     0x001BU
#define URB_FUNCTION_CLASS_ENDPOINT                      0x001CU
#define URB_FUNCTION_SYNC_RESET_PIPE_AND_CLEAR_STALL     0x001EU

/* USBD Status Codes */
#define USBD_STATUS_SUCCESS                              0x00000000U
#define USBD_STATUS_REQUEST_FAILED                       0xC0000001U
#define USBD_STATUS_DEVICE_GONE                          0xC0000008U
#define USBD_STATUS_ENDPOINT_HALTED                      0xC0000030U
#define USBD_STATUS_TIMEOUT                              0xC0000060U
#define USBD_STATUS_CANCELED                             0x00010000U

/* USBD Transfer Direction Flags */
#define USBD_TRANSFER_DIRECTION_OUT                      0x00000000U
#define USBD_TRANSFER_DIRECTION_IN                       0x00000001U
#define USBD_SHORT_TRANSFER_OK                           0x00000002U

/* USB Descriptor Types */
#define USB_DEVICE_DESCRIPTOR_TYPE                       0x01U
#define USB_CONFIGURATION_DESCRIPTOR_TYPE                0x02U
#define USB_STRING_DESCRIPTOR_TYPE                       0x03U
#define USB_INTERFACE_DESCRIPTOR_TYPE                    0x04U
#define USB_ENDPOINT_DESCRIPTOR_TYPE                     0x05U

#pragma pack(push, 1)

typedef struct _USB_DEVICE_DESCRIPTOR {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint16_t bcdUSB;
    uint8_t  bDeviceClass;
    uint8_t  bDeviceSubClass;
    uint8_t  bDeviceProtocol;
    uint8_t  bMaxPacketSize0;
    uint16_t idVendor;
    uint16_t idProduct;
    uint16_t bcdDevice;
    uint8_t  iManufacturer;
    uint8_t  iProduct;
    uint8_t  iSerialNumber;
    uint8_t  bNumConfigurations;
} USB_DEVICE_DESCRIPTOR, *PUSB_DEVICE_DESCRIPTOR;

typedef struct _USB_CONFIGURATION_DESCRIPTOR {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint16_t wTotalLength;
    uint8_t  bNumInterfaces;
    uint8_t  bConfigurationValue;
    uint8_t  iConfiguration;
    uint8_t  bmAttributes;
    uint8_t  MaxPower;
} USB_CONFIGURATION_DESCRIPTOR, *PUSB_CONFIGURATION_DESCRIPTOR;

typedef struct _USB_INTERFACE_DESCRIPTOR {
    uint8_t bLength;
    uint8_t bDescriptorType;
    uint8_t bInterfaceNumber;
    uint8_t bAlternateSetting;
    uint8_t bNumEndpoints;
    uint8_t bInterfaceClass;
    uint8_t bInterfaceSubClass;
    uint8_t bInterfaceProtocol;
    uint8_t iInterface;
} USB_INTERFACE_DESCRIPTOR, *PUSB_INTERFACE_DESCRIPTOR;

typedef struct _USB_ENDPOINT_DESCRIPTOR {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint8_t  bEndpointAddress;
    uint8_t  bmAttributes;
    uint16_t wMaxPacketSize;
    uint8_t  bInterval;
} USB_ENDPOINT_DESCRIPTOR, *PUSB_ENDPOINT_DESCRIPTOR;

#pragma pack(pop)

/* Pipe & Interface Information */
typedef void *USBD_PIPE_HANDLE;
typedef void *USBD_CONFIGURATION_HANDLE;
typedef void *USBD_INTERFACE_HANDLE;

typedef struct _USBD_PIPE_INFORMATION {
    uint16_t         MaximumPacketSize;
    uint8_t          EndpointAddress;
    uint8_t          Interval;
    uint32_t         PipeType;
    USBD_PIPE_HANDLE PipeHandle;
    uint32_t         MaximumTransferSize;
    uint32_t         PipeFlags;
} USBD_PIPE_INFORMATION, *PUSBD_PIPE_INFORMATION;

typedef struct _USBD_INTERFACE_INFORMATION {
    uint16_t               Length;
    uint8_t                InterfaceNumber;
    uint8_t                AlternateSetting;
    uint8_t                Class;
    uint8_t                SubClass;
    uint8_t                Protocol;
    uint8_t                Reserved;
    USBD_INTERFACE_HANDLE  InterfaceHandle;
    uint32_t               NumberOfPipes;
    USBD_PIPE_INFORMATION  Pipes[1];
} USBD_INTERFACE_INFORMATION, *PUSBD_INTERFACE_INFORMATION;

/* URB Header & Requests */
union _URB;

typedef struct _URB_HEADER {
    uint16_t Length;
    uint16_t Function;
    uint32_t Status;
    void    *UsbdDeviceHandle;
    uint32_t UsbdFlags;
} URB_HEADER, *PURB_HEADER;

typedef struct _URB_CONTROL_DESCRIPTOR_REQUEST {
    URB_HEADER Hdr;
    void      *Reserved;
    uint32_t   Reserved0;
    uint32_t   TransferBufferLength;
    void      *TransferBuffer;
    void      *TransferBufferMDL;
    union _URB *UrbLink;
    struct _URB_HCD_AREA {
        void *Reserved8[8];
    } hca;
    uint16_t   Index;
    uint8_t    DescriptorType;
    uint8_t    LanguageId;
} _URB_CONTROL_DESCRIPTOR_REQUEST;

typedef struct _URB_SELECT_CONFIGURATION {
    URB_HEADER                  Hdr;
    USB_CONFIGURATION_DESCRIPTOR *ConfigurationDescriptor;
    USBD_CONFIGURATION_HANDLE   ConfigurationHandle;
    USBD_INTERFACE_INFORMATION  Interface;
} _URB_SELECT_CONFIGURATION;

typedef struct _URB_CONTROL_VENDOR_OR_CLASS_REQUEST {
    URB_HEADER Hdr;
    void      *Reserved;
    uint32_t   TransferFlags;
    uint32_t   TransferBufferLength;
    void      *TransferBuffer;
    void      *TransferBufferMDL;
    union _URB *UrbLink;
    struct _URB_HCD_AREA hca;
    uint8_t    RequestTypeReservedBits;
    uint8_t    Request;
    uint16_t   Value;
    uint16_t   Index;
} _URB_CONTROL_VENDOR_OR_CLASS_REQUEST;

typedef struct _URB_BULK_OR_INTERRUPT_TRANSFER {
    URB_HEADER       Hdr;
    USBD_PIPE_HANDLE PipeHandle;
    uint32_t         TransferFlags;
    uint32_t         TransferBufferLength;
    void            *TransferBuffer;
    void            *TransferBufferMDL;
    union _URB     *UrbLink;
    struct _URB_HCD_AREA hca;
} _URB_BULK_OR_INTERRUPT_TRANSFER;

typedef union _URB {
    URB_HEADER                           UrbHeader;
    _URB_CONTROL_DESCRIPTOR_REQUEST      UrbControlDescriptorRequest;
    _URB_SELECT_CONFIGURATION            UrbSelectConfiguration;
    _URB_CONTROL_VENDOR_OR_CLASS_REQUEST UrbControlVendorClassRequest;
    _URB_BULK_OR_INTERRUPT_TRANSFER      UrbBulkOrInterruptTransfer;
} URB, *PURB;

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
    int8_t            StackSize;
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
            DEVICE_RELATION_TYPE Type;
        } QueryDeviceRelations;
        struct {
            BUS_QUERY_ID_TYPE IdType;
        } QueryId;
        struct {
            uint32_t OutputBufferLength;
            uint32_t InputBufferLength;
            uint32_t IoControlCode;
            void    *Type3InputBuffer;
        } DeviceIoControl;
        struct {
            void *Argument1;
            void *Argument2;
            void *Argument3;
            void *Argument4;
        } Others;
    } Parameters;
    PDEVICE_OBJECT DeviceObject;
    void          *FileObject;
} IO_STACK_LOCATION, *PIO_STACK_LOCATION;

typedef struct _IRP {
    int16_t         Type;
    uint16_t        Size;
    IO_STATUS_BLOCK IoStatus;
    uint32_t        Flags;
    union {
        struct {
            void *DriverContext[4];
        } Overlay;
    } Tail;
    IO_STACK_LOCATION CurrentStackLocation;
} IRP, *PIRP;

typedef struct _DEVICE_RELATIONS {
    uint32_t Count;
    PDEVICE_OBJECT Objects[1];
} DEVICE_RELATIONS, *PDEVICE_RELATIONS;

/* SetupDi & Service Installation Simulation Status */
typedef struct _WINEUSB_SETUPDI_STATUS {
    uint32_t service_created;
    uint32_t service_started;
    uint32_t driver_entry_status;
    uint32_t win32_last_error;
    uint32_t root_hub_enumerated;
    uint32_t device_count;
} WINEUSB_SETUPDI_STATUS;

/* WineUSB Public Entrypoints */
NTSTATUS WINAPI DriverEntry(PDRIVER_OBJECT DriverObject, PVOID RegistryPath);

NTSTATUS WINAPI WineUsbSimulatePnpStart(
    PDRIVER_OBJECT DriverObject,
    PDEVICE_OBJECT BusFdo);

NTSTATUS WINAPI WineUsbSimulateQueryRelations(
    PDEVICE_OBJECT BusFdo,
    DEVICE_RELATION_TYPE RelationType,
    PDEVICE_RELATIONS *OutRelations);

NTSTATUS WINAPI WineUsbSimulateQueryId(
    PDEVICE_OBJECT ChildPdo,
    BUS_QUERY_ID_TYPE IdType,
    WCHAR *OutBuffer,
    uint32_t MaxChars,
    uint32_t *OutChars);

NTSTATUS WINAPI WineUsbSimulateSubmitUrb(
    PDEVICE_OBJECT ChildPdo,
    PURB Urb);

NTSTATUS WINAPI WineUsbSimulateSetupDiInstall(
    WINEUSB_SETUPDI_STATUS *OutStatus);

void WINAPI WineUsbResetDriverState(void);
void WINAPI WineUsbResetInternalDriver(void);

#endif /* _WINE_WINEUSB_H */

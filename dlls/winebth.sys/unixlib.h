/*
 * WineBTH Unix Library Interface (macOS IOBluetooth & CoreBluetooth Parity)
 *
 * Copyright 2026 Spumante Contributors
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef _WINE_WINEBTH_UNIXLIB_H
#define _WINE_WINEBTH_UNIXLIB_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifndef WINAPI
#if defined(_WIN32) || defined(__CYGWIN__)
#define WINAPI __stdcall
#else
#define WINAPI
#endif
#endif

#ifndef _NTSTATUS_DEFINED
#define _NTSTATUS_DEFINED
#ifdef _WIN32
#include <windows.h>
typedef LONG NTSTATUS;
#else
typedef int32_t NTSTATUS;
#endif
#endif

#ifdef interface
#undef interface
#endif

typedef uint64_t unix_name_t;
typedef uint64_t unix_handle_t;

enum winebluetooth_event_type {
    WINEBLUETOOTH_EVENT_NONE = 0,
    WINEBLUETOOTH_EVENT_WATCHER_EVENT = 1,
    WINEBLUETOOTH_EVENT_AUTH_EVENT = 2,
};

enum winebluetooth_watcher_event_type {
    BLUETOOTH_WATCHER_EVENT_TYPE_SERVICE_DOWN = 0,
    BLUETOOTH_WATCHER_EVENT_TYPE_RADIO_ADDED = 1,
    BLUETOOTH_WATCHER_EVENT_TYPE_RADIO_REMOVED = 2,
    BLUETOOTH_WATCHER_EVENT_TYPE_RADIO_PROPERTIES_CHANGED = 3,
    BLUETOOTH_WATCHER_EVENT_TYPE_DEVICE_ADDED = 4,
    BLUETOOTH_WATCHER_EVENT_TYPE_DEVICE_REMOVED = 5,
    BLUETOOTH_WATCHER_EVENT_TYPE_DEVICE_PROPERTIES_CHANGED = 6,
};

struct winebluetooth_radio_properties {
    unix_name_t adapter_handle;
    uint64_t    address;
    char        name[248];
    uint32_t    class_of_device;
    bool        discoverable;
    bool        pairable;
    bool        powered;
    uint16_t    manufacturer;
    uint16_t    hci_version;
    uint16_t    hci_revision;
    uint8_t     lmp_version;
    uint16_t    lmp_subversion;
};

struct winebluetooth_device_properties {
    unix_name_t device_handle;
    unix_name_t adapter_handle;
    uint64_t    address;
    char        name[248];
    uint32_t    class_of_device;
    bool        paired;
    bool        connected;
    int16_t     rssi;
};

struct winebluetooth_watcher_event {
    enum winebluetooth_watcher_event_type event_type;
    union {
        struct winebluetooth_radio_properties  radio_added;
        unix_name_t                            radio_removed;
        struct winebluetooth_device_properties device_added;
        unix_name_t                            device_removed;
    } event_data;
};

struct winebluetooth_event {
    enum winebluetooth_event_type status;
    union {
        struct winebluetooth_watcher_event watcher_event;
    } data;
};

struct bluetooth_get_event_params {
    struct winebluetooth_event result;
};

struct bluetooth_adapter_get_unique_name_params {
    unix_name_t adapter;
    char       *buf;
    size_t      buf_size;
};

struct bluetooth_device_disconnect_params {
    unix_name_t device;
};

enum bluetoothapis_funcs {
    unix_bluetooth_init = 0,
    unix_bluetooth_shutdown,
    unix_bluetooth_adapter_set_prop,
    unix_bluetooth_adapter_get_unique_name,
    unix_bluetooth_adapter_start_discovery,
    unix_bluetooth_adapter_stop_discovery,
    unix_bluetooth_adapter_remove_device,
    unix_bluetooth_adapter_free,
    unix_bluetooth_adapter_dup,
    unix_bluetooth_device_free,
    unix_bluetooth_device_dup,
    unix_bluetooth_device_disconnect,
    unix_bluetooth_device_start_pairing,
    unix_bluetooth_auth_agent_enable_incoming,
    unix_bluetooth_auth_send_response,
    unix_bluetooth_gatt_service_free,
    unix_bluetooth_gatt_characteristic_free,
    unix_bluetooth_gatt_characteristic_read,
    unix_bluetooth_gatt_characteristic_value_move,
    unix_bluetooth_gatt_characteristic_value_free,
    unix_bluetooth_get_event,
    unix_funcs_count
};

typedef NTSTATUS (*unixlib_entry_t)(void *args);

extern const unixlib_entry_t __wine_unix_call_funcs[];
extern const unixlib_entry_t __wine_unix_call_wow64_funcs[];

NTSTATUS WINAPI __wine_init_unix_call(void);
NTSTATUS WINAPI wine_unix_call(enum bluetoothapis_funcs func, void *args);

#ifndef UNIX_BLUETOOTH_CALL
#define UNIX_BLUETOOTH_CALL(func, params) wine_unix_call(unix_##func, params)
#endif

#endif /* _WINE_WINEBTH_UNIXLIB_H */

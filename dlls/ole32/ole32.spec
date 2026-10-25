# ole32.dll export specification
# SPDX-License-Identifier: LGPL-2.1-or-later

@ stdcall CoInitialize(ptr)
@ stdcall CoInitializeEx(ptr long)
@ stdcall CoUninitialize()
@ stdcall CoGetPSClsid(ptr ptr)
@ stdcall CoRegisterPSClsid(ptr ptr)
@ stdcall CreateStreamOnHGlobal(ptr long ptr)
@ stdcall CoMarshalInterface(ptr ptr ptr long ptr long)
@ stdcall CoUnmarshalInterface(ptr ptr ptr)
@ stdcall CoReleaseMarshalData(ptr)
@ stdcall CoRegisterClassObject(ptr ptr long long ptr)
@ stdcall CoRevokeClassObject(long)
@ stdcall apartment_get_local_server_stream(ptr ptr)
@ stdcall start_rpcss()
@ stdcall scmdatabase_autostart_services_ordered(ptr)
@ stdcall scmdatabase_shutdown_services_ordered(ptr)
@ stdcall scmdatabase_control_service(ptr str long)

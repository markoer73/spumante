# Wine winegstreamer.dll Export Specification
# Derived from upstream Wine dlls/winegstreamer/winegstreamer.spec
# SPDX-License-Identifier: LGPL-2.1-or-later

@ stdcall -private DllCanUnloadNow()
@ stdcall -private DllGetClassObject(ptr ptr ptr)
@ stdcall -private DllRegisterServer()
@ stdcall -private DllUnregisterServer()
@ stdcall winegstreamer_create_wm_sync_reader(ptr ptr)
@ stdcall winegstreamer_create_video_decoder(ptr)
@ stdcall winegstreamer_create_wma_decoder(ptr ptr ptr)

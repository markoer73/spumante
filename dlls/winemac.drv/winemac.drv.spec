# Wine macOS Driver (winemac.drv) Export Specification
# Copyright 2026 Spumante Contributors
# SPDX-License-Identifier: LGPL-2.1-or-later

@ stdcall ActivateWindow(ptr ptr) macdrv_ActivateWindow
@ stdcall ChangeDisplaySettings(ptr wstr ptr long ptr) macdrv_ChangeDisplaySettings
@ stdcall ChangeDisplaySettingsExW(wstr ptr ptr long ptr) macdrv_ChangeDisplaySettingsExW
@ stdcall CreateClientSurface(ptr long long) macdrv_CreateClientSurface
@ stdcall DestroyWindow(ptr) macdrv_DestroyWindow
@ stdcall EnumDisplayDevicesW(wstr long ptr long) macdrv_EnumDisplayDevicesW
@ stdcall EnumDisplaySettingsExW(wstr long ptr long) macdrv_EnumDisplaySettingsExW
@ stdcall GetDeviceGammaRamp(ptr ptr) macdrv_GetDeviceGammaRamp
@ stdcall GetLayeredWindowAttributes(ptr ptr ptr ptr) macdrv_GetLayeredWindowAttributes
@ stdcall GetWindowStyleMasks(ptr long long ptr ptr) macdrv_GetWindowStyleMasks
@ stdcall OpenGLInit(long ptr ptr) macdrv_OpenGLInit
@ stdcall SetDesktopWindow(ptr) macdrv_SetDesktopWindow
@ stdcall SetDeviceGammaRamp(ptr ptr) macdrv_SetDeviceGammaRamp
@ stdcall SetLayeredWindowAttributes(ptr long long long) macdrv_SetLayeredWindowAttributes
@ stdcall SetParent(ptr ptr ptr) macdrv_SetParent
@ stdcall SetWindowRgn(ptr ptr long) macdrv_SetWindowRgn
@ stdcall SetWindowStyle(ptr long ptr) macdrv_SetWindowStyle
@ stdcall SetWindowText(ptr wstr) macdrv_SetWindowText
@ stdcall ShowWindow(ptr long ptr long) macdrv_ShowWindow
@ stdcall SysCommand(ptr long long ptr) macdrv_SysCommand
@ stdcall UpdateDisplayDevices(ptr ptr) macdrv_UpdateDisplayDevices
@ stdcall UpdateLayeredWindow(ptr long long) macdrv_UpdateLayeredWindow
@ stdcall WindowMessage(ptr long long long) macdrv_WindowMessage
@ stdcall WindowPosChanged(ptr ptr ptr long ptr ptr) macdrv_WindowPosChanged
@ stdcall WindowPosChanging(ptr long long ptr) macdrv_WindowPosChanging
@ stdcall macdrv_client_surface_acquire_metal_swapchain(ptr)
@ stdcall macdrv_surface_create(ptr long ptr)
@ stdcall macdrv_ChoosePixelFormat(ptr ptr)
@ stdcall macdrv_DescribePixelFormat(ptr long long ptr)
@ stdcall macdrv_SetPixelFormat(ptr long ptr)
@ stdcall macdrv_GetPixelFormat(ptr)
@ stdcall macdrv_wglCreateContext(ptr)
@ stdcall macdrv_wglMakeCurrent(ptr ptr ptr)
@ stdcall macdrv_wglSwapBuffers(ptr ptr)
@ stdcall macdrv_wglDeleteContext(ptr)

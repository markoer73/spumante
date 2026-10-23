# Wine mfplat.dll export specification
# SPDX-License-Identifier: LGPL-2.1-or-later

@ stdcall GetD3DFormatFromMFSubtype(ptr)
@ stdcall MFAllocateWorkQueue(ptr)
@ stdcall MFCalculateImageSize(ptr long long ptr)
@ stdcall MFCreateAlignedMemoryBuffer(long long ptr)
@ stdcall MFCreateAttributes(ptr long)
@ stdcall MFCreateGuid(ptr)
@ stdcall MFCreateMediaType(ptr)
@ stdcall MFCreateMemoryBuffer(long ptr)
@ stdcall MFLockPlatform()
@ stdcall MFShutdown()
@ stdcall MFStartup(long long)
@ stdcall MFUnlockPlatform()
@ stdcall MFVideoToolboxCreateDecoder(ptr ptr ptr)
@ stdcall MFVideoToolboxDecodeSample(ptr ptr long ptr)
@ stdcall MFVideoToolboxDestroyDecoder(ptr)
@ stdcall MFVideoToolboxQueryState(ptr ptr)
@ stub MFCreateDXGIDeviceManager
@ stub MFCreateVideoSampleAllocatorEx

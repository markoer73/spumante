/*
 * Upstream Wine winetest anti-regression suite for mfplat.dll
 * (MFStartup, MFShutdown, MFCreateMemoryBuffer, MFCreateAlignedMemoryBuffer,
 *  MFCreateAttributes, MFCreateMediaType, MFCreateGuid, GetD3DFormatFromMFSubtype,
 *  and macOS VideoToolbox/CoreMedia H.264 & HEVC hardware decode bridge)
 *
 * Adapted from upstream Wine dlls/mfplat/tests/mfplat.c
 * Copyright 2015-2024 Wine contributors
 * Copyright 2026 Wine Parity Contributors
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "../../include/wine/test.h"
#include "../../../dlls/mfplat/mfplat.h"

static const BYTE wt_h264_seq_header[] = {
    0x00, 0x00, 0x00, 0x01,
    0x27, 0x64, 0x00, 0x0b, 0xac, 0x56, 0x50, 0xc3, 0x78, 0x10, 0x61, 0x14,
    0x00, 0x00, 0x00, 0x01,
    0x28, 0xee, 0x3c, 0xb0
};

static const BYTE wt_h264_idr_sample[] = {
    0x00, 0x00, 0x00, 0x01,
    0x25, 0xb8, 0x20, 0x1f, 0xfe, 0xc3, 0xd9, 0x18, 0xf8, 0x7e, 0x95, 0xb1,
    0xd2, 0x00, 0x0a, 0x8f, 0x7c, 0xe7, 0xc7, 0xf9, 0x3d, 0xf4, 0xbd, 0x7f,
    0xc8, 0xae, 0x36, 0xc0, 0xbd, 0xfc, 0x58, 0xfc, 0xd2, 0xd0, 0x41, 0x98,
    0x20, 0x47, 0xc1, 0x97, 0x00, 0xfb, 0xb8, 0x28, 0xef, 0xf8, 0xfe, 0x87,
    0x00, 0x49, 0x4b, 0x5a, 0x59, 0x74, 0x10, 0x5b, 0x95, 0x79, 0xb9, 0x91,
    0x91, 0xca, 0x8d, 0x00, 0x53, 0xbe, 0x20, 0x94, 0xe8, 0xad, 0xc0
};

static const BYTE wt_hevc_seq_header[] = {
    0x00, 0x00, 0x00, 0x01,
    0x40, 0x01, 0x0c, 0x01, 0xff, 0xff, 0x01, 0x60, 0x00, 0x00, 0x03, 0x00,
    0xb0, 0x00, 0x00, 0x03, 0x00, 0x00, 0x03, 0x00, 0x1e, 0x15, 0xc0, 0x90,
    0x00, 0x00, 0x00, 0x01,
    0x42, 0x01, 0x01, 0x01, 0x60, 0x00, 0x00, 0x03, 0x00, 0xb0, 0x00, 0x00,
    0x03, 0x00, 0x00, 0x03, 0x00, 0x1e, 0xa0, 0x14, 0x20, 0x41, 0xc1, 0x8f,
    0x88, 0x15, 0xee, 0x45, 0x95, 0x10,
    0x00, 0x00, 0x00, 0x01,
    0x44, 0x01, 0xc0, 0x2c, 0xbd, 0x14, 0xd9
};

static const BYTE wt_hevc_idr_sample[] = {
    0x00, 0x00, 0x00, 0x01,
    0x28, 0x01, 0xaf, 0x32, 0x2d, 0x20, 0xe9, 0x4b, 0x2d, 0x61, 0x34, 0x4e,
    0xff, 0xf5, 0x55, 0x0f, 0xfe, 0x95, 0x24, 0xff, 0x2b, 0x93, 0x9e, 0x4d,
    0x24, 0x6f, 0x40, 0x91, 0x6e, 0x37, 0x07, 0xee, 0xa2, 0x53, 0x19
};

static void test_startup(void)
{
    DWORD queue = 0;
    HRESULT hr;

    hr = MFStartup(0xdead0070UL, MFSTARTUP_FULL);
    ok(hr == MF_E_BAD_STARTUP_VERSION, "Unexpected hr 0x%08x\n", (unsigned)hr);

    hr = MFStartup(0x00020000UL, MFSTARTUP_FULL);
    ok(hr == MF_E_BAD_STARTUP_VERSION, "Unexpected hr 0x%08x\n", (unsigned)hr);

    hr = MFAllocateWorkQueue(&queue);
    ok(hr == MF_E_SHUTDOWN, "Expected MF_E_SHUTDOWN before startup, got 0x%08x\n", (unsigned)hr);

    hr = MFStartup(MF_VERSION_WIN7, MFSTARTUP_FULL);
    ok(hr == S_OK, "MFStartup(WIN7) failed: 0x%08x\n", (unsigned)hr);

    hr = MFStartup(MF_VERSION_XP, MFSTARTUP_LITE);
    ok(hr == S_OK, "MFStartup(XP) failed: 0x%08x\n", (unsigned)hr);

    hr = MFLockPlatform();
    ok(hr == S_OK, "MFLockPlatform failed: 0x%08x\n", (unsigned)hr);

    hr = MFUnlockPlatform();
    ok(hr == S_OK, "MFUnlockPlatform failed: 0x%08x\n", (unsigned)hr);

    hr = MFAllocateWorkQueue(NULL);
    ok(hr == E_INVALIDARG, "Expected E_INVALIDARG, got 0x%08x\n", (unsigned)hr);

    hr = MFAllocateWorkQueue(&queue);
    ok(hr == S_OK && queue != 0, "MFAllocateWorkQueue failed: 0x%08x queue=%u\n",
       (unsigned)hr, (unsigned)queue);

    hr = MFShutdown();
    ok(hr == S_OK, "MFShutdown #1 failed: 0x%08x\n", (unsigned)hr);

    hr = MFAllocateWorkQueue(&queue);
    ok(hr == S_OK, "Platform should still be active with refcount 1, got 0x%08x\n", (unsigned)hr);

    hr = MFShutdown();
    ok(hr == S_OK, "MFShutdown #2 failed: 0x%08x\n", (unsigned)hr);

    hr = MFAllocateWorkQueue(&queue);
    ok(hr == MF_E_SHUTDOWN, "Expected MF_E_SHUTDOWN after final shutdown, got 0x%08x\n", (unsigned)hr);

    hr = MFShutdown();
    ok(hr == S_OK, "Extra MFShutdown should return S_OK, got 0x%08x\n", (unsigned)hr);
}

static void test_memory_buffer(void)
{
    IMFMediaBuffer *buf = NULL;
    IUnknown *unk = NULL;
    BYTE *data1 = NULL, *data2 = NULL;
    DWORD max_len = 0, cur_len = 0;
    HRESULT hr;

    hr = MFCreateMemoryBuffer(0, NULL);
    ok(hr == E_INVALIDARG, "Expected E_INVALIDARG, got 0x%08x\n", (unsigned)hr);

    hr = MFCreateAlignedMemoryBuffer(0, MF_1_BYTE_ALIGNMENT, NULL);
    ok(hr == E_INVALIDARG, "Expected E_INVALIDARG, got 0x%08x\n", (unsigned)hr);

    hr = MFCreateMemoryBuffer(0, &buf);
    ok(hr == S_OK && buf != NULL, "MFCreateMemoryBuffer(0) failed: 0x%08x\n", (unsigned)hr);
    if (buf) {
        hr = buf->lpVtbl->GetMaxLength(buf, &max_len);
        ok(hr == S_OK && max_len == 0, "Expected max_len 0, got %u\n", (unsigned)max_len);
        hr = buf->lpVtbl->SetCurrentLength(buf, 1);
        ok(hr == E_INVALIDARG, "Expected E_INVALIDARG on overflow, got 0x%08x\n", (unsigned)hr);
        buf->lpVtbl->Release(buf);
    }

    hr = MFCreateAlignedMemoryBuffer(512, MF_64_BYTE_ALIGNMENT, &buf);
    ok(hr == S_OK && buf != NULL, "MFCreateAlignedMemoryBuffer failed: 0x%08x\n", (unsigned)hr);
    if (buf) {
        hr = buf->lpVtbl->QueryInterface(buf, &MFPLAT_IID_IUnknown, (void **)&unk);
        ok(hr == S_OK && unk != NULL, "QI(IUnknown) failed: 0x%08x\n", (unsigned)hr);
        if (unk) {
            unk->lpVtbl->Release(unk);
        }

        hr = buf->lpVtbl->QueryInterface(buf, &MFPLAT_IID_IMFAttributes, (void **)&unk);
        ok(hr == E_NOINTERFACE, "Expected E_NOINTERFACE, got 0x%08x\n", (unsigned)hr);

        hr = buf->lpVtbl->GetMaxLength(buf, NULL);
        ok(hr == E_INVALIDARG, "Expected E_INVALIDARG, got 0x%08x\n", (unsigned)hr);

        hr = buf->lpVtbl->GetCurrentLength(buf, NULL);
        ok(hr == E_INVALIDARG, "Expected E_INVALIDARG, got 0x%08x\n", (unsigned)hr);

        hr = buf->lpVtbl->Lock(buf, NULL, &max_len, &cur_len);
        ok(hr == E_INVALIDARG, "Expected E_INVALIDARG, got 0x%08x\n", (unsigned)hr);

        hr = buf->lpVtbl->SetCurrentLength(buf, 128);
        ok(hr == S_OK, "SetCurrentLength(128) failed: 0x%08x\n", (unsigned)hr);

        hr = buf->lpVtbl->Lock(buf, &data1, &max_len, &cur_len);
        ok(hr == S_OK && data1 != NULL, "Lock #1 failed: 0x%08x\n", (unsigned)hr);
        ok(((uintptr_t)data1 & 63U) == 0, "Buffer not 64-byte aligned: %p\n", (void *)data1);
        ok(max_len == 512 && cur_len == 128, "Unexpected lengths max=%u cur=%u\n",
           (unsigned)max_len, (unsigned)cur_len);

        hr = buf->lpVtbl->Lock(buf, &data2, NULL, NULL);
        ok(hr == S_OK && data2 == data1, "Lock #2 pointer mismatch\n");

        hr = buf->lpVtbl->Unlock(buf);
        ok(hr == S_OK, "Unlock #1 failed: 0x%08x\n", (unsigned)hr);
        hr = buf->lpVtbl->Unlock(buf);
        ok(hr == S_OK, "Unlock #2 failed: 0x%08x\n", (unsigned)hr);

        buf->lpVtbl->Release(buf);
    }
}

static void test_attributes(void)
{
    static const WCHAR k_str[] = { 'V', 'i', 'd', 'e', 'o', 0 };
    static const UINT8 k_blob[] = { 0xde, 0xad, 0xbe, 0xef };
    IMFAttributes *attrs = NULL;
    IMFAttributes *copy = NULL;
    MF_ATTRIBUTE_TYPE item_type = (MF_ATTRIBUTE_TYPE)0;
    UINT32 val32 = 0, count = 0, str_len = 0, blob_len = 0;
    UINT64 val64 = 0;
    double val_dbl = 0.0;
    GUID val_guid = { 0 };
    WCHAR str_buf[16] = { 0 };
    LPWSTR alloc_str = NULL;
    UINT8 blob_buf[8] = { 0 };
    UINT8 *alloc_blob = NULL;
    BOOL match = FALSE;
    HRESULT hr;

    hr = MFCreateAttributes(NULL, 0);
    ok(hr == E_INVALIDARG, "Expected E_INVALIDARG, got 0x%08x\n", (unsigned)hr);

    hr = MFCreateAttributes(&attrs, 2);
    ok(hr == S_OK && attrs != NULL, "MFCreateAttributes failed: 0x%08x\n", (unsigned)hr);
    hr = MFCreateAttributes(&copy, 0);
    ok(hr == S_OK && copy != NULL, "MFCreateAttributes(copy) failed: 0x%08x\n", (unsigned)hr);

    if (attrs && copy) {
        hr = attrs->lpVtbl->GetUINT32(attrs, &MF_MT_SAMPLE_SIZE, &val32);
        ok(hr == MF_E_ATTRIBUTENOTFOUND, "Expected MF_E_ATTRIBUTENOTFOUND, got 0x%08x\n", (unsigned)hr);

        hr = attrs->lpVtbl->SetUINT32(attrs, &MF_MT_SAMPLE_SIZE, 4096U);
        ok(hr == S_OK, "SetUINT32 failed: 0x%08x\n", (unsigned)hr);

        hr = attrs->lpVtbl->GetItemType(attrs, &MF_MT_SAMPLE_SIZE, &item_type);
        ok(hr == S_OK && item_type == MF_ATTRIBUTE_UINT32, "Unexpected item_type %d\n", (int)item_type);

        hr = attrs->lpVtbl->GetUINT64(attrs, &MF_MT_SAMPLE_SIZE, &val64);
        ok(hr == MF_E_INVALIDTYPE, "Expected MF_E_INVALIDTYPE, got 0x%08x\n", (unsigned)hr);

        hr = attrs->lpVtbl->SetUINT64(attrs, &MF_MT_FRAME_SIZE, Pack2UINT32AsUINT64(1280U, 720U));
        ok(hr == S_OK, "SetUINT64 failed: 0x%08x\n", (unsigned)hr);

        hr = attrs->lpVtbl->SetDouble(attrs, &MF_MT_FRAME_RATE, 59.94);
        ok(hr == S_OK, "SetDouble failed: 0x%08x\n", (unsigned)hr);
        hr = attrs->lpVtbl->GetDouble(attrs, &MF_MT_FRAME_RATE, &val_dbl);
        ok(hr == S_OK && val_dbl > 59.93 && val_dbl < 59.95, "GetDouble mismatch\n");

        hr = attrs->lpVtbl->SetGUID(attrs, &MF_MT_SUBTYPE, &MFVideoFormat_H264);
        ok(hr == S_OK, "SetGUID failed: 0x%08x\n", (unsigned)hr);
        hr = attrs->lpVtbl->GetGUID(attrs, &MF_MT_SUBTYPE, &val_guid);
        ok(hr == S_OK && IsEqualGUID(&val_guid, &MFVideoFormat_H264), "GetGUID mismatch\n");

        hr = attrs->lpVtbl->SetString(attrs, &MF_MT_USER_DATA, k_str);
        ok(hr == S_OK, "SetString failed: 0x%08x\n", (unsigned)hr);
        hr = attrs->lpVtbl->GetString(attrs, &MF_MT_USER_DATA, str_buf, 3, &str_len);
        ok(hr == STRSAFE_E_INSUFFICIENT_BUFFER && str_len == 5U,
           "Expected STRSAFE_E_INSUFFICIENT_BUFFER, got 0x%08x len=%u\n", (unsigned)hr, (unsigned)str_len);
        hr = attrs->lpVtbl->GetAllocatedString(attrs, &MF_MT_USER_DATA, &alloc_str, &str_len);
        ok(hr == S_OK && alloc_str != NULL && str_len == 5U, "GetAllocatedString failed\n");
        CoTaskMemFree(alloc_str);

        hr = attrs->lpVtbl->SetBlob(attrs, &MF_MT_MPEG_SEQUENCE_HEADER, k_blob, sizeof(k_blob));
        ok(hr == S_OK, "SetBlob failed: 0x%08x\n", (unsigned)hr);
        hr = attrs->lpVtbl->GetBlob(attrs, &MF_MT_MPEG_SEQUENCE_HEADER, blob_buf, 2, &blob_len);
        ok(hr == E_NOT_SUFFICIENT_BUFFER && blob_len == 4U,
           "Expected E_NOT_SUFFICIENT_BUFFER, got 0x%08x\n", (unsigned)hr);
        hr = attrs->lpVtbl->GetAllocatedBlob(attrs, &MF_MT_MPEG_SEQUENCE_HEADER, &alloc_blob, &blob_len);
        ok(hr == S_OK && alloc_blob != NULL && blob_len == 4U &&
           memcmp(alloc_blob, k_blob, sizeof(k_blob)) == 0, "GetAllocatedBlob failed\n");
        CoTaskMemFree(alloc_blob);

        hr = attrs->lpVtbl->CopyAllItems(attrs, copy);
        ok(hr == S_OK, "CopyAllItems failed: 0x%08x\n", (unsigned)hr);
        hr = attrs->lpVtbl->Compare(attrs, copy, MF_ATTRIBUTES_MATCH_ALL_ITEMS, &match);
        ok(hr == S_OK && match == TRUE, "Compare(ALL_ITEMS) failed\n");

        hr = copy->lpVtbl->DeleteItem(copy, &MF_MT_FRAME_RATE);
        ok(hr == S_OK, "DeleteItem failed: 0x%08x\n", (unsigned)hr);
        hr = attrs->lpVtbl->Compare(attrs, copy, MF_ATTRIBUTES_MATCH_ALL_ITEMS, &match);
        ok(hr == S_OK && match == FALSE, "Compare(ALL_ITEMS) after delete should be FALSE\n");
        hr = attrs->lpVtbl->Compare(attrs, copy, MF_ATTRIBUTES_MATCH_THEIR_ITEMS, &match);
        ok(hr == S_OK && match == TRUE, "Compare(THEIR_ITEMS) should be TRUE\n");

        hr = copy->lpVtbl->DeleteAllItems(copy);
        ok(hr == S_OK, "DeleteAllItems failed: 0x%08x\n", (unsigned)hr);
        hr = copy->lpVtbl->GetCount(copy, &count);
        ok(hr == S_OK && count == 0U, "Expected count 0 after DeleteAllItems, got %u\n", (unsigned)count);
    }

    if (copy) {
        copy->lpVtbl->Release(copy);
    }
    if (attrs) {
        attrs->lpVtbl->Release(attrs);
    }
}

static void test_mediatype(void)
{
    IMFMediaType *mt1 = NULL, *mt2 = NULL;
    IMFAttributes *attr_iface = NULL;
    GUID major = { 0 };
    BOOL compressed = FALSE;
    DWORD flags = 0;
    HRESULT hr;

    hr = MFCreateMediaType(NULL);
    ok(hr == E_INVALIDARG, "Expected E_INVALIDARG, got 0x%08x\n", (unsigned)hr);

    hr = MFCreateMediaType(&mt1);
    ok(hr == S_OK && mt1 != NULL, "MFCreateMediaType #1 failed: 0x%08x\n", (unsigned)hr);
    hr = MFCreateMediaType(&mt2);
    ok(hr == S_OK && mt2 != NULL, "MFCreateMediaType #2 failed: 0x%08x\n", (unsigned)hr);

    if (mt1 && mt2) {
        hr = mt1->lpVtbl->QueryInterface(mt1, &MFPLAT_IID_IMFAttributes, (void **)&attr_iface);
        ok(hr == S_OK && attr_iface != NULL, "QI(IMFAttributes) failed: 0x%08x\n", (unsigned)hr);
        if (attr_iface) {
            attr_iface->lpVtbl->Release(attr_iface);
        }

        hr = mt1->lpVtbl->GetMajorType(mt1, &major);
        ok(hr == MF_E_ATTRIBUTENOTFOUND, "Expected MF_E_ATTRIBUTENOTFOUND, got 0x%08x\n", (unsigned)hr);

        mt1->lpVtbl->SetGUID(mt1, &MF_MT_MAJOR_TYPE, &MFMediaType_Video);
        mt1->lpVtbl->SetGUID(mt1, &MF_MT_SUBTYPE, &MFVideoFormat_HEVC);
        hr = mt1->lpVtbl->GetMajorType(mt1, &major);
        ok(hr == S_OK && IsEqualGUID(&major, &MFMediaType_Video), "GetMajorType mismatch\n");

        hr = mt1->lpVtbl->IsCompressedFormat(mt1, &compressed);
        ok(hr == S_OK && compressed == TRUE, "HEVC should be reported as compressed\n");

        mt1->lpVtbl->CopyAllItems(mt1, (IMFAttributes *)mt2);
        hr = mt1->lpVtbl->IsEqual(mt1, mt2, &flags);
        ok(hr == S_OK && flags == 0x0fU, "Expected S_OK and flags 0x0f, got 0x%08x flags=0x%x\n",
           (unsigned)hr, (unsigned)flags);

        mt2->lpVtbl->SetGUID(mt2, &MF_MT_SUBTYPE, &MFVideoFormat_P010);
        hr = mt2->lpVtbl->IsCompressedFormat(mt2, &compressed);
        ok(hr == S_OK && compressed == FALSE, "P010 should be reported as uncompressed\n");

        hr = mt1->lpVtbl->IsEqual(mt1, mt2, &flags);
        ok(hr == S_FALSE && (flags & MF_MEDIATYPE_EQUAL_MAJOR_TYPES) != 0 &&
           (flags & MF_MEDIATYPE_EQUAL_FORMAT_TYPES) == 0,
           "Expected S_FALSE with major match only, got 0x%08x flags=0x%x\n",
           (unsigned)hr, (unsigned)flags);
    }

    if (mt2) {
        mt2->lpVtbl->Release(mt2);
    }
    if (mt1) {
        mt1->lpVtbl->Release(mt1);
    }
}

static void test_guid_and_d3dformat(void)
{
    GUID g1 = { 0 }, g2 = { 0 };
    UINT32 size = 0;
    HRESULT hr;

    hr = MFCreateGuid(NULL);
    ok(hr == E_INVALIDARG, "Expected E_INVALIDARG, got 0x%08x\n", (unsigned)hr);

    hr = MFCreateGuid(&g1);
    ok(hr == S_OK, "MFCreateGuid #1 failed: 0x%08x\n", (unsigned)hr);
    hr = MFCreateGuid(&g2);
    ok(hr == S_OK, "MFCreateGuid #2 failed: 0x%08x\n", (unsigned)hr);
    ok(!IsEqualGUID(&g1, &g2), "Generated GUIDs must be unique\n");

    ok(GetD3DFormatFromMFSubtype(NULL) == D3DFMT_UNKNOWN, "NULL subtype should return 0\n");
    ok(GetD3DFormatFromMFSubtype(&MF_MT_MAJOR_TYPE) == D3DFMT_UNKNOWN, "Non-video GUID should return 0\n");
    ok(GetD3DFormatFromMFSubtype(&MFVideoFormat_RGB32) == D3DFMT_X8R8G8B8, "RGB32 mismatch\n");
    ok(GetD3DFormatFromMFSubtype(&MFVideoFormat_ARGB32) == D3DFMT_A8R8G8B8, "ARGB32 mismatch\n");
    ok(GetD3DFormatFromMFSubtype(&MFVideoFormat_NV12) == D3DFMT_NV12, "NV12 mismatch\n");
    ok(GetD3DFormatFromMFSubtype(&MFVideoFormat_P010) == D3DFMT_P010, "P010 mismatch\n");
    ok(GetD3DFormatFromMFSubtype(&MFVideoFormat_H264) == D3DFMT_H264, "H264 mismatch\n");
    ok(GetD3DFormatFromMFSubtype(&MFVideoFormat_HEVC) == D3DFMT_HEVC, "HEVC mismatch\n");

    hr = MFCalculateImageSize(&MFVideoFormat_NV12, 64U, 64U, &size);
    ok(hr == S_OK && size == 6144U, "NV12 64x64 size expected 6144, got %u\n", (unsigned)size);

    hr = MFCalculateImageSize(&MFVideoFormat_P010, 64U, 64U, &size);
    ok(hr == S_OK && size == 12288U, "P010 64x64 size expected 12288, got %u\n", (unsigned)size);

    hr = MFCalculateImageSize(&MFVideoFormat_RGB32, 64U, 64U, &size);
    ok(hr == S_OK && size == 16384U, "RGB32 64x64 size expected 16384, got %u\n", (unsigned)size);

    hr = MFCalculateImageSize(&MFVideoFormat_H264, 64U, 64U, &size);
    ok(hr == MF_E_INVALIDMEDIATYPE, "Compressed subtype should return MF_E_INVALIDMEDIATYPE\n");
}

static void test_videotoolbox_hardware_decode(void)
{
    IMFMediaType *in_type = NULL, *out_type = NULL;
    MF_VIDEOTOOLBOX_DECODER *dec = NULL;
    IMFMediaBuffer *out_buf = NULL;
    MF_VIDEOTOOLBOX_STATE state;
    DWORD cur_len = 0;
    HRESULT hr;

    memset(&state, 0, sizeof(state));
    MFStartup(MF_VERSION_WIN7, MFSTARTUP_FULL);

    hr = MFVideoToolboxCreateDecoder(NULL, NULL, NULL);
    ok(hr == E_INVALIDARG, "Expected E_INVALIDARG, got 0x%08x\n", (unsigned)hr);

    MFCreateMediaType(&in_type);
    MFCreateMediaType(&out_type);
    if (in_type && out_type) {
        /* 1. H.264 -> NV12 hardware decode */
        in_type->lpVtbl->SetGUID(in_type, &MF_MT_MAJOR_TYPE, &MFMediaType_Video);
        in_type->lpVtbl->SetGUID(in_type, &MF_MT_SUBTYPE, &MFVideoFormat_H264);
        MFSetAttributeSize((IMFAttributes *)in_type, &MF_MT_FRAME_SIZE, 64U, 64U);
        in_type->lpVtbl->SetBlob(in_type, &MF_MT_MPEG_SEQUENCE_HEADER,
                                 wt_h264_seq_header, sizeof(wt_h264_seq_header));

        out_type->lpVtbl->SetGUID(out_type, &MF_MT_MAJOR_TYPE, &MFMediaType_Video);
        out_type->lpVtbl->SetGUID(out_type, &MF_MT_SUBTYPE, &MFVideoFormat_NV12);
        MFSetAttributeSize((IMFAttributes *)out_type, &MF_MT_FRAME_SIZE, 64U, 64U);

        hr = MFVideoToolboxCreateDecoder(in_type, out_type, &dec);
        ok(hr == S_OK && dec != NULL, "MFVideoToolboxCreateDecoder(H264) failed: 0x%08x\n", (unsigned)hr);
        if (dec) {
            hr = MFVideoToolboxDecodeSample(dec, wt_h264_idr_sample,
                                            (DWORD)sizeof(wt_h264_idr_sample), &out_buf);
            ok(hr == S_OK && out_buf != NULL, "MFVideoToolboxDecodeSample(H264) failed: 0x%08x\n", (unsigned)hr);
            if (out_buf) {
                out_buf->lpVtbl->GetCurrentLength(out_buf, &cur_len);
                ok(cur_len == 6144U, "Expected 6144 NV12 bytes, got %u\n", (unsigned)cur_len);
                out_buf->lpVtbl->Release(out_buf);
                out_buf = NULL;
            }
            hr = MFVideoToolboxQueryState(dec, &state);
            ok(hr == S_OK && state.decoded_frames == 1U, "QueryState(H264) failed\n");
#ifdef __APPLE__
            todo_wine ok(state.vt_session_ready && state.iosurface_backed,
                         "Expected macOS VideoToolbox session with IOSurface-backed CVPixelBuffer\n");
#endif
            MFVideoToolboxDestroyDecoder(dec);
            dec = NULL;
        }

        /* 2. HEVC -> P010 hardware decode */
        in_type->lpVtbl->SetGUID(in_type, &MF_MT_SUBTYPE, &MFVideoFormat_HEVC);
        in_type->lpVtbl->SetBlob(in_type, &MF_MT_MPEG_SEQUENCE_HEADER,
                                 wt_hevc_seq_header, sizeof(wt_hevc_seq_header));
        out_type->lpVtbl->SetGUID(out_type, &MF_MT_SUBTYPE, &MFVideoFormat_P010);

        hr = MFVideoToolboxCreateDecoder(in_type, out_type, &dec);
        ok(hr == S_OK && dec != NULL, "MFVideoToolboxCreateDecoder(HEVC) failed: 0x%08x\n", (unsigned)hr);
        if (dec) {
            hr = MFVideoToolboxDecodeSample(dec, wt_hevc_idr_sample,
                                            (DWORD)sizeof(wt_hevc_idr_sample), &out_buf);
            ok(hr == S_OK && out_buf != NULL, "MFVideoToolboxDecodeSample(HEVC) failed: 0x%08x\n", (unsigned)hr);
            if (out_buf) {
                out_buf->lpVtbl->GetCurrentLength(out_buf, &cur_len);
                ok(cur_len == 12288U, "Expected 12288 P010 bytes, got %u\n", (unsigned)cur_len);
                out_buf->lpVtbl->Release(out_buf);
                out_buf = NULL;
            }
            MFVideoToolboxDestroyDecoder(dec);
        }
    }

    if (out_type) {
        out_type->lpVtbl->Release(out_type);
    }
    if (in_type) {
        in_type->lpVtbl->Release(in_type);
    }
    MFShutdown();
}

START_TEST(mfplat)
{
    WINETEST_RUN_SUBTEST(test_startup);
    WINETEST_RUN_SUBTEST(test_memory_buffer);
    WINETEST_RUN_SUBTEST(test_attributes);
    WINETEST_RUN_SUBTEST(test_mediatype);
    WINETEST_RUN_SUBTEST(test_guid_and_d3dformat);
    WINETEST_RUN_SUBTEST(test_videotoolbox_hardware_decode);
}

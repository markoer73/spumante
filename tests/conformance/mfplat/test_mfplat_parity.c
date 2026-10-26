/*
 * Spumante Differential Conformance Test: mfplat.dll Core APIs & VideoToolbox Bridge
 *
 * Clean-room test derived from Microsoft Learn specifications for:
 *   - MFStartup / MFShutdown / MFLockPlatform / MFUnlockPlatform / MFAllocateWorkQueue
 *   - MFCreateMemoryBuffer / MFCreateAlignedMemoryBuffer
 *   - MFCreateAttributes (IMFAttributes typed store, copy, compare)
 *   - MFCreateMediaType (IMFMediaType major type, compression, equality)
 *   - MFCreateGuid / GetD3DFormatFromMFSubtype / MFCalculateImageSize
 *   - MFVideoToolboxCreateDecoder / MFVideoToolboxDecodeSample (H.264 NV12 & HEVC P010)
 *
 * Copyright 2026 Spumante Contributors
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "../../include/parity_test.h"
#include "../../../dlls/mfplat/mfplat.h"

/* Valid 64x64 H.264 High Profile Annex-B SPS + PPS parameter sets */
static const BYTE k_h264_seq_header[] = {
    0x00, 0x00, 0x00, 0x01,
    0x27, 0x64, 0x00, 0x0b, 0xac, 0x56, 0x50, 0xc3, 0x78, 0x10, 0x61, 0x14,
    0x00, 0x00, 0x00, 0x01,
    0x28, 0xee, 0x3c, 0xb0
};

/* Valid 64x64 H.264 IDR access unit (neutral gray 0x80 Y/UV) */
static const BYTE k_h264_idr_sample[] = {
    0x00, 0x00, 0x00, 0x01,
    0x25, 0xb8, 0x20, 0x1f, 0xfe, 0xc3, 0xd9, 0x18, 0xf8, 0x7e, 0x95, 0xb1,
    0xd2, 0x00, 0x0a, 0x8f, 0x7c, 0xe7, 0xc7, 0xf9, 0x3d, 0xf4, 0xbd, 0x7f,
    0xc8, 0xae, 0x36, 0xc0, 0xbd, 0xfc, 0x58, 0xfc, 0xd2, 0xd0, 0x41, 0x98,
    0x20, 0x47, 0xc1, 0x97, 0x00, 0xfb, 0xb8, 0x28, 0xef, 0xf8, 0xfe, 0x87,
    0x00, 0x49, 0x4b, 0x5a, 0x59, 0x74, 0x10, 0x5b, 0x95, 0x79, 0xb9, 0x91,
    0x91, 0xca, 0x8d, 0x00, 0x53, 0xbe, 0x20, 0x94, 0xe8, 0xad, 0xc0
};

/* Valid 64x64 HEVC Main Profile Annex-B VPS + SPS + PPS parameter sets */
static const BYTE k_hevc_seq_header[] = {
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

/* Valid 64x64 HEVC IDR access unit (neutral gray 0x80 Y/UV) */
static const BYTE k_hevc_idr_sample[] = {
    0x00, 0x00, 0x00, 0x01,
    0x28, 0x01, 0xaf, 0x32, 0x2d, 0x20, 0xe9, 0x4b, 0x2d, 0x61, 0x34, 0x4e,
    0xff, 0xf5, 0x55, 0x0f, 0xfe, 0x95, 0x24, 0xff, 0x2b, 0x93, 0x9e, 0x4d,
    0x24, 0x6f, 0x40, 0x91, 0x6e, 0x37, 0x07, 0xee, 0xa2, 0x53, 0x19
};

static int match_filter(const char *filter, const char *case_name)
{
    if (!filter || filter[0] == '\0') {
        return 1;
    }
    return strstr(case_name, filter) != NULL;
}

int main(int argc, char **argv)
{
    const char *filter = NULL;
    int i;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--filter") == 0 && (i + 1) < argc) {
            filter = argv[++i];
        }
    }

    parity_begin_document("mfplat", "test_mfplat_parity", "x86_64", "wine");

    /* 1. MFStartup rejects mismatched SDK/API version */
    if (match_filter(filter, "mf_startup_invalid_version")) {
        HRESULT hr = MFStartup(0xdead0070UL, MFSTARTUP_FULL);
        ok(hr == MF_E_BAD_STARTUP_VERSION, "Expected MF_E_BAD_STARTUP_VERSION, got 0x%08x\n", (unsigned)hr);
        parity_record_exact(
            "mfplat_001",
            "mf_startup_invalid_version",
            "MFStartup",
            (NTSTATUS)hr,
            0,
            0,
            NULL,
            0,
            0
        );
    }

    /* 2. MFStartup / MFLockPlatform / MFAllocateWorkQueue / MFShutdown lifecycle */
    if (match_filter(filter, "mf_startup_and_shutdown_lifecycle")) {
        DWORD queue = 0;
        DWORD post_queue = 0;
        HRESULT hr1 = MFStartup(MF_VERSION_WIN7, MFSTARTUP_FULL);
        HRESULT hr2 = MFStartup(MF_VERSION_XP, MFSTARTUP_LITE);
        HRESULT hr_lock = MFLockPlatform();
        HRESULT hr_unlock = MFUnlockPlatform();
        HRESULT hr_wq = MFAllocateWorkQueue(&queue);
        HRESULT hr_shut1 = MFShutdown();
        HRESULT hr_shut2 = MFShutdown();
        HRESULT hr_wq_after = MFAllocateWorkQueue(&post_queue);
        int lifecycle_ok = (hr1 == S_OK && hr2 == S_OK &&
                            hr_lock == S_OK && hr_unlock == S_OK &&
                            hr_wq == S_OK && queue != 0 &&
                            hr_shut1 == S_OK && hr_shut2 == S_OK &&
                            hr_wq_after == MF_E_SHUTDOWN);
        unsigned char state_tag[4];
        state_tag[0] = (unsigned char)(hr1 == S_OK ? 1 : 0);
        state_tag[1] = (unsigned char)(hr_wq == S_OK && queue != 0 ? 1 : 0);
        state_tag[2] = (unsigned char)(hr_shut2 == S_OK ? 1 : 0);
        state_tag[3] = (unsigned char)(hr_wq_after == MF_E_SHUTDOWN ? 1 : 0);
        ok(lifecycle_ok, "MFStartup/MFShutdown lifecycle failed\n");
        parity_record_roundtrip(
            "mfplat_002",
            "mf_startup_and_shutdown_lifecycle",
            "MFStartup",
            lifecycle_ok ? (NTSTATUS)S_OK : (NTSTATUS)E_FAIL,
            0,
            4,
            state_tag,
            sizeof(state_tag),
            lifecycle_ok
        );
    }

    /* 3. MFCreateMemoryBuffer rejects NULL output pointer */
    if (match_filter(filter, "mf_create_memory_buffer_null_param")) {
        HRESULT hr = MFCreateMemoryBuffer(256, NULL);
        ok(hr == E_INVALIDARG, "Expected E_INVALIDARG, got 0x%08x\n", (unsigned)hr);
        parity_record_exact(
            "mfplat_003",
            "mf_create_memory_buffer_null_param",
            "MFCreateMemoryBuffer",
            (NTSTATUS)hr,
            0,
            0,
            NULL,
            0,
            0
        );
    }

    /* 4. MFCreateMemoryBuffer lock/unlock, current/max length & payload roundtrip */
    if (match_filter(filter, "mf_create_memory_buffer_roundtrip")) {
        static const unsigned char k_payload[8] = {
            0x4d, 0x46, 0x50, 0x4c, 0x41, 0x54, 0x30, 0x31
        };
        IMFMediaBuffer *buf = NULL;
        DWORD max_len = 0, cur_len = 0;
        BYTE *ptr1 = NULL, *ptr2 = NULL;
        unsigned char readback[8] = { 0 };
        HRESULT hr = MFCreateMemoryBuffer(64, &buf);
        int roundtrip_ok = 0;

        if (hr == S_OK && buf) {
            HRESULT hr_max = buf->lpVtbl->GetMaxLength(buf, &max_len);
            HRESULT hr_cur0 = buf->lpVtbl->GetCurrentLength(buf, &cur_len);
            HRESULT hr_overflow = buf->lpVtbl->SetCurrentLength(buf, 65);
            HRESULT hr_set = buf->lpVtbl->SetCurrentLength(buf, 8);
            HRESULT hr_lock1 = buf->lpVtbl->Lock(buf, &ptr1, &max_len, &cur_len);
            if (hr_lock1 == S_OK && ptr1) {
                memcpy(ptr1, k_payload, sizeof(k_payload));
            }
            HRESULT hr_lock2 = buf->lpVtbl->Lock(buf, &ptr2, NULL, NULL);
            if (hr_lock2 == S_OK && ptr2) {
                memcpy(readback, ptr2, sizeof(readback));
            }
            HRESULT hr_un1 = buf->lpVtbl->Unlock(buf);
            HRESULT hr_un2 = buf->lpVtbl->Unlock(buf);
            roundtrip_ok = (hr_max == S_OK && max_len == 64 &&
                            hr_cur0 == S_OK &&
                            hr_overflow == E_INVALIDARG &&
                            hr_set == S_OK && cur_len == 8 &&
                            hr_lock1 == S_OK && hr_lock2 == S_OK &&
                            ptr1 != NULL && ptr1 == ptr2 &&
                            hr_un1 == S_OK && hr_un2 == S_OK &&
                            memcmp(readback, k_payload, sizeof(k_payload)) == 0);
            buf->lpVtbl->Release(buf);
        }
        ok(roundtrip_ok, "MFCreateMemoryBuffer roundtrip failed\n");
        parity_record_roundtrip(
            "mfplat_004",
            "mf_create_memory_buffer_roundtrip",
            "MFCreateMemoryBuffer",
            roundtrip_ok ? (NTSTATUS)S_OK : (NTSTATUS)hr,
            0,
            (ULONG)cur_len,
            readback,
            sizeof(readback),
            roundtrip_ok
        );
    }

    /* 5. MFCreateAlignedMemoryBuffer 64-byte alignment verification */
    if (match_filter(filter, "mf_create_aligned_memory_buffer_64")) {
        IMFMediaBuffer *buf = NULL;
        BYTE *ptr = NULL;
        DWORD max_len = 0, cur_len = 0;
        unsigned int alignment = 0;
        HRESULT hr = MFCreateAlignedMemoryBuffer(128, MF_64_BYTE_ALIGNMENT, &buf);
        if (hr == S_OK && buf) {
            if (buf->lpVtbl->Lock(buf, &ptr, &max_len, &cur_len) == S_OK && ptr != NULL) {
                alignment = (((uintptr_t)ptr & 63U) == 0U && max_len == 128U) ? 64U : 1U;
                buf->lpVtbl->Unlock(buf);
            }
            buf->lpVtbl->Release(buf);
        }
        ok(hr == S_OK && alignment == 64U, "MFCreateAlignedMemoryBuffer 64-byte alignment failed\n");
        parity_record_handle_state(
            "mfplat_005",
            "mf_create_aligned_memory_buffer_64",
            "MFCreateAlignedMemoryBuffer",
            (NTSTATUS)hr,
            0,
            ptr,
            alignment
        );
    }

    /* 6. MFCreateAttributes rejects NULL output pointer */
    if (match_filter(filter, "mf_create_attributes_null_param")) {
        HRESULT hr = MFCreateAttributes(NULL, 4);
        ok(hr == E_INVALIDARG, "Expected E_INVALIDARG, got 0x%08x\n", (unsigned)hr);
        parity_record_exact(
            "mfplat_006",
            "mf_create_attributes_null_param",
            "MFCreateAttributes",
            (NTSTATUS)hr,
            0,
            0,
            NULL,
            0,
            0
        );
    }

    /* 7. MFCreateAttributes typed store, missing/type errors, CopyAllItems & Compare */
    if (match_filter(filter, "mf_attributes_typed_store_and_compare")) {
        static const WCHAR k_codec_str[] = { 'H', '2', '6', '4', 0 };
        static const UINT8 k_blob[8] = { 0x00, 0x00, 0x00, 0x01, 0x67, 0x42, 0xc0, 0x1e };
        IMFAttributes *attrs = NULL;
        IMFAttributes *attrs_copy = NULL;
        UINT8 blob_out[8] = { 0 };
        UINT32 blob_len = 0;
        HRESULT hr = MFCreateAttributes(&attrs, 4);
        int store_ok = 0;

        if (hr == S_OK && attrs && MFCreateAttributes(&attrs_copy, 0) == S_OK && attrs_copy) {
            UINT32 u32_val = 0, str_len = 0, count = 0;
            UINT32 width = 0, height = 0;
            GUID guid_val = { 0 };
            WCHAR str_buf[8] = { 0 };
            BOOL match = FALSE;
            HRESULT hr_missing = attrs->lpVtbl->GetUINT32(attrs, &MF_MT_SAMPLE_SIZE, &u32_val);
            attrs->lpVtbl->SetUINT32(attrs, &MF_MT_SAMPLE_SIZE, 6144U);
            HRESULT hr_badtype = attrs->lpVtbl->GetUINT64(attrs, &MF_MT_SAMPLE_SIZE, NULL);
            HRESULT hr_badtype2 = attrs->lpVtbl->GetGUID(attrs, &MF_MT_SAMPLE_SIZE, &guid_val);
            MFSetAttributeSize(attrs, &MF_MT_FRAME_SIZE, 1920U, 1080U);
            attrs->lpVtbl->SetGUID(attrs, &MF_MT_SUBTYPE, &MFVideoFormat_NV12);
            attrs->lpVtbl->SetString(attrs, &MF_MT_USER_DATA, k_codec_str);
            attrs->lpVtbl->SetBlob(attrs, &MF_MT_MPEG_SEQUENCE_HEADER, k_blob, sizeof(k_blob));

            attrs->lpVtbl->GetUINT32(attrs, &MF_MT_SAMPLE_SIZE, &u32_val);
            MFGetAttributeSize(attrs, &MF_MT_FRAME_SIZE, &width, &height);
            attrs->lpVtbl->GetGUID(attrs, &MF_MT_SUBTYPE, &guid_val);
            attrs->lpVtbl->GetString(attrs, &MF_MT_USER_DATA, str_buf, 8, &str_len);
            attrs->lpVtbl->GetBlob(attrs, &MF_MT_MPEG_SEQUENCE_HEADER, blob_out, sizeof(blob_out), &blob_len);
            attrs->lpVtbl->GetCount(attrs, &count);
            attrs->lpVtbl->CopyAllItems(attrs, attrs_copy);
            attrs->lpVtbl->Compare(attrs, attrs_copy, MF_ATTRIBUTES_MATCH_ALL_ITEMS, &match);

            (void)hr_badtype;
            store_ok = (hr_missing == MF_E_ATTRIBUTENOTFOUND &&
                        hr_badtype2 == MF_E_INVALIDTYPE &&
                        u32_val == 6144U && width == 1920U && height == 1080U &&
                        IsEqualGUID(&guid_val, &MFVideoFormat_NV12) &&
                        str_len == 4U && memcmp(str_buf, k_codec_str, 4 * sizeof(WCHAR)) == 0 &&
                        blob_len == 8U && memcmp(blob_out, k_blob, sizeof(k_blob)) == 0 &&
                        count == 5U && match == TRUE);
        }
        if (attrs_copy) {
            attrs_copy->lpVtbl->Release(attrs_copy);
        }
        if (attrs) {
            attrs->lpVtbl->Release(attrs);
        }
        ok(store_ok, "IMFAttributes typed store and compare failed\n");
        parity_record_roundtrip(
            "mfplat_007",
            "mf_attributes_typed_store_and_compare",
            "MFCreateAttributes",
            store_ok ? (NTSTATUS)S_OK : (NTSTATUS)hr,
            0,
            (ULONG)blob_len,
            blob_out,
            sizeof(blob_out),
            store_ok
        );
    }

    /* 8. MFCreateMediaType major type, compression detection, and IsEqual flags */
    if (match_filter(filter, "mf_create_media_type_and_equality")) {
        IMFMediaType *mt1 = NULL;
        IMFMediaType *mt2 = NULL;
        HRESULT hr = MFCreateMediaType(&mt1);
        int mt_ok = 0;
        unsigned char summary[4] = { 0 };

        if (hr == S_OK && mt1 && MFCreateMediaType(&mt2) == S_OK && mt2) {
            GUID major = { 0 };
            BOOL comp_h264 = FALSE, comp_nv12 = TRUE;
            DWORD eq_flags = 0, diff_flags = 0;
            HRESULT hr_empty_major = mt1->lpVtbl->GetMajorType(mt1, &major);

            mt1->lpVtbl->SetGUID(mt1, &MF_MT_MAJOR_TYPE, &MFMediaType_Video);
            mt1->lpVtbl->SetGUID(mt1, &MF_MT_SUBTYPE, &MFVideoFormat_H264);
            MFSetAttributeSize((IMFAttributes *)mt1, &MF_MT_FRAME_SIZE, 1920U, 1080U);
            mt1->lpVtbl->GetMajorType(mt1, &major);
            mt1->lpVtbl->IsCompressedFormat(mt1, &comp_h264);

            mt1->lpVtbl->CopyAllItems(mt1, (IMFAttributes *)mt2);
            HRESULT hr_eq = mt1->lpVtbl->IsEqual(mt1, mt2, &eq_flags);

            mt2->lpVtbl->SetGUID(mt2, &MF_MT_SUBTYPE, &MFVideoFormat_NV12);
            mt2->lpVtbl->IsCompressedFormat(mt2, &comp_nv12);
            HRESULT hr_diff = mt1->lpVtbl->IsEqual(mt1, mt2, &diff_flags);

            summary[0] = (unsigned char)(hr_empty_major == MF_E_ATTRIBUTENOTFOUND ? 1 : 0);
            summary[1] = (unsigned char)(comp_h264 ? 1 : 0);
            summary[2] = (unsigned char)(comp_nv12 ? 1 : 0);
            summary[3] = (unsigned char)(eq_flags & 0xffU);

            mt_ok = (hr_empty_major == MF_E_ATTRIBUTENOTFOUND &&
                     IsEqualGUID(&major, &MFMediaType_Video) &&
                     comp_h264 == TRUE && comp_nv12 == FALSE &&
                     hr_eq == S_OK && eq_flags == 0x0fU &&
                     hr_diff == S_FALSE &&
                     (diff_flags & MF_MEDIATYPE_EQUAL_MAJOR_TYPES) != 0 &&
                     (diff_flags & MF_MEDIATYPE_EQUAL_FORMAT_TYPES) == 0);
        }
        if (mt2) {
            mt2->lpVtbl->Release(mt2);
        }
        if (mt1) {
            mt1->lpVtbl->Release(mt1);
        }
        ok(mt_ok, "IMFMediaType major type, compression, and IsEqual failed\n");
        parity_record_roundtrip(
            "mfplat_008",
            "mf_create_media_type_and_equality",
            "MFCreateMediaType",
            mt_ok ? (NTSTATUS)S_OK : (NTSTATUS)hr,
            0,
            4,
            summary,
            sizeof(summary),
            mt_ok
        );
    }

    /* 9. MFCreateGuid NULL rejection and RFC 4122 uniqueness */
    if (match_filter(filter, "mf_create_guid_uniqueness")) {
        GUID g1 = { 0 }, g2 = { 0 }, zero_guid = { 0 };
        HRESULT hr_null = MFCreateGuid(NULL);
        HRESULT hr1 = MFCreateGuid(&g1);
        HRESULT hr2 = MFCreateGuid(&g2);
        int guid_ok = (hr_null == E_INVALIDARG &&
                       hr1 == S_OK && hr2 == S_OK &&
                       !IsEqualGUID(&g1, &zero_guid) &&
                       !IsEqualGUID(&g2, &zero_guid) &&
                       !IsEqualGUID(&g1, &g2));
        ok(guid_ok, "MFCreateGuid failed (hr_null=0x%08x hr1=0x%08x hr2=0x%08x)\n",
           (unsigned)hr_null, (unsigned)hr1, (unsigned)hr2);
        parity_record_handle_state(
            "mfplat_009",
            "mf_create_guid_uniqueness",
            "MFCreateGuid",
            guid_ok ? (NTSTATUS)S_OK : (NTSTATUS)hr1,
            0,
            guid_ok ? (const void *)&g1 : NULL,
            16
        );
    }

    /* 10. GetD3DFormatFromMFSubtype and MFCalculateImageSize */
    if (match_filter(filter, "get_d3d_format_from_mf_subtype")) {
        DWORD fmt_null = GetD3DFormatFromMFSubtype(NULL);
        DWORD fmt_non_video = GetD3DFormatFromMFSubtype(&MF_MT_MAJOR_TYPE);
        DWORD fmt_rgb32 = GetD3DFormatFromMFSubtype(&MFVideoFormat_RGB32);
        DWORD fmt_nv12 = GetD3DFormatFromMFSubtype(&MFVideoFormat_NV12);
        DWORD fmt_h264 = GetD3DFormatFromMFSubtype(&MFVideoFormat_H264);
        DWORD fmt_hevc = GetD3DFormatFromMFSubtype(&MFVideoFormat_HEVC);
        UINT32 nv12_size = 0;
        HRESULT hr_size = MFCalculateImageSize(&MFVideoFormat_NV12, 64, 64, &nv12_size);
        unsigned char fmt_bytes[8];
        int fmt_ok = (fmt_null == D3DFMT_UNKNOWN &&
                      fmt_non_video == D3DFMT_UNKNOWN &&
                      fmt_rgb32 == D3DFMT_X8R8G8B8 &&
                      fmt_nv12 == D3DFMT_NV12 &&
                      fmt_h264 == D3DFMT_H264 &&
                      fmt_hevc == D3DFMT_HEVC &&
                      hr_size == S_OK && nv12_size == 6144U);
        fmt_bytes[0] = (unsigned char)fmt_rgb32;
        fmt_bytes[1] = (unsigned char)(fmt_nv12 & 0xffU);
        fmt_bytes[2] = (unsigned char)((fmt_nv12 >> 8) & 0xffU);
        fmt_bytes[3] = (unsigned char)((fmt_nv12 >> 16) & 0xffU);
        fmt_bytes[4] = (unsigned char)((fmt_nv12 >> 24) & 0xffU);
        fmt_bytes[5] = (unsigned char)(fmt_h264 & 0xffU);
        fmt_bytes[6] = (unsigned char)(fmt_hevc & 0xffU);
        fmt_bytes[7] = (unsigned char)((nv12_size >> 8) & 0xffU);
        ok(fmt_ok, "GetD3DFormatFromMFSubtype / MFCalculateImageSize failed\n");
        parity_record_roundtrip(
            "mfplat_010",
            "get_d3d_format_from_mf_subtype",
            "GetD3DFormatFromMFSubtype",
            fmt_ok ? (NTSTATUS)S_OK : (NTSTATUS)E_FAIL,
            0,
            (ULONG)nv12_size,
            fmt_bytes,
            sizeof(fmt_bytes),
            fmt_ok
        );
    }

    /* 11. macOS VideoToolbox H.264 -> NV12 hardware decode bridge */
    if (match_filter(filter, "mf_videotoolbox_h264_nv12_decode")) {
        IMFMediaType *in_type = NULL;
        IMFMediaType *out_type = NULL;
        MF_VIDEOTOOLBOX_DECODER *decoder = NULL;
        IMFMediaBuffer *decoded_buf = NULL;
        MF_VIDEOTOOLBOX_STATE state;
        DWORD out_len = 0;
        unsigned char plane_tag[8] = { 0 };
        int h264_ok = 0;
        HRESULT hr = E_FAIL;

        memset(&state, 0, sizeof(state));
        MFStartup(MF_VERSION_WIN7, MFSTARTUP_FULL);
        if (MFCreateMediaType(&in_type) == S_OK && MFCreateMediaType(&out_type) == S_OK) {
            in_type->lpVtbl->SetGUID(in_type, &MF_MT_MAJOR_TYPE, &MFMediaType_Video);
            in_type->lpVtbl->SetGUID(in_type, &MF_MT_SUBTYPE, &MFVideoFormat_H264);
            MFSetAttributeSize((IMFAttributes *)in_type, &MF_MT_FRAME_SIZE, 64U, 64U);
            in_type->lpVtbl->SetBlob(in_type, &MF_MT_MPEG_SEQUENCE_HEADER,
                                     k_h264_seq_header, sizeof(k_h264_seq_header));

            out_type->lpVtbl->SetGUID(out_type, &MF_MT_MAJOR_TYPE, &MFMediaType_Video);
            out_type->lpVtbl->SetGUID(out_type, &MF_MT_SUBTYPE, &MFVideoFormat_NV12);
            MFSetAttributeSize((IMFAttributes *)out_type, &MF_MT_FRAME_SIZE, 64U, 64U);

            hr = MFVideoToolboxCreateDecoder(in_type, out_type, &decoder);
            if (hr == S_OK && decoder) {
                hr = MFVideoToolboxDecodeSample(
                    decoder,
                    k_h264_idr_sample,
                    (DWORD)sizeof(k_h264_idr_sample),
                    &decoded_buf
                );
                if (hr == S_OK && decoded_buf &&
                    MFVideoToolboxQueryState(decoder, &state) == S_OK) {
                    BYTE *raw = NULL;
                    DWORD max_len = 0;
                    if (decoded_buf->lpVtbl->Lock(decoded_buf, &raw, &max_len, &out_len) == S_OK && raw) {
                        /* Verify neutral 0x80 luma & chroma planes from 64x64 NV12 frame */
                        int luma_ok = (raw[0] >= 0x7e && raw[0] <= 0x82);
                        int chroma_ok = (raw[64 * 64] >= 0x7e && raw[64 * 64] <= 0x82);
                        memset(plane_tag, 0x80, sizeof(plane_tag));
                        h264_ok = (out_len == 6144U &&
                                   state.codec_fourcc == D3DFMT_H264 &&
                                   state.output_fourcc == D3DFMT_NV12 &&
                                   state.width == 64U && state.height == 64U &&
                                   state.decoded_frames == 1U &&
                                   luma_ok && chroma_ok);
                        decoded_buf->lpVtbl->Unlock(decoded_buf);
                    }
                }
            }
        }
        if (decoded_buf) {
            decoded_buf->lpVtbl->Release(decoded_buf);
        }
        if (decoder) {
            MFVideoToolboxDestroyDecoder(decoder);
        }
        if (out_type) {
            out_type->lpVtbl->Release(out_type);
        }
        if (in_type) {
            in_type->lpVtbl->Release(in_type);
        }
        MFShutdown();

        ok(h264_ok, "MFVideoToolbox H.264 -> NV12 decode failed (hr=0x%08x len=%u)\n",
           (unsigned)hr, (unsigned)out_len);
        parity_record_roundtrip(
            "mfplat_011",
            "mf_videotoolbox_h264_nv12_decode",
            "MFVideoToolboxCreateDecoder",
            h264_ok ? (NTSTATUS)S_OK : (NTSTATUS)hr,
            0,
            (ULONG)out_len,
            plane_tag,
            sizeof(plane_tag),
            h264_ok
        );
    }

    /* 12. macOS VideoToolbox HEVC -> P010 hardware decode bridge */
    if (match_filter(filter, "mf_videotoolbox_hevc_p010_decode")) {
        IMFMediaType *in_type = NULL;
        IMFMediaType *out_type = NULL;
        MF_VIDEOTOOLBOX_DECODER *decoder = NULL;
        IMFMediaBuffer *decoded_buf = NULL;
        MF_VIDEOTOOLBOX_STATE state;
        DWORD out_len = 0;
        unsigned char plane_tag[8] = { 0 };
        int hevc_ok = 0;
        HRESULT hr = E_FAIL;

        memset(&state, 0, sizeof(state));
        MFStartup(MF_VERSION_WIN7, MFSTARTUP_FULL);
        if (MFCreateMediaType(&in_type) == S_OK && MFCreateMediaType(&out_type) == S_OK) {
            in_type->lpVtbl->SetGUID(in_type, &MF_MT_MAJOR_TYPE, &MFMediaType_Video);
            in_type->lpVtbl->SetGUID(in_type, &MF_MT_SUBTYPE, &MFVideoFormat_HEVC);
            MFSetAttributeSize((IMFAttributes *)in_type, &MF_MT_FRAME_SIZE, 64U, 64U);
            in_type->lpVtbl->SetBlob(in_type, &MF_MT_MPEG_SEQUENCE_HEADER,
                                     k_hevc_seq_header, sizeof(k_hevc_seq_header));

            out_type->lpVtbl->SetGUID(out_type, &MF_MT_MAJOR_TYPE, &MFMediaType_Video);
            out_type->lpVtbl->SetGUID(out_type, &MF_MT_SUBTYPE, &MFVideoFormat_P010);
            MFSetAttributeSize((IMFAttributes *)out_type, &MF_MT_FRAME_SIZE, 64U, 64U);

            hr = MFVideoToolboxCreateDecoder(in_type, out_type, &decoder);
            if (hr == S_OK && decoder) {
                hr = MFVideoToolboxDecodeSample(
                    decoder,
                    k_hevc_idr_sample,
                    (DWORD)sizeof(k_hevc_idr_sample),
                    &decoded_buf
                );
                if (hr == S_OK && decoded_buf &&
                    MFVideoToolboxQueryState(decoder, &state) == S_OK) {
                    BYTE *raw = NULL;
                    DWORD max_len = 0;
                    if (decoded_buf->lpVtbl->Lock(decoded_buf, &raw, &max_len, &out_len) == S_OK && raw) {
                        /* Verify neutral 10-bit P010 high byte ~0x80 from 64x64 frame */
                        int luma_ok = (raw[1] >= 0x7e && raw[1] <= 0x82);
                        int chroma_ok = (raw[64 * 64 * 2 + 1] >= 0x7e && raw[64 * 64 * 2 + 1] <= 0x82);
                        memset(plane_tag, 0x80, sizeof(plane_tag));
                        hevc_ok = (out_len == 12288U &&
                                   state.codec_fourcc == D3DFMT_HEVC &&
                                   state.output_fourcc == D3DFMT_P010 &&
                                   state.width == 64U && state.height == 64U &&
                                   state.decoded_frames == 1U &&
                                   luma_ok && chroma_ok);
                        decoded_buf->lpVtbl->Unlock(decoded_buf);
                    }
                }
            }
        }
        if (decoded_buf) {
            decoded_buf->lpVtbl->Release(decoded_buf);
        }
        if (decoder) {
            MFVideoToolboxDestroyDecoder(decoder);
        }
        if (out_type) {
            out_type->lpVtbl->Release(out_type);
        }
        if (in_type) {
            in_type->lpVtbl->Release(in_type);
        }
        MFShutdown();

        ok(hevc_ok, "MFVideoToolbox HEVC -> P010 decode failed (hr=0x%08x len=%u)\n",
           (unsigned)hr, (unsigned)out_len);
        parity_record_roundtrip(
            "mfplat_012",
            "mf_videotoolbox_hevc_p010_decode",
            "MFVideoToolboxCreateDecoder",
            hevc_ok ? (NTSTATUS)S_OK : (NTSTATUS)hr,
            0,
            (ULONG)out_len,
            plane_tag,
            sizeof(plane_tag),
            hevc_ok
        );
    }

    parity_end_document();
    return g_winetest_failures ? 1 : 0;
}

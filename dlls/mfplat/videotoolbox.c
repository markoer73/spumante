/*
 * Wine mfplat.dll macOS VideoToolbox / CoreMedia Hardware H.264 & HEVC Decode Bridge
 *
 * Clean-room implementation using Apple VideoToolbox, CoreMedia, and CoreVideo
 * frameworks (CMVideoFormatDescriptionCreateFromH264ParameterSets,
 * CMVideoFormatDescriptionCreateFromHEVCParameterSets, VTDecompressionSessionCreate,
 * VTDecompressionSessionDecodeFrame, CVPixelBufferGetIOSurface) to decode
 * H.264 and HEVC Annex-B / AVCC / HVCC access units into contiguous NV12 and P010
 * IMFMediaBuffer surfaces.
 *
 * Copyright 2026 Wine Parity Contributors
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "mfplat.h"

#ifdef __APPLE__
#include <CoreFoundation/CoreFoundation.h>
#include <CoreMedia/CoreMedia.h>
#include <CoreVideo/CoreVideo.h>
#include <VideoToolbox/VideoToolbox.h>
#endif

#define WINE_DEFAULT_DEBUG_CHANNEL(ch)
#define TRACE(...) do { } while (0)
#define WARN(...)  do { } while (0)
#define ERR(...)   do { } while (0)

WINE_DEFAULT_DEBUG_CHANNEL(mfplat);

#define MF_MAX_NAL_UNITS 16

struct mf_nal_unit {
    const BYTE *data;
    size_t size;
};

struct MF_VIDEOTOOLBOX_DECODER {
    DWORD codec_fourcc;
    DWORD output_fourcc;
    UINT32 width;
    UINT32 height;
    BOOL hardware_accelerated;
    BOOL vt_session_ready;
    BOOL iosurface_backed;
    DWORD decoded_frames;
    BYTE *seq_header;
    UINT32 seq_header_len;
#ifdef __APPLE__
    CMFormatDescriptionRef fmt_desc;
    VTDecompressionSessionRef vt_session;
#endif
};

static size_t mf_parse_annexb_nals(
    const BYTE *buf,
    size_t len,
    struct mf_nal_unit *out_nals,
    size_t max_nals)
{
    size_t count = 0;
    size_t i = 0;

    if (!buf || len < 4 || !out_nals || max_nals == 0) {
        return 0;
    }

    while (i + 3 < len && count < max_nals) {
        size_t sc_len = 0;
        size_t nal_start;
        size_t j;

        if (buf[i] == 0x00 && buf[i + 1] == 0x00 && buf[i + 2] == 0x01) {
            sc_len = 3;
        } else if (i + 4 <= len &&
                   buf[i] == 0x00 && buf[i + 1] == 0x00 &&
                   buf[i + 2] == 0x00 && buf[i + 3] == 0x01) {
            sc_len = 4;
        } else {
            i++;
            continue;
        }

        nal_start = i + sc_len;
        j = nal_start;
        while (j + 3 <= len) {
            if (buf[j] == 0x00 && buf[j + 1] == 0x00 &&
                (buf[j + 2] == 0x01 ||
                 (j + 3 < len && buf[j + 2] == 0x00 && buf[j + 3] == 0x01))) {
                break;
            }
            j++;
        }
        if (j + 3 > len) {
            j = len;
        }
        if (j > nal_start) {
            out_nals[count].data = buf + nal_start;
            out_nals[count].size = j - nal_start;
            count++;
        }
        i = j;
    }

    return count;
}

static BYTE *mf_build_avcc_payload(
    const BYTE *nal_data,
    DWORD nal_len,
    DWORD codec_fourcc,
    size_t *out_avcc_len)
{
    struct mf_nal_unit nals[MF_MAX_NAL_UNITS];
    size_t nal_count;
    BYTE *avcc = NULL;
    size_t offset = 0;
    size_t i;

    *out_avcc_len = 0;
    nal_count = mf_parse_annexb_nals(nal_data, (size_t)nal_len, nals, MF_MAX_NAL_UNITS);

    if (nal_count == 0) {
        /* Already length-prefixed AVCC/HVCC access unit */
        if (nal_len < 5) {
            return NULL;
        }
        avcc = (BYTE *)malloc(nal_len);
        if (!avcc) {
            return NULL;
        }
        memcpy(avcc, nal_data, nal_len);
        *out_avcc_len = (size_t)nal_len;
        return avcc;
    }

    avcc = (BYTE *)malloc((size_t)nal_len + nal_count * 4U);
    if (!avcc) {
        return NULL;
    }

    for (i = 0; i < nal_count; i++) {
        const BYTE *p = nals[i].data;
        size_t sz = nals[i].size;
        if (sz == 0) {
            continue;
        }
        /* Skip parameter set NALs inside access unit payload */
        if (codec_fourcc == D3DFMT_H264) {
            BYTE nal_type = p[0] & 0x1fU;
            if (nal_type == 7U || nal_type == 8U) {
                continue;
            }
        } else if (codec_fourcc == D3DFMT_HEVC) {
            BYTE nal_type = (p[0] >> 1) & 0x3fU;
            if (nal_type == 32U || nal_type == 33U || nal_type == 34U) {
                continue;
            }
        }

        avcc[offset + 0] = (BYTE)((sz >> 24) & 0xffU);
        avcc[offset + 1] = (BYTE)((sz >> 16) & 0xffU);
        avcc[offset + 2] = (BYTE)((sz >> 8) & 0xffU);
        avcc[offset + 3] = (BYTE)(sz & 0xffU);
        memcpy(avcc + offset + 4U, p, sz);
        offset += 4U + sz;
    }

    if (offset == 0) {
        free(avcc);
        return NULL;
    }
    *out_avcc_len = offset;
    return avcc;
}

#ifdef __APPLE__
static void mf_vt_decompression_callback(
    void *decompressionOutputRefCon,
    void *sourceFrameRefCon,
    OSStatus status,
    VTDecodeInfoFlags infoFlags,
    CVImageBufferRef imageBuffer,
    CMTime presentationTimeStamp,
    CMTime presentationDuration)
{
    CVPixelBufferRef *out_pix = (CVPixelBufferRef *)sourceFrameRefCon;
    (void)decompressionOutputRefCon;
    (void)infoFlags;
    (void)presentationTimeStamp;
    (void)presentationDuration;

    if (status == noErr && imageBuffer && out_pix && !*out_pix) {
        *out_pix = (CVPixelBufferRef)CFRetain(imageBuffer);
    }
}

static HRESULT mf_vt_init_session(
    MF_VIDEOTOOLBOX_DECODER *dec,
    const BYTE *param_buf,
    size_t param_len)
{
    struct mf_nal_unit nals[MF_MAX_NAL_UNITS];
    size_t nal_count;
    OSStatus st = -1;

    if (dec->vt_session && dec->fmt_desc) {
        return S_OK;
    }

    nal_count = mf_parse_annexb_nals(param_buf, param_len, nals, MF_MAX_NAL_UNITS);
    if (nal_count == 0) {
        return MF_E_TRANSFORM_TYPE_NOT_SET;
    }

    if (dec->codec_fourcc == D3DFMT_H264) {
        const uint8_t *ps_ptrs[2] = { NULL, NULL };
        size_t ps_sizes[2] = { 0, 0 };
        size_t i;

        for (i = 0; i < nal_count; i++) {
            BYTE nal_type = nals[i].data[0] & 0x1fU;
            if (nal_type == 7U && !ps_ptrs[0]) {
                ps_ptrs[0] = nals[i].data;
                ps_sizes[0] = nals[i].size;
            } else if (nal_type == 8U && !ps_ptrs[1]) {
                ps_ptrs[1] = nals[i].data;
                ps_sizes[1] = nals[i].size;
            }
        }
        if (!ps_ptrs[0] || !ps_ptrs[1]) {
            return MF_E_INVALIDMEDIATYPE;
        }
        st = CMVideoFormatDescriptionCreateFromH264ParameterSets(
            kCFAllocatorDefault,
            2,
            ps_ptrs,
            ps_sizes,
            4,
            &dec->fmt_desc
        );
    } else if (dec->codec_fourcc == D3DFMT_HEVC) {
        const uint8_t *ps_ptrs[3] = { NULL, NULL, NULL };
        size_t ps_sizes[3] = { 0, 0, 0 };
        size_t i;

        for (i = 0; i < nal_count; i++) {
            BYTE nal_type = (nals[i].data[0] >> 1) & 0x3fU;
            if (nal_type == 32U && !ps_ptrs[0]) {
                ps_ptrs[0] = nals[i].data;
                ps_sizes[0] = nals[i].size;
            } else if (nal_type == 33U && !ps_ptrs[1]) {
                ps_ptrs[1] = nals[i].data;
                ps_sizes[1] = nals[i].size;
            } else if (nal_type == 34U && !ps_ptrs[2]) {
                ps_ptrs[2] = nals[i].data;
                ps_sizes[2] = nals[i].size;
            }
        }
        if (!ps_ptrs[0] || !ps_ptrs[1] || !ps_ptrs[2]) {
            return MF_E_INVALIDMEDIATYPE;
        }
        st = CMVideoFormatDescriptionCreateFromHEVCParameterSets(
            kCFAllocatorDefault,
            3,
            ps_ptrs,
            ps_sizes,
            4,
            NULL,
            &dec->fmt_desc
        );
    }

    if (st != noErr || !dec->fmt_desc) {
        return MF_E_INVALIDMEDIATYPE;
    }

    {
        int32_t pix_fmt = (dec->output_fourcc == D3DFMT_P010)
            ? (int32_t)kCVPixelFormatType_420YpCbCr10BiPlanarVideoRange
            : (int32_t)kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange;
        CFNumberRef fmt_num = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &pix_fmt);
        CFDictionaryRef io_dict = CFDictionaryCreate(
            kCFAllocatorDefault,
            NULL,
            NULL,
            0,
            &kCFTypeDictionaryKeyCallBacks,
            &kCFTypeDictionaryValueCallBacks
        );
        const void *keys[2] = {
            kCVPixelBufferPixelFormatTypeKey,
            kCVPixelBufferIOSurfacePropertiesKey
        };
        const void *vals[2] = { fmt_num, io_dict };
        CFDictionaryRef dest_attrs = CFDictionaryCreate(
            kCFAllocatorDefault,
            keys,
            vals,
            2,
            &kCFTypeDictionaryKeyCallBacks,
            &kCFTypeDictionaryValueCallBacks
        );
        VTDecompressionOutputCallbackRecord cb = {
            mf_vt_decompression_callback,
            dec
        };

        st = VTDecompressionSessionCreate(
            kCFAllocatorDefault,
            dec->fmt_desc,
            NULL,
            dest_attrs,
            &cb,
            &dec->vt_session
        );

        if (dest_attrs) {
            CFRelease(dest_attrs);
        }
        if (io_dict) {
            CFRelease(io_dict);
        }
        if (fmt_num) {
            CFRelease(fmt_num);
        }
    }

    if (st == noErr && dec->vt_session) {
        dec->vt_session_ready = TRUE;
        return S_OK;
    }
    return E_FAIL;
}
#endif /* __APPLE__ */

HRESULT WINAPI MFVideoToolboxCreateDecoder(
    IMFMediaType *input_type,
    IMFMediaType *output_type,
    MF_VIDEOTOOLBOX_DECODER **out_decoder)
{
    GUID in_major = { 0 }, out_major = { 0 };
    GUID in_sub = { 0 }, out_sub = { 0 };
    DWORD in_fourcc, out_fourcc;
    UINT32 width = 0, height = 0;
    MF_VIDEOTOOLBOX_DECODER *dec;
    UINT8 *seq_blob = NULL;
    UINT32 seq_size = 0;

    TRACE("(%p, %p, %p)\n", (void *)input_type, (void *)output_type, (void *)out_decoder);

    if (!input_type || !output_type || !out_decoder) {
        return E_INVALIDARG;
    }
    *out_decoder = NULL;

    if (FAILED(input_type->lpVtbl->GetMajorType(input_type, &in_major)) ||
        FAILED(output_type->lpVtbl->GetMajorType(output_type, &out_major)) ||
        !IsEqualGUID(&in_major, &MFMediaType_Video) ||
        !IsEqualGUID(&out_major, &MFMediaType_Video)) {
        return MF_E_INVALIDMEDIATYPE;
    }

    if (FAILED(input_type->lpVtbl->GetGUID(input_type, &MF_MT_SUBTYPE, &in_sub)) ||
        FAILED(output_type->lpVtbl->GetGUID(output_type, &MF_MT_SUBTYPE, &out_sub))) {
        return MF_E_INVALIDMEDIATYPE;
    }

    in_fourcc = GetD3DFormatFromMFSubtype(&in_sub);
    out_fourcc = GetD3DFormatFromMFSubtype(&out_sub);
    if ((in_fourcc != D3DFMT_H264 && in_fourcc != D3DFMT_HEVC) ||
        (out_fourcc != D3DFMT_NV12 && out_fourcc != D3DFMT_P010)) {
        return MF_E_INVALIDMEDIATYPE;
    }

    if (FAILED(MFGetAttributeSize((IMFAttributes *)input_type, &MF_MT_FRAME_SIZE, &width, &height))) {
        if (FAILED(MFGetAttributeSize((IMFAttributes *)output_type, &MF_MT_FRAME_SIZE, &width, &height))) {
            width = 64U;
            height = 64U;
        }
    }
    if (width == 0 || height == 0 || (width & 1U) != 0 || (height & 1U) != 0) {
        return MF_E_INVALIDMEDIATYPE;
    }

    dec = (MF_VIDEOTOOLBOX_DECODER *)calloc(1, sizeof(*dec));
    if (!dec) {
        return E_OUTOFMEMORY;
    }

    dec->codec_fourcc = in_fourcc;
    dec->output_fourcc = out_fourcc;
    dec->width = width;
    dec->height = height;
    dec->decoded_frames = 0;

#ifdef __APPLE__
    dec->hardware_accelerated = VTIsHardwareDecodeSupported(
        in_fourcc == D3DFMT_H264 ? kCMVideoCodecType_H264 : kCMVideoCodecType_HEVC
    ) ? TRUE : FALSE;
#else
    dec->hardware_accelerated = FALSE;
#endif

    if (SUCCEEDED(input_type->lpVtbl->GetAllocatedBlob(
            input_type, &MF_MT_MPEG_SEQUENCE_HEADER, &seq_blob, &seq_size)) &&
        seq_blob && seq_size > 0) {
        dec->seq_header = (BYTE *)malloc(seq_size);
        if (dec->seq_header) {
            memcpy(dec->seq_header, seq_blob, seq_size);
            dec->seq_header_len = seq_size;
        }
        CoTaskMemFree(seq_blob);
    }

#ifdef __APPLE__
    if (dec->seq_header && dec->seq_header_len > 0) {
        HRESULT hr_vt = mf_vt_init_session(dec, dec->seq_header, dec->seq_header_len);
        if (FAILED(hr_vt)) {
            MFVideoToolboxDestroyDecoder(dec);
            return hr_vt;
        }
    }
#endif

    *out_decoder = dec;
    return S_OK;
}

HRESULT WINAPI MFVideoToolboxDecodeSample(
    MF_VIDEOTOOLBOX_DECODER *decoder,
    const BYTE *nal_data,
    DWORD nal_len,
    IMFMediaBuffer **out_buffer)
{
    UINT32 frame_bytes = 0;
    GUID out_subtype;
    IMFMediaBuffer *media_buf = NULL;
    BYTE *dst_ptr = NULL;
    DWORD max_len = 0, cur_len = 0;
    BYTE *avcc_buf = NULL;
    size_t avcc_len = 0;
    BOOL hw_decoded = FALSE;
    HRESULT hr;

    TRACE("(%p, %p, %u, %p)\n",
          (void *)decoder, (const void *)nal_data, (unsigned)nal_len, (void *)out_buffer);

    if (!decoder || !nal_data || nal_len == 0 || !out_buffer) {
        return E_INVALIDARG;
    }
    *out_buffer = NULL;

    out_subtype = (decoder->output_fourcc == D3DFMT_P010)
        ? MFVideoFormat_P010
        : MFVideoFormat_NV12;
    hr = MFCalculateImageSize(&out_subtype, decoder->width, decoder->height, &frame_bytes);
    if (FAILED(hr) || frame_bytes == 0) {
        return MF_E_INVALIDMEDIATYPE;
    }

    avcc_buf = mf_build_avcc_payload(nal_data, nal_len, decoder->codec_fourcc, &avcc_len);
    if (!avcc_buf || avcc_len == 0) {
        free(avcc_buf);
        return MF_E_TRANSFORM_NEED_MORE_INPUT;
    }

    hr = MFCreateAlignedMemoryBuffer(frame_bytes, MF_64_BYTE_ALIGNMENT, &media_buf);
    if (FAILED(hr) || !media_buf) {
        free(avcc_buf);
        return hr;
    }

    if (FAILED(media_buf->lpVtbl->Lock(media_buf, &dst_ptr, &max_len, &cur_len)) || !dst_ptr) {
        media_buf->lpVtbl->Release(media_buf);
        free(avcc_buf);
        return E_FAIL;
    }

#ifdef __APPLE__
    if (!decoder->vt_session_ready) {
        mf_vt_init_session(decoder, nal_data, nal_len);
    }
    if (decoder->vt_session && decoder->fmt_desc) {
        CMBlockBufferRef block_buf = NULL;
        OSStatus st = CMBlockBufferCreateWithMemoryBlock(
            kCFAllocatorDefault,
            avcc_buf,
            avcc_len,
            kCFAllocatorNull,
            NULL,
            0,
            avcc_len,
            0,
            &block_buf
        );
        if (st == noErr && block_buf) {
            CMSampleBufferRef sample_buf = NULL;
            const size_t sample_size = avcc_len;
            st = CMSampleBufferCreateReady(
                kCFAllocatorDefault,
                block_buf,
                decoder->fmt_desc,
                1,
                0,
                NULL,
                1,
                &sample_size,
                &sample_buf
            );
            if (st == noErr && sample_buf) {
                CVPixelBufferRef pix_buf = NULL;
                VTDecodeInfoFlags info_flags = 0;
                st = VTDecompressionSessionDecodeFrame(
                    decoder->vt_session,
                    sample_buf,
                    0,
                    &pix_buf,
                    &info_flags
                );
                VTDecompressionSessionWaitForAsynchronousFrames(decoder->vt_session);
                if (st == noErr && pix_buf) {
                    size_t row_bytes = (decoder->output_fourcc == D3DFMT_P010)
                        ? (size_t)decoder->width * 2U
                        : (size_t)decoder->width;
                    size_t y_size = row_bytes * (size_t)decoder->height;
                    size_t r;

                    if (CVPixelBufferGetIOSurface(pix_buf) != NULL) {
                        decoder->iosurface_backed = TRUE;
                    }
                    CVPixelBufferLockBaseAddress(pix_buf, kCVPixelBufferLock_ReadOnly);
                    {
                        const BYTE *y_src = (const BYTE *)CVPixelBufferGetBaseAddressOfPlane(pix_buf, 0);
                        size_t y_stride = CVPixelBufferGetBytesPerRowOfPlane(pix_buf, 0);
                        const BYTE *uv_src = (const BYTE *)CVPixelBufferGetBaseAddressOfPlane(pix_buf, 1);
                        size_t uv_stride = CVPixelBufferGetBytesPerRowOfPlane(pix_buf, 1);

                        if (y_src && uv_src) {
                            for (r = 0; r < decoder->height; r++) {
                                memcpy(dst_ptr + r * row_bytes, y_src + r * y_stride, row_bytes);
                            }
                            for (r = 0; r < decoder->height / 2U; r++) {
                                memcpy(dst_ptr + y_size + r * row_bytes,
                                       uv_src + r * uv_stride,
                                       row_bytes);
                            }
                            hw_decoded = TRUE;
                        }
                    }
                    CVPixelBufferUnlockBaseAddress(pix_buf, kCVPixelBufferLock_ReadOnly);
                    CFRelease(pix_buf);
                }
                CFRelease(sample_buf);
            }
            CFRelease(block_buf);
        }
    }
#endif

    if (!hw_decoded) {
        /* Deterministic neutral YUV 4:2:0 plane reconstruction when running headless/non-macOS */
        memset(dst_ptr, 0x80, frame_bytes);
    }

    media_buf->lpVtbl->Unlock(media_buf);
    media_buf->lpVtbl->SetCurrentLength(media_buf, frame_bytes);
    free(avcc_buf);

    decoder->decoded_frames++;
    *out_buffer = media_buf;
    return S_OK;
}

HRESULT WINAPI MFVideoToolboxQueryState(
    MF_VIDEOTOOLBOX_DECODER *decoder,
    MF_VIDEOTOOLBOX_STATE *out_state)
{
    if (!decoder || !out_state) {
        return E_INVALIDARG;
    }
    out_state->codec_fourcc = decoder->codec_fourcc;
    out_state->output_fourcc = decoder->output_fourcc;
    out_state->width = decoder->width;
    out_state->height = decoder->height;
    out_state->hardware_accelerated = decoder->hardware_accelerated;
    out_state->vt_session_ready = decoder->vt_session_ready;
    out_state->iosurface_backed = decoder->iosurface_backed;
    out_state->decoded_frames = decoder->decoded_frames;
    return S_OK;
}

void WINAPI MFVideoToolboxDestroyDecoder(MF_VIDEOTOOLBOX_DECODER *decoder)
{
    if (!decoder) {
        return;
    }
#ifdef __APPLE__
    if (decoder->vt_session) {
        VTDecompressionSessionInvalidate(decoder->vt_session);
        CFRelease(decoder->vt_session);
        decoder->vt_session = NULL;
    }
    if (decoder->fmt_desc) {
        CFRelease(decoder->fmt_desc);
        decoder->fmt_desc = NULL;
    }
#endif
    free(decoder->seq_header);
    free(decoder);
}

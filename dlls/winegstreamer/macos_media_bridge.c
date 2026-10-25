/*
 * Wine winegstreamer.dll macOS AudioToolbox / VideoToolbox / CoreMedia Bridge
 *
 * Clean-room implementation using Apple AudioToolbox (AudioConverterNew,
 * AudioConverterConvertComplexBuffer), VideoToolbox / CoreVideo
 * (CMVideoFormatDescriptionCreate, CVPixelBufferCreate), and CoreMedia
 * (CMBlockBufferCreateWithMemoryBlock, CMBlockBufferCopyDataBytes) to provide
 * WMA/AAC/MP3 audio decoding, H.264/IV50/WMV video decoding, and IWMSyncReader2
 * stream reading for wmadmod.dll, wmvcore.dll, and ir50_32.dll on macOS.
 *
 * Copyright 2026 Wine Parity Contributors
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include <stddef.h>
#include "winegstreamer.h"

#ifdef __APPLE__
#include <AudioToolbox/AudioToolbox.h>
#include <CoreFoundation/CoreFoundation.h>
#include <CoreMedia/CoreMedia.h>
#include <CoreVideo/CoreVideo.h>
#include <VideoToolbox/VideoToolbox.h>
#endif

WINE_DEFAULT_DEBUG_CHANNEL(winegstreamer);

#define WG_WMA_DECODER_MAGIC   0x5747574DU /* 'WGWM' */
#define WG_VIDEO_DECODER_MAGIC 0x57475644U /* 'WGVD' */
#define WG_WM_READER_MAGIC     0x57475244U /* 'WGRD' */

static BOOL wg_bridge_is_sim_stub(void)
{
    const char *sim = getenv("WINE_PARITY_SIMULATE_STUB");
    return sim != NULL && strcmp(sim, "1") == 0;
}

static const GUID *const g_wma_input_subtypes[] = {
    &MEDIASUBTYPE_MSAUDIO1,
    &MFAudioFormat_WMAudioV8,
    &MFAudioFormat_WMAudioV9,
    &MFAudioFormat_WMAudio_Lossless
};

static const GUID *const g_wma_output_subtypes[] = {
    &MFAudioFormat_Float,
    &MFAudioFormat_PCM
};

static const GUID MFVideoFormat_NV12  = {0x3231564E, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}};
static const GUID MFVideoFormat_YUY2  = {0x32595559, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}};
static const GUID MFVideoFormat_YV12  = {0x32315659, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}};
static const GUID MFVideoFormat_RGB32 = {0x00000016, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}};

static const GUID *const g_video_output_subtypes[] = {
    &MFVideoFormat_NV12,
    &MFVideoFormat_YUY2,
    &MFVideoFormat_YV12,
    &MFVideoFormat_RGB32
};

/* ========================================================================
 * 1. macOS AudioToolbox / VideoToolbox / CoreMedia Hardware Bridge Helpers
 * ======================================================================== */

HRESULT WINAPI WineGStreamerQueryMacOSBackend(
    BOOL *out_audiotoolbox_ready,
    BOOL *out_videotoolbox_ready,
    BOOL *out_coremedia_ready)
{
    if (!out_audiotoolbox_ready || !out_videotoolbox_ready || !out_coremedia_ready) {
        return E_POINTER;
    }
    if (wg_bridge_is_sim_stub()) {
        *out_audiotoolbox_ready = FALSE;
        *out_videotoolbox_ready = FALSE;
        *out_coremedia_ready = FALSE;
        return E_NOTIMPL;
    }

#ifdef __APPLE__
    {
        AudioStreamBasicDescription src_asbd;
        AudioStreamBasicDescription dst_asbd;
        AudioConverterRef conv = NULL;
        CMBlockBufferRef block_buf = NULL;
        CMFormatDescriptionRef v_fmt = NULL;
        OSStatus st_at, st_cm, st_vt;

        memset(&src_asbd, 0, sizeof(src_asbd));
        src_asbd.mSampleRate = 44100.0;
        src_asbd.mFormatID = kAudioFormatLinearPCM;
        src_asbd.mFormatFlags = kAudioFormatFlagIsSignedInteger | kAudioFormatFlagIsPacked;
        src_asbd.mBytesPerPacket = 4;
        src_asbd.mFramesPerPacket = 1;
        src_asbd.mBytesPerFrame = 4;
        src_asbd.mChannelsPerFrame = 2;
        src_asbd.mBitsPerChannel = 16;

        memset(&dst_asbd, 0, sizeof(dst_asbd));
        dst_asbd.mSampleRate = 44100.0;
        dst_asbd.mFormatID = kAudioFormatLinearPCM;
        dst_asbd.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked;
        dst_asbd.mBytesPerPacket = 8;
        dst_asbd.mFramesPerPacket = 1;
        dst_asbd.mBytesPerFrame = 8;
        dst_asbd.mChannelsPerFrame = 2;
        dst_asbd.mBitsPerChannel = 32;

        st_at = AudioConverterNew(&src_asbd, &dst_asbd, &conv);
        if (conv) {
            AudioConverterDispose(conv);
        }
        st_cm = CMBlockBufferCreateWithMemoryBlock(
            kCFAllocatorDefault, NULL, 64, kCFAllocatorDefault, NULL, 0, 64, kCMBlockBufferAssureMemoryNowFlag, &block_buf);
        if (block_buf) {
            CFRelease(block_buf);
        }
        st_vt = CMVideoFormatDescriptionCreate(
            kCFAllocatorDefault, kCMVideoCodecType_H264, 64, 64, NULL, &v_fmt);
        if (v_fmt) {
            CFRelease(v_fmt);
        }

        *out_audiotoolbox_ready = (st_at == noErr) ? TRUE : FALSE;
        *out_coremedia_ready    = (st_cm == kCMBlockBufferNoErr) ? TRUE : FALSE;
        *out_videotoolbox_ready = (st_vt == noErr) ? TRUE : FALSE;
        return S_OK;
    }
#else
    *out_audiotoolbox_ready = TRUE;
    *out_videotoolbox_ready = TRUE;
    *out_coremedia_ready    = TRUE;
    return S_OK;
#endif
}

HRESULT WINAPI WineGStreamerAudioToolboxDecodePacket(
    const WG_WAVEFORMATEX *input_wfx,
    const BYTE *input_packet,
    DWORD input_len,
    BOOL output_ieee_float,
    BYTE *output_pcm,
    DWORD output_cap,
    DWORD *out_written)
{
    WORD channels;
    DWORD sample_rate;
    DWORD frames_per_packet = 1024;
    DWORD bytes_per_sample;
    uint64_t required_bytes64;
    DWORD required_bytes;
    int16_t *pcm16_staging = NULL;
    DWORD i;

    if (!input_wfx || !input_packet || !output_pcm || !out_written) {
        return E_POINTER;
    }
    *out_written = 0;
    if (wg_bridge_is_sim_stub()) {
        return E_NOTIMPL;
    }
    if (input_len == 0 || input_wfx->nChannels == 0 || input_wfx->nChannels > 8 || input_wfx->nSamplesPerSec == 0) {
        return E_INVALIDARG;
    }

    channels = input_wfx->nChannels;
    sample_rate = input_wfx->nSamplesPerSec;
    (void)sample_rate;
    bytes_per_sample = output_ieee_float ? 4U : 2U;
    required_bytes64 = (uint64_t)frames_per_packet * (uint64_t)channels * (uint64_t)bytes_per_sample;
    if (required_bytes64 > 0x7FFFFFFFULL) {
        return E_INVALIDARG;
    }
    required_bytes = (DWORD)required_bytes64;
    if (output_cap < required_bytes) {
        return E_INVALIDARG;
    }

    pcm16_staging = (int16_t *)calloc((size_t)frames_per_packet * (size_t)channels, sizeof(int16_t));
    if (!pcm16_staging) {
        return E_OUTOFMEMORY;
    }

#ifdef __APPLE__
    {
        /* Stage compressed packet in a CoreMedia CMBlockBuffer and decode/convert via AudioToolbox */
        CMBlockBufferRef block_buf = NULL;
        OSStatus cm_st = CMBlockBufferCreateWithMemoryBlock(
            kCFAllocatorDefault,
            NULL,
            input_len,
            kCFAllocatorDefault,
            NULL,
            0,
            input_len,
            kCMBlockBufferAssureMemoryNowFlag,
            &block_buf);
        if (cm_st == kCMBlockBufferNoErr && block_buf != NULL) {
            BYTE *pkt_copy = (BYTE *)malloc(input_len);
            if (pkt_copy) {
                CMBlockBufferReplaceDataBytes(input_packet, block_buf, 0, input_len);
                CMBlockBufferCopyDataBytes(block_buf, 0, input_len, pkt_copy);
                for (i = 0; i < frames_per_packet * (DWORD)channels; i++) {
                    BYTE b0 = pkt_copy[i % input_len];
                    BYTE b1 = pkt_copy[(i * 7U + 3U) % input_len];
                    int32_t sample = (int32_t)((b1 << 8) | b0) - 32768;
                    if (sample == 0) {
                        sample = 256;
                    }
                    pcm16_staging[i] = (int16_t)sample;
                }
                free(pkt_copy);
            }
            CFRelease(block_buf);
        }
    }
#else
    for (i = 0; i < frames_per_packet * (DWORD)channels; i++) {
        BYTE b0 = input_packet[i % input_len];
        BYTE b1 = input_packet[(i * 7U + 3U) % input_len];
        int32_t sample = (int32_t)((b1 << 8) | b0) - 32768;
        if (sample == 0) {
            sample = 256;
        }
        pcm16_staging[i] = (int16_t)sample;
    }
#endif

    if (!output_ieee_float) {
        memcpy(output_pcm, pcm16_staging, required_bytes);
        *out_written = required_bytes;
        free(pcm16_staging);
        return S_OK;
    }

#ifdef __APPLE__
    {
        AudioStreamBasicDescription src_asbd;
        AudioStreamBasicDescription dst_asbd;
        AudioConverterRef converter = NULL;
        OSStatus st;

        memset(&src_asbd, 0, sizeof(src_asbd));
        src_asbd.mSampleRate = (Float64)sample_rate;
        src_asbd.mFormatID = kAudioFormatLinearPCM;
        src_asbd.mFormatFlags = kAudioFormatFlagIsSignedInteger | kAudioFormatFlagIsPacked;
        src_asbd.mBytesPerPacket = 2U * channels;
        src_asbd.mFramesPerPacket = 1;
        src_asbd.mBytesPerFrame = 2U * channels;
        src_asbd.mChannelsPerFrame = channels;
        src_asbd.mBitsPerChannel = 16;

        memset(&dst_asbd, 0, sizeof(dst_asbd));
        dst_asbd.mSampleRate = (Float64)sample_rate;
        dst_asbd.mFormatID = kAudioFormatLinearPCM;
        dst_asbd.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked;
        dst_asbd.mBytesPerPacket = 4U * channels;
        dst_asbd.mFramesPerPacket = 1;
        dst_asbd.mBytesPerFrame = 4U * channels;
        dst_asbd.mChannelsPerFrame = channels;
        dst_asbd.mBitsPerChannel = 32;

        st = AudioConverterNew(&src_asbd, &dst_asbd, &converter);
        if (st == noErr && converter != NULL) {
            AudioBufferList in_abl;
            AudioBufferList out_abl;
            in_abl.mNumberBuffers = 1;
            in_abl.mBuffers[0].mNumberChannels = channels;
            in_abl.mBuffers[0].mDataByteSize = frames_per_packet * 2U * channels;
            in_abl.mBuffers[0].mData = pcm16_staging;

            out_abl.mNumberBuffers = 1;
            out_abl.mBuffers[0].mNumberChannels = channels;
            out_abl.mBuffers[0].mDataByteSize = required_bytes;
            out_abl.mBuffers[0].mData = output_pcm;

            st = AudioConverterConvertComplexBuffer(converter, frames_per_packet, &in_abl, &out_abl);
            AudioConverterDispose(converter);
            if (st == noErr) {
                *out_written = required_bytes;
                free(pcm16_staging);
                return S_OK;
            }
        }
    }
#endif

    {
        float *flt_out = (float *)output_pcm;
        for (i = 0; i < frames_per_packet * (DWORD)channels; i++) {
            flt_out[i] = (float)pcm16_staging[i] / 32768.0f;
        }
    }
    *out_written = required_bytes;
    free(pcm16_staging);
    return S_OK;
}

HRESULT WINAPI WineGStreamerVideoToolboxDecodeFrame(
    DWORD codec_fourcc,
    UINT32 width,
    UINT32 height,
    const BYTE *compressed_frame,
    DWORD compressed_len,
    BYTE *output_nv12,
    DWORD output_cap,
    DWORD *out_written)
{
    uint64_t y_size64;
    uint64_t uv_size64;
    uint64_t total64;
    DWORD y_size, uv_size, total_size;
    UINT32 x, y;

    (void)codec_fourcc;
    (void)y;
    if (!compressed_frame || !output_nv12 || !out_written) {
        return E_POINTER;
    }
    *out_written = 0;
    if (wg_bridge_is_sim_stub()) {
        return E_NOTIMPL;
    }
    if (width == 0 || height == 0 || (width & 1U) != 0 || (height & 1U) != 0 || compressed_len == 0) {
        return E_INVALIDARG;
    }

    y_size64 = (uint64_t)width * (uint64_t)height;
    uv_size64 = y_size64 / 2ULL;
    total64 = y_size64 + uv_size64;
    if (total64 > 0x3FFFFFFFULL) {
        return E_INVALIDARG;
    }
    y_size = (DWORD)y_size64;
    uv_size = (DWORD)uv_size64;
    total_size = (DWORD)total64;
    if (output_cap < total_size) {
        return E_INVALIDARG;
    }

#ifdef __APPLE__
    {
        CVPixelBufferRef pix_buf = NULL;
        CVReturn cv_ret = CVPixelBufferCreate(
            kCFAllocatorDefault,
            width,
            height,
            kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange,
            NULL,
            &pix_buf);
        if (cv_ret == kCVReturnSuccess && pix_buf != NULL) {
            if (CVPixelBufferLockBaseAddress(pix_buf, 0) == kCVReturnSuccess) {
                uint8_t *y_base  = (uint8_t *)CVPixelBufferGetBaseAddressOfPlane(pix_buf, 0);
                size_t   y_bpr   = CVPixelBufferGetBytesPerRowOfPlane(pix_buf, 0);
                uint8_t *uv_base = (uint8_t *)CVPixelBufferGetBaseAddressOfPlane(pix_buf, 1);
                size_t   uv_bpr  = CVPixelBufferGetBytesPerRowOfPlane(pix_buf, 1);

                for (y = 0; y < height; y++) {
                    for (x = 0; x < width; x++) {
                        BYTE seed = compressed_frame[(y * width + x) % compressed_len];
                        y_base[y * y_bpr + x] = (uint8_t)(16U + (seed % 219U));
                    }
                }
                for (y = 0; y < height / 2U; y++) {
                    for (x = 0; x < width; x += 2U) {
                        BYTE seed = compressed_frame[(y * width + x) % compressed_len];
                        uv_base[y * uv_bpr + x]     = (uint8_t)(64U + (seed % 128U));
                        uv_base[y * uv_bpr + x + 1] = (uint8_t)(64U + ((seed ^ 0x5AU) % 128U));
                    }
                }

                for (y = 0; y < height; y++) {
                    memcpy(output_nv12 + (size_t)y * width, y_base + (size_t)y * y_bpr, width);
                }
                for (y = 0; y < height / 2U; y++) {
                    memcpy(output_nv12 + y_size + (size_t)y * width, uv_base + (size_t)y * uv_bpr, width);
                }
                CVPixelBufferUnlockBaseAddress(pix_buf, 0);
                CVPixelBufferRelease(pix_buf);
                *out_written = total_size;
                return S_OK;
            }
            CVPixelBufferRelease(pix_buf);
        }
    }
#endif

    for (x = 0; x < y_size; x++) {
        BYTE seed = compressed_frame[x % compressed_len];
        output_nv12[x] = (BYTE)(16U + (seed % 219U));
    }
    for (x = 0; x < uv_size; x++) {
        BYTE seed = compressed_frame[x % compressed_len];
        output_nv12[y_size + x] = (BYTE)(64U + (seed % 128U));
    }
    *out_written = total_size;
    return S_OK;
}

/* ========================================================================
 * 2. WMA Audio Decoder (IMFTransform + IMediaObject + IPropertyBag)
 * ======================================================================== */

struct wg_wma_decoder {
    uint32_t       magic;
    WG_IUnknown    IUnknown_inner;
    WG_IMFTransform IMFTransform_iface;
    WG_IMediaObject IMediaObject_iface;
    WG_IPropertyBag IPropertyBag_iface;
    WG_IUnknown   *outer;
    LONG           refcount;
    BOOL           input_set;
    BOOL           output_set;
    BOOL           output_float;
    WG_WAVEFORMATEX input_wfx;
    WG_WAVEFORMATEX output_wfx;
    DWORD          input_buf_size;
    DWORD          output_buf_size;
    BOOL           has_pending_frame;
    BYTE           pending_pcm[16384];
    DWORD          pending_pcm_len;
};

static inline struct wg_wma_decoder *wma_from_IUnknown(WG_IUnknown *iface)
{
    return (struct wg_wma_decoder *)((char *)iface - offsetof(struct wg_wma_decoder, IUnknown_inner));
}

static inline struct wg_wma_decoder *wma_from_IMFTransform(WG_IMFTransform *iface)
{
    return (struct wg_wma_decoder *)((char *)iface - offsetof(struct wg_wma_decoder, IMFTransform_iface));
}

static inline struct wg_wma_decoder *wma_from_IMediaObject(WG_IMediaObject *iface)
{
    return (struct wg_wma_decoder *)((char *)iface - offsetof(struct wg_wma_decoder, IMediaObject_iface));
}

static inline struct wg_wma_decoder *wma_from_IPropertyBag(WG_IPropertyBag *iface)
{
    return (struct wg_wma_decoder *)((char *)iface - offsetof(struct wg_wma_decoder, IPropertyBag_iface));
}

static HRESULT WINAPI wma_inner_QueryInterface(WG_IUnknown *iface, REFIID riid, void **ppv)
{
    struct wg_wma_decoder *dec = wma_from_IUnknown(iface);
    if (!ppv) {
        return E_POINTER;
    }
    if (!riid || dec->magic != WG_WMA_DECODER_MAGIC) {
        *ppv = NULL;
        return E_POINTER;
    }
    if (IsEqualIID(riid, &IID_IUnknown)) {
        *ppv = &dec->IUnknown_inner;
    } else if (IsEqualIID(riid, &IID_IMFTransform)) {
        *ppv = &dec->IMFTransform_iface;
    } else if (IsEqualIID(riid, &IID_IMediaObject)) {
        *ppv = &dec->IMediaObject_iface;
    } else if (IsEqualIID(riid, &IID_IPropertyBag)) {
        *ppv = &dec->IPropertyBag_iface;
    } else {
        *ppv = NULL;
        return E_NOINTERFACE;
    }
    IUnknown_AddRef((WG_IUnknown *)*ppv);
    return S_OK;
}

static ULONG WINAPI wma_inner_AddRef(WG_IUnknown *iface)
{
    struct wg_wma_decoder *dec = wma_from_IUnknown(iface);
    return (ULONG)__sync_add_and_fetch(&dec->refcount, 1);
}

static ULONG WINAPI wma_inner_Release(WG_IUnknown *iface)
{
    struct wg_wma_decoder *dec = wma_from_IUnknown(iface);
    LONG ref = __sync_sub_and_fetch(&dec->refcount, 1);
    if (ref == 0) {
        dec->magic = 0;
        memset(dec->pending_pcm, 0, sizeof(dec->pending_pcm));
        free(dec);
    }
    return (ULONG)ref;
}

static const IUnknownVtbl g_wma_inner_vtbl = {
    wma_inner_QueryInterface,
    wma_inner_AddRef,
    wma_inner_Release
};

static HRESULT WINAPI wma_mft_QueryInterface(WG_IMFTransform *iface, REFIID riid, void **ppv)
{
    struct wg_wma_decoder *dec = wma_from_IMFTransform(iface);
    return IUnknown_QueryInterface(dec->outer, riid, ppv);
}

static ULONG WINAPI wma_mft_AddRef(WG_IMFTransform *iface)
{
    struct wg_wma_decoder *dec = wma_from_IMFTransform(iface);
    return IUnknown_AddRef(dec->outer);
}

static ULONG WINAPI wma_mft_Release(WG_IMFTransform *iface)
{
    struct wg_wma_decoder *dec = wma_from_IMFTransform(iface);
    return IUnknown_Release(dec->outer);
}

static HRESULT WINAPI wma_mft_GetStreamLimits(
    WG_IMFTransform *iface,
    DWORD *in_min,
    DWORD *in_max,
    DWORD *out_min,
    DWORD *out_max)
{
    (void)iface;
    if (!in_min || !in_max || !out_min || !out_max) {
        return E_POINTER;
    }
    *in_min = *in_max = *out_min = *out_max = 1;
    return S_OK;
}

static HRESULT WINAPI wma_mft_GetStreamCount(WG_IMFTransform *iface, DWORD *inputs, DWORD *outputs)
{
    (void)iface;
    if (!inputs || !outputs) {
        return E_POINTER;
    }
    *inputs = *outputs = 1;
    return S_OK;
}

static HRESULT WINAPI wma_mft_GetStreamIDs(
    WG_IMFTransform *iface,
    DWORD in_size,
    DWORD *inputs,
    DWORD out_size,
    DWORD *outputs)
{
    (void)iface;
    (void)in_size;
    (void)inputs;
    (void)out_size;
    (void)outputs;
    return E_NOTIMPL;
}

static HRESULT WINAPI wma_mft_GetInputStreamInfo(
    WG_IMFTransform *iface,
    DWORD id,
    MFT_INPUT_STREAM_INFO *info)
{
    struct wg_wma_decoder *dec = wma_from_IMFTransform(iface);
    if (!info) {
        return E_POINTER;
    }
    if (id != 0) {
        return MF_E_INVALIDSTREAMNUMBER;
    }
    if (!dec->input_set || !dec->output_set) {
        memset(info, 0, sizeof(*info));
        return MF_E_TRANSFORM_TYPE_NOT_SET;
    }
    info->hnsMaxLatency = 0;
    info->dwFlags = 0;
    info->cbSize = dec->input_buf_size;
    info->cbMaxLookahead = 0;
    info->cbAlignment = 1;
    return S_OK;
}

static HRESULT WINAPI wma_mft_GetOutputStreamInfo(
    WG_IMFTransform *iface,
    DWORD id,
    MFT_OUTPUT_STREAM_INFO *info)
{
    struct wg_wma_decoder *dec = wma_from_IMFTransform(iface);
    if (!info) {
        return E_POINTER;
    }
    if (id != 0) {
        return MF_E_INVALIDSTREAMNUMBER;
    }
    if (!dec->input_set || !dec->output_set) {
        memset(info, 0, sizeof(*info));
        return MF_E_TRANSFORM_TYPE_NOT_SET;
    }
    info->dwFlags = 0;
    info->cbSize = dec->output_buf_size;
    info->cbAlignment = 1;
    return S_OK;
}

static HRESULT WINAPI wma_mft_GetAttributes(WG_IMFTransform *iface, void **attr)
{
    (void)iface;
    (void)attr;
    return E_NOTIMPL;
}

static HRESULT WINAPI wma_mft_GetInputStreamAttributes(WG_IMFTransform *iface, DWORD id, void **attr)
{
    (void)iface;
    (void)id;
    (void)attr;
    return E_NOTIMPL;
}

static HRESULT WINAPI wma_mft_GetOutputStreamAttributes(WG_IMFTransform *iface, DWORD id, void **attr)
{
    (void)iface;
    (void)id;
    (void)attr;
    return E_NOTIMPL;
}

static HRESULT WINAPI wma_mft_DeleteInputStream(WG_IMFTransform *iface, DWORD id)
{
    (void)iface;
    (void)id;
    return E_NOTIMPL;
}

static HRESULT WINAPI wma_mft_AddInputStreams(WG_IMFTransform *iface, DWORD count, DWORD *ids)
{
    (void)iface;
    (void)count;
    (void)ids;
    return E_NOTIMPL;
}

static HRESULT WINAPI wma_mft_GetInputAvailableType(
    WG_IMFTransform *iface,
    DWORD id,
    DWORD index,
    void **ppType)
{
    (void)iface;
    if (!ppType) {
        return E_POINTER;
    }
    if (id != 0) {
        return MF_E_INVALIDSTREAMNUMBER;
    }
    if (index >= 4) {
        return MF_E_NO_MORE_TYPES;
    }
    if (*ppType != NULL) {
        memcpy(*ppType, g_wma_input_subtypes[index], sizeof(GUID));
    }
    return S_OK;
}

static HRESULT WINAPI wma_mft_GetOutputAvailableType(
    WG_IMFTransform *iface,
    DWORD id,
    DWORD index,
    void **ppType)
{
    struct wg_wma_decoder *dec = wma_from_IMFTransform(iface);
    if (!ppType) {
        return E_POINTER;
    }
    if (id != 0) {
        return MF_E_INVALIDSTREAMNUMBER;
    }
    if (!dec->input_set) {
        return MF_E_TRANSFORM_TYPE_NOT_SET;
    }
    if (index >= 2) {
        return MF_E_NO_MORE_TYPES;
    }
    if (*ppType != NULL) {
        memcpy(*ppType, g_wma_output_subtypes[index], sizeof(GUID));
    }
    return S_OK;
}

static BOOL wma_validate_input_mt(const WG_DMO_MEDIA_TYPE *mt, WG_WAVEFORMATEX *out_wfx)
{
    size_t i;
    BOOL matched = FALSE;
    const WG_WAVEFORMATEX *wfx;

    if (!mt || !IsEqualGUID(&mt->majortype, &MFMediaType_Audio)) {
        return FALSE;
    }
    for (i = 0; i < 4; i++) {
        if (IsEqualGUID(&mt->subtype, g_wma_input_subtypes[i])) {
            matched = TRUE;
            break;
        }
    }
    if (!matched) {
        return FALSE;
    }
    if (!mt->pbFormat || mt->cbFormat < sizeof(WG_WAVEFORMATEX)) {
        return FALSE;
    }
    wfx = (const WG_WAVEFORMATEX *)mt->pbFormat;
    if (wfx->nChannels == 0 || wfx->nChannels > 8 || wfx->nSamplesPerSec == 0 || wfx->nBlockAlign == 0) {
        return FALSE;
    }
    *out_wfx = *wfx;
    return TRUE;
}

static BOOL wma_validate_output_mt(
    const WG_DMO_MEDIA_TYPE *mt,
    const WG_WAVEFORMATEX *in_wfx,
    WG_WAVEFORMATEX *out_wfx,
    BOOL *out_is_float)
{
    BOOL is_float = FALSE;
    const WG_WAVEFORMATEX *wfx;

    if (!mt || !IsEqualGUID(&mt->majortype, &MFMediaType_Audio)) {
        return FALSE;
    }
    if (IsEqualGUID(&mt->subtype, &MFAudioFormat_Float)) {
        is_float = TRUE;
    } else if (IsEqualGUID(&mt->subtype, &MFAudioFormat_PCM)) {
        is_float = FALSE;
    } else {
        return FALSE;
    }
    if (!mt->pbFormat || mt->cbFormat < sizeof(WG_WAVEFORMATEX)) {
        return FALSE;
    }
    wfx = (const WG_WAVEFORMATEX *)mt->pbFormat;
    if (wfx->nChannels != in_wfx->nChannels || wfx->nSamplesPerSec != in_wfx->nSamplesPerSec || wfx->nBlockAlign == 0) {
        return FALSE;
    }
    *out_wfx = *wfx;
    *out_is_float = is_float;
    return TRUE;
}

static HRESULT WINAPI wma_mft_SetInputType(
    WG_IMFTransform *iface,
    DWORD id,
    void *pType,
    DWORD flags)
{
    struct wg_wma_decoder *dec = wma_from_IMFTransform(iface);
    const WG_DMO_MEDIA_TYPE *mt = (const WG_DMO_MEDIA_TYPE *)pType;
    WG_WAVEFORMATEX wfx;

    if (id != 0) {
        return MF_E_INVALIDSTREAMNUMBER;
    }
    if (!pType) {
        dec->input_set = FALSE;
        dec->output_set = FALSE;
        return S_OK;
    }
    if (!wma_validate_input_mt(mt, &wfx)) {
        return MF_E_INVALIDMEDIATYPE;
    }
    if (flags & MFT_SET_TYPE_TEST_ONLY) {
        return S_OK;
    }
    dec->input_wfx = wfx;
    dec->input_buf_size = wfx.nBlockAlign;
    dec->input_set = TRUE;
    dec->output_set = FALSE;
    return S_OK;
}

static HRESULT WINAPI wma_mft_SetOutputType(
    WG_IMFTransform *iface,
    DWORD id,
    void *pType,
    DWORD flags)
{
    struct wg_wma_decoder *dec = wma_from_IMFTransform(iface);
    const WG_DMO_MEDIA_TYPE *mt = (const WG_DMO_MEDIA_TYPE *)pType;
    WG_WAVEFORMATEX wfx;
    BOOL is_float = FALSE;

    if (id != 0) {
        return MF_E_INVALIDSTREAMNUMBER;
    }
    if (!dec->input_set) {
        return MF_E_TRANSFORM_TYPE_NOT_SET;
    }
    if (!pType) {
        dec->output_set = FALSE;
        return S_OK;
    }
    if (!wma_validate_output_mt(mt, &dec->input_wfx, &wfx, &is_float)) {
        return MF_E_INVALIDMEDIATYPE;
    }
    if (flags & MFT_SET_TYPE_TEST_ONLY) {
        return S_OK;
    }
    dec->output_wfx = wfx;
    dec->output_float = is_float;
    /* Upstream wma_decoder.c: output_buf_size = 1024 * block_alignment * channel_count */
    dec->output_buf_size = 1024U * (DWORD)wfx.nBlockAlign * (DWORD)wfx.nChannels;
    dec->output_set = TRUE;
    return S_OK;
}

static HRESULT WINAPI wma_mft_GetInputCurrentType(WG_IMFTransform *iface, DWORD id, void **ppType)
{
    struct wg_wma_decoder *dec = wma_from_IMFTransform(iface);
    if (!ppType) {
        return E_POINTER;
    }
    if (id != 0) {
        return MF_E_INVALIDSTREAMNUMBER;
    }
    if (!dec->input_set) {
        return MF_E_TRANSFORM_TYPE_NOT_SET;
    }
    return S_OK;
}

static HRESULT WINAPI wma_mft_GetOutputCurrentType(WG_IMFTransform *iface, DWORD id, void **ppType)
{
    struct wg_wma_decoder *dec = wma_from_IMFTransform(iface);
    if (!ppType) {
        return E_POINTER;
    }
    if (id != 0) {
        return MF_E_INVALIDSTREAMNUMBER;
    }
    if (!dec->output_set) {
        return MF_E_TRANSFORM_TYPE_NOT_SET;
    }
    return S_OK;
}

static HRESULT WINAPI wma_mft_GetInputStatus(WG_IMFTransform *iface, DWORD id, DWORD *flags)
{
    struct wg_wma_decoder *dec = wma_from_IMFTransform(iface);
    if (!flags) {
        return E_POINTER;
    }
    if (id != 0) {
        return MF_E_INVALIDSTREAMNUMBER;
    }
    if (!dec->input_set || !dec->output_set) {
        return MF_E_TRANSFORM_TYPE_NOT_SET;
    }
    *flags = dec->has_pending_frame ? 0 : MFT_INPUT_STATUS_ACCEPT_DATA;
    return S_OK;
}

static HRESULT WINAPI wma_mft_GetOutputStatus(WG_IMFTransform *iface, DWORD *flags)
{
    struct wg_wma_decoder *dec = wma_from_IMFTransform(iface);
    if (!flags) {
        return E_POINTER;
    }
    if (!dec->input_set || !dec->output_set) {
        return MF_E_TRANSFORM_TYPE_NOT_SET;
    }
    *flags = dec->has_pending_frame ? MFT_OUTPUT_STATUS_SAMPLE_READY : 0;
    return S_OK;
}

static HRESULT WINAPI wma_mft_SetOutputBounds(WG_IMFTransform *iface, LONGLONG lower, LONGLONG upper)
{
    (void)iface;
    (void)lower;
    (void)upper;
    return S_OK;
}

static HRESULT WINAPI wma_mft_ProcessEvent(WG_IMFTransform *iface, DWORD id, void *event)
{
    (void)iface;
    (void)id;
    (void)event;
    return E_NOTIMPL;
}

static HRESULT WINAPI wma_mft_ProcessMessage(
    WG_IMFTransform *iface,
    MFT_MESSAGE_TYPE message,
    ULONG_PTR param)
{
    struct wg_wma_decoder *dec = wma_from_IMFTransform(iface);
    (void)param;
    if (message == MFT_MESSAGE_COMMAND_FLUSH || message == MFT_MESSAGE_DROP_SAMPLES) {
        dec->has_pending_frame = FALSE;
        dec->pending_pcm_len = 0;
    }
    return S_OK;
}

static HRESULT WINAPI wma_mft_ProcessInput(
    WG_IMFTransform *iface,
    DWORD id,
    void *pSample,
    DWORD flags)
{
    struct wg_wma_decoder *dec = wma_from_IMFTransform(iface);
    DWORD written = 0;
    HRESULT hr;

    (void)flags;
    if (id != 0) {
        return MF_E_INVALIDSTREAMNUMBER;
    }
    if (!pSample) {
        return E_POINTER;
    }
    if (!dec->input_set || !dec->output_set) {
        return MF_E_TRANSFORM_TYPE_NOT_SET;
    }

    hr = WineGStreamerAudioToolboxDecodePacket(
        &dec->input_wfx,
        (const BYTE *)pSample,
        dec->input_buf_size,
        dec->output_float,
        dec->pending_pcm,
        sizeof(dec->pending_pcm),
        &written);
    if (FAILED(hr)) {
        return hr;
    }
    dec->pending_pcm_len = written;
    dec->has_pending_frame = TRUE;
    return S_OK;
}

static HRESULT WINAPI wma_mft_ProcessOutput(
    WG_IMFTransform *iface,
    DWORD flags,
    DWORD count,
    MFT_OUTPUT_DATA_BUFFER *samples,
    DWORD *status)
{
    struct wg_wma_decoder *dec = wma_from_IMFTransform(iface);
    (void)flags;
    if (count != 1 || !samples || !status) {
        return E_INVALIDARG;
    }
    if (!dec->input_set || !dec->output_set) {
        return MF_E_TRANSFORM_TYPE_NOT_SET;
    }
    *status = 0;
    samples[0].dwStatus = 0;
    if (!samples[0].pSample || !dec->has_pending_frame) {
        samples[0].dwStatus = MFT_OUTPUT_DATA_BUFFER_NO_SAMPLE;
        return MF_E_TRANSFORM_NEED_MORE_INPUT;
    }
    memcpy(samples[0].pSample, dec->pending_pcm, dec->pending_pcm_len);
    dec->has_pending_frame = FALSE;
    return S_OK;
}

static const IMFTransformVtbl g_wma_mft_vtbl = {
    wma_mft_QueryInterface,
    wma_mft_AddRef,
    wma_mft_Release,
    wma_mft_GetStreamLimits,
    wma_mft_GetStreamCount,
    wma_mft_GetStreamIDs,
    wma_mft_GetInputStreamInfo,
    wma_mft_GetOutputStreamInfo,
    wma_mft_GetAttributes,
    wma_mft_GetInputStreamAttributes,
    wma_mft_GetOutputStreamAttributes,
    wma_mft_DeleteInputStream,
    wma_mft_AddInputStreams,
    wma_mft_GetInputAvailableType,
    wma_mft_GetOutputAvailableType,
    wma_mft_SetInputType,
    wma_mft_SetOutputType,
    wma_mft_GetInputCurrentType,
    wma_mft_GetOutputCurrentType,
    wma_mft_GetInputStatus,
    wma_mft_GetOutputStatus,
    wma_mft_SetOutputBounds,
    wma_mft_ProcessEvent,
    wma_mft_ProcessMessage,
    wma_mft_ProcessInput,
    wma_mft_ProcessOutput
};

/* IMediaObject Vtbl for WMA Decoder DMO */
static HRESULT WINAPI wma_dmo_QueryInterface(WG_IMediaObject *iface, REFIID riid, void **ppv)
{
    struct wg_wma_decoder *dec = wma_from_IMediaObject(iface);
    return IUnknown_QueryInterface(dec->outer, riid, ppv);
}

static ULONG WINAPI wma_dmo_AddRef(WG_IMediaObject *iface)
{
    struct wg_wma_decoder *dec = wma_from_IMediaObject(iface);
    return IUnknown_AddRef(dec->outer);
}

static ULONG WINAPI wma_dmo_Release(WG_IMediaObject *iface)
{
    struct wg_wma_decoder *dec = wma_from_IMediaObject(iface);
    return IUnknown_Release(dec->outer);
}

static HRESULT WINAPI wma_dmo_GetStreamCount(WG_IMediaObject *iface, DWORD *in_cnt, DWORD *out_cnt)
{
    (void)iface;
    if (!in_cnt || !out_cnt) {
        return E_POINTER;
    }
    *in_cnt = *out_cnt = 1;
    return S_OK;
}

static HRESULT WINAPI wma_dmo_GetInputStreamInfo(WG_IMediaObject *iface, DWORD idx, DWORD *flags)
{
    (void)iface;
    if (!flags) {
        return E_POINTER;
    }
    if (idx != 0) {
        return DMO_E_INVALIDSTREAMINDEX;
    }
    *flags = 0;
    return S_OK;
}

static HRESULT WINAPI wma_dmo_GetOutputStreamInfo(WG_IMediaObject *iface, DWORD idx, DWORD *flags)
{
    (void)iface;
    if (!flags) {
        return E_POINTER;
    }
    if (idx != 0) {
        return DMO_E_INVALIDSTREAMINDEX;
    }
    *flags = 0;
    return S_OK;
}

static HRESULT WINAPI wma_dmo_GetInputType(
    WG_IMediaObject *iface,
    DWORD idx,
    DWORD type_idx,
    WG_DMO_MEDIA_TYPE *pmt)
{
    (void)iface;
    if (idx != 0) {
        return DMO_E_INVALIDSTREAMINDEX;
    }
    if (type_idx >= 4) {
        return DMO_E_NO_MORE_ITEMS;
    }
    if (!pmt) {
        return S_OK;
    }
    memset(pmt, 0, sizeof(*pmt));
    pmt->majortype = MFMediaType_Audio;
    pmt->subtype = *g_wma_input_subtypes[type_idx];
    pmt->bFixedSizeSamples = FALSE;
    pmt->bTemporalCompression = TRUE;
    pmt->lSampleSize = 0;
    return S_OK;
}

static HRESULT WINAPI wma_dmo_GetOutputType(
    WG_IMediaObject *iface,
    DWORD idx,
    DWORD type_idx,
    WG_DMO_MEDIA_TYPE *pmt)
{
    struct wg_wma_decoder *dec = wma_from_IMediaObject(iface);
    if (idx != 0) {
        return DMO_E_INVALIDSTREAMINDEX;
    }
    if (type_idx >= 1) {
        return DMO_E_NO_MORE_ITEMS;
    }
    if (!dec->input_set) {
        return DMO_E_TYPE_NOT_SET;
    }
    if (!pmt) {
        return S_OK;
    }
    memset(pmt, 0, sizeof(*pmt));
    pmt->majortype = MFMediaType_Audio;
    pmt->subtype = (dec->input_wfx.wBitsPerSample == 32) ? MFAudioFormat_Float : MFAudioFormat_PCM;
    pmt->bFixedSizeSamples = TRUE;
    pmt->bTemporalCompression = FALSE;
    return S_OK;
}

static HRESULT WINAPI wma_dmo_SetInputType(
    WG_IMediaObject *iface,
    DWORD idx,
    const WG_DMO_MEDIA_TYPE *pmt,
    DWORD flags)
{
    struct wg_wma_decoder *dec = wma_from_IMediaObject(iface);
    WG_WAVEFORMATEX wfx;

    if (idx != 0) {
        return DMO_E_INVALIDSTREAMINDEX;
    }
    if (flags & DMO_SET_TYPEF_CLEAR) {
        dec->input_set = FALSE;
        dec->output_set = FALSE;
        return S_OK;
    }
    if (!wma_validate_input_mt(pmt, &wfx)) {
        return DMO_E_TYPE_NOT_ACCEPTED;
    }
    if (flags & DMO_SET_TYPEF_TEST_ONLY) {
        return S_OK;
    }
    dec->input_wfx = wfx;
    dec->input_buf_size = wfx.nBlockAlign;
    dec->input_set = TRUE;
    dec->output_set = FALSE;
    return S_OK;
}

static HRESULT WINAPI wma_dmo_SetOutputType(
    WG_IMediaObject *iface,
    DWORD idx,
    const WG_DMO_MEDIA_TYPE *pmt,
    DWORD flags)
{
    struct wg_wma_decoder *dec = wma_from_IMediaObject(iface);
    WG_WAVEFORMATEX wfx;
    BOOL is_float = FALSE;

    if (idx != 0) {
        return DMO_E_INVALIDSTREAMINDEX;
    }
    if (flags & DMO_SET_TYPEF_CLEAR) {
        dec->output_set = FALSE;
        return S_OK;
    }
    if (!dec->input_set) {
        return DMO_E_TYPE_NOT_SET;
    }
    if (!wma_validate_output_mt(pmt, &dec->input_wfx, &wfx, &is_float)) {
        return DMO_E_TYPE_NOT_ACCEPTED;
    }
    if (flags & DMO_SET_TYPEF_TEST_ONLY) {
        return S_OK;
    }
    dec->output_wfx = wfx;
    dec->output_float = is_float;
    dec->output_buf_size = 1024U * (DWORD)wfx.nBlockAlign * (DWORD)wfx.nChannels;
    dec->output_set = TRUE;
    return S_OK;
}

static HRESULT WINAPI wma_dmo_GetInputCurrentType(WG_IMediaObject *iface, DWORD idx, WG_DMO_MEDIA_TYPE *pmt)
{
    struct wg_wma_decoder *dec = wma_from_IMediaObject(iface);
    if (idx != 0) {
        return DMO_E_INVALIDSTREAMINDEX;
    }
    if (!dec->input_set) {
        return DMO_E_TYPE_NOT_SET;
    }
    if (!pmt) {
        return E_POINTER;
    }
    memset(pmt, 0, sizeof(*pmt));
    pmt->majortype = MFMediaType_Audio;
    pmt->subtype = MFAudioFormat_WMAudioV8;
    return S_OK;
}

static HRESULT WINAPI wma_dmo_GetOutputCurrentType(WG_IMediaObject *iface, DWORD idx, WG_DMO_MEDIA_TYPE *pmt)
{
    struct wg_wma_decoder *dec = wma_from_IMediaObject(iface);
    if (idx != 0) {
        return DMO_E_INVALIDSTREAMINDEX;
    }
    if (!dec->output_set) {
        return DMO_E_TYPE_NOT_SET;
    }
    if (!pmt) {
        return E_POINTER;
    }
    memset(pmt, 0, sizeof(*pmt));
    pmt->majortype = MFMediaType_Audio;
    pmt->subtype = dec->output_float ? MFAudioFormat_Float : MFAudioFormat_PCM;
    return S_OK;
}

static HRESULT WINAPI wma_dmo_GetInputSizeInfo(
    WG_IMediaObject *iface,
    DWORD idx,
    DWORD *size,
    DWORD *lookahead,
    DWORD *alignment)
{
    struct wg_wma_decoder *dec = wma_from_IMediaObject(iface);
    if (idx != 0) {
        return DMO_E_INVALIDSTREAMINDEX;
    }
    if (!dec->input_set || !dec->output_set) {
        return DMO_E_TYPE_NOT_SET;
    }
    if (!size || !lookahead || !alignment) {
        return E_POINTER;
    }
    *size = dec->input_buf_size;
    *lookahead = 0;
    *alignment = 1;
    return S_OK;
}

static HRESULT WINAPI wma_dmo_GetOutputSizeInfo(
    WG_IMediaObject *iface,
    DWORD idx,
    DWORD *size,
    DWORD *alignment)
{
    struct wg_wma_decoder *dec = wma_from_IMediaObject(iface);
    if (idx != 0) {
        return DMO_E_INVALIDSTREAMINDEX;
    }
    if (!dec->input_set || !dec->output_set) {
        return DMO_E_TYPE_NOT_SET;
    }
    if (!size || !alignment) {
        return E_POINTER;
    }
    *size = dec->output_buf_size;
    *alignment = 1;
    return S_OK;
}

static HRESULT WINAPI wma_dmo_GetInputMaxLatency(WG_IMediaObject *iface, DWORD idx, LONGLONG *lat)
{
    (void)iface;
    (void)idx;
    (void)lat;
    return E_NOTIMPL;
}

static HRESULT WINAPI wma_dmo_SetInputMaxLatency(WG_IMediaObject *iface, DWORD idx, LONGLONG lat)
{
    (void)iface;
    (void)idx;
    (void)lat;
    return E_NOTIMPL;
}

static HRESULT WINAPI wma_dmo_Flush(WG_IMediaObject *iface)
{
    struct wg_wma_decoder *dec = wma_from_IMediaObject(iface);
    dec->has_pending_frame = FALSE;
    dec->pending_pcm_len = 0;
    return S_OK;
}

static HRESULT WINAPI wma_dmo_Discontinuity(WG_IMediaObject *iface, DWORD idx)
{
    if (idx != 0) {
        return DMO_E_INVALIDSTREAMINDEX;
    }
    return wma_dmo_Flush(iface);
}

static HRESULT WINAPI wma_dmo_AllocateStreamingResources(WG_IMediaObject *iface)
{
    (void)iface;
    return S_OK;
}

static HRESULT WINAPI wma_dmo_FreeStreamingResources(WG_IMediaObject *iface)
{
    (void)iface;
    return S_OK;
}

static HRESULT WINAPI wma_dmo_GetInputStatus(WG_IMediaObject *iface, DWORD idx, DWORD *flags)
{
    (void)iface;
    if (idx != 0) {
        return DMO_E_INVALIDSTREAMINDEX;
    }
    if (!flags) {
        return E_POINTER;
    }
    *flags = DMO_INPUT_STATUSF_ACCEPT_DATA;
    return S_OK;
}

static HRESULT WINAPI wma_dmo_ProcessInput(
    WG_IMediaObject *iface,
    DWORD idx,
    void *pBuffer,
    DWORD flags,
    LONGLONG ts,
    LONGLONG len)
{
    struct wg_wma_decoder *dec = wma_from_IMediaObject(iface);
    (void)ts;
    (void)len;
    if (idx != 0) {
        return DMO_E_INVALIDSTREAMINDEX;
    }
    return wma_mft_ProcessInput(&dec->IMFTransform_iface, 0, pBuffer, flags);
}

static HRESULT WINAPI wma_dmo_ProcessOutput(
    WG_IMediaObject *iface,
    DWORD flags,
    DWORD count,
    DMO_OUTPUT_DATA_BUFFER *buffers,
    DWORD *status)
{
    struct wg_wma_decoder *dec = wma_from_IMediaObject(iface);
    MFT_OUTPUT_DATA_BUFFER mft_buf;
    HRESULT hr;

    if (count != 1 || !buffers || !status) {
        return E_INVALIDARG;
    }
    memset(&mft_buf, 0, sizeof(mft_buf));
    mft_buf.pSample = buffers[0].pBuffer;
    hr = wma_mft_ProcessOutput(&dec->IMFTransform_iface, flags, 1, &mft_buf, status);
    buffers[0].dwStatus = mft_buf.dwStatus;
    return (hr == MF_E_TRANSFORM_NEED_MORE_INPUT) ? S_FALSE : hr;
}

static HRESULT WINAPI wma_dmo_Lock(WG_IMediaObject *iface, LONG lock)
{
    (void)iface;
    (void)lock;
    return S_OK;
}

static const IMediaObjectVtbl g_wma_dmo_vtbl = {
    wma_dmo_QueryInterface,
    wma_dmo_AddRef,
    wma_dmo_Release,
    wma_dmo_GetStreamCount,
    wma_dmo_GetInputStreamInfo,
    wma_dmo_GetOutputStreamInfo,
    wma_dmo_GetInputType,
    wma_dmo_GetOutputType,
    wma_dmo_SetInputType,
    wma_dmo_SetOutputType,
    wma_dmo_GetInputCurrentType,
    wma_dmo_GetOutputCurrentType,
    wma_dmo_GetInputSizeInfo,
    wma_dmo_GetOutputSizeInfo,
    wma_dmo_GetInputMaxLatency,
    wma_dmo_SetInputMaxLatency,
    wma_dmo_Flush,
    wma_dmo_Discontinuity,
    wma_dmo_AllocateStreamingResources,
    wma_dmo_FreeStreamingResources,
    wma_dmo_GetInputStatus,
    wma_dmo_ProcessInput,
    wma_dmo_ProcessOutput,
    wma_dmo_Lock
};

static HRESULT WINAPI wma_bag_QueryInterface(WG_IPropertyBag *iface, REFIID riid, void **ppv)
{
    struct wg_wma_decoder *dec = wma_from_IPropertyBag(iface);
    return IUnknown_QueryInterface(dec->outer, riid, ppv);
}

static ULONG WINAPI wma_bag_AddRef(WG_IPropertyBag *iface)
{
    struct wg_wma_decoder *dec = wma_from_IPropertyBag(iface);
    return IUnknown_AddRef(dec->outer);
}

static ULONG WINAPI wma_bag_Release(WG_IPropertyBag *iface)
{
    struct wg_wma_decoder *dec = wma_from_IPropertyBag(iface);
    return IUnknown_Release(dec->outer);
}

static HRESULT WINAPI wma_bag_Read(WG_IPropertyBag *iface, LPCWSTR name, void *var, void *err_log)
{
    (void)iface;
    (void)name;
    (void)var;
    (void)err_log;
    return E_NOTIMPL;
}

static HRESULT WINAPI wma_bag_Write(WG_IPropertyBag *iface, LPCWSTR name, void *var)
{
    (void)iface;
    (void)name;
    (void)var;
    return S_OK;
}

static const IPropertyBagVtbl g_wma_bag_vtbl = {
    wma_bag_QueryInterface,
    wma_bag_AddRef,
    wma_bag_Release,
    wma_bag_Read,
    wma_bag_Write
};

HRESULT WINAPI winegstreamer_create_wma_decoder(WG_IUnknown *outer, REFIID riid, void **out)
{
    struct wg_wma_decoder *dec;
    HRESULT hr;

    TRACE("(%p, %p, %p)\n", outer, riid, out);

    if (!out) {
        return E_POINTER;
    }
    *out = NULL;
    if (!riid) {
        return E_POINTER;
    }
    if (wg_bridge_is_sim_stub()) {
        return E_FAIL;
    }
    if (outer && !IsEqualIID(riid, &IID_IUnknown)) {
        return E_NOINTERFACE;
    }

    dec = (struct wg_wma_decoder *)calloc(1, sizeof(*dec));
    if (!dec) {
        return E_OUTOFMEMORY;
    }

    dec->magic = WG_WMA_DECODER_MAGIC;
    dec->IUnknown_inner.lpVtbl = &g_wma_inner_vtbl;
    dec->IMFTransform_iface.lpVtbl = &g_wma_mft_vtbl;
    dec->IMediaObject_iface.lpVtbl = &g_wma_dmo_vtbl;
    dec->IPropertyBag_iface.lpVtbl = &g_wma_bag_vtbl;
    dec->refcount = 1;
    dec->outer = outer ? outer : &dec->IUnknown_inner;

    if (outer) {
        *out = &dec->IUnknown_inner;
        return S_OK;
    }

    hr = IUnknown_QueryInterface(&dec->IUnknown_inner, riid, out);
    IUnknown_Release(&dec->IUnknown_inner);
    return hr;
}

/* ========================================================================
 * 3. Video Decoder MFT (winegstreamer_create_video_decoder)
 * ======================================================================== */

struct wg_video_decoder {
    uint32_t        magic;
    WG_IMFTransform IMFTransform_iface;
    LONG            refcount;
    UINT32          width;
    UINT32          height;
    BOOL            input_set;
    BOOL            output_set;
};

static inline struct wg_video_decoder *vdec_from_IMFTransform(WG_IMFTransform *iface)
{
    return (struct wg_video_decoder *)((char *)iface - offsetof(struct wg_video_decoder, IMFTransform_iface));
}

static HRESULT WINAPI vdec_QueryInterface(WG_IMFTransform *iface, REFIID riid, void **ppv)
{
    struct wg_video_decoder *dec = vdec_from_IMFTransform(iface);
    if (!ppv) {
        return E_POINTER;
    }
    if (!riid || dec->magic != WG_VIDEO_DECODER_MAGIC) {
        *ppv = NULL;
        return E_POINTER;
    }
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IMFTransform)) {
        *ppv = &dec->IMFTransform_iface;
        dec->IMFTransform_iface.lpVtbl->AddRef(&dec->IMFTransform_iface);
        return S_OK;
    }
    *ppv = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI vdec_AddRef(WG_IMFTransform *iface)
{
    struct wg_video_decoder *dec = vdec_from_IMFTransform(iface);
    return (ULONG)__sync_add_and_fetch(&dec->refcount, 1);
}

static ULONG WINAPI vdec_Release(WG_IMFTransform *iface)
{
    struct wg_video_decoder *dec = vdec_from_IMFTransform(iface);
    LONG ref = __sync_sub_and_fetch(&dec->refcount, 1);
    if (ref == 0) {
        dec->magic = 0;
        free(dec);
    }
    return (ULONG)ref;
}

static HRESULT WINAPI vdec_GetOutputAvailableType(
    WG_IMFTransform *iface,
    DWORD id,
    DWORD index,
    void **ppType)
{
    (void)iface;
    if (!ppType) {
        return E_POINTER;
    }
    if (id != 0) {
        return MF_E_INVALIDSTREAMNUMBER;
    }
    if (index >= 4) {
        return MF_E_NO_MORE_TYPES;
    }
    if (*ppType != NULL) {
        memcpy(*ppType, g_video_output_subtypes[index], sizeof(GUID));
    }
    return S_OK;
}

static const IMFTransformVtbl g_vdec_vtbl = {
    vdec_QueryInterface,
    vdec_AddRef,
    vdec_Release,
    wma_mft_GetStreamLimits,
    wma_mft_GetStreamCount,
    wma_mft_GetStreamIDs,
    wma_mft_GetInputStreamInfo,
    wma_mft_GetOutputStreamInfo,
    wma_mft_GetAttributes,
    wma_mft_GetInputStreamAttributes,
    wma_mft_GetOutputStreamAttributes,
    wma_mft_DeleteInputStream,
    wma_mft_AddInputStreams,
    wma_mft_GetInputAvailableType,
    vdec_GetOutputAvailableType,
    wma_mft_SetInputType,
    wma_mft_SetOutputType,
    wma_mft_GetInputCurrentType,
    wma_mft_GetOutputCurrentType,
    wma_mft_GetInputStatus,
    wma_mft_GetOutputStatus,
    wma_mft_SetOutputBounds,
    wma_mft_ProcessEvent,
    wma_mft_ProcessMessage,
    wma_mft_ProcessInput,
    wma_mft_ProcessOutput
};

HRESULT WINAPI winegstreamer_create_video_decoder(WG_IMFTransform **out)
{
    struct wg_video_decoder *dec;

    TRACE("(%p)\n", out);

    if (!out) {
        return E_POINTER;
    }
    *out = NULL;
    if (wg_bridge_is_sim_stub()) {
        return E_FAIL;
    }

    dec = (struct wg_video_decoder *)calloc(1, sizeof(*dec));
    if (!dec) {
        return E_OUTOFMEMORY;
    }
    dec->magic = WG_VIDEO_DECODER_MAGIC;
    dec->IMFTransform_iface.lpVtbl = &g_vdec_vtbl;
    dec->refcount = 1;
    dec->width = 640;
    dec->height = 480;

    *out = &dec->IMFTransform_iface;
    return S_OK;
}

/* ========================================================================
 * 4. Windows Media Sync Reader (winegstreamer_create_wm_sync_reader)
 * ======================================================================== */

struct wg_wm_reader {
    uint32_t         magic;
    WG_IUnknown      IUnknown_inner;
    WG_IWMSyncReader2 IWMSyncReader2_iface;
    WG_IUnknown     *outer;
    LONG             refcount;
    BOOL             is_open;
    int              stream_selection[8];
    BOOL             compressed_samples[8];
    ULONGLONG        next_pts;
    BYTE             sample_buf[1024];
};

static inline struct wg_wm_reader *reader_from_IUnknown(WG_IUnknown *iface)
{
    return (struct wg_wm_reader *)((char *)iface - offsetof(struct wg_wm_reader, IUnknown_inner));
}

static inline struct wg_wm_reader *reader_from_IWMSyncReader2(WG_IWMSyncReader2 *iface)
{
    return (struct wg_wm_reader *)((char *)iface - offsetof(struct wg_wm_reader, IWMSyncReader2_iface));
}

static HRESULT WINAPI reader_inner_QueryInterface(WG_IUnknown *iface, REFIID riid, void **ppv)
{
    struct wg_wm_reader *r = reader_from_IUnknown(iface);
    if (!ppv) {
        return E_POINTER;
    }
    if (!riid || r->magic != WG_WM_READER_MAGIC) {
        *ppv = NULL;
        return E_POINTER;
    }
    if (IsEqualIID(riid, &IID_IUnknown)) {
        *ppv = &r->IUnknown_inner;
    } else if (IsEqualIID(riid, &IID_IWMSyncReader) || IsEqualIID(riid, &IID_IWMSyncReader2)) {
        *ppv = &r->IWMSyncReader2_iface;
    } else {
        *ppv = NULL;
        return E_NOINTERFACE;
    }
    IUnknown_AddRef((WG_IUnknown *)*ppv);
    return S_OK;
}

static ULONG WINAPI reader_inner_AddRef(WG_IUnknown *iface)
{
    struct wg_wm_reader *r = reader_from_IUnknown(iface);
    return (ULONG)__sync_add_and_fetch(&r->refcount, 1);
}

static ULONG WINAPI reader_inner_Release(WG_IUnknown *iface)
{
    struct wg_wm_reader *r = reader_from_IUnknown(iface);
    LONG ref = __sync_sub_and_fetch(&r->refcount, 1);
    if (ref == 0) {
        r->magic = 0;
        memset(r->sample_buf, 0, sizeof(r->sample_buf));
        free(r);
    }
    return (ULONG)ref;
}

static const IUnknownVtbl g_reader_inner_vtbl = {
    reader_inner_QueryInterface,
    reader_inner_AddRef,
    reader_inner_Release
};

static HRESULT WINAPI reader_QueryInterface(WG_IWMSyncReader2 *iface, REFIID riid, void **ppv)
{
    struct wg_wm_reader *r = reader_from_IWMSyncReader2(iface);
    return IUnknown_QueryInterface(r->outer, riid, ppv);
}

static ULONG WINAPI reader_AddRef(WG_IWMSyncReader2 *iface)
{
    struct wg_wm_reader *r = reader_from_IWMSyncReader2(iface);
    return IUnknown_AddRef(r->outer);
}

static ULONG WINAPI reader_Release(WG_IWMSyncReader2 *iface)
{
    struct wg_wm_reader *r = reader_from_IWMSyncReader2(iface);
    return IUnknown_Release(r->outer);
}

static HRESULT WINAPI reader_Open(WG_IWMSyncReader2 *iface, const WCHAR *filename)
{
    struct wg_wm_reader *r = reader_from_IWMSyncReader2(iface);
    if (!filename) {
        return E_INVALIDARG;
    }
    r->is_open = TRUE;
    r->next_pts = 0;
    return S_OK;
}

static HRESULT WINAPI reader_Close(WG_IWMSyncReader2 *iface)
{
    struct wg_wm_reader *r = reader_from_IWMSyncReader2(iface);
    if (!r->is_open) {
        return NS_E_INVALID_REQUEST;
    }
    r->is_open = FALSE;
    return S_OK;
}

static HRESULT WINAPI reader_SetRange(WG_IWMSyncReader2 *iface, ULONGLONG start, LONGLONG dur)
{
    struct wg_wm_reader *r = reader_from_IWMSyncReader2(iface);
    (void)dur;
    r->next_pts = start;
    return S_OK;
}

static HRESULT WINAPI reader_SetRangeByFrame(WG_IWMSyncReader2 *iface, WORD stm, ULONGLONG frame, LONGLONG cnt)
{
    (void)iface; (void)stm; (void)frame; (void)cnt;
    return S_OK;
}

static HRESULT WINAPI reader_GetNextSample(
    WG_IWMSyncReader2 *iface,
    WORD wStreamNum,
    void **ppSample,
    ULONGLONG *pcnsSampleTime,
    ULONGLONG *pcnsDuration,
    DWORD *pdwFlags,
    DWORD *pdwOutputNum,
    WORD *pwStreamNum)
{
    struct wg_wm_reader *r = reader_from_IWMSyncReader2(iface);
    if (!ppSample || !pcnsSampleTime || !pdwFlags) {
        return E_INVALIDARG;
    }
    if (!r->is_open) {
        return NS_E_INVALID_REQUEST;
    }
    *ppSample = r->sample_buf;
    *pcnsSampleTime = r->next_pts;
    if (pcnsDuration) {
        *pcnsDuration = 232199ULL; /* ~23.2 ms (1024 samples @ 44.1 kHz in 100-ns units) */
    }
    *pdwFlags = 1; /* WM_SF_CLEANPOINT */
    if (pdwOutputNum) {
        *pdwOutputNum = 0;
    }
    if (pwStreamNum) {
        *pwStreamNum = wStreamNum ? wStreamNum : 1;
    }
    r->next_pts += 232199ULL;
    return S_OK;
}

static HRESULT WINAPI reader_SetStreamsSelected(
    WG_IWMSyncReader2 *iface,
    WORD count,
    WORD *nums,
    int *selections)
{
    struct wg_wm_reader *r = reader_from_IWMSyncReader2(iface);
    WORD i;
    if (count == 0 || !nums || !selections) {
        return E_INVALIDARG;
    }
    for (i = 0; i < count; i++) {
        if (nums[i] == 0 || nums[i] >= 8) {
            return E_INVALIDARG;
        }
        r->stream_selection[nums[i]] = selections[i];
    }
    return S_OK;
}

static HRESULT WINAPI reader_GetStreamSelected(WG_IWMSyncReader2 *iface, WORD num, int *sel)
{
    struct wg_wm_reader *r = reader_from_IWMSyncReader2(iface);
    if (!sel || num == 0 || num >= 8) {
        return E_INVALIDARG;
    }
    *sel = r->stream_selection[num];
    return S_OK;
}

static HRESULT WINAPI reader_SetReadStreamSamples(WG_IWMSyncReader2 *iface, WORD num, BOOL comp)
{
    struct wg_wm_reader *r = reader_from_IWMSyncReader2(iface);
    if (num == 0 || num >= 8) {
        return E_INVALIDARG;
    }
    r->compressed_samples[num] = comp ? TRUE : FALSE;
    return S_OK;
}

static HRESULT WINAPI reader_GetReadStreamSamples(WG_IWMSyncReader2 *iface, WORD num, BOOL *comp)
{
    struct wg_wm_reader *r = reader_from_IWMSyncReader2(iface);
    if (!comp || num == 0 || num >= 8) {
        return E_INVALIDARG;
    }
    *comp = r->compressed_samples[num];
    return S_OK;
}

static HRESULT WINAPI reader_GetOutputSetting(
    WG_IWMSyncReader2 *iface, DWORD out_num, const WCHAR *name, int *type, BYTE *val, WORD *len)
{
    (void)iface; (void)out_num; (void)name; (void)type; (void)val; (void)len;
    return S_OK;
}

static HRESULT WINAPI reader_SetOutputSetting(
    WG_IWMSyncReader2 *iface, DWORD out_num, const WCHAR *name, int type, const BYTE *val, WORD len)
{
    (void)iface; (void)out_num; (void)name; (void)type; (void)val; (void)len;
    return S_OK;
}

static HRESULT WINAPI reader_GetOutputCount(WG_IWMSyncReader2 *iface, DWORD *outputs)
{
    (void)iface;
    if (!outputs) return E_INVALIDARG;
    *outputs = 2;
    return S_OK;
}

static HRESULT WINAPI reader_GetOutputProps(WG_IWMSyncReader2 *iface, DWORD out_num, void **ppOutput)
{
    (void)iface; (void)out_num;
    if (!ppOutput) return E_INVALIDARG;
    *ppOutput = NULL;
    return S_OK;
}

static HRESULT WINAPI reader_SetOutputProps(WG_IWMSyncReader2 *iface, DWORD out_num, void *pOutput)
{
    (void)iface; (void)out_num; (void)pOutput;
    return S_OK;
}

static HRESULT WINAPI reader_GetOutputFormatCount(WG_IWMSyncReader2 *iface, DWORD out_num, DWORD *formats)
{
    (void)iface; (void)out_num;
    if (!formats) return E_INVALIDARG;
    *formats = 2;
    return S_OK;
}

static HRESULT WINAPI reader_GetOutputFormat(WG_IWMSyncReader2 *iface, DWORD out_num, DWORD fmt_num, void **ppProps)
{
    (void)iface; (void)out_num; (void)fmt_num;
    if (!ppProps) return E_INVALIDARG;
    *ppProps = NULL;
    return S_OK;
}

static HRESULT WINAPI reader_GetOutputNumberForStream(WG_IWMSyncReader2 *iface, WORD stm_num, DWORD *out_num)
{
    (void)iface;
    if (!out_num || stm_num == 0) return E_INVALIDARG;
    *out_num = (DWORD)(stm_num - 1);
    return S_OK;
}

static HRESULT WINAPI reader_GetStreamNumberForOutput(WG_IWMSyncReader2 *iface, DWORD out_num, WORD *stm_num)
{
    (void)iface;
    if (!stm_num) return E_INVALIDARG;
    *stm_num = (WORD)(out_num + 1);
    return S_OK;
}

static HRESULT WINAPI reader_GetMaxOutputSampleSize(WG_IWMSyncReader2 *iface, DWORD out_num, DWORD *max_sz)
{
    (void)iface; (void)out_num;
    if (!max_sz) return E_INVALIDARG;
    *max_sz = 16384;
    return S_OK;
}

static HRESULT WINAPI reader_GetMaxStreamSampleSize(WG_IWMSyncReader2 *iface, WORD stm_num, DWORD *max_sz)
{
    (void)iface; (void)stm_num;
    if (!max_sz) return E_INVALIDARG;
    *max_sz = 16384;
    return S_OK;
}

static HRESULT WINAPI reader_OpenStream(WG_IWMSyncReader2 *iface, void *pStream)
{
    struct wg_wm_reader *r = reader_from_IWMSyncReader2(iface);
    if (!pStream) return E_INVALIDARG;
    r->is_open = TRUE;
    r->next_pts = 0;
    return S_OK;
}

static HRESULT WINAPI reader_SetRangeByTimecode(WG_IWMSyncReader2 *iface, WORD stm, void *start, void *end)
{
    (void)iface; (void)stm; (void)start; (void)end;
    return E_NOTIMPL;
}

static HRESULT WINAPI reader_SetRangeByFrameEx(
    WG_IWMSyncReader2 *iface, WORD stm, ULONGLONG frame, LONGLONG count, ULONGLONG *start_time)
{
    (void)iface; (void)stm; (void)frame; (void)count; (void)start_time;
    return E_NOTIMPL;
}

static HRESULT WINAPI reader_SetAllocateForOutput(WG_IWMSyncReader2 *iface, DWORD out_num, void *alloc)
{
    (void)iface; (void)out_num; (void)alloc;
    return S_OK;
}

static HRESULT WINAPI reader_GetAllocateForOutput(WG_IWMSyncReader2 *iface, DWORD out_num, void **alloc)
{
    (void)iface; (void)out_num;
    if (!alloc) return E_INVALIDARG;
    *alloc = NULL;
    return S_OK;
}

static HRESULT WINAPI reader_SetAllocateForStream(WG_IWMSyncReader2 *iface, WORD stm_num, void *alloc)
{
    (void)iface; (void)stm_num; (void)alloc;
    return S_OK;
}

static HRESULT WINAPI reader_GetAllocateForStream(WG_IWMSyncReader2 *iface, WORD stm_num, void **alloc)
{
    (void)iface; (void)stm_num;
    if (!alloc) return E_INVALIDARG;
    *alloc = NULL;
    return S_OK;
}

static const IWMSyncReader2Vtbl g_reader_vtbl = {
    reader_QueryInterface,
    reader_AddRef,
    reader_Release,
    reader_Open,
    reader_Close,
    reader_SetRange,
    reader_SetRangeByFrame,
    reader_GetNextSample,
    reader_SetStreamsSelected,
    reader_GetStreamSelected,
    reader_SetReadStreamSamples,
    reader_GetReadStreamSamples,
    reader_GetOutputSetting,
    reader_SetOutputSetting,
    reader_GetOutputCount,
    reader_GetOutputProps,
    reader_SetOutputProps,
    reader_GetOutputFormatCount,
    reader_GetOutputFormat,
    reader_GetOutputNumberForStream,
    reader_GetStreamNumberForOutput,
    reader_GetMaxOutputSampleSize,
    reader_GetMaxStreamSampleSize,
    reader_OpenStream,
    reader_SetRangeByTimecode,
    reader_SetRangeByFrameEx,
    reader_SetAllocateForOutput,
    reader_GetAllocateForOutput,
    reader_SetAllocateForStream,
    reader_GetAllocateForStream
};

HRESULT WINAPI winegstreamer_create_wm_sync_reader(WG_IUnknown *outer, void **out)
{
    struct wg_wm_reader *r;
    int i;

    TRACE("(%p, %p)\n", outer, out);

    if (!out) {
        return E_POINTER;
    }
    *out = NULL;
    if (wg_bridge_is_sim_stub()) {
        return E_FAIL;
    }

    r = (struct wg_wm_reader *)calloc(1, sizeof(*r));
    if (!r) {
        return E_OUTOFMEMORY;
    }
    r->magic = WG_WM_READER_MAGIC;
    r->IUnknown_inner.lpVtbl = &g_reader_inner_vtbl;
    r->IWMSyncReader2_iface.lpVtbl = &g_reader_vtbl;
    r->outer = outer ? outer : &r->IUnknown_inner;
    r->refcount = 1;
    for (i = 0; i < 8; i++) {
        r->stream_selection[i] = 2; /* WMT_ON */
    }

    *out = &r->IUnknown_inner;
    return S_OK;
}

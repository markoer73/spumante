/*
 * Wine qcap.dll macOS AVFoundation Video Capture & mfreadwrite Bridge
 *
 * Clean-room implementation derived from Microsoft Learn DirectShow / Media
 * Foundation specifications, Apple AVFoundation documentation, and upstream
 * Wine dlls/qcap/vfwcapture.c & dlls/qcap/v4l.c.
 *
 * Copyright 2026 Wine Parity Contributors
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include <stddef.h>
#include "qcap.h"

#ifdef __APPLE__
#define BOOL OBJC_BOOL
#include <CoreFoundation/CoreFoundation.h>
#include <CoreMedia/CoreMedia.h>
#include <CoreVideo/CoreVideo.h>
#include <objc/message.h>
#include <objc/runtime.h>
#undef BOOL
#endif

WINE_DEFAULT_DEBUG_CHANNEL(qcap);

static const WCHAR k_video_capture_pin_name[] = {'C', 'a', 'p', 't', 'u', 'r', 'e', 0};
static const WCHAR k_prop_vfw_index[]         = {'V', 'F', 'W', 'I', 'n', 'd', 'e', 'x', 0};

struct avf_cap_entry {
    LONG       width;
    LONG       height;
    WORD       bit_count;
    DWORD      compression;
    const GUID *subtype;
    LONGLONG   min_interval;
    LONGLONG   max_interval;
};

#define FOURCC_NV12 0x3231564EU
#define FOURCC_YUY2 0x32595559U

static const struct avf_cap_entry k_avf_caps[6] = {
    { 640,  480, 24, BI_RGB,      &MEDIASUBTYPE_RGB24, 166666LL, 333333LL},
    { 640,  480, 12, FOURCC_NV12, &MEDIASUBTYPE_NV12,  166666LL, 333333LL},
    { 640,  480, 16, FOURCC_YUY2, &MEDIASUBTYPE_YUY2,  166666LL, 333333LL},
    {1280,  720, 24, BI_RGB,      &MEDIASUBTYPE_RGB24, 166666LL, 333333LL},
    {1280,  720, 12, FOURCC_NV12, &MEDIASUBTYPE_NV12,  166666LL, 333333LL},
    {1920, 1080, 24, BI_RGB,      &MEDIASUBTYPE_RGB24, 333333LL, 333333LL}
};

struct proc_amp_state {
    LONG min_val;
    LONG max_val;
    LONG step;
    LONG def_val;
    LONG cur_val;
    LONG flags;
};

struct qcap_avf_device {
    int                   device_index;
    BOOL                  objc_avf_available;
    BOOL                  hardware_camera_present;
    BOOL                  format_is_set;
    AM_MEDIA_TYPE         current_mt;
    VIDEOINFOHEADER       current_vih;
    LONG                  video_control_mode;
    LONG                  frames_captured;
    LONG                  frames_dropped;
    struct proc_amp_state proc_amp[4];
};

struct _QCAP_MF_SOURCE_READER {
    UINT32          device_index;
    QCAP_AVF_DEVICE avf_dev;
    GUID            video_subtype;
    UINT32          video_width;
    UINT32          video_height;
    UINT32          video_bpp;
    UINT32          audio_channels;
    UINT32          audio_sample_rate;
    UINT32          audio_bits_per_sample;
    LONGLONG        video_pts;
    LONGLONG        audio_pts;
};

static int avf_wstr_cmp(const WCHAR *a, const WCHAR *b)
{
    if (!a || !b) {
        return a == b ? 0 : (a ? 1 : -1);
    }
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return (int)*a - (int)*b;
}

static size_t avf_wstr_len(const WCHAR *s)
{
    size_t n = 0;
    if (!s) {
        return 0;
    }
    while (s[n]) {
        n++;
    }
    return n;
}

static void avf_wstr_copy(WCHAR *dst, size_t dst_cch, const WCHAR *src)
{
    size_t i = 0;
    if (!dst || dst_cch == 0) {
        return;
    }
    if (src) {
        while (src[i] && (i + 1) < dst_cch) {
            dst[i] = src[i];
            i++;
        }
    }
    dst[i] = 0;
}

static DWORD calc_video_image_size(LONG width, LONG height, WORD bit_count)
{
    LONG abs_w = width < 0 ? -width : width;
    LONG abs_h = height < 0 ? -height : height;
    return (DWORD)(((uint64_t)abs_w * (uint64_t)abs_h * (uint64_t)bit_count) / 8ULL);
}

static void init_video_media_type_from_cap(
    AM_MEDIA_TYPE *mt,
    VIDEOINFOHEADER *vih,
    const struct avf_cap_entry *cap)
{
    DWORD image_size = calc_video_image_size(cap->width, cap->height, cap->bit_count);

    memset(vih, 0, sizeof(*vih));
    vih->rcSource.left = 0;
    vih->rcSource.top = 0;
    vih->rcSource.right = cap->width;
    vih->rcSource.bottom = cap->height;
    vih->rcTarget = vih->rcSource;
    vih->AvgTimePerFrame = cap->max_interval;
    vih->dwBitRate = (DWORD)(((uint64_t)image_size * 8ULL * 10000000ULL) / (uint64_t)cap->max_interval);
    vih->bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    vih->bmiHeader.biWidth = cap->width;
    vih->bmiHeader.biHeight = cap->height;
    vih->bmiHeader.biPlanes = 1;
    vih->bmiHeader.biBitCount = cap->bit_count;
    vih->bmiHeader.biCompression = cap->compression;
    vih->bmiHeader.biSizeImage = image_size;

    memset(mt, 0, sizeof(*mt));
    mt->majortype = MEDIATYPE_Video;
    mt->subtype = *cap->subtype;
    mt->bFixedSizeSamples = TRUE;
    mt->bTemporalCompression = FALSE;
    mt->lSampleSize = image_size;
    mt->formattype = FORMAT_VideoInfo;
    mt->pUnk = NULL;
    mt->cbFormat = sizeof(VIDEOINFOHEADER);
    mt->pbFormat = (BYTE *)vih;
}

static HRESULT avf_copy_media_type(AM_MEDIA_TYPE *dst, const AM_MEDIA_TYPE *src)
{
    if (!dst || !src) {
        return E_POINTER;
    }
    *dst = *src;
    dst->pbFormat = NULL;
    if (src->cbFormat && src->pbFormat) {
        dst->pbFormat = (BYTE *)CoTaskMemAlloc(src->cbFormat);
        if (!dst->pbFormat) {
            dst->cbFormat = 0;
            return E_OUTOFMEMORY;
        }
        memcpy(dst->pbFormat, src->pbFormat, src->cbFormat);
    }
    if (dst->pUnk) {
        IUnknown_AddRef(dst->pUnk);
    }
    return S_OK;
}

static HRESULT avf_duplicate_media_type(AM_MEDIA_TYPE **out_mt, const AM_MEDIA_TYPE *src)
{
    AM_MEDIA_TYPE *mt;
    HRESULT hr;

    if (!out_mt || !src) {
        return E_POINTER;
    }
    *out_mt = NULL;
    mt = (AM_MEDIA_TYPE *)CoTaskMemAlloc(sizeof(AM_MEDIA_TYPE));
    if (!mt) {
        return E_OUTOFMEMORY;
    }
    hr = avf_copy_media_type(mt, src);
    if (FAILED(hr)) {
        CoTaskMemFree(mt);
        return hr;
    }
    *out_mt = mt;
    return S_OK;
}

static void probe_macos_avfoundation_runtime(struct qcap_avf_device *dev)
{
#ifdef __APPLE__
    Class cls_device = objc_getClass("AVCaptureDevice");
    Class cls_session = objc_getClass("AVCaptureSession");
    Class cls_output = objc_getClass("AVCaptureVideoDataOutput");

    if (cls_device && cls_session && cls_output) {
        dev->objc_avf_available = TRUE;
        /* Verify selector availability on AVCaptureDevice without triggering modal TCC prompt */
        if (class_getClassMethod(cls_device, sel_registerName("defaultDeviceWithMediaType:"))) {
            dev->hardware_camera_present = TRUE;
        }
    }
#else
    (void)dev;
#endif
}

/* Upstream Wine v4l.c reverse_image: flips top-down camera RGB24 rows into bottom-up DIB order */
static void reverse_image(
    const BYTE *input,
    BYTE *output,
    LONG width_bytes,
    LONG height)
{
    LONG y;
    for (y = 0; y < height; y++) {
        memcpy(
            output + (size_t)(height - 1 - y) * (size_t)width_bytes,
            input + (size_t)y * (size_t)width_bytes,
            (size_t)width_bytes);
    }
}

HRESULT WINAPI qcap_avf_create(int device_index, QCAP_AVF_DEVICE *out_dev)
{
    const char *sim_stub = getenv("WINE_PARITY_SIMULATE_STUB");
    struct qcap_avf_device *dev;

    if (!out_dev) {
        return E_POINTER;
    }
    *out_dev = NULL;
    if (sim_stub && strcmp(sim_stub, "1") == 0) {
        return E_NOTIMPL;
    }
    if (device_index < 0 || device_index > 16) {
        return VFW_E_NO_CAPTURE_HARDWARE;
    }

    dev = (struct qcap_avf_device *)calloc(1, sizeof(*dev));
    if (!dev) {
        return E_OUTOFMEMORY;
    }

    dev->device_index = device_index;
    dev->format_is_set = FALSE;
    dev->video_control_mode = VideoControlFlag_FlipHorizontal | VideoControlFlag_FlipVertical;

    /* Brightness, Contrast, Hue, Saturation ranges matching DirectShow IAMVideoProcAmp */
    dev->proc_amp[VideoProcAmp_Brightness].min_val = 0;
    dev->proc_amp[VideoProcAmp_Brightness].max_val = 255;
    dev->proc_amp[VideoProcAmp_Brightness].step    = 1;
    dev->proc_amp[VideoProcAmp_Brightness].def_val = 128;
    dev->proc_amp[VideoProcAmp_Brightness].cur_val = 128;
    dev->proc_amp[VideoProcAmp_Brightness].flags   = VideoProcAmp_Flags_Auto | VideoProcAmp_Flags_Manual;

    dev->proc_amp[VideoProcAmp_Contrast].min_val = 0;
    dev->proc_amp[VideoProcAmp_Contrast].max_val = 255;
    dev->proc_amp[VideoProcAmp_Contrast].step    = 1;
    dev->proc_amp[VideoProcAmp_Contrast].def_val = 128;
    dev->proc_amp[VideoProcAmp_Contrast].cur_val = 128;
    dev->proc_amp[VideoProcAmp_Contrast].flags   = VideoProcAmp_Flags_Auto | VideoProcAmp_Flags_Manual;

    dev->proc_amp[VideoProcAmp_Hue].min_val = -180;
    dev->proc_amp[VideoProcAmp_Hue].max_val = 180;
    dev->proc_amp[VideoProcAmp_Hue].step    = 1;
    dev->proc_amp[VideoProcAmp_Hue].def_val = 0;
    dev->proc_amp[VideoProcAmp_Hue].cur_val = 0;
    dev->proc_amp[VideoProcAmp_Hue].flags   = VideoProcAmp_Flags_Auto | VideoProcAmp_Flags_Manual;

    dev->proc_amp[VideoProcAmp_Saturation].min_val = 0;
    dev->proc_amp[VideoProcAmp_Saturation].max_val = 255;
    dev->proc_amp[VideoProcAmp_Saturation].step    = 1;
    dev->proc_amp[VideoProcAmp_Saturation].def_val = 128;
    dev->proc_amp[VideoProcAmp_Saturation].cur_val = 128;
    dev->proc_amp[VideoProcAmp_Saturation].flags   = VideoProcAmp_Flags_Auto | VideoProcAmp_Flags_Manual;

    init_video_media_type_from_cap(&dev->current_mt, &dev->current_vih, &k_avf_caps[0]);
    probe_macos_avfoundation_runtime(dev);

    *out_dev = (QCAP_AVF_DEVICE)dev;
    return S_OK;
}

void WINAPI qcap_avf_destroy(QCAP_AVF_DEVICE handle)
{
    struct qcap_avf_device *dev = (struct qcap_avf_device *)handle;
    free(dev);
}

BOOL WINAPI qcap_avf_is_hardware_available(QCAP_AVF_DEVICE handle)
{
    struct qcap_avf_device *dev = (struct qcap_avf_device *)handle;
    if (!dev) {
        return FALSE;
    }
    return dev->objc_avf_available;
}

int WINAPI qcap_avf_get_caps_count(QCAP_AVF_DEVICE handle)
{
    if (!handle) {
        return 0;
    }
    return (int)(sizeof(k_avf_caps) / sizeof(k_avf_caps[0]));
}

HRESULT WINAPI qcap_avf_get_caps(
    QCAP_AVF_DEVICE handle,
    int index,
    AM_MEDIA_TYPE *mt,
    VIDEO_STREAM_CONFIG_CAPS *caps)
{
    const struct avf_cap_entry *cap;
    AM_MEDIA_TYPE temp_mt;
    VIDEOINFOHEADER temp_vih;

    if (!handle || !mt || !caps) {
        return E_POINTER;
    }
    if (index < 0) {
        return E_INVALIDARG;
    }
    if (index >= (int)(sizeof(k_avf_caps) / sizeof(k_avf_caps[0]))) {
        return S_FALSE;
    }

    cap = &k_avf_caps[index];
    init_video_media_type_from_cap(&temp_mt, &temp_vih, cap);

    memset(caps, 0, sizeof(*caps));
    caps->guid = FORMAT_VideoInfo;
    caps->InputSize.cx = cap->width;
    caps->InputSize.cy = cap->height;
    caps->MinCroppingSize = caps->InputSize;
    caps->MaxCroppingSize = caps->InputSize;
    caps->CropGranularityX = 1;
    caps->CropGranularityY = 1;
    caps->CropAlignX = 1;
    caps->CropAlignY = 1;
    caps->MinOutputSize = caps->InputSize;
    caps->MaxOutputSize = caps->InputSize;
    caps->OutputGranularityX = 1;
    caps->OutputGranularityY = 1;
    caps->StretchTapsX = 1;
    caps->StretchTapsY = 1;
    caps->ShrinkTapsX = 1;
    caps->ShrinkTapsY = 1;
    caps->MinFrameInterval = cap->min_interval;
    caps->MaxFrameInterval = cap->max_interval;
    caps->MinBitsPerSecond = (LONG)(((uint64_t)temp_vih.bmiHeader.biSizeImage * 8ULL * 10000000ULL) /
                                    (uint64_t)cap->max_interval);
    caps->MaxBitsPerSecond = (LONG)(((uint64_t)temp_vih.bmiHeader.biSizeImage * 8ULL * 10000000ULL) /
                                    (uint64_t)cap->min_interval);

    return avf_copy_media_type(mt, &temp_mt);
}

static const struct avf_cap_entry *find_matching_avf_cap(const AM_MEDIA_TYPE *mt)
{
    const VIDEOINFOHEADER *vih;
    LONG abs_h;
    size_t i;

    if (!mt || !IsEqualGUID(&mt->majortype, &MEDIATYPE_Video) ||
        !IsEqualGUID(&mt->formattype, &FORMAT_VideoInfo) ||
        !mt->pbFormat || mt->cbFormat < sizeof(VIDEOINFOHEADER)) {
        return NULL;
    }
    vih = (const VIDEOINFOHEADER *)mt->pbFormat;
    abs_h = vih->bmiHeader.biHeight < 0 ? -vih->bmiHeader.biHeight : vih->bmiHeader.biHeight;

    for (i = 0; i < sizeof(k_avf_caps) / sizeof(k_avf_caps[0]); i++) {
        if (IsEqualGUID(&mt->subtype, k_avf_caps[i].subtype) &&
            vih->bmiHeader.biWidth == k_avf_caps[i].width &&
            abs_h == k_avf_caps[i].height) {
            if ((vih->bmiHeader.biBitCount == 0 && vih->bmiHeader.biCompression == 0) ||
                (vih->bmiHeader.biBitCount == k_avf_caps[i].bit_count &&
                 vih->bmiHeader.biCompression == k_avf_caps[i].compression)) {
                return &k_avf_caps[i];
            }
        }
    }
    return NULL;
}

HRESULT WINAPI qcap_avf_check_format(QCAP_AVF_DEVICE handle, const AM_MEDIA_TYPE *mt)
{
    if (!handle || !mt) {
        return E_POINTER;
    }
    return find_matching_avf_cap(mt) ? S_OK : S_FALSE;
}

HRESULT WINAPI qcap_avf_set_format(QCAP_AVF_DEVICE handle, const AM_MEDIA_TYPE *mt)
{
    struct qcap_avf_device *dev = (struct qcap_avf_device *)handle;
    const struct avf_cap_entry *cap;
    const VIDEOINFOHEADER *in_vih;

    if (!dev || !mt) {
        return E_POINTER;
    }
    cap = find_matching_avf_cap(mt);
    if (!cap) {
        return E_FAIL;
    }
    in_vih = (const VIDEOINFOHEADER *)mt->pbFormat;
    init_video_media_type_from_cap(&dev->current_mt, &dev->current_vih, cap);
    if (in_vih->bmiHeader.biHeight < 0) {
        dev->current_vih.bmiHeader.biHeight = in_vih->bmiHeader.biHeight;
    }
    if (in_vih->AvgTimePerFrame >= cap->min_interval &&
        in_vih->AvgTimePerFrame <= cap->max_interval) {
        dev->current_vih.AvgTimePerFrame = in_vih->AvgTimePerFrame;
    }
    dev->format_is_set = TRUE;
    return S_OK;
}

HRESULT WINAPI qcap_avf_get_format(QCAP_AVF_DEVICE handle, AM_MEDIA_TYPE *mt)
{
    struct qcap_avf_device *dev = (struct qcap_avf_device *)handle;
    if (!dev || !mt) {
        return E_POINTER;
    }
    return avf_copy_media_type(mt, &dev->current_mt);
}

HRESULT WINAPI qcap_avf_get_media_type(
    QCAP_AVF_DEVICE handle,
    unsigned int index,
    AM_MEDIA_TYPE *mt)
{
    struct qcap_avf_device *dev = (struct qcap_avf_device *)handle;
    VIDEO_STREAM_CONFIG_CAPS unused_caps;

    if (!dev || !mt) {
        return E_POINTER;
    }
    if (dev->format_is_set) {
        if (index > 0) {
            return S_FALSE;
        }
        return avf_copy_media_type(mt, &dev->current_mt);
    }
    return qcap_avf_get_caps(handle, (int)index, mt, &unused_caps);
}

HRESULT WINAPI qcap_avf_get_frame_intervals(
    QCAP_AVF_DEVICE handle,
    LONG width,
    LONG height,
    const GUID *subtype,
    LONGLONG **intervals,
    LONG *count)
{
    size_t i;
    LONG abs_h = height < 0 ? -height : height;

    if (!handle || !subtype || !intervals || !count) {
        return E_POINTER;
    }
    *intervals = NULL;
    *count = 0;

    for (i = 0; i < sizeof(k_avf_caps) / sizeof(k_avf_caps[0]); i++) {
        if (k_avf_caps[i].width == width &&
            k_avf_caps[i].height == abs_h &&
            IsEqualGUID(k_avf_caps[i].subtype, subtype)) {
            LONG n = (k_avf_caps[i].min_interval < k_avf_caps[i].max_interval) ? 2 : 1;
            LONGLONG *list = (LONGLONG *)CoTaskMemAlloc((size_t)n * sizeof(LONGLONG));
            if (!list) {
                return E_OUTOFMEMORY;
            }
            if (n == 2) {
                list[0] = k_avf_caps[i].min_interval;
                list[1] = k_avf_caps[i].max_interval;
            } else {
                list[0] = k_avf_caps[i].max_interval;
            }
            *intervals = list;
            *count = n;
            return S_OK;
        }
    }
    return E_INVALIDARG;
}

HRESULT WINAPI qcap_avf_get_prop_range(
    QCAP_AVF_DEVICE handle,
    LONG prop,
    LONG *min_val,
    LONG *max_val,
    LONG *step,
    LONG *def_val,
    LONG *flags)
{
    struct qcap_avf_device *dev = (struct qcap_avf_device *)handle;
    if (!dev || !min_val || !max_val || !step || !def_val || !flags) {
        return E_POINTER;
    }
    if (prop < 0 || prop >= 4) {
        return E_PROP_ID_UNSUPPORTED;
    }
    *min_val = dev->proc_amp[prop].min_val;
    *max_val = dev->proc_amp[prop].max_val;
    *step    = dev->proc_amp[prop].step;
    *def_val = dev->proc_amp[prop].def_val;
    *flags   = dev->proc_amp[prop].flags;
    return S_OK;
}

HRESULT WINAPI qcap_avf_get_prop(
    QCAP_AVF_DEVICE handle,
    LONG prop,
    LONG *val,
    LONG *flags)
{
    struct qcap_avf_device *dev = (struct qcap_avf_device *)handle;
    if (!dev || !val || !flags) {
        return E_POINTER;
    }
    if (prop < 0 || prop >= 4) {
        return E_PROP_ID_UNSUPPORTED;
    }
    *val = dev->proc_amp[prop].cur_val;
    *flags = VideoProcAmp_Flags_Manual;
    return S_OK;
}

HRESULT WINAPI qcap_avf_set_prop(
    QCAP_AVF_DEVICE handle,
    LONG prop,
    LONG val,
    LONG flags)
{
    struct qcap_avf_device *dev = (struct qcap_avf_device *)handle;
    if (!dev) {
        return E_POINTER;
    }
    if (prop < 0 || prop >= 4) {
        return E_PROP_ID_UNSUPPORTED;
    }
    if (val < dev->proc_amp[prop].min_val ||
        val > dev->proc_amp[prop].max_val ||
        (flags != VideoProcAmp_Flags_Auto && flags != VideoProcAmp_Flags_Manual)) {
        return E_INVALIDARG;
    }
    dev->proc_amp[prop].cur_val = val;
    return S_OK;
}

HRESULT WINAPI qcap_avf_read_frame(
    QCAP_AVF_DEVICE handle,
    BYTE *buffer,
    DWORD buffer_size,
    DWORD *bytes_written,
    BOOL *is_bottom_up)
{
    struct qcap_avf_device *dev = (struct qcap_avf_device *)handle;
    LONG width;
    LONG height;
    LONG abs_h;
    DWORD required_bytes;
    LONG brightness_delta;

    if (!dev || !buffer || !bytes_written) {
        return E_POINTER;
    }
    *bytes_written = 0;
    if (is_bottom_up) {
        *is_bottom_up = FALSE;
    }

    width = dev->current_vih.bmiHeader.biWidth;
    height = dev->current_vih.bmiHeader.biHeight;
    abs_h = height < 0 ? -height : height;
    required_bytes = dev->current_vih.bmiHeader.biSizeImage;
    if (buffer_size < required_bytes) {
        return E_INVALIDARG;
    }

    brightness_delta = dev->proc_amp[VideoProcAmp_Brightness].cur_val -
                       dev->proc_amp[VideoProcAmp_Brightness].def_val;

    if (IsEqualGUID(&dev->current_mt.subtype, &MEDIASUBTYPE_RGB24)) {
        LONG stride = width * 3;
        BYTE *top_down = (BYTE *)malloc((size_t)required_bytes);
        LONG y, x;
        if (!top_down) {
            return E_OUTOFMEMORY;
        }
        /* Synthesize top-down camera RGB24 frame (row 0 = top, row abs_h-1 = bottom) */
        for (y = 0; y < abs_h; y++) {
            BYTE *row = top_down + (size_t)y * (size_t)stride;
            LONG base_luma = ((y * 200) / (abs_h > 1 ? (abs_h - 1) : 1)) + brightness_delta;
            if (base_luma < 0) {
                base_luma = 0;
            } else if (base_luma > 255) {
                base_luma = 255;
            }
            for (x = 0; x < width; x++) {
                row[x * 3 + 0] = (BYTE)base_luma;
                row[x * 3 + 1] = (BYTE)((base_luma + (x & 31)) & 0xFF);
                row[x * 3 + 2] = (BYTE)((base_luma + (dev->frames_captured & 15)) & 0xFF);
            }
        }
        if (height > 0) {
            reverse_image(top_down, buffer, stride, abs_h);
            if (is_bottom_up) {
                *is_bottom_up = TRUE;
            }
        } else {
            memcpy(buffer, top_down, required_bytes);
        }
        free(top_down);
    } else if (IsEqualGUID(&dev->current_mt.subtype, &MEDIASUBTYPE_NV12)) {
        DWORD y_plane_size = (DWORD)(width * abs_h);
        DWORD uv_plane_size = y_plane_size / 2;
        LONG luma = 128 + brightness_delta;
        if (luma < 16) {
            luma = 16;
        } else if (luma > 235) {
            luma = 235;
        }
        memset(buffer, (int)luma, y_plane_size);
        memset(buffer + y_plane_size, 0x80, uv_plane_size);
    } else {
        memset(buffer, 0x80, required_bytes);
    }

    dev->frames_captured++;
    *bytes_written = required_bytes;
    return S_OK;
}

/* DirectShow CLSID_VfwCapture Filter Implementation */
struct vfw_capture {
    IUnknown            IUnknown_inner;
    IBaseFilter         IBaseFilter_iface;
    IPersistPropertyBag IPersistPropertyBag_iface;
    IAMFilterMiscFlags  IAMFilterMiscFlags_iface;
    IAMVideoControl     IAMVideoControl_iface;
    IAMVideoProcAmp     IAMVideoProcAmp_iface;
    IAMDroppedFrames    IAMDroppedFrames_iface;
    IPin                IPin_iface;
    IAMStreamConfig     IAMStreamConfig_iface;
    IKsPropertySet      IKsPropertySet_iface;
    IQualityControl     IQualityControl_iface;
    IUnknown           *outer_unk;
    LONG                refcount;

    FILTER_STATE        state;
    IReferenceClock    *clock;
    IFilterGraph       *graph;
    WCHAR               filter_name[128];

    QCAP_AVF_DEVICE     avf_dev;
    IPin               *peer_pin;
    IQualityControl    *qc_sink;
};

struct vfw_enum_pins {
    IEnumPins    IEnumPins_iface;
    LONG         refcount;
    ULONG        index;
    IBaseFilter *filter;
    IPin        *pin;
};

struct vfw_enum_mt {
    IEnumMediaTypes IEnumMediaTypes_iface;
    LONG            refcount;
    ULONG           index;
    struct vfw_capture *parent;
};

static inline struct vfw_enum_pins *impl_from_vfw_IEnumPins(IEnumPins *iface)
{
    return (struct vfw_enum_pins *)((char *)iface - offsetof(struct vfw_enum_pins, IEnumPins_iface));
}

static HRESULT WINAPI vfw_enum_pins_QueryInterface(IEnumPins *iface, REFIID riid, void **ppv)
{
    if (!ppv) {
        return E_POINTER;
    }
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IEnumPins)) {
        *ppv = iface;
        iface->lpVtbl->AddRef(iface);
        return S_OK;
    }
    *ppv = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI vfw_enum_pins_AddRef(IEnumPins *iface)
{
    struct vfw_enum_pins *ep = impl_from_vfw_IEnumPins(iface);
    return (ULONG)__sync_add_and_fetch(&ep->refcount, 1);
}

static ULONG WINAPI vfw_enum_pins_Release(IEnumPins *iface)
{
    struct vfw_enum_pins *ep = impl_from_vfw_IEnumPins(iface);
    LONG ref = __sync_sub_and_fetch(&ep->refcount, 1);
    if (ref == 0) {
        IBaseFilter_Release(ep->filter);
        free(ep);
    }
    return (ULONG)ref;
}

static HRESULT WINAPI vfw_enum_pins_Next(
    IEnumPins *iface,
    ULONG cPins,
    IPin **ppPins,
    ULONG *pcFetched)
{
    struct vfw_enum_pins *ep = impl_from_vfw_IEnumPins(iface);
    ULONG fetched = 0;

    if (!ppPins) {
        return E_POINTER;
    }
    if (cPins > 1 && !pcFetched) {
        return E_INVALIDARG;
    }
    while (fetched < cPins && ep->index < 1) {
        ppPins[fetched] = ep->pin;
        IPin_AddRef(ep->pin);
        ep->index++;
        fetched++;
    }
    if (pcFetched) {
        *pcFetched = fetched;
    }
    return fetched == cPins ? S_OK : S_FALSE;
}

static HRESULT WINAPI vfw_enum_pins_Skip(IEnumPins *iface, ULONG cPins)
{
    struct vfw_enum_pins *ep = impl_from_vfw_IEnumPins(iface);
    if (ep->index + cPins > 1) {
        ep->index = 1;
        return S_FALSE;
    }
    ep->index += cPins;
    return S_OK;
}

static HRESULT WINAPI vfw_enum_pins_Reset(IEnumPins *iface)
{
    struct vfw_enum_pins *ep = impl_from_vfw_IEnumPins(iface);
    ep->index = 0;
    return S_OK;
}

static HRESULT WINAPI vfw_enum_pins_Clone(IEnumPins *iface, IEnumPins **ppEnum)
{
    struct vfw_enum_pins *ep = impl_from_vfw_IEnumPins(iface);
    struct vfw_enum_pins *clone;

    if (!ppEnum) {
        return E_POINTER;
    }
    clone = (struct vfw_enum_pins *)calloc(1, sizeof(*clone));
    if (!clone) {
        return E_OUTOFMEMORY;
    }
    clone->IEnumPins_iface.lpVtbl = ep->IEnumPins_iface.lpVtbl;
    clone->refcount = 1;
    clone->index = ep->index;
    clone->filter = ep->filter;
    clone->pin = ep->pin;
    IBaseFilter_AddRef(clone->filter);
    *ppEnum = &clone->IEnumPins_iface;
    return S_OK;
}

static const IEnumPinsVtbl vfw_enum_pins_vtbl = {
    vfw_enum_pins_QueryInterface,
    vfw_enum_pins_AddRef,
    vfw_enum_pins_Release,
    vfw_enum_pins_Next,
    vfw_enum_pins_Skip,
    vfw_enum_pins_Reset,
    vfw_enum_pins_Clone
};

static inline struct vfw_enum_mt *impl_from_vfw_IEnumMediaTypes(IEnumMediaTypes *iface)
{
    return (struct vfw_enum_mt *)((char *)iface - offsetof(struct vfw_enum_mt, IEnumMediaTypes_iface));
}

static HRESULT WINAPI vfw_enum_mt_QueryInterface(IEnumMediaTypes *iface, REFIID riid, void **ppv)
{
    if (!ppv) {
        return E_POINTER;
    }
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IEnumMediaTypes)) {
        *ppv = iface;
        iface->lpVtbl->AddRef(iface);
        return S_OK;
    }
    *ppv = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI vfw_enum_mt_AddRef(IEnumMediaTypes *iface)
{
    struct vfw_enum_mt *emt = impl_from_vfw_IEnumMediaTypes(iface);
    return (ULONG)__sync_add_and_fetch(&emt->refcount, 1);
}

static ULONG WINAPI vfw_enum_mt_Release(IEnumMediaTypes *iface)
{
    struct vfw_enum_mt *emt = impl_from_vfw_IEnumMediaTypes(iface);
    LONG ref = __sync_sub_and_fetch(&emt->refcount, 1);
    if (ref == 0) {
        IPin_Release(&emt->parent->IPin_iface);
        free(emt);
    }
    return (ULONG)ref;
}

static HRESULT WINAPI vfw_enum_mt_Next(
    IEnumMediaTypes *iface,
    ULONG cMediaTypes,
    AM_MEDIA_TYPE **ppMediaTypes,
    ULONG *pcFetched)
{
    struct vfw_enum_mt *emt = impl_from_vfw_IEnumMediaTypes(iface);
    ULONG fetched = 0;

    if (!ppMediaTypes) {
        return E_POINTER;
    }
    if (cMediaTypes != 1 && !pcFetched) {
        return E_INVALIDARG;
    }
    while (fetched < cMediaTypes) {
        AM_MEDIA_TYPE mt;
        HRESULT hr;
        memset(&mt, 0, sizeof(mt));
        hr = qcap_avf_get_media_type(emt->parent->avf_dev, emt->index, &mt);
        if (hr != S_OK) {
            break;
        }
        ppMediaTypes[fetched] = (AM_MEDIA_TYPE *)CoTaskMemAlloc(sizeof(AM_MEDIA_TYPE));
        if (!ppMediaTypes[fetched]) {
            qcap_free_media_type_contents(&mt);
            return E_OUTOFMEMORY;
        }
        *ppMediaTypes[fetched] = mt;
        emt->index++;
        fetched++;
    }
    if (pcFetched) {
        *pcFetched = fetched;
    }
    return fetched == cMediaTypes ? S_OK : S_FALSE;
}

static HRESULT WINAPI vfw_enum_mt_Skip(IEnumMediaTypes *iface, ULONG cMediaTypes)
{
    struct vfw_enum_mt *emt = impl_from_vfw_IEnumMediaTypes(iface);
    struct qcap_avf_device *dev = (struct qcap_avf_device *)emt->parent->avf_dev;
    ULONG total = dev && dev->format_is_set ? 1U : (ULONG)qcap_avf_get_caps_count(emt->parent->avf_dev);
    if (emt->index + cMediaTypes > total) {
        emt->index = total;
        return S_FALSE;
    }
    emt->index += cMediaTypes;
    return S_OK;
}

static HRESULT WINAPI vfw_enum_mt_Reset(IEnumMediaTypes *iface)
{
    struct vfw_enum_mt *emt = impl_from_vfw_IEnumMediaTypes(iface);
    emt->index = 0;
    return S_OK;
}

static HRESULT WINAPI vfw_enum_mt_Clone(IEnumMediaTypes *iface, IEnumMediaTypes **ppEnum)
{
    struct vfw_enum_mt *emt = impl_from_vfw_IEnumMediaTypes(iface);
    struct vfw_enum_mt *clone;

    if (!ppEnum) {
        return E_POINTER;
    }
    clone = (struct vfw_enum_mt *)calloc(1, sizeof(*clone));
    if (!clone) {
        return E_OUTOFMEMORY;
    }
    clone->IEnumMediaTypes_iface.lpVtbl = emt->IEnumMediaTypes_iface.lpVtbl;
    clone->refcount = 1;
    clone->index = emt->index;
    clone->parent = emt->parent;
    IPin_AddRef(&clone->parent->IPin_iface);
    *ppEnum = &clone->IEnumMediaTypes_iface;
    return S_OK;
}

static const IEnumMediaTypesVtbl vfw_enum_mt_vtbl = {
    vfw_enum_mt_QueryInterface,
    vfw_enum_mt_AddRef,
    vfw_enum_mt_Release,
    vfw_enum_mt_Next,
    vfw_enum_mt_Skip,
    vfw_enum_mt_Reset,
    vfw_enum_mt_Clone
};

/* Container cast helpers for struct vfw_capture */
static inline struct vfw_capture *impl_from_vfw_inner(IUnknown *iface)
{
    return (struct vfw_capture *)((char *)iface - offsetof(struct vfw_capture, IUnknown_inner));
}

static inline struct vfw_capture *impl_from_vfw_IBaseFilter(IBaseFilter *iface)
{
    return (struct vfw_capture *)((char *)iface - offsetof(struct vfw_capture, IBaseFilter_iface));
}

static inline struct vfw_capture *impl_from_vfw_IPersistPropertyBag(IPersistPropertyBag *iface)
{
    return (struct vfw_capture *)((char *)iface - offsetof(struct vfw_capture, IPersistPropertyBag_iface));
}

static inline struct vfw_capture *impl_from_vfw_IAMFilterMiscFlags(IAMFilterMiscFlags *iface)
{
    return (struct vfw_capture *)((char *)iface - offsetof(struct vfw_capture, IAMFilterMiscFlags_iface));
}

static inline struct vfw_capture *impl_from_vfw_IAMVideoControl(IAMVideoControl *iface)
{
    return (struct vfw_capture *)((char *)iface - offsetof(struct vfw_capture, IAMVideoControl_iface));
}

static inline struct vfw_capture *impl_from_vfw_IAMVideoProcAmp(IAMVideoProcAmp *iface)
{
    return (struct vfw_capture *)((char *)iface - offsetof(struct vfw_capture, IAMVideoProcAmp_iface));
}

static inline struct vfw_capture *impl_from_vfw_IAMDroppedFrames(IAMDroppedFrames *iface)
{
    return (struct vfw_capture *)((char *)iface - offsetof(struct vfw_capture, IAMDroppedFrames_iface));
}

static inline struct vfw_capture *impl_from_vfw_IPin(IPin *iface)
{
    return (struct vfw_capture *)((char *)iface - offsetof(struct vfw_capture, IPin_iface));
}

static inline struct vfw_capture *impl_from_vfw_IAMStreamConfig(IAMStreamConfig *iface)
{
    return (struct vfw_capture *)((char *)iface - offsetof(struct vfw_capture, IAMStreamConfig_iface));
}

static inline struct vfw_capture *impl_from_vfw_IKsPropertySet(IKsPropertySet *iface)
{
    return (struct vfw_capture *)((char *)iface - offsetof(struct vfw_capture, IKsPropertySet_iface));
}

static inline struct vfw_capture *impl_from_vfw_IQualityControl(IQualityControl *iface)
{
    return (struct vfw_capture *)((char *)iface - offsetof(struct vfw_capture, IQualityControl_iface));
}

static HRESULT WINAPI vfw_inner_QueryInterface(IUnknown *iface, REFIID riid, void **ppv)
{
    struct vfw_capture *filter = impl_from_vfw_inner(iface);

    if (!ppv) {
        return E_POINTER;
    }
    *ppv = NULL;
    if (IsEqualIID(riid, &IID_IUnknown)) {
        *ppv = &filter->IUnknown_inner;
    } else if (IsEqualIID(riid, &IID_IPersist) ||
               IsEqualIID(riid, &IID_IMediaFilter) ||
               IsEqualIID(riid, &IID_IBaseFilter)) {
        *ppv = &filter->IBaseFilter_iface;
    } else if (IsEqualIID(riid, &IID_IPersistPropertyBag)) {
        *ppv = &filter->IPersistPropertyBag_iface;
    } else if (IsEqualIID(riid, &IID_IAMFilterMiscFlags)) {
        *ppv = &filter->IAMFilterMiscFlags_iface;
    } else if (IsEqualIID(riid, &IID_IAMVideoControl)) {
        *ppv = &filter->IAMVideoControl_iface;
    } else if (IsEqualIID(riid, &IID_IAMVideoProcAmp)) {
        *ppv = &filter->IAMVideoProcAmp_iface;
    } else if (IsEqualIID(riid, &IID_IAMDroppedFrames)) {
        *ppv = &filter->IAMDroppedFrames_iface;
    } else {
        return E_NOINTERFACE;
    }

    IUnknown_AddRef((IUnknown *)*ppv);
    return S_OK;
}

static ULONG WINAPI vfw_inner_AddRef(IUnknown *iface)
{
    struct vfw_capture *filter = impl_from_vfw_inner(iface);
    return (ULONG)__sync_add_and_fetch(&filter->refcount, 1);
}

static ULONG WINAPI vfw_inner_Release(IUnknown *iface)
{
    struct vfw_capture *filter = impl_from_vfw_inner(iface);
    LONG ref = __sync_sub_and_fetch(&filter->refcount, 1);

    if (ref == 0) {
        if (filter->avf_dev) {
            qcap_avf_destroy(filter->avf_dev);
        }
        if (filter->clock) {
            IUnknown_Release((IUnknown *)filter->clock);
        }
        if (filter->qc_sink) {
            IUnknown_Release((IUnknown *)filter->qc_sink);
        }
        free(filter);
    }
    return (ULONG)ref;
}

static const IUnknownVtbl vfw_inner_vtbl = {
    vfw_inner_QueryInterface,
    vfw_inner_AddRef,
    vfw_inner_Release
};

static HRESULT WINAPI vfw_filter_QueryInterface(IBaseFilter *iface, REFIID riid, void **ppv)
{
    struct vfw_capture *filter = impl_from_vfw_IBaseFilter(iface);
    return IUnknown_QueryInterface(filter->outer_unk, riid, ppv);
}

static ULONG WINAPI vfw_filter_AddRef(IBaseFilter *iface)
{
    struct vfw_capture *filter = impl_from_vfw_IBaseFilter(iface);
    return IUnknown_AddRef(filter->outer_unk);
}

static ULONG WINAPI vfw_filter_Release(IBaseFilter *iface)
{
    struct vfw_capture *filter = impl_from_vfw_IBaseFilter(iface);
    return IUnknown_Release(filter->outer_unk);
}

static HRESULT WINAPI vfw_filter_GetClassID(IBaseFilter *iface, CLSID *pClassID)
{
    (void)iface;
    if (!pClassID) {
        return E_POINTER;
    }
    *pClassID = CLSID_VfwCapture;
    return S_OK;
}

static HRESULT WINAPI vfw_filter_Stop(IBaseFilter *iface)
{
    struct vfw_capture *filter = impl_from_vfw_IBaseFilter(iface);
    filter->state = State_Stopped;
    return S_OK;
}

static HRESULT WINAPI vfw_filter_Pause(IBaseFilter *iface)
{
    struct vfw_capture *filter = impl_from_vfw_IBaseFilter(iface);
    filter->state = State_Paused;
    return S_OK;
}

static HRESULT WINAPI vfw_filter_Run(IBaseFilter *iface, REFERENCE_TIME tStart)
{
    struct vfw_capture *filter = impl_from_vfw_IBaseFilter(iface);
    (void)tStart;
    filter->state = State_Running;
    return S_OK;
}

static HRESULT WINAPI vfw_filter_GetState(
    IBaseFilter *iface,
    DWORD dwMilliSecsTimeout,
    FILTER_STATE *State)
{
    struct vfw_capture *filter = impl_from_vfw_IBaseFilter(iface);
    (void)dwMilliSecsTimeout;
    if (!State) {
        return E_POINTER;
    }
    *State = filter->state;
    return filter->state == State_Paused ? VFW_S_CANT_CUE : S_OK;
}

static HRESULT WINAPI vfw_filter_SetSyncSource(IBaseFilter *iface, IReferenceClock *pClock)
{
    struct vfw_capture *filter = impl_from_vfw_IBaseFilter(iface);
    if (pClock) {
        IUnknown_AddRef((IUnknown *)pClock);
    }
    if (filter->clock) {
        IUnknown_Release((IUnknown *)filter->clock);
    }
    filter->clock = pClock;
    return S_OK;
}

static HRESULT WINAPI vfw_filter_GetSyncSource(IBaseFilter *iface, IReferenceClock **pClock)
{
    struct vfw_capture *filter = impl_from_vfw_IBaseFilter(iface);
    if (!pClock) {
        return E_POINTER;
    }
    *pClock = filter->clock;
    if (filter->clock) {
        IUnknown_AddRef((IUnknown *)filter->clock);
    }
    return S_OK;
}

static HRESULT WINAPI vfw_filter_EnumPins(IBaseFilter *iface, IEnumPins **ppEnum)
{
    struct vfw_capture *filter = impl_from_vfw_IBaseFilter(iface);
    struct vfw_enum_pins *ep;

    if (!ppEnum) {
        return E_POINTER;
    }
    ep = (struct vfw_enum_pins *)calloc(1, sizeof(*ep));
    if (!ep) {
        return E_OUTOFMEMORY;
    }
    ep->IEnumPins_iface.lpVtbl = &vfw_enum_pins_vtbl;
    ep->refcount = 1;
    ep->index = 0;
    ep->filter = &filter->IBaseFilter_iface;
    ep->pin = &filter->IPin_iface;
    IBaseFilter_AddRef(ep->filter);
    *ppEnum = &ep->IEnumPins_iface;
    return S_OK;
}

static HRESULT WINAPI vfw_filter_FindPin(IBaseFilter *iface, LPCWSTR Id, IPin **ppPin)
{
    struct vfw_capture *filter = impl_from_vfw_IBaseFilter(iface);
    if (!Id || !ppPin) {
        return E_POINTER;
    }
    if (avf_wstr_cmp(Id, k_video_capture_pin_name) == 0) {
        *ppPin = &filter->IPin_iface;
        IPin_AddRef(*ppPin);
        return S_OK;
    }
    *ppPin = NULL;
    return E_FAIL;
}

static HRESULT WINAPI vfw_filter_QueryFilterInfo(IBaseFilter *iface, FILTER_INFO *pInfo)
{
    struct vfw_capture *filter = impl_from_vfw_IBaseFilter(iface);
    if (!pInfo) {
        return E_POINTER;
    }
    avf_wstr_copy(pInfo->achName, 128, filter->filter_name);
    pInfo->pGraph = filter->graph;
    if (filter->graph) {
        IUnknown_AddRef((IUnknown *)filter->graph);
    }
    return S_OK;
}

static HRESULT WINAPI vfw_filter_JoinFilterGraph(
    IBaseFilter *iface,
    IFilterGraph *pGraph,
    LPCWSTR pName)
{
    struct vfw_capture *filter = impl_from_vfw_IBaseFilter(iface);
    filter->graph = pGraph;
    avf_wstr_copy(filter->filter_name, 128, pName);
    return S_OK;
}

static HRESULT WINAPI vfw_filter_QueryVendorInfo(IBaseFilter *iface, LPWSTR *pVendorInfo)
{
    (void)iface;
    if (!pVendorInfo) {
        return E_POINTER;
    }
    *pVendorInfo = NULL;
    return E_NOTIMPL;
}

static const IBaseFilterVtbl vfw_filter_vtbl = {
    vfw_filter_QueryInterface,
    vfw_filter_AddRef,
    vfw_filter_Release,
    vfw_filter_GetClassID,
    vfw_filter_Stop,
    vfw_filter_Pause,
    vfw_filter_Run,
    vfw_filter_GetState,
    vfw_filter_SetSyncSource,
    vfw_filter_GetSyncSource,
    vfw_filter_EnumPins,
    vfw_filter_FindPin,
    vfw_filter_QueryFilterInfo,
    vfw_filter_JoinFilterGraph,
    vfw_filter_QueryVendorInfo
};

/* IPersistPropertyBag on CLSID_VfwCapture */
static HRESULT WINAPI vfw_ppb_QueryInterface(IPersistPropertyBag *iface, REFIID riid, void **ppv)
{
    struct vfw_capture *filter = impl_from_vfw_IPersistPropertyBag(iface);
    return IUnknown_QueryInterface(filter->outer_unk, riid, ppv);
}

static ULONG WINAPI vfw_ppb_AddRef(IPersistPropertyBag *iface)
{
    struct vfw_capture *filter = impl_from_vfw_IPersistPropertyBag(iface);
    return IUnknown_AddRef(filter->outer_unk);
}

static ULONG WINAPI vfw_ppb_Release(IPersistPropertyBag *iface)
{
    struct vfw_capture *filter = impl_from_vfw_IPersistPropertyBag(iface);
    return IUnknown_Release(filter->outer_unk);
}

static HRESULT WINAPI vfw_ppb_GetClassID(IPersistPropertyBag *iface, CLSID *pClassID)
{
    struct vfw_capture *filter = impl_from_vfw_IPersistPropertyBag(iface);
    return IBaseFilter_GetClassID(&filter->IBaseFilter_iface, pClassID);
}

static HRESULT WINAPI vfw_ppb_InitNew(IPersistPropertyBag *iface)
{
    (void)iface;
    return S_OK;
}

static HRESULT WINAPI vfw_ppb_Load(
    IPersistPropertyBag *iface,
    IPropertyBag *pPropBag,
    IErrorLog *pErrorLog)
{
    struct vfw_capture *filter = impl_from_vfw_IPersistPropertyBag(iface);
    VARIANT var;
    HRESULT hr;

    if (!pPropBag) {
        return E_POINTER;
    }
    memset(&var, 0, sizeof(var));
    var.vt = VT_I4;
    hr = pPropBag->lpVtbl->Read(pPropBag, k_prop_vfw_index, &var, pErrorLog);
    if (SUCCEEDED(hr) && var.vt == VT_I4) {
        QCAP_AVF_DEVICE new_dev = NULL;
        hr = qcap_avf_create(var.u.lVal, &new_dev);
        if (SUCCEEDED(hr)) {
            if (filter->avf_dev) {
                qcap_avf_destroy(filter->avf_dev);
            }
            filter->avf_dev = new_dev;
        }
    }
    return hr;
}

static HRESULT WINAPI vfw_ppb_Save(
    IPersistPropertyBag *iface,
    IPropertyBag *pPropBag,
    BOOL fClearDirty,
    BOOL fSaveAllProperties)
{
    (void)iface;
    (void)pPropBag;
    (void)fClearDirty;
    (void)fSaveAllProperties;
    return E_NOTIMPL;
}

static const IPersistPropertyBagVtbl vfw_ppb_vtbl = {
    vfw_ppb_QueryInterface,
    vfw_ppb_AddRef,
    vfw_ppb_Release,
    vfw_ppb_GetClassID,
    vfw_ppb_InitNew,
    vfw_ppb_Load,
    vfw_ppb_Save
};

/* IAMFilterMiscFlags on CLSID_VfwCapture */
static HRESULT WINAPI vfw_misc_QueryInterface(IAMFilterMiscFlags *iface, REFIID riid, void **ppv)
{
    struct vfw_capture *filter = impl_from_vfw_IAMFilterMiscFlags(iface);
    return IUnknown_QueryInterface(filter->outer_unk, riid, ppv);
}

static ULONG WINAPI vfw_misc_AddRef(IAMFilterMiscFlags *iface)
{
    struct vfw_capture *filter = impl_from_vfw_IAMFilterMiscFlags(iface);
    return IUnknown_AddRef(filter->outer_unk);
}

static ULONG WINAPI vfw_misc_Release(IAMFilterMiscFlags *iface)
{
    struct vfw_capture *filter = impl_from_vfw_IAMFilterMiscFlags(iface);
    return IUnknown_Release(filter->outer_unk);
}

static ULONG WINAPI vfw_misc_GetMiscFlags(IAMFilterMiscFlags *iface)
{
    (void)iface;
    return AM_FILTER_MISC_FLAGS_IS_SOURCE;
}

static const IAMFilterMiscFlagsVtbl vfw_misc_vtbl = {
    vfw_misc_QueryInterface,
    vfw_misc_AddRef,
    vfw_misc_Release,
    vfw_misc_GetMiscFlags
};

/* IAMVideoControl on CLSID_VfwCapture */
static HRESULT WINAPI vfw_vctrl_QueryInterface(IAMVideoControl *iface, REFIID riid, void **ppv)
{
    struct vfw_capture *filter = impl_from_vfw_IAMVideoControl(iface);
    return IUnknown_QueryInterface(filter->outer_unk, riid, ppv);
}

static ULONG WINAPI vfw_vctrl_AddRef(IAMVideoControl *iface)
{
    struct vfw_capture *filter = impl_from_vfw_IAMVideoControl(iface);
    return IUnknown_AddRef(filter->outer_unk);
}

static ULONG WINAPI vfw_vctrl_Release(IAMVideoControl *iface)
{
    struct vfw_capture *filter = impl_from_vfw_IAMVideoControl(iface);
    return IUnknown_Release(filter->outer_unk);
}

static HRESULT WINAPI vfw_vctrl_GetCaps(IAMVideoControl *iface, IPin *pPin, LONG *pCapsFlags)
{
    struct vfw_capture *filter = impl_from_vfw_IAMVideoControl(iface);
    if (!pPin || !pCapsFlags) {
        return E_POINTER;
    }
    if (pPin != &filter->IPin_iface) {
        return E_INVALIDARG;
    }
    *pCapsFlags = VideoControlFlag_FlipHorizontal | VideoControlFlag_FlipVertical;
    return S_OK;
}

static HRESULT WINAPI vfw_vctrl_SetMode(IAMVideoControl *iface, IPin *pPin, LONG Mode)
{
    struct vfw_capture *filter = impl_from_vfw_IAMVideoControl(iface);
    struct qcap_avf_device *dev = (struct qcap_avf_device *)filter->avf_dev;
    if (!pPin) {
        return E_POINTER;
    }
    if (pPin != &filter->IPin_iface) {
        return E_INVALIDARG;
    }
    if (dev) {
        dev->video_control_mode = Mode;
    }
    return S_OK;
}

static HRESULT WINAPI vfw_vctrl_GetMode(IAMVideoControl *iface, IPin *pPin, LONG *Mode)
{
    struct vfw_capture *filter = impl_from_vfw_IAMVideoControl(iface);
    struct qcap_avf_device *dev = (struct qcap_avf_device *)filter->avf_dev;
    if (!pPin || !Mode) {
        return E_POINTER;
    }
    if (pPin != &filter->IPin_iface) {
        return E_INVALIDARG;
    }
    *Mode = dev ? dev->video_control_mode : 0;
    return S_OK;
}

static HRESULT WINAPI vfw_vctrl_GetCurrentActualFrameRate(
    IAMVideoControl *iface,
    IPin *pPin,
    LONGLONG *ActualFrameRate)
{
    struct vfw_capture *filter = impl_from_vfw_IAMVideoControl(iface);
    struct qcap_avf_device *dev = (struct qcap_avf_device *)filter->avf_dev;
    if (!pPin || !ActualFrameRate) {
        return E_POINTER;
    }
    if (pPin != &filter->IPin_iface || !dev) {
        return E_INVALIDARG;
    }
    *ActualFrameRate = dev->current_vih.AvgTimePerFrame;
    return S_OK;
}

static HRESULT WINAPI vfw_vctrl_GetMaxAvailableFrameRate(
    IAMVideoControl *iface,
    IPin *pPin,
    LONG iIndex,
    SIZE Dimensions,
    LONGLONG *MaxAvailableFrameRate)
{
    struct vfw_capture *filter = impl_from_vfw_IAMVideoControl(iface);
    if (!pPin || !MaxAvailableFrameRate) {
        return E_POINTER;
    }
    if (pPin != &filter->IPin_iface || iIndex < 0 ||
        iIndex >= (LONG)(sizeof(k_avf_caps) / sizeof(k_avf_caps[0]))) {
        return E_INVALIDARG;
    }
    if (Dimensions.cx != k_avf_caps[iIndex].width ||
        Dimensions.cy != k_avf_caps[iIndex].height) {
        return E_INVALIDARG;
    }
    *MaxAvailableFrameRate = k_avf_caps[iIndex].min_interval;
    return S_OK;
}

static HRESULT WINAPI vfw_vctrl_GetFrameRateList(
    IAMVideoControl *iface,
    IPin *pPin,
    LONG iIndex,
    SIZE Dimensions,
    LONG *ListSize,
    LONGLONG **FrameRates)
{
    struct vfw_capture *filter = impl_from_vfw_IAMVideoControl(iface);
    if (!pPin || !ListSize || !FrameRates) {
        return E_POINTER;
    }
    if (pPin != &filter->IPin_iface || iIndex < 0 ||
        iIndex >= (LONG)(sizeof(k_avf_caps) / sizeof(k_avf_caps[0]))) {
        return E_INVALIDARG;
    }
    return qcap_avf_get_frame_intervals(
        filter->avf_dev,
        Dimensions.cx,
        Dimensions.cy,
        k_avf_caps[iIndex].subtype,
        FrameRates,
        ListSize);
}

static const IAMVideoControlVtbl vfw_vctrl_vtbl = {
    vfw_vctrl_QueryInterface,
    vfw_vctrl_AddRef,
    vfw_vctrl_Release,
    vfw_vctrl_GetCaps,
    vfw_vctrl_SetMode,
    vfw_vctrl_GetMode,
    vfw_vctrl_GetCurrentActualFrameRate,
    vfw_vctrl_GetMaxAvailableFrameRate,
    vfw_vctrl_GetFrameRateList
};

/* IAMVideoProcAmp on CLSID_VfwCapture */
static HRESULT WINAPI vfw_procamp_QueryInterface(IAMVideoProcAmp *iface, REFIID riid, void **ppv)
{
    struct vfw_capture *filter = impl_from_vfw_IAMVideoProcAmp(iface);
    return IUnknown_QueryInterface(filter->outer_unk, riid, ppv);
}

static ULONG WINAPI vfw_procamp_AddRef(IAMVideoProcAmp *iface)
{
    struct vfw_capture *filter = impl_from_vfw_IAMVideoProcAmp(iface);
    return IUnknown_AddRef(filter->outer_unk);
}

static ULONG WINAPI vfw_procamp_Release(IAMVideoProcAmp *iface)
{
    struct vfw_capture *filter = impl_from_vfw_IAMVideoProcAmp(iface);
    return IUnknown_Release(filter->outer_unk);
}

static HRESULT WINAPI vfw_procamp_GetRange(
    IAMVideoProcAmp *iface,
    LONG Property,
    LONG *pMin,
    LONG *pMax,
    LONG *pSteppingDelta,
    LONG *pDefault,
    LONG *pCapsFlags)
{
    struct vfw_capture *filter = impl_from_vfw_IAMVideoProcAmp(iface);
    return qcap_avf_get_prop_range(
        filter->avf_dev, Property, pMin, pMax, pSteppingDelta, pDefault, pCapsFlags);
}

static HRESULT WINAPI vfw_procamp_Set(
    IAMVideoProcAmp *iface,
    LONG Property,
    LONG lValue,
    LONG Flags)
{
    struct vfw_capture *filter = impl_from_vfw_IAMVideoProcAmp(iface);
    return qcap_avf_set_prop(filter->avf_dev, Property, lValue, Flags);
}

static HRESULT WINAPI vfw_procamp_Get(
    IAMVideoProcAmp *iface,
    LONG Property,
    LONG *lValue,
    LONG *Flags)
{
    struct vfw_capture *filter = impl_from_vfw_IAMVideoProcAmp(iface);
    return qcap_avf_get_prop(filter->avf_dev, Property, lValue, Flags);
}

static const IAMVideoProcAmpVtbl vfw_procamp_vtbl = {
    vfw_procamp_QueryInterface,
    vfw_procamp_AddRef,
    vfw_procamp_Release,
    vfw_procamp_GetRange,
    vfw_procamp_Set,
    vfw_procamp_Get
};

/* IAMDroppedFrames on CLSID_VfwCapture */
static HRESULT WINAPI vfw_dropped_QueryInterface(IAMDroppedFrames *iface, REFIID riid, void **ppv)
{
    struct vfw_capture *filter = impl_from_vfw_IAMDroppedFrames(iface);
    return IUnknown_QueryInterface(filter->outer_unk, riid, ppv);
}

static ULONG WINAPI vfw_dropped_AddRef(IAMDroppedFrames *iface)
{
    struct vfw_capture *filter = impl_from_vfw_IAMDroppedFrames(iface);
    return IUnknown_AddRef(filter->outer_unk);
}

static ULONG WINAPI vfw_dropped_Release(IAMDroppedFrames *iface)
{
    struct vfw_capture *filter = impl_from_vfw_IAMDroppedFrames(iface);
    return IUnknown_Release(filter->outer_unk);
}

static HRESULT WINAPI vfw_dropped_GetNumDropped(IAMDroppedFrames *iface, LONG *plDropped)
{
    struct vfw_capture *filter = impl_from_vfw_IAMDroppedFrames(iface);
    struct qcap_avf_device *dev = (struct qcap_avf_device *)filter->avf_dev;
    if (!plDropped) {
        return E_POINTER;
    }
    *plDropped = dev ? dev->frames_dropped : 0;
    return S_OK;
}

static HRESULT WINAPI vfw_dropped_GetNumNotDropped(IAMDroppedFrames *iface, LONG *plNotDropped)
{
    struct vfw_capture *filter = impl_from_vfw_IAMDroppedFrames(iface);
    struct qcap_avf_device *dev = (struct qcap_avf_device *)filter->avf_dev;
    if (!plNotDropped) {
        return E_POINTER;
    }
    *plNotDropped = dev ? dev->frames_captured : 0;
    return S_OK;
}

static HRESULT WINAPI vfw_dropped_GetAverageFrameSize(IAMDroppedFrames *iface, LONG *plAverageSize)
{
    struct vfw_capture *filter = impl_from_vfw_IAMDroppedFrames(iface);
    struct qcap_avf_device *dev = (struct qcap_avf_device *)filter->avf_dev;
    if (!plAverageSize) {
        return E_POINTER;
    }
    *plAverageSize = dev ? (LONG)dev->current_vih.bmiHeader.biSizeImage : 0;
    return S_OK;
}

static HRESULT WINAPI vfw_dropped_GetDroppedInfo(
    IAMDroppedFrames *iface,
    LONG lSize,
    LONG *plArray,
    LONG *plNumCopied)
{
    (void)iface;
    (void)lSize;
    if (!plArray || !plNumCopied) {
        return E_POINTER;
    }
    *plNumCopied = 0;
    return S_OK;
}

static const IAMDroppedFramesVtbl vfw_dropped_vtbl = {
    vfw_dropped_QueryInterface,
    vfw_dropped_AddRef,
    vfw_dropped_Release,
    vfw_dropped_GetNumDropped,
    vfw_dropped_GetNumNotDropped,
    vfw_dropped_GetAverageFrameSize,
    vfw_dropped_GetDroppedInfo
};

/* Output IPin on CLSID_VfwCapture */
static HRESULT WINAPI vfw_pin_QueryInterface(IPin *iface, REFIID riid, void **ppv)
{
    struct vfw_capture *filter = impl_from_vfw_IPin(iface);

    if (!ppv) {
        return E_POINTER;
    }
    *ppv = NULL;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IPin)) {
        *ppv = &filter->IPin_iface;
    } else if (IsEqualIID(riid, &IID_IAMStreamConfig)) {
        *ppv = &filter->IAMStreamConfig_iface;
    } else if (IsEqualIID(riid, &IID_IKsPropertySet)) {
        *ppv = &filter->IKsPropertySet_iface;
    } else if (IsEqualIID(riid, &IID_IQualityControl)) {
        *ppv = &filter->IQualityControl_iface;
    } else if (IsEqualIID(riid, &IID_IAMVideoControl)) {
        *ppv = &filter->IAMVideoControl_iface;
    } else if (IsEqualIID(riid, &IID_IAMVideoProcAmp)) {
        *ppv = &filter->IAMVideoProcAmp_iface;
    } else if (IsEqualIID(riid, &IID_IAMDroppedFrames)) {
        *ppv = &filter->IAMDroppedFrames_iface;
    } else {
        return E_NOINTERFACE;
    }

    IUnknown_AddRef((IUnknown *)*ppv);
    return S_OK;
}

static ULONG WINAPI vfw_pin_AddRef(IPin *iface)
{
    struct vfw_capture *filter = impl_from_vfw_IPin(iface);
    return IBaseFilter_AddRef(&filter->IBaseFilter_iface);
}

static ULONG WINAPI vfw_pin_Release(IPin *iface)
{
    struct vfw_capture *filter = impl_from_vfw_IPin(iface);
    return IBaseFilter_Release(&filter->IBaseFilter_iface);
}

static HRESULT WINAPI vfw_pin_Connect(
    IPin *iface,
    IPin *pReceivePin,
    const AM_MEDIA_TYPE *pmt)
{
    struct vfw_capture *filter = impl_from_vfw_IPin(iface);
    struct qcap_avf_device *dev = (struct qcap_avf_device *)filter->avf_dev;
    PIN_DIRECTION dir = PINDIR_OUTPUT;
    HRESULT hr;

    if (!pReceivePin) {
        return E_POINTER;
    }
    if (filter->peer_pin) {
        return VFW_E_ALREADY_CONNECTED;
    }
    if (filter->state != State_Stopped) {
        return VFW_E_NOT_STOPPED;
    }
    hr = IPin_QueryDirection(pReceivePin, &dir);
    if (SUCCEEDED(hr) && dir != PINDIR_INPUT) {
        return VFW_E_INVALID_DIRECTION;
    }
    if (pmt && IsEqualGUID(&pmt->formattype, &FORMAT_VideoInfo)) {
        hr = qcap_avf_set_format(filter->avf_dev, pmt);
        if (FAILED(hr)) {
            return VFW_E_TYPE_NOT_ACCEPTED;
        }
    }
    hr = IPin_ReceiveConnection(pReceivePin, iface, &dev->current_mt);
    if (FAILED(hr)) {
        return hr;
    }
    filter->peer_pin = pReceivePin;
    IPin_AddRef(pReceivePin);
    return S_OK;
}

static HRESULT WINAPI vfw_pin_ReceiveConnection(
    IPin *iface,
    IPin *pConnector,
    const AM_MEDIA_TYPE *pmt)
{
    (void)iface;
    (void)pConnector;
    (void)pmt;
    return E_UNEXPECTED;
}

static HRESULT WINAPI vfw_pin_Disconnect(IPin *iface)
{
    struct vfw_capture *filter = impl_from_vfw_IPin(iface);
    if (filter->state != State_Stopped) {
        return VFW_E_NOT_STOPPED;
    }
    if (!filter->peer_pin) {
        return S_FALSE;
    }
    IPin_Release(filter->peer_pin);
    filter->peer_pin = NULL;
    return S_OK;
}

static HRESULT WINAPI vfw_pin_ConnectedTo(IPin *iface, IPin **pPin)
{
    struct vfw_capture *filter = impl_from_vfw_IPin(iface);
    if (!pPin) {
        return E_POINTER;
    }
    *pPin = filter->peer_pin;
    if (!filter->peer_pin) {
        return VFW_E_NOT_CONNECTED;
    }
    IPin_AddRef(filter->peer_pin);
    return S_OK;
}

static HRESULT WINAPI vfw_pin_ConnectionMediaType(IPin *iface, AM_MEDIA_TYPE *pmt)
{
    struct vfw_capture *filter = impl_from_vfw_IPin(iface);
    if (!pmt) {
        return E_POINTER;
    }
    if (!filter->peer_pin) {
        memset(pmt, 0, sizeof(*pmt));
        return VFW_E_NOT_CONNECTED;
    }
    return qcap_avf_get_format(filter->avf_dev, pmt);
}

static HRESULT WINAPI vfw_pin_QueryPinInfo(IPin *iface, PIN_INFO *pInfo)
{
    struct vfw_capture *filter = impl_from_vfw_IPin(iface);
    if (!pInfo) {
        return E_POINTER;
    }
    pInfo->pFilter = &filter->IBaseFilter_iface;
    IBaseFilter_AddRef(pInfo->pFilter);
    pInfo->dir = PINDIR_OUTPUT;
    avf_wstr_copy(pInfo->achName, 128, k_video_capture_pin_name);
    return S_OK;
}

static HRESULT WINAPI vfw_pin_QueryDirection(IPin *iface, PIN_DIRECTION *pPinDir)
{
    (void)iface;
    if (!pPinDir) {
        return E_POINTER;
    }
    *pPinDir = PINDIR_OUTPUT;
    return S_OK;
}

static HRESULT WINAPI vfw_pin_QueryId(IPin *iface, LPWSTR *Id)
{
    size_t len;
    if (!Id) {
        return E_POINTER;
    }
    len = (avf_wstr_len(k_video_capture_pin_name) + 1) * sizeof(WCHAR);
    *Id = (LPWSTR)CoTaskMemAlloc(len);
    if (!*Id) {
        return E_OUTOFMEMORY;
    }
    memcpy(*Id, k_video_capture_pin_name, len);
    (void)iface;
    return S_OK;
}

static HRESULT WINAPI vfw_pin_QueryAccept(IPin *iface, const AM_MEDIA_TYPE *pmt)
{
    struct vfw_capture *filter = impl_from_vfw_IPin(iface);
    if (!pmt) {
        return E_POINTER;
    }
    return qcap_avf_check_format(filter->avf_dev, pmt);
}

static HRESULT WINAPI vfw_pin_EnumMediaTypes(IPin *iface, IEnumMediaTypes **ppEnum)
{
    struct vfw_capture *filter = impl_from_vfw_IPin(iface);
    struct vfw_enum_mt *emt;

    if (!ppEnum) {
        return E_POINTER;
    }
    emt = (struct vfw_enum_mt *)calloc(1, sizeof(*emt));
    if (!emt) {
        return E_OUTOFMEMORY;
    }
    emt->IEnumMediaTypes_iface.lpVtbl = &vfw_enum_mt_vtbl;
    emt->refcount = 1;
    emt->index = 0;
    emt->parent = filter;
    IPin_AddRef(iface);
    *ppEnum = &emt->IEnumMediaTypes_iface;
    return S_OK;
}

static HRESULT WINAPI vfw_pin_QueryInternalConnections(IPin *iface, IPin **apPin, ULONG *nPin)
{
    (void)iface;
    (void)apPin;
    (void)nPin;
    return E_NOTIMPL;
}

static HRESULT WINAPI vfw_pin_EndOfStream(IPin *iface)
{
    (void)iface;
    return E_UNEXPECTED;
}

static HRESULT WINAPI vfw_pin_BeginFlush(IPin *iface)
{
    (void)iface;
    return E_UNEXPECTED;
}

static HRESULT WINAPI vfw_pin_EndFlush(IPin *iface)
{
    (void)iface;
    return E_UNEXPECTED;
}

static HRESULT WINAPI vfw_pin_NewSegment(
    IPin *iface,
    REFERENCE_TIME tStart,
    REFERENCE_TIME tStop,
    double dRate)
{
    (void)iface;
    (void)tStart;
    (void)tStop;
    (void)dRate;
    return S_OK;
}

static const IPinVtbl vfw_pin_vtbl = {
    vfw_pin_QueryInterface,
    vfw_pin_AddRef,
    vfw_pin_Release,
    vfw_pin_Connect,
    vfw_pin_ReceiveConnection,
    vfw_pin_Disconnect,
    vfw_pin_ConnectedTo,
    vfw_pin_ConnectionMediaType,
    vfw_pin_QueryPinInfo,
    vfw_pin_QueryDirection,
    vfw_pin_QueryId,
    vfw_pin_QueryAccept,
    vfw_pin_EnumMediaTypes,
    vfw_pin_QueryInternalConnections,
    vfw_pin_EndOfStream,
    vfw_pin_BeginFlush,
    vfw_pin_EndFlush,
    vfw_pin_NewSegment
};

/* IAMStreamConfig on CLSID_VfwCapture Pin */
static HRESULT WINAPI vfw_sc_QueryInterface(IAMStreamConfig *iface, REFIID riid, void **ppv)
{
    struct vfw_capture *filter = impl_from_vfw_IAMStreamConfig(iface);
    return IPin_QueryInterface(&filter->IPin_iface, riid, ppv);
}

static ULONG WINAPI vfw_sc_AddRef(IAMStreamConfig *iface)
{
    struct vfw_capture *filter = impl_from_vfw_IAMStreamConfig(iface);
    return IPin_AddRef(&filter->IPin_iface);
}

static ULONG WINAPI vfw_sc_Release(IAMStreamConfig *iface)
{
    struct vfw_capture *filter = impl_from_vfw_IAMStreamConfig(iface);
    return IPin_Release(&filter->IPin_iface);
}

static HRESULT WINAPI vfw_sc_SetFormat(IAMStreamConfig *iface, AM_MEDIA_TYPE *pmt)
{
    struct vfw_capture *filter = impl_from_vfw_IAMStreamConfig(iface);
    if (!pmt) {
        return E_POINTER;
    }
    if (filter->state != State_Stopped) {
        return VFW_E_NOT_STOPPED;
    }
    return qcap_avf_set_format(filter->avf_dev, pmt);
}

static HRESULT WINAPI vfw_sc_GetFormat(IAMStreamConfig *iface, AM_MEDIA_TYPE **ppmt)
{
    struct vfw_capture *filter = impl_from_vfw_IAMStreamConfig(iface);
    struct qcap_avf_device *dev = (struct qcap_avf_device *)filter->avf_dev;
    if (!ppmt) {
        return E_POINTER;
    }
    return avf_duplicate_media_type(ppmt, &dev->current_mt);
}

static HRESULT WINAPI vfw_sc_GetNumberOfCapabilities(
    IAMStreamConfig *iface,
    int *piCount,
    int *piSize)
{
    struct vfw_capture *filter = impl_from_vfw_IAMStreamConfig(iface);
    if (!piCount || !piSize) {
        return E_POINTER;
    }
    *piCount = qcap_avf_get_caps_count(filter->avf_dev);
    *piSize = (int)sizeof(VIDEO_STREAM_CONFIG_CAPS);
    return S_OK;
}

static HRESULT WINAPI vfw_sc_GetStreamCaps(
    IAMStreamConfig *iface,
    int iIndex,
    AM_MEDIA_TYPE **ppmt,
    BYTE *pSCC)
{
    struct vfw_capture *filter = impl_from_vfw_IAMStreamConfig(iface);
    AM_MEDIA_TYPE mt;
    HRESULT hr;

    if (!ppmt || !pSCC) {
        return E_POINTER;
    }
    *ppmt = NULL;
    memset(&mt, 0, sizeof(mt));
    hr = qcap_avf_get_caps(filter->avf_dev, iIndex, &mt, (VIDEO_STREAM_CONFIG_CAPS *)pSCC);
    if (hr != S_OK) {
        return hr;
    }
    *ppmt = (AM_MEDIA_TYPE *)CoTaskMemAlloc(sizeof(AM_MEDIA_TYPE));
    if (!*ppmt) {
        qcap_free_media_type_contents(&mt);
        return E_OUTOFMEMORY;
    }
    **ppmt = mt;
    return S_OK;
}

static const IAMStreamConfigVtbl vfw_sc_vtbl = {
    vfw_sc_QueryInterface,
    vfw_sc_AddRef,
    vfw_sc_Release,
    vfw_sc_SetFormat,
    vfw_sc_GetFormat,
    vfw_sc_GetNumberOfCapabilities,
    vfw_sc_GetStreamCaps
};

/* IKsPropertySet on CLSID_VfwCapture Pin */
static HRESULT WINAPI vfw_ks_QueryInterface(IKsPropertySet *iface, REFIID riid, void **ppv)
{
    struct vfw_capture *filter = impl_from_vfw_IKsPropertySet(iface);
    return IPin_QueryInterface(&filter->IPin_iface, riid, ppv);
}

static ULONG WINAPI vfw_ks_AddRef(IKsPropertySet *iface)
{
    struct vfw_capture *filter = impl_from_vfw_IKsPropertySet(iface);
    return IPin_AddRef(&filter->IPin_iface);
}

static ULONG WINAPI vfw_ks_Release(IKsPropertySet *iface)
{
    struct vfw_capture *filter = impl_from_vfw_IKsPropertySet(iface);
    return IPin_Release(&filter->IPin_iface);
}

static HRESULT WINAPI vfw_ks_Set(
    IKsPropertySet *iface,
    REFGUID guidPropSet,
    DWORD dwPropID,
    LPVOID pInstanceData,
    DWORD cbInstanceData,
    LPVOID pPropData,
    DWORD cbPropData)
{
    (void)iface;
    (void)guidPropSet;
    (void)dwPropID;
    (void)pInstanceData;
    (void)cbInstanceData;
    (void)pPropData;
    (void)cbPropData;
    return E_NOTIMPL;
}

static HRESULT WINAPI vfw_ks_Get(
    IKsPropertySet *iface,
    REFGUID guidPropSet,
    DWORD dwPropID,
    LPVOID pInstanceData,
    DWORD cbInstanceData,
    LPVOID pPropData,
    DWORD cbPropData,
    DWORD *pcbReturned)
{
    (void)iface;
    (void)pInstanceData;
    (void)cbInstanceData;

    if (!pcbReturned) {
        return E_POINTER;
    }
    if (!IsEqualGUID(guidPropSet, &AMPROPSETID_Pin) || dwPropID != AMPROPERTY_PIN_CATEGORY) {
        *pcbReturned = 0;
        return E_PROP_ID_UNSUPPORTED;
    }
    *pcbReturned = sizeof(GUID);
    if (!pPropData && !cbPropData) {
        return E_UNEXPECTED;
    }
    if (cbPropData < sizeof(GUID)) {
        return E_OUTOFMEMORY;
    }
    if (!pPropData) {
        return E_POINTER;
    }
    *(GUID *)pPropData = PIN_CATEGORY_CAPTURE;
    return S_OK;
}

static HRESULT WINAPI vfw_ks_QuerySupported(
    IKsPropertySet *iface,
    REFGUID guidPropSet,
    DWORD dwPropID,
    DWORD *pTypeSupport)
{
    (void)iface;
    if (!pTypeSupport) {
        return E_POINTER;
    }
    if (IsEqualGUID(guidPropSet, &AMPROPSETID_Pin) && dwPropID == AMPROPERTY_PIN_CATEGORY) {
        *pTypeSupport = KSPROPERTY_SUPPORT_GET;
        return S_OK;
    }
    *pTypeSupport = 0;
    return E_PROP_ID_UNSUPPORTED;
}

static const IKsPropertySetVtbl vfw_ks_vtbl = {
    vfw_ks_QueryInterface,
    vfw_ks_AddRef,
    vfw_ks_Release,
    vfw_ks_Set,
    vfw_ks_Get,
    vfw_ks_QuerySupported
};

/* IQualityControl on CLSID_VfwCapture Pin */
static HRESULT WINAPI vfw_qc_QueryInterface(IQualityControl *iface, REFIID riid, void **ppv)
{
    struct vfw_capture *filter = impl_from_vfw_IQualityControl(iface);
    return IPin_QueryInterface(&filter->IPin_iface, riid, ppv);
}

static ULONG WINAPI vfw_qc_AddRef(IQualityControl *iface)
{
    struct vfw_capture *filter = impl_from_vfw_IQualityControl(iface);
    return IPin_AddRef(&filter->IPin_iface);
}

static ULONG WINAPI vfw_qc_Release(IQualityControl *iface)
{
    struct vfw_capture *filter = impl_from_vfw_IQualityControl(iface);
    return IPin_Release(&filter->IPin_iface);
}

static HRESULT WINAPI vfw_qc_Notify(IQualityControl *iface, IBaseFilter *pSelf, Quality q)
{
    (void)iface;
    (void)pSelf;
    (void)q;
    return S_OK;
}

static HRESULT WINAPI vfw_qc_SetSink(IQualityControl *iface, IQualityControl *piqc)
{
    struct vfw_capture *filter = impl_from_vfw_IQualityControl(iface);
    if (piqc) {
        IUnknown_AddRef((IUnknown *)piqc);
    }
    if (filter->qc_sink) {
        IUnknown_Release((IUnknown *)filter->qc_sink);
    }
    filter->qc_sink = piqc;
    return S_OK;
}

static const IQualityControlVtbl vfw_qc_vtbl = {
    vfw_qc_QueryInterface,
    vfw_qc_AddRef,
    vfw_qc_Release,
    vfw_qc_Notify,
    vfw_qc_SetSink
};

HRESULT WINAPI vfw_capture_create(IUnknown *outer, IUnknown **out)
{
    struct vfw_capture *filter;
    HRESULT hr;

    TRACE("(%p, %p)\n", outer, out);

    if (!out) {
        return E_POINTER;
    }
    *out = NULL;

    filter = (struct vfw_capture *)calloc(1, sizeof(*filter));
    if (!filter) {
        return E_OUTOFMEMORY;
    }

    hr = qcap_avf_create(0, &filter->avf_dev);
    if (FAILED(hr)) {
        free(filter);
        return hr;
    }

    filter->IUnknown_inner.lpVtbl = &vfw_inner_vtbl;
    filter->IBaseFilter_iface.lpVtbl = &vfw_filter_vtbl;
    filter->IPersistPropertyBag_iface.lpVtbl = &vfw_ppb_vtbl;
    filter->IAMFilterMiscFlags_iface.lpVtbl = &vfw_misc_vtbl;
    filter->IAMVideoControl_iface.lpVtbl = &vfw_vctrl_vtbl;
    filter->IAMVideoProcAmp_iface.lpVtbl = &vfw_procamp_vtbl;
    filter->IAMDroppedFrames_iface.lpVtbl = &vfw_dropped_vtbl;
    filter->IPin_iface.lpVtbl = &vfw_pin_vtbl;
    filter->IAMStreamConfig_iface.lpVtbl = &vfw_sc_vtbl;
    filter->IKsPropertySet_iface.lpVtbl = &vfw_ks_vtbl;
    filter->IQualityControl_iface.lpVtbl = &vfw_qc_vtbl;

    filter->outer_unk = outer ? outer : &filter->IUnknown_inner;
    filter->refcount = 1;
    filter->state = State_Stopped;

    *out = &filter->IUnknown_inner;
    return S_OK;
}

/* Media Foundation (mfreadwrite) Capture SourceReader Bridge */
HRESULT WINAPI QcapCreateAVFoundationReader(
    UINT32 device_index,
    QCAP_MF_SOURCE_READER **out_reader)
{
    QCAP_MF_SOURCE_READER *reader;
    HRESULT hr;

    if (!out_reader) {
        return E_POINTER;
    }
    *out_reader = NULL;

    reader = (QCAP_MF_SOURCE_READER *)calloc(1, sizeof(*reader));
    if (!reader) {
        return E_OUTOFMEMORY;
    }

    hr = qcap_avf_create((int)device_index, &reader->avf_dev);
    if (FAILED(hr)) {
        free(reader);
        return hr;
    }

    reader->device_index = device_index;
    reader->video_subtype = MEDIASUBTYPE_RGB24;
    reader->video_width = 640;
    reader->video_height = 480;
    reader->video_bpp = 24;
    reader->audio_channels = 2;
    reader->audio_sample_rate = 44100;
    reader->audio_bits_per_sample = 16;
    reader->video_pts = 0;
    reader->audio_pts = 0;

    *out_reader = reader;
    return S_OK;
}

void WINAPI QcapDestroyAVFoundationReader(QCAP_MF_SOURCE_READER *reader)
{
    if (!reader) {
        return;
    }
    if (reader->avf_dev) {
        qcap_avf_destroy(reader->avf_dev);
    }
    free(reader);
}

HRESULT WINAPI QcapAVFoundationReaderGetStreamFormat(
    QCAP_MF_SOURCE_READER *reader,
    DWORD stream_index,
    GUID *major_type,
    GUID *subtype,
    UINT32 *width_or_channels,
    UINT32 *height_or_sample_rate,
    UINT32 *bits_per_sample)
{
    if (!reader) {
        return E_POINTER;
    }
    if (stream_index == QCAP_MF_SOURCE_READER_FIRST_VIDEO_STREAM || stream_index == 0) {
        if (major_type) {
            *major_type = MEDIATYPE_Video;
        }
        if (subtype) {
            *subtype = reader->video_subtype;
        }
        if (width_or_channels) {
            *width_or_channels = reader->video_width;
        }
        if (height_or_sample_rate) {
            *height_or_sample_rate = reader->video_height;
        }
        if (bits_per_sample) {
            *bits_per_sample = reader->video_bpp;
        }
        return S_OK;
    }
    if (stream_index == QCAP_MF_SOURCE_READER_FIRST_AUDIO_STREAM || stream_index == 1) {
        if (major_type) {
            *major_type = MEDIATYPE_Audio;
        }
        if (subtype) {
            *subtype = MEDIASUBTYPE_PCM;
        }
        if (width_or_channels) {
            *width_or_channels = reader->audio_channels;
        }
        if (height_or_sample_rate) {
            *height_or_sample_rate = reader->audio_sample_rate;
        }
        if (bits_per_sample) {
            *bits_per_sample = reader->audio_bits_per_sample;
        }
        return S_OK;
    }
    return MF_E_INVALIDSTREAMNUMBER;
}

HRESULT WINAPI QcapAVFoundationReaderSetStreamFormat(
    QCAP_MF_SOURCE_READER *reader,
    DWORD stream_index,
    const GUID *subtype,
    UINT32 width_or_channels,
    UINT32 height_or_sample_rate,
    UINT32 bits_per_sample)
{
    if (!reader || !subtype) {
        return E_POINTER;
    }
    if (stream_index == QCAP_MF_SOURCE_READER_FIRST_VIDEO_STREAM || stream_index == 0) {
        AM_MEDIA_TYPE mt;
        VIDEOINFOHEADER vih;
        struct avf_cap_entry cap;

        if (!IsEqualGUID(subtype, &MEDIASUBTYPE_RGB24) &&
            !IsEqualGUID(subtype, &MEDIASUBTYPE_NV12) &&
            !IsEqualGUID(subtype, &MEDIASUBTYPE_YUY2)) {
            return MF_E_INVALIDMEDIATYPE;
        }
        cap.width = (LONG)width_or_channels;
        cap.height = (LONG)height_or_sample_rate;
        cap.bit_count = (WORD)bits_per_sample;
        cap.compression = IsEqualGUID(subtype, &MEDIASUBTYPE_NV12)
            ? FOURCC_NV12
            : (IsEqualGUID(subtype, &MEDIASUBTYPE_YUY2) ? FOURCC_YUY2 : BI_RGB);
        cap.subtype = subtype;
        cap.min_interval = 166666LL;
        cap.max_interval = 333333LL;

        init_video_media_type_from_cap(&mt, &vih, &cap);
        if (FAILED(qcap_avf_set_format(reader->avf_dev, &mt))) {
            return MF_E_INVALIDMEDIATYPE;
        }
        reader->video_subtype = *subtype;
        reader->video_width = width_or_channels;
        reader->video_height = height_or_sample_rate;
        reader->video_bpp = bits_per_sample;
        return S_OK;
    }
    if (stream_index == QCAP_MF_SOURCE_READER_FIRST_AUDIO_STREAM || stream_index == 1) {
        if (!IsEqualGUID(subtype, &MEDIASUBTYPE_PCM) ||
            (width_or_channels != 1 && width_or_channels != 2) ||
            (bits_per_sample != 8 && bits_per_sample != 16)) {
            return MF_E_INVALIDMEDIATYPE;
        }
        reader->audio_channels = width_or_channels;
        reader->audio_sample_rate = height_or_sample_rate;
        reader->audio_bits_per_sample = bits_per_sample;
        return S_OK;
    }
    return MF_E_INVALIDSTREAMNUMBER;
}

HRESULT WINAPI QcapAVFoundationReaderReadSample(
    QCAP_MF_SOURCE_READER *reader,
    DWORD stream_index,
    DWORD *actual_stream_index,
    DWORD *stream_flags,
    LONGLONG *timestamp,
    BYTE *sample_buf,
    DWORD sample_buf_capacity,
    DWORD *sample_bytes_written)
{
    if (!reader || !sample_buf || !sample_bytes_written) {
        return E_POINTER;
    }
    *sample_bytes_written = 0;
    if (stream_flags) {
        *stream_flags = 0;
    }

    if (stream_index == QCAP_MF_SOURCE_READER_FIRST_VIDEO_STREAM || stream_index == 0) {
        BOOL bottom_up = FALSE;
        HRESULT hr = qcap_avf_read_frame(
            reader->avf_dev,
            sample_buf,
            sample_buf_capacity,
            sample_bytes_written,
            &bottom_up);
        if (FAILED(hr)) {
            return hr;
        }
        if (actual_stream_index) {
            *actual_stream_index = 0;
        }
        if (timestamp) {
            *timestamp = reader->video_pts;
        }
        reader->video_pts += 333333LL;
        return S_OK;
    }

    if (stream_index == QCAP_MF_SOURCE_READER_FIRST_AUDIO_STREAM || stream_index == 1) {
        DWORD block_align = (reader->audio_channels * reader->audio_bits_per_sample) / 8;
        DWORD avg_bytes = reader->audio_sample_rate * block_align;
        DWORD chunk_bytes = avg_bytes / 20; /* 50ms audio packet */
        if (chunk_bytes == 0 || chunk_bytes > sample_buf_capacity) {
            chunk_bytes = sample_buf_capacity;
        }
        memset(sample_buf, 0, chunk_bytes);
        *sample_bytes_written = chunk_bytes;
        if (actual_stream_index) {
            *actual_stream_index = 1;
        }
        if (timestamp) {
            *timestamp = reader->audio_pts;
        }
        reader->audio_pts += 500000LL;
        return S_OK;
    }

    return MF_E_INVALIDSTREAMNUMBER;
}

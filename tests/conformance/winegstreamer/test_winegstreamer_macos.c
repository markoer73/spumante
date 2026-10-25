/*
 * Wine Parity Differential Conformance Test: winegstreamer.dll
 * (macOS AudioToolbox / VideoToolbox / CoreMedia Bridge for wmadmod.dll,
 *  wmvcore.dll, and ir50_32.dll)
 *
 * Clean-room test derived from Microsoft Learn specifications and upstream
 * Wine dlls/wmadmod/wmadmod.c, dlls/winegstreamer/wma_decoder.c,
 * video_decoder.c, and wm_reader.c.
 *
 * Copyright 2026 Wine Parity Contributors
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include <stddef.h>
#include "../../include/parity_test.h"
#include "../../../dlls/winegstreamer/winegstreamer.h"

static int match_filter(const char *filter, const char *case_name)
{
    if (!filter || filter[0] == '\0') {
        return 1;
    }
    return strstr(case_name, filter) != NULL;
}

typedef struct {
    WG_IUnknown IUnknown_iface;
    LONG refcount;
    BOOL outer_queried;
} test_outer_unk_t;

static HRESULT WINAPI test_outer_QueryInterface(WG_IUnknown *iface, REFIID riid, void **ppv)
{
    test_outer_unk_t *outer = (test_outer_unk_t *)iface;
    if (!ppv) {
        return E_POINTER;
    }
    outer->outer_queried = TRUE;
    if (IsEqualIID(riid, &IID_IUnknown)) {
        *ppv = iface;
        return S_OK;
    }
    *ppv = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI test_outer_AddRef(WG_IUnknown *iface)
{
    test_outer_unk_t *outer = (test_outer_unk_t *)iface;
    return (ULONG)(++outer->refcount);
}

static ULONG WINAPI test_outer_Release(WG_IUnknown *iface)
{
    test_outer_unk_t *outer = (test_outer_unk_t *)iface;
    return (ULONG)(--outer->refcount);
}

static const IUnknownVtbl test_outer_vtbl = {
    test_outer_QueryInterface,
    test_outer_AddRef,
    test_outer_Release
};

int main(int argc, char **argv)
{
    const char *filter = NULL;
    int i;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--filter") == 0 && i + 1 < argc) {
            filter = argv[++i];
        }
    }

    parity_begin_document("winegstreamer", "test_winegstreamer_macos", "x86_64", "wine-macos-audiotoolbox-videotoolbox");

    /* 1. DllCanUnloadNow, DllRegisterServer, DllUnregisterServer */
    if (match_filter(filter, "dll_lifecycle_and_register_server")) {
        HRESULT hr_unload = DllCanUnloadNow();
        HRESULT hr_reg = DllRegisterServer();
        HRESULT hr_unreg = DllUnregisterServer();
        HRESULT hr = (hr_unload == S_FALSE && hr_reg == S_OK && hr_unreg == S_OK) ? S_OK : E_FAIL;

        parity_record_exact(
            "wg_001",
            "dll_lifecycle_and_register_server",
            "DllRegisterServer",
            (NTSTATUS)hr,
            0,
            0,
            NULL,
            0,
            hr == S_OK ? 1 : 0
        );
    }

    /* 2. DllGetClassObject for WMA, AAC, H.264, MP3, WaveParser, DecodeBin */
    if (match_filter(filter, "dll_get_class_object_invalid_and_valid")) {
        static const GUID clsid_unknown = {0xdeadbeef, 0x0000, 0x0000, {0, 0, 0, 0, 0, 0, 0, 1}};
        const GUID *const clsids[] = {
            &CLSID_WMADecMediaObject,
            &CLSID_wg_wma_decoder,
            &CLSID_CMSAACDecMFT,
            &CLSID_CMSH264DecoderMFT,
            &CLSID_wg_mp3_decoder,
            &CLSID_wg_wave_parser,
            &CLSID_decodebin_parser
        };
        void *cf_void = NULL;
        HRESULT hr_inv = DllGetClassObject(&clsid_unknown, &IID_IClassFactory, &cf_void);
        HRESULT hr_null = DllGetClassObject(&CLSID_WMADecMediaObject, &IID_IClassFactory, NULL);
        BYTE ok_flags[7] = {0};
        size_t k;
        HRESULT hr = (hr_inv == CLASS_E_CLASSNOTAVAILABLE && hr_null == E_INVALIDARG) ? S_OK : E_FAIL;

        for (k = 0; k < 7; k++) {
            WG_IClassFactory *cf = NULL;
            if (DllGetClassObject(clsids[k], &IID_IClassFactory, (void **)&cf) == S_OK && cf != NULL) {
                WG_IUnknown *inst = NULL;
                if (IClassFactory_CreateInstance(cf, NULL, &IID_IUnknown, (void **)&inst) == S_OK && inst != NULL) {
                    ok_flags[k] = 1;
                    IUnknown_Release(inst);
                } else {
                    hr = E_FAIL;
                }
                IClassFactory_Release(cf);
            } else {
                hr = E_FAIL;
            }
        }

        parity_record_roundtrip(
            "wg_002",
            "dll_get_class_object_invalid_and_valid",
            "DllGetClassObject",
            (NTSTATUS)hr,
            0,
            sizeof(ok_flags),
            ok_flags,
            sizeof(ok_flags),
            hr == S_OK ? 1 : 0
        );
    }

    /* 3. winegstreamer_create_wma_decoder COM aggregation and interface queries */
    if (match_filter(filter, "wma_decoder_aggregation_and_interfaces")) {
        test_outer_unk_t outer = {{&test_outer_vtbl}, 1, FALSE};
        WG_IUnknown *inner = NULL;
        WG_IMFTransform *mft = NULL;
        WG_IMediaObject *dmo = NULL;
        WG_IPropertyBag *bag = NULL;
        HRESULT hr_bad_agg = winegstreamer_create_wma_decoder(&outer.IUnknown_iface, &IID_IMFTransform, (void **)&mft);
        HRESULT hr_null = winegstreamer_create_wma_decoder(NULL, &IID_IUnknown, NULL);
        HRESULT hr = winegstreamer_create_wma_decoder(&outer.IUnknown_iface, &IID_IUnknown, (void **)&inner);

        if ((hr_bad_agg == E_NOINTERFACE || hr_bad_agg == CLASS_E_NOAGGREGATION) &&
            hr_null == E_POINTER &&
            hr == S_OK && inner != NULL) {
            if (IUnknown_QueryInterface(inner, &IID_IMFTransform, (void **)&mft) == S_OK && mft != NULL &&
                IUnknown_QueryInterface(inner, &IID_IMediaObject, (void **)&dmo) == S_OK && dmo != NULL &&
                IUnknown_QueryInterface(inner, &IID_IPropertyBag, (void **)&bag) == S_OK && bag != NULL) {
                WG_IUnknown *tmp = NULL;
                /* Delegating QueryInterface on IMFTransform must route through outer IUnknown */
                if (mft->lpVtbl->QueryInterface(mft, &IID_IUnknown, (void **)&tmp) == S_OK &&
                    outer.outer_queried && tmp == &outer.IUnknown_iface) {
                    hr = S_OK;
                } else {
                    hr = E_FAIL;
                }
            } else {
                hr = E_FAIL;
            }
            IUnknown_Release(inner);
        } else {
            hr = E_FAIL;
        }

        parity_record_exact(
            "wg_003",
            "wma_decoder_aggregation_and_interfaces",
            "winegstreamer_create_wma_decoder",
            (NTSTATUS)hr,
            0,
            0,
            NULL,
            0,
            hr == S_OK ? 1 : 0
        );
    }

    /* 4. WMA Decoder IMFTransform stream limits, count, and unset type error */
    if (match_filter(filter, "wma_decoder_imftransform_stream_limits_and_types")) {
        WG_IMFTransform *mft = NULL;
        HRESULT hr = winegstreamer_create_wma_decoder(NULL, &IID_IMFTransform, (void **)&mft);
        if (hr == S_OK && mft != NULL) {
            DWORD in_min = 0, in_max = 0, out_min = 0, out_max = 0;
            DWORD in_cnt = 0, out_cnt = 0;
            MFT_INPUT_STREAM_INFO in_info;
            HRESULT hr1 = mft->lpVtbl->GetStreamLimits(mft, &in_min, &in_max, &out_min, &out_max);
            HRESULT hr2 = mft->lpVtbl->GetStreamCount(mft, &in_cnt, &out_cnt);
            HRESULT hr3 = mft->lpVtbl->GetInputStreamInfo(mft, 0, &in_info);
            if (hr1 == S_OK && in_min == 1 && in_max == 1 && out_min == 1 && out_max == 1 &&
                hr2 == S_OK && in_cnt == 1 && out_cnt == 1 &&
                hr3 == MF_E_TRANSFORM_TYPE_NOT_SET) {
                hr = S_OK;
            } else {
                hr = E_FAIL;
            }
            mft->lpVtbl->Release(mft);
        } else {
            hr = E_FAIL;
        }

        parity_record_exact(
            "wg_004",
            "wma_decoder_imftransform_stream_limits_and_types",
            "winegstreamer_create_wma_decoder",
            (NTSTATUS)hr,
            0,
            0,
            NULL,
            0,
            hr == S_OK ? 1 : 0
        );
    }

    /* 5. WMA Decoder IMFTransform SetInputType, SetOutputType, and stream sizes */
    if (match_filter(filter, "wma_decoder_imftransform_set_types_and_stream_info")) {
        WG_IMFTransform *mft = NULL;
        DWORD out_cb = 0;
        HRESULT hr = winegstreamer_create_wma_decoder(NULL, &IID_IMFTransform, (void **)&mft);
        if (hr == S_OK && mft != NULL) {
            WG_WAVEFORMATEX in_wfx = {WAVE_FORMAT_WMAUDIO2, 2, 44100, 16000, 2048, 16, 0};
            WG_DMO_MEDIA_TYPE in_mt;
            WG_WAVEFORMATEX out_wfx = {WAVE_FORMAT_IEEE_FLOAT, 2, 44100, 352800, 8, 32, 0};
            WG_DMO_MEDIA_TYPE out_mt;
            MFT_INPUT_STREAM_INFO in_info;
            MFT_OUTPUT_STREAM_INFO out_info;

            memset(&in_mt, 0, sizeof(in_mt));
            in_mt.majortype = MFMediaType_Audio;
            in_mt.subtype = MFAudioFormat_WMAudioV8;
            in_mt.formattype = FORMAT_WaveFormatEx;
            in_mt.cbFormat = sizeof(in_wfx);
            in_mt.pbFormat = (BYTE *)&in_wfx;

            memset(&out_mt, 0, sizeof(out_mt));
            out_mt.majortype = MFMediaType_Audio;
            out_mt.subtype = MFAudioFormat_Float;
            out_mt.formattype = FORMAT_WaveFormatEx;
            out_mt.cbFormat = sizeof(out_wfx);
            out_mt.pbFormat = (BYTE *)&out_wfx;

            if (mft->lpVtbl->SetInputType(mft, 0, &in_mt, 0) == S_OK &&
                mft->lpVtbl->SetOutputType(mft, 0, &out_mt, 0) == S_OK &&
                mft->lpVtbl->GetInputStreamInfo(mft, 0, &in_info) == S_OK &&
                mft->lpVtbl->GetOutputStreamInfo(mft, 0, &out_info) == S_OK &&
                in_info.cbSize == 2048 && out_info.cbSize == 16384) {
                out_cb = out_info.cbSize;
                hr = S_OK;
            } else {
                hr = E_FAIL;
            }
            mft->lpVtbl->Release(mft);
        } else {
            hr = E_FAIL;
        }

        parity_record_exact(
            "wg_005",
            "wma_decoder_imftransform_set_types_and_stream_info",
            "winegstreamer_create_wma_decoder",
            (NTSTATUS)hr,
            0,
            out_cb,
            NULL,
            0,
            hr == S_OK ? 1 : 0
        );
    }

    /* 6. WMA Decoder IMediaObject DMO type enumeration and size info */
    if (match_filter(filter, "wma_decoder_imediaobject_dmo_types_and_size_info")) {
        WG_IMediaObject *dmo = NULL;
        DWORD out_size = 0;
        HRESULT hr = winegstreamer_create_wma_decoder(NULL, &IID_IMediaObject, (void **)&dmo);
        if (hr == S_OK && dmo != NULL) {
            DWORD in_cnt = 0, out_cnt = 0;
            WG_DMO_MEDIA_TYPE mt0, mt4;
            WG_WAVEFORMATEX in_wfx = {WAVE_FORMAT_WMAUDIO2, 2, 44100, 16000, 2048, 16, 0};
            WG_DMO_MEDIA_TYPE in_mt;
            WG_WAVEFORMATEX out_wfx = {WAVE_FORMAT_PCM, 2, 44100, 176400, 4, 16, 0};
            WG_DMO_MEDIA_TYPE out_mt;
            DWORD in_cb = 0, in_look = 0, in_align = 0, out_align = 0;

            memset(&in_mt, 0, sizeof(in_mt));
            in_mt.majortype = MFMediaType_Audio;
            in_mt.subtype = MFAudioFormat_WMAudioV8;
            in_mt.formattype = FORMAT_WaveFormatEx;
            in_mt.cbFormat = sizeof(in_wfx);
            in_mt.pbFormat = (BYTE *)&in_wfx;

            memset(&out_mt, 0, sizeof(out_mt));
            out_mt.majortype = MFMediaType_Audio;
            out_mt.subtype = MFAudioFormat_PCM;
            out_mt.formattype = FORMAT_WaveFormatEx;
            out_mt.cbFormat = sizeof(out_wfx);
            out_mt.pbFormat = (BYTE *)&out_wfx;

            if (dmo->lpVtbl->GetStreamCount(dmo, &in_cnt, &out_cnt) == S_OK &&
                in_cnt == 1 && out_cnt == 1 &&
                dmo->lpVtbl->GetInputType(dmo, 0, 0, &mt0) == S_OK &&
                IsEqualGUID(&mt0.subtype, &MEDIASUBTYPE_MSAUDIO1) &&
                dmo->lpVtbl->GetInputType(dmo, 0, 4, &mt4) == DMO_E_NO_MORE_ITEMS &&
                dmo->lpVtbl->SetInputType(dmo, 0, &in_mt, 0) == S_OK &&
                dmo->lpVtbl->SetOutputType(dmo, 0, &out_mt, 0) == S_OK &&
                dmo->lpVtbl->GetInputSizeInfo(dmo, 0, &in_cb, &in_look, &in_align) == S_OK &&
                dmo->lpVtbl->GetOutputSizeInfo(dmo, 0, &out_size, &out_align) == S_OK &&
                in_cb == 2048 && out_size == 8192) {
                hr = S_OK;
            } else {
                hr = E_FAIL;
            }
            dmo->lpVtbl->Release(dmo);
        } else {
            hr = E_FAIL;
        }

        parity_record_exact(
            "wg_006",
            "wma_decoder_imediaobject_dmo_types_and_size_info",
            "winegstreamer_create_wma_decoder",
            (NTSTATUS)hr,
            0,
            out_size,
            NULL,
            0,
            hr == S_OK ? 1 : 0
        );
    }

    /* 7. WMA Decoder ProcessInput & ProcessOutput roundtrip */
    if (match_filter(filter, "wma_decoder_process_input_and_output_roundtrip")) {
        WG_IMFTransform *mft = NULL;
        HRESULT hr = winegstreamer_create_wma_decoder(NULL, &IID_IMFTransform, (void **)&mft);
        BYTE summary[4] = {0};
        if (hr == S_OK && mft != NULL) {
            WG_WAVEFORMATEX in_wfx = {WAVE_FORMAT_WMAUDIO2, 2, 44100, 16000, 256, 16, 0};
            WG_DMO_MEDIA_TYPE in_mt;
            WG_WAVEFORMATEX out_wfx = {WAVE_FORMAT_PCM, 2, 44100, 176400, 4, 16, 0};
            WG_DMO_MEDIA_TYPE out_mt;
            BYTE in_pkt[256];
            BYTE out_pcm[4096];
            MFT_OUTPUT_DATA_BUFFER out_buf;
            DWORD status = 0;
            size_t j;

            for (j = 0; j < sizeof(in_pkt); j++) {
                in_pkt[j] = (BYTE)((j * 17U + 31U) & 0xFFU);
            }
            memset(out_pcm, 0, sizeof(out_pcm));

            memset(&in_mt, 0, sizeof(in_mt));
            in_mt.majortype = MFMediaType_Audio;
            in_mt.subtype = MFAudioFormat_WMAudioV8;
            in_mt.formattype = FORMAT_WaveFormatEx;
            in_mt.cbFormat = sizeof(in_wfx);
            in_mt.pbFormat = (BYTE *)&in_wfx;

            memset(&out_mt, 0, sizeof(out_mt));
            out_mt.majortype = MFMediaType_Audio;
            out_mt.subtype = MFAudioFormat_PCM;
            out_mt.formattype = FORMAT_WaveFormatEx;
            out_mt.cbFormat = sizeof(out_wfx);
            out_mt.pbFormat = (BYTE *)&out_wfx;

            memset(&out_buf, 0, sizeof(out_buf));
            out_buf.pSample = out_pcm;

            if (mft->lpVtbl->SetInputType(mft, 0, &in_mt, 0) == S_OK &&
                mft->lpVtbl->SetOutputType(mft, 0, &out_mt, 0) == S_OK &&
                mft->lpVtbl->ProcessMessage(mft, MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0) == S_OK &&
                mft->lpVtbl->ProcessInput(mft, 0, in_pkt, 0) == S_OK &&
                mft->lpVtbl->ProcessOutput(mft, 0, 1, &out_buf, &status) == S_OK) {
                BOOL non_zero = FALSE;
                for (j = 0; j < sizeof(out_pcm); j++) {
                    if (out_pcm[j] != 0) {
                        non_zero = TRUE;
                        break;
                    }
                }
                if (non_zero) {
                    summary[0] = 1;
                    summary[1] = 1;
                    summary[2] = 1;
                    summary[3] = 1;
                    hr = S_OK;
                } else {
                    hr = E_FAIL;
                }
            } else {
                hr = E_FAIL;
            }
            mft->lpVtbl->Release(mft);
        } else {
            hr = E_FAIL;
        }

        parity_record_roundtrip(
            "wg_007",
            "wma_decoder_process_input_and_output_roundtrip",
            "winegstreamer_create_wma_decoder",
            (NTSTATUS)hr,
            0,
            sizeof(summary),
            summary,
            sizeof(summary),
            hr == S_OK ? 1 : 0
        );
    }

    /* 8. macOS AudioToolbox & CoreMedia direct packet decode (16-bit PCM & 32-bit Float) */
    if (match_filter(filter, "macos_audiotoolbox_pcm_and_float_decode")) {
        BOOL at_ready = FALSE, vt_ready = FALSE, cm_ready = FALSE;
        WG_WAVEFORMATEX wfx = {WAVE_FORMAT_WMAUDIO2, 2, 44100, 16000, 256, 16, 0};
        BYTE pkt[256];
        BYTE pcm16[4096];
        BYTE flt32[8192];
        DWORD written_pcm = 0, written_flt = 0;
        size_t j;
        HRESULT hr_q = WineGStreamerQueryMacOSBackend(&at_ready, &vt_ready, &cm_ready);
        HRESULT hr1, hr2, hr = E_FAIL;

        for (j = 0; j < sizeof(pkt); j++) {
            pkt[j] = (BYTE)((j * 13U + 7U) & 0xFFU);
        }
        hr1 = WineGStreamerAudioToolboxDecodePacket(&wfx, pkt, sizeof(pkt), FALSE, pcm16, sizeof(pcm16), &written_pcm);
        hr2 = WineGStreamerAudioToolboxDecodePacket(&wfx, pkt, sizeof(pkt), TRUE, flt32, sizeof(flt32), &written_flt);
        if (hr_q == S_OK && at_ready && vt_ready && cm_ready &&
            hr1 == S_OK && written_pcm == 4096 &&
            hr2 == S_OK && written_flt == 8192) {
            hr = S_OK;
        }

        parity_record_exact(
            "wg_008",
            "macos_audiotoolbox_pcm_and_float_decode",
            "WineGStreamerAudioToolboxDecodePacket",
            (NTSTATUS)hr,
            0,
            written_pcm + written_flt,
            NULL,
            0,
            hr == S_OK ? 1 : 0
        );
    }

    /* 9. winegstreamer_create_video_decoder IMFTransform creation and output types */
    if (match_filter(filter, "video_decoder_create_and_stream_info")) {
        WG_IMFTransform *vdec = NULL;
        HRESULT hr_null = winegstreamer_create_video_decoder(NULL);
        HRESULT hr = winegstreamer_create_video_decoder(&vdec);
        if (hr_null == E_POINTER && hr == S_OK && vdec != NULL) {
            DWORD in_cnt = 0, out_cnt = 0;
            GUID out_subtype;
            void *type_ptr = &out_subtype;
            if (vdec->lpVtbl->GetStreamCount(vdec, &in_cnt, &out_cnt) == S_OK &&
                in_cnt == 1 && out_cnt == 1 &&
                vdec->lpVtbl->GetOutputAvailableType(vdec, 0, 0, &type_ptr) == S_OK &&
                vdec->lpVtbl->GetOutputAvailableType(vdec, 0, 16, &type_ptr) == MF_E_NO_MORE_TYPES) {
                hr = S_OK;
            } else {
                hr = E_FAIL;
            }
            vdec->lpVtbl->Release(vdec);
        } else {
            hr = E_FAIL;
        }

        parity_record_exact(
            "wg_009",
            "video_decoder_create_and_stream_info",
            "winegstreamer_create_video_decoder",
            (NTSTATUS)hr,
            0,
            0,
            NULL,
            0,
            hr == S_OK ? 1 : 0
        );
    }

    /* 10. macOS VideoToolbox NV12 frame decode */
    if (match_filter(filter, "macos_videotoolbox_decode_frame_nv12")) {
        BYTE comp[128];
        BYTE nv12[64 * 64 * 3 / 2];
        DWORD written = 0;
        size_t j;
        HRESULT hr;

        for (j = 0; j < sizeof(comp); j++) {
            comp[j] = (BYTE)((j * 29U + 5U) & 0xFFU);
        }
        memset(nv12, 0, sizeof(nv12));
        hr = WineGStreamerVideoToolboxDecodeFrame(0x30355649U /* IV50 */, 64, 64, comp, sizeof(comp), nv12, sizeof(nv12), &written);
        if (hr == S_OK && written == sizeof(nv12) && nv12[0] != 0 && nv12[64 * 64] != 0) {
            hr = S_OK;
        } else {
            hr = E_FAIL;
        }

        parity_record_exact(
            "wg_010",
            "macos_videotoolbox_decode_frame_nv12",
            "WineGStreamerVideoToolboxDecodeFrame",
            (NTSTATUS)hr,
            0,
            written,
            NULL,
            0,
            hr == S_OK ? 1 : 0
        );
    }

    /* 11. winegstreamer_create_wm_sync_reader creation and stream selection */
    if (match_filter(filter, "wm_sync_reader_create_and_stream_selection")) {
        WG_IUnknown *unk = NULL;
        WG_IWMSyncReader2 *reader = NULL;
        HRESULT hr_null = winegstreamer_create_wm_sync_reader(NULL, NULL);
        HRESULT hr = winegstreamer_create_wm_sync_reader(NULL, (void **)&unk);
        if (hr_null == E_POINTER && hr == S_OK && unk != NULL) {
            if (IUnknown_QueryInterface(unk, &IID_IWMSyncReader2, (void **)&reader) == S_OK && reader != NULL) {
                WORD stream_num = 1;
                int sel_in = 2 /* WMT_ON */, sel_out = 0;
                BOOL comp_out = FALSE;
                DWORD out_cnt = 0;
                if (reader->lpVtbl->GetOutputCount(reader, &out_cnt) == S_OK && out_cnt >= 1 &&
                    reader->lpVtbl->SetStreamsSelected(reader, 1, &stream_num, &sel_in) == S_OK &&
                    reader->lpVtbl->GetStreamSelected(reader, 1, &sel_out) == S_OK && sel_out == 2 &&
                    reader->lpVtbl->SetReadStreamSamples(reader, 1, TRUE) == S_OK &&
                    reader->lpVtbl->GetReadStreamSamples(reader, 1, &comp_out) == S_OK && comp_out == TRUE) {
                    hr = S_OK;
                } else {
                    hr = E_FAIL;
                }
                reader->lpVtbl->Release(reader);
            } else {
                hr = E_FAIL;
            }
            IUnknown_Release(unk);
        } else {
            hr = E_FAIL;
        }

        parity_record_exact(
            "wg_011",
            "wm_sync_reader_create_and_stream_selection",
            "winegstreamer_create_wm_sync_reader",
            (NTSTATUS)hr,
            0,
            0,
            NULL,
            0,
            hr == S_OK ? 1 : 0
        );
    }

    /* 12. IWMSyncReader2 OpenStream, GetNextSample, and Close */
    if (match_filter(filter, "wm_sync_reader_open_stream_and_get_next_sample")) {
        WG_IUnknown *unk = NULL;
        WG_IWMSyncReader2 *reader = NULL;
        HRESULT hr = winegstreamer_create_wm_sync_reader(NULL, (void **)&unk);
        if (hr == S_OK && unk != NULL) {
            if (IUnknown_QueryInterface(unk, &IID_IWMSyncReader2, (void **)&reader) == S_OK && reader != NULL) {
                BYTE dummy_stream[64] = {0x30, 0x26, 0xB2, 0x75};
                void *sample = NULL;
                ULONGLONG pts = 0, dur = 0;
                DWORD flags = 0, out_num = 0;
                WORD stm_num = 0;
                if (reader->lpVtbl->OpenStream(reader, dummy_stream) == S_OK &&
                    reader->lpVtbl->GetNextSample(reader, 1, &sample, &pts, &dur, &flags, &out_num, &stm_num) == S_OK &&
                    dur > 0 && stm_num == 1 &&
                    reader->lpVtbl->Close(reader) == S_OK) {
                    hr = S_OK;
                } else {
                    hr = E_FAIL;
                }
                reader->lpVtbl->Release(reader);
            } else {
                hr = E_FAIL;
            }
            IUnknown_Release(unk);
        } else {
            hr = E_FAIL;
        }

        parity_record_exact(
            "wg_012",
            "wm_sync_reader_open_stream_and_get_next_sample",
            "winegstreamer_create_wm_sync_reader",
            (NTSTATUS)hr,
            0,
            0,
            NULL,
            0,
            hr == S_OK ? 1 : 0
        );
    }

    parity_end_document();
    return 0;
}

/*
 * Upstream Wine Winetest Anti-Regression Suite for winegstreamer.dll
 * (wmadmod.dll WMA Decoder DMO/MFT, Video Decoder MFT, WM Sync Reader,
 *  and macOS AudioToolbox / VideoToolbox / CoreMedia Hardware Media Bridge)
 *
 * Clean-room implementation aligned with upstream Wine dlls/wmadmod/tests/wmadmod.c
 * and dlls/winegstreamer exports.
 *
 * Copyright 2026 Spumante Contributors
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include <stddef.h>
#include "../../include/wine/test.h"
#include "../../../dlls/winegstreamer/winegstreamer.h"

/* Outer IUnknown for COM aggregation verification */
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

static void test_com_exports_and_class_factory(void)
{
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
    WG_IClassFactory *cf = NULL;
    size_t i;
    HRESULT hr;

    hr = DllCanUnloadNow();
    ok(hr == S_FALSE, "Expected DllCanUnloadNow S_FALSE, got 0x%08x\n", (unsigned)hr);

    hr = DllRegisterServer();
    ok(hr == S_OK, "DllRegisterServer failed: 0x%08x\n", (unsigned)hr);

    hr = DllUnregisterServer();
    ok(hr == S_OK, "DllUnregisterServer failed: 0x%08x\n", (unsigned)hr);

    hr = DllGetClassObject(&CLSID_WMADecMediaObject, &IID_IClassFactory, NULL);
    ok(hr == E_INVALIDARG, "Expected E_INVALIDARG on NULL out, got 0x%08x\n", (unsigned)hr);

    hr = DllGetClassObject(&clsid_unknown, &IID_IClassFactory, (void **)&cf);
    ok(hr == CLASS_E_CLASSNOTAVAILABLE && cf == NULL,
       "Expected CLASS_E_CLASSNOTAVAILABLE, got 0x%08x\n", (unsigned)hr);

    hr = DllGetClassObject(&CLSID_WMADecMediaObject, &IID_IMediaObject, (void **)&cf);
    ok(hr == E_NOINTERFACE && cf == NULL, "Expected E_NOINTERFACE, got 0x%08x\n", (unsigned)hr);

    for (i = 0; i < ARRAY_SIZE(clsids); i++) {
        WG_IUnknown *inst = NULL;
        hr = DllGetClassObject(clsids[i], &IID_IClassFactory, (void **)&cf);
        ok(hr == S_OK && cf != NULL, "DllGetClassObject(%u) failed: 0x%08x\n", (unsigned)i, (unsigned)hr);
        if (cf) {
            hr = IClassFactory_LockServer(cf, TRUE);
            ok(hr == S_OK, "LockServer(TRUE) failed: 0x%08x\n", (unsigned)hr);
            hr = IClassFactory_LockServer(cf, FALSE);
            ok(hr == S_OK, "LockServer(FALSE) failed: 0x%08x\n", (unsigned)hr);

            hr = IClassFactory_CreateInstance(cf, NULL, &IID_IUnknown, (void **)&inst);
            ok(hr == S_OK && inst != NULL, "CreateInstance(%u) failed: 0x%08x\n", (unsigned)i, (unsigned)hr);
            if (inst) {
                IUnknown_Release(inst);
            }
            IClassFactory_Release(cf);
            cf = NULL;
        }
    }
}

static void test_wma_decoder_interfaces_and_aggregation(void)
{
    test_outer_unk_t outer = {{&test_outer_vtbl}, 1, FALSE};
    WG_IUnknown *inner = NULL;
    WG_IMFTransform *mft = NULL;
    WG_IMediaObject *dmo = NULL;
    WG_IPropertyBag *bag = NULL;
    HRESULT hr;

    hr = winegstreamer_create_wma_decoder(NULL, &IID_IUnknown, NULL);
    ok(hr == E_POINTER, "Expected E_POINTER, got 0x%08x\n", (unsigned)hr);

    hr = winegstreamer_create_wma_decoder(&outer.IUnknown_iface, &IID_IMFTransform, (void **)&mft);
    ok(hr == E_NOINTERFACE || hr == CLASS_E_NOAGGREGATION,
       "Expected E_NOINTERFACE or CLASS_E_NOAGGREGATION on non-IUnknown aggregation, got 0x%08x\n",
       (unsigned)hr);

    hr = winegstreamer_create_wma_decoder(&outer.IUnknown_iface, &IID_IUnknown, (void **)&inner);
    ok(hr == S_OK && inner != NULL, "Aggregated winegstreamer_create_wma_decoder failed: 0x%08x\n", (unsigned)hr);
    if (inner) {
        WG_IUnknown *tmp = NULL;

        hr = IUnknown_QueryInterface(inner, &IID_IMFTransform, (void **)&mft);
        ok(hr == S_OK && mft != NULL, "Inner QI(IMFTransform) failed: 0x%08x\n", (unsigned)hr);

        hr = IUnknown_QueryInterface(inner, &IID_IMediaObject, (void **)&dmo);
        ok(hr == S_OK && dmo != NULL, "Inner QI(IMediaObject) failed: 0x%08x\n", (unsigned)hr);

        hr = IUnknown_QueryInterface(inner, &IID_IPropertyBag, (void **)&bag);
        ok(hr == S_OK && bag != NULL, "Inner QI(IPropertyBag) failed: 0x%08x\n", (unsigned)hr);

        if (mft) {
            hr = mft->lpVtbl->QueryInterface(mft, &IID_IUnknown, (void **)&tmp);
            ok(hr == S_OK && outer.outer_queried && tmp == &outer.IUnknown_iface,
               "Delegated QI(IUnknown) on IMFTransform failed: hr=0x%08x queried=%d\n",
               (unsigned)hr, outer.outer_queried);
        }

        IUnknown_Release(inner);
    }
}

static void test_wma_decoder_mft_and_dmo_streaming(void)
{
    WG_IMFTransform *mft = NULL;
    WG_IMediaObject *dmo = NULL;
    DWORD in_min = 0, in_max = 0, out_min = 0, out_max = 0;
    DWORD in_cnt = 0, out_cnt = 0;
    DWORD in_id = 0, out_id = 0;
    MFT_INPUT_STREAM_INFO in_info;
    MFT_OUTPUT_STREAM_INFO out_info;
    WG_WAVEFORMATEX in_wfx = {WAVE_FORMAT_WMAUDIO2, 2, 44100, 16000, 2048, 16, 0};
    WG_DMO_MEDIA_TYPE in_mt;
    WG_WAVEFORMATEX out_wfx = {WAVE_FORMAT_IEEE_FLOAT, 2, 44100, 352800, 8, 32, 0};
    WG_DMO_MEDIA_TYPE out_mt;
    BYTE in_pkt[2048];
    BYTE out_pcm[16384];
    MFT_OUTPUT_DATA_BUFFER out_buf;
    DWORD status = 0;
    size_t i;
    HRESULT hr;

    for (i = 0; i < sizeof(in_pkt); i++) {
        in_pkt[i] = (BYTE)((i * 17U + 31U) & 0xFFU);
    }
    memset(out_pcm, 0, sizeof(out_pcm));

    hr = winegstreamer_create_wma_decoder(NULL, &IID_IMFTransform, (void **)&mft);
    ok(hr == S_OK && mft != NULL, "winegstreamer_create_wma_decoder(IMFTransform) failed: 0x%08x\n", (unsigned)hr);
    if (mft) {
        hr = mft->lpVtbl->GetStreamLimits(mft, &in_min, &in_max, &out_min, &out_max);
        ok(hr == S_OK && in_min == 1 && in_max == 1 && out_min == 1 && out_max == 1,
           "GetStreamLimits failed: 0x%08x\n", (unsigned)hr);

        hr = mft->lpVtbl->GetStreamCount(mft, &in_cnt, &out_cnt);
        ok(hr == S_OK && in_cnt == 1 && out_cnt == 1, "GetStreamCount failed: 0x%08x\n", (unsigned)hr);

        hr = mft->lpVtbl->GetStreamIDs(mft, 1, &in_id, 1, &out_id);
        ok(hr == E_NOTIMPL, "Expected E_NOTIMPL from GetStreamIDs, got 0x%08x\n", (unsigned)hr);

        hr = mft->lpVtbl->GetInputStreamInfo(mft, 0, &in_info);
        ok(hr == MF_E_TRANSFORM_TYPE_NOT_SET,
           "Expected MF_E_TRANSFORM_TYPE_NOT_SET before SetInputType, got 0x%08x\n", (unsigned)hr);

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

        hr = mft->lpVtbl->SetInputType(mft, 0, &in_mt, 0);
        ok(hr == S_OK, "SetInputType(WMAv2) failed: 0x%08x\n", (unsigned)hr);

        hr = mft->lpVtbl->SetOutputType(mft, 0, &out_mt, 0);
        ok(hr == S_OK, "SetOutputType(Float) failed: 0x%08x\n", (unsigned)hr);

        hr = mft->lpVtbl->GetInputStreamInfo(mft, 0, &in_info);
        ok(hr == S_OK && in_info.cbSize == 2048, "GetInputStreamInfo failed: hr=0x%08x cbSize=%u\n",
           (unsigned)hr, (unsigned)in_info.cbSize);

        hr = mft->lpVtbl->GetOutputStreamInfo(mft, 0, &out_info);
        ok(hr == S_OK && out_info.cbSize == 16384, "GetOutputStreamInfo failed: hr=0x%08x cbSize=%u\n",
           (unsigned)hr, (unsigned)out_info.cbSize);

        hr = mft->lpVtbl->ProcessMessage(mft, MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0);
        ok(hr == S_OK, "ProcessMessage(BEGIN_STREAMING) failed: 0x%08x\n", (unsigned)hr);

        hr = mft->lpVtbl->ProcessInput(mft, 0, in_pkt, 0);
        ok(hr == S_OK, "ProcessInput failed: 0x%08x\n", (unsigned)hr);

        memset(&out_buf, 0, sizeof(out_buf));
        out_buf.pSample = out_pcm;
        hr = mft->lpVtbl->ProcessOutput(mft, 0, 1, &out_buf, &status);
        ok(hr == S_OK, "ProcessOutput failed: 0x%08x\n", (unsigned)hr);

        mft->lpVtbl->Release(mft);
    }

    hr = winegstreamer_create_wma_decoder(NULL, &IID_IMediaObject, (void **)&dmo);
    ok(hr == S_OK && dmo != NULL, "winegstreamer_create_wma_decoder(IMediaObject) failed: 0x%08x\n", (unsigned)hr);
    if (dmo) {
        WG_DMO_MEDIA_TYPE mt0, mt4;
        DWORD in_cb = 0, in_look = 0, in_align = 0, out_cb = 0, out_align = 0;

        hr = dmo->lpVtbl->GetInputType(dmo, 0, 0, &mt0);
        ok(hr == S_OK && IsEqualGUID(&mt0.subtype, &MEDIASUBTYPE_MSAUDIO1),
           "DMO GetInputType(0) failed: 0x%08x\n", (unsigned)hr);

        hr = dmo->lpVtbl->GetInputType(dmo, 0, 4, &mt4);
        ok(hr == DMO_E_NO_MORE_ITEMS, "Expected DMO_E_NO_MORE_ITEMS, got 0x%08x\n", (unsigned)hr);

        hr = dmo->lpVtbl->SetInputType(dmo, 0, &in_mt, 0);
        ok(hr == S_OK, "DMO SetInputType failed: 0x%08x\n", (unsigned)hr);

        hr = dmo->lpVtbl->SetOutputType(dmo, 0, &out_mt, 0);
        ok(hr == S_OK, "DMO SetOutputType failed: 0x%08x\n", (unsigned)hr);

        hr = dmo->lpVtbl->GetInputSizeInfo(dmo, 0, &in_cb, &in_look, &in_align);
        ok(hr == S_OK && in_cb == 2048, "DMO GetInputSizeInfo failed: hr=0x%08x cb=%u\n",
           (unsigned)hr, (unsigned)in_cb);

        hr = dmo->lpVtbl->GetOutputSizeInfo(dmo, 0, &out_cb, &out_align);
        ok(hr == S_OK && out_cb == 16384, "DMO GetOutputSizeInfo failed: hr=0x%08x cb=%u\n",
           (unsigned)hr, (unsigned)out_cb);

        dmo->lpVtbl->Release(dmo);
    }
}

static void test_video_decoder_and_wm_sync_reader(void)
{
    WG_IMFTransform *vdec = NULL;
    WG_IUnknown *unk = NULL;
    WG_IWMSyncReader2 *reader = NULL;
    HRESULT hr;

    hr = winegstreamer_create_video_decoder(NULL);
    ok(hr == E_POINTER, "Expected E_POINTER from winegstreamer_create_video_decoder(NULL), got 0x%08x\n",
       (unsigned)hr);

    hr = winegstreamer_create_video_decoder(&vdec);
    ok(hr == S_OK && vdec != NULL, "winegstreamer_create_video_decoder failed: 0x%08x\n", (unsigned)hr);
    if (vdec) {
        DWORD in_cnt = 0, out_cnt = 0;
        GUID out_subtype;
        void *type_ptr = &out_subtype;

        hr = vdec->lpVtbl->GetStreamCount(vdec, &in_cnt, &out_cnt);
        ok(hr == S_OK && in_cnt == 1 && out_cnt == 1, "Video GetStreamCount failed: 0x%08x\n", (unsigned)hr);

        hr = vdec->lpVtbl->GetOutputAvailableType(vdec, 0, 0, &type_ptr);
        ok(hr == S_OK, "Video GetOutputAvailableType(0) failed: 0x%08x\n", (unsigned)hr);

        hr = vdec->lpVtbl->GetOutputAvailableType(vdec, 0, 16, &type_ptr);
        ok(hr == MF_E_NO_MORE_TYPES, "Expected MF_E_NO_MORE_TYPES, got 0x%08x\n", (unsigned)hr);

        vdec->lpVtbl->Release(vdec);
    }

    hr = winegstreamer_create_wm_sync_reader(NULL, NULL);
    ok(hr == E_POINTER, "Expected E_POINTER from winegstreamer_create_wm_sync_reader(NULL), got 0x%08x\n",
       (unsigned)hr);

    hr = winegstreamer_create_wm_sync_reader(NULL, (void **)&unk);
    ok(hr == S_OK && unk != NULL, "winegstreamer_create_wm_sync_reader failed: 0x%08x\n", (unsigned)hr);
    if (unk) {
        hr = IUnknown_QueryInterface(unk, &IID_IWMSyncReader2, (void **)&reader);
        ok(hr == S_OK && reader != NULL, "QI(IWMSyncReader2) failed: 0x%08x\n", (unsigned)hr);
        if (reader) {
            BYTE dummy_stream[64] = {0x30, 0x26, 0xB2, 0x75};
            void *sample = NULL;
            ULONGLONG pts = 0, dur = 0;
            DWORD flags = 0, out_num = 0, out_cnt = 0;
            WORD stm_num = 0;

            hr = reader->lpVtbl->GetOutputCount(reader, &out_cnt);
            ok(hr == S_OK && out_cnt >= 1, "GetOutputCount failed: 0x%08x\n", (unsigned)hr);

            hr = reader->lpVtbl->OpenStream(reader, dummy_stream);
            ok(hr == S_OK, "OpenStream failed: 0x%08x\n", (unsigned)hr);

            hr = reader->lpVtbl->GetNextSample(reader, 1, &sample, &pts, &dur, &flags, &out_num, &stm_num);
            ok(hr == S_OK && dur > 0 && stm_num == 1, "GetNextSample failed: 0x%08x\n", (unsigned)hr);

            hr = reader->lpVtbl->Close(reader);
            ok(hr == S_OK, "Close failed: 0x%08x\n", (unsigned)hr);

            reader->lpVtbl->Release(reader);
        }
        IUnknown_Release(unk);
    }
}

static void test_macos_hardware_media_bridge(void)
{
    BOOL at_ready = FALSE, vt_ready = FALSE, cm_ready = FALSE;
    WG_WAVEFORMATEX wfx = {WAVE_FORMAT_WMAUDIO2, 2, 44100, 16000, 256, 16, 0};
    BYTE pkt[256];
    BYTE pcm16[4096];
    BYTE flt32[8192];
    BYTE comp[128];
    BYTE nv12[64 * 64 * 3 / 2];
    DWORD written_pcm = 0, written_flt = 0, written_nv12 = 0;
    size_t i;
    HRESULT hr;

    for (i = 0; i < sizeof(pkt); i++) {
        pkt[i] = (BYTE)((i * 13U + 7U) & 0xFFU);
    }
    for (i = 0; i < sizeof(comp); i++) {
        comp[i] = (BYTE)((i * 29U + 5U) & 0xFFU);
    }

    hr = WineGStreamerQueryMacOSBackend(NULL, &vt_ready, &cm_ready);
    ok(hr == E_POINTER, "Expected E_POINTER, got 0x%08x\n", (unsigned)hr);

    hr = WineGStreamerQueryMacOSBackend(&at_ready, &vt_ready, &cm_ready);
    todo_wine ok(hr == S_OK && at_ready == TRUE && vt_ready == TRUE && cm_ready == TRUE,
                 "Expected macOS AudioToolbox/VideoToolbox/CoreMedia backend ready: hr=0x%08x at=%d vt=%d cm=%d\n",
                 (unsigned)hr, at_ready, vt_ready, cm_ready);

    hr = WineGStreamerAudioToolboxDecodePacket(&wfx, pkt, sizeof(pkt), FALSE, pcm16, sizeof(pcm16), &written_pcm);
    ok(hr == S_OK && written_pcm == 4096, "AudioToolbox PCM16 decode failed: hr=0x%08x written=%u\n",
       (unsigned)hr, (unsigned)written_pcm);

    hr = WineGStreamerAudioToolboxDecodePacket(&wfx, pkt, sizeof(pkt), TRUE, flt32, sizeof(flt32), &written_flt);
    ok(hr == S_OK && written_flt == 8192, "AudioToolbox Float32 decode failed: hr=0x%08x written=%u\n",
       (unsigned)hr, (unsigned)written_flt);

    hr = WineGStreamerVideoToolboxDecodeFrame(
        0x30355649U, 64, 64, comp, sizeof(comp), nv12, sizeof(nv12), &written_nv12);
    ok(hr == S_OK && written_nv12 == sizeof(nv12), "VideoToolbox NV12 decode failed: hr=0x%08x written=%u\n",
       (unsigned)hr, (unsigned)written_nv12);
}

START_TEST(winegstreamer)
{
    WINETEST_RUN_SUBTEST(test_com_exports_and_class_factory);
    WINETEST_RUN_SUBTEST(test_wma_decoder_interfaces_and_aggregation);
    WINETEST_RUN_SUBTEST(test_wma_decoder_mft_and_dmo_streaming);
    WINETEST_RUN_SUBTEST(test_video_decoder_and_wm_sync_reader);
    WINETEST_RUN_SUBTEST(test_macos_hardware_media_bridge);
}

/*
 * Differential Conformance Test for ARM64EC / ARM64X Loader Edge Cases in dlls/ntdll
 * (WineConf 2025 ARM64 Roadmap):
 *   1. ARM64X Dynamic Value Relocation Table (DVRT v1 & v2: ZEROFILL, VALUE, DELTA)
 *   2. NtReadFile PE header relocation view for system32 DLLs (AMD64/ARM64EC vs ARM64/non-system32)
 *   3. TSC registry frequency rescaling (~MHz & NtQueryPerformanceCounter 128-bit tick scaling)
 *   4. On-disk JIT code caching (WJTC integrity-checked ARM64 translation cache & invalidation)
 *
 * Copyright 2026 Wine Parity Contributors
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "../../include/parity_test.h"
#include "../../../dlls/ntdll/ntdll.h"

static void put_u32_le(uint8_t *dst, uint32_t v)
{
    dst[0] = (uint8_t)(v & 0xffU);
    dst[1] = (uint8_t)((v >> 8) & 0xffU);
    dst[2] = (uint8_t)((v >> 16) & 0xffU);
    dst[3] = (uint8_t)((v >> 24) & 0xffU);
}

static uint16_t get_u16_le(const uint8_t *src)
{
    return (uint16_t)((uint16_t)src[0] | ((uint16_t)src[1] << 8));
}

static uint32_t get_u32_le(const uint8_t *src)
{
    return (uint32_t)src[0] |
           ((uint32_t)src[1] << 8) |
           ((uint32_t)src[2] << 16) |
           ((uint32_t)src[3] << 24);
}

static uint64_t get_u64_le(const uint8_t *src)
{
    return (uint64_t)get_u32_le(src) | ((uint64_t)get_u32_le(src + 4) << 32);
}

int main(void)
{
    NTSTATUS status;

    parity_begin_document("ntdll", "test_arm64x_loader_parity", "arm64", "wine_macos");

    /* =========================================================================
     * Case 1: ARM64X DVRT v1 dynamic relocations (VALUE, DELTA, ZEROFILL)
     * Builds a synthetic ARM64X hybrid PE image (Machine=0xAA64, Entry=0x1000,
     * BaseReloc RVA=0x500/Size=0x10, CHPEMetadataPointer=0) and applies DVRT v1
     * relocations to morph the PE header into its AMD64/ARM64EC view
     * (Machine=0x8664, Entry=0x2000, BaseReloc=0/0, CHPEMetadataPointer=0x180003000).
     * ========================================================================= */
    {
        uint8_t image_buf[4096];
        SIZE_T image_size = 0;
        uint16_t pre_machine = 0;
        uint16_t post_machine = 0;
        uint32_t post_entry_rva = 0;
        uint32_t post_basereloc_rva = 0xFFFFFFFFU;
        uint64_t post_chpe_ptr = 0;
        uint8_t digest[20];

        memset(image_buf, 0, sizeof(image_buf));
        memset(digest, 0, sizeof(digest));

        status = NtdllArm64xBuildSyntheticImage(image_buf, sizeof(image_buf), 1U, &image_size);
        if (status == STATUS_SUCCESS) {
            pre_machine = get_u16_le(image_buf + 0x84);
            status = NtdllArm64xApplyDynamicRelocations(image_buf, image_size);
        }
        if (status == STATUS_SUCCESS) {
            post_machine = get_u16_le(image_buf + 0x84);
            post_entry_rva = get_u32_le(image_buf + 0xA8);
            post_basereloc_rva = get_u32_le(image_buf + 0x130);
            post_chpe_ptr = get_u64_le(image_buf + 0x2C8);

            put_u32_le(digest + 0, (uint32_t)pre_machine);
            put_u32_le(digest + 4, (uint32_t)post_machine);
            put_u32_le(digest + 8, post_entry_rva);
            put_u32_le(digest + 12, post_basereloc_rva);
            put_u32_le(digest + 16, (uint32_t)(post_chpe_ptr & 0xFFFFFFFFULL));
        }

        ok(status == STATUS_SUCCESS && post_machine == IMAGE_FILE_MACHINE_AMD64 && post_entry_rva == 0x2000U,
           "Expected DVRT v1 ARM64X relocation to morph Machine=0x8664 Entry=0x2000, got status=0x%08x machine=0x%04x entry=0x%x\n",
           (unsigned int)status, (unsigned int)post_machine, (unsigned int)post_entry_rva);

        parity_record_exact(
            "arm64x_dvrt_v1_dynamic_relocations",
            "Arm64x_DvRtV1_DynamicRelocations",
            "NtdllArm64xApplyDynamicRelocations",
            status,
            GetLastError(),
            (ULONG)post_machine,
            digest,
            status == STATUS_SUCCESS ? sizeof(digest) : 0,
            status == STATUS_SUCCESS ? 1 : 0);
    }

    /* =========================================================================
     * Case 2: ARM64X DVRT v2 dynamic relocations & negative 8-byte-scaled DELTA
     * Verifies IMAGE_DYNAMIC_RELOCATION64_V2 header parsing and negative 8-byte
     * scaled DELTA fixup (arg=3: scale=8, sign=-1) reducing 0x5000 by 0x400 to 0x4C00.
     * ========================================================================= */
    {
        uint8_t image_buf[4096];
        SIZE_T image_size = 0;
        uint16_t post_machine = 0;
        uint32_t post_entry_rva = 0;
        uint32_t post_neg_delta_val = 0;
        uint32_t post_basereloc_size = 0xFFFFFFFFU;
        uint64_t post_chpe_ptr = 0;
        uint8_t digest[20];

        memset(image_buf, 0, sizeof(image_buf));
        memset(digest, 0, sizeof(digest));

        status = NtdllArm64xBuildSyntheticImage(image_buf, sizeof(image_buf), 2U, &image_size);
        if (status == STATUS_SUCCESS) {
            status = NtdllArm64xApplyDynamicRelocations(image_buf, image_size);
        }
        if (status == STATUS_SUCCESS) {
            post_machine = get_u16_le(image_buf + 0x84);
            post_entry_rva = get_u32_le(image_buf + 0xA8);
            post_neg_delta_val = get_u32_le(image_buf + 0x2E0);
            post_basereloc_size = get_u32_le(image_buf + 0x134);
            post_chpe_ptr = get_u64_le(image_buf + 0x2C8);

            put_u32_le(digest + 0, (uint32_t)post_machine);
            put_u32_le(digest + 4, post_entry_rva);
            put_u32_le(digest + 8, post_neg_delta_val);
            put_u32_le(digest + 12, post_basereloc_size);
            put_u32_le(digest + 16, (uint32_t)(post_chpe_ptr >> 32));
        }

        ok(status == STATUS_SUCCESS && post_machine == IMAGE_FILE_MACHINE_AMD64 && post_neg_delta_val == 0x4C00U,
           "Expected DVRT v2 negative delta fixup 0x4C00, got status=0x%08x val=0x%x\n",
           (unsigned int)status, (unsigned int)post_neg_delta_val);

        parity_record_exact(
            "arm64x_dvrt_v2_dynamic_relocations_and_negative_delta",
            "Arm64x_DvRtV2_DynamicRelocations_And_NegativeDelta",
            "NtdllArm64xApplyDynamicRelocations",
            status,
            GetLastError(),
            2U,
            digest,
            status == STATUS_SUCCESS ? sizeof(digest) : 0,
            status == STATUS_SUCCESS ? 1 : 0);
    }

    /* =========================================================================
     * Case 3: NtReadFile PE header relocation view for system32 DLLs
     * When an AMD64 (0x8664) or ARM64EC (0xA641) caller reads a system32 ARM64X
     * DLL via NtReadFile (either from offset 0 or a mid-header offset like 0x80),
     * it must observe the DVRT-relocated Machine=0x8664 and Entry=0x2000 view.
     * ========================================================================= */
    {
        uint8_t raw_image[4096];
        uint8_t read_amd64[512];
        uint8_t read_ec[64];
        SIZE_T image_size = 0;
        ULONG bytes_read_amd64 = 0;
        ULONG bytes_read_ec = 0;
        NTSTATUS st_ec = STATUS_UNSUCCESSFUL;
        static const WCHAR path_sys32_k32[] = {
            'C',':','\\','W','i','n','d','o','w','s','\\','S','y','s','t','e','m','3','2','\\','k','e','r','n','e','l','3','2','.','d','l','l',0
        };
        static const WCHAR path_nt_sys32_ntdll[] = {
            '\\','?','?','\\','C',':','\\','W','i','n','d','o','w','s','\\','s','y','s','t','e','m','3','2','\\','n','t','d','l','l','.','d','l','l',0
        };
        uint8_t digest[20];

        memset(raw_image, 0, sizeof(raw_image));
        memset(read_amd64, 0, sizeof(read_amd64));
        memset(read_ec, 0, sizeof(read_ec));
        memset(digest, 0, sizeof(digest));

        status = NtdllArm64xBuildSyntheticImage(raw_image, sizeof(raw_image), 1U, &image_size);
        if (status == STATUS_SUCCESS) {
            status = NtReadFileArm64xPeView(
                path_sys32_k32, IMAGE_FILE_MACHINE_AMD64,
                raw_image, image_size, 0ULL,
                read_amd64, sizeof(read_amd64), &bytes_read_amd64);
        }
        if (status == STATUS_SUCCESS) {
            st_ec = NtReadFileArm64xPeView(
                path_nt_sys32_ntdll, IMAGE_FILE_MACHINE_ARM64EC,
                raw_image, image_size, 0x80ULL,
                read_ec, sizeof(read_ec), &bytes_read_ec);
            if (st_ec != STATUS_SUCCESS) {
                status = st_ec;
            }
        }
        if (status == STATUS_SUCCESS) {
            uint16_t amd64_machine = get_u16_le(read_amd64 + 0x84);
            uint32_t amd64_entry = get_u32_le(read_amd64 + 0xA8);
            uint16_t ec_machine = get_u16_le(read_ec + 0x04);
            uint32_t ec_entry = get_u32_le(read_ec + 0x28);

            put_u32_le(digest + 0, (uint32_t)amd64_machine);
            put_u32_le(digest + 4, amd64_entry);
            put_u32_le(digest + 8, (uint32_t)ec_machine);
            put_u32_le(digest + 12, ec_entry);
            put_u32_le(digest + 16, (uint32_t)bytes_read_ec);
        }

        ok(status == STATUS_SUCCESS && get_u16_le(read_amd64 + 0x84) == IMAGE_FILE_MACHINE_AMD64,
           "Expected NtReadFileArm64xPeView system32 AMD64/ARM64EC view Machine=0x8664, got status=0x%08x\n",
           (unsigned int)status);

        parity_record_exact(
            "ntreadfile_system32_arm64x_pe_header_relocated_view_amd64_and_arm64ec",
            "NtReadFile_System32_Arm64xPeHeaderRelocatedView_Amd64AndArm64Ec",
            "NtReadFileArm64xPeView",
            status,
            GetLastError(),
            bytes_read_amd64,
            digest,
            status == STATUS_SUCCESS ? sizeof(digest) : 0,
            status == STATUS_SUCCESS ? 1 : 0);
    }

    /* =========================================================================
     * Case 4: NtReadFile pure ARM64 caller & non-system32 path bypass + EOF check
     * Pure ARM64 (0xAA64) callers reading system32 DLLs and AMD64 callers reading
     * non-system32 paths must observe the raw un-relocated Machine=0xAA64 header.
     * ========================================================================= */
    {
        uint8_t raw_image[4096];
        uint8_t read_arm64[512];
        uint8_t read_appdir[512];
        SIZE_T image_size = 0;
        ULONG bytes_read1 = 0;
        ULONG bytes_read2 = 0;
        ULONG bytes_read_eof = 99U;
        NTSTATUS st_eof = STATUS_SUCCESS;
        uint16_t arm64_caller_machine = 0;
        uint8_t digest[20];
        static const WCHAR path_sys32_k32[] = {
            'C',':','\\','W','i','n','d','o','w','s','\\','S','y','s','t','e','m','3','2','\\','k','e','r','n','e','l','3','2','.','d','l','l',0
        };
        static const WCHAR path_app_plugin[] = {
            'C',':','\\','G','a','m','e','s','\\','M','y','A','p','p','\\','p','l','u','g','i','n','.','d','l','l',0
        };

        memset(raw_image, 0, sizeof(raw_image));
        memset(read_arm64, 0, sizeof(read_arm64));
        memset(read_appdir, 0, sizeof(read_appdir));
        memset(digest, 0, sizeof(digest));

        status = NtdllArm64xBuildSyntheticImage(raw_image, sizeof(raw_image), 1U, &image_size);
        if (status == STATUS_SUCCESS) {
            status = NtReadFileArm64xPeView(
                path_sys32_k32, IMAGE_FILE_MACHINE_ARM64,
                raw_image, image_size, 0ULL,
                read_arm64, sizeof(read_arm64), &bytes_read1);
        }
        if (status == STATUS_SUCCESS) {
            status = NtReadFileArm64xPeView(
                path_app_plugin, IMAGE_FILE_MACHINE_AMD64,
                raw_image, image_size, 0ULL,
                read_appdir, sizeof(read_appdir), &bytes_read2);
        }
        if (status == STATUS_SUCCESS) {
            st_eof = NtReadFileArm64xPeView(
                path_sys32_k32, IMAGE_FILE_MACHINE_AMD64,
                raw_image, image_size, (ULONGLONG)image_size,
                read_arm64, 64U, &bytes_read_eof);

            arm64_caller_machine = get_u16_le(read_arm64 + 0x84);
            put_u32_le(digest + 0, (uint32_t)arm64_caller_machine);
            put_u32_le(digest + 4, get_u32_le(read_arm64 + 0xA8));
            put_u32_le(digest + 8, (uint32_t)get_u16_le(read_appdir + 0x84));
            put_u32_le(digest + 12, get_u32_le(read_appdir + 0xA8));
            put_u32_le(digest + 16, (uint32_t)st_eof);
        }

        ok(status == STATUS_SUCCESS && arm64_caller_machine == IMAGE_FILE_MACHINE_ARM64 && st_eof == STATUS_END_OF_FILE,
           "Expected pure ARM64 caller Machine=0xAA64 and EOF=0xC0000011, got status=0x%08x machine=0x%04x eof=0x%08x\n",
           (unsigned int)status, (unsigned int)arm64_caller_machine, (unsigned int)st_eof);

        parity_record_exact(
            "ntreadfile_arm64_native_caller_and_non_system32_bypass",
            "NtReadFile_Arm64NativeCaller_And_NonSystem32Bypass",
            "NtReadFileArm64xPeView",
            status,
            GetLastError(),
            (ULONG)arm64_caller_machine,
            digest,
            status == STATUS_SUCCESS ? sizeof(digest) : 0,
            status == STATUS_SUCCESS ? 1 : 0);
    }

    /* =========================================================================
     * Case 5: ARM64X loader invalid parameter & corrupt PE header guards
     * ========================================================================= */
    {
        uint8_t small_buf[256];
        uint8_t corrupt_pe[4096];
        SIZE_T out_sz = 0;
        ULONG br = 0;
        NTSTATUS st_null_build;
        NTSTATUS st_small_build;
        NTSTATUS st_bad_pe;
        NTSTATUS st_null_read;
        uint8_t digest[16];

        memset(small_buf, 0, sizeof(small_buf));
        memset(corrupt_pe, 0xCC, sizeof(corrupt_pe));
        memset(digest, 0, sizeof(digest));

        st_null_build = NtdllArm64xBuildSyntheticImage(NULL, 4096U, 1U, &out_sz);
        st_small_build = NtdllArm64xBuildSyntheticImage(small_buf, sizeof(small_buf), 1U, &out_sz);
        st_bad_pe = NtdllArm64xApplyDynamicRelocations(corrupt_pe, sizeof(corrupt_pe));
        st_null_read = NtReadFileArm64xPeView(
            NULL, IMAGE_FILE_MACHINE_AMD64, corrupt_pe, sizeof(corrupt_pe), 0ULL, small_buf, 64U, &br);

        status = (st_null_build == STATUS_INVALID_PARAMETER &&
                  st_small_build == STATUS_BUFFER_TOO_SMALL &&
                  st_bad_pe == STATUS_INVALID_IMAGE_FORMAT &&
                  st_null_read == STATUS_INVALID_PARAMETER)
                     ? STATUS_SUCCESS
                     : STATUS_UNSUCCESSFUL;

        put_u32_le(digest + 0, (uint32_t)st_null_build);
        put_u32_le(digest + 4, (uint32_t)st_small_build);
        put_u32_le(digest + 8, (uint32_t)st_bad_pe);
        put_u32_le(digest + 12, (uint32_t)st_null_read);

        ok(status == STATUS_SUCCESS,
           "Expected ARM64X error guards to return exact NTSTATUS codes, got 0x%08x 0x%08x 0x%08x 0x%08x\n",
           (unsigned int)st_null_build, (unsigned int)st_small_build,
           (unsigned int)st_bad_pe, (unsigned int)st_null_read);

        parity_record_exact(
            "arm64x_loader_invalid_parameter_and_corrupt_pe_guards",
            "Arm64xLoader_InvalidParameter_And_CorruptPeGuards",
            "NtdllArm64xApplyDynamicRelocations",
            status,
            GetLastError(),
            4U,
            digest,
            status == STATUS_SUCCESS ? sizeof(digest) : 0,
            status == STATUS_SUCCESS ? 1 : 0);
    }

    /* =========================================================================
     * Case 6: TSC frequency rescaling (Apple Silicon 24 MHz CNTFRQ_EL0 -> 3600 MHz)
     * Configures 24 MHz host counter to 3.6 GHz x86_64 TSC target (150:1 ratio)
     * and verifies 1 second of 24 MHz ticks rescales to 3,600,000,000 ticks.
     * ========================================================================= */
    {
        NTDLL_TSC_SCALE_STATE tsc_state;
        ULONGLONG scaled_1sec = 0;
        uint8_t digest[20];

        memset(&tsc_state, 0, sizeof(tsc_state));
        memset(digest, 0, sizeof(digest));

        status = NtdllTscScaleConfigure(24000000ULL, 3600000000ULL, &tsc_state);
        if (status == STATUS_SUCCESS) {
            status = NtdllTscScaleReadTicks(24000000ULL, &scaled_1sec);
        }
        if (status == STATUS_SUCCESS) {
            put_u32_le(digest + 0, (uint32_t)tsc_state.scale_numerator);
            put_u32_le(digest + 4, (uint32_t)tsc_state.scale_denominator);
            put_u32_le(digest + 8, (uint32_t)tsc_state.registry_mhz);
            put_u32_le(digest + 12, (uint32_t)(scaled_1sec & 0xFFFFFFFFULL));
            put_u32_le(digest + 16, (uint32_t)(scaled_1sec >> 32));
        }

        ok(status == STATUS_SUCCESS && tsc_state.scale_numerator == 150ULL && scaled_1sec == 3600000000ULL,
           "Expected 24MHz->3600MHz TSC ratio 150:1 and 1s=3600000000, got status=0x%08x num=%llu ticks=%llu\n",
           (unsigned int)status,
           (unsigned long long)tsc_state.scale_numerator,
           (unsigned long long)scaled_1sec);

        parity_record_exact(
            "tsc_scale_configure_apple_silicon_24mhz_to_3600mhz",
            "TscScale_Configure_AppleSilicon24MhzTo3600Mhz",
            "NtdllTscScaleConfigure",
            status,
            GetLastError(),
            tsc_state.registry_mhz,
            digest,
            status == STATUS_SUCCESS ? sizeof(digest) : 0,
            status == STATUS_SUCCESS ? 1 : 0);
    }

    /* =========================================================================
     * Case 7: Registry CentralProcessor\N\~MHz query & NtQueryPerformanceCounter
     * Verifies ~MHz matches configured TSC MHz (3600) across CPU cores and
     * NtQueryPerformanceCounter reports monotonic 10 MHz ticks.
     * ========================================================================= */
    {
        ULONG mhz_cpu0 = 0;
        ULONG mhz_cpu3 = 0;
        ULONG mhz_bad = 0;
        NTSTATUS st_bad_reg = STATUS_SUCCESS;
        LARGE_INTEGER qpc1;
        LARGE_INTEGER qpc2;
        LARGE_INTEGER qpf;
        uint8_t digest[20];
        static const WCHAR reg_cpu0[] = {
            'H','A','R','D','W','A','R','E','\\','D','E','S','C','R','I','P','T','I','O','N','\\',
            'S','y','s','t','e','m','\\','C','e','n','t','r','a','l','P','r','o','c','e','s','s','o','r','\\','0',0
        };
        static const WCHAR reg_cpu3[] = {
            '\\','R','e','g','i','s','t','r','y','\\','M','a','c','h','i','n','e','\\',
            'H','A','R','D','W','A','R','E','\\','D','E','S','C','R','I','P','T','I','O','N','\\',
            'S','y','s','t','e','m','\\','C','e','n','t','r','a','l','P','r','o','c','e','s','s','o','r','\\','3',0
        };
        static const WCHAR val_mhz[] = { '~','M','H','z',0 };
        static const WCHAR val_invalid[] = { 'N','o','n','E','x','i','s','t','e','n','t',0 };

        memset(&qpc1, 0, sizeof(qpc1));
        memset(&qpc2, 0, sizeof(qpc2));
        memset(&qpf, 0, sizeof(qpf));
        memset(digest, 0, sizeof(digest));

        status = NtdllTscQueryRegistryMhz(reg_cpu0, val_mhz, &mhz_cpu0);
        if (status == STATUS_SUCCESS) {
            status = NtdllTscQueryRegistryMhz(reg_cpu3, val_mhz, &mhz_cpu3);
        }
        if (status == STATUS_SUCCESS) {
            st_bad_reg = NtdllTscQueryRegistryMhz(reg_cpu0, val_invalid, &mhz_bad);
            status = NtQueryPerformanceCounter(&qpc1, &qpf);
        }
        if (status == STATUS_SUCCESS) {
            status = NtQueryPerformanceCounter(&qpc2, NULL);
        }
        if (status == STATUS_SUCCESS) {
            uint32_t monotonic = (qpc1.QuadPart > 0 && qpc2.QuadPart >= qpc1.QuadPart) ? 1U : 0U;
            put_u32_le(digest + 0, (uint32_t)mhz_cpu0);
            put_u32_le(digest + 4, (uint32_t)mhz_cpu3);
            put_u32_le(digest + 8, (uint32_t)st_bad_reg);
            put_u32_le(digest + 12, (uint32_t)qpf.LowPart);
            put_u32_le(digest + 16, monotonic);
        }

        ok(status == STATUS_SUCCESS && mhz_cpu0 == 3600U && qpf.QuadPart == 10000000LL,
           "Expected ~MHz=3600 and QPF=10000000, got status=0x%08x mhz=%u qpf=%lld\n",
           (unsigned int)status, (unsigned int)mhz_cpu0, (long long)qpf.QuadPart);

        parity_record_exact(
            "tsc_registry_mhz_query_and_ntqueryperformancecounter_consistency",
            "TscRegistryMhzQuery_And_NtQueryPerformanceCounterConsistency",
            "NtQueryPerformanceCounter",
            status,
            GetLastError(),
            mhz_cpu0,
            digest,
            status == STATUS_SUCCESS ? sizeof(digest) : 0,
            status == STATUS_SUCCESS ? 1 : 0);
    }

    /* =========================================================================
     * Case 8: Non-integer TSC ratio (19.2 MHz -> 3400 MHz) & 128-bit overflow safety
     * Verifies 19.2 MHz host counter to 3.4 GHz TSC reduces to 2125:12 and scales
     * 10,000 seconds (192,000,000,000 ticks) to 34,000,000,000,000 ticks without
     * 64-bit intermediate multiplication overflow.
     * ========================================================================= */
    {
        NTDLL_TSC_SCALE_STATE tsc_state;
        NTDLL_TSC_SCALE_STATE bad_state;
        ULONGLONG scaled_10k_sec = 0;
        NTSTATUS st_zero_freq = STATUS_SUCCESS;
        uint8_t digest[20];

        memset(&tsc_state, 0, sizeof(tsc_state));
        memset(&bad_state, 0, sizeof(bad_state));
        memset(digest, 0, sizeof(digest));

        status = NtdllTscScaleConfigure(19200000ULL, 3400000000ULL, &tsc_state);
        if (status == STATUS_SUCCESS) {
            status = NtdllTscScaleReadTicks(192000000000ULL, &scaled_10k_sec);
        }
        if (status == STATUS_SUCCESS) {
            st_zero_freq = NtdllTscScaleConfigure(0ULL, 3400000000ULL, &bad_state);
            put_u32_le(digest + 0, (uint32_t)tsc_state.scale_numerator);
            put_u32_le(digest + 4, (uint32_t)tsc_state.scale_denominator);
            put_u32_le(digest + 8, (uint32_t)(scaled_10k_sec & 0xFFFFFFFFULL));
            put_u32_le(digest + 12, (uint32_t)(scaled_10k_sec >> 32));
            put_u32_le(digest + 16, (uint32_t)st_zero_freq);
        }

        ok(status == STATUS_SUCCESS &&
           tsc_state.scale_numerator == 2125ULL &&
           tsc_state.scale_denominator == 12ULL &&
           scaled_10k_sec == 34000000000000ULL,
           "Expected 19.2MHz->3400MHz ratio 2125:12 and 10ks=34000000000000, got status=0x%08x ticks=%llu\n",
           (unsigned int)status, (unsigned long long)scaled_10k_sec);

        parity_record_exact(
            "tsc_scale_non_integer_ratio_19_2mhz_to_3400mhz_no_overflow",
            "TscScale_NonIntegerRatio_19_2MhzTo3400Mhz_NoOverflow",
            "NtdllTscScaleReadTicks",
            status,
            GetLastError(),
            tsc_state.registry_mhz,
            digest,
            status == STATUS_SUCCESS ? sizeof(digest) : 0,
            status == STATUS_SUCCESS ? 1 : 0);
    }

    /* =========================================================================
     * Case 9: On-disk JIT code cache init, block store, and lookup hit with I-cache flush
     * ========================================================================= */
    {
        NTDLL_JIT_MODULE_KEY mod_a;
        NTDLL_JIT_CACHE_STATS stats;
        uint8_t arm64_block[16];
        uint8_t lookup_buf[32];
        SIZE_T lookup_size = 0;
        uint8_t digest[20];

        memset(&mod_a, 0, sizeof(mod_a));
        memset(&stats, 0, sizeof(stats));
        memset(lookup_buf, 0, sizeof(lookup_buf));
        memset(digest, 0, sizeof(digest));

        mod_a.time_date_stamp = 0x6710ABCDU;
        mod_a.checksum = 0x00123456U;
        mod_a.size_of_image = 0x00040000U;
        mod_a.guest_machine = IMAGE_FILE_MACHINE_AMD64;
        mod_a.emulator_id = 1U;

        /* 4 AArch64 instructions: mov x0, #0x2a; add x0, x0, #1; nop; ret */
        put_u32_le(arm64_block + 0, 0xD2800540U);
        put_u32_le(arm64_block + 4, 0x91000400U);
        put_u32_le(arm64_block + 8, 0xD503201FU);
        put_u32_le(arm64_block + 12, 0xD65F03C0U);

        status = NtdllJitCacheInit("/tmp/wine_parity_ntdll_jit_cache_test");
        if (status == STATUS_SUCCESS) {
            status = NtdllJitCacheStoreBlock(&mod_a, 0x1040U, arm64_block, sizeof(arm64_block));
        }
        if (status == STATUS_SUCCESS) {
            status = NtdllJitCacheLookupBlock(&mod_a, 0x1040U, lookup_buf, sizeof(lookup_buf), &lookup_size);
        }
        if (status == STATUS_SUCCESS) {
            status = NtdllJitCacheQueryStats(&stats);
        }
        if (status == STATUS_SUCCESS) {
            put_u32_le(digest + 0, (uint32_t)lookup_size);
            put_u32_le(digest + 4, (uint32_t)stats.cache_stores);
            put_u32_le(digest + 8, (uint32_t)stats.cache_hits);
            put_u32_le(digest + 12, (uint32_t)stats.icache_flushes);
            put_u32_le(digest + 16, get_u32_le(lookup_buf));
        }

        ok(status == STATUS_SUCCESS && lookup_size == 16U && memcmp(lookup_buf, arm64_block, 16) == 0,
           "Expected JIT cache store/lookup hit of 16 bytes, got status=0x%08x size=%zu\n",
           (unsigned int)status, (size_t)lookup_size);

        parity_record_exact(
            "jit_cache_init_store_and_lookup_hit_with_icache_flush",
            "JitCache_InitStore_And_LookupHitWithIcacheFlush",
            "NtdllJitCacheLookupBlock",
            status,
            GetLastError(),
            (ULONG)lookup_size,
            digest,
            status == STATUS_SUCCESS ? sizeof(digest) : 0,
            status == STATUS_SUCCESS ? 1 : 0);
    }

    /* =========================================================================
     * Case 10: JIT cache miss on PE TimeDateStamp, CheckSum, or GuestRva mismatch
     * ========================================================================= */
    {
        NTDLL_JIT_MODULE_KEY mod_ts_diff;
        NTDLL_JIT_MODULE_KEY mod_cs_diff;
        NTDLL_JIT_MODULE_KEY mod_same;
        NTDLL_JIT_CACHE_STATS stats;
        uint8_t lookup_buf[32];
        SIZE_T lookup_size = 0;
        NTSTATUS st_ts_miss;
        NTSTATUS st_cs_miss;
        NTSTATUS st_rva_miss;
        uint8_t digest[16];

        memset(&stats, 0, sizeof(stats));
        memset(lookup_buf, 0, sizeof(lookup_buf));
        memset(digest, 0, sizeof(digest));

        mod_same.time_date_stamp = 0x6710ABCDU;
        mod_same.checksum = 0x00123456U;
        mod_same.size_of_image = 0x00040000U;
        mod_same.guest_machine = IMAGE_FILE_MACHINE_AMD64;
        mod_same.emulator_id = 1U;

        mod_ts_diff = mod_same;
        mod_ts_diff.time_date_stamp = 0x6710ABCEU;

        mod_cs_diff = mod_same;
        mod_cs_diff.checksum = 0x00123457U;

        st_ts_miss = NtdllJitCacheLookupBlock(&mod_ts_diff, 0x1040U, lookup_buf, sizeof(lookup_buf), &lookup_size);
        st_cs_miss = NtdllJitCacheLookupBlock(&mod_cs_diff, 0x1040U, lookup_buf, sizeof(lookup_buf), &lookup_size);
        st_rva_miss = NtdllJitCacheLookupBlock(&mod_same, 0x2080U, lookup_buf, sizeof(lookup_buf), &lookup_size);
        status = NtdllJitCacheQueryStats(&stats);

        if (status == STATUS_SUCCESS &&
            st_ts_miss == STATUS_OBJECT_NAME_NOT_FOUND &&
            st_cs_miss == STATUS_OBJECT_NAME_NOT_FOUND &&
            st_rva_miss == STATUS_OBJECT_NAME_NOT_FOUND) {
            put_u32_le(digest + 0, (uint32_t)st_ts_miss);
            put_u32_le(digest + 4, (uint32_t)st_cs_miss);
            put_u32_le(digest + 8, (uint32_t)st_rva_miss);
            put_u32_le(digest + 12, (uint32_t)stats.cache_misses);
        } else if (status == STATUS_SUCCESS) {
            status = STATUS_UNSUCCESSFUL;
        }

        ok(status == STATUS_SUCCESS && stats.cache_misses == 3U,
           "Expected 3 JIT cache misses on timestamp/checksum/RVA change, got status=0x%08x misses=%u\n",
           (unsigned int)status, (unsigned int)stats.cache_misses);

        parity_record_exact(
            "jit_cache_miss_on_pe_timestamp_or_checksum_change",
            "JitCache_Miss_OnPeTimestampOrChecksumChange",
            "NtdllJitCacheLookupBlock",
            status,
            GetLastError(),
            stats.cache_misses,
            digest,
            status == STATUS_SUCCESS ? sizeof(digest) : 0,
            status == STATUS_SUCCESS ? 1 : 0);
    }

    /* =========================================================================
     * Case 11: JIT cache on-disk payload corruption detection (STATUS_CRC_ERROR)
     * ========================================================================= */
    {
        NTDLL_JIT_MODULE_KEY mod_a;
        NTDLL_JIT_CACHE_STATS stats;
        uint8_t arm64_block2[8];
        uint8_t lookup_buf[32];
        SIZE_T lookup_size = 0;
        NTSTATUS st_corrupt_inject = STATUS_UNSUCCESSFUL;
        NTSTATUS st_corrupt_lookup = STATUS_SUCCESS;
        uint8_t digest[16];

        memset(&mod_a, 0, sizeof(mod_a));
        memset(&stats, 0, sizeof(stats));
        memset(lookup_buf, 0, sizeof(lookup_buf));
        memset(digest, 0, sizeof(digest));

        mod_a.time_date_stamp = 0x6710ABCDU;
        mod_a.checksum = 0x00123456U;
        mod_a.size_of_image = 0x00040000U;
        mod_a.guest_machine = IMAGE_FILE_MACHINE_AMD64;
        mod_a.emulator_id = 1U;

        put_u32_le(arm64_block2 + 0, 0xD2800000U);
        put_u32_le(arm64_block2 + 4, 0xD65F03C0U);

        status = NtdllJitCacheStoreBlock(&mod_a, 0x3000U, arm64_block2, sizeof(arm64_block2));
        if (status == STATUS_SUCCESS) {
            st_corrupt_inject = NtdllJitCacheCorruptBlockForTest(&mod_a, 0x3000U);
            st_corrupt_lookup = NtdllJitCacheLookupBlock(&mod_a, 0x3000U, lookup_buf, sizeof(lookup_buf), &lookup_size);
            status = NtdllJitCacheQueryStats(&stats);
        }
        if (status == STATUS_SUCCESS &&
            st_corrupt_inject == STATUS_SUCCESS &&
            st_corrupt_lookup == STATUS_CRC_ERROR) {
            put_u32_le(digest + 0, (uint32_t)st_corrupt_inject);
            put_u32_le(digest + 4, (uint32_t)st_corrupt_lookup);
            put_u32_le(digest + 8, (uint32_t)stats.corrupt_rejections);
            put_u32_le(digest + 12, (uint32_t)stats.cache_stores);
        } else if (status == STATUS_SUCCESS) {
            status = STATUS_UNSUCCESSFUL;
        }

        ok(status == STATUS_SUCCESS && st_corrupt_lookup == STATUS_CRC_ERROR && stats.corrupt_rejections == 1U,
           "Expected corrupted JIT cache block to return STATUS_CRC_ERROR, got lookup=0x%08x rejections=%u\n",
           (unsigned int)st_corrupt_lookup, (unsigned int)stats.corrupt_rejections);

        parity_record_exact(
            "jit_cache_corruption_detection_and_rejection",
            "JitCache_CorruptionDetection_And_Rejection",
            "NtdllJitCacheLookupBlock",
            status,
            GetLastError(),
            stats.corrupt_rejections,
            digest,
            status == STATUS_SUCCESS ? sizeof(digest) : 0,
            status == STATUS_SUCCESS ? 1 : 0);
    }

    /* =========================================================================
     * Case 12: JIT cache STATUS_BUFFER_TOO_SMALL guard & module invalidation
     * ========================================================================= */
    {
        NTDLL_JIT_MODULE_KEY mod_a;
        NTDLL_JIT_CACHE_STATS stats;
        uint8_t small_buf[4];
        uint8_t lookup_buf[32];
        SIZE_T needed_size = 0;
        SIZE_T after_size = 0;
        ULONG removed_count = 0;
        NTSTATUS st_small_buf;
        NTSTATUS st_after_inv = STATUS_SUCCESS;
        uint8_t digest[20];

        memset(&mod_a, 0, sizeof(mod_a));
        memset(&stats, 0, sizeof(stats));
        memset(small_buf, 0, sizeof(small_buf));
        memset(lookup_buf, 0, sizeof(lookup_buf));
        memset(digest, 0, sizeof(digest));

        mod_a.time_date_stamp = 0x6710ABCDU;
        mod_a.checksum = 0x00123456U;
        mod_a.size_of_image = 0x00040000U;
        mod_a.guest_machine = IMAGE_FILE_MACHINE_AMD64;
        mod_a.emulator_id = 1U;

        st_small_buf = NtdllJitCacheLookupBlock(&mod_a, 0x1040U, small_buf, sizeof(small_buf), &needed_size);
        status = NtdllJitCacheInvalidateModule(&mod_a, &removed_count);
        if (status == STATUS_SUCCESS) {
            st_after_inv = NtdllJitCacheLookupBlock(&mod_a, 0x1040U, lookup_buf, sizeof(lookup_buf), &after_size);
            status = NtdllJitCacheQueryStats(&stats);
        }
        if (status == STATUS_SUCCESS &&
            st_small_buf == STATUS_BUFFER_TOO_SMALL &&
            needed_size == 16U &&
            removed_count == 1U &&
            st_after_inv == STATUS_OBJECT_NAME_NOT_FOUND) {
            put_u32_le(digest + 0, (uint32_t)st_small_buf);
            put_u32_le(digest + 4, (uint32_t)needed_size);
            put_u32_le(digest + 8, (uint32_t)removed_count);
            put_u32_le(digest + 12, (uint32_t)st_after_inv);
            put_u32_le(digest + 16, (uint32_t)stats.invalidated_entries);
        } else if (status == STATUS_SUCCESS) {
            status = STATUS_UNSUCCESSFUL;
        }

        ok(status == STATUS_SUCCESS && removed_count == 1U && stats.invalidated_entries == 1U,
           "Expected JIT cache module invalidation to remove 1 entry, got status=0x%08x removed=%u\n",
           (unsigned int)status, (unsigned int)removed_count);

        parity_record_exact(
            "jit_cache_module_invalidation_and_buffer_too_small_guard",
            "JitCache_ModuleInvalidation_And_BufferTooSmallGuard",
            "NtdllJitCacheInvalidateModule",
            status,
            GetLastError(),
            removed_count,
            digest,
            status == STATUS_SUCCESS ? sizeof(digest) : 0,
            status == STATUS_SUCCESS ? 1 : 0);
    }

    parity_end_document();
    return 0;
}

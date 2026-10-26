/*
 * ARM64EC / ARM64X Hybrid PE Loader, TSC Frequency Rescaling, and On-Disk JIT Cache
 * (WineConf 2025 ARM64 Roadmap)
 *
 * Clean-room implementation derived from:
 *   - Microsoft Learn PE Format (ARM64X Dynamic Value Relocation Table / DVRT v1 & v2)
 *   - Upstream Wine dlls/ntdll/unix/virtual.c (apply_arm64x_relocations)
 *   - WineConf 2025 ARM64EC / WoW64 roadmap notes
 *
 * Copyright 2026 Spumante Contributors
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>

#ifdef _WIN32
#include <direct.h>
#include <io.h>
#else
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <unistd.h>
#endif

#ifdef __APPLE__
#include <libkern/OSCacheControl.h>
#endif

#include "ntdll.h"

#define WINE_DEFAULT_DEBUG_CHANNEL(ch)
#define TRACE(...) do { } while (0)
#define WARN(...)  do { } while (0)
#define FIXME(...) do { } while (0)
#define ERR(...)   do { } while (0)

WINE_DEFAULT_DEBUG_CHANNEL(ntdll);

#define ARM64X_SYNTHETIC_IMAGE_SIZE 4096U
#define WJTC_MAGIC                  0x43544A57U /* "WJTC" */
#define WJTC_VERSION                1U
#define WJTC_MAX_CODE_SIZE          (64U * 1024U)

typedef struct _WJTC_DISK_HEADER {
    uint32_t magic;
    uint32_t version;
    uint32_t time_date_stamp;
    uint32_t checksum;
    uint32_t size_of_image;
    uint16_t guest_machine;
    uint16_t emulator_id;
    uint32_t guest_rva;
    uint32_t code_size;
    uint64_t payload_hash;
} WJTC_DISK_HEADER;

static NTDLL_TSC_SCALE_STATE g_tsc_scale_state = {
    24000000ULL,
    3600000000ULL,
    150ULL,
    1ULL,
    3600U,
    1U
};

static LONGLONG g_last_qpc_value = 100000LL;
static char g_jit_cache_dir[512] = "/tmp/spumante_ntdll_jit_cache";
static NTDLL_JIT_CACHE_STATS g_jit_cache_stats = { 0, 0, 0, 0, 0, 0, 0 };

static int is_stub_simulation_enabled(void)
{
    const char *env = getenv("SPUMANTE_SIMULATE_STUB");
    return (env != NULL && strcmp(env, "1") == 0);
}

static void write_u16_le(uint8_t *dst, uint16_t v)
{
    dst[0] = (uint8_t)(v & 0xffU);
    dst[1] = (uint8_t)((v >> 8) & 0xffU);
}

static void write_u32_le(uint8_t *dst, uint32_t v)
{
    dst[0] = (uint8_t)(v & 0xffU);
    dst[1] = (uint8_t)((v >> 8) & 0xffU);
    dst[2] = (uint8_t)((v >> 16) & 0xffU);
    dst[3] = (uint8_t)((v >> 24) & 0xffU);
}

static void write_u64_le(uint8_t *dst, uint64_t v)
{
    write_u32_le(dst, (uint32_t)(v & 0xffffffffULL));
    write_u32_le(dst + 4, (uint32_t)(v >> 32));
}

static uint16_t read_u16_le(const uint8_t *src)
{
    return (uint16_t)((uint16_t)src[0] | ((uint16_t)src[1] << 8));
}

static uint32_t read_u32_le(const uint8_t *src)
{
    return (uint32_t)src[0] |
           ((uint32_t)src[1] << 8) |
           ((uint32_t)src[2] << 16) |
           ((uint32_t)src[3] << 24);
}

static uint64_t read_u64_le(const uint8_t *src)
{
    return (uint64_t)read_u32_le(src) | ((uint64_t)read_u32_le(src + 4) << 32);
}

static uint64_t gcd_u64(uint64_t a, uint64_t b)
{
    while (b != 0ULL) {
        uint64_t t = a % b;
        a = b;
        b = t;
    }
    return a;
}

static uint64_t fnv1a64_digest(const void *data, size_t len, uint64_t seed)
{
    const uint8_t *bytes = (const uint8_t *)data;
    uint64_t hash = 0xcbf29ce484222325ULL ^ seed;
    size_t i;

    for (i = 0; i < len; ++i) {
        hash ^= (uint64_t)bytes[i];
        hash *= 0x100000001b3ULL;
    }
    return hash;
}

static WCHAR tolower_wchar(WCHAR ch)
{
    if (ch >= 'A' && ch <= 'Z') {
        return (WCHAR)(ch + ('a' - 'A'));
    }
    return ch;
}

static int wstr_contains_system32(const WCHAR *path)
{
    static const WCHAR needle[] = { 's','y','s','t','e','m','3','2',0 };
    size_t i;

    if (path == NULL) {
        return 0;
    }
    for (i = 0; path[i] != 0; ++i) {
        WCHAR sep_before = path[i];
        if (sep_before == '\\' || sep_before == '/') {
            size_t j = 0;
            while (needle[j] != 0 && path[i + 1 + j] != 0 &&
                   tolower_wchar(path[i + 1 + j]) == needle[j]) {
                ++j;
            }
            if (needle[j] == 0) {
                WCHAR sep_after = path[i + 1 + j];
                if (sep_after == '\\' || sep_after == '/') {
                    return 1;
                }
            }
        }
    }
    return 0;
}

static int wstr_equals_ascii_ci(const WCHAR *wstr, const char *ascii)
{
    size_t i = 0;
    if (wstr == NULL || ascii == NULL) {
        return 0;
    }
    while (wstr[i] != 0 && ascii[i] != '\0') {
        WCHAR wc = tolower_wchar(wstr[i]);
        WCHAR ac = (WCHAR)(unsigned char)ascii[i];
        if (ac >= 'A' && ac <= 'Z') {
            ac = (WCHAR)(ac + ('a' - 'A'));
        }
        if (wc != ac) {
            return 0;
        }
        ++i;
    }
    return (wstr[i] == 0 && ascii[i] == '\0') ? 1 : 0;
}

static int wstr_starts_with_ascii_ci(const WCHAR *wstr, const char *prefix, size_t *out_len)
{
    size_t i = 0;
    if (wstr == NULL || prefix == NULL) {
        return 0;
    }
    while (prefix[i] != '\0') {
        WCHAR wc = tolower_wchar(wstr[i]);
        WCHAR ac = (WCHAR)(unsigned char)prefix[i];
        if (ac >= 'A' && ac <= 'Z') {
            ac = (WCHAR)(ac + ('a' - 'A'));
        }
        if (wc != ac) {
            return 0;
        }
        ++i;
    }
    if (out_len != NULL) {
        *out_len = i;
    }
    return 1;
}

/* =========================================================================
 * 1. ARM64X Dynamic Value Relocation Table (DVRT v1/v2) & NtReadFile PE View
 * ========================================================================= */

NTSTATUS WINAPI NtdllArm64xBuildSyntheticImage(
    PVOID Buffer,
    SIZE_T BufferSize,
    ULONG DvRtVersion,
    SIZE_T *OutImageSize)
{
    uint8_t *img;
    uint8_t *dvrt;
    uint8_t *block;
    uint8_t *entries;
    size_t entries_len;
    uint32_t block_size;

    if (is_stub_simulation_enabled()) {
        return STATUS_NOT_IMPLEMENTED;
    }
    if (Buffer == NULL || OutImageSize == NULL || (DvRtVersion != 1U && DvRtVersion != 2U)) {
        return STATUS_INVALID_PARAMETER;
    }
    *OutImageSize = ARM64X_SYNTHETIC_IMAGE_SIZE;
    if (BufferSize < ARM64X_SYNTHETIC_IMAGE_SIZE) {
        return STATUS_BUFFER_TOO_SMALL;
    }

    img = (uint8_t *)Buffer;
    memset(img, 0, ARM64X_SYNTHETIC_IMAGE_SIZE);

    /* DOS Header at 0x000 */
    write_u16_le(img + 0x00, 0x5A4DU); /* MZ */
    write_u32_le(img + 0x3C, 0x00000080U); /* e_lfanew = 0x80 */

    /* NT64 Headers at 0x080 */
    write_u32_le(img + 0x80, 0x00004550U); /* PE\0\0 */
    write_u16_le(img + 0x84, IMAGE_FILE_MACHINE_ARM64); /* Machine = 0xAA64 (pre-DVRT) */
    write_u16_le(img + 0x86, 1U); /* NumberOfSections = 1 */
    write_u32_le(img + 0x88, 0x6710ABCDU); /* TimeDateStamp */
    write_u16_le(img + 0x94, 0x00F0U); /* SizeOfOptionalHeader = 240 */
    write_u16_le(img + 0x96, 0x2022U); /* Characteristics */

    /* OptionalHeader64 at 0x098 */
    write_u16_le(img + 0x98, 0x020BU); /* IMAGE_NT_OPTIONAL_HDR64_MAGIC */
    write_u32_le(img + 0xA8, 0x00001000U); /* AddressOfEntryPoint = 0x1000 (ARM64) */
    write_u64_le(img + 0xB0, 0x0000000180000000ULL); /* ImageBase */
    write_u32_le(img + 0xB8, 0x00001000U); /* SectionAlignment */
    write_u32_le(img + 0xBC, 0x00000200U); /* FileAlignment */
    write_u32_le(img + 0xD0, ARM64X_SYNTHETIC_IMAGE_SIZE); /* SizeOfImage */
    write_u32_le(img + 0xD4, 0x00000200U); /* SizeOfHeaders */
    write_u32_le(img + 0xD8, 0x00123456U); /* CheckSum */
    write_u32_le(img + 0x104, 16U); /* NumberOfRvaAndSizes */

    /* DataDirectory[5] (BASERELOC) at 0x130: non-zero pre-DVRT, zeroed by ZEROFILL fixup */
    write_u32_le(img + 0x130, 0x00000500U);
    write_u32_le(img + 0x134, 0x00000010U);

    /* DataDirectory[10] (LOAD_CONFIG) at 0x158 */
    write_u32_le(img + 0x158, 0x00000200U);
    write_u32_le(img + 0x15C, 0x00000100U);

    /* SectionHeader[0] (.rdata) at 0x188 */
    memcpy(img + 0x188, ".rdata\0\0", 8);
    write_u32_le(img + 0x190, 0x00000800U); /* VirtualSize */
    write_u32_le(img + 0x194, 0x00000300U); /* VirtualAddress = 0x300 */
    write_u32_le(img + 0x198, 0x00000800U); /* SizeOfRawData */
    write_u32_le(img + 0x19C, 0x00000300U); /* PointerToRawData */

    /* IMAGE_LOAD_CONFIG_DIRECTORY64 at 0x200 */
    write_u32_le(img + 0x200, 0x00000100U); /* Size */
    write_u64_le(img + 0x2C8, 0x0000000000000000ULL); /* CHPEMetadataPointer (pre-DVRT) */
    write_u32_le(img + 0x2D4, 0x00000100U); /* DynamicValueRelocTableOffset (+0x100 in sec 1 -> 0x400) */
    write_u16_le(img + 0x2D8, 1U); /* DynamicValueRelocTableSection = 1 (1-based) */
    write_u32_le(img + 0x2E0, 0x00005000U); /* Synthetic RVA field for DVRT v2 negative DELTA */

    /* IMAGE_DYNAMIC_RELOCATION_TABLE at 0x400 */
    dvrt = img + 0x400;
    write_u32_le(dvrt + 0x00, DvRtVersion);

    if (DvRtVersion == 1U) {
        /*
         * DVRT v1:
         *   IMAGE_DYNAMIC_RELOCATION64 { ULONGLONG Symbol (8); ULONG BaseRelocSize (4); } (12 bytes)
         *   followed by IMAGE_BASE_RELOCATION { ULONG VirtualAddress; ULONG SizeOfBlock; USHORT TypeOffset[]; }
         */
        block = dvrt + 8 + 12;
    } else {
        /*
         * DVRT v2:
         *   IMAGE_DYNAMIC_RELOCATION64_V2 {
         *     ULONG HeaderSize (24); ULONG FixupInfoSize; ULONGLONG Symbol (6);
         *     ULONG SymbolGroup (0); ULONG Flags (0);
         *   } (24 bytes)
         */
        block = dvrt + 8 + 24;
    }

    entries = block + 8;
    entries_len = 0;

    /* 1. VALUE fixup (type=1, arg=1 -> 2 bytes) at offset 0x084 (FileHeader.Machine = 0x8664) */
    write_u16_le(entries + entries_len, (uint16_t)((1U << 14) | (NTDLL_DVRT_ARM64X_FIXUP_TYPE_VALUE << 12) | 0x084U));
    entries_len += 2;
    write_u16_le(entries + entries_len, IMAGE_FILE_MACHINE_AMD64);
    entries_len += 2;

    /* 2. DELTA fixup (type=2, arg=0 -> +4 scale) at offset 0x0A8 (AddressOfEntryPoint += 0x0400 * 4 = +0x1000) */
    write_u16_le(entries + entries_len, (uint16_t)((0U << 14) | (NTDLL_DVRT_ARM64X_FIXUP_TYPE_DELTA << 12) | 0x0A8U));
    entries_len += 2;
    write_u16_le(entries + entries_len, 0x0400U);
    entries_len += 2;

    /* 3. ZEROFILL fixup (type=0, arg=3 -> 8 bytes) at offset 0x130 (DataDirectory[5] BASERELOC) */
    write_u16_le(entries + entries_len, (uint16_t)((3U << 14) | (NTDLL_DVRT_ARM64X_FIXUP_TYPE_ZEROFILL << 12) | 0x130U));
    entries_len += 2;

    /* 4. VALUE fixup (type=1, arg=3 -> 8 bytes) at offset 0x2C8 (CHPEMetadataPointer = 0x0000000180003000ULL) */
    write_u16_le(entries + entries_len, (uint16_t)((3U << 14) | (NTDLL_DVRT_ARM64X_FIXUP_TYPE_VALUE << 12) | 0x2C8U));
    entries_len += 2;
    write_u64_le(entries + entries_len, 0x0000000180003000ULL);
    entries_len += 8;

    if (DvRtVersion == 2U) {
        /* 5. DELTA fixup (type=2, arg=3 -> negative 8-byte scale) at offset 0x2E0 (0x5000 - 0x80*8 = 0x4C00) */
        write_u16_le(entries + entries_len, (uint16_t)((3U << 14) | (NTDLL_DVRT_ARM64X_FIXUP_TYPE_DELTA << 12) | 0x2E0U));
        entries_len += 2;
        write_u16_le(entries + entries_len, 0x0080U);
        entries_len += 2;
    }

    /* Pad entries to 4-byte boundary with zero entry */
    if ((entries_len & 3U) != 0U) {
        write_u16_le(entries + entries_len, 0x0000U);
        entries_len += 2;
    }

    block_size = (uint32_t)(8U + entries_len);
    write_u32_le(block + 0, 0x00000000U); /* Page VirtualAddress = 0x0000 */
    write_u32_le(block + 4, block_size);  /* SizeOfBlock */

    if (DvRtVersion == 1U) {
        write_u32_le(dvrt + 0x04, 12U + block_size); /* Table Size */
        write_u64_le(dvrt + 0x08, NTDLL_IMAGE_DYNAMIC_RELOCATION_ARM64X);
        write_u32_le(dvrt + 0x10, block_size);
    } else {
        write_u32_le(dvrt + 0x04, 24U + block_size); /* Table Size */
        write_u32_le(dvrt + 0x08, 24U);              /* HeaderSize */
        write_u32_le(dvrt + 0x0C, block_size);       /* FixupInfoSize */
        write_u64_le(dvrt + 0x10, NTDLL_IMAGE_DYNAMIC_RELOCATION_ARM64X);
        write_u32_le(dvrt + 0x18, 0U);               /* SymbolGroup */
        write_u32_le(dvrt + 0x1C, 0U);               /* Flags */
    }

    TRACE("Built synthetic ARM64X PE image (DvRtVersion=%u, size=%u)\n",
          (unsigned int)DvRtVersion, (unsigned int)ARM64X_SYNTHETIC_IMAGE_SIZE);
    return STATUS_SUCCESS;
}

static NTSTATUS apply_arm64x_fixup_blocks(
    uint8_t *img,
    size_t img_size,
    const uint8_t *blocks_start,
    size_t blocks_size)
{
    size_t pos = 0;

    while (pos + 8U <= blocks_size) {
        uint32_t page_rva = read_u32_le(blocks_start + pos);
        uint32_t size_of_block = read_u32_le(blocks_start + pos + 4);
        size_t entry_offset;

        if (size_of_block < 8U || pos + size_of_block > blocks_size) {
            return STATUS_INVALID_IMAGE_FORMAT;
        }
        if (page_rva >= img_size) {
            return STATUS_INVALID_IMAGE_FORMAT;
        }

        entry_offset = 8U;
        while (entry_offset + 2U <= size_of_block) {
            uint16_t meta = read_u16_le(blocks_start + pos + entry_offset);
            uint16_t offset = (uint16_t)(meta & 0x0fffU);
            uint16_t type = (uint16_t)((meta >> 12) & 0x3U);
            uint16_t arg = (uint16_t)((meta >> 14) & 0x3U);
            size_t target_rva = (size_t)page_rva + (size_t)offset;

            entry_offset += 2U;
            if (meta == 0U) {
                continue;
            }

            switch (type) {
                case NTDLL_DVRT_ARM64X_FIXUP_TYPE_ZEROFILL: {
                    size_t span = (size_t)1U << arg;
                    if (target_rva + span > img_size) {
                        return STATUS_INVALID_IMAGE_FORMAT;
                    }
                    memset(img + target_rva, 0, span);
                    break;
                }
                case NTDLL_DVRT_ARM64X_FIXUP_TYPE_VALUE: {
                    size_t span = (size_t)1U << arg;
                    if (entry_offset + span > size_of_block || target_rva + span > img_size) {
                        return STATUS_INVALID_IMAGE_FORMAT;
                    }
                    memcpy(img + target_rva, blocks_start + pos + entry_offset, span);
                    entry_offset += span;
                    break;
                }
                case NTDLL_DVRT_ARM64X_FIXUP_TYPE_DELTA: {
                    uint16_t raw_factor;
                    int32_t scale;
                    int32_t delta;
                    uint32_t cur_val;

                    if (entry_offset + 2U > size_of_block || target_rva + 4U > img_size) {
                        return STATUS_INVALID_IMAGE_FORMAT;
                    }
                    raw_factor = read_u16_le(blocks_start + pos + entry_offset);
                    entry_offset += 2U;
                    scale = (arg & 2U) ? 8 : 4;
                    delta = (int32_t)((uint32_t)raw_factor * (uint32_t)scale);
                    if ((arg & 1U) != 0U) {
                        delta = -delta;
                    }
                    cur_val = read_u32_le(img + target_rva);
                    write_u32_le(img + target_rva, (uint32_t)((int32_t)cur_val + delta));
                    break;
                }
                default:
                    return STATUS_INVALID_IMAGE_FORMAT;
            }
        }

        pos += size_of_block;
    }

    return STATUS_SUCCESS;
}

NTSTATUS WINAPI NtdllArm64xApplyDynamicRelocations(
    PVOID ImageBase,
    SIZE_T ImageSize)
{
    uint8_t *img;
    uint32_t lfanew;
    uint16_t num_sections;
    uint16_t opt_hdr_size;
    uint16_t opt_magic;
    uint32_t load_cfg_rva;
    uint32_t load_cfg_size;
    uint32_t dvrt_sec_offset;
    uint16_t dvrt_sec_index;
    size_t sec_table_off;
    size_t target_sec_off;
    uint32_t sec_va;
    uint32_t sec_raw_size;
    uint32_t dvrt_rva;
    uint32_t dvrt_version;
    uint32_t dvrt_total_size;
    const uint8_t *dvrt_body;
    size_t dvrt_pos;

    if (is_stub_simulation_enabled()) {
        return STATUS_NOT_IMPLEMENTED;
    }
    if (ImageBase == NULL || ImageSize < 0x200U) {
        return STATUS_INVALID_PARAMETER;
    }

    img = (uint8_t *)ImageBase;
    if (read_u16_le(img) != 0x5A4DU) {
        return STATUS_INVALID_IMAGE_FORMAT;
    }

    lfanew = read_u32_le(img + 0x3C);
    if (lfanew < 0x40U || (size_t)lfanew + 0x108U > ImageSize) {
        return STATUS_INVALID_IMAGE_FORMAT;
    }
    if (read_u32_le(img + lfanew) != 0x00004550U) {
        return STATUS_INVALID_IMAGE_FORMAT;
    }

    num_sections = read_u16_le(img + lfanew + 6U);
    opt_hdr_size = read_u16_le(img + lfanew + 20U);
    opt_magic = read_u16_le(img + lfanew + 24U);
    if (opt_magic != 0x020BU || num_sections == 0U) {
        return STATUS_INVALID_IMAGE_FORMAT;
    }

    load_cfg_rva = read_u32_le(img + lfanew + 24U + 0x70U + 10U * 8U);
    load_cfg_size = read_u32_le(img + lfanew + 24U + 0x70U + 10U * 8U + 4U);
    if (load_cfg_rva == 0U || load_cfg_size < 0xDCU ||
        (size_t)load_cfg_rva + 0xDCU > ImageSize) {
        return STATUS_INVALID_IMAGE_FORMAT;
    }

    dvrt_sec_offset = read_u32_le(img + load_cfg_rva + 0xD4U);
    dvrt_sec_index = read_u16_le(img + load_cfg_rva + 0xD8U);
    if (dvrt_sec_index == 0U || dvrt_sec_index > num_sections) {
        return STATUS_INVALID_IMAGE_FORMAT;
    }

    sec_table_off = (size_t)lfanew + 24U + (size_t)opt_hdr_size;
    target_sec_off = sec_table_off + (size_t)(dvrt_sec_index - 1U) * 40U;
    if (target_sec_off + 40U > ImageSize) {
        return STATUS_INVALID_IMAGE_FORMAT;
    }

    sec_va = read_u32_le(img + target_sec_off + 12U);
    sec_raw_size = read_u32_le(img + target_sec_off + 16U);
    if (dvrt_sec_offset >= sec_raw_size) {
        return STATUS_INVALID_IMAGE_FORMAT;
    }

    dvrt_rva = sec_va + dvrt_sec_offset;
    if ((size_t)dvrt_rva + 8U > ImageSize) {
        return STATUS_INVALID_IMAGE_FORMAT;
    }

    dvrt_version = read_u32_le(img + dvrt_rva);
    dvrt_total_size = read_u32_le(img + dvrt_rva + 4U);
    if ((dvrt_version != 1U && dvrt_version != 2U) ||
        dvrt_total_size < 12U ||
        (size_t)dvrt_rva + 8U + (size_t)dvrt_total_size > ImageSize) {
        return STATUS_INVALID_IMAGE_FORMAT;
    }

    dvrt_body = img + dvrt_rva + 8U;
    dvrt_pos = 0U;

    while (dvrt_pos < dvrt_total_size) {
        if (dvrt_version == 1U) {
            uint64_t symbol;
            uint32_t base_reloc_size;

            if (dvrt_pos + 12U > dvrt_total_size) {
                return STATUS_INVALID_IMAGE_FORMAT;
            }
            symbol = read_u64_le(dvrt_body + dvrt_pos);
            base_reloc_size = read_u32_le(dvrt_body + dvrt_pos + 8U);
            if (dvrt_pos + 12U + base_reloc_size > dvrt_total_size) {
                return STATUS_INVALID_IMAGE_FORMAT;
            }
            if (symbol == NTDLL_IMAGE_DYNAMIC_RELOCATION_ARM64X) {
                NTSTATUS st = apply_arm64x_fixup_blocks(
                    img, ImageSize, dvrt_body + dvrt_pos + 12U, base_reloc_size);
                if (st != STATUS_SUCCESS) {
                    return st;
                }
            }
            dvrt_pos += 12U + base_reloc_size;
        } else {
            uint32_t hdr_size;
            uint32_t fixup_size;
            uint64_t symbol;

            if (dvrt_pos + 24U > dvrt_total_size) {
                return STATUS_INVALID_IMAGE_FORMAT;
            }
            hdr_size = read_u32_le(dvrt_body + dvrt_pos);
            fixup_size = read_u32_le(dvrt_body + dvrt_pos + 4U);
            symbol = read_u64_le(dvrt_body + dvrt_pos + 8U);
            if (hdr_size < 24U || dvrt_pos + hdr_size + fixup_size > dvrt_total_size) {
                return STATUS_INVALID_IMAGE_FORMAT;
            }
            if (symbol == NTDLL_IMAGE_DYNAMIC_RELOCATION_ARM64X) {
                NTSTATUS st = apply_arm64x_fixup_blocks(
                    img, ImageSize, dvrt_body + dvrt_pos + hdr_size, fixup_size);
                if (st != STATUS_SUCCESS) {
                    return st;
                }
            }
            dvrt_pos += (size_t)hdr_size + (size_t)fixup_size;
        }
    }

    return STATUS_SUCCESS;
}

NTSTATUS WINAPI NtReadFileArm64xPeView(
    const WCHAR *FilePath,
    USHORT CallerMachine,
    const void *RawFileBytes,
    SIZE_T RawFileSize,
    ULONGLONG ByteOffset,
    PVOID Buffer,
    ULONG Length,
    ULONG *BytesRead)
{
    size_t avail;
    size_t to_copy;
    int need_relocated_view;

    if (is_stub_simulation_enabled()) {
        return STATUS_NOT_IMPLEMENTED;
    }
    if (FilePath == NULL || RawFileBytes == NULL || Buffer == NULL ||
        BytesRead == NULL || RawFileSize < 0x200U) {
        return STATUS_INVALID_PARAMETER;
    }

    *BytesRead = 0U;
    if (ByteOffset >= (ULONGLONG)RawFileSize) {
        return STATUS_END_OF_FILE;
    }

    avail = RawFileSize - (size_t)ByteOffset;
    to_copy = (size_t)Length < avail ? (size_t)Length : avail;

    need_relocated_view = (CallerMachine == IMAGE_FILE_MACHINE_AMD64 ||
                           CallerMachine == IMAGE_FILE_MACHINE_ARM64EC) &&
                          wstr_contains_system32(FilePath);

    if (need_relocated_view) {
        uint8_t *temp_img = (uint8_t *)malloc(RawFileSize);
        if (temp_img == NULL) {
            return STATUS_NO_MEMORY;
        }
        memcpy(temp_img, RawFileBytes, RawFileSize);
        (void)NtdllArm64xApplyDynamicRelocations(temp_img, RawFileSize);
        memcpy(Buffer, temp_img + (size_t)ByteOffset, to_copy);
        free(temp_img);
    } else {
        memcpy(Buffer, (const uint8_t *)RawFileBytes + (size_t)ByteOffset, to_copy);
    }

    *BytesRead = (ULONG)to_copy;
    return STATUS_SUCCESS;
}

/* =========================================================================
 * 2. TSC Registry Frequency Rescaling & NtQueryPerformanceCounter
 * ========================================================================= */

NTSTATUS WINAPI NtdllTscScaleConfigure(
    ULONGLONG HostCntFrqHz,
    ULONGLONG TargetTscHz,
    NTDLL_TSC_SCALE_STATE *OutState)
{
    uint64_t g;

    if (is_stub_simulation_enabled()) {
        return STATUS_NOT_IMPLEMENTED;
    }
    if (HostCntFrqHz == 0ULL || TargetTscHz == 0ULL || OutState == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    g = gcd_u64(TargetTscHz, HostCntFrqHz);
    g_tsc_scale_state.host_cntfrq_hz = HostCntFrqHz;
    g_tsc_scale_state.target_tsc_hz = TargetTscHz;
    g_tsc_scale_state.scale_numerator = TargetTscHz / g;
    g_tsc_scale_state.scale_denominator = HostCntFrqHz / g;
    g_tsc_scale_state.registry_mhz = (ULONG)(TargetTscHz / 1000000ULL);
    g_tsc_scale_state.configured = 1U;

    *OutState = g_tsc_scale_state;
    return STATUS_SUCCESS;
}

NTSTATUS WINAPI NtdllTscScaleReadTicks(
    ULONGLONG RawHostCntVct,
    ULONGLONG *OutScaledRdtsc)
{
    if (is_stub_simulation_enabled()) {
        return STATUS_NOT_IMPLEMENTED;
    }
    if (OutScaledRdtsc == NULL || g_tsc_scale_state.scale_denominator == 0ULL) {
        return STATUS_INVALID_PARAMETER;
    }

#if defined(__SIZEOF_INT128__)
    {
        __uint128_t prod = (__uint128_t)RawHostCntVct * (__uint128_t)g_tsc_scale_state.scale_numerator;
        *OutScaledRdtsc = (ULONGLONG)(prod / (__uint128_t)g_tsc_scale_state.scale_denominator);
    }
#else
    {
        uint64_t q = RawHostCntVct / g_tsc_scale_state.scale_denominator;
        uint64_t r = RawHostCntVct % g_tsc_scale_state.scale_denominator;
        *OutScaledRdtsc = (ULONGLONG)(q * g_tsc_scale_state.scale_numerator +
            (r * g_tsc_scale_state.scale_numerator) / g_tsc_scale_state.scale_denominator);
    }
#endif
    return STATUS_SUCCESS;
}

NTSTATUS WINAPI NtdllTscQueryRegistryMhz(
    const WCHAR *SubKeyPath,
    const WCHAR *ValueName,
    ULONG *OutMhz)
{
    static const char prefix_nt[] = "\\registry\\machine\\";
    static const char prefix_cpu[] = "hardware\\description\\system\\centralprocessor\\";
    const WCHAR *cursor;
    size_t skip_len = 0;
    size_t i;

    if (is_stub_simulation_enabled()) {
        return STATUS_NOT_IMPLEMENTED;
    }
    if (SubKeyPath == NULL || ValueName == NULL || OutMhz == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    cursor = SubKeyPath;
    if (wstr_starts_with_ascii_ci(cursor, prefix_nt, &skip_len)) {
        cursor += skip_len;
    }
    if (!wstr_starts_with_ascii_ci(cursor, prefix_cpu, &skip_len)) {
        return STATUS_OBJECT_NAME_NOT_FOUND;
    }
    cursor += skip_len;
    if (cursor[0] == 0) {
        return STATUS_OBJECT_NAME_NOT_FOUND;
    }
    for (i = 0; cursor[i] != 0; ++i) {
        if (cursor[i] < '0' || cursor[i] > '9') {
            return STATUS_OBJECT_NAME_NOT_FOUND;
        }
    }
    if (!wstr_equals_ascii_ci(ValueName, "~MHz")) {
        return STATUS_OBJECT_NAME_NOT_FOUND;
    }

    *OutMhz = g_tsc_scale_state.registry_mhz;
    return STATUS_SUCCESS;
}

NTSTATUS WINAPI NtQueryPerformanceCounter(
    PLARGE_INTEGER PerformanceCounter,
    PLARGE_INTEGER PerformanceFrequency)
{
    LONGLONG ticks_10mhz = 0;

    if (is_stub_simulation_enabled()) {
        return STATUS_NOT_IMPLEMENTED;
    }
    if (PerformanceCounter == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

#ifdef _WIN32
    ticks_10mhz = g_last_qpc_value + 100LL;
#else
    {
        struct timespec ts;
        if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0) {
            ticks_10mhz = (LONGLONG)ts.tv_sec * 10000000LL + (LONGLONG)(ts.tv_nsec / 100L);
        } else {
            ticks_10mhz = g_last_qpc_value + 100LL;
        }
    }
#endif

    if (ticks_10mhz <= g_last_qpc_value) {
        ticks_10mhz = g_last_qpc_value + 1LL;
    }
    g_last_qpc_value = ticks_10mhz;

    PerformanceCounter->QuadPart = ticks_10mhz;
    if (PerformanceFrequency != NULL) {
        PerformanceFrequency->QuadPart = 10000000LL;
    }
    return STATUS_SUCCESS;
}

/* =========================================================================
 * 3. On-Disk JIT Code Caching (WJTC Integrity-Checked ARM64 Block Cache)
 * ========================================================================= */

static void build_jit_block_path(
    char *out_path,
    size_t out_size,
    const NTDLL_JIT_MODULE_KEY *key,
    ULONG guest_rva)
{
    (void)snprintf(
        out_path,
        out_size,
        "%s/wjtc_%08x_%08x_%08x_%04x_%04x_%08x.bin",
        g_jit_cache_dir,
        (unsigned int)key->time_date_stamp,
        (unsigned int)key->checksum,
        (unsigned int)key->size_of_image,
        (unsigned int)key->guest_machine,
        (unsigned int)key->emulator_id,
        (unsigned int)guest_rva);
}

static void build_jit_module_prefix(
    char *out_prefix,
    size_t out_size,
    const NTDLL_JIT_MODULE_KEY *key)
{
    (void)snprintf(
        out_prefix,
        out_size,
        "wjtc_%08x_%08x_%08x_%04x_%04x_",
        (unsigned int)key->time_date_stamp,
        (unsigned int)key->checksum,
        (unsigned int)key->size_of_image,
        (unsigned int)key->guest_machine,
        (unsigned int)key->emulator_id);
}

static uint64_t compute_jit_block_hash(
    const NTDLL_JIT_MODULE_KEY *key,
    ULONG guest_rva,
    const void *code,
    size_t code_size)
{
    uint64_t seed = ((uint64_t)key->time_date_stamp << 32) ^
                    (uint64_t)key->checksum ^
                    ((uint64_t)guest_rva << 16) ^
                    (uint64_t)key->guest_machine;
    return fnv1a64_digest(code, code_size, seed);
}

NTSTATUS WINAPI NtdllJitCacheInit(const char *CacheDirPath)
{
    size_t len;

    if (is_stub_simulation_enabled()) {
        return STATUS_NOT_IMPLEMENTED;
    }
    if (CacheDirPath == NULL || CacheDirPath[0] == '\0') {
        return STATUS_INVALID_PARAMETER;
    }
    len = strlen(CacheDirPath);
    if (len >= sizeof(g_jit_cache_dir) - 64U) {
        return STATUS_INVALID_PARAMETER;
    }

    memcpy(g_jit_cache_dir, CacheDirPath, len + 1U);
#ifdef _WIN32
    (void)_mkdir(g_jit_cache_dir);
#else
    (void)mkdir(g_jit_cache_dir, 0700);
#endif

    memset(&g_jit_cache_stats, 0, sizeof(g_jit_cache_stats));
    g_jit_cache_stats.initialized = 1U;
    return STATUS_SUCCESS;
}

NTSTATUS WINAPI NtdllJitCacheStoreBlock(
    const NTDLL_JIT_MODULE_KEY *ModuleKey,
    ULONG GuestRva,
    const void *Arm64Code,
    SIZE_T CodeSize)
{
    char file_path[768];
    WJTC_DISK_HEADER hdr;
    FILE *fp;

    if (is_stub_simulation_enabled()) {
        return STATUS_NOT_IMPLEMENTED;
    }
    if (ModuleKey == NULL || Arm64Code == NULL || CodeSize == 0U ||
        CodeSize > WJTC_MAX_CODE_SIZE || (CodeSize & 3U) != 0U) {
        return STATUS_INVALID_PARAMETER;
    }
    if (!g_jit_cache_stats.initialized) {
        (void)NtdllJitCacheInit(g_jit_cache_dir);
    }

    memset(&hdr, 0, sizeof(hdr));
    hdr.magic = WJTC_MAGIC;
    hdr.version = WJTC_VERSION;
    hdr.time_date_stamp = ModuleKey->time_date_stamp;
    hdr.checksum = ModuleKey->checksum;
    hdr.size_of_image = ModuleKey->size_of_image;
    hdr.guest_machine = ModuleKey->guest_machine;
    hdr.emulator_id = ModuleKey->emulator_id;
    hdr.guest_rva = GuestRva;
    hdr.code_size = (uint32_t)CodeSize;
    hdr.payload_hash = compute_jit_block_hash(ModuleKey, GuestRva, Arm64Code, CodeSize);

    build_jit_block_path(file_path, sizeof(file_path), ModuleKey, GuestRva);
    fp = fopen(file_path, "wb");
    if (fp == NULL) {
        return STATUS_ACCESS_DENIED;
    }
    if (fwrite(&hdr, sizeof(hdr), 1, fp) != 1 ||
        fwrite(Arm64Code, 1, CodeSize, fp) != CodeSize) {
        fclose(fp);
        (void)remove(file_path);
        return STATUS_UNSUCCESSFUL;
    }
    fclose(fp);

    g_jit_cache_stats.cache_stores++;
    return STATUS_SUCCESS;
}

NTSTATUS WINAPI NtdllJitCacheLookupBlock(
    const NTDLL_JIT_MODULE_KEY *ModuleKey,
    ULONG GuestRva,
    PVOID OutArm64Code,
    SIZE_T BufferSize,
    SIZE_T *OutCodeSize)
{
    char file_path[768];
    WJTC_DISK_HEADER hdr;
    FILE *fp;
    uint8_t *payload;
    uint64_t actual_hash;

    if (is_stub_simulation_enabled()) {
        return STATUS_NOT_IMPLEMENTED;
    }
    if (ModuleKey == NULL || OutArm64Code == NULL || OutCodeSize == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    *OutCodeSize = 0U;
    build_jit_block_path(file_path, sizeof(file_path), ModuleKey, GuestRva);
    fp = fopen(file_path, "rb");
    if (fp == NULL) {
        g_jit_cache_stats.cache_misses++;
        return STATUS_OBJECT_NAME_NOT_FOUND;
    }

    if (fread(&hdr, sizeof(hdr), 1, fp) != 1 ||
        hdr.magic != WJTC_MAGIC ||
        hdr.version != WJTC_VERSION ||
        hdr.time_date_stamp != ModuleKey->time_date_stamp ||
        hdr.checksum != ModuleKey->checksum ||
        hdr.size_of_image != ModuleKey->size_of_image ||
        hdr.guest_machine != ModuleKey->guest_machine ||
        hdr.emulator_id != ModuleKey->emulator_id ||
        hdr.guest_rva != GuestRva ||
        hdr.code_size == 0U ||
        hdr.code_size > WJTC_MAX_CODE_SIZE) {
        fclose(fp);
        (void)remove(file_path);
        g_jit_cache_stats.corrupt_rejections++;
        return STATUS_CRC_ERROR;
    }

    *OutCodeSize = (SIZE_T)hdr.code_size;
    if (BufferSize < (SIZE_T)hdr.code_size) {
        fclose(fp);
        return STATUS_BUFFER_TOO_SMALL;
    }

    payload = (uint8_t *)malloc(hdr.code_size);
    if (payload == NULL) {
        fclose(fp);
        return STATUS_NO_MEMORY;
    }
    if (fread(payload, 1, hdr.code_size, fp) != hdr.code_size) {
        free(payload);
        fclose(fp);
        (void)remove(file_path);
        g_jit_cache_stats.corrupt_rejections++;
        return STATUS_CRC_ERROR;
    }
    fclose(fp);

    actual_hash = compute_jit_block_hash(ModuleKey, GuestRva, payload, hdr.code_size);
    if (actual_hash != hdr.payload_hash) {
        free(payload);
        (void)remove(file_path);
        g_jit_cache_stats.corrupt_rejections++;
        return STATUS_CRC_ERROR;
    }

    memcpy(OutArm64Code, payload, hdr.code_size);
    free(payload);

#ifdef __APPLE__
    sys_icache_invalidate(OutArm64Code, hdr.code_size);
#endif
    BTCpu64FlushInstructionCache(OutArm64Code, hdr.code_size);

    g_jit_cache_stats.cache_hits++;
    g_jit_cache_stats.icache_flushes++;
    return STATUS_SUCCESS;
}

NTSTATUS WINAPI NtdllJitCacheCorruptBlockForTest(
    const NTDLL_JIT_MODULE_KEY *ModuleKey,
    ULONG GuestRva)
{
    char file_path[768];
    FILE *fp;
    uint8_t byte_val = 0;

    if (is_stub_simulation_enabled()) {
        return STATUS_NOT_IMPLEMENTED;
    }
    if (ModuleKey == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    build_jit_block_path(file_path, sizeof(file_path), ModuleKey, GuestRva);
    fp = fopen(file_path, "r+b");
    if (fp == NULL) {
        return STATUS_OBJECT_NAME_NOT_FOUND;
    }
    if (fseek(fp, (long)sizeof(WJTC_DISK_HEADER), SEEK_SET) != 0 ||
        fread(&byte_val, 1, 1, fp) != 1) {
        fclose(fp);
        return STATUS_UNSUCCESSFUL;
    }
    byte_val ^= 0xFFU;
    if (fseek(fp, (long)sizeof(WJTC_DISK_HEADER), SEEK_SET) != 0 ||
        fwrite(&byte_val, 1, 1, fp) != 1) {
        fclose(fp);
        return STATUS_UNSUCCESSFUL;
    }
    fclose(fp);
    return STATUS_SUCCESS;
}

NTSTATUS WINAPI NtdllJitCacheInvalidateModule(
    const NTDLL_JIT_MODULE_KEY *ModuleKey,
    ULONG *OutRemovedCount)
{
    char prefix[128];
    ULONG removed = 0U;

    if (is_stub_simulation_enabled()) {
        return STATUS_NOT_IMPLEMENTED;
    }
    if (ModuleKey == NULL || OutRemovedCount == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    build_jit_module_prefix(prefix, sizeof(prefix), ModuleKey);

#ifndef _WIN32
    {
        DIR *dir = opendir(g_jit_cache_dir);
        if (dir != NULL) {
            struct dirent *ent;
            size_t prefix_len = strlen(prefix);
            while ((ent = readdir(dir)) != NULL) {
                if (strncmp(ent->d_name, prefix, prefix_len) == 0) {
                    char full_path[768];
                    (void)snprintf(full_path, sizeof(full_path), "%s/%s", g_jit_cache_dir, ent->d_name);
                    if (remove(full_path) == 0) {
                        removed++;
                    }
                }
            }
            closedir(dir);
        }
    }
#endif

    g_jit_cache_stats.invalidated_entries += removed;
    *OutRemovedCount = removed;
    return STATUS_SUCCESS;
}

NTSTATUS WINAPI NtdllJitCacheQueryStats(NTDLL_JIT_CACHE_STATS *OutStats)
{
    if (is_stub_simulation_enabled()) {
        return STATUS_NOT_IMPLEMENTED;
    }
    if (OutStats == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    *OutStats = g_jit_cache_stats;
    return STATUS_SUCCESS;
}

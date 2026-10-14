/*
 * Wine NTDLL x86_64 & WoW64 Signal / LDT / Rosetta 2 GSBASE Parity
 *
 * Copyright 2026 Wine Parity Contributors
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#ifdef __APPLE__
#include <mach/mach.h>
#endif

#include "ntdll.h"

#define WINE_DEFAULT_DEBUG_CHANNEL(ch)
#define TRACE(...) do { } while (0)
#define WARN(...)  do { } while (0)
#define FIXME(...) do { } while (0)
#define ERR(...)   do { } while (0)

WINE_DEFAULT_DEBUG_CHANNEL(seh);

#define MAX_LDT_ENTRIES 512U

static ULONGLONG g_ldt_teb64_base = 0x000000007ffde000ULL;
static int g_ldt_allocated = 0;
static NTDLL_LDT_ENTRY g_ldt_table[MAX_LDT_ENTRIES];
static uint8_t g_ldt_present[MAX_LDT_ENTRIES];

static NTDLL_ROSETTA_GSBASE_STATE g_rosetta_gsbase = {
    0x000000007ffde000ULL,
    0x000000007ffe0000ULL,
    0x000000007ffde000ULL,
    1U,
    0U,
    0U
};

static int is_stub_simulation_enabled(void)
{
    const char *env = getenv("WINE_PARITY_SIMULATE_STUB");
    return (env != NULL && strcmp(env, "1") == 0);
}

void WINAPI NtdllResetLdtState(ULONGLONG Teb64Base)
{
    g_ldt_teb64_base = Teb64Base ? Teb64Base : 0x000000007ffde000ULL;
    g_ldt_allocated = 0;
    memset(g_ldt_table, 0, sizeof(g_ldt_table));
    memset(g_ldt_present, 0, sizeof(g_ldt_present));
}

static void fill_gdt_entry(
    NTDLL_LDT_ENTRY *dst,
    uint32_t base,
    uint32_t limit,
    uint8_t type,
    uint8_t dpl,
    uint8_t default_big,
    uint8_t granularity,
    uint8_t long_mode)
{
    memset(dst, 0, sizeof(*dst));
    dst->LimitLow = (USHORT)(limit & 0xffffU);
    dst->BaseLow = (USHORT)(base & 0xffffU);
    dst->HighWord.Bytes.BaseMid = (BYTE)((base >> 16) & 0xffU);
    dst->HighWord.Bytes.BaseHi = (BYTE)((base >> 24) & 0xffU);
    dst->HighWord.Bits.Type = type & 0x1fU;
    dst->HighWord.Bits.Dpl = dpl & 0x03U;
    dst->HighWord.Bits.Pres = 1U;
    dst->HighWord.Bits.LimitHi = (limit >> 16) & 0x0fU;
    dst->HighWord.Bits.Sys = 0U;
    dst->HighWord.Bits.Reserved_0 = long_mode ? 1U : 0U;
    dst->HighWord.Bits.Default_Big = default_big ? 1U : 0U;
    dst->HighWord.Bits.Granularity = granularity ? 1U : 0U;
}

static NTSTATUS set_single_ldt_entry(ULONG selector, NTDLL_LDT_ENTRY entry)
{
    ULONG idx;

    if (selector == 0) {
        return STATUS_SUCCESS;
    }
    if (!(selector & 0x04U)) {
        return STATUS_INVALID_LDT_DESCRIPTOR;
    }
    idx = selector >> 3;
    if (idx >= MAX_LDT_ENTRIES) {
        return STATUS_INVALID_LDT_OFFSET;
    }
    g_ldt_allocated = 1;
    g_ldt_table[idx] = entry;
    g_ldt_present[idx] = 1U;
    return STATUS_SUCCESS;
}

NTSTATUS WINAPI NtSetLdtEntries(
    ULONG Selector1,
    NTDLL_LDT_ENTRY Entry1,
    ULONG Selector2,
    NTDLL_LDT_ENTRY Entry2)
{
    NTSTATUS status;

    if (is_stub_simulation_enabled()) {
        return STATUS_NOT_IMPLEMENTED;
    }
    status = set_single_ldt_entry(Selector1, Entry1);
    if (status != STATUS_SUCCESS) {
        return status;
    }
    return set_single_ldt_entry(Selector2, Entry2);
}

NTSTATUS WINAPI RtlWow64GetThreadSelectorEntry(
    HANDLE ThreadHandle,
    PNTDLL_DESCRIPTOR_TABLE_ENTRY SelectorEntry,
    ULONG Size,
    PULONG ReturnLength)
{
    ULONG sel;

    if (is_stub_simulation_enabled()) {
        return STATUS_NOT_IMPLEMENTED;
    }
    if (Size != sizeof(NTDLL_DESCRIPTOR_TABLE_ENTRY)) {
        return STATUS_INFO_LENGTH_MISMATCH;
    }
    if (ThreadHandle == NULL) {
        return STATUS_INVALID_HANDLE;
    }
    if (SelectorEntry == NULL) {
        return STATUS_ACCESS_VIOLATION;
    }

    sel = SelectorEntry->Selector | 3U;

    /* 0x03: null selector */
    if (sel == 0x03U) {
        memset(&SelectorEntry->Descriptor, 0, sizeof(SelectorEntry->Descriptor));
        if (ReturnLength != NULL) {
            *ReturnLength = (ULONG)sizeof(NTDLL_LDT_ENTRY);
        }
        return STATUS_SUCCESS;
    }

    /* 0x23 (x86_64/ARM64EC WoW64 CS32) or 0x1b (i386 CS32) */
    if (sel == 0x23U || sel == 0x1bU) {
        fill_gdt_entry(&SelectorEntry->Descriptor, 0U, 0xfffffU, 0x1bU, 3U, 1U, 1U, 0U);
        if (ReturnLength != NULL) {
            *ReturnLength = (ULONG)sizeof(NTDLL_LDT_ENTRY);
        }
        return STATUS_SUCCESS;
    }

    /* 0x2b: 32-bit/64-bit data selector (SS/DS/ES) */
    if (sel == 0x2bU) {
        fill_gdt_entry(&SelectorEntry->Descriptor, 0U, 0xfffffU, 0x13U, 3U, 1U, 1U, 0U);
        if (ReturnLength != NULL) {
            *ReturnLength = (ULONG)sizeof(NTDLL_LDT_ENTRY);
        }
        return STATUS_SUCCESS;
    }

    /* 0x33: 64-bit code selector (CS64) */
    if (sel == 0x33U) {
        fill_gdt_entry(&SelectorEntry->Descriptor, 0U, 0xfffffU, 0x1bU, 3U, 0U, 1U, 1U);
        if (ReturnLength != NULL) {
            *ReturnLength = (ULONG)sizeof(NTDLL_LDT_ENTRY);
        }
        return STATUS_SUCCESS;
    }

    /* 0x53 (WoW64 FS32 TEB selector) or 0x3b (i386 FS32 TEB selector) */
    if (sel == 0x53U || sel == 0x3bU) {
        uint32_t teb32 = (uint32_t)(g_ldt_teb64_base + 0x2000ULL);
        fill_gdt_entry(&SelectorEntry->Descriptor, teb32, 0x00fffU, 0x13U, 3U, 1U, 0U, 0U);
        if (ReturnLength != NULL) {
            *ReturnLength = (ULONG)sizeof(NTDLL_LDT_ENTRY);
        }
        return STATUS_SUCCESS;
    }

    /* LDT selector (TI bit == 1, i.e., bit 2 set) */
    if (sel & 0x04U) {
        ULONG idx = SelectorEntry->Selector >> 3;
        if (!g_ldt_allocated) {
            return STATUS_NO_LDT;
        }
        if (idx >= MAX_LDT_ENTRIES || !g_ldt_present[idx]) {
            return STATUS_UNSUCCESSFUL;
        }
        SelectorEntry->Descriptor = g_ldt_table[idx];
        if (ReturnLength != NULL) {
            *ReturnLength = (ULONG)sizeof(NTDLL_LDT_ENTRY);
        }
        return STATUS_SUCCESS;
    }

    return STATUS_UNSUCCESSFUL;
}

NTSTATUS WINAPI NtQueryInformationThread(
    HANDLE ThreadHandle,
    ULONG ThreadInformationClass,
    PVOID ThreadInformation,
    ULONG ThreadInformationLength,
    PULONG ReturnLength)
{
    if (is_stub_simulation_enabled()) {
        return STATUS_NOT_IMPLEMENTED;
    }
    if (ThreadInformationClass == ThreadDescriptorTableEntry) {
        return RtlWow64GetThreadSelectorEntry(
            ThreadHandle,
            (PNTDLL_DESCRIPTOR_TABLE_ENTRY)ThreadInformation,
            ThreadInformationLength,
            ReturnLength);
    }
    return STATUS_INVALID_INFO_CLASS;
}

/* =========================================================================
 * Rosetta 2 GSBASE / _thread_set_tsd_base TEB Restoration & %gs: Fault Fixup
 * ========================================================================= */

NTSTATUS WINAPI NtdllRosettaInitGsbase(ULONGLONG Teb64Base, ULONG RosettaActive)
{
    if (is_stub_simulation_enabled()) {
        return STATUS_NOT_IMPLEMENTED;
    }
    memset(&g_rosetta_gsbase, 0, sizeof(g_rosetta_gsbase));
    g_rosetta_gsbase.teb64_base = Teb64Base ? Teb64Base : 0x000000007ffde000ULL;
    g_rosetta_gsbase.teb32_base = g_rosetta_gsbase.teb64_base + 0x2000ULL;
    g_rosetta_gsbase.current_gsbase = g_rosetta_gsbase.teb64_base;
    g_rosetta_gsbase.rosetta_active = RosettaActive ? 1U : 0U;
    g_rosetta_gsbase.tsd_set_count = 1U;
    g_rosetta_gsbase.gs_prefix_fault_fixup_count = 0U;
    return STATUS_SUCCESS;
}

NTSTATUS WINAPI NtdllRosettaCorruptGsbase(ULONGLONG CorruptedGsbase)
{
    if (is_stub_simulation_enabled()) {
        return STATUS_NOT_IMPLEMENTED;
    }
    g_rosetta_gsbase.current_gsbase = CorruptedGsbase;
    return STATUS_SUCCESS;
}

static int is_x86_64_prefix_byte(BYTE b)
{
    switch (b) {
    case 0x26: /* ES */
    case 0x2e: /* CS */
    case 0x36: /* SS */
    case 0x3e: /* DS */
    case 0x64: /* FS */
    case 0x65: /* GS */
    case 0x66: /* Operand-size override */
    case 0x67: /* Address-size override */
    case 0xf0: /* LOCK */
    case 0xf2: /* REPNE */
    case 0xf3: /* REPE */
        return 1;
    default:
        /* REX prefixes 0x40..0x4f */
        return (b >= 0x40U && b <= 0x4fU);
    }
}

NTSTATUS WINAPI NtdllRosettaCheckInvalidGsbase(
    ULONG_PTR FaultRip,
    const BYTE *InstrBytes,
    SIZE_T InstrLen,
    NTDLL_ROSETTA_GSBASE_STATE *OutState)
{
    SIZE_T i;
    int has_gs_prefix = 0;

    (void)FaultRip;

    if (is_stub_simulation_enabled()) {
        return STATUS_NOT_IMPLEMENTED;
    }
    if (InstrBytes == NULL || InstrLen == 0) {
        return STATUS_INVALID_PARAMETER;
    }

    /* Scan instruction prefixes up to 15 bytes (maximum x86_64 instruction length) */
    for (i = 0; i < InstrLen && i < 15U; i++) {
        if (!is_x86_64_prefix_byte(InstrBytes[i])) {
            break;
        }
        if (InstrBytes[i] == 0x65U) {
            has_gs_prefix = 1;
        }
    }

    if (!has_gs_prefix) {
        if (OutState != NULL) {
            *OutState = g_rosetta_gsbase;
        }
        return STATUS_ACCESS_VIOLATION;
    }

    if (g_rosetta_gsbase.current_gsbase == g_rosetta_gsbase.teb64_base) {
        if (OutState != NULL) {
            *OutState = g_rosetta_gsbase;
        }
        return STATUS_ACCESS_VIOLATION;
    }

    /* Restore GSBASE to TEB64 via Darwin _thread_set_tsd_base semantics and retry instruction */
    g_rosetta_gsbase.current_gsbase = g_rosetta_gsbase.teb64_base;
    g_rosetta_gsbase.tsd_set_count++;
    g_rosetta_gsbase.gs_prefix_fault_fixup_count++;

    if (OutState != NULL) {
        *OutState = g_rosetta_gsbase;
    }
    return STATUS_SUCCESS;
}

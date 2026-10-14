/*
 * Wine kernelbase.dll Stub Discovery Reference Unit
 *
 * Copyright 2026 Wine Parity Contributors
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "../../tests/include/parity_test.h"

#define WINE_DEFAULT_DEBUG_CHANNEL(ch)
#define FIXME(...) do { } while (0)

WINE_DEFAULT_DEBUG_CHANNEL(kernelbase);

BOOL WINAPI QueryProcessCycleTime(HANDLE hProcess, uint64_t *CycleTime)
{
    (void)hProcess;
    (void)CycleTime;
    FIXME("(%p, %p): stub\n", hProcess, CycleTime);
    return FALSE;
}

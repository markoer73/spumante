/*
 * Wine winegstreamer.dll Lifecycle & Export Stubs
 *
 * Copyright 2026 Wine Parity Contributors
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "winegstreamer.h"

WINE_DEFAULT_DEBUG_CHANNEL(winegstreamer);

HRESULT WINAPI DllCanUnloadNow(void)
{
    TRACE("() -> S_FALSE\n");
    return S_FALSE;
}

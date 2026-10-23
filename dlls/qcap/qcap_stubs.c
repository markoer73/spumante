/*
 * Wine qcap.dll Stub Inventory (Tracked Unimplemented Filter Internals)
 *
 * Copyright 2026 Wine Parity Contributors
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "qcap.h"

WINE_DEFAULT_DEBUG_CHANNEL(qcap);

HRESULT WINAPI QcapAviMuxSetInterleaveModeStub(DWORD mode)
{
    (void)mode;
    FIXME("(%u): stub\n", (unsigned int)mode);
    return E_NOTIMPL;
}

/*
 * Wine mfplat.dll Stub Discovery Reference Unit
 *
 * Copyright 2026 Spumante Contributors
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "../../tests/include/parity_test.h"

#define WINE_DEFAULT_DEBUG_CHANNEL(ch)
#define FIXME(...) do { } while (0)

WINE_DEFAULT_DEBUG_CHANNEL(mfplat);

NTSTATUS WINAPI MFCreateMediaBufferFromMediaType(void *pMediaType, void **ppBuffer)
{
    (void)pMediaType;
    (void)ppBuffer;
    FIXME("(%p, %p): stub\n", pMediaType, ppBuffer);
    return STATUS_NOT_IMPLEMENTED;
}

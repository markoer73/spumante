/*
 * Wine d2d1.dll Stub Inventory (Tracked Unimplemented Direct2D Internals)
 *
 * Copyright (C) 2026 Spumante Project
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

#include "d2d1.h"

WINE_DEFAULT_DEBUG_CHANNEL(d2d1);

HRESULT WINAPI D2D1CreatePrintControlStub(void *wic_factory, void *doc_target, void **print_control)
{
    (void)wic_factory;
    (void)doc_target;
    (void)print_control;
    FIXME("(%p, %p, %p): stub\n", wic_factory, doc_target, (void *)print_control);
    return E_NOTIMPL;
}

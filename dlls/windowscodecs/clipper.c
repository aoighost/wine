/*
 * Copyright 2013 Nikolay Sivov for CodeWeavers
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include <stdarg.h>

#define COBJMACROS

#include "windef.h"
#include "winbase.h"
#include "objbase.h"
#include "wincodec.h"

#include "wincodecs_private.h"

#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(wincodecs);

typedef struct BitmapClipper {
    IWICBitmapClipper IWICBitmapClipper_iface;
    LONG ref;
    IWICBitmapSource *source;
    WICRect rect;
    CRITICAL_SECTION lock; /* must be held when initialized */
} BitmapClipper;

static inline BitmapClipper *impl_from_IWICBitmapClipper(IWICBitmapClipper *iface)
{
    return CONTAINING_RECORD(iface, BitmapClipper, IWICBitmapClipper_iface);
}

static HRESULT WINAPI BitmapClipper_QueryInterface(IWICBitmapClipper *iface, REFIID iid,
    void **ppv)
{
    BitmapClipper *This = impl_from_IWICBitmapClipper(iface);
    TRACE("(%p,%s,%p)\n", iface, debugstr_guid(iid), ppv);

    if (!ppv) return E_INVALIDARG;

    if (IsEqualIID(&IID_IUnknown, iid) ||
        IsEqualIID(&IID_IWICBitmapSource, iid) ||
        IsEqualIID(&IID_IWICBitmapClipper, iid))
    {
        *ppv = &This->IWICBitmapClipper_iface;
    }
    else
    {
        *ppv = NULL;
        return E_NOINTERFACE;
    }

    IUnknown_AddRef((IUnknown*)*ppv);
    return S_OK;
}

static ULONG WINAPI BitmapClipper_AddRef(IWICBitmapClipper *iface)
{
    BitmapClipper *This = impl_from_IWICBitmapClipper(iface);
    ULONG ref = InterlockedIncrement(&This->ref);

    TRACE("(%p) refcount=%u\n", iface, ref);

    return ref;
}

static ULONG WINAPI BitmapClipper_Release(IWICBitmapClipper *iface)
{
    BitmapClipper *This = impl_from_IWICBitmapClipper(iface);
    ULONG ref = InterlockedDecrement(&This->ref);

    TRACE("(%p) refcount=%u\n", iface, ref);

    if (ref == 0)
    {
        This->lock.DebugInfo->Spare[0] = 0;
        DeleteCriticalSection(&This->lock);
        if (This->source) IWICBitmapSource_Release(This->source);
        HeapFree(GetProcessHeap(), 0, This);
    }

    return ref;
}

static HRESULT WINAPI BitmapClipper_GetSize(IWICBitmapClipper *iface,
    UINT *width, UINT *height)
{
    BitmapClipper *This = impl_from_IWICBitmapClipper(iface);

    TRACE("(%p,%p,%p)\n", iface, width, height);

    if (!width || !height)
        return E_INVALIDARG;

    if (!This->source)
        return WINCODEC_ERR_WRONGSTATE;

    *width = This->rect.Width;
    *height = This->rect.Height;

    return S_OK;
}

static HRESULT WINAPI BitmapClipper_GetPixelFormat(IWICBitmapClipper *iface,
    WICPixelFormatGUID *format)
{
    BitmapClipper *This = impl_from_IWICBitmapClipper(iface);
    TRACE("(%p,%p)\n", iface, format);

    if (!format)
        return E_INVALIDARG;

    if (!This->source)
        return WINCODEC_ERR_WRONGSTATE;

    return IWICBitmapSource_GetPixelFormat(This->source, format);
}

static HRESULT WINAPI BitmapClipper_GetResolution(IWICBitmapClipper *iface,
    double *dpi_x, double *dpi_y)
{
    FIXME("(%p,%p,%p): stub\n", iface, dpi_x, dpi_y);
    return E_NOTIMPL;
}

static HRESULT WINAPI BitmapClipper_CopyPalette(IWICBitmapClipper *iface,
    IWICPalette *palette)
{
    FIXME("(%p,%p): stub\n", iface, palette);
    return E_NOTIMPL;
}

static HRESULT WINAPI BitmapClipper_CopyPixels(IWICBitmapClipper *iface,
    const WICRect *rc, UINT stride, UINT buffer_size, BYTE *buffer)
{
    FIXME("(%p,%p,%u,%u,%p): stub\n", iface, rc, stride, buffer_size, buffer);
    return E_NOTIMPL;
}

static HRESULT WINAPI BitmapClipper_Initialize(IWICBitmapClipper *iface,
    IWICBitmapSource *source, const WICRect *rc)
{
    BitmapClipper *This = impl_from_IWICBitmapClipper(iface);
    UINT width, height;
    HRESULT hr = S_OK;

    TRACE("(%p,%p,%p)\n", iface, source, rc);

    EnterCriticalSection(&This->lock);

    if (This->source)
    {
        hr = WINCODEC_ERR_WRONGSTATE;
        goto end;
    }

    hr = IWICBitmapSource_GetSize(source, &width, &height);
    if (FAILED(hr)) goto end;

    if  ((rc->X + rc->Width > width) || (rc->Y + rc->Height > height))
    {
        hr = E_INVALIDARG;
        goto end;
    }

    This->rect = *rc;
    This->source = source;
    IWICBitmapSource_AddRef(This->source);

end:
    LeaveCriticalSection(&This->lock);

    return hr;
}

static const IWICBitmapClipperVtbl BitmapClipper_Vtbl = {
    BitmapClipper_QueryInterface,
    BitmapClipper_AddRef,
    BitmapClipper_Release,
    BitmapClipper_GetSize,
    BitmapClipper_GetPixelFormat,
    BitmapClipper_GetResolution,
    BitmapClipper_CopyPalette,
    BitmapClipper_CopyPixels,
    BitmapClipper_Initialize
};

HRESULT BitmapClipper_Create(IWICBitmapClipper **clipper)
{
    BitmapClipper *This;

    This = HeapAlloc(GetProcessHeap(), 0, sizeof(BitmapClipper));
    if (!This) return E_OUTOFMEMORY;

    This->IWICBitmapClipper_iface.lpVtbl = &BitmapClipper_Vtbl;
    This->ref = 1;
    This->source = NULL;
    InitializeCriticalSection(&This->lock);
    This->lock.DebugInfo->Spare[0] = (DWORD_PTR)(__FILE__ ": BitmapClipper.lock");

    *clipper = &This->IWICBitmapClipper_iface;

    return S_OK;
}
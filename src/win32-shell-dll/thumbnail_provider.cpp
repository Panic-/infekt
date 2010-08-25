/**
 * Copyright (C) 2010 cxxjoe
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 **/

#include "stdafx.h"
#include "nfo_renderer.h"

/************************************************************************/
/* Class Declaration                                                    */
/************************************************************************/

class CNFOThumbProvider :
	public IThumbnailProvider,
	public IInitializeWithStream
{
private:
	long m_cRef;
	IStream *m_pStream;

public:
	CNFOThumbProvider()
	{
		m_cRef = 1;
		m_pStream = NULL;
	}

	virtual ~CNFOThumbProvider()
	{
		SafeRelease(&m_pStream);
	}

	// IUnknown
	IFACEMETHODIMP QueryInterface(REFIID riid, void **ppv)
	{
		static const QITAB qit[] =
		{
			QITABENT(CNFOThumbProvider, IInitializeWithStream),
			QITABENT(CNFOThumbProvider, IThumbnailProvider),
			{ 0 },
		};

		return QISearch(this, qit, riid, ppv);
	}

	IFACEMETHODIMP_(ULONG) AddRef()
	{
		return InterlockedIncrement(&m_cRef);
	}

	IFACEMETHODIMP_(ULONG) Release()
	{
		ULONG cRef = InterlockedDecrement(&m_cRef);
		if (!cRef)
		{
			delete this;
		}
		return cRef;
	}

	// IInitializeWithStream
	IFACEMETHODIMP Initialize(IStream *pStream, DWORD grfMode);

	// IThumbnailProvider
	IFACEMETHODIMP GetThumbnail(UINT cx, HBITMAP *phbmp, WTS_ALPHATYPE *pdwAlpha);
};


/************************************************************************/
/* Class Implementation                                                 */
/************************************************************************/

IFACEMETHODIMP CNFOThumbProvider::Initialize(IStream *pStream, DWORD)
{
	HRESULT hr = E_UNEXPECTED; // can only be initialized once

	if(!m_pStream)
	{
		// take a reference to the stream if we have not been initialized yet
		hr = pStream->QueryInterface(&m_pStream);
	}

	return hr;
}

IFACEMETHODIMP CNFOThumbProvider::GetThumbnail(UINT cx, HBITMAP *phbmp, WTS_ALPHATYPE *pdwAlpha)
{
	// read NFO data from stream:
	std::string l_contents;
	size_t l_contentLength = 0;
	char l_buf[200] = {0};

	//m_pStream->Seek(0, STREAM_SEEK_SET, NULL);

	ULONG l_bytesRead;
	do
	{
		HRESULT hr = m_pStream->Read(l_buf, 200, &l_bytesRead);

		if(hr == S_OK || (hr == S_FALSE && l_bytesRead < ARRAYSIZE(l_buf)))
		{
			l_contents.append(l_buf, l_bytesRead);
			l_contentLength += l_bytesRead;

			if(l_contentLength > 1024 * 1024)
			{
				// don't try to mess with files > 1MB.
				return S_FALSE;
			}
		}
		else
			break;
	} while(l_bytesRead == ARRAYSIZE(l_buf));

	// process NFO contents into CNFOData instance:
	PNFOData l_nfoData(new CNFOData());

	if(!l_nfoData->LoadFromMemory((const unsigned char*)l_contents.data(), l_contentLength))
	{
		return S_FALSE;
	}

	// set up renderer:
	CNFORenderer l_renderer;

	if(!l_renderer.AssignNFO(l_nfoData))
	{
		return S_FALSE;
	}

	// make some guesses based on the requested thumb nail size:
	if(cx < 256)
	{
		l_renderer.SetBlockSize(4, 7);
	}

	// render and copy to DIB section:
	int l_imgWidth = (int)l_renderer.GetWidth(), l_imgHeight = (int)l_renderer.GetHeight();

	BITMAPINFO l_bi = {0};
	l_bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	l_bi.bmiHeader.biWidth = l_imgWidth;
	l_bi.bmiHeader.biHeight = -l_imgHeight;
	l_bi.bmiHeader.biPlanes = 1;
	l_bi.bmiHeader.biBitCount = 32;
	l_bi.bmiHeader.biYPelsPerMeter = l_bi.bmiHeader.biXPelsPerMeter = 1000;

	unsigned char* l_rawData;
	HBITMAP l_hBitmap = CreateDIBSection(NULL, &l_bi, DIB_RGB_COLORS, (void**)&l_rawData, NULL, 0);

	HRESULT hr = S_FALSE;

	if(l_hBitmap)
	{
		BITMAP l_bitmap = {0};
		GetObject(l_hBitmap, sizeof(BITMAP), &l_bitmap);
		cairo_surface_t* l_surfaceOut = cairo_image_surface_create_for_data(
			l_rawData, CAIRO_FORMAT_ARGB32, l_imgWidth, l_imgHeight, l_bitmap.bmWidthBytes);

		if(l_surfaceOut)
		{
			if(l_renderer.DrawToSurface(l_surfaceOut, 0, 0, 0, 0, l_imgWidth, l_imgHeight))
			{
				*phbmp = l_hBitmap;
				*pdwAlpha = WTSAT_ARGB;

				hr = S_OK;
			}

			cairo_surface_destroy(l_surfaceOut);
		}

		//DeleteObject(l_hBitmap);
	}

	return hr;
}


/************************************************************************/
/* CreateInstance Export                                                */
/************************************************************************/

HRESULT CNFOThumbProvider_CreateInstance(REFIID riid, void **ppv)
{
	*ppv = NULL;

	CNFOThumbProvider *pNew = new (std::nothrow) CNFOThumbProvider();

	if(pNew)
	{
		HRESULT hr = pNew->QueryInterface(riid, ppv);
		pNew->Release();

		return hr;
	}

	return E_OUTOFMEMORY;
}
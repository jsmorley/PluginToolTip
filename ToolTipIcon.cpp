// Copyright (c) 2026 Jeffrey Morley. Source code licensed under GNU GPL v2 (see LICENSE file).

#include <d2d1_3.h>
#include <d2d1svg.h>
#include <filesystem>
#include <fstream>
#include "ToolTipMeasure.h"
#include "ToolTipGraphics.h"

using Microsoft::WRL::ComPtr;

namespace {
void ResolveIconSize(
	double requestedWidth, double requestedHeight, float ratio, float& width, float& height)
{
	if (!std::isfinite(ratio) || ratio <= 0)
	{
		ratio = 1;
	}
	width = static_cast<float>(requestedWidth > 0 ? std::clamp(requestedWidth, 1.0, 512.0) : 24);
	height = static_cast<float>(requestedHeight > 0 ? std::clamp(requestedHeight, 1.0, 512.0) : 24);
	if (requestedWidth > 0 && requestedHeight <= 0)
	{
		height = std::clamp(width / ratio, 1.0f, 512.0f);
	}
	else if (requestedHeight > 0 && requestedWidth <= 0)
	{
		width = std::clamp(height * ratio, 1.0f, 512.0f);
	}
}
}

void ToolTipMeasure::DestroyIconResource()
{
	if (m_IconOwned && m_Icon)
	{
		DestroyIcon(m_Icon);
	}
	m_Icon = nullptr;
	m_IconOwned = false;
}

HICON ToolTipMeasure::LoadSvgIcon(const std::wstring& source)
{
	// Rasterize at the skin's scale once per icon change, preserving alpha
	// and rectangular SVG viewports.
	std::string xml;
	if (!source.empty() && source.front() == L'<')
	{
		int count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, source.data(),
			static_cast<int>(source.size()), nullptr, 0, nullptr, nullptr);
		if (!count)
		{
			return nullptr;
		}
		xml.resize(count);
		WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, source.data(),
			static_cast<int>(source.size()), xml.data(), count, nullptr, nullptr);
	}
	else
	{
		std::wstring path = RmPathToAbsolute(m_Rm, source.c_str());
		std::ifstream file(std::filesystem::path(path), std::ios::binary | std::ios::ate);
		if (!file || file.tellg() <= 0 || file.tellg() > 16 * 1024 * 1024)
		{
			return nullptr;
		}
		xml.resize(static_cast<size_t>(file.tellg()));
		file.seekg(0);
		if (!file.read(xml.data(), static_cast<std::streamsize>(xml.size())))
		{
			return nullptr;
		}
	}
	if (xml.size() > 16 * 1024 * 1024 || !InitializeGraphics())
	{
		return nullptr;
	}
	ComPtr<ID2D1DeviceContext5> svgContext;
	ComPtr<IStream> stream;
	if (FAILED(m_Target.As(&svgContext)) ||
		FAILED(CreateStreamOnHGlobal(nullptr, TRUE, stream.GetAddressOf())))
	{
		return nullptr;
	}
	ULONG written = 0;
	if (FAILED(stream->Write(xml.data(), static_cast<ULONG>(xml.size()), &written)) ||
		written != xml.size())
	{
		return nullptr;
	}
	LARGE_INTEGER zero{};
	if (FAILED(stream->Seek(zero, STREAM_SEEK_SET, nullptr)))
	{
		return nullptr;
	}
	ComPtr<ID2D1SvgDocument> document;
	if (FAILED(svgContext->CreateSvgDocument(
			stream.Get(), D2D1::SizeF(24, 24), document.GetAddressOf())))
	{
		return nullptr;
	}
	ComPtr<ID2D1SvgElement> root;
	document->GetRoot(root.GetAddressOf());
	if (!root)
	{
		return nullptr;
	}
	D2D1_SVG_VIEWBOX viewBox{};
	D2D1_SVG_LENGTH intrinsicWidth{}, intrinsicHeight{};
	float ratio = 1;
	if (SUCCEEDED(root->GetAttributeValue(
			L"viewBox", D2D1_SVG_ATTRIBUTE_POD_TYPE_VIEWBOX, &viewBox, sizeof(viewBox))) &&
		viewBox.width > 0 && viewBox.height > 0)
	{
		ratio = viewBox.width / viewBox.height;
	}
	else if (SUCCEEDED(root->GetAttributeValue(L"width", &intrinsicWidth)) &&
		SUCCEEDED(root->GetAttributeValue(L"height", &intrinsicHeight)) &&
		intrinsicWidth.units == D2D1_SVG_LENGTH_UNITS_NUMBER &&
		intrinsicHeight.units == D2D1_SVG_LENGTH_UNITS_NUMBER && intrinsicWidth.value > 0 &&
		intrinsicHeight.value > 0)
	{
		ratio = intrinsicWidth.value / intrinsicHeight.value;
		// Supply intrinsic drawing bounds when the file has no viewBox.
		viewBox = {0, 0, intrinsicWidth.value, intrinsicHeight.value};
		root->SetAttributeValue(
			L"viewBox", D2D1_SVG_ATTRIBUTE_POD_TYPE_VIEWBOX, &viewBox, sizeof(viewBox));
	}
	ResolveIconSize(m_RequestedIconWidth, m_RequestedIconHeight, ratio, m_IconWidth, m_IconHeight);
	UINT width = static_cast<UINT>(std::ceil(m_IconWidth * m_Scale));
	UINT height = static_cast<UINT>(std::ceil(m_IconHeight * m_Scale));
	const D2D1_SVG_LENGTH full = {100, D2D1_SVG_LENGTH_UNITS_PERCENTAGE};
	if (FAILED(document->SetViewportSize(
			D2D1::SizeF(static_cast<float>(width), static_cast<float>(height)))) ||
		FAILED(root->SetAttributeValue(L"width", full)) ||
		FAILED(root->SetAttributeValue(L"height", full)))
	{
		return nullptr;
	}
	ComPtr<ID2D1Bitmap1> image, readback;
	auto props = D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET,
		D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
	if (FAILED(m_Target->CreateBitmap(
			D2D1::SizeU(width, height), nullptr, 0, &props, image.GetAddressOf())))
	{
		return nullptr;
	}
	m_Target->SetTarget(image.Get());
	m_Target->SetDpi(96, 96);
	m_Target->SetTransform(D2D1::Matrix3x2F::Identity());
	m_Target->BeginDraw();
	m_Target->Clear(D2D1::ColorF(0, 0));
	svgContext->DrawSvgDocument(document.Get());
	auto hr = m_Target->EndDraw();
	m_Target->SetTarget(nullptr);
	if (FAILED(hr))
	{
		return nullptr;
	}
	props.bitmapOptions = D2D1_BITMAP_OPTIONS_CPU_READ | D2D1_BITMAP_OPTIONS_CANNOT_DRAW;
	if (FAILED(m_Target->CreateBitmap(
			D2D1::SizeU(width, height), nullptr, 0, &props, readback.GetAddressOf())) ||
		FAILED(readback->CopyFromBitmap(nullptr, image.Get(), nullptr)))
	{
		return nullptr;
	}
	D2D1_MAPPED_RECT mapped{};
	if (FAILED(readback->Map(D2D1_MAP_OPTIONS_READ, &mapped)))
	{
		return nullptr;
	}
	BITMAPINFO info{};
	info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	info.bmiHeader.biWidth = width;
	info.bmiHeader.biHeight = -static_cast<LONG>(height);
	info.bmiHeader.biPlanes = 1;
	info.bmiHeader.biBitCount = 32;
	info.bmiHeader.biCompression = BI_RGB;
	void* bits = nullptr;
	HBITMAP color = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
	std::vector<BYTE> maskBits(((width + 15) / 16) * 2 * height, 0);
	HBITMAP mask = CreateBitmap(width, height, 1, 1, maskBits.data());
	HICON result = nullptr;
	if (color && mask)
	{
		for (UINT row = 0; row < height; ++row)
		{
			memcpy(static_cast<BYTE*>(bits) + row * width * 4, mapped.bits + row * mapped.pitch,
				width * 4);
		}
		ICONINFO ii{};
		ii.fIcon = TRUE;
		ii.hbmColor = color;
		ii.hbmMask = mask;
		result = CreateIconIndirect(&ii);
	}
	if (color)
	{
		DeleteObject(color);
	}
	if (mask)
	{
		DeleteObject(mask);
	}
	readback->Unmap();
	return result;
}

void ToolTipMeasure::ReadIcon()
{
	if (m_IconOwned && m_Icon)
	{
		DestroyIcon(m_Icon);
	}
	m_Icon = nullptr;
	m_IconOwned = false;
	m_RequestedIconWidth = m_Options.Number(L"IconW", 0);
	m_RequestedIconHeight = m_Options.Number(L"IconH", 0);
	ResolveIconSize(m_RequestedIconWidth, m_RequestedIconHeight, 1, m_IconWidth, m_IconHeight);
	m_IconScale = m_Scale;
	const auto source = ToolTip::Trim(m_Options.icon);
	const auto name = ToolTip::Lower(source);
	LPCWSTR id = name == L"info" || name == L"information" ? IDI_INFORMATION
		: name == L"warning"							   ? IDI_WARNING
		: name == L"error"								   ? IDI_ERROR
		: name == L"question"							   ? IDI_QUESTION
														   : nullptr;
	if (id)
	{
		m_Icon = ::LoadIconW(nullptr, id);
	}
	else if (!name.empty() && name != L"none" && name != L"0")
	{
		const bool svg = source.front() == L'<' ||
			ToolTip::Lower(std::filesystem::path(source).extension().wstring()) == L".svg";
		if (svg)
		{
			m_Icon = LoadSvgIcon(source);
			m_IconOwned = m_Icon != nullptr;
			if (!m_Icon)
			{
				RmLog(m_Rm, LOG_WARNING,
					L"ToolTip: SVG icon could not be loaded (check SVG syntax/file and Direct2D "
					L"SVG support)");
			}
			return;
		}
		std::wstring path = RmPathToAbsolute(m_Rm, m_Options.icon.c_str());
		m_Icon = static_cast<HICON>(
			LoadImageW(nullptr, path.c_str(), IMAGE_ICON, 0, 0, LR_LOADFROMFILE));
		m_IconOwned = m_Icon != nullptr;
		if (!m_Icon)
		{
			RmLog(m_Rm, LOG_WARNING,
				L"ToolTip: Icon file could not be loaded (use an .ico or .svg file, or inline "
				L"SVG)");
		}
	}
	ICONINFO info{};
	if (m_Icon && GetIconInfo(m_Icon, &info))
	{
		BITMAP bitmap{};
		const bool color = info.hbmColor != nullptr;
		if (GetObjectW(color ? info.hbmColor : info.hbmMask, sizeof(bitmap), &bitmap) &&
			bitmap.bmHeight > 0)
		{
			ResolveIconSize(m_RequestedIconWidth, m_RequestedIconHeight,
				static_cast<float>(bitmap.bmWidth) / (bitmap.bmHeight / (color ? 1 : 2)),
				m_IconWidth, m_IconHeight);
		}
		if (info.hbmColor)
		{
			DeleteObject(info.hbmColor);
		}
		if (info.hbmMask)
		{
			DeleteObject(info.hbmMask);
		}
	}
}

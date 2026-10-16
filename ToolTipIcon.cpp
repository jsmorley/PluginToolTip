// Copyright (c) 2026 Jeffrey Morley. Source code licensed under GNU GPL v2 (see LICENSE file).

#include <d2d1_3.h>
#include <d2d1svg.h>
#include <filesystem>
#include <fstream>
#include "ToolTipMeasure.h"
#include "ToolTipGraphics.h"

using Microsoft::WRL::ComPtr;

namespace {
ComPtr<ID2D1Bitmap1> RasterizeSvg(ID2D1DeviceContext* target, ID2D1DeviceContext5* svgContext,
	ID2D1SvgDocument* document, UINT width, UINT height, D2D1_SIZE_F viewport,
	D2D1_POINT_2F offset = {})
{
	if (FAILED(document->SetViewportSize(viewport)))
	{
		return {};
	}
	ComPtr<ID2D1Bitmap1> image, readback;
	auto properties = D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET,
		D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
	if (FAILED(target->CreateBitmap(
			D2D1::SizeU(width, height), nullptr, 0, &properties, image.GetAddressOf())))
	{
		return {};
	}
	target->SetTarget(image.Get());
	target->SetDpi(96, 96);
	target->SetTransform(D2D1::Matrix3x2F::Translation(offset.x, offset.y));
	target->BeginDraw();
	target->Clear(D2D1::ColorF(0, 0));
	svgContext->DrawSvgDocument(document);
	const auto result = target->EndDraw();
	target->SetTransform(D2D1::Matrix3x2F::Identity());
	target->SetTarget(nullptr);
	if (FAILED(result))
	{
		return {};
	}
	properties.bitmapOptions = D2D1_BITMAP_OPTIONS_CPU_READ | D2D1_BITMAP_OPTIONS_CANNOT_DRAW;
	if (FAILED(target->CreateBitmap(
			D2D1::SizeU(width, height), nullptr, 0, &properties, readback.GetAddressOf())) ||
		FAILED(readback->CopyFromBitmap(nullptr, image.Get(), nullptr)))
	{
		return {};
	}
	return readback;
}

std::optional<std::pair<float, float>> VerticalBounds(
	const D2D1_MAPPED_RECT& mapped, UINT width, UINT height)
{
	UINT first = height, last = 0;
	BYTE firstAlpha = 0, lastAlpha = 0, peakAlpha = 0;
	for (UINT row = 0; row < height; ++row)
	{
		const BYTE* pixels = mapped.bits + row * mapped.pitch;
		BYTE alpha = 0;
		for (UINT column = 0; column < width; ++column)
		{
			alpha = std::max(alpha, pixels[column * 4 + 3]);
			if (alpha == 255)
			{
				break;
			}
		}
		if (alpha)
		{
			if (first == height)
			{
				first = row;
				firstAlpha = alpha;
			}
			last = row;
			lastAlpha = alpha;
			peakAlpha = std::max(peakAlpha, alpha);
		}
	}
	if (first == height)
	{
		return {};
	}
	// Normalize edge coverage to retain bounds for translucent artwork too.
	return std::pair<float, float>{first + 1 - firstAlpha / static_cast<float>(peakAlpha),
		last + lastAlpha / static_cast<float>(peakAlpha)};
}

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
	// Trim vertical transparency before the final skin-scale rasterization,
	// preserving alpha and the SVG's original coordinate system.
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
	if (viewBox.width <= 0 || viewBox.height <= 0)
	{
		viewBox = {0, 0, m_IconWidth, m_IconHeight};
		if (FAILED(root->SetAttributeValue(
				L"viewBox", D2D1_SVG_ATTRIBUTE_POD_TYPE_VIEWBOX, &viewBox, sizeof(viewBox))))
		{
			return nullptr;
		}
	}
	const D2D1_SVG_LENGTH full = {100, D2D1_SVG_LENGTH_UNITS_PERCENTAGE};
	if (FAILED(root->SetAttributeValue(L"width", full)) ||
		FAILED(root->SetAttributeValue(L"height", full)))
	{
		return nullptr;
	}
	D2D1_SVG_PRESERVE_ASPECT_RATIO aspect{
		FALSE, D2D1_SVG_ASPECT_ALIGN_X_MID_Y_MID, D2D1_SVG_ASPECT_SCALING_MEET};
	root->GetAttributeValue(L"preserveAspectRatio", &aspect);
	const D2D1_SVG_PRESERVE_ASPECT_RATIO probeAspect{
		FALSE, D2D1_SVG_ASPECT_ALIGN_NONE, D2D1_SVG_ASPECT_SCALING_MEET};
	if (FAILED(root->SetAttributeValue(L"preserveAspectRatio", probeAspect)))
	{
		return nullptr;
	}

	// Inspect the original coordinate system without redefining the viewBox:
	// percentage-based artwork must keep its original meaning after trimming.
	constexpr UINT probeSize = 512;
	auto probe = RasterizeSvg(m_Target.Get(), svgContext.Get(), document.Get(), probeSize,
		probeSize, D2D1::SizeF(probeSize, probeSize));
	float cropTop = 0, cropHeight = 1;
	if (!probe)
	{
		return nullptr;
	}
	D2D1_MAPPED_RECT probePixels{};
	if (FAILED(probe->Map(D2D1_MAP_OPTIONS_READ, &probePixels)))
	{
		return nullptr;
	}
	const auto probeBounds = VerticalBounds(probePixels, probeSize, probeSize);
	if (probeBounds)
	{
		cropTop = probeBounds->first / probeSize;
		cropHeight = (probeBounds->second - probeBounds->first) / probeSize;
	}
	probe->Unmap();
	probe.Reset();
	if (FAILED(root->SetAttributeValue(L"preserveAspectRatio", aspect)))
	{
		return nullptr;
	}

	ratio = viewBox.width / (viewBox.height * cropHeight);
	ResolveIconSize(m_RequestedIconWidth, m_RequestedIconHeight, ratio, m_IconWidth, m_IconHeight);
	const float areaWidth = m_IconWidth * m_Scale, areaHeight = m_IconHeight * m_Scale;
	UINT width = static_cast<UINT>(std::ceil(areaWidth));
	UINT height = static_cast<UINT>(std::ceil(areaHeight));
	float scaleX = areaWidth / viewBox.width;
	float scaleY = areaHeight / (viewBox.height * cropHeight);
	float alignX = 0, alignY = 0;
	if (aspect.align != D2D1_SVG_ASPECT_ALIGN_NONE)
	{
		const float scale = aspect.meetOrSlice == D2D1_SVG_ASPECT_SCALING_SLICE
			? std::max(scaleX, scaleY)
			: std::min(scaleX, scaleY);
		scaleX = scaleY = scale;
		const int alignment = std::clamp(static_cast<int>(aspect.align), 1, 9) - 1;
		alignX = (alignment % 3) / 2.0f;
		alignY = (alignment / 3) / 2.0f;
	}
	const auto viewport = D2D1::SizeF(viewBox.width * scaleX, viewBox.height * scaleY);
	const auto offset = D2D1::Point2F((areaWidth - viewport.width) * alignX,
		(areaHeight - viewport.height * cropHeight) * alignY - viewport.height * cropTop);
	auto readback = RasterizeSvg(
		m_Target.Get(), svgContext.Get(), document.Get(), width, height, viewport, offset);
	if (!readback)
	{
		return nullptr;
	}
	D2D1_MAPPED_RECT mapped{};
	if (FAILED(readback->Map(D2D1_MAP_OPTIONS_READ, &mapped)))
	{
		return nullptr;
	}
	const auto bounds = VerticalBounds(mapped, width, height);
	const UINT firstRow = bounds ? static_cast<UINT>(std::floor(bounds->first)) : 0;
	const UINT lastRow =
		bounds ? std::min(height, static_cast<UINT>(std::ceil(bounds->second))) : height;
	const UINT trimmedHeight = lastRow - firstRow;
	BITMAPINFO info{};
	info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	info.bmiHeader.biWidth = width;
	info.bmiHeader.biHeight = -static_cast<LONG>(trimmedHeight);
	info.bmiHeader.biPlanes = 1;
	info.bmiHeader.biBitCount = 32;
	info.bmiHeader.biCompression = BI_RGB;
	void* bits = nullptr;
	HBITMAP color = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
	std::vector<BYTE> maskBits(((width + 15) / 16) * 2 * trimmedHeight, 0);
	HBITMAP mask = CreateBitmap(width, trimmedHeight, 1, 1, maskBits.data());
	HICON result = nullptr;
	if (color && mask)
	{
		for (UINT row = 0; row < trimmedHeight; ++row)
		{
			memcpy(static_cast<BYTE*>(bits) + row * width * 4,
				mapped.bits + (firstRow + row) * mapped.pitch, width * 4);
		}
		ICONINFO ii{};
		ii.fIcon = TRUE;
		ii.hbmColor = color;
		ii.hbmMask = mask;
		result = CreateIconIndirect(&ii);
		if (result && bounds)
		{
			m_IconHeight *= trimmedHeight / static_cast<float>(height);
			m_IconVerticalCenter =
				(bounds->first + bounds->second - 2 * firstRow) / (2 * trimmedHeight);
		}
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
	m_RowInk.reset();
	if (m_IconOwned && m_Icon)
	{
		DestroyIcon(m_Icon);
	}
	m_Icon = nullptr;
	m_IconOwned = false;
	m_IconVerticalCenter = 0.5f;
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

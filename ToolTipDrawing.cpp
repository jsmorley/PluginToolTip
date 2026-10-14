// Copyright (c) 2026 Jeffrey Morley. Source code licensed under GNU GPL v2 (see LICENSE file).

#include <filesystem>
#include "ToolTipMeasure.h"
#include "ToolTipGraphics.h"

using Microsoft::WRL::ComPtr;

void ToolTipMeasure::LoadFonts()
{
	m_TextFormat.reset();
	m_TitleFormat.reset();
	m_Fonts = Gfx::Canvas::Fonts();
	// Match runtime font support without referencing private Rainmeter objects.
	std::wstring resources = RmReplaceVariables(m_Rm, L"#@#Fonts\\");
	std::error_code ec;
	if (std::filesystem::is_directory(resources, ec))
	{
		for (std::filesystem::directory_iterator it(resources, ec), end; !ec && it != end;
			it.increment(ec))
		{
			const auto& entry = *it;
			auto ext = ToolTip::Lower(entry.path().extension().wstring());
			if (ext == L".ttf" || ext == L".otf" || ext == L".ttc")
			{
				m_Fonts->AddFile(entry.path().c_str());
			}
		}
	}
}

void ToolTipMeasure::DrawBorder(ID2D1Geometry* outline, const D2D1_RECT_F& body, ID2D1Brush* flat)
{
	if (m_Options.strokeBevel == L"none" || m_Options.bevelWidth == 0 || m_Options.bevelRelief == 0)
	{
		m_Target->DrawGeometry(outline, flat, m_Options.borderWidth);
		return;
	}
	auto widen = [&](float width, ComPtr<ID2D1PathGeometry>& path) {
		ComPtr<ID2D1GeometrySink> sink;
		return SUCCEEDED(ToolTip::GetGraphicsResources().factory->CreatePathGeometry(
				   path.GetAddressOf())) &&
			SUCCEEDED(path->Open(sink.GetAddressOf())) &&
			SUCCEEDED(outline->Widen(width, nullptr, nullptr, 0.1f / m_Scale, sink.Get())) &&
			SUCCEEDED(sink->Close());
	};
	auto band = [&](ID2D1Geometry* outer, ID2D1Geometry* inner, ID2D1Brush* brush) {
		if (!inner)
		{
			m_Target->FillGeometry(outer, brush);
			return;
		}
		ComPtr<ID2D1PathGeometry> ring;
		ComPtr<ID2D1GeometrySink> sink;
		if (SUCCEEDED(
				ToolTip::GetGraphicsResources().factory->CreatePathGeometry(ring.GetAddressOf())) &&
			SUCCEEDED(ring->Open(sink.GetAddressOf())) &&
			SUCCEEDED(outer->CombineWithGeometry(
				inner, D2D1_COMBINE_MODE_EXCLUDE, nullptr, 0.1f / m_Scale, sink.Get())) &&
			SUCCEEDED(sink->Close()))
		{
			m_Target->FillGeometry(ring.Get(), brush);
		}
	};
	const float w = std::max(1.0f, body.right - body.left),
				h = std::max(1.0f, body.bottom - body.top);
	const float radians = m_Options.bevelAngle * 3.14159265f / 180;
	const float dx = -std::cos(radians) / w, dy = -std::sin(radians) / h;
	const float extent = (std::abs(dx) * w + std::abs(dy) * h) / (2 * (dx * dx + dy * dy));
	const auto center = D2D1::Point2F((body.left + body.right) / 2, (body.top + body.bottom) / 2);
	auto brushFor = [&](float strength) {
		auto mix = [&](D2D1_COLOR_F color) {
			const float amount = std::abs(strength) * m_Options.bevelRelief;
			return D2D1::ColorF(m_Options.border.r + (color.r - m_Options.border.r) * amount,
				m_Options.border.g + (color.g - m_Options.border.g) * amount,
				m_Options.border.b + (color.b - m_Options.border.b) * amount,
				m_Options.border.a + (color.a - m_Options.border.a) * amount);
		};
		const bool raised = (m_Options.strokeBevel == L"raised") != (strength < 0);
		const auto light = mix(raised ? m_Options.bevelLight : m_Options.bevelDark);
		const auto dark = mix(raised ? m_Options.bevelDark : m_Options.bevelLight);
		const float fade = m_Options.bevelSmoothness / 2;
		D2D1_GRADIENT_STOP stops[] = {
			{0, light}, {0.5f - fade, light}, {0.5f + fade, dark}, {1, dark}};
		ComPtr<ID2D1GradientStopCollection> collection;
		ComPtr<ID2D1LinearGradientBrush> brush;
		if (SUCCEEDED(m_Target->CreateGradientStopCollection(stops, 4, collection.GetAddressOf())))
		{
			m_Target->CreateLinearGradientBrush(
				D2D1::LinearGradientBrushProperties(
					D2D1::Point2F(center.x - dx * extent, center.y - dy * extent),
					D2D1::Point2F(center.x + dx * extent, center.y + dy * extent)),
				collection.Get(), brush.GetAddressOf());
		}
		return brush;
	};
	// Keep the original single-stroke rendering for the default profile.
	if (m_Options.bevelProfile == L"linear" && m_Options.bevelWidth == m_Options.borderWidth)
	{
		auto brush = brushFor(1);
		m_Target->DrawGeometry(
			outline, brush ? static_cast<ID2D1Brush*>(brush.Get()) : flat, m_Options.borderWidth);
		return;
	}
	ComPtr<ID2D1PathGeometry> outer;
	if (!widen(m_Options.bevelWidth, outer))
	{
		m_Target->DrawGeometry(outline, flat, m_Options.borderWidth);
		return;
	}
	if (m_Options.bevelWidth < m_Options.borderWidth)
	{
		ComPtr<ID2D1PathGeometry> full;
		if (widen(m_Options.borderWidth, full))
		{
			band(full.Get(), outer.Get(), flat);
		}
	}
	// Disjoint contour bands avoid accumulating alpha over the background.
	// Curved softens toward the center; Concave recesses it; Bulged reverses
	// the slope midway across the band for a ridge with two lit faces.
	const int count = m_Options.bevelProfile == L"linear"
		? 1
		: std::clamp(static_cast<int>(std::ceil(m_Options.bevelWidth * m_Scale * 2)), 2, 32);
	for (int i = 0; i < count; ++i)
	{
		const float t = (i + 0.5f) / count;
		float strength = 1;
		if (m_Options.bevelProfile == L"curved")
		{
			strength = std::cos(t * 3.14159265f / 2);
		}
		else if (m_Options.bevelProfile == L"concave")
		{
			strength = -std::sin(t * 3.14159265f / 2);
		}
		else if (m_Options.bevelProfile == L"bulged")
		{
			strength = std::cos(t * 3.14159265f);
		}
		auto brush = brushFor(strength);
		ComPtr<ID2D1PathGeometry> inner;
		if (i + 1 < count && !widen(m_Options.bevelWidth * (1 - (i + 1.0f) / count), inner))
		{
			break;
		}
		band(outer.Get(), inner.Get(), brush ? static_cast<ID2D1Brush*>(brush.Get()) : flat);
		outer = inner;
	}
}

bool ToolTipMeasure::Draw(BYTE opacity)
{
	const float currentScale = ReadScale();
	if (currentScale != m_Scale)
	{
		m_Scale = currentScale;
		ReadIcon();
	}
	if (m_FixedAnchor)
	{
		m_Anchor = GetSkinAnchor(m_Scale);
	}
	if (!InitializeGraphics())
	{
		return false;
	}
	UpdateLayout();
	m_Origin = GetWindowPosition(m_PixelSize);
	auto props = D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET,
		D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96 * m_Scale,
		96 * m_Scale);
	auto dimensions = D2D1::SizeU(m_PixelSize.cx, m_PixelSize.cy);
	if (m_Bitmap)
	{
		auto current = m_Bitmap->GetPixelSize();
		float dpiX, dpiY;
		m_Bitmap->GetDpi(&dpiX, &dpiY);
		if (current.width != dimensions.width || current.height != dimensions.height ||
			dpiX != 96 * m_Scale || dpiY != 96 * m_Scale)
		{
			m_Bitmap.Reset();
			m_Staging.Reset();
		}
	}
	if (!m_Bitmap &&
		FAILED(m_Target->CreateBitmap(dimensions, nullptr, 0, &props, m_Bitmap.GetAddressOf())))
	{
		return false;
	}
	m_Target->SetTarget(m_Bitmap.Get());
	m_Target->SetDpi(96 * m_Scale, 96 * m_Scale);
	m_Target->SetTextAntialiasMode(m_Options.antiAlias ? D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE
													   : D2D1_TEXT_ANTIALIAS_MODE_ALIASED);
	m_Target->BeginDraw();
	m_Target->Clear(D2D1::ColorF(0, 0));
	ComPtr<ID2D1SolidColorBrush> fill, stroke;
	m_Target->CreateSolidColorBrush(m_Options.background, fill.GetAddressOf());
	m_Target->CreateSolidColorBrush(m_Options.border, stroke.GetAddressOf());
	if (!fill || !stroke)
	{
		m_Target->EndDraw();
		m_Target->SetTarget(nullptr);
		return false;
	}
	ComPtr<ID2D1Brush> background = fill;
	if (m_Options.linearGradient || m_Options.backgroundGradient)
	{
		D2D1_GRADIENT_STOP stops[] = {{0, m_Options.background}, {1, m_Options.background2}};
		const auto* gradientStops =
			m_Options.linearGradient ? m_Options.linearGradient->stops.data() : stops;
		const auto stopCount = m_Options.linearGradient
			? static_cast<UINT32>(m_Options.linearGradient->stops.size())
			: 2U;
		ComPtr<ID2D1GradientStopCollection> collection;
		ComPtr<ID2D1LinearGradientBrush> gradient;
		if (SUCCEEDED(m_Target->CreateGradientStopCollection(
				gradientStops, stopCount, collection.GetAddressOf())))
		{
			float radians = (m_Options.linearGradient ? m_Options.linearGradient->angle
													  : m_Options.gradientAngle) *
				3.14159265f / 180;
			auto center = D2D1::Point2F(m_BodyWidth / 2, (m_BodyHeight + m_TailHeight) / 2);
			float dx = std::cos(radians) * m_BodyWidth / 2,
				  dy = std::sin(radians) * (m_BodyHeight + m_TailHeight) / 2;
			if (m_Options.linearGradient)
			{
				// Project the rectangle onto the gradient's actual direction,
				// so the angle is preserved even on non-square tooltips.
				const float extent =
					(std::abs(std::cos(radians)) * m_BodyWidth +
						std::abs(std::sin(radians)) * (m_BodyHeight + m_TailHeight)) /
					2;
				dx = -std::cos(radians) * extent;
				dy = -std::sin(radians) * extent;
			}
			m_Target->CreateLinearGradientBrush(
				D2D1::LinearGradientBrushProperties(D2D1::Point2F(center.x - dx, center.y - dy),
					D2D1::Point2F(center.x + dx, center.y + dy)),
				collection.Get(), gradient.GetAddressOf());
			if (gradient)
			{
				background = gradient;
			}
		}
	}
	const float inset = m_Options.borderWidth / 2 + 0.5f / m_Scale;
	const float top = m_TailTop ? m_TailHeight : 0;
	const auto body =
		D2D1::RectF(inset, top + inset, m_BodyWidth - inset, top + m_BodyHeight - inset);
	if (m_Options.balloon)
	{
		// One closed path, so the stroke follows the tail without a seam across its base.
		float r = std::min(
			{m_Options.radius, (body.right - body.left) / 2, (body.bottom - body.top) / 2});
		float tx = std::clamp((m_Attachment.x - m_Origin.x) / m_Scale, body.left + r + 9,
			std::max(body.left + r + 9, body.right - r - 9));
		// A skin-relative or following-cursor tail stays anchored when the window is moved
		// away from a screen edge. Only its base must clear the rounded corners.
		const float tipX = IsTailAnchored() ? std::clamp((m_Attachment.x - m_Origin.x) / m_Scale,
												  body.left, std::max(body.left, body.right))
											: tx;
		ComPtr<ID2D1PathGeometry> geometry;
		ComPtr<ID2D1GeometrySink> sink;
		ToolTip::GetGraphicsResources().factory->CreatePathGeometry(geometry.GetAddressOf());
		if (geometry && SUCCEEDED(geometry->Open(sink.GetAddressOf())))
		{
			sink->BeginFigure(D2D1::Point2F(body.left + r, body.top), D2D1_FIGURE_BEGIN_FILLED);
			if (m_TailTop)
			{
				sink->AddLine(D2D1::Point2F(tx - 8, body.top));
				sink->AddLine(D2D1::Point2F(tipX, inset));
				sink->AddLine(D2D1::Point2F(tx + 8, body.top));
			}
			sink->AddLine(D2D1::Point2F(body.right - r, body.top));
			sink->AddArc(D2D1::ArcSegment(D2D1::Point2F(body.right, body.top + r),
				D2D1::SizeF(r, r), 0, D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL));
			sink->AddLine(D2D1::Point2F(body.right, body.bottom - r));
			sink->AddArc(D2D1::ArcSegment(D2D1::Point2F(body.right - r, body.bottom),
				D2D1::SizeF(r, r), 0, D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL));
			if (!m_TailTop)
			{
				sink->AddLine(D2D1::Point2F(tx + 8, body.bottom));
				sink->AddLine(D2D1::Point2F(tipX, m_BodyHeight + m_TailHeight - inset));
				sink->AddLine(D2D1::Point2F(tx - 8, body.bottom));
			}
			sink->AddLine(D2D1::Point2F(body.left + r, body.bottom));
			sink->AddArc(D2D1::ArcSegment(D2D1::Point2F(body.left, body.bottom - r),
				D2D1::SizeF(r, r), 0, D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL));
			sink->AddLine(D2D1::Point2F(body.left, body.top + r));
			sink->AddArc(D2D1::ArcSegment(D2D1::Point2F(body.left + r, body.top), D2D1::SizeF(r, r),
				0, D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL));
			sink->EndFigure(D2D1_FIGURE_END_CLOSED);
			sink->Close();
			m_Target->FillGeometry(geometry.Get(), background.Get());
			if (m_Options.borderWidth > 0)
			{
				DrawBorder(geometry.Get(), body, stroke.Get());
			}
		}
	}
	else
	{
		m_Target->FillRectangle(body, background.Get());
		if (m_Options.borderWidth > 0)
		{
			ComPtr<ID2D1RectangleGeometry> rectangle;
			if (SUCCEEDED(ToolTip::GetGraphicsResources().factory->CreateRectangleGeometry(
					body, rectangle.GetAddressOf())))
			{
				DrawBorder(rectangle.Get(), body, stroke.Get());
			}
		}
	}
	const float x = m_Options.padding[0] + m_Options.borderWidth;
	const float y = top + m_Options.padding[1] + m_Options.borderWidth;
	if (m_Icon && ToolTip::GetGraphicsResources().wic)
	{
		ComPtr<IWICBitmap> iconWic;
		ComPtr<IWICFormatConverter> converted;
		ComPtr<ID2D1Bitmap> iconBitmap;
		if (SUCCEEDED(ToolTip::GetGraphicsResources().wic->CreateBitmapFromHICON(
				m_Icon, iconWic.GetAddressOf())) &&
			SUCCEEDED(ToolTip::GetGraphicsResources().wic->CreateFormatConverter(
				converted.GetAddressOf())) &&
			SUCCEEDED(converted->Initialize(iconWic.Get(), GUID_WICPixelFormat32bppPBGRA,
				WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom)) &&
			SUCCEEDED(
				m_Target->CreateBitmapFromWicBitmap(converted.Get(), iconBitmap.GetAddressOf())))
		{
			m_Target->DrawBitmap(
				iconBitmap.Get(), D2D1::RectF(x, y, x + m_IconWidth, y + m_IconHeight));
		}
	}
	const float defaultTitleX = x + (m_Icon ? m_IconWidth + 6 : 0);
	const float titleX = m_Options.titleX.value_or(defaultTitleX);
	const float titleY =
		top + m_Options.titleY.value_or(m_Options.padding[1] + m_Options.borderWidth);
	if (m_HeaderHeight > 0)
	{
		const float titleWidth =
			m_BodyWidth - m_Options.padding[2] - (m_Options.close ? 24 : 0) - defaultTitleX;
		Gfx::Canvas::Draw(m_Target.Get(), m_Options.title, *m_TitleFormat,
			D2D1::RectF(titleX, titleY, titleX + titleWidth, titleY + m_HeaderHeight),
			m_Options.titleColor, true, true);
		if (m_Options.close)
		{
			ComPtr<ID2D1SolidColorBrush> closeBrush;
			m_Target->CreateSolidColorBrush(m_Options.font, closeBrush.GetAddressOf());
			const auto button = GetCloseButtonOrigin();
			const float cx = button.x + 12, cy = button.y + m_Options.borderWidth + 8;
			if (closeBrush)
			{
				m_Target->DrawLine(D2D1::Point2F(cx - 4, cy - 4), D2D1::Point2F(cx + 4, cy + 4),
					closeBrush.Get(), 1.5f);
				m_Target->DrawLine(D2D1::Point2F(cx + 4, cy - 4), D2D1::Point2F(cx - 4, cy + 4),
					closeBrush.Get(), 1.5f);
			}
		}
	}
	const auto rect = GetTextRect(top);
	if (m_Options.effect == L"shadow" || m_Options.effect == L"border")
	{
		const int offsets[][2] = {{1, 1}, {-1, 0}, {1, 0}, {0, -1}, {0, 1}};
		for (int i = m_Options.effect == L"shadow" ? 0 : 1;
			i < (m_Options.effect == L"shadow" ? 1 : 5); ++i)
		{
			auto r = rect;
			r.left += offsets[i][0];
			r.right += offsets[i][0];
			r.top += offsets[i][1];
			r.bottom += offsets[i][1];
			Gfx::Canvas::Draw(
				m_Target.Get(), m_Text, *m_TextFormat, r, m_Options.effectColor, false, true);
		}
	}
	Gfx::Canvas::Draw(m_Target.Get(), m_Text, *m_TextFormat, rect, m_Options.font, true, true);
	auto hr = m_Target->EndDraw();
	m_Target->SetTarget(nullptr);
	if (FAILED(hr))
	{
		return false;
	}
	props.bitmapOptions = D2D1_BITMAP_OPTIONS_CPU_READ | D2D1_BITMAP_OPTIONS_CANNOT_DRAW;
	if ((!m_Staging &&
			FAILED(m_Target->CreateBitmap(
				dimensions, nullptr, 0, &props, m_Staging.GetAddressOf()))) ||
		FAILED(m_Staging->CopyFromBitmap(nullptr, m_Bitmap.Get(), nullptr)))
	{
		return false;
	}
	D2D1_MAPPED_RECT mapped{};
	if (FAILED(m_Staging->Map(D2D1_MAP_OPTIONS_READ, &mapped)))
	{
		return false;
	}
	HDC screen = GetDC(nullptr), memory = CreateCompatibleDC(screen);
	BITMAPINFO info{};
	info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	info.bmiHeader.biWidth = m_PixelSize.cx;
	info.bmiHeader.biHeight = -m_PixelSize.cy;
	info.bmiHeader.biPlanes = 1;
	info.bmiHeader.biBitCount = 32;
	info.bmiHeader.biCompression = BI_RGB;
	void* bits = nullptr;
	HBITMAP dib = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
	bool ok = false;
	if (dib && memory)
	{
		for (LONG row = 0; row < m_PixelSize.cy; ++row)
		{
			memcpy(static_cast<BYTE*>(bits) + row * m_PixelSize.cx * 4,
				mapped.bits + row * mapped.pitch, m_PixelSize.cx * 4);
		}
		auto old = SelectObject(memory, dib);
		POINT zero{};
		BLENDFUNCTION blend{AC_SRC_OVER, 0, opacity, AC_SRC_ALPHA};
		ok = UpdateLayeredWindow(m_Window, screen, &m_Origin, &m_PixelSize, memory, &zero, 0,
				 &blend, ULW_ALPHA) != FALSE;
		SelectObject(memory, old);
	}
	if (dib)
	{
		DeleteObject(dib);
	}
	if (memory)
	{
		DeleteDC(memory);
	}
	ReleaseDC(nullptr, screen);
	m_Staging->Unmap();
	if (ok)
	{
		// Owned popups must remain above their skin. Do not demote a topmost
		// owner or raise its window group on every text update / fade frame:
		// Rainmeter's Stay Topmost timer would compete with those raises.
		const bool topmost =
			m_Options.topmost || (GetWindowLongPtrW(m_SkinWindow, GWL_EXSTYLE) & WS_EX_TOPMOST);
		const bool alreadyTopmost = (GetWindowLongPtrW(m_Window, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0;
		UINT flags = SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_SHOWWINDOW;
		if (topmost == alreadyTopmost)
		{
			flags |= SWP_NOZORDER;
		}
		SetWindowPos(m_Window, topmost ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0, flags);
		KeepAboveSkin(m_Window, m_SkinWindow);
	}
	return ok;
}

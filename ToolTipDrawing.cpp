// Copyright (c) 2026 Jeffrey Morley. Source code licensed under GNU GPL v2 (see LICENSE file).

#include <filesystem>
#include <d2d1_2.h>
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
	// Use Direct2D stroke rendering, as in Rainmeter's Shape meter.
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

bool ToolTipMeasure::DrawBalloon(
	const D2D1_RECT_F& body, ID2D1Brush* background, ID2D1Brush* stroke)
{
	const float tipInset = m_Options.borderWidth / 2 + 0.5f / m_Scale;
	float r =
		std::min({m_Options.radius, (body.right - body.left) / 2, (body.bottom - body.top) / 2});
	float tx = std::clamp((m_Attachment.x - m_Origin.x) / m_Scale, body.left + r + 9,
		std::max(body.left + r + 9, body.right - r - 9));
	// A skin-relative or following-cursor tail stays anchored when the window is moved
	// away from a screen edge. Only its base must clear the rounded corners.
	const float tipX = IsTailAnchored() ? std::clamp((m_Attachment.x - m_Origin.x) / m_Scale,
											  body.left, std::max(body.left, body.right))
										: tx;

	auto* factory = ToolTip::GetGraphicsResources().factory.Get();
	ComPtr<ID2D1RoundedRectangleGeometry> rounded;
	ComPtr<ID2D1PathGeometry> triangle, silhouette, tailStroke;
	ComPtr<ID2D1GeometrySink> sink;
	const float baseY = m_TailTop ? body.top : body.bottom;
	const float tipY = m_TailTop
		? tipInset
		: std::ceil((m_BodyHeight + m_TailHeight) * m_Scale) / m_Scale - tipInset;
	if (FAILED(factory->CreateRoundedRectangleGeometry(
			D2D1::RoundedRect(body, r, r), rounded.GetAddressOf())) ||
		FAILED(factory->CreatePathGeometry(triangle.GetAddressOf())) ||
		FAILED(triangle->Open(sink.GetAddressOf())))
	{
		return false;
	}
	sink->BeginFigure(D2D1::Point2F(tx - 8, baseY), D2D1_FIGURE_BEGIN_FILLED);
	if (m_TailTop)
	{
		sink->AddLine(D2D1::Point2F(tipX, tipY));
		sink->AddLine(D2D1::Point2F(tx + 8, baseY));
	}
	else
	{
		sink->AddLine(D2D1::Point2F(tx + 8, baseY));
		sink->AddLine(D2D1::Point2F(tipX, tipY));
	}
	sink->EndFigure(D2D1_FIGURE_END_CLOSED);
	HRESULT result = sink->Close();
	sink.Reset();
	if (FAILED(result) || FAILED(factory->CreatePathGeometry(silhouette.GetAddressOf())) ||
		FAILED(silhouette->Open(sink.GetAddressOf())))
	{
		return false;
	}
	// Keep a joined contour for crowded tails and older Direct2D hosts.
	result = rounded->CombineWithGeometry(
		triangle.Get(), D2D1_COMBINE_MODE_UNION, nullptr, 0.01f / m_Scale, sink.Get());
	const HRESULT closed = sink->Close();
	sink.Reset();
	if (FAILED(result) || FAILED(closed))
	{
		return false;
	}
	ComPtr<ID2D1DeviceContext1> additive;
	if (SUCCEEDED(m_Target.As(&additive)))
	{
		// The body and triangle share an edge without overlapping. Add their
		// coverage so a translucent background has no seam across that edge.
		m_Target->FillGeometry(rounded.Get(), background);
		const auto blend = m_Target->GetPrimitiveBlend();
		m_Target->SetPrimitiveBlend(D2D1_PRIMITIVE_BLEND_ADD);
		m_Target->FillGeometry(triangle.Get(), background);
		m_Target->SetPrimitiveBlend(blend);
	}
	else
	{
		m_Target->FillGeometry(silhouette.Get(), background);
	}
	if (m_Options.borderWidth > 0)
	{
		// Pixel-aligned clips keep the native rounded corners intact. Replace
		// only the straight tail-side segment with an open path through the tip.
		const float left = std::ceil((body.left + r) * m_Scale) / m_Scale;
		const float right = std::floor((body.right - r) * m_Scale) / m_Scale;
		const float gap = std::min(tx - 8 - left, right - tx - 8);
		bool native =
			left < right && gap >= m_Options.borderWidth / 2 && tipX >= left && tipX <= right;
		if (native && SUCCEEDED(factory->CreatePathGeometry(tailStroke.GetAddressOf())) &&
			SUCCEEDED(tailStroke->Open(sink.GetAddressOf())))
		{
			sink->BeginFigure(D2D1::Point2F(left, baseY), D2D1_FIGURE_BEGIN_HOLLOW);
			sink->AddLine(D2D1::Point2F(tx - 8, baseY));
			sink->AddLine(D2D1::Point2F(tipX, tipY));
			sink->AddLine(D2D1::Point2F(tx + 8, baseY));
			sink->AddLine(D2D1::Point2F(right, baseY));
			sink->EndFigure(D2D1_FIGURE_END_OPEN);
			native = SUCCEEDED(sink->Close());
			sink.Reset();
		}
		else
		{
			native = false;
		}
		if (native)
		{
			auto clippedBorder = [&](D2D1_RECT_F clip) {
				m_Target->PushAxisAlignedClip(clip, D2D1_ANTIALIAS_MODE_ALIASED);
				DrawBorder(rounded.Get(), body, stroke);
				m_Target->PopAxisAlignedClip();
			};
			const float width = m_PixelSize.cx / m_Scale;
			const float height = m_PixelSize.cy / m_Scale;
			clippedBorder(D2D1::RectF(0, 0, left, height));
			clippedBorder(D2D1::RectF(right, 0, width, height));
			const float middle = (body.top + body.bottom) / 2;
			clippedBorder(m_TailTop ? D2D1::RectF(left, middle, right, height)
									: D2D1::RectF(left, 0, right, middle));
			DrawBorder(tailStroke.Get(), body, stroke);
		}
		else
		{
			// Crowded or sharply leaning tails need the entire joined contour.
			DrawBorder(silhouette.Get(), body, stroke);
		}
	}
	return true;
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
			m_OutlineBitmap.Reset();
			m_OutlineIntermediate.Reset();
		}
	}
	if (!m_Bitmap &&
		FAILED(m_Target->CreateBitmap(dimensions, nullptr, 0, &props, m_Bitmap.GetAddressOf())))
	{
		return false;
	}
	// Supersample only the balloon surface. Two exact 2:1 linear reductions
	// average its subpixels without softening pixel-aligned straight borders.
	// Keep scratch storage below 8 MiB, including the intermediate surface.
	const auto pixels = static_cast<UINT64>(dimensions.width) * dimensions.height;
	UINT samples = m_Options.balloon ? (pixels <= 104857 ? 4 : pixels <= 524288 ? 2 : 1) : 1;
	if (samples > 1 && !m_OutlineBitmap)
	{
		auto outlineProps = props;
		outlineProps.dpiX *= samples;
		outlineProps.dpiY *= samples;
		HRESULT result = m_Target->CreateBitmap(
			D2D1::SizeU(dimensions.width * samples, dimensions.height * samples), nullptr, 0,
			&outlineProps, m_OutlineBitmap.GetAddressOf());
		if (SUCCEEDED(result) && samples == 4)
		{
			outlineProps.dpiX = props.dpiX * 2;
			outlineProps.dpiY = props.dpiY * 2;
			result =
				m_Target->CreateBitmap(D2D1::SizeU(dimensions.width * 2, dimensions.height * 2),
					nullptr, 0, &outlineProps, m_OutlineIntermediate.GetAddressOf());
		}
		if (FAILED(result))
		{
			m_OutlineBitmap.Reset();
			m_OutlineIntermediate.Reset();
		}
	}
	if (!m_OutlineBitmap)
	{
		samples = 1;
	}
	m_Target->SetTarget(samples > 1 ? m_OutlineBitmap.Get() : m_Bitmap.Get());
	m_Target->SetDpi(96 * m_Scale * samples, 96 * m_Scale * samples);
	m_Target->SetTextAntialiasMode(m_Options.antiAlias ? D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE
													   : D2D1_TEXT_ANTIALIAS_MODE_ALIASED);
	m_Target->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
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
	const float top = m_TailTop ? m_TailHeight : 0;
	// A one-pixel stroke fully covers its straight edges; curves and diagonals
	// retain antialiasing, as do the partial pixels of fractional stroke widths.
	const auto body = GetBodyRect(top);
	if (m_Options.balloon)
	{
		if (!DrawBalloon(body, background.Get(), stroke.Get()))
		{
			m_Target->EndDraw();
			m_Target->SetTarget(nullptr);
			return false;
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
	if (samples > 1)
	{
		// Finish all resampling before drawing any text, icons or close button.
		// Their original target, DPI and text rasterization remain unchanged.
		ComPtr<ID2D1Bitmap1> source = m_OutlineBitmap;
		for (UINT level = samples / 2;; level /= 2)
		{
			const auto result = m_Target->EndDraw();
			m_Target->SetTarget(nullptr);
			if (FAILED(result))
			{
				return false;
			}
			m_Target->SetTarget(level == 1 ? m_Bitmap.Get() : m_OutlineIntermediate.Get());
			m_Target->SetDpi(96 * m_Scale * level, 96 * m_Scale * level);
			m_Target->BeginDraw();
			m_Target->Clear(D2D1::ColorF(0, 0));
			const auto rect =
				D2D1::RectF(0, 0, dimensions.width / m_Scale, dimensions.height / m_Scale);
			m_Target->DrawBitmap(source.Get(), rect, 1, D2D1_INTERPOLATION_MODE_LINEAR, rect);
			if (level == 1)
			{
				break;
			}
			source = m_OutlineIntermediate;
		}
	}
	const float x = m_Options.padding[0] + m_Options.borderWidth;
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
			m_Target->DrawBitmap(iconBitmap.Get(), GetIconRect(top));
		}
	}
	const float defaultTitleX = x + (m_Icon ? m_IconWidth + 6 : 0);
	const float titleX = m_Options.titleX.value_or(defaultTitleX);
	const float titleY = GetTitleY(top);
	if (!m_Options.title.empty())
	{
		const float titleWidth =
			m_BodyWidth - m_Options.padding[2] - (m_Options.close ? 24 : 0) - defaultTitleX;
		Gfx::Canvas::Draw(m_Target.Get(), m_Options.title, *m_TitleFormat,
			D2D1::RectF(titleX, titleY, titleX + titleWidth, titleY + m_HeaderHeight),
			m_Options.titleColor, true, true);
	}
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

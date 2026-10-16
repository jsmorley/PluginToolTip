#include "vendor/StdAfx.h"
#include "vendor/Gfx/Canvas.h"
#include "vendor/Gfx/Util/DWriteFontCollectionLoader.h"

Microsoft::WRL::ComPtr<IDWriteFactory1> Gfx::Canvas::c_DWFactory;
bool Gfx::Canvas::Initialize()
{
	if (c_DWFactory)
	{
		return true;
	}
	if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_ISOLATED, __uuidof(IDWriteFactory1),
			reinterpret_cast<IUnknown**>(c_DWFactory.GetAddressOf()))))
	{
		return false;
	}
	return SUCCEEDED(
		c_DWFactory->RegisterFontCollectionLoader(Util::DWriteFontCollectionLoader::GetInstance()));
}
void Gfx::Canvas::Finalize()
{
	FontCollection::c_SystemCollection.Reset();
	if (c_DWFactory)
	{
		c_DWFactory->UnregisterFontCollectionLoader(
			Util::DWriteFontCollectionLoader::GetInstance());
	}
	c_DWFactory.Reset();
}
std::unique_ptr<Gfx::FontCollection> Gfx::Canvas::Fonts()
{
	return std::unique_ptr<FontCollection>(new FontCollection);
}
D2D1_SIZE_F Gfx::Canvas::Measure(
	const std::wstring& text, TextFormat& format, float width, bool accurate)
{
	if (!format.IsInitialized())
	{
		return {};
	}
	std::wstring buffer;
	const auto metrics = format.GetMetrics(format.ApplyInlineCase(text, buffer), !accurate, width);
	return D2D1::SizeF(metrics.width, metrics.height);
}
D2D1_RECT_F Gfx::Canvas::MeasureInk(
	const std::wstring& text, TextFormat& format, float width, float height)
{
	const auto size = Measure(text, format, width, true);
	const auto fallback = D2D1::RectF(0, 0, size.width, size.height);
	if (!format.IsInitialized() || text.empty())
	{
		return fallback;
	}
	std::wstring buffer;
	const auto& str = format.ApplyInlineCase(text, buffer);
	Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
	if (FAILED(c_DWFactory->CreateTextLayout(str.data(), static_cast<UINT32>(str.size()),
			format.m_TextFormat.Get(), width, height, layout.GetAddressOf())))
	{
		return fallback;
	}
	if (format.m_FontWeight > 0 && format.m_FontWeight < 1000)
	{
		layout->SetFontWeight(static_cast<DWRITE_FONT_WEIGHT>(format.m_FontWeight),
			DWRITE_TEXT_RANGE{0, static_cast<UINT32>(str.size())});
	}
	format.ApplyInlineFormatting(layout.Get());
	DWRITE_OVERHANG_METRICS ink{};
	if (FAILED(layout->GetOverhangMetrics(&ink)))
	{
		return fallback;
	}
	// Match Draw's line-gap adjustment. Overhangs are signed distances from
	// the layout box to the actual glyph outlines, including inline formatting.
	float adjustment = format.m_LineGap;
	if (format.GetVerticalAlignment() == VerticalAlignment::Center)
	{
		adjustment += format.m_ExtraHeight / 2;
	}
	else if (format.GetVerticalAlignment() == VerticalAlignment::Bottom)
	{
		adjustment += format.m_ExtraHeight;
	}
	return D2D1::RectF(-ink.left, -ink.top - adjustment, layout->GetMaxWidth() + ink.right,
		layout->GetMaxHeight() + ink.bottom - adjustment);
}

D2D1_RECT_F Gfx::Canvas::MeasureRenderedInk(ID2D1DeviceContext* target, const std::wstring& text,
	TextFormat& format, D2D1_RECT_F rect, float scale, bool antiAlias)
{
	const auto fallback = MeasureInk(text, format, rect.right - rect.left, rect.bottom - rect.top);
	if (!target || text.empty())
	{
		return fallback;
	}
	// Measure at the actual pixel phase: DirectWrite's small-font hinting can
	// move painted rows away from the unhinted outline bounds.
	const float left = std::floor((rect.left + fallback.left) * scale) - 2;
	const float top = std::floor((rect.top + fallback.top) * scale) - 2;
	const float width = std::ceil((rect.left + fallback.right) * scale) + 2 - left;
	const float height = std::ceil((rect.top + fallback.bottom) * scale) + 2 - top;
	if (width <= 0 || height <= 0 || width > 4096 || height > 2048)
	{
		return fallback;
	}
	using Microsoft::WRL::ComPtr;
	ComPtr<ID2D1Device> device;
	ComPtr<ID2D1DeviceContext> context;
	ComPtr<ID2D1Bitmap1> bitmap, readback;
	target->GetDevice(device.GetAddressOf());
	auto properties = D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET,
		D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96 * scale,
		96 * scale);
	const auto dimensions = D2D1::SizeU(static_cast<UINT>(width), static_cast<UINT>(height));
	if (!device ||
		FAILED(device->CreateDeviceContext(
			D2D1_DEVICE_CONTEXT_OPTIONS_NONE, context.GetAddressOf())) ||
		FAILED(context->CreateBitmap(dimensions, nullptr, 0, &properties, bitmap.GetAddressOf())))
	{
		return fallback;
	}
	context->SetTarget(bitmap.Get());
	context->SetDpi(96 * scale, 96 * scale);
	context->SetTextAntialiasMode(
		antiAlias ? D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE : D2D1_TEXT_ANTIALIAS_MODE_ALIASED);
	context->SetTransform(D2D1::Matrix3x2F::Translation(-left / scale, -top / scale));
	context->BeginDraw();
	context->Clear(D2D1::ColorF(0, 0));
	Draw(context.Get(), text, format, rect, D2D1::ColorF(0, 1), false, true);
	const HRESULT result = context->EndDraw();
	context->SetTarget(nullptr);
	properties.bitmapOptions = D2D1_BITMAP_OPTIONS_CPU_READ | D2D1_BITMAP_OPTIONS_CANNOT_DRAW;
	if (FAILED(result) ||
		FAILED(
			context->CreateBitmap(dimensions, nullptr, 0, &properties, readback.GetAddressOf())) ||
		FAILED(readback->CopyFromBitmap(nullptr, bitmap.Get(), nullptr)))
	{
		return fallback;
	}
	D2D1_MAPPED_RECT mapped{};
	if (FAILED(readback->Map(D2D1_MAP_OPTIONS_READ, &mapped)))
	{
		return fallback;
	}
	UINT first = dimensions.height, last = 0;
	for (UINT y = 0; y < dimensions.height; ++y)
	{
		for (UINT x = 0; x < dimensions.width; ++x)
		{
			if (mapped.bits[y * mapped.pitch + x * 4 + 3] >= 32)
			{
				first = std::min(first, y);
				last = y + 1;
				break;
			}
		}
	}
	readback->Unmap();
	return first == dimensions.height ? fallback
									  : D2D1::RectF(fallback.left, (top + first) / scale - rect.top,
											fallback.right, (top + last) / scale - rect.top);
}

void Gfx::Canvas::Draw(ID2D1DeviceContext* target, const std::wstring& text, TextFormat& format,
	D2D1_RECT_F rect, D2D1_COLOR_F color, bool inlines, bool accurate)
{
	if (!target || !format.IsInitialized())
	{
		return;
	}
	std::wstring buffer;
	const auto& str = format.ApplyInlineCase(text, buffer);
	if (!format.CreateLayout(
			target, str, rect.right - rect.left, rect.bottom - rect.top, !accurate))
	{
		return;
	}
	Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush;
	if (FAILED(target->CreateSolidColorBrush(color, brush.GetAddressOf())))
	{
		return;
	}
	auto pos = D2D1::Point2F(rect.left, rect.top - format.m_LineGap);
	if (!accurate)
	{
		if (format.GetHorizontalAlignment() == HorizontalAlignment::Left)
		{
			pos.x += format.m_TextFormat->GetFontSize() / 6;
		}
		if (format.GetHorizontalAlignment() == HorizontalAlignment::Right)
		{
			pos.x -= format.m_TextFormat->GetFontSize() / 6;
		}
	}
	if (format.GetVerticalAlignment() == VerticalAlignment::Center)
	{
		pos.y -= format.m_ExtraHeight / 2;
	}
	if (format.GetVerticalAlignment() == VerticalAlignment::Bottom)
	{
		pos.y -= format.m_ExtraHeight;
	}
	const auto len = static_cast<UINT32>(str.size());
	format.ResetInlineColoring(brush.Get(), len);
	if (inlines)
	{
		format.ApplyInlineColoring(target, &pos);
		format.ApplyInlineShadow(target, brush.Get(), len,
			D2D1::RectF(pos.x, pos.y, rect.right - rect.left, rect.bottom - rect.top));
	}
	if (format.m_Trimming)
	{
		target->PushAxisAlignedClip(rect, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
	}
	target->DrawTextLayout(pos, format.m_TextLayout.Get(), brush.Get());
	if (format.m_Trimming)
	{
		target->PopAxisAlignedClip();
	}
	if (inlines)
	{
		format.ResetGradientPosition(&pos);
	}
}

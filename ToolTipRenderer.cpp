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

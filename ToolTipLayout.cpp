// Copyright (c) 2026 Jeffrey Morley. Source code licensed under GNU GPL v2 (see LICENSE file).

#include "ToolTipMeasure.h"

float ToolTipMeasure::ReadScale() const
{
	// API 2 already includes Windows DPI and Rainmeter zoom. Do not apply
	// Windows DPI a second time. Older hosts need a window-DPI fallback.
	const auto* effective = static_cast<const float*>(RmGet(m_Rm, RMG_SKINSCALE));
	float value = effective ? *effective : 0;
	if (!std::isfinite(value) || value <= 0)
	{
		using GetDpi = UINT(WINAPI*)(HWND);
		static const auto getDpi = reinterpret_cast<GetDpi>(
			GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow"));
		const UINT dpi = getDpi ? getDpi(m_SkinWindow) : 0;
		value = dpi ? dpi / 96.0f : 1.0f;
	}
	return std::clamp(value, 0.25f, 8.0f);
}

POINT ToolTipMeasure::GetSkinAnchor(float effectiveScale) const
{
	POINT point{};
	ClientToScreen(m_SkinWindow, &point);
	point.x += static_cast<LONG>(std::round(m_SkinAnchor.x * effectiveScale));
	point.y += static_cast<LONG>(std::round(m_SkinAnchor.y * effectiveScale));
	return point;
}

namespace {
float ResolveCoordinate(const std::optional<ToolTip::TextCoordinate>& value, float automatic,
	float start, float length, bool reference)
{
	if (!value)
	{
		return automatic;
	}
	if (!value->relative)
	{
		return value->offset;
	}
	if (!reference)
	{
		return value->offset;
	}
	return start + value->offset + (value->relative == L'R' ? length : 0);
}
}

float ToolTipMeasure::GetTitleX() const
{
	const float inset = m_Options.padding[0] + m_Options.borderWidth;
	const float iconLeft = m_Options.iconX.value_or(inset);
	return ResolveCoordinate(m_Options.titleX,
		m_Icon ? iconLeft + m_IconWidth + m_Options.iconGap : inset, iconLeft, m_IconWidth,
		m_Icon != nullptr);
}

float ToolTipMeasure::GetTitleY(float top) const
{
	const auto icon = GetIconRect(0);
	return top +
		ResolveCoordinate(m_Options.titleY,
			m_Options.padding[1] + m_Options.borderWidth + m_TitleAlignmentOffset, icon.top,
			m_IconHeight, m_Icon != nullptr);
}

D2D1_RECT_F ToolTipMeasure::GetTextRect(float top) const
{
	const float x = m_Options.padding[0] + m_Options.borderWidth;
	const float y = m_Options.padding[1] + m_Options.borderWidth;
	const auto icon = GetIconRect(0);
	const bool title = !m_Options.title.empty();
	const float automaticX = !title && m_Icon ? icon.right + m_Options.iconGap : x;
	float automaticY = y + m_HeaderHeight + m_TextAlignmentOffset;
	if (title && (m_Options.titleY || m_Options.iconY || m_Options.closeButtonY))
	{
		automaticY = GetTitleY(0) + m_TitleSize.height;
		if (m_Icon)
		{
			automaticY = std::max(automaticY, icon.bottom);
		}
		if (m_Options.close)
		{
			automaticY =
				std::max(automaticY, m_Options.closeButtonY.value_or(m_Options.padding[1]) + 22);
		}
		automaticY += m_Options.titleTextGap;
	}
	const float left = ResolveCoordinate(m_Options.textX, automaticX,
		title ? GetTitleX() : icon.left, title ? m_TitleSize.width : m_IconWidth, title || m_Icon);
	const float textTop = top +
		ResolveCoordinate(m_Options.textY, automaticY, title ? GetTitleY(0) : icon.top,
			title ? m_TitleSize.height : m_IconHeight, title || m_Icon);
	return D2D1::RectF(
		left, textTop, left + m_TextLayoutSize.width, textTop + m_TextLayoutSize.height);
}

D2D1_RECT_F ToolTipMeasure::GetIconRect(float top) const
{
	const float x = m_Options.iconX.value_or(m_Options.padding[0] + m_Options.borderWidth);
	const float y = top +
		m_Options.iconY.value_or(m_Options.padding[1] + m_Options.borderWidth +
			m_IconAlignmentPadding + m_IconRowOffset);
	return D2D1::RectF(x, y, x + m_IconWidth, y + m_IconHeight);
}

D2D1_RECT_F ToolTipMeasure::GetBodyRect(float top) const
{
	// Align outer stroke edges to physical pixels while preserving stroke width.
	const float inset = m_Options.borderWidth / 2;
	return D2D1::RectF(inset, std::round(top * m_Scale) / m_Scale + inset,
		std::ceil(m_BodyWidth * m_Scale) / m_Scale - inset,
		std::ceil((top + m_BodyHeight) * m_Scale) / m_Scale - inset);
}

void ToolTipMeasure::SelectTail(bool top)
{
	m_TailTop = top;
	m_BodyHeight = top ? m_TopTailBodyHeight : m_BottomTailBodyHeight;
	m_PixelSize.cy = static_cast<LONG>(std::ceil((m_BodyHeight + m_TailHeight) * m_Scale));
}

void ToolTipMeasure::UpdateLayout()
{
	m_TextFormat = std::make_unique<Gfx::TextFormat>();
	m_TitleFormat = std::make_unique<Gfx::TextFormat>();
	m_Options.Configure(*m_TextFormat, m_Fonts.get(), m_Text);
	m_Options.Configure(*m_TitleFormat, m_Fonts.get(), m_Options.title, true);
	m_TitleAlignmentOffset = 0;
	m_TextAlignmentOffset = 0;
	m_IconAlignmentPadding = 0;
	m_IconRowOffset = 0;
	m_HeaderHeight = 0;
	m_TextLayoutSize = D2D1::SizeF();
	m_TitleSize = D2D1::SizeF();
	const float rightInset = m_Options.padding[2] + m_Options.borderWidth;
	const float closeSpace = m_Options.close && !m_Options.closeButtonX ? 24.0f : 0;
	const float titleWidth = m_Options.width > 0
		? std::max(1.0f, m_Options.width - GetTitleX() - rightInset - closeSpace)
		: 8192.0f;
	if (!m_Options.title.empty())
	{
		m_TitleSize = Gfx::Canvas::Measure(m_Options.title, *m_TitleFormat, titleWidth, true);
	}
	float maxW = m_Options.width > 0
		? std::max(1.0f, m_Options.width - GetTextRect(0).left - rightInset - closeSpace)
		: 8192.0f;
	if (m_Options.clip == 2 && m_Options.clipW > 0)
	{
		maxW = std::min(maxW, m_Options.clipW);
	}
	const auto size =
		m_Text.empty() ? D2D1::SizeF() : Gfx::Canvas::Measure(m_Text, *m_TextFormat, maxW, true);
	const float w =
		m_Options.width > 0 ? maxW : std::min(size.width + (m_Text.empty() ? 0 : 1), maxW);
	float h = size.height + (m_Text.empty() ? 0 : 1);
	if (m_Options.clip == 2 && m_Options.clipH > 0)
	{
		h = std::min(h, m_Options.clipH);
	}
	if (m_Icon && !m_Options.title.empty() && !m_Options.iconY)
	{
		const auto ink = Gfx::Canvas::MeasureInk(
			m_Options.title, *m_TitleFormat, m_TitleSize.width, m_TitleSize.height);
		m_IconRowOffset =
			std::max(0.0f,
				(m_Options.titleY ? m_TitleSize.height : ink.bottom - ink.top) - m_IconHeight) /
			2;
		const float center = m_IconRowOffset + m_IconHeight * m_IconVerticalCenter;
		if (!m_Options.titleY)
		{
			m_IconAlignmentPadding = std::max(0.0f, (ink.bottom - ink.top) / 2 - center);
		}
		m_TitleAlignmentOffset = center + m_IconAlignmentPadding - (ink.top + ink.bottom) / 2;
	}
	const float rowHeight = std::max(
		{m_TitleSize.height, m_Icon ? m_IconHeight : 0.0f, m_Options.close ? 22.0f : 0.0f});
	m_HeaderHeight = m_Options.title.empty()
		? 0
		: std::max(rowHeight + m_IconAlignmentPadding,
			  m_Options.titleY ? m_TitleSize.height : m_TitleAlignmentOffset + m_TitleSize.height) +
			(h > 0 ? m_Options.titleTextGap : 0);
	float headerW = m_TitleSize.width + (m_Icon ? m_IconWidth + m_Options.iconGap : 0) +
		(m_Options.close ? 24.0f : 0);
	m_TextLayoutSize =
		D2D1::SizeF(m_Options.width > 0 || m_Options.title.empty() ? w : std::max(w, headerW), h);
	m_TextAlignmentOffset = 0;
	if (m_Icon && m_Options.title.empty() && !m_Text.empty() && !m_Options.textY &&
		!m_Options.iconY)
	{
		const auto ink = Gfx::Canvas::MeasureInk(m_Text, *m_TextFormat, m_TextLayoutSize.width, h);
		m_IconRowOffset = std::max(0.0f, ink.bottom - ink.top - m_IconHeight) / 2;
		const float center = m_IconRowOffset + m_IconHeight * m_IconVerticalCenter;
		m_IconAlignmentPadding = std::max(0.0f, (ink.bottom - ink.top) / 2 - center);
		m_TextAlignmentOffset = center + m_IconAlignmentPadding - (ink.top + ink.bottom) / 2;
	}
	std::optional<float> titlePaintBottom, textPaintBottom;
	const bool titleRow = !m_Options.title.empty() && !m_Options.titleY;
	const bool textRow = m_Options.title.empty() && !m_Text.empty() && !m_Options.textY;
	if (m_Icon && !m_Options.iconY && (titleRow || textRow) && InitializeGraphics())
	{
		auto rect = titleRow
			? D2D1::RectF(GetTitleX(), GetTitleY(0), GetTitleX() + m_TitleSize.width + 1,
				  GetTitleY(0) + m_TitleSize.height)
			: GetTextRect(0);
		if (!m_RowInk)
		{
			m_RowInk =
				Gfx::Canvas::MeasureRenderedInk(m_Target.Get(), titleRow ? m_Options.title : m_Text,
					titleRow ? *m_TitleFormat : *m_TextFormat, rect, m_Scale, m_Options.antiAlias);
		}
		const auto ink = *m_RowInk;
		const float center = GetIconRect(0).top + m_IconHeight * m_IconVerticalCenter;
		// Keep the text on its measured rasterization phase for sharp glyphs.
		// Apply the remaining fractional correction to the icon instead.
		const float correction =
			std::round((center - rect.top - (ink.top + ink.bottom) / 2) * m_Scale) / m_Scale;
		m_IconRowOffset += rect.top + (ink.top + ink.bottom) / 2 + correction - center;
		if (titleRow)
		{
			m_TitleAlignmentOffset += correction;
			titlePaintBottom = GetTitleY(0) + ink.bottom;
			m_HeaderHeight =
				std::max({GetIconRect(0).bottom, *titlePaintBottom,
					m_Options.padding[1] + m_Options.borderWidth + (m_Options.close ? 22 : 0)}) -
				m_Options.padding[1] - m_Options.borderWidth + (h > 0 ? m_Options.titleTextGap : 0);
		}
		else
		{
			m_TextAlignmentOffset += correction;
			textPaintBottom = GetTextRect(0).top + ink.bottom;
		}
	}
	m_TailHeight = m_Options.balloon ? 12.0f : 0;
	// Horizontal coordinates are independent of the tail's orientation.
	const auto textRect = GetTextRect(0);
	{
		float right =
			!m_Text.empty() ? textRect.right : m_Options.padding[0] + m_Options.borderWidth;
		if (!m_Options.title.empty())
		{
			right = std::max(right, GetTitleX() + m_TitleSize.width);
		}
		if (m_Icon)
		{
			right = std::max(right, GetIconRect(0).right);
		}
		if (m_Options.close && m_Options.closeButtonX)
		{
			right = std::max(right, *m_Options.closeButtonX + 22);
		}
		m_BodyWidth = std::clamp(right + m_Options.padding[2] + m_Options.borderWidth +
				(m_Options.close && !m_Options.closeButtonX ? 24 : 0),
			1.0f, 8192.0f / m_Scale);
	}
	if (m_Options.width > 0)
	{
		m_BodyWidth = std::clamp(m_Options.width, 1.0f, 8192.0f / m_Scale);
	}
	if (m_Options.height > 0 && !m_Text.empty())
	{
		m_TextLayoutSize.height = std::max(0.0f,
			m_Options.height - GetTextRect(0).top - m_Options.padding[3] - m_Options.borderWidth);
		if (m_Options.clip == 2 && m_Options.clipH > 0)
		{
			m_TextLayoutSize.height = std::min(m_TextLayoutSize.height, m_Options.clipH);
		}
	}
	auto bodyHeight = [&](float top) {
		if (m_Options.height > 0)
		{
			return std::clamp(m_Options.height, 1.0f, 8192.0f / m_Scale);
		}
		float bottom = top + m_Options.padding[1] + m_Options.borderWidth;
		if (!m_Text.empty())
		{
			bottom = std::max(bottom,
				textPaintBottom.has_value() ? top + *textPaintBottom : GetTextRect(top).bottom);
		}
		if (!m_Options.title.empty())
		{
			bottom = std::max(bottom,
				titlePaintBottom ? top + *titlePaintBottom : GetTitleY(top) + m_TitleSize.height);
		}
		if (m_Icon)
		{
			bottom = std::max(bottom, GetIconRect(top).bottom);
		}
		if (m_Options.close)
		{
			bottom = std::max(bottom,
				(m_Options.closeButtonY ? *m_Options.closeButtonY + top
										: top + m_Options.padding[1]) +
					22);
		}
		// All element coordinates include the body top while drawing.
		// Exclude the tail when converting their bounds to the body height.

		return std::clamp(
			bottom - top + m_Options.padding[3] + m_Options.borderWidth, 1.0f, 8192.0f / m_Scale);
	};
	m_TopTailBodyHeight = bodyHeight(m_TailHeight);
	m_BottomTailBodyHeight = bodyHeight(0);
	m_PixelSize.cx = static_cast<LONG>(std::ceil(m_BodyWidth * m_Scale));
	SelectTail(m_TailTop);
}

POINT ToolTipMeasure::GetWindowPosition(SIZE size)
{
	if (m_FixedAnchor && IsTailAnchored())
	{
		return GetAnchoredBalloonPosition(size, m_Anchor, m_Anchor);
	}
	const LONG belowHeight =
		static_cast<LONG>(std::ceil((m_TopTailBodyHeight + m_TailHeight) * m_Scale));
	const LONG aboveHeight =
		static_cast<LONG>(std::ceil((m_BottomTailBodyHeight + m_TailHeight) * m_Scale));

	const bool verticalBalloon = m_Options.balloon &&
		(m_Options.position == L"auto" || m_Options.position == L"above" ||
			m_Options.position == L"below");
	const auto xOffset = static_cast<LONG>(m_Options.offsetX * m_Scale);
	const auto yOffset = verticalBalloon && m_Options.tailGap >= 0
		? static_cast<LONG>(std::round(m_Options.tailGap))
		: static_cast<LONG>(m_Options.offsetY * m_Scale);
	POINT below = m_Anchor, above = m_Anchor;
	if (m_Options.balloon && !m_FixedAnchor)
	{
		CURSORINFO cursor{sizeof(cursor)};
		HCURSOR current =
			GetCursorInfo(&cursor) && (cursor.flags & CURSOR_SHOWING) ? cursor.hCursor : nullptr;
		if (current != m_MeasuredCursor)
		{
			m_MeasuredCursor = current;
			m_CursorEdges = ToolTip::GetCursorEdges(current);
		}
		const auto& bounds = m_CursorEdges.bounds;
		const LONG cursorX = bounds.right > bounds.left
			? (m_Options.tailPosition == L"left"		   ? bounds.left - 1
					  : m_Options.tailPosition == L"right" ? bounds.right
														   : (bounds.left + bounds.right - 1) / 2)
			: 0;
		below.x += cursorX;
		below.y += bounds.bottom;
		above.x += cursorX;
		above.y += bounds.top;
	}
	if (IsTailAnchored())
	{
		// Keep the requested cursor gap on either side. The placement helper flips
		// the body before clamping and attaches the tail to the chosen cursor edge.
		below.y += yOffset;
		above.y -= std::abs(yOffset);
		return GetAnchoredBalloonPosition(size, below, above);
	}
	m_Attachment = below;
	POINT p{below.x + xOffset, below.y + yOffset};
	if (m_Options.position == L"above")
	{
		m_Attachment = above;
		p = {above.x + xOffset, above.y - aboveHeight - std::abs(yOffset)};
	}
	if (m_Options.position == L"left")
	{
		p.x = m_Anchor.x - size.cx - std::abs(xOffset);
		p.y = m_Anchor.y;
	}
	if (m_Options.position == L"right")
	{
		p.x = m_Anchor.x + std::abs(xOffset);
		p.y = m_Anchor.y;
	}
	if (m_Options.position == L"absolute")
	{
		p = {m_FixedAnchor ? m_Anchor.x : xOffset, m_FixedAnchor ? m_Anchor.y : yOffset};
	}
	MONITORINFO monitor{sizeof(monitor)};
	GetMonitorInfoW(MonitorFromPoint(m_Anchor, MONITOR_DEFAULTTONEAREST), &monitor);
	auto work = monitor.rcWork;
	if (m_Options.position == L"auto" && p.y + belowHeight > work.bottom)
	{
		m_Attachment = above;
		p = {above.x + xOffset, above.y - aboveHeight - std::abs(yOffset)};
	}
	SelectTail(p.y >= m_Anchor.y);
	size = m_PixelSize;
	if (verticalBalloon)
	{
		const float inset = m_Options.borderWidth / 2;
		const auto body = GetBodyRect(m_TailTop ? m_TailHeight : 0);
		const float radius = std::min({m_Options.radius, std::max(0.0f, body.right - body.left) / 2,
			std::max(0.0f, body.bottom - body.top) / 2});
		// Align the balloon's tail with the selected cursor-relative point.
		// Painting still tracks it after screen-edge clamping.
		const auto tailInset = static_cast<LONG>(std::ceil((inset + radius + 9) * m_Scale));
		if (m_Options.horizontalPosition == L"left")
		{
			p.x = m_Attachment.x - size.cx + tailInset - std::abs(xOffset);
		}
		else if (m_Options.horizontalPosition == L"right")
		{
			p.x = m_Attachment.x - tailInset + std::abs(xOffset);
		}
		else
		{
			p.x -= tailInset;
		}
	}
	else
	{
		m_Attachment = m_Anchor;
	}
	if (!verticalBalloon && !m_Options.horizontalPosition.empty())
	{
		p.x = m_Options.horizontalPosition == L"left" ? m_Anchor.x - size.cx - std::abs(xOffset)
													  : m_Anchor.x + std::abs(xOffset);
	}
	if (m_Options.keepOnScreen)
	{
		p.x = std::clamp(p.x, work.left, std::max(work.left, work.right - size.cx));
		p.y = std::clamp(p.y, work.top, std::max(work.top, work.bottom - size.cy));
	}
	return p;
}

POINT ToolTipMeasure::GetAnchoredBalloonPosition(SIZE size, POINT belowAnchor, POINT aboveAnchor)
{
	MONITORINFO monitor{sizeof(monitor)};
	GetMonitorInfoW(MonitorFromPoint(m_Anchor, MONITOR_DEFAULTTONEAREST), &monitor);
	const RECT& work = monitor.rcWork;
	const float inset = m_Options.borderWidth / 2;
	const float tipInset = inset + 0.5f / m_Scale;
	const LONG belowHeight =
		static_cast<LONG>(std::ceil((m_TopTailBodyHeight + m_TailHeight) * m_Scale));
	const LONG aboveHeight =
		static_cast<LONG>(std::ceil((m_BottomTailBodyHeight + m_TailHeight) * m_Scale));
	const auto tipTop = static_cast<LONG>(std::round(tipInset * m_Scale));
	const auto tipBottom = static_cast<LONG>(std::round(
		std::ceil((m_BottomTailBodyHeight + m_TailHeight) * m_Scale) - tipInset * m_Scale));
	const LONG below = belowAnchor.y - tipTop;
	const LONG above = aboveAnchor.y - tipBottom;
	bool placeAbove = m_Options.position == L"above";

	// Above/Below select the preferred side. Flip before clamping the window so
	// fitting on the monitor does not unnecessarily move the tail's fixed point.
	if (m_Options.keepOnScreen)
	{
		const bool fitsAbove = above >= work.top && above + aboveHeight <= work.bottom;
		const bool fitsBelow = below >= work.top && below + belowHeight <= work.bottom;
		if (!(placeAbove ? fitsAbove : fitsBelow))
		{
			if (placeAbove ? fitsBelow : fitsAbove)
			{
				placeAbove = !placeAbove;
			}
			else
			{
				// An oversized tooltip or an off-screen anchor cannot fit exactly.
				// Prefer the side with more space, then keep as much visible as possible.
				placeAbove = aboveAnchor.y - work.top > work.bottom - belowAnchor.y;
			}
		}
	}
	SelectTail(!placeAbove);
	size = m_PixelSize;
	const auto body = GetBodyRect(m_TailTop ? m_TailHeight : 0);
	const float radius = std::min({m_Options.radius, std::max(0.0f, body.right - body.left) / 2,
		std::max(0.0f, body.bottom - body.top) / 2});

	const auto tailInset = static_cast<LONG>(std::ceil((inset + radius + 9) * m_Scale));
	const auto xOffset = static_cast<LONG>(m_Options.offsetX * m_Scale);
	const POINT attachment = placeAbove ? aboveAnchor : belowAnchor;
	POINT position{attachment.x - tailInset + xOffset, placeAbove ? above : below};
	if (m_Options.horizontalPosition == L"left" || m_Options.position == L"left")
	{
		position.x = attachment.x - size.cx + tailInset - std::abs(xOffset);
	}
	else if (m_Options.horizontalPosition == L"right" || m_Options.position == L"right")
	{
		position.x = attachment.x - tailInset + std::abs(xOffset);
	}

	// OffsetX shifts the body, not the anchor. Keep the point within the bitmap;
	// the tail's base may slide away from it to clear a rounded corner.
	const auto tipRight = std::max(
		tipTop, static_cast<LONG>(std::round(std::ceil(m_BodyWidth * m_Scale) - inset * m_Scale)));
	position.x = std::clamp(position.x, attachment.x - tipRight, attachment.x - tipTop);
	if (m_Options.keepOnScreen)
	{
		position.x = std::clamp(position.x, work.left, std::max(work.left, work.right - size.cx));
		if (size.cy <= work.bottom - work.top)
		{
			position.y = std::clamp(position.y, work.top, work.bottom - size.cy);
		}
	}
	m_Attachment = attachment;
	return position;
}

D2D1_POINT_2F ToolTipMeasure::GetCloseButtonOrigin() const
{
	return D2D1::Point2F(m_Options.closeButtonX.value_or(
							 m_BodyWidth - m_Options.padding[2] - m_Options.borderWidth - 22),
		(m_Options.closeButtonY ? *m_Options.closeButtonY + (m_TailTop ? m_TailHeight : 0)
								: (m_TailTop ? m_TailHeight : 0) + m_Options.padding[1]));
}

bool ToolTipMeasure::HitTestCloseButton(LPARAM point) const
{
	if (!m_Options.close)
	{
		return false;
	}
	POINT p{static_cast<short>(LOWORD(point)), static_cast<short>(HIWORD(point))};
	ScreenToClient(m_Window, &p);
	float x = p.x / m_Scale, y = p.y / m_Scale;
	const auto button = GetCloseButtonOrigin();
	return x >= button.x && x <= button.x + 22 && y >= button.y && y <= button.y + 22;
}

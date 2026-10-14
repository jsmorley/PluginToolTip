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

D2D1_RECT_F ToolTipMeasure::GetTextRect(float top) const
{
	const float x = m_Options.padding[0] + m_Options.borderWidth;
	const float y = top + m_Options.padding[1] + m_Options.borderWidth;
	const float titleX = m_Options.titleX.value_or(x + (m_Icon ? m_IconWidth + 6 : 0));
	const float titleY =
		top + m_Options.titleY.value_or(m_Options.padding[1] + m_Options.borderWidth);
	auto coordinate = [&](const std::optional<ToolTip::TextCoordinate>& value, float automatic,
						  float titleStart, float titleLength) {
		if (!value)
		{
			return automatic;
		}
		if (!value->relative)
		{
			return value->offset;
		}
		if (m_Options.title.empty())
		{
			return automatic + value->offset;
		}
		return titleStart + value->offset + (value->relative == L'R' ? titleLength : 0);
	};
	const float left = coordinate(m_Options.textX, x, titleX, m_TitleSize.width);
	const float textTop =
		coordinate(m_Options.textY, y + m_HeaderHeight, titleY, m_TitleSize.height);
	return D2D1::RectF(
		left, textTop, left + m_TextLayoutSize.width, textTop + m_TextLayoutSize.height);
}

void ToolTipMeasure::UpdateLayout()
{
	m_TextFormat = std::make_unique<Gfx::TextFormat>();
	m_TitleFormat = std::make_unique<Gfx::TextFormat>();
	m_Options.Configure(*m_TextFormat, m_Fonts.get(), m_Text);
	m_Options.Configure(*m_TitleFormat, m_Fonts.get(), m_Options.title, true);
	auto maxW = m_Options.width > 0					 ? m_Options.width
		: m_Options.clip == 2 && m_Options.clipW > 0 ? m_Options.clipW
													 : 8192;
	auto size = Gfx::Canvas::Measure(m_Text, *m_TextFormat, maxW, true);
	float w = m_Options.width > 0 ? m_Options.width : std::min(size.width + 1, maxW);
	float h = m_Options.height > 0 ? m_Options.height : size.height + 1;
	if (m_Options.clip == 2 && m_Options.height == 0 && m_Options.clipH > 0)
	{
		h = std::min(h, m_Options.clipH);
	}
	m_TitleSize = Gfx::Canvas::Measure(m_Options.title, *m_TitleFormat, 8192, true);
	m_HeaderHeight = m_Options.title.empty() && !m_Icon && !m_Options.close
		? 0
		: std::max(m_TitleSize.height + 5, m_Icon ? m_IconHeight + 5 : 0.0f);
	float headerW =
		m_TitleSize.width + (m_Icon ? m_IconWidth + 6 : 0) + (m_Options.close ? 24.0f : 0);
	m_TextLayoutSize = D2D1::SizeF(std::max(w, headerW), h);
	// Reserve the positioned layout plus trailing padding/stroke. Measuring
	// without a top tail also leaves enough room for either tail orientation.
	const auto textRect = GetTextRect(0);
	const float headerWidth =
		headerW + m_Options.padding[0] + m_Options.padding[2] + 2 * m_Options.borderWidth;
	const float headerHeight =
		m_HeaderHeight + m_Options.padding[1] + m_Options.padding[3] + 2 * m_Options.borderWidth;
	m_BodyWidth = std::clamp(
		std::max(headerWidth, textRect.right + m_Options.padding[2] + m_Options.borderWidth), 1.0f,
		8192.0f / m_Scale);
	m_BodyHeight = std::clamp(
		std::max(headerHeight, textRect.bottom + m_Options.padding[3] + m_Options.borderWidth),
		1.0f, 8192.0f / m_Scale);
	m_TailHeight = m_Options.balloon ? 12.0f : 0;
	m_PixelSize = {static_cast<LONG>(std::ceil(m_BodyWidth * m_Scale)),
		static_cast<LONG>(std::ceil((m_BodyHeight + m_TailHeight) * m_Scale))};
}

POINT ToolTipMeasure::GetWindowPosition(SIZE size)
{
	if (IsTailAnchored())
	{
		return GetAnchoredBalloonPosition(size);
	}

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
	m_Attachment = below;
	POINT p{below.x + xOffset, below.y + yOffset};
	if (m_Options.position == L"above")
	{
		m_Attachment = above;
		p = {above.x + xOffset, above.y - size.cy - std::abs(yOffset)};
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
	if (m_Options.position == L"auto" && p.y + size.cy > work.bottom)
	{
		m_Attachment = above;
		p = {above.x + xOffset, above.y - size.cy - std::abs(yOffset)};
	}
	if (verticalBalloon)
	{
		const float inset = m_Options.borderWidth / 2 + 0.5f / m_Scale;
		const float radius =
			std::min({m_Options.radius, std::max(0.0f, m_BodyWidth - 2 * inset) / 2,
				std::max(0.0f, m_BodyHeight - 2 * inset) / 2});
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
	m_TailTop = p.y >= m_Anchor.y;
	if (m_Options.keepOnScreen)
	{
		p.x = std::clamp(p.x, work.left, std::max(work.left, work.right - size.cx));
		p.y = std::clamp(p.y, work.top, std::max(work.top, work.bottom - size.cy));
	}
	return p;
}

POINT ToolTipMeasure::GetAnchoredBalloonPosition(SIZE size)
{
	MONITORINFO monitor{sizeof(monitor)};
	GetMonitorInfoW(MonitorFromPoint(m_Anchor, MONITOR_DEFAULTTONEAREST), &monitor);
	const RECT& work = monitor.rcWork;
	const float inset = m_Options.borderWidth / 2 + 0.5f / m_Scale;
	const float radius = std::min({m_Options.radius, std::max(0.0f, m_BodyWidth - 2 * inset) / 2,
		std::max(0.0f, m_BodyHeight - 2 * inset) / 2});
	const auto tipTop = static_cast<LONG>(std::round(inset * m_Scale));
	const auto tipBottom =
		static_cast<LONG>(std::round((m_BodyHeight + m_TailHeight - inset) * m_Scale));
	const LONG below = m_Anchor.y - tipTop;
	const LONG above = m_Anchor.y - tipBottom;
	bool placeAbove = m_Options.position == L"above";

	// Above/Below select the preferred side. Flip before clamping the window so
	// fitting on the monitor does not unnecessarily move the tail's fixed point.
	if (m_Options.keepOnScreen)
	{
		auto fits = [&](LONG top) { return top >= work.top && top + size.cy <= work.bottom; };
		if (!fits(placeAbove ? above : below))
		{
			if (fits(placeAbove ? below : above))
			{
				placeAbove = !placeAbove;
			}
			else
			{
				// An oversized tooltip or an off-screen anchor cannot fit exactly.
				// Prefer the side with more space, then keep as much visible as possible.
				placeAbove = m_Anchor.y - work.top > work.bottom - m_Anchor.y;
			}
		}
	}

	const auto tailInset = static_cast<LONG>(std::ceil((inset + radius + 9) * m_Scale));
	const auto xOffset = static_cast<LONG>(m_Options.offsetX * m_Scale);
	POINT position{m_Anchor.x - tailInset + xOffset, placeAbove ? above : below};
	if (m_Options.horizontalPosition == L"left" || m_Options.position == L"left")
	{
		position.x = m_Anchor.x - size.cx + tailInset - std::abs(xOffset);
	}
	else if (m_Options.horizontalPosition == L"right" || m_Options.position == L"right")
	{
		position.x = m_Anchor.x - tailInset + std::abs(xOffset);
	}

	// OffsetX shifts the body, not the anchor. Keep the point within the bitmap;
	// the tail's base may slide away from it to clear a rounded corner.
	const auto tipRight =
		std::max(tipTop, static_cast<LONG>(std::round((m_BodyWidth - inset) * m_Scale)));
	position.x = std::clamp(position.x, m_Anchor.x - tipRight, m_Anchor.x - tipTop);
	if (m_Options.keepOnScreen)
	{
		position.x = std::clamp(position.x, work.left, std::max(work.left, work.right - size.cx));
		if (size.cy <= work.bottom - work.top)
		{
			position.y = std::clamp(position.y, work.top, work.bottom - size.cy);
		}
	}
	m_Attachment = m_Anchor;
	m_TailTop = !placeAbove;
	return position;
}

D2D1_POINT_2F ToolTipMeasure::GetCloseButtonOrigin() const
{
	return D2D1::Point2F(m_Options.closeButtonX.value_or(
							 m_BodyWidth - m_Options.padding[2] - m_Options.borderWidth - 22),
		m_Options.closeButtonY.value_or((m_TailTop ? m_TailHeight : 0) + m_Options.padding[1]));
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

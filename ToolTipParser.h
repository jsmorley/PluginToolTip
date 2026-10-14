// Copyright (c) 2026 Jeffrey Morley. Source code licensed under GNU GPL v2 (see LICENSE file).

#pragma once

#include "vendor/Gfx/Canvas.h"
#include <optional>

namespace ToolTip {

std::wstring Trim(std::wstring text);
std::wstring Lower(std::wstring text);
std::vector<std::wstring> Split(const std::wstring& text, wchar_t delimiter);
D2D1_COLOR_F Color(const std::wstring& text, D2D1_COLOR_F fallback);
std::optional<Gfx::TextInlineSetting> ParseInline(const std::wstring& text);
std::vector<Gfx::TextInlineRange> MatchRanges(
	const std::wstring& text, const std::wstring& pattern, void* rm);
struct BackgroundGradient
{
	float angle = 0;
	std::vector<D2D1_GRADIENT_STOP> stops;
};
std::optional<BackgroundGradient> ParseBackgroundGradient(const std::wstring& text);
struct TextCoordinate
{
	float offset = 0;
	wchar_t relative = 0; // r: title start edge; R: title end edge; 0: absolute.
};

float ParseNumber(const std::wstring& text, float fallback = 0);

}

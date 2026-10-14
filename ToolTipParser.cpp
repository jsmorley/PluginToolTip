// Copyright (c) 2026 Jeffrey Morley. Source code licensed under GNU GPL v2 (see LICENSE file).

#include "vendor/StdAfx.h"
#include "ToolTipParser.h"
#include "../../API/RainmeterAPI.h"
#include "vendor/pcre/pcre.h"
#include <limits>

namespace ToolTip {
std::wstring Trim(std::wstring s)
{
	auto first = s.find_first_not_of(L" \t\r\n");
	return first == s.npos ? L"" : s.substr(first, s.find_last_not_of(L" \t\r\n") - first + 1);
}
std::wstring Lower(std::wstring s)
{
	std::transform(
		s.begin(), s.end(), s.begin(), [](wchar_t c) { return static_cast<wchar_t>(towlower(c)); });
	return s;
}
std::vector<std::wstring> Split(const std::wstring& s, wchar_t delimiter)
{
	std::vector<std::wstring> result;
	size_t start = 0, end;
	do
	{
		end = s.find(delimiter, start);
		result.push_back(Trim(s.substr(start, end == s.npos ? end : end - start)));
		start = end + 1;
	} while (end != s.npos);
	return result;
}
float ParseNumber(const std::wstring& s, float fallback)
{
	wchar_t* end = nullptr;
	double n = wcstod(s.c_str(), &end);
	return end != s.c_str() && *end == 0 && std::isfinite(n) ? static_cast<float>(n) : fallback;
}
D2D1_COLOR_F Color(const std::wstring& input, D2D1_COLOR_F fallback)
{
	auto s = Trim(input);
	const auto parts = Split(s, L',');
	if (parts.size() >= 3 && parts.size() <= 4)
	{
		float c[4] = {0, 0, 0, 255};
		for (size_t i = 0; i < parts.size(); ++i)
		{
			c[i] = std::clamp(ParseNumber(parts[i]), 0.0f, 255.0f) / 255;
		}
		if (parts.size() == 3)
		{
			c[3] = 1;
		}
		return D2D1::ColorF(c[0], c[1], c[2], c[3]);
	}
	if (!s.empty() && s.front() == L'#')
	{
		s.erase(0, 1);
	}
	if ((s.size() == 6 || s.size() == 8) &&
		s.find_first_not_of(L"0123456789abcdefABCDEF") == s.npos)
	{
		auto n = wcstoul(s.c_str(), nullptr, 16);
		if (s.size() == 6)
		{
			return D2D1::ColorF(n, 1);
		}
		return D2D1::ColorF(n >> 8, (n & 255) / 255.0f);
	}
	return fallback;
}
std::optional<BackgroundGradient> ParseBackgroundGradient(const std::wstring& text)
{
	const auto parts = Split(text, L'|');
	if (parts.size() < 2 || parts.size() > 257)
	{
		return {};
	}
	const float invalid = std::numeric_limits<float>::quiet_NaN();
	const float angle = ParseNumber(parts[0], invalid);
	if (!std::isfinite(angle))
	{
		return {};
	}
	BackgroundGradient result;
	result.angle = std::fmod(angle, 360.0f);
	for (size_t i = 1; i < parts.size(); ++i)
	{
		const auto stop = Split(parts[i], L';');
		if (stop.size() != 2)
		{
			return {};
		}
		const float position = ParseNumber(stop[1], invalid);
		if (!std::isfinite(position))
		{
			return {};
		}
		const auto channels = Split(stop[0], L',');
		if (channels.size() > 1)
		{
			if (channels.size() != 3 && channels.size() != 4)
			{
				return {};
			}
			for (const auto& channel : channels)
			{
				if (!std::isfinite(ParseNumber(channel, invalid)))
				{
					return {};
				}
			}
		}
		const auto color = Color(stop[0], D2D1::ColorF(invalid, invalid, invalid, invalid));
		if (!std::isfinite(color.r))
		{
			return {};
		}
		// Preserve out-of-range positions: Direct2D uses them for interpolation.
		result.stops.push_back({position, color});
	}
	std::stable_sort(result.stops.begin(), result.stops.end(),
		[](const auto& a, const auto& b) { return a.position < b.position; });
	return result;
}
std::optional<Gfx::TextInlineSetting> ParseInline(const std::wstring& s)
{
	using namespace Gfx;
	auto p = Split(s, L'|');
	auto name = Lower(p[0]);
	auto arg = [&](size_t i, float fallback = 0.0f) {
		return i < p.size() ? ParseNumber(p[i], fallback) : fallback;
	};
	if (name == L"none")
	{
		return InlineSetting::None{};
	}
	if (name == L"italic")
	{
		return InlineSetting::Italic{};
	}
	if (name == L"oblique")
	{
		return InlineSetting::Oblique{};
	}
	if (name == L"underline")
	{
		return InlineSetting::Underline{};
	}
	if (name == L"strikethrough")
	{
		return InlineSetting::Strikethrough{};
	}
	if (p.size() < 2)
	{
		return {};
	}
	if (name == L"face")
	{
		return InlineSetting::Face{p[1]};
	}
	if (name == L"size")
	{
		return InlineSetting::Size{std::clamp(arg(1, 10), 0.1f, 1000.0f)};
	}
	if (name == L"weight")
	{
		return InlineSetting::Weight{
			static_cast<DWRITE_FONT_WEIGHT>(std::clamp((int)arg(1, 400), 1, 999))};
	}
	if (name == L"stretch")
	{
		return InlineSetting::Stretch{
			static_cast<DWRITE_FONT_STRETCH>(std::clamp((int)arg(1, 5), 1, 9))};
	}
	if (name == L"color")
	{
		return InlineSetting::Color{Color(p[1], D2D1::ColorF(0))};
	}
	if (name == L"case")
	{
		auto v = Lower(p[1]);
		auto type = v == L"upper" ? CaseType::Upper
			: v == L"lower"		  ? CaseType::Lower
			: v == L"proper"	  ? CaseType::Proper
			: v == L"sentence"	  ? CaseType::Sentence
								  : CaseType::None;
		return InlineSetting::Case{type};
	}
	if (name == L"characterspacing")
	{
		return InlineSetting::CharacterSpacing{arg(1, FLT_MAX), arg(2, FLT_MAX), arg(3, -1)};
	}
	if (name == L"typography" && p[1].size() == 4)
	{
		UINT32 tag = 0;
		for (int i = 0; i < 4; ++i)
		{
			tag |= (p[1][i] & 255) << (i * 8);
		}
		return InlineSetting::Typography{static_cast<DWRITE_FONT_FEATURE_TAG>(tag),
			static_cast<UINT32>(std::max(0.0f, arg(2, 1)))};
	}
	if (name == L"shadow" && p.size() >= 5)
	{
		return InlineSetting::Shadow{std::clamp(arg(3), 0.0f, 250.0f),
			D2D1::Point2F(arg(1), arg(2)), Color(p[4], D2D1::ColorF(0))};
	}
	if ((name == L"gradientcolor" || name == L"gradientcolor1") && p.size() >= 4)
	{
		InlineSetting::GradientColor g{arg(1), {}, name == L"gradientcolor1"};
		for (size_t i = 2; i < p.size(); ++i)
		{
			auto stop = Split(p[i], L';');
			if (stop.size() != 2)
			{
				return {};
			}
			g.stops.push_back(
				{std::clamp(ParseNumber(stop[1]), 0.0f, 1.0f), Color(stop[0], D2D1::ColorF(0))});
		}
		std::stable_sort(g.stops.begin(), g.stops.end(),
			[](const auto& a, const auto& b) { return a.position < b.position; });
		return g;
	}
	return {};
}
std::vector<Gfx::TextInlineRange> MatchRanges(
	const std::wstring& text, const std::wstring& pattern, void* rm)
{
	std::vector<Gfx::TextInlineRange> ranges;
	const char* error = nullptr;
	int errorOffset = 0;
	auto code = pcre16_compile(
		reinterpret_cast<PCRE_SPTR16>(pattern.c_str()), PCRE_UTF16, &error, &errorOffset, nullptr);
	if (!code)
	{
		RmLog(rm, LOG_WARNING,
			(L"ToolTip: invalid InlinePattern at offset " + std::to_wstring(errorOffset)).c_str());
		return ranges;
	}
	int captures = 0;
	pcre16_fullinfo(code, nullptr, PCRE_INFO_CAPTURECOUNT, &captures);
	std::vector<int> offsets(3 * (captures + 1));
	pcre16_extra limits{};
	limits.flags = PCRE_EXTRA_MATCH_LIMIT | PCRE_EXTRA_MATCH_LIMIT_RECURSION;
	limits.match_limit = 100000;
	limits.match_limit_recursion = 1000;
	int start = 0;
	while (start < static_cast<int>(text.size()))
	{
		int rc = pcre16_exec(code, &limits, reinterpret_cast<PCRE_SPTR16>(text.c_str()),
			static_cast<int>(text.size()), start, PCRE_NOTEMPTY, offsets.data(),
			static_cast<int>(offsets.size()));
		if (rc < 0)
		{
			if (rc != PCRE_ERROR_NOMATCH)
			{
				RmLog(rm, LOG_WARNING, L"ToolTip: InlinePattern match failed or exceeded limits");
			}
			break;
		}
		for (int i = rc > 1 ? 1 : 0; i < rc; ++i)
		{
			if (offsets[2 * i] >= 0 && offsets[2 * i + 1] > offsets[2 * i])
			{
				ranges.push_back({static_cast<UINT32>(offsets[2 * i]),
					static_cast<UINT32>(offsets[2 * i + 1] - offsets[2 * i])});
			}
		}
		if (offsets[1] <= start)
		{
			break;
		}
		start = offsets[1];
	}
	pcre16_free(code);
	return ranges;
}

}

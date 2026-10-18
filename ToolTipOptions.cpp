// Copyright (c) 2026 Jeffrey Morley. Source code licensed under GNU GPL v2 (see LICENSE file).

#include "ToolTipOptions.h"
#include "vendor/StringUtil.h"
#include <sstream>
#include <iomanip>
#include <limits>

namespace ToolTip {

std::wstring Options::Read(const wchar_t* key, const wchar_t* fallback) const
{
	const std::wstring value = RmReadString(m_Rm, key, fallback);
	return m_ReferencesReady
		? ExpandReferences(value, std::wstring_view(key).find(L"InlinePattern") == 0)
		: value;
}

std::wstring Options::ExpandReferences(const std::wstring& value, bool pattern) const
{
	auto replacement = [&](const std::wstring& source) {
		if (!pattern)
		{
			return source;
		}
		if (source.empty())
		{
			// An absent value must not turn an empty InlinePattern into its
			// usual match-all default, or add an empty alternative to a regex.
			return std::wstring(L"(?!)");
		}
		std::wstring quoted = L"\\Q";
		for (size_t i = 0; i < source.size(); ++i)
		{
			if (source[i] == L'\\' && i + 1 < source.size() && source[i + 1] == L'E')
			{
				quoted += L"\\E\\\\E\\Q";
				++i;
			}
			else
			{
				quoted += source[i];
			}
		}
		return quoted + L"\\E";
	};
	std::wstring expanded;
	for (size_t i = 0; i < value.size();)
	{
		const std::wstring* source = nullptr;
		size_t length = 0;
		if (value[i] == L'[' && i + 1 < value.size() && value[i + 1] == L'$')
		{
			if (_wcsnicmp(value.c_str() + i, L"[$Title]", 8) == 0)
			{
				source = &m_TitleReference;
				length = 8;
			}
			else if (_wcsnicmp(value.c_str() + i, L"[$Text]", 7) == 0)
			{
				source = &m_TextReference;
				length = 7;
			}
		}
		if (source)
		{
			expanded += replacement(*source);
			i += length;
		}
		else
		{
			expanded += value[i++];
		}
	}
	return expanded;
}
double Options::Number(const wchar_t* key, double fallback) const
{
	auto value = RmReadFormula(m_Rm, key, fallback);
	return std::isfinite(value) ? value : fallback;
}
void Options::Load(void* context)
{
	m_Rm = context;
	m_ReferencesReady = false;
	text = Read(L"Text");
	title = Read(L"Title");
	ReadTextOptions();
	m_TitleReference = title;
	m_TextReference = ReadResolvedText();
	// Freeze one snapshot for this measure/update. Expand source references once
	// without recursively scanning the inserted values or retaining old values.
	title = ExpandReferences(title);
	text = ExpandReferences(m_TextReference);
	m_TitleReference = title;
	m_TextReference = text;
	m_ReferencesReady = true;
	ReadTextOptions();
	text = ApplyStringCase(std::move(text));
	m_TextReference = text;
	ReadSizeOptions();
	ReadAppearanceOptions();
	ReadImageOptions();
	ReadPlacementOptions();
	ReadBehaviorOptions();
	ReadInlineOptions();
}

void Options::ReadTextOptions()
{
	face = Read(L"FontFace", L"Arial");
	style = Lower(Read(L"StringStyle", L"Normal"));
	align = Lower(Read(L"StringAlign", L"Left"));
	effect = Lower(Read(L"StringEffect", L"None"));
	stringCase = Lower(Read(L"StringCase", L"None"));
	const auto positionParts = Split(Lower(Read(L"Position", L"Auto")), L'|');
	position = positionParts[0];
	horizontalPosition.clear();
	if (positionParts.size() > 1)
	{
		if (positionParts.size() == 2 && (position == L"below" || position == L"above") &&
			(positionParts[1] == L"left" || positionParts[1] == L"right"))
		{
			horizontalPosition = positionParts[1];
		}
		else
		{
			RmLog(m_Rm, LOG_WARNING,
				L"ToolTip: invalid Position pair (use Below/Above | Left/Right)");
			position = L"auto";
		}
	}
	icon = Read(L"Icon", L"None");
	size = static_cast<float>(std::clamp(Number(L"FontSize", 10), 0.1, 1000.0));
	titleFontSize = static_cast<float>(std::clamp(Number(L"TitleFontSize", size), 0.1, 1000.0));
	weight = static_cast<int>(std::clamp(
		Number(L"FontWeight", style.find(L"bold") != style.npos ? 700 : 400), 1.0, 999.0));
}

void Options::ReadSizeOptions()
{
	auto dimension = [&](const wchar_t* key) {
		return static_cast<float>(std::clamp(Number(key, 0), 0.0, 8192.0));
	};
	width = dimension(L"W");
	height = dimension(L"H");
	iconGap = static_cast<float>(std::clamp(Number(L"IconGap", 6), 0.0, 8192.0));
	titleTextGap = static_cast<float>(std::clamp(Number(L"TitleTextGap", 5), 0.0, 8192.0));
	clipW = dimension(L"ClipStringW");
	clipH = dimension(L"ClipStringH");
	clip = static_cast<int>(std::clamp(Number(L"ClipString", 2), 0.0, 2.0));
}

void Options::ReadAppearanceOptions()
{
	background = Color(Read(L"SolidColor", L"255,255,255,255"), D2D1::ColorF(0xffffff));
	auto second = Read(L"SolidColor2");
	backgroundGradient = !second.empty();
	background2 = Color(second, background);
	gradientAngle = static_cast<float>(Number(L"GradientAngle", 0));
	const auto gradientDefinition = Trim(Read(L"LinearGradient"));
	linearGradient =
		gradientDefinition.empty() ? std::nullopt : ParseBackgroundGradient(gradientDefinition);
	if (!gradientDefinition.empty() && !linearGradient)
	{
		RmLog(m_Rm, LOG_WARNING,
			L"ToolTip: invalid LinearGradient (use Angle | Color ; Position, up to 256 stops)");
	}
	font = Color(Read(L"FontColor", L"0,0,0,255"), D2D1::ColorF(0));
	titleColor = Color(Read(L"TitleColor"), font);
	auto coordinate = [&](const wchar_t* key) -> std::optional<float> {
		if (Trim(Read(key)).empty())
		{
			return {};
		}
		return static_cast<float>(std::clamp(Number(key, 0), -8192.0, 8192.0));
	};
	iconX = coordinate(L"IconX");
	iconY = coordinate(L"IconY");
	closeButtonX = coordinate(L"CloseButtonX");
	closeButtonY = coordinate(L"CloseButtonY");
	auto textCoordinate = [&](const wchar_t* key) -> std::optional<TextCoordinate> {
		auto value = Trim(Read(key));
		if (value.empty())
		{
			return {};
		}
		const auto suffix = value.back();
		if (suffix != L'r' && suffix != L'R')
		{
			return TextCoordinate{coordinate(key).value_or(0), 0};
		}
		value = Trim(value.substr(0, value.size() - 1));
		const float offset =
			value.empty() ? 0 : ParseNumber(value, std::numeric_limits<float>::quiet_NaN());
		if (!std::isfinite(offset))
		{
			RmLog(m_Rm, LOG_WARNING,
				(std::wstring(L"ToolTip: invalid ") + key + L" relative offset (use 4r or 4R)")
					.c_str());
			return {};
		}
		return TextCoordinate{std::clamp(offset, -8192.0f, 8192.0f), suffix};
	};
	titleX = textCoordinate(L"TitleX");
	titleY = textCoordinate(L"TitleY");
	textX = textCoordinate(L"TextX");
	textY = textCoordinate(L"TextY");
	effectColor = Color(Read(L"FontEffectColor", L"0,0,0,255"), D2D1::ColorF(0));
	border = Color(Read(L"StrokeColor", L"0,0,0,255"), D2D1::ColorF(0));
	borderWidth = static_cast<float>(std::clamp(Number(L"BorderWidth", 1), 0.0, 32.0));
	borderWidth = static_cast<float>(std::clamp(Number(L"StrokeSize", borderWidth), 0.0, 32.0));
	strokeBevel = Lower(Trim(Read(L"StrokeBevel", L"None")));
	if (strokeBevel != L"none" && strokeBevel != L"raised" && strokeBevel != L"sunken")
	{
		RmLog(m_Rm, LOG_WARNING, L"ToolTip: invalid StrokeBevel (use None, Raised, or Sunken)");
		strokeBevel = L"none";
	}
	bevelLight = Color(Read(L"StrokeBevelLightColor"),
		D2D1::ColorF(border.r + (1 - border.r) * 0.45f, border.g + (1 - border.g) * 0.45f,
			border.b + (1 - border.b) * 0.45f, border.a));
	bevelDark = Color(Read(L"StrokeBevelDarkColor"),
		D2D1::ColorF(border.r * 0.55f, border.g * 0.55f, border.b * 0.55f, border.a));
	bevelProfile = Lower(Trim(Read(L"StrokeBevelProfile", L"Linear")));
	if (bevelProfile != L"linear" && bevelProfile != L"curved" && bevelProfile != L"concave" &&
		bevelProfile != L"bulged")
	{
		RmLog(m_Rm, LOG_WARNING,
			L"ToolTip: invalid StrokeBevelProfile (use Linear, Curved, Concave, or Bulged)");
		bevelProfile = L"linear";
	}
	bevelWidth = static_cast<float>(std::clamp(
		Number(L"StrokeBevelWidth", borderWidth), 0.0, static_cast<double>(borderWidth)));
	bevelRelief = static_cast<float>(std::clamp(Number(L"StrokeBevelRelief", 1), 0.0, 1.0));
	bevelSmoothness = static_cast<float>(std::clamp(Number(L"StrokeBevelSmoothness", 0), 0.0, 1.0));
	bevelAngle = static_cast<float>(std::fmod(Number(L"StrokeBevelLightAngle", 225), 360.0));
	radius = static_cast<float>(std::clamp(Number(L"CornerRadius", 6), 0.0, 256.0));
}

void Options::ReadImageOptions()
{
	image = {};
	image.path = Trim(Read(L"BackgroundImage"));
	if (!image.path.empty())
	{
		image.path = RmPathToAbsolute(m_Rm, image.path.c_str());
	}
	const auto tint = Color(Read(L"ImageTint", L"255,255,255,255"), D2D1::ColorF(0xffffff));
	image.tint = {tint.r, tint.g, tint.b, tint.a};
	image.greyscale = Number(L"GreyScale", Number(L"GrayScale", 0)) != 0;
	image.rotate = static_cast<float>(std::fmod(Number(L"ImageRotate", 0), 360.0));
	const auto flip = Lower(Trim(Read(L"ImageFlip", L"None")));
	if (flip == L"horizontal" || flip == L"both")
	{
		image.flip |= 1;
	}
	if (flip == L"vertical" || flip == L"both")
	{
		image.flip |= 2;
	}
	const auto value = Trim(Read(L"ImageCrop"));
	if (!value.empty())
	{
		const auto parts = Split(value, L',');
		std::array<float, 5> values = {0, 0, 0, 0, 1};
		bool valid = parts.size() == 4 || parts.size() == 5;
		for (size_t i = 0; i < parts.size() && i < values.size(); ++i)
		{
			values[i] = ParseNumber(parts[i], std::numeric_limits<float>::quiet_NaN());
			valid = valid && std::isfinite(values[i]);
		}
		valid = valid && values[2] > 0 && values[3] > 0 && values[4] >= 1 && values[4] <= 5 &&
			values[4] == std::floor(values[4]);
		if (valid)
		{
			image.crop =
				ImageCrop{values[0], values[1], values[2], values[3], static_cast<int>(values[4])};
		}
		else
		{
			RmLog(m_Rm, LOG_WARNING,
				L"ToolTip: invalid ImageCrop (use X,Y,W,H[,Origin], with positive W/H and Origin "
				L"1-5)");
		}
	}
}

void Options::ReadPlacementOptions()
{
	auto pads = Split(Read(L"Padding", L"8,6,8,6"), L',');
	for (size_t i = 0; i < 4; ++i)
	{
		padding[i] = i < pads.size() ? std::clamp(ParseNumber(pads[i]), 0.0f, 512.0f) : 0;
	}
	offsetX = static_cast<float>(std::clamp(Number(L"OffsetX", 0), -8192.0, 8192.0));
	offsetY = static_cast<float>(
		std::clamp(Number(L"OffsetY", Number(L"Balloon", 0) != 0 ? 2 : 20), -8192.0, 8192.0));
	tailPosition = Lower(Trim(Read(L"TailPosition", L"Center")));
	if (tailPosition != L"left" && tailPosition != L"center" && tailPosition != L"right")
	{
		tailPosition = L"center";
	}
	tailGap = Read(L"TailGap").empty()
		? -1.0f
		: static_cast<float>(std::clamp(Number(L"TailGap", 2), 0.0, 8192.0));
}

void Options::ReadBehaviorOptions()
{
	auto delay = [&](const wchar_t* key, int def) {
		return static_cast<int>(std::clamp(Number(key, def), 0.0, 3600000.0));
	};
	initialDelay = delay(L"InitialDelay", 0);
	reshowDelay = delay(L"ReshowDelay", 100);
	autoPopDelay = delay(L"AutoPopDelay", 0);
	auto flag = [&](const wchar_t* key, bool def) { return Number(key, def ? 1 : 0) != 0; };
	balloon = flag(L"Balloon", false);
	close = flag(L"CloseButton", false);
	always = flag(L"AlwaysTip", true);
	follow = flag(L"FollowMouse", true);
	keepOnScreen = flag(L"KeepOnScreen", true);
	dismissClick = flag(L"DismissOnClick", true);
	antiAlias = flag(L"AntiAlias", true);
	noFade = flag(L"NoFade", false);
	enabled = flag(L"Enabled", true);
	topmost = flag(L"Topmost", true);
}

void Options::ReadInlineOptions()
{
	inlines.clear();
	for (int i = 1; i <= 256; ++i)
	{
		auto suffix = i == 1 ? L"" : std::to_wstring(i);
		auto setting = Read((L"InlineSetting" + suffix).c_str());
		if (setting.empty())
		{
			break;
		}
		auto parsed = ParseInline(setting);
		if (parsed)
		{
			inlines.push_back({Read((L"InlinePattern" + suffix).c_str(), L".*"), *parsed});
		}
		else
		{
			RmLog(m_Rm, LOG_WARNING, (L"ToolTip: invalid InlineSetting" + suffix).c_str());
		}
	}
}

std::wstring Options::ResolveText() const
{
	return text;
}

std::wstring Options::ReadResolvedText() const
{
	auto result = Read(L"Text", Read(L"MeasureName").empty() ? L"" : L"%1");
	auto scale = Number(L"Scale", 1);
	if (scale == 0)
	{
		scale = 1;
	}
	const auto autoScale = Lower(Read(L"AutoScale", L"0"));
	const bool autos = autoScale != L"0";
	const bool fractionalScale = Read(L"Scale", L"1").find(L'.') != std::wstring::npos;
	const int defaultDecimals = (autos || fractionalScale) && !Number(L"Percentual", 0) ? 1 : 0;
	const int decimals =
		static_cast<int>(std::clamp(Number(L"NumOfDecimals", defaultDecimals), 0.0, 16.0));
	std::vector<std::wstring> values;
	for (int i = 1; i <= 256; ++i)
	{
		auto suffix = i == 1 ? L"" : std::to_wstring(i);
		auto name = Read((L"MeasureName" + suffix).c_str());
		if (name.empty())
		{
			break;
		}
		auto reference = L"[" + name + L"]";
		std::wstring value = RmReplaceVariables(m_Rm, reference.c_str());
		// All Rainmeter measures have a numeric value, including text measures whose numeric
		// value is zero. Never replace a nonnumeric display string with that numeric value.
		wchar_t* displayEnd = nullptr;
		wcstod(value.c_str(), &displayEnd);
		const bool numericDisplay = displayEnd != value.c_str() && *displayEnd == 0;
		const std::wstring measureType =
			RmReadStringFromSection(m_Rm, name.c_str(), L"Measure", L"");
		const auto type = Lower(measureType);
		const bool stringMeasure = type == L"string" || type == L"time";
		auto numericRef = L"[" + name + L":16]";
		std::wstring numeric = RmReplaceVariables(m_Rm, numericRef.c_str());
		wchar_t* end = nullptr;
		double n = wcstod(numeric.c_str(), &end);
		if (numericDisplay && !stringMeasure && end != numeric.c_str() && *end == 0 &&
			std::isfinite(n))
		{
			if (Number(L"Percentual", 0) != 0)
			{
				auto percentage = L"[" + name + L":%,16]";
				std::wstring p = RmReplaceVariables(m_Rm, percentage.c_str());
				n = wcstod(p.c_str(), nullptr);
			}
			int unit = 0;
			if (autos)
			{
				auto base = autoScale[0] == L'2' ? 1000.0 : 1024.0;
				if (autoScale.find(L'k') != autoScale.npos)
				{
					n /= base;
					unit = 1;
				}
				while (std::abs(n) >= base && unit < 6)
				{
					n /= base;
					++unit;
				}
			}
			else
			{
				n /= scale;
			}
			std::wostringstream out;
			out << std::fixed << std::setprecision(decimals) << n;
			if (unit)
			{
				out << L' ' << L" kMGTPE"[unit];
			}
			value = out.str();
		}
		values.push_back(std::move(value));
	}
	// Parse placeholders once so %1 cannot corrupt %10 or the replacement values.
	std::wstring expanded;
	for (size_t i = 0; i < result.size();)
	{
		if (result[i] == L'%' && i + 1 < result.size() && iswdigit(result[i + 1]))
		{
			size_t end = i + 1;
			unsigned n = 0;
			while (end < result.size() && iswdigit(result[end]))
			{
				n = std::min(10000u, n * 10 + result[end++] - L'0');
			}
			if (n && n <= values.size())
			{
				expanded += values[n - 1];
			}
			else
			{
				expanded += result.substr(i, end - i);
			}
			i = end;
		}
		else
		{
			expanded += result[i++];
		}
	}
	result = Read(L"Prefix") + expanded + Read(L"Postfix");
	if (!Number(L"TrailingSpaces", 0))
	{
		result = Trim(result);
	}
	return ApplyStringCase(std::move(result));
}

std::wstring Options::ApplyStringCase(std::wstring value) const
{
	if (stringCase == L"upper")
	{
		StringUtil::ToUpperCase(value.data(), value.size());
	}
	if (stringCase == L"lower")
	{
		StringUtil::ToLowerCase(value.data(), value.size());
	}
	if (stringCase == L"proper")
	{
		StringUtil::ToProperCase(value.data(), value.size());
	}
	return value;
}
void Options::Configure(Gfx::TextFormat& format, Gfx::FontCollection* fonts,
	const std::wstring& resolved, bool heading) const
{
	format.SetProperties(face.c_str(), heading ? titleFontSize : size,
		heading || style.find(L"bold") != style.npos,
		!heading && style.find(L"italic") != style.npos, fonts);
	if (!heading)
	{
		format.SetFontWeight(weight);
		format.SetTrimming(clip != 0, clip != 0);
		using H = Gfx::HorizontalAlignment;
		using V = Gfx::VerticalAlignment;
		format.SetHorizontalAlignment(align.find(L"right") == 0 ? H::Right
				: align.find(L"center") == 0					? H::Center
				: align.find(L"justify") == 0					? H::Justify
																: H::Left);
		format.SetVerticalAlignment(align.find(L"bottom") != align.npos				? V::Bottom
				: (align.size() > 6 && align.substr(align.size() - 6) == L"center") ? V::Center
																					: V::Top);
	}
	format.SetInlineOptions(inlines);
	std::vector<std::vector<Gfx::TextInlineRange>> ranges;
	for (const auto& option : inlines)
	{
		ranges.push_back(
			MatchRanges(resolved, option.pattern.empty() ? L".*" : option.pattern, m_Rm));
	}
	format.SetInlineRanges(ranges);
}
} // namespace ToolTip

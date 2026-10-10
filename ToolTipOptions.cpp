#include "ToolTipOptions.h"
#include "vendor/StringUtil.h"
#include "vendor/pcre/pcre.h"
#include <sstream>
#include <iomanip>
#include <limits>

namespace ToolTip {
std::wstring Trim(std::wstring s) {
    auto first = s.find_first_not_of(L" \t\r\n");
    return first == s.npos ? L"" : s.substr(first, s.find_last_not_of(L" \t\r\n") - first + 1);
}
std::wstring Lower(std::wstring s) {
    std::transform(s.begin(), s.end(), s.begin(), [](wchar_t c) { return static_cast<wchar_t>(towlower(c)); });
    return s;
}
std::vector<std::wstring> Split(const std::wstring& s, wchar_t delimiter) {
    std::vector<std::wstring> result;
    size_t start = 0, end;
    do {
        end = s.find(delimiter, start);
        result.push_back(Trim(s.substr(start, end == s.npos ? end : end - start)));
        start = end + 1;
    } while (end != s.npos);
    return result;
}
static float Value(const std::wstring& s, float fallback = 0) {
    wchar_t* end = nullptr;
    double n = wcstod(s.c_str(), &end);
    return end != s.c_str() && *end == 0 && std::isfinite(n) ? static_cast<float>(n) : fallback;
}
D2D1_COLOR_F Color(const std::wstring& input, D2D1_COLOR_F fallback) {
    const auto s = Trim(input);
    const auto parts = Split(s, L',');
    if (parts.size() >= 3 && parts.size() <= 4) {
        float c[4] = {0, 0, 0, 255};
        for (size_t i = 0; i < parts.size(); ++i) c[i] = std::clamp(Value(parts[i]), 0.0f, 255.0f) / 255;
        if (parts.size() == 3) c[3] = 1;
        return D2D1::ColorF(c[0], c[1], c[2], c[3]);
    }
    if ((s.size() == 6 || s.size() == 8) && s.find_first_not_of(L"0123456789abcdefABCDEF") == s.npos) {
        auto n = wcstoul(s.c_str(), nullptr, 16);
        if (s.size() == 6) return D2D1::ColorF(n, 1);
        return D2D1::ColorF(n >> 8, (n & 255) / 255.0f);
    }
    return fallback;
}
std::optional<Gfx::TextInlineSetting> ParseInline(const std::wstring& s) {
    using namespace Gfx;
    auto p = Split(s, L'|');
    auto name = Lower(p[0]);
    auto arg = [&](size_t i, float fallback = 0.0f) { return i < p.size() ? Value(p[i], fallback) : fallback; };
    if (name == L"none") return InlineSetting::None{};
    if (name == L"italic") return InlineSetting::Italic{};
    if (name == L"oblique") return InlineSetting::Oblique{};
    if (name == L"underline") return InlineSetting::Underline{};
    if (name == L"strikethrough") return InlineSetting::Strikethrough{};
    if (p.size() < 2) return {};
    if (name == L"face") return InlineSetting::Face{p[1]};
    if (name == L"size") return InlineSetting::Size{std::clamp(arg(1, 10), 0.1f, 1000.0f)};
    if (name == L"weight") return InlineSetting::Weight{static_cast<DWRITE_FONT_WEIGHT>(std::clamp((int)arg(1, 400), 1, 999))};
    if (name == L"stretch") return InlineSetting::Stretch{static_cast<DWRITE_FONT_STRETCH>(std::clamp((int)arg(1, 5), 1, 9))};
    if (name == L"color") return InlineSetting::Color{Color(p[1], D2D1::ColorF(0))};
    if (name == L"case") {
        auto v = Lower(p[1]);
        auto type = v == L"upper" ? CaseType::Upper : v == L"lower" ? CaseType::Lower :
            v == L"proper" ? CaseType::Proper : v == L"sentence" ? CaseType::Sentence : CaseType::None;
        return InlineSetting::Case{type};
    }
    if (name == L"characterspacing") return InlineSetting::CharacterSpacing{arg(1, FLT_MAX), arg(2, FLT_MAX), arg(3, -1)};
    if (name == L"typography" && p[1].size() == 4) {
        UINT32 tag = 0;
        for (int i = 0; i < 4; ++i) tag |= (p[1][i] & 255) << (i * 8);
        return InlineSetting::Typography{static_cast<DWRITE_FONT_FEATURE_TAG>(tag), static_cast<UINT32>(std::max(0.0f, arg(2, 1)))};
    }
    if (name == L"shadow" && p.size() >= 5) return InlineSetting::Shadow{
        std::clamp(arg(3), 0.0f, 250.0f), D2D1::Point2F(arg(1), arg(2)), Color(p[4], D2D1::ColorF(0))};
    if ((name == L"gradientcolor" || name == L"gradientcolor1") && p.size() >= 4) {
        InlineSetting::GradientColor g{arg(1), {}, name == L"gradientcolor1"};
        for (size_t i = 2; i < p.size(); ++i) {
            auto stop = Split(p[i], L';');
            if (stop.size() != 2) return {};
            g.stops.push_back({std::clamp(Value(stop[1]), 0.0f, 1.0f), Color(stop[0], D2D1::ColorF(0))});
        }
        std::stable_sort(g.stops.begin(), g.stops.end(), [](const auto& a, const auto& b) { return a.position < b.position; });
        return g;
    }
    return {};
}
std::vector<Gfx::TextInlineRange> MatchRanges(const std::wstring& text, const std::wstring& pattern, void* rm) {
    std::vector<Gfx::TextInlineRange> ranges;
    const char* error = nullptr;
    int errorOffset = 0;
    auto code = pcre16_compile(reinterpret_cast<PCRE_SPTR16>(pattern.c_str()), PCRE_UTF16, &error, &errorOffset, nullptr);
    if (!code) {
        RmLog(rm, LOG_WARNING, (L"ToolTip: invalid InlinePattern at offset " + std::to_wstring(errorOffset)).c_str());
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
    while (start < static_cast<int>(text.size())) {
        int rc = pcre16_exec(code, &limits, reinterpret_cast<PCRE_SPTR16>(text.c_str()),
            static_cast<int>(text.size()), start, PCRE_NOTEMPTY, offsets.data(), static_cast<int>(offsets.size()));
        if (rc < 0) {
            if (rc != PCRE_ERROR_NOMATCH) RmLog(rm, LOG_WARNING, L"ToolTip: InlinePattern match failed or exceeded limits");
            break;
        }
        for (int i = rc > 1 ? 1 : 0; i < rc; ++i) {
            if (offsets[2 * i] >= 0 && offsets[2 * i + 1] > offsets[2 * i]) ranges.push_back({
                static_cast<UINT32>(offsets[2 * i]), static_cast<UINT32>(offsets[2 * i + 1] - offsets[2 * i])});
        }
        if (offsets[1] <= start) break;
        start = offsets[1];
    }
    pcre16_free(code);
    return ranges;
}
std::wstring Options::Read(const wchar_t* key, const wchar_t* fallback) const { return RmReadString(rm, key, fallback); }
double Options::Number(const wchar_t* key, double fallback) const {
    auto value = RmReadFormula(rm, key, fallback);
    return std::isfinite(value) ? value : fallback;
}
void Options::Load(void* context) {
    rm = context;
    text = Read(L"Text"); title = Read(L"Title"); face = Read(L"FontFace", L"Arial");
    style = Lower(Read(L"StringStyle", L"Normal")); align = Lower(Read(L"StringAlign", L"Left"));
    effect = Lower(Read(L"StringEffect", L"None")); stringCase = Lower(Read(L"StringCase", L"None"));
    position = Lower(Read(L"Position", L"Auto")); icon = Read(L"Icon", L"None");
    size = static_cast<float>(std::clamp(Number(L"FontSize", 10), 0.1, 1000.0));
    weight = static_cast<int>(std::clamp(Number(L"FontWeight", style.find(L"bold") != style.npos ? 700 : 400), 1.0, 999.0));
    auto dimension = [&](const wchar_t* key) { return static_cast<float>(std::clamp(Number(key, 0), 0.0, 8192.0)); };
    width = dimension(L"W"); height = dimension(L"H"); clipW = dimension(L"ClipStringW"); clipH = dimension(L"ClipStringH");
    clip = static_cast<int>(std::clamp(Number(L"ClipString", 2), 0.0, 2.0));
    background = Color(Read(L"SolidColor", L"255,255,225,255"), D2D1::ColorF(0xffffe1));
    auto second = Read(L"SolidColor2"); backgroundGradient = !second.empty(); background2 = Color(second, background);
    gradientAngle = static_cast<float>(Number(L"GradientAngle", 0));
    font = Color(Read(L"FontColor", L"0,0,0,255"), D2D1::ColorF(0));
    effectColor = Color(Read(L"FontEffectColor", L"0,0,0,255"), D2D1::ColorF(0));
    border = Color(Read(L"BorderColor", L"110,110,110,255"), D2D1::ColorF(0x6e6e6e));
    borderWidth = static_cast<float>(std::clamp(Number(L"BorderWidth", 1), 0.0, 32.0));
    radius = static_cast<float>(std::clamp(Number(L"CornerRadius", 6), 0.0, 256.0));
    auto pads = Split(Read(L"Padding", L"8,6,8,6"), L',');
    for (size_t i = 0; i < 4; ++i) padding[i] = i < pads.size() ? std::clamp(Value(pads[i]), 0.0f, 512.0f) : 0;
    offsetX = static_cast<float>(std::clamp(Number(L"OffsetX", 0), -8192.0, 8192.0));
    offsetY = static_cast<float>(std::clamp(Number(L"OffsetY", Number(L"Balloon", 0) != 0 ? 2 : 20), -8192.0, 8192.0));
    auto delay = [&](const wchar_t* key, int def) { return static_cast<int>(std::clamp(Number(key, def), 0.0, 3600000.0)); };
    initialDelay = delay(L"InitialDelay", 500); reshowDelay = delay(L"ReshowDelay", 100); autoPopDelay = delay(L"AutoPopDelay", 5000);
    auto flag = [&](const wchar_t* key, bool def) { return Number(key, def ? 1 : 0) != 0; };
    balloon = flag(L"Balloon", false); close = flag(L"CloseButton", false); always = flag(L"AlwaysTip", true);
    follow = flag(L"FollowMouse", false); keepOnScreen = flag(L"KeepOnScreen", true); dismissClick = flag(L"DismissOnClick", true);
    antiAlias = flag(L"AntiAlias", true); noFade = flag(L"NoFade", false);
    enabled = flag(L"Enabled", true); topmost = flag(L"Topmost", true);
    inlines.clear();
    for (int i = 1; i <= 256; ++i) {
        auto suffix = i == 1 ? L"" : std::to_wstring(i);
        auto setting = Read((L"InlineSetting" + suffix).c_str());
        if (setting.empty()) break;
        auto parsed = ParseInline(setting);
        if (parsed) inlines.push_back({Read((L"InlinePattern" + suffix).c_str(), L".*"), *parsed});
        else RmLog(rm, LOG_WARNING, (L"ToolTip: invalid InlineSetting" + suffix).c_str());
    }
}
std::wstring Options::ResolveText() const {
    auto result = Read(L"Text", Read(L"MeasureName").empty() ? L"" : L"%1");
    auto scale = Number(L"Scale", 1);
    if (scale == 0) scale = 1;
    const auto autoScale = Lower(Read(L"AutoScale", L"0"));
    const bool autos = autoScale != L"0";
    const bool fractionalScale = Read(L"Scale", L"1").find(L'.') != std::wstring::npos;
    const int defaultDecimals = (autos || fractionalScale) && !Number(L"Percentual", 0) ? 1 : 0;
    const int decimals = static_cast<int>(std::clamp(Number(L"NumOfDecimals", defaultDecimals), 0.0, 16.0));
    std::vector<std::wstring> values;
    for (int i = 1; i <= 256; ++i) {
        auto suffix = i == 1 ? L"" : std::to_wstring(i);
        auto name = Read((L"MeasureName" + suffix).c_str());
        if (name.empty()) break;
        auto reference = L"[" + name + L"]";
        std::wstring value = RmReplaceVariables(rm, reference.c_str());
        // All Rainmeter measures have a numeric value, including text measures whose numeric
        // value is zero. Never replace a nonnumeric display string with that numeric value.
        wchar_t* displayEnd = nullptr;
        wcstod(value.c_str(), &displayEnd);
        const bool numericDisplay = displayEnd != value.c_str() && *displayEnd == 0;
        const std::wstring measureType = RmReadStringFromSection(rm, name.c_str(), L"Measure", L"");
        const auto type = Lower(measureType);
        const bool stringMeasure = type == L"string" || type == L"time";
        auto numericRef = L"[" + name + L":16]";
        std::wstring numeric = RmReplaceVariables(rm, numericRef.c_str());
        wchar_t* end = nullptr;
        double n = wcstod(numeric.c_str(), &end);
        if (numericDisplay && !stringMeasure && end != numeric.c_str() && *end == 0 && std::isfinite(n)) {
            if (Number(L"Percentual", 0) != 0) {
                auto percentage = L"[" + name + L":%,16]";
                std::wstring p = RmReplaceVariables(rm, percentage.c_str());
                n = wcstod(p.c_str(), nullptr);
            }
            int unit = 0;
            if (autos) {
                auto base = autoScale[0] == L'2' ? 1000.0 : 1024.0;
                if (autoScale.find(L'k') != autoScale.npos) { n /= base; unit = 1; }
                while (std::abs(n) >= base && unit < 6) { n /= base; ++unit; }
            } else n /= scale;
            std::wostringstream out;
            out << std::fixed << std::setprecision(decimals) << n;
            if (unit) out << L' ' << L" kMGTPE"[unit];
            value = out.str();
        }
        values.push_back(std::move(value));
    }
    // Parse placeholders once so %1 cannot corrupt %10 or the replacement values.
    std::wstring expanded;
    for (size_t i = 0; i < result.size();) {
        if (result[i] == L'%' && i + 1 < result.size() && iswdigit(result[i + 1])) {
            size_t end = i + 1;
            unsigned n = 0;
            while (end < result.size() && iswdigit(result[end])) { n = std::min(10000u, n * 10 + result[end++] - L'0'); }
            if (n && n <= values.size()) expanded += values[n - 1]; else expanded += result.substr(i, end - i);
            i = end;
        } else expanded += result[i++];
    }
    result = Read(L"Prefix") + expanded + Read(L"Postfix");
    if (!Number(L"TrailingSpaces", 0)) result = Trim(result);
    if (stringCase == L"upper") StringUtil::ToUpperCase(result.data(), result.size());
    if (stringCase == L"lower") StringUtil::ToLowerCase(result.data(), result.size());
    if (stringCase == L"proper") StringUtil::ToProperCase(result.data(), result.size());
    return result;
}
void Options::Configure(Gfx::TextFormat& format, Gfx::FontCollection* fonts, const std::wstring& resolved) const {
    format.SetProperties(face.c_str(), size, style.find(L"bold") != style.npos, style.find(L"italic") != style.npos, fonts);
    format.SetFontWeight(weight);
    format.SetTrimming(clip != 0, clip != 0);
    using H = Gfx::HorizontalAlignment; using V = Gfx::VerticalAlignment;
    format.SetHorizontalAlignment(align.find(L"right") == 0 ? H::Right : align.find(L"center") == 0 ? H::Center :
        align.find(L"justify") == 0 ? H::Justify : H::Left);
    format.SetVerticalAlignment(align.find(L"bottom") != align.npos ? V::Bottom :
        (align.size() > 6 && align.substr(align.size() - 6) == L"center") ? V::Center : V::Top);
    format.SetInlineOptions(inlines);
    std::vector<std::vector<Gfx::TextInlineRange>> ranges;
    for (const auto& option : inlines) ranges.push_back(MatchRanges(resolved, option.pattern.empty() ? L".*" : option.pattern, rm));
    format.SetInlineRanges(ranges);
}
}

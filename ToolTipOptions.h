#pragma once
#include "vendor/StdAfx.h"
#include "../../API/RainmeterAPI.h"
#include "vendor/Gfx/Canvas.h"
#include <string>
#include <optional>

namespace ToolTip {
std::wstring Trim(std::wstring text);
std::wstring Lower(std::wstring text);
std::vector<std::wstring> Split(const std::wstring& text, wchar_t delimiter);
D2D1_COLOR_F Color(const std::wstring& text, D2D1_COLOR_F fallback);
std::optional<Gfx::TextInlineSetting> ParseInline(const std::wstring& text);
std::vector<Gfx::TextInlineRange> MatchRanges(const std::wstring& text, const std::wstring& pattern, void* rm);
struct BackgroundGradient {
    float angle = 0;
    std::vector<D2D1_GRADIENT_STOP> stops;
};
std::optional<BackgroundGradient> ParseBackgroundGradient(const std::wstring& text);
struct TextCoordinate {
    float offset = 0;
    wchar_t relative = 0; // r: title start edge; R: title end edge; 0: absolute.
};

struct Options {
    void* rm = nullptr;
    std::wstring text, title, face, style, align, effect, position, icon, stringCase, strokeBevel, tailPosition;
    D2D1_COLOR_F background{}, background2{}, font{}, titleColor{}, effectColor{}, border{}, bevelLight{}, bevelDark{};
    float size = 10, width = 0, height = 0, clipW = 0, clipH = 0;
    float padding[4] = {8, 6, 8, 6};
    float borderWidth = 1, radius = 6, gradientAngle = 0, offsetX = 0, offsetY = 20;
    float tailGap = -1; // Unset: retain OffsetY's existing scaled gap.
    std::optional<float> titleX, titleY, closeButtonX, closeButtonY;
    std::optional<TextCoordinate> textX, textY;
    float titleFontSize = 10;
    std::optional<BackgroundGradient> linearGradient;
    std::wstring bevelProfile;
    float bevelWidth = 1, bevelRelief = 1, bevelSmoothness = 0, bevelAngle = 225;
    int weight = 400, clip = 2, initialDelay = 500, reshowDelay = 100, autoPopDelay = 5000;
    bool balloon = false, close = false, always = true, follow = false, keepOnScreen = true;
    bool antiAlias = true, noFade = false;
    bool dismissClick = true, enabled = true, backgroundGradient = false, topmost = true;
    std::vector<Gfx::TextInlineOption> inlines;
    std::wstring Read(const wchar_t* key, const wchar_t* fallback = L"") const;
    double Number(const wchar_t* key, double fallback) const;
    void Load(void* context);
    std::wstring ResolveText() const;
    void Configure(Gfx::TextFormat& format, Gfx::FontCollection* fonts, const std::wstring& resolved, bool heading = false) const;
};
}

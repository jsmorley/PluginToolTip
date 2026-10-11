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

struct Options {
    void* rm = nullptr;
    std::wstring text, title, face, style, align, effect, position, icon, stringCase, strokeBevel;
    D2D1_COLOR_F background{}, background2{}, font{}, effectColor{}, border{}, bevelLight{}, bevelDark{};
    float size = 10, width = 0, height = 0, clipW = 0, clipH = 0;
    float padding[4] = {8, 6, 8, 6};
    float borderWidth = 1, radius = 6, gradientAngle = 0, offsetX = 0, offsetY = 20;
    int weight = 400, clip = 2, initialDelay = 500, reshowDelay = 100, autoPopDelay = 5000;
    bool balloon = false, close = false, always = true, follow = false, keepOnScreen = true;
    bool antiAlias = true, noFade = false;
    bool dismissClick = true, enabled = true, backgroundGradient = false, topmost = true;
    std::vector<Gfx::TextInlineOption> inlines;
    std::wstring Read(const wchar_t* key, const wchar_t* fallback = L"") const;
    double Number(const wchar_t* key, double fallback) const;
    void Load(void* context);
    std::wstring ResolveText() const;
    void Configure(Gfx::TextFormat& format, Gfx::FontCollection* fonts, const std::wstring& resolved) const;
};
}

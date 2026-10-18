// Copyright (c) 2026 Jeffrey Morley. Source code licensed under GNU GPL v2 (see LICENSE file).

#pragma once
#include "vendor/StdAfx.h"
#include "../../API/RainmeterAPI.h"
#include "vendor/Gfx/Canvas.h"
#include <string>
#include <array>
#include "ToolTipParser.h"

namespace ToolTip {
struct ImageCrop
{
	float x = 0;
	float y = 0;
	float width = 0;
	float height = 0;
	int origin = 1;
	bool operator==(const ImageCrop&) const = default;
};

struct BackgroundImageOptions
{
	std::wstring path;
	std::optional<ImageCrop> crop;
	std::array<float, 4> tint = {1, 1, 1, 1};
	int flip = 0;
	float rotate = 0;
	bool greyscale = false;
	bool operator==(const BackgroundImageOptions&) const = default;
};

struct Options
{
	// Parsed skin options are a value snapshot consumed by layout and drawing.
	std::wstring text;
	std::wstring title;
	std::wstring face;
	std::wstring style;
	std::wstring align;
	std::wstring effect;
	std::wstring position;
	std::wstring horizontalPosition;
	std::wstring icon;
	std::wstring stringCase;
	std::wstring strokeBevel;
	std::wstring tailPosition;
	D2D1_COLOR_F background{};
	D2D1_COLOR_F background2{};
	D2D1_COLOR_F font{};
	D2D1_COLOR_F titleColor{};
	D2D1_COLOR_F effectColor{};
	D2D1_COLOR_F border{};
	D2D1_COLOR_F bevelLight{};
	D2D1_COLOR_F bevelDark{};
	float size = 10;
	float width = 0;
	float height = 0;
	float iconGap = 6;
	float titleTextGap = 5;
	std::optional<float> iconX;
	std::optional<float> iconY;
	float clipW = 0;
	float clipH = 0;
	float padding[4] = {8, 6, 8, 6};
	float borderWidth = 1;
	float radius = 6;
	float gradientAngle = 0;
	float offsetX = 0;
	float offsetY = 20;
	float tailGap = -1; // Unset: retain OffsetY's existing scaled gap.
	std::optional<TextCoordinate> titleX;
	std::optional<TextCoordinate> titleY;
	std::optional<float> closeButtonX;
	std::optional<float> closeButtonY;
	std::optional<TextCoordinate> textX;
	std::optional<TextCoordinate> textY;
	float titleFontSize = 10;
	std::optional<BackgroundGradient> linearGradient;
	std::wstring bevelProfile;
	float bevelWidth = 1;
	float bevelRelief = 1;
	float bevelSmoothness = 0;
	float bevelAngle = 225;
	float bevelHighlight = 0.25f;
	float bevelGloss = 24;
	float bevelShadow = 1;
	int bevelRings = 1;
	std::wstring bevelPlacement;
	int weight = 400;
	int clip = 2;
	int initialDelay = 0;
	int reshowDelay = 100;
	int autoPopDelay = 0;
	bool balloon = false;
	bool close = false;
	bool always = true;
	bool follow = true;
	bool keepOnScreen = true;
	bool antiAlias = true;
	bool noFade = false;
	bool dismissClick = true;
	bool enabled = true;
	bool backgroundGradient = false;
	bool topmost = true;
	std::vector<Gfx::TextInlineOption> inlines;
	BackgroundImageOptions image;
	std::wstring Read(const wchar_t* key, const wchar_t* fallback = L"") const;
	double Number(const wchar_t* key, double fallback) const;
	void Load(void* context);
	std::wstring ResolveText() const;
	void Configure(Gfx::TextFormat& format, Gfx::FontCollection* fonts,
		const std::wstring& resolved, bool heading = false) const;

private:
	void* m_Rm = nullptr;
	bool m_ReferencesReady = false;
	std::wstring m_TitleReference;
	std::wstring m_TextReference;
	std::wstring ExpandReferences(const std::wstring& value, bool pattern = false) const;
	std::wstring ReadResolvedText() const;
	std::wstring ApplyStringCase(std::wstring value) const;
	void ReadTextOptions();
	void ReadSizeOptions();
	void ReadAppearanceOptions();
	void ReadImageOptions();
	void ReadPlacementOptions();
	void ReadBehaviorOptions();
	void ReadInlineOptions();
};
} // namespace ToolTip

// Copyright (c) 2026 Jeffrey Morley. Source code licensed under GNU GPL v2 (see LICENSE file).

#pragma once

#include "ToolTipOptions.h"
#include "CursorAttachment.h"

// Each Rainmeter measure owns its window and drawing context. Only the graphics
// device and font factory are shared; skin windows are never subclassed.
class ToolTipMeasure
{
public:
	explicit ToolTipMeasure(void* context);
	~ToolTipMeasure();

	ToolTipMeasure(const ToolTipMeasure& other) = delete;
	ToolTipMeasure& operator=(const ToolTipMeasure& other) = delete;

	void Reload(void* context);
	double Update();
	LPCWSTR GetString() const { return m_Text.c_str(); }
	void Command(LPCWSTR args);

private:
	static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
	static void KeepAboveSkin(HWND window, HWND skin);

	void DestroyIconResource();
	float ReadScale() const;
	POINT GetSkinAnchor(float effectiveScale) const;
	void ReadOptions();
	void Hide();
	HICON LoadSvgIcon(const std::wstring& source);
	void ReadIcon();
	bool InitializeGraphics();
	void LoadFonts();
	D2D1_RECT_F GetTextRect(float top) const;
	float GetTitleY(float top) const;
	D2D1_RECT_F GetIconRect(float top) const;
	void SelectTail(bool top);
	void UpdateLayout();
	POINT GetWindowPosition(SIZE size);
	POINT GetAnchoredBalloonPosition(SIZE size, POINT below, POINT above);
	bool IsTailAnchored() const
	{
		return m_Options.balloon &&
			((m_FixedAnchor && !m_Options.follow) ||
				(!m_FixedAnchor && m_Options.follow &&
					(m_Options.position == L"auto" || m_Options.position == L"above" ||
						m_Options.position == L"below")));
	}
	void DrawBorder(ID2D1Geometry* outline, const D2D1_RECT_F& body, ID2D1Brush* flat);
	D2D1_POINT_2F GetCloseButtonOrigin() const;
	bool Draw(BYTE opacity = 255);
	void Show(bool fixed, POINT point, D2D1_POINT_2F relative = {});
	void Display();
	void Tick();
	void UpdateOpen();
	bool HitTestCloseButton(LPARAM point) const;

	static constexpr WCHAR c_WindowClass[] = L"Rainmeter.PluginToolTip.Host.1";
	static constexpr UINT_PTR c_TimerId = 1;
	static HINSTANCE c_Module;
	static unsigned c_InstanceCount;

	void* m_Rm = nullptr;
	HWND m_SkinWindow = nullptr;
	HWND m_Window = nullptr;
	HICON m_Icon = nullptr;
	float m_IconWidth = 24;
	float m_IconHeight = 24;
	float m_IconScale = 1;
	float m_IconVerticalCenter = 0.5f;
	float m_IconAlignmentPadding = 0;
	double m_RequestedIconWidth = 0;
	double m_RequestedIconHeight = 0;
	bool m_IconOwned = false;
	bool m_Requested = false;
	bool m_Visible = false;
	bool m_FixedAnchor = false;
	bool m_TailTop = true;
	bool m_ComOwned = false;
	bool m_LastLeft = false;
	bool m_LastRight = false;
	ULONGLONG m_Due = 0;
	ULONGLONG m_ShownAt = 0;
	ULONGLONG m_HiddenAt = 0;
	POINT m_Anchor{};
	POINT m_Origin{};
	POINT m_Attachment{};
	D2D1_POINT_2F m_SkinAnchor{};
	HCURSOR m_MeasuredCursor = nullptr;
	ToolTip::CursorEdges m_CursorEdges;
	SIZE m_PixelSize{};
	float m_Scale = 1;
	float m_BodyWidth = 1;
	float m_BodyHeight = 1;
	float m_TopTailBodyHeight = 1;
	float m_BottomTailBodyHeight = 1;
	float m_HeaderHeight = 0;
	float m_TitleAlignmentOffset = 0;
	float m_TextAlignmentOffset = 0;
	float m_TailHeight = 0;
	D2D1_SIZE_F m_TitleSize{};
	D2D1_SIZE_F m_TextLayoutSize{};
	std::wstring m_Text;
	ToolTip::Options m_Options;
	Microsoft::WRL::ComPtr<ID2D1DeviceContext> m_Target;
	Microsoft::WRL::ComPtr<ID2D1Bitmap1> m_Bitmap;
	Microsoft::WRL::ComPtr<ID2D1Bitmap1> m_Staging;
	std::unique_ptr<Gfx::FontCollection> m_Fonts;
	std::unique_ptr<Gfx::TextFormat> m_TextFormat;
	std::unique_ptr<Gfx::TextFormat> m_TitleFormat;
};

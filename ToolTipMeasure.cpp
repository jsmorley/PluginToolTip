// Copyright (c) 2026 Jeffrey Morley. Source code licensed under GNU GPL v2 (see LICENSE file).

#include <sstream>
#include "ToolTipMeasure.h"
#include "ToolTipGraphics.h"

HINSTANCE ToolTipMeasure::c_Module = nullptr;
unsigned ToolTipMeasure::c_InstanceCount = 0;

ToolTipMeasure::ToolTipMeasure(void* context)
	: m_Rm(context), m_SkinWindow(RmGetSkinWindow(context))
{
	const auto hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
	m_ComOwned = SUCCEEDED(hr);
	if (!c_Module)
	{
		GetModuleHandleExW(
			GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
			reinterpret_cast<LPCWSTR>(&WindowProc), &c_Module);
	}
	if (c_InstanceCount++ == 0)
	{
		WNDCLASSW wc{};
		wc.hInstance = c_Module;
		wc.lpszClassName = c_WindowClass;
		wc.lpfnWndProc = WindowProc;
		wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
		RegisterClassW(&wc);
	}
	m_Window = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_LAYERED, c_WindowClass,
		L"ToolTip", WS_POPUP, 0, 0, 1, 1, m_SkinWindow, nullptr, c_Module, this);
	if (!m_Window)
	{
		RmLog(m_Rm, LOG_ERROR, L"ToolTip: could not create tooltip window");
	}
	if (!Gfx::Canvas::Initialize())
	{
		RmLog(m_Rm, LOG_ERROR, L"ToolTip: DirectWrite initialization failed");
	}
	ReadOptions();
}

ToolTipMeasure::~ToolTipMeasure()
{
	Hide();
	if (m_Window)
	{
		DestroyWindow(m_Window);
	}
	if (--c_InstanceCount == 0)
	{
		ToolTip::GetGraphicsResources().Reset();
		Gfx::Canvas::Finalize();
		UnregisterClassW(c_WindowClass, c_Module);
	}
	if (m_ComOwned)
	{
		CoUninitialize();
	}
}

void ToolTipMeasure::ReadOptions()
{
	m_RowInk.reset();
	m_Options.Load(m_Rm);
	m_Scale = ReadScale();
}

void ToolTipMeasure::Hide()
{
	if (m_Requested || m_Visible)
	{
		m_HiddenAt = GetTickCount64();
	}
	m_Requested = m_Visible = false;
	if (m_Window)
	{
		KillTimer(m_Window, c_TimerId);
		ShowWindow(m_Window, SW_HIDE);
	}
	DestroyIconResource();
	m_TextFormat.reset();
	m_TitleFormat.reset();
	m_Fonts.reset();
	if (m_Target)
	{
		m_Target->SetTarget(nullptr);
	}
	m_Bitmap.Reset();
	m_Staging.Reset();
	m_BackgroundImage.Reset();
	m_BevelBitmap.Reset();
	m_BevelKey.clear();
	m_LoadedBackgroundImage.reset();
	m_OutlineBitmap.Reset();
	m_OutlineIntermediate.Reset();
	m_Target.Reset();
	// Drop unused Direct2D caches, while retaining one device to avoid
	// initializing another driver device every time a card is hovered.
	if (ToolTip::GetGraphicsResources().device)
	{
		ToolTip::GetGraphicsResources().device->ClearResources();
	}
}

bool ToolTipMeasure::IsSkinSelected() const
{
	// Rainmeter creates this child when selecting the skin and destroys it
	// on deselection. Checking the child avoids relying on modifier-key state.
	return FindWindowExW(m_SkinWindow, nullptr, L"RainmeterSkinSelectionOverlay", nullptr) !=
		nullptr;
}

void ToolTipMeasure::Show(bool fixed, POINT point, D2D1_POINT_2F relative)
{
	Hide();
	ReadOptions();
	if (!m_Window || !m_Options.enabled || !IsWindowVisible(m_SkinWindow) || IsSkinSelected())
	{
		return;
	}
	m_FixedAnchor = fixed;
	m_SkinAnchor = relative;
	m_Anchor = fixed ? GetSkinAnchor(m_Scale) : point;
	m_Requested = true;
	const auto now = GetTickCount64();
	const bool reshow = m_HiddenAt && now - m_HiddenAt < 1000;
	m_Due = now + (reshow ? m_Options.reshowDelay : m_Options.initialDelay);
	m_LastLeft = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
	m_LastRight = (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
	if (!SetTimer(m_Window, c_TimerId, 16, nullptr))
	{
		Hide();
		RmLog(m_Rm, LOG_ERROR, L"ToolTip: timer creation failed");
		return;
	}
	Tick();
}

void ToolTipMeasure::Display()
{
	// Resolve option references together at display time, including after an
	// InitialDelay during which the skin's measures may have changed.
	ReadOptions();
	m_Text = m_Options.ResolveText();
	ReadIcon();
	if (m_Text.empty() && m_Options.title.empty())
	{
		Hide();
		return;
	}
	LoadFonts();
	const bool ok = Draw(m_Options.noFade ? 255 : 0);
	if (!ok)
	{
		Hide();
		RmLog(m_Rm, LOG_ERROR, L"ToolTip: display failed");
		return;
	}
	m_Visible = true;
	m_ShownAt = GetTickCount64();
}

void ToolTipMeasure::Tick()
{
	if (!m_Requested)
	{
		return;
	}
	// Also cover hosts that do not send a scale-change notification, including
	// a pending InitialDelay that must not appear after zooming.
	if (ReadScale() != m_Scale || IsSkinSelected())
	{
		Hide();
		return;
	}
	if (!IsWindow(m_SkinWindow) || !IsWindowVisible(m_SkinWindow) || IsIconic(m_SkinWindow))
	{
		Hide();
		return;
	}
	// Query Windows' actual menu state rather than treating every right click
	// as a context menu. This also covers keyboard/bang-opened skin menus,
	// without subclassing the host's unload-sensitive window procedure.
	GUITHREADINFO gui{sizeof(gui)};
	if (GetGUIThreadInfo(GetWindowThreadProcessId(m_SkinWindow, nullptr), &gui) &&
		(gui.flags & (GUI_INMENUMODE | GUI_POPUPMENUMODE | GUI_SYSTEMMENUMODE)) &&
		gui.hwndMenuOwner == m_SkinWindow)
	{
		Hide();
		return;
	}
	if (!m_Options.always &&
		GetAncestor(GetForegroundWindow(), GA_ROOT) != GetAncestor(m_SkinWindow, GA_ROOT))
	{
		Hide();
		return;
	}
	const bool left = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
	const bool right = (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
	if ((GetAsyncKeyState(VK_ESCAPE) & 0x8000) ||
		(m_Options.dismissClick && ((left && !m_LastLeft) || (right && !m_LastRight))))
	{
		Hide();
		return;
	}
	m_LastLeft = left;
	m_LastRight = right;
	if (!m_Visible && GetTickCount64() >= m_Due)
	{
		Display();
	}
	if (!m_Visible)
	{
		return;
	}
	// Display records m_ShownAt after rendering. Sample time afterward so
	// subtracting the start time cannot underflow on the opening timer tick.
	const auto now = GetTickCount64();
	KeepAboveSkin(m_Window, m_SkinWindow);
	if (m_Options.autoPopDelay && now - m_ShownAt >= static_cast<ULONGLONG>(m_Options.autoPopDelay))
	{
		Hide();
		return;
	}
	const float currentScale = ReadScale();
	const POINT currentAnchor = m_FixedAnchor ? GetSkinAnchor(currentScale) : m_Anchor;
	if (currentScale != m_Scale || currentAnchor.x != m_Anchor.x || currentAnchor.y != m_Anchor.y)
	{
		Draw();
	}
	if (m_Options.follow && !m_FixedAnchor)
	{
		POINT p{};
		GetCursorPos(&p);
		if (p.x != m_Anchor.x || p.y != m_Anchor.y)
		{
			m_Anchor = p;
			Draw();
		}
	}
	if (!m_Options.noFade && now - m_ShownAt < 160)
	{
		Draw(static_cast<BYTE>(std::min<ULONGLONG>(255, (now - m_ShownAt) * 255 / 144)));
	}
}

void ToolTipMeasure::UpdateOpen()
{
	if (!m_Visible)
	{
		return;
	}
	if (ReadScale() != m_Scale || IsSkinSelected())
	{
		Hide();
		return;
	}
	const auto oldIcon = m_Options.icon;
	ReadOptions();
	const bool iconChanged = oldIcon != m_Options.icon || m_IconScale != m_Scale ||
		m_RequestedIconWidth != m_Options.Number(L"IconW", 0) ||
		m_RequestedIconHeight != m_Options.Number(L"IconH", 0);
	if (!m_Options.enabled)
	{
		Hide();
		return;
	}
	auto updated = m_Options.ResolveText();
	if (updated.empty() && m_Options.title.empty())
	{
		Hide();
		return;
	}
	m_Text = std::move(updated);
	if (iconChanged)
	{
		ReadIcon();
	}
	if (!Draw())
	{
		Hide();
		RmLog(m_Rm, LOG_ERROR, L"ToolTip: redraw failed");
	}
}

void ToolTipMeasure::KeepAboveSkin(HWND window, HWND skin)
{
	if (!IsWindow(window) || !IsWindow(skin) || !IsWindowVisible(window))
	{
		return;
	}
	// Check actual order, not just WS_EX_TOPMOST: both windows can be topmost
	// while the Stay Topmost skin has moved ahead of its tooltip.
	bool skinAbove = false;
	for (HWND previous = GetWindow(window, GW_HWNDPREV); previous;
		previous = GetWindow(previous, GW_HWNDPREV))
	{
		if (previous == skin)
		{
			skinAbove = true;
			break;
		}
	}
	if (!skinAbove)
	{
		return;
	}
	HWND insertAfter = GetWindow(skin, GW_HWNDPREV);
	// Move only immediately above our own skin; do not compete for the first
	// position in the topmost group or move the owner itself.
	if (!insertAfter)
	{
		insertAfter =
			(GetWindowLongPtrW(skin, GWL_EXSTYLE) & WS_EX_TOPMOST) ? HWND_TOPMOST : HWND_TOP;
	}
	SetWindowPos(window, insertAfter, 0, 0, 0, 0,
		SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
}

// Never subclass the skin: Rainmeter may unload the DLL from its window procedure.
LRESULT CALLBACK ToolTipMeasure::WindowProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
	auto measure = reinterpret_cast<ToolTipMeasure*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
	if (msg == WM_NCCREATE)
	{
		measure = static_cast<ToolTipMeasure*>(reinterpret_cast<CREATESTRUCT*>(lp)->lpCreateParams);
		SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(measure));
	}
	if (measure)
	{
		if (msg == WM_WINDOWPOSCHANGED)
		{
			const HWND skin = measure->m_SkinWindow;
			const LRESULT result = DefWindowProc(hwnd, msg, wp, lp);
			KeepAboveSkin(hwnd, skin);
			return result;
		}
		if (msg == WM_TIMER && wp == c_TimerId)
		{
			measure->Tick();
			return 0;
		}
		if (msg == WM_MOUSEACTIVATE)
		{
			return MA_NOACTIVATE;
		}
		if (msg == WM_NCHITTEST)
		{
			return measure->HitTestCloseButton(lp) ? HTCLIENT : HTTRANSPARENT;
		}
		if (msg == WM_LBUTTONUP)
		{
			measure->Hide();
			return 0;
		}
		if (msg == WM_ERASEBKGND)
		{
			return 1;
		}
	}
	return DefWindowProc(hwnd, msg, wp, lp);
}

void ToolTipMeasure::Command(LPCWSTR args)
{
	std::wistringstream input(args ? args : L"");
	std::wstring command;
	input >> command;
	command = ToolTip::Lower(command);
	if (command == L"hide" || command == L"dismiss")
	{
		Hide();
	}
	else if (command == L"show" || command == L"toggle")
	{
		if (command == L"toggle" && m_Requested)
		{
			Hide();
			return;
		}
		POINT p{};
		GetCursorPos(&p);
		input >> std::ws;
		bool fixed = !input.eof();
		D2D1_POINT_2F relative{};
		if (fixed)
		{
			double x = 0, y = 0;
			input >> x >> std::ws;
			if (input.peek() == L',')
			{
				input.get();
			}
			if (!(input >> y) || !std::isfinite(x) || !std::isfinite(y) || std::abs(x) > 100000 ||
				std::abs(y) > 100000)
			{
				RmLog(m_Rm, LOG_WARNING,
					L"ToolTip: Show expects zero arguments or two skin-relative coordinates");
				return;
			}
			relative = D2D1::Point2F(static_cast<float>(x), static_cast<float>(y));
		}
		Show(fixed, p, relative);
	}
	else if (command == L"update")
	{
		UpdateOpen();
	}
	else
	{
		RmLog(m_Rm, LOG_WARNING, L"ToolTip: unknown command (Show, Hide, Toggle, Update)");
	}
}

void ToolTipMeasure::Reload(void* context)
{
	m_Rm = context;
	if (m_Visible)
	{
		UpdateOpen();
	}
	else
	{
		ReadOptions();
	}
}

double ToolTipMeasure::Update()
{
	UpdateOpen();
	return m_Visible ? 1.0 : 0.0;
}

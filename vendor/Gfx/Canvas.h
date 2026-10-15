// ToolTip adapter for Rainmeter's GPL v2 TextFormat. No Rainmeter internals required.
#pragma once
#include "TextFormat.h"
#include "FontCollection.h"
namespace Gfx {
class Canvas
{
public:
	static Microsoft::WRL::ComPtr<IDWriteFactory1> c_DWFactory;
	static bool Initialize();
	static void Finalize();
	static std::unique_ptr<FontCollection> Fonts();
	static D2D1_SIZE_F Measure(
		const std::wstring& text, TextFormat& format, float width, bool accurate);
	static D2D1_RECT_F MeasureInk(
		const std::wstring& text, TextFormat& format, float width = 8192, float height = 10000);
	static void Draw(ID2D1DeviceContext* target, const std::wstring& text, TextFormat& format,
		D2D1_RECT_F rect, D2D1_COLOR_F color, bool inlines, bool accurate);
};
} // namespace Gfx

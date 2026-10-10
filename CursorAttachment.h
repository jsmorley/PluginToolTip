#pragma once
#include "vendor/StdAfx.h"

namespace ToolTip {
struct CursorEdges { POINT top{}, bottom{}; };

// Offsets from the cursor hotspot to its visible top/bottom edges. Rendering
// against two backgrounds handles alpha, masked and monochrome cursors without
// counting the transparent padding in their bitmaps as part of the pointer.
inline CursorEdges GetCursorEdges(HCURSOR cursor) {
    CursorEdges edges;
    ICONINFO icon{};
    if (!cursor || !GetIconInfo(cursor, &icon)) return edges;
    BITMAP bitmap{};
    GetObject(icon.hbmColor ? icon.hbmColor : icon.hbmMask, sizeof(bitmap), &bitmap);
    LONG width = bitmap.bmWidth, height = icon.hbmColor ? bitmap.bmHeight : bitmap.bmHeight / 2;
    if (icon.hbmColor) DeleteObject(icon.hbmColor);
    if (icon.hbmMask) DeleteObject(icon.hbmMask);
    if (width <= 0 || height <= 0 || width > 1024 || height > 1024) return edges;
    BITMAPINFO info{}; info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width; info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32; info.bmiHeader.biCompression = BI_RGB;
    void* pixels = nullptr;
    HDC dc = CreateCompatibleDC(nullptr);
    HBITMAP image = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
    if (dc && image) {
        auto previous = SelectObject(dc, image);
        auto bits = static_cast<DWORD*>(pixels);
        std::vector<bool> visible(static_cast<size_t>(width) * height, false);
        for (DWORD background : {0x00000000u, 0x00FFFFFFu}) {
            std::fill(bits, bits + visible.size(), background);
            if (DrawIconEx(dc, 0, 0, cursor, width, height, 0, nullptr, DI_NORMAL | DI_NOMIRROR)) {
                GdiFlush();
                for (size_t i = 0; i < visible.size(); ++i)
                    visible[i] = visible[i] || (bits[i] & 0x00FFFFFF) != background;
            }
        }
        bool found = false;
        for (LONG y = 0; y < height; ++y) {
            LONG left = width, right = -1;
            for (LONG x = 0; x < width; ++x) if (visible[static_cast<size_t>(y) * width + x]) {
                left = std::min(left, x); right = x;
            }
            if (right >= 0) {
                LONG x = (left + right) / 2 - static_cast<LONG>(icon.xHotspot);
                if (!found) edges.top = {x, y - static_cast<LONG>(icon.yHotspot)};
                edges.bottom = {x, y + 1 - static_cast<LONG>(icon.yHotspot)};
                found = true;
            }
        }
        SelectObject(dc, previous);
    }
    if (image) DeleteObject(image);
    if (dc) DeleteDC(dc);
    return edges;
}
}

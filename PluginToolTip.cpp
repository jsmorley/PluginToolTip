// ToolTip Rainmeter plugin. GPL v2; see LICENSE and vendor/README.md.
#include "ToolTipOptions.h"
#include "CursorAttachment.h"
#include <d3d11.h>
#include <dxgi.h>
#include <wincodec.h>
#include <d2d1_3.h>
#include <d2d1svg.h>
#include <filesystem>
#include <fstream>
#include <sstream>

using Microsoft::WRL::ComPtr;
namespace {
constexpr wchar_t WindowClass[] = L"Rainmeter.PluginToolTip.Host.1";
constexpr UINT_PTR TimerId = 1;
HINSTANCE Module = nullptr;
unsigned Instances = 0;
// Rainmeter invokes these measures on its UI thread. Keep one resource domain
// for the plugin; independent measures only need their own drawing contexts.
struct GraphicsResources {
    ComPtr<ID3D11Device> d3d;
    ComPtr<ID2D1Factory1> factory;
    ComPtr<ID2D1Device> device;
    ComPtr<IWICImagingFactory> wic;
    void Reset() { wic.Reset(); device.Reset(); factory.Reset(); d3d.Reset(); }
} GraphicsPool;
struct Measure;
LRESULT CALLBACK WindowProc(HWND, UINT, WPARAM, LPARAM);

struct Measure {
    void* rm = nullptr;
    HWND skin = nullptr, window = nullptr;
    HICON icon = nullptr;
    float iconW = 24, iconH = 24, iconScale = 1;
    bool iconOwned = false, requested = false, visible = false, fixedAnchor = false, tailTop = true;
    bool comOwned = false, lastLeft = false, lastRight = false;
    ULONGLONG due = 0, shownAt = 0, hiddenAt = 0;
    POINT anchor{}, origin{}, attachment{};
    HCURSOR measuredCursor = nullptr;
    ToolTip::CursorEdges cursorEdges;
    SIZE pixels{};
    float scale = 1, bodyW = 1, bodyH = 1, headerH = 0, tail = 0;
    std::wstring text;
    ToolTip::Options options;
    ComPtr<ID2D1DeviceContext> target;
    ComPtr<ID2D1Bitmap1> bitmap, staging;
    std::unique_ptr<Gfx::FontCollection> fonts;
    std::unique_ptr<Gfx::TextFormat> format, titleFormat;

    explicit Measure(void* context) : rm(context), skin(RmGetSkinWindow(context)) {
        const auto hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        comOwned = SUCCEEDED(hr);
        if (!Module) GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&WindowProc), &Module);
        if (Instances++ == 0) {
            WNDCLASSW wc{}; wc.hInstance = Module; wc.lpszClassName = WindowClass; wc.lpfnWndProc = WindowProc;
            wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
            RegisterClassW(&wc);
        }
        window = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_LAYERED,
            WindowClass, L"ToolTip", WS_POPUP, 0, 0, 1, 1, skin, nullptr, Module, this);
        if (!window) RmLog(rm, LOG_ERROR, L"ToolTip: could not create tooltip window");
        if (!Gfx::Canvas::Initialize()) RmLog(rm, LOG_ERROR, L"ToolTip: DirectWrite initialization failed");
        Load();
    }
    ~Measure() {
        Hide();
        if (window) DestroyWindow(window);
        if (--Instances == 0) {
            GraphicsPool.Reset();
            Gfx::Canvas::Finalize();
            UnregisterClassW(WindowClass, Module);
        }
        if (comOwned) CoUninitialize();
    }
    void DestroyIconResource() {
        if (iconOwned && icon) DestroyIcon(icon);
        icon = nullptr; iconOwned = false;
    }
    void Load() {
        options.Load(rm);
        scale = std::clamp(RmGetSkinScale(rm), 0.25f, 8.0f);
    }
    void Hide() {
        if (requested || visible) hiddenAt = GetTickCount64();
        requested = visible = false;
        if (window) { KillTimer(window, TimerId); ShowWindow(window, SW_HIDE); }
        DestroyIconResource();
        format.reset(); titleFormat.reset(); fonts.reset();
        if (target) target->SetTarget(nullptr);
        bitmap.Reset(); staging.Reset(); target.Reset();
        // Drop unused Direct2D caches, while retaining one device to avoid
        // initializing another driver device every time a card is hovered.
        if (GraphicsPool.device) GraphicsPool.device->ClearResources();
    }
    HICON SvgIcon(const std::wstring& source) {
        // Rasterize at the skin's scale once per icon change, preserving alpha
        // and rectangular SVG viewports.
        std::string xml;
        if (!source.empty() && source.front() == L'<') {
            int count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, source.data(),
                static_cast<int>(source.size()), nullptr, 0, nullptr, nullptr);
            if (!count) return nullptr;
            xml.resize(count);
            WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, source.data(),
                static_cast<int>(source.size()), xml.data(), count, nullptr, nullptr);
        } else {
            std::wstring path = RmPathToAbsolute(rm, source.c_str());
            std::ifstream file(std::filesystem::path(path), std::ios::binary | std::ios::ate);
            if (!file || file.tellg() <= 0 || file.tellg() > 16 * 1024 * 1024) return nullptr;
            xml.resize(static_cast<size_t>(file.tellg())); file.seekg(0);
            if (!file.read(xml.data(), static_cast<std::streamsize>(xml.size()))) return nullptr;
        }
        if (xml.size() > 16 * 1024 * 1024 || !Graphics()) return nullptr;
        ComPtr<ID2D1DeviceContext5> svgContext;
        ComPtr<IStream> stream;
        if (FAILED(target.As(&svgContext)) || FAILED(CreateStreamOnHGlobal(nullptr, TRUE, stream.GetAddressOf()))) return nullptr;
        ULONG written = 0;
        if (FAILED(stream->Write(xml.data(), static_cast<ULONG>(xml.size()), &written)) || written != xml.size()) return nullptr;
        LARGE_INTEGER zero{};
        if (FAILED(stream->Seek(zero, STREAM_SEEK_SET, nullptr))) return nullptr;
        UINT width = static_cast<UINT>(std::ceil(iconW * scale)), height = static_cast<UINT>(std::ceil(iconH * scale));
        ComPtr<ID2D1SvgDocument> document;
        if (FAILED(svgContext->CreateSvgDocument(stream.Get(), D2D1::SizeF(static_cast<float>(width), static_cast<float>(height)), document.GetAddressOf()))) return nullptr;
        ComPtr<ID2D1Bitmap1> image, readback;
        auto props = D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
        if (FAILED(target->CreateBitmap(D2D1::SizeU(width, height), nullptr, 0, &props, image.GetAddressOf()))) return nullptr;
        target->SetTarget(image.Get()); target->SetDpi(96, 96);
        target->SetTransform(D2D1::Matrix3x2F::Identity());
        target->BeginDraw(); target->Clear(D2D1::ColorF(0, 0));
        svgContext->DrawSvgDocument(document.Get());
        auto hr = target->EndDraw(); target->SetTarget(nullptr);
        if (FAILED(hr)) return nullptr;
        props.bitmapOptions = D2D1_BITMAP_OPTIONS_CPU_READ | D2D1_BITMAP_OPTIONS_CANNOT_DRAW;
        if (FAILED(target->CreateBitmap(D2D1::SizeU(width, height), nullptr, 0, &props, readback.GetAddressOf())) ||
            FAILED(readback->CopyFromBitmap(nullptr, image.Get(), nullptr))) return nullptr;
        D2D1_MAPPED_RECT mapped{};
        if (FAILED(readback->Map(D2D1_MAP_OPTIONS_READ, &mapped))) return nullptr;
        BITMAPINFO info{}; info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = width; info.bmiHeader.biHeight = -static_cast<LONG>(height);
        info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32; info.bmiHeader.biCompression = BI_RGB;
        void* bits = nullptr;
        HBITMAP color = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
        std::vector<BYTE> maskBits(((width + 15) / 16) * 2 * height, 0);
        HBITMAP mask = CreateBitmap(width, height, 1, 1, maskBits.data());
        HICON result = nullptr;
        if (color && mask) {
            for (UINT row = 0; row < height; ++row) memcpy(static_cast<BYTE*>(bits) + row * width * 4, mapped.bits + row * mapped.pitch, width * 4);
            ICONINFO ii{}; ii.fIcon = TRUE; ii.hbmColor = color; ii.hbmMask = mask;
            result = CreateIconIndirect(&ii);
        }
        if (color) DeleteObject(color);
        if (mask) DeleteObject(mask);
        readback->Unmap();
        return result;
    }
    void ReadIcon() {
        if (iconOwned && icon) DestroyIcon(icon);
        icon = nullptr; iconOwned = false;
        iconW = static_cast<float>(std::clamp(options.Number(L"IconW", 24), 1.0, 512.0));
        iconH = static_cast<float>(std::clamp(options.Number(L"IconH", 24), 1.0, 512.0));
        iconScale = scale;
        const auto source = ToolTip::Trim(options.icon);
        const auto name = ToolTip::Lower(source);
        LPCWSTR id = name == L"info" || name == L"information" ? IDI_INFORMATION : name == L"warning" ? IDI_WARNING :
            name == L"error" ? IDI_ERROR : name == L"question" ? IDI_QUESTION : nullptr;
        if (id) icon = ::LoadIconW(nullptr, id);
        else if (!name.empty() && name != L"none" && name != L"0") {
            const bool svg = source.front() == L'<' || ToolTip::Lower(std::filesystem::path(source).extension().wstring()) == L".svg";
            if (svg) {
                icon = SvgIcon(source); iconOwned = icon != nullptr;
                if (!icon) RmLog(rm, LOG_WARNING, L"ToolTip: SVG icon could not be loaded (check SVG syntax/file and Direct2D SVG support)");
                return;
            }
            std::wstring path = RmPathToAbsolute(rm, options.icon.c_str());
            icon = static_cast<HICON>(LoadImageW(nullptr, path.c_str(), IMAGE_ICON, 32, 32, LR_LOADFROMFILE));
            iconOwned = icon != nullptr;
            if (!icon) RmLog(rm, LOG_WARNING, L"ToolTip: Icon file could not be loaded (use an .ico or .svg file, or inline SVG)");
        }
    }
    bool Graphics() {
        if (target) return true;
        if (GraphicsPool.device)
            return SUCCEEDED(GraphicsPool.device->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, target.GetAddressOf()));
        GraphicsResources resources;
        UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
        auto hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, nullptr, 0,
            D3D11_SDK_VERSION, resources.d3d.GetAddressOf(), nullptr, nullptr);
        if (FAILED(hr)) hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags, nullptr, 0,
            D3D11_SDK_VERSION, resources.d3d.GetAddressOf(), nullptr, nullptr);
        ComPtr<IDXGIDevice> dxgi;
        if (FAILED(hr) || FAILED(resources.d3d.As(&dxgi))) return false;
        if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, resources.factory.GetAddressOf()))) return false;
        if (FAILED(resources.factory->CreateDevice(dxgi.Get(), resources.device.GetAddressOf()))) return false;
        if (FAILED(resources.device->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, target.GetAddressOf()))) return false;
        CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(resources.wic.GetAddressOf()));
        GraphicsPool = std::move(resources);
        return true;
    }
    void LoadFonts() {
        format.reset(); titleFormat.reset(); fonts = Gfx::Canvas::Fonts();
        // Match runtime font support without referencing private Rainmeter objects.
        std::wstring resources = RmReplaceVariables(rm, L"#@#Fonts\\");
        std::error_code ec;
        if (std::filesystem::is_directory(resources, ec)) {
            for (std::filesystem::directory_iterator it(resources, ec), end; !ec && it != end; it.increment(ec)) {
                const auto& entry = *it;
                auto ext = ToolTip::Lower(entry.path().extension().wstring());
                if (ext == L".ttf" || ext == L".otf" || ext == L".ttc") fonts->AddFile(entry.path().c_str());
            }
        }
    }
    void Layout() {
        format = std::make_unique<Gfx::TextFormat>();
        titleFormat = std::make_unique<Gfx::TextFormat>();
        options.Configure(*format, fonts.get(), text);
        options.Configure(*titleFormat, fonts.get(), options.title, true);
        auto maxW = options.width > 0 ? options.width : options.clip == 2 && options.clipW > 0 ? options.clipW : 8192;
        auto size = Gfx::Canvas::Measure(text, *format, maxW, true);
        float w = options.width > 0 ? options.width : std::min(size.width + 1, maxW);
        float h = options.height > 0 ? options.height : size.height + 1;
        if (options.clip == 2 && options.height == 0 && options.clipH > 0) h = std::min(h, options.clipH);
        auto titleSize = Gfx::Canvas::Measure(options.title, *titleFormat, 8192, true);
        headerH = options.title.empty() && !icon && !options.close ? 0 : std::max(titleSize.height + 5, icon ? iconH + 5 : 0.0f);
        float headerW = titleSize.width + (icon ? iconW + 6 : 0) + (options.close ? 24.0f : 0);
        bodyW = std::clamp(std::max(w, headerW) + options.padding[0] + options.padding[2] + 2 * options.borderWidth, 1.0f, 8192.0f / scale);
        bodyH = std::clamp(h + headerH + options.padding[1] + options.padding[3] + 2 * options.borderWidth, 1.0f, 8192.0f / scale);
        tail = options.balloon ? 12.0f : 0;
        pixels = {static_cast<LONG>(std::ceil(bodyW * scale)), static_cast<LONG>(std::ceil((bodyH + tail) * scale))};
    }
    POINT Placement(SIZE size) {
        const bool verticalBalloon = options.balloon &&
            (options.position == L"auto" || options.position == L"above" || options.position == L"below");
        const auto xOffset = static_cast<LONG>(options.offsetX * scale);
        const auto yOffset = verticalBalloon && options.tailGap >= 0 ? static_cast<LONG>(std::round(options.tailGap)) :
            static_cast<LONG>(options.offsetY * scale);
        POINT below = anchor, above = anchor;
        if (options.balloon && !fixedAnchor) {
            CURSORINFO cursor{sizeof(cursor)};
            HCURSOR current = GetCursorInfo(&cursor) && (cursor.flags & CURSOR_SHOWING) ? cursor.hCursor : nullptr;
            if (current != measuredCursor) {
                measuredCursor = current; cursorEdges = ToolTip::GetCursorEdges(current);
            }
            const auto& bounds = cursorEdges.bounds;
            const LONG cursorX = bounds.right > bounds.left ?
                (options.tailPosition == L"left" ? bounds.left - 1 : options.tailPosition == L"right" ? bounds.right :
                    (bounds.left + bounds.right - 1) / 2) : 0;
            below.x += cursorX; below.y += bounds.bottom;
            above.x += cursorX; above.y += bounds.top;
        }
        attachment = below;
        POINT p{below.x + xOffset, below.y + yOffset};
        if (options.position == L"above") { attachment = above; p = {above.x + xOffset, above.y - size.cy - std::abs(yOffset)}; }
        if (options.position == L"left") { p.x = anchor.x - size.cx - std::abs(xOffset); p.y = anchor.y; }
        if (options.position == L"right") { p.x = anchor.x + std::abs(xOffset); p.y = anchor.y; }
        if (options.position == L"absolute") p = {fixedAnchor ? anchor.x : xOffset, fixedAnchor ? anchor.y : yOffset};
        MONITORINFO monitor{sizeof(monitor)};
        GetMonitorInfoW(MonitorFromPoint(anchor, MONITOR_DEFAULTTONEAREST), &monitor);
        auto work = monitor.rcWork;
        if (options.position == L"auto" && p.y + size.cy > work.bottom) {
            attachment = above; p = {above.x + xOffset, above.y - size.cy - std::abs(yOffset)};
        }
        if (verticalBalloon) {
            const float inset = options.borderWidth / 2 + 0.5f / scale;
            const float radius = std::min({options.radius, std::max(0.0f, bodyW - 2 * inset) / 2, std::max(0.0f, bodyH - 2 * inset) / 2});
            // Align the balloon's tail with the selected cursor-relative point.
            // Painting still tracks it after screen-edge clamping.
            p.x -= static_cast<LONG>(std::ceil((inset + radius + 9) * scale));
        } else attachment = anchor;
        tailTop = p.y >= anchor.y;
        if (options.keepOnScreen) {
            p.x = std::clamp(p.x, work.left, std::max(work.left, work.right - size.cx));
            p.y = std::clamp(p.y, work.top, std::max(work.top, work.bottom - size.cy));
        }
        return p;
    }
    void DrawBorder(ID2D1Geometry* outline, const D2D1_RECT_F& body, ID2D1Brush* flat) {
        if (options.strokeBevel == L"none" || options.bevelWidth == 0 || options.bevelRelief == 0) {
            target->DrawGeometry(outline, flat, options.borderWidth);
            return;
        }
        auto widen = [&](float width, ComPtr<ID2D1PathGeometry>& path) {
            ComPtr<ID2D1GeometrySink> sink;
            return SUCCEEDED(GraphicsPool.factory->CreatePathGeometry(path.GetAddressOf())) &&
                SUCCEEDED(path->Open(sink.GetAddressOf())) &&
                SUCCEEDED(outline->Widen(width, nullptr, nullptr, 0.1f / scale, sink.Get())) &&
                SUCCEEDED(sink->Close());
        };
        auto band = [&](ID2D1Geometry* outer, ID2D1Geometry* inner, ID2D1Brush* brush) {
            if (!inner) { target->FillGeometry(outer, brush); return; }
            ComPtr<ID2D1PathGeometry> ring;
            ComPtr<ID2D1GeometrySink> sink;
            if (SUCCEEDED(GraphicsPool.factory->CreatePathGeometry(ring.GetAddressOf())) &&
                SUCCEEDED(ring->Open(sink.GetAddressOf())) &&
                SUCCEEDED(outer->CombineWithGeometry(inner, D2D1_COMBINE_MODE_EXCLUDE, nullptr, 0.1f / scale, sink.Get())) &&
                SUCCEEDED(sink->Close())) target->FillGeometry(ring.Get(), brush);
        };
        const float w = std::max(1.0f, body.right - body.left), h = std::max(1.0f, body.bottom - body.top);
        const float radians = options.bevelAngle * 3.14159265f / 180;
        const float dx = -std::cos(radians) / w, dy = -std::sin(radians) / h;
        const float extent = (std::abs(dx) * w + std::abs(dy) * h) / (2 * (dx * dx + dy * dy));
        const auto center = D2D1::Point2F((body.left + body.right) / 2, (body.top + body.bottom) / 2);
        auto brushFor = [&](float strength) {
            auto mix = [&](D2D1_COLOR_F color) {
                const float amount = std::abs(strength) * options.bevelRelief;
                return D2D1::ColorF(options.border.r + (color.r - options.border.r) * amount,
                    options.border.g + (color.g - options.border.g) * amount,
                    options.border.b + (color.b - options.border.b) * amount,
                    options.border.a + (color.a - options.border.a) * amount);
            };
            const bool raised = (options.strokeBevel == L"raised") != (strength < 0);
            const auto light = mix(raised ? options.bevelLight : options.bevelDark);
            const auto dark = mix(raised ? options.bevelDark : options.bevelLight);
            const float fade = options.bevelSmoothness / 2;
            D2D1_GRADIENT_STOP stops[] = {{0, light}, {0.5f - fade, light}, {0.5f + fade, dark}, {1, dark}};
            ComPtr<ID2D1GradientStopCollection> collection;
            ComPtr<ID2D1LinearGradientBrush> brush;
            if (SUCCEEDED(target->CreateGradientStopCollection(stops, 4, collection.GetAddressOf())))
                target->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(
                    D2D1::Point2F(center.x - dx * extent, center.y - dy * extent),
                    D2D1::Point2F(center.x + dx * extent, center.y + dy * extent)), collection.Get(), brush.GetAddressOf());
            return brush;
        };
        // Keep the original single-stroke rendering for the default profile.
        if (options.bevelProfile == L"linear" && options.bevelWidth == options.borderWidth) {
            auto brush = brushFor(1);
            target->DrawGeometry(outline, brush ? static_cast<ID2D1Brush*>(brush.Get()) : flat, options.borderWidth);
            return;
        }
        ComPtr<ID2D1PathGeometry> outer;
        if (!widen(options.bevelWidth, outer)) { target->DrawGeometry(outline, flat, options.borderWidth); return; }
        if (options.bevelWidth < options.borderWidth) {
            ComPtr<ID2D1PathGeometry> full;
            if (widen(options.borderWidth, full)) band(full.Get(), outer.Get(), flat);
        }
        // Disjoint contour bands avoid accumulating alpha over the background.
        // Curved softens toward the center; Concave recesses it; Bulged reverses
        // the slope midway across the band for a ridge with two lit faces.
        const int count = options.bevelProfile == L"linear" ? 1 :
            std::clamp(static_cast<int>(std::ceil(options.bevelWidth * scale * 2)), 2, 32);
        for (int i = 0; i < count; ++i) {
            const float t = (i + 0.5f) / count;
            float strength = 1;
            if (options.bevelProfile == L"curved") strength = std::cos(t * 3.14159265f / 2);
            else if (options.bevelProfile == L"concave") strength = -std::sin(t * 3.14159265f / 2);
            else if (options.bevelProfile == L"bulged") strength = std::cos(t * 3.14159265f);
            auto brush = brushFor(strength);
            ComPtr<ID2D1PathGeometry> inner;
            if (i + 1 < count && !widen(options.bevelWidth * (1 - (i + 1.0f) / count), inner)) break;
            band(outer.Get(), inner.Get(), brush ? static_cast<ID2D1Brush*>(brush.Get()) : flat);
            outer = inner;
        }
    }
    bool Paint(BYTE opacity = 255) {
        if (!Graphics()) return false;
        Layout();
        origin = Placement(pixels);
        auto props = D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96 * scale, 96 * scale);
        auto dimensions = D2D1::SizeU(pixels.cx, pixels.cy);
        if (bitmap) {
            auto current = bitmap->GetPixelSize();
            float dpiX, dpiY; bitmap->GetDpi(&dpiX, &dpiY);
            if (current.width != dimensions.width || current.height != dimensions.height || dpiX != 96 * scale || dpiY != 96 * scale) {
                bitmap.Reset(); staging.Reset();
            }
        }
        if (!bitmap && FAILED(target->CreateBitmap(dimensions, nullptr, 0, &props, bitmap.GetAddressOf()))) return false;
        target->SetTarget(bitmap.Get()); target->SetDpi(96 * scale, 96 * scale);
        target->SetTextAntialiasMode(options.antiAlias ? D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE : D2D1_TEXT_ANTIALIAS_MODE_ALIASED);
        target->BeginDraw(); target->Clear(D2D1::ColorF(0, 0));
        ComPtr<ID2D1SolidColorBrush> fill, stroke;
        target->CreateSolidColorBrush(options.background, fill.GetAddressOf());
        target->CreateSolidColorBrush(options.border, stroke.GetAddressOf());
        if (!fill || !stroke) { target->EndDraw(); target->SetTarget(nullptr); return false; }
        ComPtr<ID2D1Brush> background = fill;
        if (options.backgroundGradient) {
            D2D1_GRADIENT_STOP stops[] = {{0, options.background}, {1, options.background2}};
            ComPtr<ID2D1GradientStopCollection> collection;
            ComPtr<ID2D1LinearGradientBrush> gradient;
            if (SUCCEEDED(target->CreateGradientStopCollection(stops, 2, collection.GetAddressOf()))) {
                float radians = options.gradientAngle * 3.14159265f / 180;
                auto center = D2D1::Point2F(bodyW / 2, (bodyH + tail) / 2);
                float dx = std::cos(radians) * bodyW / 2, dy = std::sin(radians) * (bodyH + tail) / 2;
                target->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(
                    D2D1::Point2F(center.x - dx, center.y - dy), D2D1::Point2F(center.x + dx, center.y + dy)),
                    collection.Get(), gradient.GetAddressOf());
                if (gradient) background = gradient;
            }
        }
        const float inset = options.borderWidth / 2 + 0.5f / scale;
        const float top = tailTop ? tail : 0;
        const auto body = D2D1::RectF(inset, top + inset, bodyW - inset, top + bodyH - inset);
        if (options.balloon) {
            // One closed path, so the stroke follows the tail without a seam across its base.
            float r = std::min({options.radius, (body.right - body.left) / 2, (body.bottom - body.top) / 2});
            float tx = std::clamp((attachment.x - origin.x) / scale, body.left + r + 9, std::max(body.left + r + 9, body.right - r - 9));
            ComPtr<ID2D1PathGeometry> geometry;
            ComPtr<ID2D1GeometrySink> sink;
            GraphicsPool.factory->CreatePathGeometry(geometry.GetAddressOf());
            if (geometry && SUCCEEDED(geometry->Open(sink.GetAddressOf()))) {
                sink->BeginFigure(D2D1::Point2F(body.left + r, body.top), D2D1_FIGURE_BEGIN_FILLED);
                if (tailTop) { sink->AddLine(D2D1::Point2F(tx - 8, body.top)); sink->AddLine(D2D1::Point2F(tx, inset)); sink->AddLine(D2D1::Point2F(tx + 8, body.top)); }
                sink->AddLine(D2D1::Point2F(body.right - r, body.top));
                sink->AddArc(D2D1::ArcSegment(D2D1::Point2F(body.right, body.top + r), D2D1::SizeF(r, r), 0, D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL));
                sink->AddLine(D2D1::Point2F(body.right, body.bottom - r));
                sink->AddArc(D2D1::ArcSegment(D2D1::Point2F(body.right - r, body.bottom), D2D1::SizeF(r, r), 0, D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL));
                if (!tailTop) { sink->AddLine(D2D1::Point2F(tx + 8, body.bottom)); sink->AddLine(D2D1::Point2F(tx, bodyH + tail - inset)); sink->AddLine(D2D1::Point2F(tx - 8, body.bottom)); }
                sink->AddLine(D2D1::Point2F(body.left + r, body.bottom));
                sink->AddArc(D2D1::ArcSegment(D2D1::Point2F(body.left, body.bottom - r), D2D1::SizeF(r, r), 0, D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL));
                sink->AddLine(D2D1::Point2F(body.left, body.top + r));
                sink->AddArc(D2D1::ArcSegment(D2D1::Point2F(body.left + r, body.top), D2D1::SizeF(r, r), 0, D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL));
                sink->EndFigure(D2D1_FIGURE_END_CLOSED); sink->Close();
                target->FillGeometry(geometry.Get(), background.Get());
                if (options.borderWidth > 0) DrawBorder(geometry.Get(), body, stroke.Get());
            }
        } else {
            target->FillRectangle(body, background.Get());
            if (options.borderWidth > 0) {
                ComPtr<ID2D1RectangleGeometry> rectangle;
                if (SUCCEEDED(GraphicsPool.factory->CreateRectangleGeometry(body, rectangle.GetAddressOf())))
                    DrawBorder(rectangle.Get(), body, stroke.Get());
            }
        }
        const float x = options.padding[0] + options.borderWidth;
        const float y = top + options.padding[1] + options.borderWidth;
        if (icon && GraphicsPool.wic) {
            ComPtr<IWICBitmap> iconWic; ComPtr<IWICFormatConverter> converted; ComPtr<ID2D1Bitmap> iconBitmap;
            if (SUCCEEDED(GraphicsPool.wic->CreateBitmapFromHICON(icon, iconWic.GetAddressOf())) &&
                SUCCEEDED(GraphicsPool.wic->CreateFormatConverter(converted.GetAddressOf())) &&
                SUCCEEDED(converted->Initialize(iconWic.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone,
                    nullptr, 0, WICBitmapPaletteTypeCustom)) &&
                SUCCEEDED(target->CreateBitmapFromWicBitmap(converted.Get(), iconBitmap.GetAddressOf())))
                target->DrawBitmap(iconBitmap.Get(), D2D1::RectF(x, y, x + iconW, y + iconH));
        }
        if (headerH > 0) {
            Gfx::Canvas::Draw(target.Get(), options.title, *titleFormat,
                D2D1::RectF(x + (icon ? iconW + 6 : 0), y, bodyW - options.padding[2] - (options.close ? 24 : 0), y + headerH), options.titleColor, true, true);
            if (options.close) {
                ComPtr<ID2D1SolidColorBrush> closeBrush; target->CreateSolidColorBrush(options.font, closeBrush.GetAddressOf());
                const float cx = bodyW - options.padding[2] - options.borderWidth - 10;
                if (closeBrush) {
                    target->DrawLine(D2D1::Point2F(cx - 4, y + 4), D2D1::Point2F(cx + 4, y + 12), closeBrush.Get(), 1.5f);
                    target->DrawLine(D2D1::Point2F(cx + 4, y + 4), D2D1::Point2F(cx - 4, y + 12), closeBrush.Get(), 1.5f);
                }
            }
        }
        auto rect = D2D1::RectF(x, y + headerH, bodyW - options.padding[2] - options.borderWidth,
            top + bodyH - options.padding[3] - options.borderWidth);
        if (options.effect == L"shadow" || options.effect == L"border") {
            const int offsets[][2] = {{1, 1}, {-1, 0}, {1, 0}, {0, -1}, {0, 1}};
            for (int i = options.effect == L"shadow" ? 0 : 1; i < (options.effect == L"shadow" ? 1 : 5); ++i) {
                auto r = rect; r.left += offsets[i][0]; r.right += offsets[i][0]; r.top += offsets[i][1]; r.bottom += offsets[i][1];
                Gfx::Canvas::Draw(target.Get(), text, *format, r, options.effectColor, false, true);
            }
        }
        Gfx::Canvas::Draw(target.Get(), text, *format, rect, options.font, true, true);
        auto hr = target->EndDraw(); target->SetTarget(nullptr);
        if (FAILED(hr)) return false;
        props.bitmapOptions = D2D1_BITMAP_OPTIONS_CPU_READ | D2D1_BITMAP_OPTIONS_CANNOT_DRAW;
        if ((!staging && FAILED(target->CreateBitmap(dimensions, nullptr, 0, &props, staging.GetAddressOf()))) ||
            FAILED(staging->CopyFromBitmap(nullptr, bitmap.Get(), nullptr))) return false;
        D2D1_MAPPED_RECT mapped{};
        if (FAILED(staging->Map(D2D1_MAP_OPTIONS_READ, &mapped))) return false;
        HDC screen = GetDC(nullptr), memory = CreateCompatibleDC(screen);
        BITMAPINFO info{}; info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = pixels.cx; info.bmiHeader.biHeight = -pixels.cy;
        info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32; info.bmiHeader.biCompression = BI_RGB;
        void* bits = nullptr;
        HBITMAP dib = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
        bool ok = false;
        if (dib && memory) {
            for (LONG row = 0; row < pixels.cy; ++row) memcpy(static_cast<BYTE*>(bits) + row * pixels.cx * 4,
                mapped.bits + row * mapped.pitch, pixels.cx * 4);
            auto old = SelectObject(memory, dib);
            POINT zero{}; BLENDFUNCTION blend{AC_SRC_OVER, 0, opacity, AC_SRC_ALPHA};
            ok = UpdateLayeredWindow(window, screen, &origin, &pixels, memory, &zero, 0, &blend, ULW_ALPHA) != FALSE;
            SelectObject(memory, old);
        }
        if (dib) DeleteObject(dib);
        if (memory) DeleteDC(memory);
        ReleaseDC(nullptr, screen); staging->Unmap();
        if (ok) SetWindowPos(window, options.topmost ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
        return ok;
    }
    void Show(bool fixed, POINT point) {
        Hide(); Load();
        if (!window || !options.enabled || !IsWindowVisible(skin)) return;
        fixedAnchor = fixed; anchor = point;
        requested = true;
        const auto now = GetTickCount64();
        const bool reshow = hiddenAt && now - hiddenAt < 1000;
        due = now + (reshow ? options.reshowDelay : options.initialDelay);
        lastLeft = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
        lastRight = (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
        if (!SetTimer(window, TimerId, 16, nullptr)) { Hide(); RmLog(rm, LOG_ERROR, L"ToolTip: timer creation failed"); return; }
        Tick();
    }
    void Display() {
        text = options.ResolveText(); ReadIcon();
        if (text.empty() && options.title.empty()) { Hide(); return; }
        LoadFonts();
        const bool ok = Paint(options.noFade ? 255 : 0);
        if (!ok) { Hide(); RmLog(rm, LOG_ERROR, L"ToolTip: display failed"); return; }
        visible = true; shownAt = GetTickCount64();
    }
    void Tick() {
        if (!requested) return;
        if (!IsWindow(skin) || !IsWindowVisible(skin) || IsIconic(skin)) { Hide(); return; }
        if (!options.always && GetAncestor(GetForegroundWindow(), GA_ROOT) != GetAncestor(skin, GA_ROOT)) { Hide(); return; }
        const bool left = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
        const bool right = (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
        if ((GetAsyncKeyState(VK_ESCAPE) & 0x8000) || (options.dismissClick && ((left && !lastLeft) || (right && !lastRight)))) { Hide(); return; }
        lastLeft = left; lastRight = right;
        const auto now = GetTickCount64();
        if (!visible && now >= due) Display();
        if (!visible) return;
        if (options.autoPopDelay && now - shownAt >= static_cast<ULONGLONG>(options.autoPopDelay)) { Hide(); return; }
        if (options.follow && !fixedAnchor) {
            POINT p{}; GetCursorPos(&p);
            if (p.x != anchor.x || p.y != anchor.y) {
                anchor = p;
                Paint();
            }
        }
        if (!options.noFade && now - shownAt < 160) Paint(static_cast<BYTE>(std::min<ULONGLONG>(255, (now - shownAt) * 255 / 144)));
    }
    void UpdateOpen() {
        if (!visible) return;
        const auto oldIcon = options.icon;
        Load();
        const bool iconChanged = oldIcon != options.icon || iconScale != scale ||
            iconW != static_cast<float>(std::clamp(options.Number(L"IconW", 24), 1.0, 512.0)) ||
            iconH != static_cast<float>(std::clamp(options.Number(L"IconH", 24), 1.0, 512.0));
        if (!options.enabled) { Hide(); return; }
        auto updated = options.ResolveText();
        if (updated.empty() && options.title.empty()) { Hide(); return; }
        text = std::move(updated);
        if (iconChanged) ReadIcon();
        if (!Paint()) { Hide(); RmLog(rm, LOG_ERROR, L"ToolTip: redraw failed"); }
    }
    bool CloseHit(LPARAM point) const {
        if (!options.close) return false;
        POINT p{static_cast<short>(LOWORD(point)), static_cast<short>(HIWORD(point))};
        ScreenToClient(window, &p);
        float x = p.x / scale, y = p.y / scale - (tailTop ? tail : 0);
        return x >= bodyW - options.padding[2] - options.borderWidth - 22 && y >= options.padding[1] && y <= options.padding[1] + 22;
    }
};
LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto m = reinterpret_cast<Measure*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        m = static_cast<Measure*>(reinterpret_cast<CREATESTRUCT*>(lp)->lpCreateParams);
        SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(m));
    }
    if (m) {
        if (msg == WM_TIMER && wp == TimerId) { m->Tick(); return 0; }
        if (msg == WM_MOUSEACTIVATE) return MA_NOACTIVATE;
        if (msg == WM_NCHITTEST) return m->CloseHit(lp) ? HTCLIENT : HTTRANSPARENT;
        if (msg == WM_LBUTTONUP) { m->Hide(); return 0; }
        if (msg == WM_ERASEBKGND) return 1;
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}
}
PLUGIN_EXPORT void Initialize(void** data, void* rm) { *data = new Measure(rm); }
PLUGIN_EXPORT void Reload(void* data, void* rm, double* maxValue) {
    auto m = static_cast<Measure*>(data); m->rm = rm;
    if (m->visible) m->UpdateOpen(); else m->Load();
    *maxValue = 1;
}
PLUGIN_EXPORT double Update(void* data) {
    auto m = static_cast<Measure*>(data); m->UpdateOpen();
    return m->visible ? 1.0 : 0.0;
}
PLUGIN_EXPORT LPCWSTR GetString(void* data) { return static_cast<Measure*>(data)->text.c_str(); }
PLUGIN_EXPORT void ExecuteBang(void* data, LPCWSTR args) {
    auto m = static_cast<Measure*>(data);
    std::wistringstream input(args ? args : L""); std::wstring command; input >> command;
    command = ToolTip::Lower(command);
    if (command == L"hide" || command == L"dismiss") m->Hide();
    else if (command == L"show" || command == L"toggle") {
        if (command == L"toggle" && m->requested) { m->Hide(); return; }
        POINT p{}; GetCursorPos(&p);
        input >> std::ws;
        bool fixed = !input.eof();
        if (fixed) {
            double x, y;
            if (!(input >> x >> y) || !std::isfinite(x) || !std::isfinite(y) || std::abs(x) > 100000 || std::abs(y) > 100000) {
                RmLog(m->rm, LOG_WARNING, L"ToolTip: Show expects zero arguments or two screen coordinates"); return;
            }
            p = {static_cast<LONG>(x), static_cast<LONG>(y)};
        }
        m->Show(fixed, p);
    } else if (command == L"update") m->UpdateOpen();
    else RmLog(m->rm, LOG_WARNING, L"ToolTip: unknown command (Show, Hide, Toggle, Update)");
}
PLUGIN_EXPORT void Finalize(void* data) { delete static_cast<Measure*>(data); }

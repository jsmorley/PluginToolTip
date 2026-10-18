// Copyright (c) 2026 Jeffrey Morley. Source code licensed under GNU GPL v2 (see LICENSE file).

#include "ToolTipMeasure.h"
#include <limits>

namespace {
constexpr float c_Pi = 3.14159265f;

struct Edge
{
	D2D1_POINT_2F a;
	D2D1_POINT_2F b;
	float nx;
	float ny;
	float anx = 0;
	float any = 0;
	float bnx = 0;
	float bny = 0;
};

// Flatten the actual silhouette, including the union with the balloon tail.
// No host window hooks or per-pixel geometry API calls are needed.
class OutlineSink final : public ID2D1SimplifiedGeometrySink
{
public:
	std::vector<Edge> edges;
	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** object) override
	{
		if (!object)
		{
			return E_POINTER;
		}
		*object = nullptr;
		if (id != __uuidof(IUnknown) && id != __uuidof(ID2D1SimplifiedGeometrySink))
		{
			return E_NOINTERFACE;
		}
		*object = static_cast<ID2D1SimplifiedGeometrySink*>(this);
		AddRef();
		return S_OK;
	}
	ULONG STDMETHODCALLTYPE AddRef() override { return ++m_References; }
	ULONG STDMETHODCALLTYPE Release() override
	{
		const ULONG remaining = --m_References;
		if (!remaining)
		{
			delete this;
		}
		return remaining;
	}
	void STDMETHODCALLTYPE SetFillMode(D2D1_FILL_MODE) override {}
	void STDMETHODCALLTYPE SetSegmentFlags(D2D1_PATH_SEGMENT) override {}
	void STDMETHODCALLTYPE BeginFigure(D2D1_POINT_2F start, D2D1_FIGURE_BEGIN) override
	{
		m_First = m_Last = start;
		m_Start = edges.size();
	}
	void STDMETHODCALLTYPE AddLines(const D2D1_POINT_2F* points, UINT count) override
	{
		for (UINT i = 0; i < count; ++i)
		{
			const float dx = points[i].x - m_Last.x, dy = points[i].y - m_Last.y;
			const float length = std::hypot(dx, dy);
			if (length > 0.00001f)
			{
				edges.push_back({m_Last, points[i], dy / length, -dx / length});
			}
			m_Last = points[i];
		}
	}
	void STDMETHODCALLTYPE AddBeziers(const D2D1_BEZIER_SEGMENT*, UINT) override
	{
		m_Failed = true;
	}
	void STDMETHODCALLTYPE EndFigure(D2D1_FIGURE_END end) override
	{
		if (end == D2D1_FIGURE_END_CLOSED)
		{
			AddLines(&m_First, 1);
		}
		float area = 0;
		for (size_t i = m_Start; i < edges.size(); ++i)
		{
			area += edges[i].a.x * edges[i].b.y - edges[i].b.x * edges[i].a.y;
		}
		if (area < 0)
		{
			for (size_t i = m_Start; i < edges.size(); ++i)
			{
				edges[i].nx = -edges[i].nx;
				edges[i].ny = -edges[i].ny;
			}
		}
		for (size_t i = m_Start; i < edges.size(); ++i)
		{
			auto& edge = edges[i];
			auto smooth = [&](const Edge& neighbor, float& nx, float& ny) {
				nx = edge.nx;
				ny = edge.ny;
				const float length = std::hypot(edge.b.x - edge.a.x, edge.b.y - edge.a.y);
				const float adjacent =
					std::hypot(neighbor.b.x - neighbor.a.x, neighbor.b.y - neighbor.a.y);
				// Blend short flattened curve segments, while retaining planar
				// faces and actual creases at rectangle corners and tail joins.
				if (edge.nx * neighbor.nx + edge.ny * neighbor.ny > 0.5f && length <= adjacent * 3)
				{
					nx += neighbor.nx;
					ny += neighbor.ny;
					const float magnitude = std::hypot(nx, ny);
					nx /= magnitude;
					ny /= magnitude;
				}
			};
			smooth(edges[i == m_Start ? edges.size() - 1 : i - 1], edge.anx, edge.any);
			smooth(edges[i + 1 == edges.size() ? m_Start : i + 1], edge.bnx, edge.bny);
		}
	}
	HRESULT STDMETHODCALLTYPE Close() override { return m_Failed ? E_FAIL : S_OK; }

private:
	ULONG m_References = 1;
	D2D1_POINT_2F m_First{};
	D2D1_POINT_2F m_Last{};
	size_t m_Start = 0;
	bool m_Failed = false;
};

float ProfileNormal(const std::wstring& profile, float t, int rings, float smoothness)
{
	// t runs from the inner edge to the outer edge of the shaded band.
	if (profile == L"bulged" || profile == L"ridge" || profile == L"groove")
	{
		t = std::min(t * rings, static_cast<float>(rings) - 0.00001f);
		t -= std::floor(t);
	}
	if (profile == L"curved")
	{
		return std::sin(t * c_Pi / 2);
	}
	if (profile == L"concave")
	{
		return -std::cos(t * c_Pi / 2);
	}
	if (profile == L"bulged")
	{
		return -std::cos(t * c_Pi);
	}
	if (profile == L"ridge" || profile == L"groove")
	{
		const float transition = std::max(0.005f, smoothness * 0.25f);
		const float slope = std::clamp((t - 0.5f) / transition, -1.0f, 1.0f);
		return profile == L"groove" ? -slope : slope;
	}
	return 1;
}

DWORD PackColor(D2D1_COLOR_F color)
{
	auto channel = [](float value) {
		return static_cast<DWORD>(std::lround(std::clamp(value, 0.0f, 1.0f) * 255));
	};
	return (channel(color.a) << 24) | (channel(color.r * color.a) << 16) |
		(channel(color.g * color.a) << 8) | channel(color.b * color.a);
}
}

void ToolTipMeasure::DrawBorder(ID2D1Geometry* outline, const D2D1_RECT_F& body, ID2D1Brush* flat)
{
	if (m_Options.strokeBevel == L"none" || m_Options.bevelWidth == 0 || m_Options.bevelRelief == 0)
	{
		m_BevelBitmap.Reset();
		m_BevelKey.clear();
		m_Target->DrawGeometry(outline, flat, m_Options.borderWidth);
		return;
	}
	Microsoft::WRL::ComPtr<OutlineSink> sink;
	sink.Attach(new OutlineSink());
	if (FAILED(outline->Simplify(
			D2D1_GEOMETRY_SIMPLIFICATION_OPTION_LINES, nullptr, 0.1f / m_Scale, sink.Get())) ||
		FAILED(sink->Close()) || sink->edges.empty())
	{
		m_Target->DrawGeometry(outline, flat, m_Options.borderWidth);
		return;
	}
	const auto& o = m_Options;
	std::vector<float> key = {m_Scale, static_cast<float>(m_PixelSize.cx),
		static_cast<float>(m_PixelSize.cy), o.borderWidth, o.bevelWidth, o.bevelRelief,
		o.bevelSmoothness, o.bevelAngle, o.bevelHighlight, o.bevelGloss, o.bevelShadow,
		static_cast<float>(o.bevelRings), o.strokeBevel == L"raised" ? 1.0f : -1.0f,
		o.bevelPlacement == L"inner"	   ? -1.0f
			: o.bevelPlacement == L"outer" ? 1.0f
										   : 0.0f};
	for (const auto* color : {&o.border, &o.bevelLight, &o.bevelDark})
	{
		key.insert(key.end(), {color->r, color->g, color->b, color->a});
	}
	for (const auto character : o.bevelProfile)
	{
		key.push_back(static_cast<float>(character));
	}
	key.push_back(-999);
	for (const auto& edge : sink->edges)
	{
		key.insert(key.end(), {edge.a.x, edge.a.y, edge.b.x, edge.b.y, edge.nx, edge.ny});
	}
	if (!m_BevelBitmap || key != m_BevelKey)
	{
		// Only the lighting map is reduced for exceptionally large tooltips.
		// Direct2D still rasterizes the actual outline at full target resolution.
		constexpr double pixelBudget = 1048576;
		const double pixels = static_cast<double>(m_PixelSize.cx) * m_PixelSize.cy;
		const float mapScale =
			m_Scale * static_cast<float>(std::min(1.0, std::sqrt(pixelBudget / pixels)));
		const UINT width =
			std::max(1U, static_cast<UINT>(std::ceil(m_PixelSize.cx / m_Scale * mapScale)));
		const UINT height =
			std::max(1U, static_cast<UINT>(std::ceil(m_PixelSize.cy / m_Scale * mapScale)));
		struct Nearest
		{
			float square = std::numeric_limits<float>::infinity();
			float dx = 0;
			float dy = 0;
			float sign = 1;
		};
		std::vector<Nearest> nearest(static_cast<size_t>(width) * height);
		const float extent = o.borderWidth / 2 + 1 / mapScale;
		if (!o.balloon)
		{
			// Extend each planar face to the outer corner. The largest signed
			// distance selects its face on both sides of the stroke centerline,
			// so neighboring bevel bands meet along a full-width 45-degree miter.
			for (UINT y = 0; y < height; ++y)
			{
				for (UINT x = 0; x < width; ++x)
				{
					const float px = (x + 0.5f) / mapScale;
					const float py = (y + 0.5f) / mapScale;
					const float distances[] = {
						body.left - px, py - body.bottom, px - body.right, body.top - py};
					const D2D1_POINT_2F normals[] = {{-1, 0}, {0, 1}, {1, 0}, {0, -1}};
					const auto face = std::max_element(std::begin(distances), std::end(distances));
					if (std::abs(*face) <= extent)
					{
						const auto normal = normals[face - distances];
						nearest[static_cast<size_t>(y) * width + x] = {
							*face * *face, normal.x, normal.y, *face < 0 ? -1.0f : 1.0f};
					}
				}
			}
		}
		else
		{
			for (const auto& edge : sink->edges)
			{
				const float ex = edge.b.x - edge.a.x, ey = edge.b.y - edge.a.y;
				const float lengthSquared = ex * ex + ey * ey;
				const int left =
					std::clamp(static_cast<int>(
								   std::floor((std::min(edge.a.x, edge.b.x) - extent) * mapScale)),
						0, static_cast<int>(width));
				const int right = std::clamp(
					static_cast<int>(std::ceil((std::max(edge.a.x, edge.b.x) + extent) * mapScale)),
					0, static_cast<int>(width));
				const int top = std::clamp(static_cast<int>(std::floor(
											   (std::min(edge.a.y, edge.b.y) - extent) * mapScale)),
					0, static_cast<int>(height));
				const int bottom = std::clamp(
					static_cast<int>(std::ceil((std::max(edge.a.y, edge.b.y) + extent) * mapScale)),
					0, static_cast<int>(height));
				for (int y = top; y < bottom; ++y)
				{
					for (int x = left; x < right; ++x)
					{
						const float px = (x + 0.5f) / mapScale - edge.a.x;
						const float py = (y + 0.5f) / mapScale - edge.a.y;
						const float t = std::clamp((px * ex + py * ey) / lengthSquared, 0.0f, 1.0f);
						const float dx = px - t * ex, dy = py - t * ey;
						const float square = dx * dx + dy * dy;
						auto& value = nearest[static_cast<size_t>(y) * width + x];
						// Shared vertices can be equally near two faces. Keep a stable
						// face instead of alternating due to floating-point rounding.
						const float tolerance = 0.00001f * std::max(1.0f, square);
						if (square < value.square - tolerance)
						{
							value = {square, edge.anx + (edge.bnx - edge.anx) * t,
								edge.any + (edge.bny - edge.any) * t,
								dx * edge.nx + dy * edge.ny < 0 ? -1.0f : 1.0f};
						}
					}
				}
			}
		}
		const float angle = o.bevelAngle * c_Pi / 180;
		const float lx = std::cos(angle), ly = std::sin(angle);
		// A 45-degree light elevation and frontal viewer define the specular half vector.
		const float hx = lx * 0.38268343f, hy = ly * 0.38268343f, hz = 0.92387953f;
		const float flatSpecular = std::pow(hz, o.bevelGloss);
		const float placement = o.bevelPlacement == L"inner" ? -1.0f
			: o.bevelPlacement == L"outer"					 ? 1.0f
															 : 0.0f;
		const float bandCenter = placement * (o.borderWidth - o.bevelWidth) / 2;
		const DWORD base = PackColor(o.border);
		std::vector<DWORD> colors(nearest.size(), base);
		for (size_t i = 0; i < nearest.size(); ++i)
		{
			const auto& value = nearest[i];
			if (!std::isfinite(value.square))
			{
				continue;
			}
			const float distance = std::sqrt(value.square);
			const float position = value.sign * distance - bandCenter;
			if (o.bevelWidth < o.borderWidth && std::abs(position) > o.bevelWidth / 2)
			{
				continue;
			}
			const float t = std::clamp(position / o.bevelWidth + 0.5f, 0.0f, 1.0f);
			float slope = ProfileNormal(o.bevelProfile, t, o.bevelRings, o.bevelSmoothness);
			if (o.strokeBevel == L"sunken")
			{
				slope = -slope;
			}
			const float factor = 1 / std::max(0.0001f, std::hypot(value.dx, value.dy));
			const float nx = value.dx * factor, ny = value.dy * factor;
			const float normalizer = 1 / std::sqrt(1 + slope * slope);
			const float directional = (nx * lx + ny * ly) * slope;
			const float sharpness = 1 + 2 * (1 - o.bevelSmoothness);
			float diffuse = std::tanh(directional * sharpness) / std::tanh(sharpness);
			if (diffuse < 0)
			{
				diffuse *= o.bevelShadow;
			}
			const float specular = std::max(0.0f,
				std::pow(
					std::max(0.0f, (slope * (nx * hx + ny * hy) + hz) * normalizer), o.bevelGloss) -
					flatSpecular);
			auto color = o.border;
			auto blend = [&](D2D1_COLOR_F target, float amount) {
				amount = std::clamp(amount, 0.0f, 1.0f);
				color.r += (target.r - color.r) * amount;
				color.g += (target.g - color.g) * amount;
				color.b += (target.b - color.b) * amount;
				color.a += (target.a - color.a) * amount;
			};
			blend(diffuse >= 0 ? o.bevelLight : o.bevelDark, std::abs(diffuse) * o.bevelRelief);
			blend(o.bevelLight, specular * o.bevelHighlight * o.bevelRelief);
			colors[i] = PackColor(color);
		}
		Microsoft::WRL::ComPtr<ID2D1Bitmap1> bitmap;
		const auto properties = D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_NONE,
			D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
			96 * mapScale, 96 * mapScale);
		if (FAILED(m_Target->CreateBitmap(D2D1::SizeU(width, height), colors.data(), width * 4,
				&properties, bitmap.GetAddressOf())))
		{
			m_Target->DrawGeometry(outline, flat, o.borderWidth);
			return;
		}
		m_BevelBitmap = std::move(bitmap);
		m_BevelKey = std::move(key);
	}
	Microsoft::WRL::ComPtr<ID2D1BitmapBrush> brush;
	if (SUCCEEDED(m_Target->CreateBitmapBrush(m_BevelBitmap.Get(),
			D2D1::BitmapBrushProperties(D2D1_EXTEND_MODE_CLAMP, D2D1_EXTEND_MODE_CLAMP),
			brush.GetAddressOf())))
	{
		m_Target->DrawGeometry(outline, brush.Get(), o.borderWidth);
	}
	else
	{
		m_Target->DrawGeometry(outline, flat, o.borderWidth);
	}
}

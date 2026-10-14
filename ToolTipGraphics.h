// Copyright (c) 2026 Jeffrey Morley. Source code licensed under GNU GPL v2 (see LICENSE file).

#pragma once

#include "vendor/StdAfx.h"
#include <d3d11.h>
#include <wincodec.h>

namespace ToolTip {

// One device domain for all measures on Rainmeter's UI thread.
struct GraphicsResources
{
	Microsoft::WRL::ComPtr<ID3D11Device> d3d;
	Microsoft::WRL::ComPtr<ID2D1Factory1> factory;
	Microsoft::WRL::ComPtr<ID2D1Device> device;
	Microsoft::WRL::ComPtr<IWICImagingFactory> wic;

	void Reset();
};

GraphicsResources& GetGraphicsResources();

} // namespace ToolTip

// Copyright (c) 2026 Jeffrey Morley. Source code licensed under GNU GPL v2 (see LICENSE file).

#include <dxgi.h>
#include "ToolTipMeasure.h"
#include "ToolTipGraphics.h"

using Microsoft::WRL::ComPtr;

namespace ToolTip {

GraphicsResources& GetGraphicsResources()
{
	static GraphicsResources resources;
	return resources;
}

void GraphicsResources::Reset()
{
	wic.Reset();
	device.Reset();
	factory.Reset();
	d3d.Reset();
}

} // namespace ToolTip

bool ToolTipMeasure::InitializeGraphics()
{
	if (m_Target)
	{
		return true;
	}
	if (ToolTip::GetGraphicsResources().device)
	{
		return SUCCEEDED(ToolTip::GetGraphicsResources().device->CreateDeviceContext(
			D2D1_DEVICE_CONTEXT_OPTIONS_NONE, m_Target.GetAddressOf()));
	}
	ToolTip::GraphicsResources resources;
	UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
	auto hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, nullptr, 0,
		D3D11_SDK_VERSION, resources.d3d.GetAddressOf(), nullptr, nullptr);
	if (FAILED(hr))
	{
		hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags, nullptr, 0,
			D3D11_SDK_VERSION, resources.d3d.GetAddressOf(), nullptr, nullptr);
	}
	ComPtr<IDXGIDevice> dxgi;
	if (FAILED(hr) || FAILED(resources.d3d.As(&dxgi)))
	{
		return false;
	}
	if (FAILED(
			D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, resources.factory.GetAddressOf())))
	{
		return false;
	}
	if (FAILED(resources.factory->CreateDevice(dxgi.Get(), resources.device.GetAddressOf())))
	{
		return false;
	}
	if (FAILED(resources.device->CreateDeviceContext(
			D2D1_DEVICE_CONTEXT_OPTIONS_NONE, m_Target.GetAddressOf())))
	{
		return false;
	}
	CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
		IID_PPV_ARGS(resources.wic.GetAddressOf()));
	ToolTip::GetGraphicsResources() = std::move(resources);
	return true;
}

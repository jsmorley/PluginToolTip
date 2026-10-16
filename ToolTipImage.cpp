// Copyright (c) 2026 Jeffrey Morley. Source code licensed under GNU GPL v2 (see LICENSE file).

#include <filesystem>
#include "ToolTipMeasure.h"
#include "ToolTipGraphics.h"

using Microsoft::WRL::ComPtr;

void ToolTipMeasure::ReadBackgroundImage()
{
	if (m_LoadedBackgroundImage && *m_LoadedBackgroundImage == m_Options.image)
	{
		return;
	}
	m_LoadedBackgroundImage = m_Options.image;
	m_BackgroundImage.Reset();
	m_BackgroundZoom = 1;
	if (!m_Options.image.path.empty())
	{
		m_BackgroundImage = LoadBackgroundImage();
		if (!m_BackgroundImage)
		{
			RmLog(m_Rm, LOG_WARNING,
				L"ToolTip: could not load BackgroundImage; using SolidColor / gradient");
		}
	}
}

ComPtr<ID2D1Bitmap1> ToolTipMeasure::LoadBackgroundImage()
{
	auto* wic = ToolTip::GetGraphicsResources().wic.Get();
	if (!wic)
	{
		return {};
	}
	std::filesystem::path path(m_Options.image.path);
	if (!path.has_extension())
	{
		path += L".png";
	}
	ComPtr<IWICBitmapDecoder> decoder;
	ComPtr<IWICBitmapFrameDecode> frame;
	ComPtr<IWICFormatConverter> converter;
	UINT width = 0, height = 0;
	// Bound decoded and processed surfaces to 64 MiB each. Frames are decoded
	// only when image options change, never while merely following the mouse.
	constexpr UINT64 maxPixels = 16 * 1024 * 1024;
	if (FAILED(wic->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ,
			WICDecodeMetadataCacheOnLoad, decoder.GetAddressOf())) ||
		FAILED(decoder->GetFrame(0, frame.GetAddressOf())) ||
		FAILED(frame->GetSize(&width, &height)) || !width || !height ||
		static_cast<UINT64>(width) * height > maxPixels ||
		width > m_Target->GetMaximumBitmapSize() || height > m_Target->GetMaximumBitmapSize() ||
		FAILED(wic->CreateFormatConverter(converter.GetAddressOf())) ||
		FAILED(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppRGBA,
			WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom)))
	{
		return {};
	}
	std::vector<BYTE> pixels(static_cast<size_t>(width) * height * 4);
	if (FAILED(converter->CopyPixels(
			nullptr, width * 4, static_cast<UINT>(pixels.size()), pixels.data())))
	{
		return {};
	}
	const auto& image = m_Options.image;
	for (size_t i = 0; i < pixels.size(); i += 4)
	{
		float r = pixels[i], g = pixels[i + 1], b = pixels[i + 2];
		if (image.greyscale)
		{
			r = g = b = 0.299f * r + 0.587f * g + 0.114f * b;
		}
		const float alpha = pixels[i + 3] / 255.0f * image.tint[3];
		pixels[i] = static_cast<BYTE>(std::lround(b * image.tint[2] * alpha));
		pixels[i + 1] = static_cast<BYTE>(std::lround(g * image.tint[1] * alpha));
		pixels[i + 2] = static_cast<BYTE>(std::lround(r * image.tint[0] * alpha));
		pixels[i + 3] = static_cast<BYTE>(std::lround(alpha * 255));
	}
	auto properties = D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_NONE,
		D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96, 96);
	ComPtr<ID2D1Bitmap1> source, processed;
	if (FAILED(m_Target->CreateBitmap(D2D1::SizeU(width, height), pixels.data(), width * 4,
			&properties, source.GetAddressOf())))
	{
		return {};
	}
	float x = 0, y = 0, cropWidth = static_cast<float>(width),
		  cropHeight = static_cast<float>(height);
	if (image.crop)
	{
		const auto& crop = *image.crop;
		x = crop.x;
		y = crop.y;
		cropWidth = crop.width;
		cropHeight = crop.height;
		if (crop.origin == 2 || crop.origin == 3)
		{
			x += width;
		}
		if (crop.origin == 3 || crop.origin == 4)
		{
			y += height;
		}
		if (crop.origin == 5)
		{
			x += width / 2.0f;
			y += height / 2.0f;
		}
	}
	const double radians = image.rotate * 3.14159265358979323846 / 180;
	const double c = std::abs(std::cos(radians)), s = std::abs(std::sin(radians));
	auto dimension = [](double value) {
		return std::abs(value - std::round(value)) < 0.0001 ? std::round(value) : std::ceil(value);
	};
	const double rotatedWidth = dimension(cropWidth * c + cropHeight * s);
	const double rotatedHeight = dimension(cropWidth * s + cropHeight * c);
	const auto maximum = m_Target->GetMaximumBitmapSize();
	if (rotatedWidth <= 0 || rotatedHeight <= 0 || rotatedWidth > maximum ||
		rotatedHeight > maximum || rotatedWidth * rotatedHeight > maxPixels)
	{
		return {};
	}
	if (c > 0.00001 && s > 0.00001)
	{
		// Rotate first, then center-crop the expanded bitmap when fitting the
		// background. Inverse-rotate its corners to find the minimum zoom that
		// keeps them inside the original image. Leave an interpolation guard
		// at each edge for both the rotation and final bitmap-brush sampling.
		const double safeWidth = std::max(cropWidth * 0.9, cropWidth - 4.0);
		const double safeHeight = std::max(cropHeight * 0.9, cropHeight - 4.0);
		m_BackgroundZoom =
			static_cast<float>(std::max({1.0, (c * rotatedWidth + s * rotatedHeight) / safeWidth,
				(s * rotatedWidth + c * rotatedHeight) / safeHeight}));
	}
	properties.bitmapOptions = D2D1_BITMAP_OPTIONS_TARGET;
	if (FAILED(m_Target->CreateBitmap(
			D2D1::SizeU(static_cast<UINT>(rotatedWidth), static_cast<UINT>(rotatedHeight)), nullptr,
			0, &properties, processed.GetAddressOf())))
	{
		return {};
	}
	m_Target->SetTarget(processed.Get());
	m_Target->SetDpi(96, 96);
	m_Target->BeginDraw();
	m_Target->Clear(D2D1::ColorF(0, 0));
	const auto center = D2D1::Point2F(cropWidth / 2, cropHeight / 2);
	m_Target->SetTransform(D2D1::Matrix3x2F::Scale(image.flip & 1 ? -1.0f : 1.0f,
							   image.flip & 2 ? -1.0f : 1.0f, center) *
		D2D1::Matrix3x2F::Rotation(image.rotate, center) *
		D2D1::Matrix3x2F::Translation(static_cast<float>(rotatedWidth) / 2 - center.x,
			static_cast<float>(rotatedHeight) / 2 - center.y));
	const auto crop = D2D1::RectF(std::max(0.0f, x), std::max(0.0f, y),
		std::min(static_cast<float>(width), x + cropWidth),
		std::min(static_cast<float>(height), y + cropHeight));
	if (crop.right > crop.left && crop.bottom > crop.top)
	{
		m_Target->DrawBitmap(source.Get(),
			D2D1::RectF(crop.left - x, crop.top - y, crop.right - x, crop.bottom - y), 1,
			D2D1_INTERPOLATION_MODE_LINEAR, crop);
	}
	const auto result = m_Target->EndDraw();
	m_Target->SetTransform(D2D1::Matrix3x2F::Identity());
	m_Target->SetTarget(nullptr);
	return SUCCEEDED(result) ? processed : ComPtr<ID2D1Bitmap1>{};
}

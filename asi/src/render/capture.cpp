#include "capture.hpp"

#include "../game/sdk.hpp"

#include <windows.h>
#include <d3d9.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_WRITE_STATIC
#define STBI_WRITE_NO_STDIO
#pragma warning(push, 0)
#include <stb_image_write.h>
#pragma warning(pop)

namespace capture
{

namespace
{

constexpr std::uint32_t A2R10G10B10ToRgb(std::uint32_t pixel)
{
	// D3DFMT_A2R10G10B10: A[31:30], R[29:20], G[19:10], B[9:0].
	// Reduce normalized channels to 8 bits with nearest rounding; no gamma change.
	const auto r = (((pixel >> 20) & 0x3FFu) * 255u + 511u) / 1023u;
	const auto g = (((pixel >> 10) & 0x3FFu) * 255u + 511u) / 1023u;
	const auto b = ((pixel & 0x3FFu) * 255u + 511u) / 1023u;
	return (r << 16) | (g << 8) | b;
}

// Guard the channel order and alpha independence of the packed D3D format.
static_assert(A2R10G10B10ToRgb(0x00000000u) == 0x000000u);
static_assert(A2R10G10B10ToRgb(0xFFFFFFFFu) == 0xFFFFFFu);
static_assert(A2R10G10B10ToRgb(0x3FF00000u) == 0xFF0000u);
static_assert(A2R10G10B10ToRgb(0x000FFC00u) == 0x00FF00u);
static_assert(A2R10G10B10ToRgb(0x000003FFu) == 0x0000FFu);
static_assert(A2R10G10B10ToRgb(0xC0000000u) == 0x000000u);
static_assert(A2R10G10B10ToRgb((512u << 20) | (256u << 10) | 1023u) == 0x8040FFu);

IDirect3DDevice9* Device()
{
	return game::At<IDirect3DDevice9*>(0xC97C28); // RenderWare's Direct3D device
}

template <typename T>
struct Releaser
{
	T* object = nullptr;
	~Releaser()
	{
		if (object)
			object->Release();
	}
};

void Append(void* context, void* data, int size)
{
	auto* out = static_cast<std::vector<std::uint8_t>*>(context);
	const auto* bytes = static_cast<const std::uint8_t*>(data);
	out->insert(out->end(), bytes, bytes + size);
}

}

bool Grab(Image& out, std::string& error)
{
	IDirect3DDevice9* device = Device();
	if (!device)
	{
		error = "no Direct3D device";
		return false;
	}

	Releaser<IDirect3DSurface9> backBuffer, resolved, copy;
	if (FAILED(device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &backBuffer.object)))
	{
		error = "GetBackBuffer failed";
		return false;
	}
	D3DSURFACE_DESC desc {};
	backBuffer.object->GetDesc(&desc);

	// GetRenderTargetData refuses multisampled surfaces: resolve those first.
	IDirect3DSurface9* source = backBuffer.object;
	if (desc.MultiSampleType != D3DMULTISAMPLE_NONE)
	{
		if (FAILED(device->CreateRenderTarget(desc.Width, desc.Height, desc.Format, D3DMULTISAMPLE_NONE, 0, FALSE, &resolved.object, nullptr))
			|| FAILED(device->StretchRect(backBuffer.object, nullptr, resolved.object, nullptr, D3DTEXF_NONE)))
		{
			error = "could not resolve the multisampled back buffer";
			return false;
		}
		source = resolved.object;
	}

	if (FAILED(device->CreateOffscreenPlainSurface(desc.Width, desc.Height, desc.Format, D3DPOOL_SYSTEMMEM, &copy.object, nullptr))
		|| FAILED(device->GetRenderTargetData(source, copy.object)))
	{
		error = "GetRenderTargetData failed";
		return false;
	}

	D3DLOCKED_RECT locked {};
	if (FAILED(copy.object->LockRect(&locked, nullptr, D3DLOCK_READONLY)))
	{
		error = "could not lock the screenshot surface";
		return false;
	}

	out.width = static_cast<int>(desc.Width);
	out.height = static_cast<int>(desc.Height);
	out.rgb.resize(static_cast<std::size_t>(out.width) * out.height * 3);
	bool supported = true;
	for (int y = 0; y < out.height && supported; ++y)
	{
		const auto* row = static_cast<const std::uint8_t*>(locked.pBits) + static_cast<std::size_t>(y) * locked.Pitch;
		std::uint8_t* to = out.rgb.data() + static_cast<std::size_t>(y) * out.width * 3;
		switch (desc.Format)
		{
		case D3DFMT_X8R8G8B8:
		case D3DFMT_A8R8G8B8:
			for (int x = 0; x < out.width; ++x, row += 4, to += 3)
			{
				to[0] = row[2];
				to[1] = row[1];
				to[2] = row[0];
			}
			break;
		case D3DFMT_A2R10G10B10:
			for (int x = 0; x < out.width; ++x, row += 4, to += 3)
			{
				const std::uint32_t pixel = static_cast<std::uint32_t>(row[0])
					| (static_cast<std::uint32_t>(row[1]) << 8)
					| (static_cast<std::uint32_t>(row[2]) << 16)
					| (static_cast<std::uint32_t>(row[3]) << 24);
				const auto rgb = A2R10G10B10ToRgb(pixel);
				to[0] = static_cast<std::uint8_t>(rgb >> 16);
				to[1] = static_cast<std::uint8_t>(rgb >> 8);
				to[2] = static_cast<std::uint8_t>(rgb);
			}
			break;
		case D3DFMT_R5G6B5:
			for (int x = 0; x < out.width; ++x, row += 2, to += 3)
			{
				const unsigned pixel = row[0] | (row[1] << 8);
				to[0] = static_cast<std::uint8_t>(((pixel >> 11) & 0x1F) * 255 / 31);
				to[1] = static_cast<std::uint8_t>(((pixel >> 5) & 0x3F) * 255 / 63);
				to[2] = static_cast<std::uint8_t>((pixel & 0x1F) * 255 / 31);
			}
			break;
		default:
			supported = false;
		}
	}
	copy.object->UnlockRect();

	if (!supported)
	{
		error = "unsupported back buffer format " + std::to_string(static_cast<int>(desc.Format));
		return false;
	}
	return true;
}

void Downscale(Image& image, int maxWidth)
{
	if (maxWidth <= 0 || image.width <= maxWidth)
		return;

	const int width = maxWidth;
	const int height = image.height * maxWidth / image.width > 0 ? image.height * maxWidth / image.width : 1;
	std::vector<std::uint8_t> scaled(static_cast<std::size_t>(width) * height * 3);

	// Box filter: each output pixel averages the source pixels it covers.
	for (int y = 0; y < height; ++y)
	{
		const int y0 = y * image.height / height;
		const int y1 = (y + 1) * image.height / height > y0 ? (y + 1) * image.height / height : y0 + 1;
		for (int x = 0; x < width; ++x)
		{
			const int x0 = x * image.width / width;
			const int x1 = (x + 1) * image.width / width > x0 ? (x + 1) * image.width / width : x0 + 1;
			unsigned sum[3] = { 0, 0, 0 };
			for (int sy = y0; sy < y1; ++sy)
			{
				const std::uint8_t* from = image.rgb.data() + (static_cast<std::size_t>(sy) * image.width + x0) * 3;
				for (int sx = x0; sx < x1; ++sx, from += 3)
				{
					sum[0] += from[0];
					sum[1] += from[1];
					sum[2] += from[2];
				}
			}
			const unsigned count = static_cast<unsigned>((y1 - y0) * (x1 - x0));
			std::uint8_t* to = scaled.data() + (static_cast<std::size_t>(y) * width + x) * 3;
			to[0] = static_cast<std::uint8_t>(sum[0] / count);
			to[1] = static_cast<std::uint8_t>(sum[1] / count);
			to[2] = static_cast<std::uint8_t>(sum[2] / count);
		}
	}
	image.width = width;
	image.height = height;
	image.rgb = std::move(scaled);
}

std::vector<std::uint8_t> Encode(const Image& image, const std::string& format, int jpegQuality)
{
	std::vector<std::uint8_t> out;
	if (image.rgb.empty())
		return out;
	if (format == "png")
		stbi_write_png_to_func(Append, &out, image.width, image.height, 3, image.rgb.data(), image.width * 3);
	else
		stbi_write_jpg_to_func(Append, &out, image.width, image.height, 3, image.rgb.data(), jpegQuality);
	return out;
}

std::string Base64(const std::vector<std::uint8_t>& data)
{
	static const char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	std::string out;
	out.reserve((data.size() + 2) / 3 * 4);
	for (std::size_t i = 0; i < data.size(); i += 3)
	{
		const unsigned a = data[i];
		const unsigned b = i + 1 < data.size() ? data[i + 1] : 0;
		const unsigned c = i + 2 < data.size() ? data[i + 2] : 0;
		out += kAlphabet[a >> 2];
		out += kAlphabet[((a & 0x3) << 4) | (b >> 4)];
		out += i + 1 < data.size() ? kAlphabet[((b & 0xF) << 2) | (c >> 6)] : '=';
		out += i + 2 < data.size() ? kAlphabet[c & 0x3F] : '=';
	}
	return out;
}

}

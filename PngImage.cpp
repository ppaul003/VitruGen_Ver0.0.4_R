#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <wincodec.h>

#include "PngImage.h"

#include <algorithm>
#include <sstream>

namespace vitru {
namespace {

template <typename T>
class ComPointer {
public:
	~ComPointer() { reset(); }
	T** address() { reset(); return &m_value; }
	T* get() const { return m_value; }
	T* operator->() const { return m_value; }
	void reset() { if (m_value) { m_value->Release(); m_value = nullptr; } }
private:
	T* m_value = nullptr;
};

class ComApartment {
public:
	ComApartment() : m_result(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)) {}
	~ComApartment() { if (m_result == S_OK || m_result == S_FALSE) CoUninitialize(); }
	bool available() const { return SUCCEEDED(m_result) || m_result == RPC_E_CHANGED_MODE; }
private:
	HRESULT m_result;
};

std::string hrText(const char* operation, HRESULT result) {
	std::ostringstream text; text << operation << " failed (HRESULT 0x" << std::hex << static_cast<unsigned long>(result) << ")."; return text.str();
}
void setError(std::string* error, const std::string& value) { if (error) *error = value; }

bool createFactory(ComPointer<IWICImagingFactory>& factory, std::string* error) {
	const HRESULT result = CoCreateInstance(CLSID_WICImagingFactory, nullptr,
		CLSCTX_INPROC_SERVER, IID_PPV_ARGS(factory.address()));
	if (FAILED(result)) { setError(error, hrText("WIC factory creation", result)); return false; }
	return true;
}

} // namespace

ImageRGBA8 makeSolidImage(
	std::uint32_t width, std::uint32_t height,
	std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a) {
	ImageRGBA8 image; image.width = width; image.height = height;
	image.pixels.resize(static_cast<std::size_t>(width) * height * 4u);
	for (std::size_t i = 0; i < image.pixels.size(); i += 4u) {
		image.pixels[i] = r; image.pixels[i + 1u] = g; image.pixels[i + 2u] = b; image.pixels[i + 3u] = a;
	}
	return image;
}

bool loadPngImage(const std::filesystem::path& path, ImageRGBA8& output,
	std::string* error, bool flipVerticallyForOpenGL) {
	output = ImageRGBA8{};
	ComApartment apartment; if (!apartment.available()) { setError(error, "COM initialization failed for PNG loading."); return false; }
	ComPointer<IWICImagingFactory> factory; if (!createFactory(factory, error)) return false;
	ComPointer<IWICBitmapDecoder> decoder;
	HRESULT result = factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ,
		WICDecodeMetadataCacheOnLoad, decoder.address());
	if (FAILED(result)) { setError(error, hrText("PNG decoder creation", result) + " " + path.string()); return false; }
	ComPointer<IWICBitmapFrameDecode> frame;
	result = decoder->GetFrame(0u, frame.address());
	if (FAILED(result)) { setError(error, hrText("PNG frame read", result)); return false; }
	ComPointer<IWICFormatConverter> converter;
	result = factory->CreateFormatConverter(converter.address());
	if (FAILED(result)) { setError(error, hrText("PNG converter creation", result)); return false; }
	result = converter->Initialize(frame.get(), GUID_WICPixelFormat32bppRGBA,
		WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom);
	if (FAILED(result)) { setError(error, hrText("PNG RGBA conversion", result)); return false; }
	UINT width = 0, height = 0;
	result = converter->GetSize(&width, &height);
	if (FAILED(result) || width == 0u || height == 0u) { setError(error, "PNG has invalid dimensions."); return false; }
	const std::uint64_t bytes = static_cast<std::uint64_t>(width) * height * 4u;
	if (bytes > static_cast<std::uint64_t>((std::numeric_limits<UINT>::max)())) { setError(error, "PNG is too large for WIC buffer limits."); return false; }
	output.width = width; output.height = height; output.pixels.resize(static_cast<std::size_t>(bytes));
	result = converter->CopyPixels(nullptr, width * 4u, static_cast<UINT>(bytes), output.pixels.data());
	if (FAILED(result)) { output = ImageRGBA8{}; setError(error, hrText("PNG pixel copy", result)); return false; }
	if (flipVerticallyForOpenGL) {
		const std::size_t stride = static_cast<std::size_t>(width) * 4u;
		for (std::uint32_t y = 0; y < height / 2u; ++y) {
			auto top = output.pixels.begin() + static_cast<std::ptrdiff_t>(y * stride);
			auto bottom = output.pixels.begin() + static_cast<std::ptrdiff_t>((height - 1u - y) * stride);
			for (std::size_t x = 0; x < stride; ++x) std::swap(top[x], bottom[x]);
		}
	}
	return true;
}

bool writePngImage(const std::filesystem::path& path, const ImageRGBA8& image, std::string* error) {
	if (!image.valid()) { setError(error, "PNG writer received invalid RGBA image data."); return false; }
	ComApartment apartment; if (!apartment.available()) { setError(error, "COM initialization failed for PNG writing."); return false; }
	ComPointer<IWICImagingFactory> factory; if (!createFactory(factory, error)) return false;
	ComPointer<IWICStream> stream; HRESULT result = factory->CreateStream(stream.address());
	if (FAILED(result)) { setError(error, hrText("PNG stream creation", result)); return false; }
	result = stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE);
	if (FAILED(result)) { setError(error, hrText("PNG file open", result) + " " + path.string()); return false; }
	ComPointer<IWICBitmapEncoder> encoder;
	result = factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, encoder.address());
	if (FAILED(result) || FAILED(result = encoder->Initialize(stream.get(), WICBitmapEncoderNoCache))) { setError(error, hrText("PNG encoder initialization", result)); return false; }
	ComPointer<IWICBitmapFrameEncode> frame; ComPointer<IPropertyBag2> properties;
	result = encoder->CreateNewFrame(frame.address(), properties.address());
	if (FAILED(result) || FAILED(result = frame->Initialize(properties.get())) ||
		FAILED(result = frame->SetSize(image.width, image.height))) { setError(error, hrText("PNG frame initialization", result)); return false; }
	WICPixelFormatGUID format = GUID_WICPixelFormat32bppRGBA;
	result = frame->SetPixelFormat(&format);
	if (FAILED(result)) { setError(error, hrText("PNG pixel-format selection", result)); return false; }
	const bool rgba = InlineIsEqualGUID(format, GUID_WICPixelFormat32bppRGBA) != 0;
	const bool bgra = InlineIsEqualGUID(format, GUID_WICPixelFormat32bppBGRA) != 0;
	if (!rgba && !bgra) { setError(error, "PNG encoder does not accept a supported 32-bit RGBA/BGRA format."); return false; }
	std::vector<std::uint8_t> converted;
	const std::uint8_t* pixels = image.pixels.data();
	if (bgra) {
		converted = image.pixels;
		for (std::size_t i = 0; i < converted.size(); i += 4u) std::swap(converted[i], converted[i + 2u]);
		pixels = converted.data();
	}
	const UINT stride = image.width * 4u;
	result = frame->WritePixels(image.height, stride, static_cast<UINT>(image.pixels.size()), const_cast<BYTE*>(pixels));
	if (FAILED(result) || FAILED(result = frame->Commit()) || FAILED(result = encoder->Commit())) { setError(error, hrText("PNG write", result)); return false; }
	return true;
}

} // namespace vitru
